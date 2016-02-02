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
		stio->errnum = stio_syserrtoerrnum(errno);
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
	/* default capability. dev->dev_mth->make() can change this.
	 * stio_dev_watch() is affected by the capability change. */
	dev->dev_capa = STIO_DEV_CAPA_IN | STIO_DEV_CAPA_OUT;
	dev->dev_mth = dev_mth;
	dev->dev_evcb = dev_evcb;
	STIO_WQ_INIT(&dev->wq);

	/* call the callback function first */
	stio->errnum = STIO_ENOERR;
	if (dev->dev_mth->make (dev, make_ctx) <= -1)
	{
		if (stio->errnum == STIO_ENOERR) stio->errnum = STIO_EDEVMAKE;
		goto oops;
	}

#if defined(_WIN32)
	if (CreateIoCompletionPort ((HANDLE)dev->dev_mth->getsyshnd(dev), stio->iocp, STIO_IOCP_KEY, 0) == NULL)
	{
		/* TODO: set errnum from GetLastError()... */
		goto oops_after_make;
	}
#else
	if (stio_dev_watch (dev, STIO_DEV_WATCH_START, STIO_DEV_EVENT_IN) <= -1) goto oops_after_make;
#endif

	/* and place the new dev at the back */
	if (stio->dev.tail) stio->dev.tail->dev_next = dev;
	else stio->dev.head = dev;
	dev->dev_prev = stio->dev.tail;
	stio->dev.tail = dev;

	return dev;

oops_after_make:
	dev->dev_mth->kill (dev);
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
		STIO_WQ_DEQ (&dev->wq);

		STIO_MMGR_FREE (stio->mmgr, wq);
	}

	/* delink the dev object */
	if (!(dev->dev_capa & STIO_DEV_CAPA_RUINED))
	{
		if (dev->dev_prev)
			dev->dev_prev->dev_next = dev->dev_next;
		else
			stio->dev.head = dev->dev_next;

		if (dev->dev_next)
			dev->dev_next->dev_prev = dev->dev_prev;
		else
			stio->dev.tail = dev->dev_prev;
	}

	stio_dev_watch (dev, STIO_DEV_WATCH_STOP, 0);

	/* and call the callback function */
	dev->dev_mth->kill (dev);

	STIO_MMGR_FREE (stio->mmgr, dev);
}

