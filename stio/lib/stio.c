/*
 * $Id$
 *
    Copyright (c) 2015-2016 Chung, Hyung-Hwan. All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions
    are met:
    1. Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.

    THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR
    IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WAfRRANTIES
    OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
    IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
    INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
    NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
    THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#include "stio-prv.h"

#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>




stio_t* stio_open (stio_mmgr_t* mmgr, stio_size_t xtnsize, stio_size_t tmrcapa, stio_errnum_t* errnum)
{
	stio_t* stio;

	stio = STIO_MMGR_ALLOC (mmgr, STIO_SIZEOF(stio_t) + xtnsize);
	if (stio)
	{
		if (stio_init (stio, mmgr, tmrcapa) <= -1)
		{
			if (errnum) *errnum = stio->errnum;
			STIO_MMGR_FREE (mmgr, stio);
			stio = STIO_NULL;
		}
		else STIO_MEMSET (stio + 1, 0, xtnsize);
	}
	else
	{
		if (errnum) *errnum = STIO_ENOMEM;
	}

	return stio;
}

void stio_close (stio_t* stio)
{
	stio_fini (stio);
	STIO_MMGR_FREE (stio->mmgr, stio);
}

int stio_init (stio_t* stio, stio_mmgr_t* mmgr, stio_size_t tmrcapa)
{
	STIO_MEMSET (stio, 0, STIO_SIZEOF(*stio));
	stio->mmgr = mmgr;

	/* intialize the multiplexer object */
	stio->mux = epoll_create (1000);
	if (stio->mux == -1)
	{
/* TODO: set error number */
		//if (stio->errnum) *errnum = XXXXX
		return -1;
	}

	/* initialize the timer object */
	if (tmrcapa <= 0) tmrcapa = 1;
	stio->tmr.jobs = STIO_MMGR_ALLOC (stio->mmgr, tmrcapa * STIO_SIZEOF(stio_tmrjob_t));
	if (!stio->tmr.jobs) 
	{
		stio->errnum = STIO_ENOMEM;
		close (stio->mux);
		return -1;
	}
	stio->tmr.capa = tmrcapa;

	return 0;
}

void stio_fini (stio_t* stio)
{
	/* kill all registered devices */
	while (stio->dev.tail)
	{
		stio_killdev (stio, stio->dev.tail);
	}

	/* purge scheduled timer jobs and kill the timer */
	stio_cleartmrjobs (stio);
	STIO_MMGR_FREE (stio->mmgr, stio->tmr.jobs);

	/* close the multiplexer */
	close (stio->mux);
}

stio_dev_t* stio_makedev (stio_t* stio, stio_size_t dev_size, stio_dev_mth_t* dev_mth, stio_dev_evcb_t* dev_evcb, void* make_ctx)
{
	stio_dev_t* dev;

	if (dev_size < STIO_SIZEOF(stio_dev_t)) 
	{
		stio->errnum = STIO_EINVAL;
		return STIO_NULL;
	}

	dev = STIO_MMGR_ALLOC (stio->mmgr, dev_size);
	if (!dev)
	{
		stio->errnum = STIO_ENOMEM;
		return STIO_NULL;
	}

	STIO_MEMSET (dev, 0, dev_size);
	dev->stio = stio;
	dev->mth = dev_mth;
	dev->evcb = dev_evcb;
	STIO_WQ_INIT(&dev->wq);

	/* call the callback function first */
	stio->errnum = STIO_ENOERR;
	if (dev->mth->make (dev, make_ctx) <= -1)
	{
		if (stio->errnum == STIO_ENOERR) stio->errnum = STIO_EDEVMAKE;
		goto oops;
	}

	/* ------------------------------------ */
	{
	#if defined(_WIN32)
		if (CreateIoCompletionPort ((HANDLE)dev->mth->getsyshnd(dev), stio->iocp, STIO_IOCP_KEY, 0) == NULL)
		{
			/* TODO: set errnum from GetLastError()... */
			goto oops_after_make;
		}
		
	#else
		if (stio_dev_event (dev, STIO_DEV_EVENT_ADD, STIO_DEV_EVENT_IN) <= -1) goto oops_after_make;
	#endif
	}
	/* ------------------------------------ */

	/* and place the new dev at the back */
	if (stio->dev.tail) stio->dev.tail->next = dev;
	else stio->dev.head = dev;
	dev->prev = stio->dev.tail;
	stio->dev.tail = dev;

	return dev;

oops_after_make:
	dev->mth->kill (dev);
oops:
	STIO_MMGR_FREE (stio->mmgr, dev);
	return STIO_NULL;
}

