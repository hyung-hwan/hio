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

stio_t* stio_open (stio_mmgr_t* mmgr, stio_size_t xtnsize, stio_errnum_t* errnum)
{
	stio_t* stio;

	stio = STIO_MMGR_ALLOC (mmgr, STIO_SIZEOF(*stio));
	if (!stio) 
	{
		if (errnum) *errnum = STIO_ENOMEM;
		return STIO_NULL;
	}

	STIO_MEMSET (stio, 0, STIO_SIZEOF(*stio));
	stio->mmgr = mmgr;

	stio->mux = epoll_create (1000);
	if (stio->mux == -1)
	{
		//if (errnum) *errnum = XXXXX
		STIO_MMGR_FREE (stio->mmgr, stio);
		return STIO_NULL;
	}

	return stio;
}

void stio_close (stio_t* stio)
{
	while (stio->dev.tail)
	{
		stio_killdev (stio, stio->dev.tail);
	}

	close (stio->mux);
	STIO_MMGR_FREE (stio->mmgr, stio);
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

	/* delink the dev object first */
	if (dev->prev)
		dev->prev->next = dev->next;
	else
		stio->dev.head = dev->next;

	if (dev->next)
		dev->next->prev = dev->prev;
	else
		stio->dev.tail = dev->prev;

	/* ------------------------------------ */
	{
	#if defined(_WIN32)
		/* nothing - can't deassociate it. closing the socket
		 * will do. kill should close it */
	#else
		struct epoll_event ev; /* dummy */
		epoll_ctl (stio->mux, EPOLL_CTL_DEL, dev->mth->getsyshnd(dev), &ev); 
		/* don't care about failure */
	#endif
	}
	/* ------------------------------------ */

	/* and call the callback function */
	dev->mth->kill (dev);

	STIO_MMGR_FREE (stio->mmgr, dev);
}

int stio_prologue (stio_t* stio)
{

	return 0;
}

void stio_epilogue (stio_t* stio)
{
}

int stio_exec (stio_t* stio)
{
	int timeout;

#if defined(_WIN32)
	ULONG nentries, i;
#else
	int nentries, i;
#endif

	/*if (!stio->dev.head) return 0;*/

	timeout = 1000; //TODO: get_timeout (stio); // use the task heap to get the timeout.

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



	nentries = epoll_wait (stio->mux, stio->revs, STIO_COUNTOF(stio->revs), timeout);
	if (nentries <= -1)
	{
		/* TODO: set errnum */
		return -1;
	}

	for (i = 0; i < nentries; i++)
	{
		stio_dev_t* dev = stio->revs[i].data.ptr;

		if (dev->evcb->ready)
		{
			int x, events = 0;

			if (stio->revs[i].events & EPOLLERR) events |= STIO_DEV_EVENT_ERR;
			if (stio->revs[i].events & (EPOLLHUP | EPOLLRDHUP)) events |= STIO_DEV_EVENT_HUP;
			if (stio->revs[i].events & EPOLLIN) events |= STIO_DEV_EVENT_IN;
			if (stio->revs[i].events & EPOLLOUT) events |= STIO_DEV_EVENT_OUT;
			if (stio->revs[i].events & EPOLLPRI) events |= STIO_DEV_EVENT_PRI;

			/* return value of ready()
			 *   <= -1 - failure. kill the device.
			 *   == 0 - ok. but don't invoke recv() or send(). if you want to kill the device within the ready callback, return 0.
			 *   >= 1 - everything is ok. */
/* TODO: can the revs array contain the same file descriptor again??? */
			if ((x = dev->evcb->ready (dev, events)) <= -1) 
				stio_killdev (stio, dev);
			else if (x >= 1) goto invoke_evcb;
		}
		else
		{
		invoke_evcb:
			if (stio->revs[i].events & EPOLLPRI)
			{
				/* urgent data */
	printf ("has urgent data...\n");
			}

			if (stio->revs[i].events & EPOLLIN)
			{
				stio_len_t len;
				int x;

				len = STIO_COUNTOF(stio->bigbuf);
				x = dev->mth->recv (dev, stio->bigbuf, &len);

printf ("DATA...recv %d  length %d\n", (int)x, len);
				if (x <= -1)
				{
				}
				else if (x == 0)
				{
					stio_killdev (stio, dev);
				}
				else if (x >= 1)
				{
					/* data available??? */
					dev->evcb->on_recv (dev, stio->bigbuf, len);
				}
			}

			if (stio->revs[i].events & EPOLLOUT)
			{
				/*
				if (there is data to write)
				{
					x = dev->mth->send (dev, stio->bigbuf, &len);
					if (x <= -1)
					{
					}

					dev->evcb->on_sent (dv, stio->bigbuf, x);
				}*/
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
printf ("executing stio_exec...%p \n", stio->dev.head);
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

		case STIO_DEV_EVENT_MOD:
			epoll_op = EPOLL_CTL_MOD;
			break;

		case STIO_DEV_EVENT_DEL:
			epoll_op = EPOLL_CTL_ADD;
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