void stio_ruindev (stio_t* stio, stio_dev_t* dev)
{
	if (!(dev->dev_capa & STIO_DEV_CAPA_RUINED))
	{
		/* delink the dev object from the device list */
		if (dev->dev_prev)
			dev->dev_prev->dev_next = dev->dev_next;
		else
			stio->dev.head = dev->dev_next;
		if (dev->dev_next)
			dev->dev_next->dev_prev = dev->dev_prev;
		else
			stio->dev.tail = dev->dev_prev;

		/* place it at the beginning of the ruined device list */
		dev->dev_prev = STIO_NULL;
		dev->dev_next = stio->rdev;
		stio->rdev = dev;

		dev->dev_capa |= STIO_DEV_CAPA_RUINED;
	}
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

	/* execute the scheduled jobs before checking devices with the 
	 * multiplexer. the scheduled jobs can safely destroy the devices */
	stio_firetmrjobs (stio, STIO_NULL, STIO_NULL);

	if (stio_gettmrtmout (stio, STIO_NULL, &tmout) <= -1)
	{
		/* defaults to 1 second if timeout can't be acquired */
		tmout.sec = 1; /* TODO: make the default timeout configurable */
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
		stio->errnum = stio_syserrtoerrnum(errno);
		return -1;
	}

	/* TODO: merge events??? for the same descriptor */
	for (i = 0; i < nentries; i++)
	{
		stio_dev_t* dev;

		dev = stio->revs[i].data.ptr;

		if (dev->dev_evcb->ready)
		{
			int x, events = 0;

			if (stio->revs[i].events & EPOLLERR) events |= STIO_DEV_EVENT_ERR;
			if (stio->revs[i].events & EPOLLHUP) events |= STIO_DEV_EVENT_HUP;
			if (stio->revs[i].events & EPOLLIN) events |= STIO_DEV_EVENT_IN;
			if (stio->revs[i].events & EPOLLOUT) events |= STIO_DEV_EVENT_OUT;
			if (stio->revs[i].events & EPOLLPRI) events |= STIO_DEV_EVENT_PRI;

			#if defined(EPOLLRDHUP)
			/* interprete EPOLLRDHUP the same way as EPOLLHUP.
			 * 
			 * when EPOLLRDHUP is set, EPOLLIN or EPOLLPRI or both are 
			 * assumed to be set as EPOLLRDHUP is requested only if 
			 * STIO_DEV_WATCH_IN is set for stio_dev_watch(). 
			 * in linux, when EPOLLRDHUP is set, EPOLLIN is set together 
			 * if it's requested together. it seems to be safe to have
			 * the following assertion. however, let me commect it out 
			 * in case the assumption above is not right.
			 * STIO_ASSERT (events & (STIO_DEV_EVENT_IN | STIO_DEV_EVENT_PRI));
			 */
			if (stio->revs[i].events & EPOLLRDHUP) events |= STIO_DEV_EVENT_HUP;
			#endif

			/* return value of ready()
			 *   <= -1 - failure. kill the device.
			 *   == 0 - ok. but don't invoke recv() or send().
			 *   >= 1 - everything is ok. */
			if ((x = dev->dev_evcb->ready (dev, events)) <= -1) 
			{
				stio_ruindev (stio, dev);
				dev = STIO_NULL;
			}
			else if (x >= 1) 
			{
				goto invoke_evcb;
			}
		}
		else
		{
		invoke_evcb:
			if (dev && stio->revs[i].events & EPOLLPRI)
			{
				/* urgent data */
/* TODO: urgent data.... */
				/*x = dev->dev_mth->urgrecv (dev, stio->bugbuf, &len);*/
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
					x = dev->dev_mth->recv (dev, stio->bigbuf, &len);

					if (x <= -1)
					{
						stio_ruindev (stio, dev);
						dev = STIO_NULL;
						break;
					}
					else if (x == 0)
					{
						/* no data is available - EWOULDBLOCK or something similar */
						break;
					}
					else if (x >= 1)
					{
						if (len <= 0) 
						{
							/* EOF received. delay killing until output has been handled. */
							if (STIO_WQ_ISEMPTY(&dev->wq))
							{
								/* no pending writes - ruin the device to kill it eventually */
								stio_ruindev (stio, dev);
								/* don't set 'dev' to STIO_NULL so that 
							      * output handling can kick in if EPOLLOUT is set */
							}
							else
							{
								/* it might be in a half-open state */
								dev->dev_capa &= ~(STIO_DEV_CAPA_IN | STIO_DEV_CAPA_PRI);

								/* disable the input watching */
								if (stio_dev_watch (dev, STIO_DEV_WATCH_UPDATE, STIO_DEV_EVENT_OUT) <= -1)
								{
									stio_ruindev (stio, dev);
									dev = STIO_NULL;
								}
							}
							
							break;
						}
						else
						{
				/* TODO: for a stream device, merge received data if bigbuf isn't full and fire the on_recv callback
				 *        when x == 0 or <= -1. you can  */

							/* data available */
							if (dev->dev_evcb->on_recv (dev, stio->bigbuf, len) <= -1)
							{
								stio_ruindev (stio, dev);
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
					x = dev->dev_mth->send (dev, uptr, &ulen);
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
							y = dev->dev_evcb->on_sent (dev, q->ctx);
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
					if (dev->dev_capa & STIO_DEV_CAPA_IN)
					{
						if (stio_dev_watch (dev, STIO_DEV_WATCH_UPDATE, STIO_DEV_EVENT_IN) <= -1)
						{
							stio_ruindev (stio, dev);
							dev = STIO_NULL;
						}
					}
					else
					{
						/* the device is not capable of reading. 
						 * finish the device */
						stio_ruindev (stio, dev);
						dev = STIO_NULL;
					}
				}
			}
		}
	}

	/* kill all ruined devices */
	while (stio->rdev)
	{
		stio_dev_t* next;
		next = stio->rdev->dev_next;
		stio_killdev (stio, stio->rdev);
		stio->rdev = next;
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
	if (dev->dev_mth->ioctl) return dev->dev_mth->ioctl (dev, cmd, arg);
	dev->stio->errnum = STIO_ENOSUP; /* TODO: different error code ? */
	return -1;
}

int stio_dev_watch (stio_dev_t* dev, stio_dev_watch_cmd_t cmd, int events)
{
	struct epoll_event ev;
	int epoll_op;

	/* this function honors STIO_DEV_EVENT_IN and STIO_DEV_EVENT_OUT only
	 * as valid input event bits. it intends to provide simple abstraction
	 * by reducing the variety of event bits that the caller has to handle. */
	ev.events = EPOLLHUP | EPOLLERR;

	if (events & STIO_DEV_EVENT_IN)
	{
		if (dev->dev_capa & STIO_DEV_CAPA_IN) 
		{
			ev.events |= EPOLLIN;
		#if defined(EPOLLRDHUP)
			ev.events |= EPOLLRDHUP;
		#endif
		}
		if (dev->dev_capa & STIO_DEV_CAPA_PRI) 
		{
			ev.events |= EPOLLPRI;
		#if defined(EPOLLRDHUP)
			ev.events |= EPOLLRDHUP;
		#endif
		}
	}

	if (events & STIO_DEV_EVENT_OUT)
	{
		if (dev->dev_capa & STIO_DEV_CAPA_OUT) ev.events |= EPOLLOUT;
	}

	ev.data.ptr = dev;
	switch (cmd)
	{
		case STIO_DEV_WATCH_START:
			epoll_op = EPOLL_CTL_ADD;
			break;

		case STIO_DEV_WATCH_UPDATE:
			epoll_op = EPOLL_CTL_MOD;
			break;

		case STIO_DEV_WATCH_STOP:
			epoll_op = EPOLL_CTL_DEL;
			break;

		default:
			dev->stio->errnum = STIO_EINVAL;
			return -1;
	}

	if (epoll_ctl (dev->stio->mux, epoll_op, dev->dev_mth->getsyshnd(dev), &ev) == -1)
	{
		dev->stio->errnum = stio_syserrtoerrnum(errno);
		return -1;
	}

	return 0;
}

int stio_dev_send (stio_dev_t* dev, const void* data, stio_len_t len, void* sendctx)
{
	const stio_uint8_t* uptr;
	stio_len_t urem, ulen;
	stio_wq_t* q;
	int x, wq_empty;

	if (!(dev->dev_capa & STIO_DEV_CAPA_OUT))
	{
		dev->stio->errnum = STIO_ENOCAPA;
		return -1;
	}

	uptr = data;
	urem = len;

	wq_empty = STIO_WQ_ISEMPTY(&dev->wq);
	if (!wq_empty) goto enqueue_data;

	while (urem > 0)
	{
		ulen = urem;
		x = dev->dev_mth->send (dev, data, &ulen);
		if (x <= -1) return -1;
		else if (x == 0) goto enqueue_data; /* enqueue remaining data */
		else 
		{
			urem -= ulen;
			uptr += ulen;
		}
	}

	dev->dev_evcb->on_sent (dev, sendctx);
	return 0;

enqueue_data:
	/* queue the remaining data*/
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
		if (stio_dev_watch (dev, STIO_DEV_WATCH_UPDATE, STIO_DEV_EVENT_IN | STIO_DEV_EVENT_OUT) <= -1)
		{
			STIO_WQ_UNLINK (q); /* unlink the ENQed item */
			STIO_MMGR_FREE (dev->stio->mmgr, q);
			return -1;
		}
	}

	return 0;
}

stio_errnum_t stio_syserrtoerrnum (int no)
{
	switch (no)
	{
		case ENOMEM:
			return STIO_ENOMEM;

		case EINVAL:
			return STIO_EINVAL;

		case ENOENT:
			return STIO_ENOENT;

		case EMFILE:
			return STIO_EMFILE;

	#if defined(ENFILE)
		case ENFILE:
			return STIO_ENFILE;
	#endif

	#if defined(ECONNREFUSED)
		case ECONNREFUSED:
			return STIO_ECONRF;
	#endif

	#if defined(ECONNRESETD)
		case ECONNRESET:
			return STIO_ECONRS;
	#endif

		default:
			return STIO_ESYSERR;
	}
}