void stio_killdev (stio_t* stio, stio_dev_t* dev)
{
	STIO_ASSERT (stio == dev->stio);

	/* clear pending send requests */
	while (!STIO_WQ_ISEMPTY(&dev->wq))
	{
		stio_wq_t* wq;

		wq = STIO_WQ_HEAD(&dev->wq);
printf ("DELETING UNSENT REQUETS...%p\n", wq);
		STIO_WQ_DEQ (&dev->wq);

		STIO_MMGR_FREE (stio->mmgr, wq);
	}

	/* delink the dev object */
	if (dev->prev)
		dev->prev->next = dev->next;
	else
		stio->dev.head = dev->next;

	if (dev->next)
		dev->next->prev = dev->prev;
	else
		stio->dev.tail = dev->prev;

	stio_dev_event (dev, STIO_DEV_EVENT_DEL, 0);

	/* and call the callback function */
	dev->mth->kill (dev);

	STIO_MMGR_FREE (stio->mmgr, dev);
}

int stio_prologue (stio_t* stio)
{
	/* TODO: */
	return 0;
}

void stio_epilogue (stio_t* stio)
{
	/* TODO: */
}

int stio_exec (stio_t* stio)
{
	stio_ntime_t tmout;

#if defined(_WIN32)
	ULONG nentries, i;
#else
	int nentries, i;
#endif

	/*if (!stio->dev.head) return 0;*/

	
	if (stio_gettmrtmout (stio, STIO_NULL, &tmout) <= -1)
	{
		/* defaults to 1 second if timeout can't be acquired */
		tmout.sec = 1;
		tmout.nsec = 0;
	}


#if defined(_WIN32)
/*
	if (GetQueuedCompletionStatusEx (stio->iocp, stio->ovls, STIO_COUNTOF(stio->ovls), &nentries, timeout, FALSE) == FALSE)
	{
		// TODO: set errnum 
		return -1;
	}

	for (i = 0; i < nentries; i++)
	{
	}
*/

#else
	nentries = epoll_wait (stio->mux, stio->revs, STIO_COUNTOF(stio->revs), STIO_SECNSEC_TO_MSEC(tmout.sec, tmout.nsec));
	if (nentries <= -1)
	{
		if (errno == EINTR) return 0; /* it's actually ok */

		/* other errors are critical - EBADF, EFAULT, EINVAL */
		/* TODO: set errnum */
		return -1;
	}

	/* TODO: merge events??? for the same descriptor */
	for (i = 0; i < nentries; i++)
	{
		stio_dev_t* dev = stio->revs[i].data.ptr;

		if (dev->evcb->ready)
		{
			int x, events = 0;

			if (stio->revs[i].events & EPOLLERR) events |= STIO_DEV_EVENT_ERR;
		#if defined(EPOLLRDHUP)
			if (stio->revs[i].events & (EPOLLHUP | EPOLLRDHUP)) events |= STIO_DEV_EVENT_HUP;
		#else
			if (stio->revs[i].events & EPOLLHUP) events |= STIO_DEV_EVENT_HUP;
		#endif
			if (stio->revs[i].events & EPOLLIN) events |= STIO_DEV_EVENT_IN;
			if (stio->revs[i].events & EPOLLOUT) events |= STIO_DEV_EVENT_OUT;
			if (stio->revs[i].events & EPOLLPRI) events |= STIO_DEV_EVENT_PRI;

			/* return value of ready()
			 *   <= -1 - failure. kill the device.
			 *   == 0 - ok. but don't invoke recv() or send().
			 *   >= 1 - everything is ok. */
/* TODO: can the revs array contain the same file descriptor again??? */
			if ((x = dev->evcb->ready (dev, events)) <= -1) 
			{
				stio_killdev (stio, dev);
				dev = STIO_NULL;
			}
			else if (x >= 1) 
			{
				goto invoke_evcb;
			}
		}
		else
		{
			int dev_eof;

		invoke_evcb:

			dev_eof = 0;
			if (dev && stio->revs[i].events & EPOLLPRI)
			{
				/* urgent data */
	printf ("has urgent data...\n");
			}

			if (dev && stio->revs[i].events & EPOLLIN)
			{
				stio_len_t len;
				int x;

				/* the devices are all non-blocking. so read as much as possible */
				/* TODO: limit the number of iterations? */
				while (1)
				{
					len = STIO_COUNTOF(stio->bigbuf);
					x = dev->mth->recv (dev, stio->bigbuf, &len);

	printf ("DATA...recv %d  length %d\n", (int)x, len);
					if (x <= -1)
					{
						/*TODO: what to do? killdev? how to indicate an error? call on_recv? with error indicator?? */
						stio_killdev (stio, dev);
						dev = STIO_NULL;
						break;
					}
					else if (x == 0)
					{
						/* no data is available */
						break;
					}
					else if (x >= 1)
					{
						if (len <= 0) 
						{
							/* EOF received. delay killing until output has been handled */
							dev_eof = 1;
							break;
						}
						else
						{
							/* data available */
							if (dev->evcb->on_recv (dev, stio->bigbuf, len) <= -1)
							{
								stio_killdev (stio, dev);
								dev = STIO_NULL;
								break;
							}
						}
					}
				}
			}

			if (dev && stio->revs[i].events & EPOLLOUT)
			{
				while (!STIO_WQ_ISEMPTY(&dev->wq))
				{
					stio_wq_t* q;
					const stio_uint8_t* uptr;
					stio_len_t urem, ulen;
					int x;

					q = STIO_WQ_HEAD(&dev->wq);

					uptr = q->ptr;
					urem = q->len;

				send_leftover:
					ulen = urem;
					x = dev->mth->send (dev, uptr, &ulen);
					if (x <= -1)
					{
						/* TODO: error handling? call callback? or what? */
						stio_killdev (stio, dev);
						dev = STIO_NULL;
						break;
					}
					else if (x == 0)
					{
						/* keep the left-over */
						STIO_MEMMOVE (q->ptr, uptr, urem);
						q->len = urem;
						break;
					}
					else
					{
						uptr += ulen;
						urem -= ulen;

						if (urem <= 0)
						{
							int y;

							STIO_WQ_UNLINK (q); /* STIO_WQ_DEQ(&dev->wq); */
							y = dev->evcb->on_sent (dev, q->ctx);
							STIO_MMGR_FREE (dev->stio->mmgr, q);

							if (y <= -1)
							{
								stio_killdev (stio, dev);
								dev = STIO_NULL;
								break;
							}
						}
						else goto send_leftover;
					}
				}

				if (dev && STIO_WQ_ISEMPTY(&dev->wq))
				{
					/* no pending request to write. 
					 * watch input only. disable output watching */
					if (stio_dev_event (dev, STIO_DEV_EVENT_UPD, STIO_DEV_EVENT_IN) <= -1)
					{
						/* TODO: call an error handler??? */
						stio_killdev (stio, dev);
						dev = STIO_NULL;
					}
				}
			}

			if (dev && dev_eof)
			{
				/* handled delayed device killing */
				stio_killdev (stio, dev);
				dev = STIO_NULL;
			}
		}
	}

#endif

	return 0;
}

void stio_stop (stio_t* stio)
{
	stio->stopreq = 1;
}

int stio_loop (stio_t* stio)
{
	if (!stio->dev.head) return 0;

	stio->stopreq = 0;
	if (stio_prologue (stio) <= -1) return -1;

	while (!stio->stopreq && stio->dev.head)
	{
		if (stio_exec (stio) <= -1) break;
		/* you can do other things here */
	}

	stio_epilogue (stio);
	return 0;
}


int stio_dev_ioctl (stio_dev_t* dev, int cmd, void* arg)
{
	if (dev->mth->ioctl) return dev->mth->ioctl (dev, cmd, arg);
	dev->stio->errnum = STIO_ENOSUP; /* TODO: different error code ? */
	return -1;
}

int stio_dev_event (stio_dev_t* dev, stio_dev_event_cmd_t cmd, int flags)
{
#if defined(_WIN32)

	/* TODO */
#else
	struct epoll_event ev;
	int epoll_op;

	ev.events = EPOLLHUP | EPOLLERR;
#if defined(EPOLLRDHUP)
	ev.events |= EPOLLRDHUP;
#endif

	if (flags & STIO_DEV_EVENT_IN) ev.events |= EPOLLIN;
	if (flags & STIO_DEV_EVENT_OUT) ev.events |= EPOLLOUT;
	if (flags & STIO_DEV_EVENT_PRI) ev.events |= EPOLLPRI;
	ev.data.ptr = dev;

	switch (cmd)
	{
		case STIO_DEV_EVENT_ADD:
			epoll_op = EPOLL_CTL_ADD;
			break;

		case STIO_DEV_EVENT_UPD:
			epoll_op = EPOLL_CTL_MOD;
			break;

		case STIO_DEV_EVENT_DEL:
			epoll_op = EPOLL_CTL_DEL;
			break;

		default:
			dev->stio->errnum = STIO_EINVAL;
			return -1;
	}

	if (epoll_ctl (dev->stio->mux, epoll_op, dev->mth->getsyshnd(dev), &ev) == -1)
	{
		/* TODO: set dev->stio->errnum from errno */
		return -1;
	}

	return 0;
#endif
}

int stio_dev_send (stio_dev_t* dev, const void* data, stio_len_t len, void* sendctx)
{
	const stio_uint8_t* uptr;
	stio_len_t urem, ulen;
	int x;

	uptr = data;
	urem = len;

	while (urem > 0)
	{
		ulen = urem;
		x = dev->mth->send (dev, data, &ulen);
		if (x <= -1) 
		{
			return -1;
		}
		else if (x == 0)
		{
			stio_wq_t* q;
			int wq_empty;

			/* queue the uremaining data*/
			wq_empty = STIO_WQ_ISEMPTY(&dev->wq);

			q = (stio_wq_t*)STIO_MMGR_ALLOC (dev->stio->mmgr, STIO_SIZEOF(*q) + urem);
			if (!q)
			{
				dev->stio->errnum = STIO_ENOMEM;
				return -1;
			}

			q->ctx = sendctx;
			q->ptr = (stio_uint8_t*)(q + 1);
			q->len = urem;
			STIO_MEMCPY (q->ptr, uptr, urem);

			STIO_WQ_ENQ (&dev->wq, q);
			if (wq_empty)
			{
				if (stio_dev_event (dev, STIO_DEV_EVENT_UPD, STIO_DEV_EVENT_IN | STIO_DEV_EVENT_OUT) <= -1)
				{
					STIO_WQ_UNLINK (q); /* unlink the ENQed item */
					STIO_MMGR_FREE (dev->stio->mmgr, q);
					return -1;
				}
			}

			return 0;
		}
		else 
		{
			urem -= ulen;
			uptr += ulen;
		}
	}

	dev->evcb->on_sent (dev, sendctx);
	return 0;
}
