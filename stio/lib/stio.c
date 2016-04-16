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

#define DEV_CAPA_ALL_WATCHED (STIO_DEV_CAPA_IN_WATCHED | STIO_DEV_CAPA_OUT_WATCHED | STIO_DEV_CAPA_PRI_WATCHED)

#define IS_MSPACE(x) ((x) == STIO_MT(' ') || (x) == STIO_MT('\t'))

static int schedule_kill_zombie_job (stio_dev_t* dev);
static int kill_and_free_device (stio_dev_t* dev, int force);

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
	stio_dev_t* dev;

	/* purge scheduled timer jobs and kill the timer */
	stio_cleartmrjobs (stio);
	STIO_MMGR_FREE (stio->mmgr, stio->tmr.jobs);

	/* kill all registered devices */
	while (stio->dev.head)
	{
		stio_killdev (stio, stio->dev.head);
	}

	while (stio->hdev.head)
	{
		stio_killdev (stio, stio->hdev.head);
	}

	/* clean up all zombie devices */
	for (dev = stio->zmbdev.head; dev; )
	{
		kill_and_free_device (dev, 1);
		if (stio->zmbdev.head == dev) dev = dev->dev_next;
		else dev = stio->zmbdev.head;
	}

	while (stio->zmbdev.tail)
	{
		/* if the kill method returns failure, it can leak some resource
		 * because the device is freed regardless of the failure when 2 
		 * is given to kill_and_free_device(). */
		kill_and_free_device (stio->zmbdev.tail, 2);
	}

	/* close the multiplexer */
	close (stio->mux);
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

static STIO_INLINE void unlink_wq (stio_t* stio, stio_wq_t* q)
{
	if (q->tmridx != STIO_TMRIDX_INVALID)
	{
		stio_deltmrjob (stio, q->tmridx);
		STIO_ASSERT (q->tmridx == STIO_TMRIDX_INVALID);
	}
	STIO_WQ_UNLINK (q);
}

static STIO_INLINE void handle_event (stio_t* stio, stio_size_t i)
{
	stio_dev_t* dev;

	stio->renew_watch = 0;
	dev = stio->revs[i].data.ptr;

	STIO_ASSERT (stio == dev->stio);

	if (dev->dev_evcb->ready)
	{
		int x, events = 0;

		if (stio->revs[i].events & EPOLLIN) events |= STIO_DEV_EVENT_IN;
		if (stio->revs[i].events & EPOLLOUT) events |= STIO_DEV_EVENT_OUT;
		if (stio->revs[i].events & EPOLLPRI) events |= STIO_DEV_EVENT_PRI;
		if (stio->revs[i].events & EPOLLERR) events |= STIO_DEV_EVENT_ERR;
		if (stio->revs[i].events & EPOLLHUP) events |= STIO_DEV_EVENT_HUP;
	#if defined(EPOLLRDHUP)
		else if (stio->revs[i].events & EPOLLRDHUP) events |= STIO_DEV_EVENT_HUP;
	#endif

		/* return value of ready()
		 *   <= -1 - failure. kill the device.
		 *   == 0 - ok. but don't invoke recv() or send().
		 *   >= 1 - everything is ok. */
		x = dev->dev_evcb->ready (dev, events);
		if (x <= -1)
		{
			stio_dev_halt (dev);
			return;
		}
		else if (x == 0) goto skip_evcb;
	}

	if (dev && stio->revs[i].events & EPOLLPRI)
	{
		/* urgent data */
/* TODO: urgent data.... */
		/*x = dev->dev_mth->urgread (dev, stio->bugbuf, &len);*/
printf ("has urgent data...\n");
	}

	if (dev && stio->revs[i].events & EPOLLOUT)
	{
		/* write pending requests */
		while (!STIO_WQ_ISEMPTY(&dev->wq))
		{
			stio_wq_t* q;
			const stio_uint8_t* uptr;
			stio_iolen_t urem, ulen;
			int x;

			q = STIO_WQ_HEAD(&dev->wq);

			uptr = q->ptr;
			urem = q->len;

		send_leftover:
			ulen = urem;
			x = dev->dev_mth->write (dev, uptr, &ulen, &q->dstadr);
			if (x <= -1)
			{
				stio_dev_halt (dev);
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
					/* finished writing a single write request */
					int y, out_closed = 0;

					if (q->len <= 0 && (dev->dev_capa & STIO_DEV_CAPA_STREAM)) 
					{
						/* it was a zero-length write request. 
						 * for a stream, it is to close the output. */
						dev->dev_capa |= STIO_DEV_CAPA_OUT_CLOSED;
						stio->renew_watch = 1;
						out_closed = 1;
					}

					unlink_wq (stio, q);
					y = dev->dev_evcb->on_write (dev, q->olen, q->ctx, &q->dstadr);
					STIO_MMGR_FREE (stio->mmgr, q);

					if (y <= -1)
					{
						stio_dev_halt (dev);
						dev = STIO_NULL;
						break;
					}

					if (out_closed)
					{
						/* drain all pending requests. 
						 * callbacks are skipped for drained requests */
						while (!STIO_WQ_ISEMPTY(&dev->wq))
						{
							q = STIO_WQ_HEAD(&dev->wq);
							unlink_wq (stio, q);
							STIO_MMGR_FREE (dev->stio->mmgr, q);
						}
						break;
					}
				}
				else goto send_leftover;
			}
		}

		if (dev && STIO_WQ_ISEMPTY(&dev->wq))
		{
			/* no pending request to write */
			if ((dev->dev_capa & STIO_DEV_CAPA_IN_CLOSED) &&
			    (dev->dev_capa & STIO_DEV_CAPA_OUT_CLOSED))
			{
				stio_dev_halt (dev);
				dev = STIO_NULL;
			}
			else
			{
				stio->renew_watch = 1;
			}
		}
	}

	if (dev && stio->revs[i].events & EPOLLIN)
	{
		stio_devadr_t srcadr;
		stio_iolen_t len;
		int x;

		/* the devices are all non-blocking. read as much as possible
		 * if on_read callback returns 1 or greater. read only once
		 * if the on_read calllback returns 0. */
		while (1)
		{
			len = STIO_COUNTOF(stio->bigbuf);
			x = dev->dev_mth->read (dev, stio->bigbuf, &len, &srcadr);
			if (x <= -1)
			{
				stio_dev_halt (dev);
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
				if (len <= 0 && (dev->dev_capa & STIO_DEV_CAPA_STREAM)) 
				{
					/* EOF received. for a stream device, a zero-length 
					 * read is interpreted as EOF. */
					dev->dev_capa |= STIO_DEV_CAPA_IN_CLOSED;
					stio->renew_watch = 1;

					/* call the on_read callback to report EOF */
					if (dev->dev_evcb->on_read (dev, stio->bigbuf, len, &srcadr) <= -1 ||
					    (dev->dev_capa & STIO_DEV_CAPA_OUT_CLOSED))
					{
						/* 1. input ended and its reporting failed or 
						 * 2. input ended and no writing is possible */
						stio_dev_halt (dev);
						dev = STIO_NULL;
					}

					/* since EOF is received, reading can't be greedy */
					break;
				}
				else
				{
					int y;
		/* TODO: for a stream device, merge received data if bigbuf isn't full and fire the on_read callback
		 *        when x == 0 or <= -1. you can  */

					/* data available */
					y = dev->dev_evcb->on_read (dev, stio->bigbuf, len, &srcadr);
					if (y <= -1)
					{
						stio_dev_halt (dev);
						dev = STIO_NULL;
						break;
					}
					else if (y == 0)
					{
						/* don't be greedy. read only once 
						 * for this loop iteration */
						break;
					}
				}
			}
		}
	}

	if (dev)
	{
		if (stio->revs[i].events & (EPOLLERR | EPOLLHUP)) 
		{ 
			/* if error or hangup has been reported on the device,
			 * halt the device. this check is performed after
			 * EPOLLIN or EPOLLOUT check because EPOLLERR or EPOLLHUP
			 * can be set together with EPOLLIN or EPOLLOUT. */
			dev->dev_capa |= STIO_DEV_CAPA_IN_CLOSED | STIO_DEV_CAPA_OUT_CLOSED;
			stio->renew_watch = 1;
		}
	#if defined(EPOLLRDHUP)
		else if (dev && stio->revs[i].events & EPOLLRDHUP) 
		{
			if (stio->revs[i].events & (EPOLLIN | EPOLLOUT | EPOLLPRI))
			{
				/* it may be a half-open state. don't do anything here
				 * to let the next read detect EOF */
			}
			else
			{
				dev->dev_capa |= STIO_DEV_CAPA_IN_CLOSED | STIO_DEV_CAPA_OUT_CLOSED;
				stio->renew_watch = 1;
			}
		}
	#endif

		if ((dev->dev_capa & STIO_DEV_CAPA_IN_CLOSED) &&
		    (dev->dev_capa & STIO_DEV_CAPA_OUT_CLOSED))
		{
			stio_dev_halt (dev);
			dev = STIO_NULL;
		}
	}

skip_evcb:
	if (dev && stio->renew_watch && stio_dev_watch (dev, STIO_DEV_WATCH_RENEW, 0) <= -1)
	{
		stio_dev_halt (dev);
		dev = STIO_NULL;
	}
}

static STIO_INLINE int __exec (stio_t* stio)
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
		handle_event (stio, i);
	}

	/* kill all halted devices */
	while (stio->hdev.head) 
	{
printf ("KILLING HALTED DEVICE %p\n", stio->hdev.head);
		stio_killdev (stio, stio->hdev.head);
	}
	STIO_ASSERT (stio->hdev.tail == STIO_NULL);

#endif

	return 0;
}

int stio_exec (stio_t* stio)
{
	int n;

	stio->in_exec = 1;
	n = __exec (stio);
	stio->in_exec = 0;

	return n;
}

void stio_stop (stio_t* stio)
{
	stio->stopreq = 1;
}

int stio_loop (stio_t* stio)
{
	if (!stio->dev.head) return 0;

	stio->stopreq = 0;
	stio->renew_watch = 0;

	if (stio_prologue (stio) <= -1) return -1;

	while (!stio->stopreq && stio->dev.head)
	{
		if (stio_exec (stio) <= -1) break;
		/* you can do other things here */
	}

	stio_epilogue (stio);
	return 0;
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
	dev->dev_size = dev_size;
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

	/* the make callback must not change these fields */
	STIO_ASSERT (dev->dev_mth == dev_mth);
	STIO_ASSERT (dev->dev_evcb == dev_evcb);
	STIO_ASSERT (dev->dev_prev == STIO_NULL);
	STIO_ASSERT (dev->dev_next == STIO_NULL);

	/* set some internal capability bits according to the capabilities 
	 * removed by the device making callback for convenience sake. */
	if (!(dev->dev_capa & STIO_DEV_CAPA_IN)) dev->dev_capa |= STIO_DEV_CAPA_IN_CLOSED;
	if (!(dev->dev_capa & STIO_DEV_CAPA_OUT)) dev->dev_capa |= STIO_DEV_CAPA_OUT_CLOSED;

#if defined(_WIN32)
	if (CreateIoCompletionPort ((HANDLE)dev->dev_mth->getsyshnd(dev), stio->iocp, STIO_IOCP_KEY, 0) == NULL)
	{
		/* TODO: set errnum from GetLastError()... */
		goto oops_after_make;
	}
#else
	if (stio_dev_watch (dev, STIO_DEV_WATCH_START, 0) <= -1) goto oops_after_make;
#endif

	/* and place the new dev at the back */
	if (stio->dev.tail) stio->dev.tail->dev_next = dev;
	else stio->dev.head = dev;
	dev->dev_prev = stio->dev.tail;
	stio->dev.tail = dev;

	return dev;

oops_after_make:
	if (kill_and_free_device (dev, 0) <= -1)
	{
		/* schedule a timer job that reattempts to destroy the device */
		if (schedule_kill_zombie_job (dev) <= -1) 
		{
			/* job scheduling failed. i have no choice but to
			 * destroy the device now.
			 * 
			 * NOTE: this while loop can block the process
			 *       if the kill method keep returning failure */
			while (kill_and_free_device (dev, 1) <= -1)
			{
				if (stio->stopreq) 
				{
					/* i can't wait until destruction attempt gets
					 * fully successful. there is a chance that some
					 * resources can leak inside the device */
					kill_and_free_device (dev, 2);
					break;
				}
			}
		}

		return STIO_NULL;
	}

oops:
	STIO_MMGR_FREE (stio->mmgr, dev);
	return STIO_NULL;
}

static int kill_and_free_device (stio_dev_t* dev, int force)
{
	stio_t* stio;

	stio = dev->stio;

	if (dev->dev_mth->kill(dev, force) <= -1) 
	{
		if (force >= 2) goto free_device;

		if (!(dev->dev_capa & STIO_DEV_CAPA_ZOMBIE))
		{
			dev->dev_capa |= STIO_DEV_CAPA_ZOMBIE;

			/* place it at the back of the zombie device list */
			if (stio->hdev.tail) stio->hdev.tail->dev_next = dev;
			else stio->zmbdev.head = dev;
			dev->dev_prev = stio->hdev.tail;
			dev->dev_next = STIO_NULL;
			stio->zmbdev.tail = dev;
		}

		return -1;
	}

free_device:
	if (dev->dev_capa & STIO_DEV_CAPA_ZOMBIE)
	{
		/* detach it from the zombie device list */
		if (dev->dev_prev)
			dev->dev_prev->dev_next = dev->dev_next;
		else
			stio->zmbdev.head = dev->dev_next;

		if (dev->dev_next)
			dev->dev_next->dev_prev = dev->dev_prev;
		else
			stio->zmbdev.tail = dev->dev_prev;
	}

	STIO_MMGR_FREE (stio->mmgr, dev);
	return 0;
}

static void kill_zombie_job_handler (stio_t* stio, const stio_ntime_t* now, stio_tmrjob_t* job)
{
	stio_dev_t* dev = (stio_dev_t*)job->ctx;

	STIO_ASSERT (dev->dev_capa & STIO_DEV_CAPA_ZOMBIE);

	if (kill_and_free_device (dev, 0) <= -1)
	{
		if (schedule_kill_zombie_job (dev) <= -1)
		{
			/* i have to choice but to free up the devide by force */
			while (kill_and_free_device (dev, 1) <= -1)
			{
				if (stio->stopreq) 
				{
					/* i can't wait until destruction attempt gets
					 * fully successful. there is a chance that some
					 * resources can leak inside the device */
					kill_and_free_device (dev, 2);
					break;
				}
			}
		}
	}
}

static int schedule_kill_zombie_job (stio_dev_t* dev)
{
	stio_tmrjob_t kill_zombie_job;
	stio_ntime_t tmout;

	stio_inittime (&tmout, 3, 0); /* TODO: take it from configuration */

	STIO_MEMSET (&kill_zombie_job, 0, STIO_SIZEOF(kill_zombie_job));
	kill_zombie_job.ctx = dev;
	stio_gettime (&kill_zombie_job.when);
	stio_addtime (&kill_zombie_job.when, &tmout, &kill_zombie_job.when);
	kill_zombie_job.handler = kill_zombie_job_handler;
	/*kill_zombie_job.idxptr = &rdev->tmridx_kill_zombie;*/

	return stio_instmrjob (dev->stio, &kill_zombie_job) == STIO_TMRIDX_INVALID? -1: 0;
}

void stio_killdev (stio_t* stio, stio_dev_t* dev)
{
	STIO_ASSERT (stio == dev->stio);

	if (dev->dev_capa & STIO_DEV_CAPA_ZOMBIE)
	{
		STIO_ASSERT (STIO_WQ_ISEMPTY(&dev->wq));
		goto kill_device;
	}

	/* clear pending send requests */
	while (!STIO_WQ_ISEMPTY(&dev->wq))
	{
		stio_wq_t* q;
		q = STIO_WQ_HEAD(&dev->wq);
		unlink_wq (stio, q);
		STIO_MMGR_FREE (stio->mmgr, q);
	}

	/* delink the dev object */
	if (dev->dev_capa & STIO_DEV_CAPA_HALTED)
	{
		/* this device is in the halted state.
		 * unlink it from the halted device list */
		if (dev->dev_prev)
			dev->dev_prev->dev_next = dev->dev_next;
		else
			stio->hdev.head = dev->dev_next;

		if (dev->dev_next)
			dev->dev_next->dev_prev = dev->dev_prev;
		else
			stio->hdev.tail = dev->dev_prev;
	}
	else
	{
		/* this device has not been halted.
		 * unlink it from the normal active device list */
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

kill_device:
	if (kill_and_free_device(dev, 0) <= -1)
	{
		STIO_ASSERT (dev->dev_capa & STIO_DEV_CAPA_ZOMBIE);
		if (schedule_kill_zombie_job (dev) <= -1)
		{
			/* i have to choice but to free up the devide by force */
			while (kill_and_free_device (dev, 1) <= -1)
			{
				if (stio->stopreq) 
				{
					/* i can't wait until destruction attempt gets
					 * fully successful. there is a chance that some
					 * resources can leak inside the device */
					kill_and_free_device (dev, 2);
					break;
				}
			}
		}
	}
}

void stio_dev_halt (stio_dev_t* dev)
{
	if (!(dev->dev_capa & (STIO_DEV_CAPA_HALTED | STIO_DEV_CAPA_ZOMBIE)))
	{
		stio_t* stio;

		stio = dev->stio;

		/* delink the dev object from the device list */
		if (dev->dev_prev)
			dev->dev_prev->dev_next = dev->dev_next;
		else
			stio->dev.head = dev->dev_next;
		if (dev->dev_next)
			dev->dev_next->dev_prev = dev->dev_prev;
		else
			stio->dev.tail = dev->dev_prev;

		/* place it at the back of the halted device list */
		if (stio->hdev.tail) stio->hdev.tail->dev_next = dev;
		else stio->hdev.head = dev;
		dev->dev_prev = stio->hdev.tail;
		dev->dev_next = STIO_NULL;
		stio->hdev.tail = dev;

		dev->dev_capa |= STIO_DEV_CAPA_HALTED;
	}
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
	int dev_capa;

	/* the virtual device doesn't perform actual I/O.
	 * it's different from not hanving STIO_DEV_CAPA_IN and STIO_DEV_CAPA_OUT.
	 * a non-virtual device without the capabilities still gets attention
	 * of the system multiplexer for hangup and error. */
	if (dev->dev_capa & STIO_DEV_CAPA_VIRTUAL) return 0;

	ev.data.ptr = dev;
	switch (cmd)
	{
		case STIO_DEV_WATCH_START:
			/* upon start, only input watching is requested */
			events = STIO_DEV_EVENT_IN; 
			epoll_op = EPOLL_CTL_ADD;
			break;

		case STIO_DEV_WATCH_RENEW:
			/* auto-renwal mode. input watching is requested all the time.
			 * output watching is requested only if there're enqueued 
			 * data for writing. */
			events = STIO_DEV_EVENT_IN;
			if (!STIO_WQ_ISEMPTY(&dev->wq)) events |= STIO_DEV_EVENT_OUT;
		case STIO_DEV_WATCH_UPDATE:
			/* honor event watching requests as given by the caller */
			epoll_op = EPOLL_CTL_MOD;
			break;

		case STIO_DEV_WATCH_STOP:
			events = 0; /* override events */
			epoll_op = EPOLL_CTL_DEL;
			break;

		default:
			dev->stio->errnum = STIO_EINVAL;
			return -1;
	}

	dev_capa = dev->dev_capa;
	dev_capa &= ~(DEV_CAPA_ALL_WATCHED);

	/* this function honors STIO_DEV_EVENT_IN and STIO_DEV_EVENT_OUT only
	 * as valid input event bits. it intends to provide simple abstraction
	 * by reducing the variety of event bits that the caller has to handle. */
	ev.events = EPOLLHUP | EPOLLERR /*| EPOLLET*/;

	if ((events & STIO_DEV_EVENT_IN) && !(dev->dev_capa & (STIO_DEV_CAPA_IN_CLOSED | STIO_DEV_CAPA_IN_DISABLED)))
	{
		if (dev->dev_capa & STIO_DEV_CAPA_IN) 
		{
			ev.events |= EPOLLIN;
		#if defined(EPOLLRDHUP)
			ev.events |= EPOLLRDHUP;
		#endif
			if (dev->dev_capa & STIO_DEV_CAPA_PRI) 
			{
				ev.events |= EPOLLPRI;
				dev_capa |= STIO_DEV_CAPA_PRI_WATCHED;
			}

			dev_capa |= STIO_DEV_CAPA_IN_WATCHED;
		}
	}

	if ((events & STIO_DEV_EVENT_OUT) && !(dev->dev_capa & STIO_DEV_CAPA_OUT_CLOSED))
	{
		if (dev->dev_capa & STIO_DEV_CAPA_OUT) 
		{
			ev.events |= EPOLLOUT;
			dev_capa |= STIO_DEV_CAPA_OUT_WATCHED;
		}
	}

	if (epoll_op == EPOLL_CTL_MOD && (dev_capa & DEV_CAPA_ALL_WATCHED) == (dev->dev_capa & DEV_CAPA_ALL_WATCHED))
	{
		/* skip calling epoll_ctl */
	}
	else
	{
printf ("MODING cmd=%d %d\n", (int)cmd, (int)dev->dev_mth->getsyshnd(dev));
		if (epoll_ctl (dev->stio->mux, epoll_op, dev->dev_mth->getsyshnd(dev), &ev) == -1)
		{
			dev->stio->errnum = stio_syserrtoerrnum(errno);
			return -1;
		}
	}

	dev->dev_capa = dev_capa;
	return 0;
}

int stio_dev_read (stio_dev_t* dev, int enabled)
{
	if (dev->dev_capa & STIO_DEV_CAPA_IN_CLOSED)
	{
		dev->stio->errnum = STIO_ENOCAPA;
		return -1;
	}

	if (enabled)
	{
		dev->dev_capa &= ~STIO_DEV_CAPA_IN_DISABLED;
		if (!dev->stio->in_exec && (dev->dev_capa & STIO_DEV_CAPA_IN_WATCHED)) goto renew_watch_now;
	}
	else
	{
		dev->dev_capa |= STIO_DEV_CAPA_IN_DISABLED;
		if (!dev->stio->in_exec && !(dev->dev_capa & STIO_DEV_CAPA_IN_WATCHED)) goto renew_watch_now;
	}

	dev->stio->renew_watch = 1;
	return 0;

renew_watch_now:
	if (stio_dev_watch (dev, STIO_DEV_WATCH_RENEW, 0) <= -1) return -1;
	return 0;
}

static void on_write_timeout (stio_t* stio, const stio_ntime_t* now, stio_tmrjob_t* job)
{
	stio_wq_t* q;
	stio_dev_t* dev;
	int x;

	q = (stio_wq_t*)job->ctx;
	dev = q->dev;

	dev->stio->errnum = STIO_ETMOUT;
	x = dev->dev_evcb->on_write (dev, -1, q->ctx, &q->dstadr); 

	STIO_ASSERT (q->tmridx == STIO_TMRIDX_INVALID);
	STIO_WQ_UNLINK(q);
	STIO_MMGR_FREE (stio->mmgr, q);

	if (x <= -1) stio_dev_halt (dev);
}

static int __dev_write (stio_dev_t* dev, const void* data, stio_iolen_t len, const stio_ntime_t* tmout, void* wrctx, const stio_devadr_t* dstadr)
{
	const stio_uint8_t* uptr;
	stio_iolen_t urem, ulen;
	stio_wq_t* q;
	int x;

	if (dev->dev_capa & STIO_DEV_CAPA_OUT_CLOSED)
	{
		dev->stio->errnum = STIO_ENOCAPA;
		return -1;
	}

	uptr = data;
	urem = len;

	if (!STIO_WQ_ISEMPTY(&dev->wq)) 
	{
		/* the writing queue is not empty. 
		 * enqueue this request immediately */
		goto enqueue_data;
	}

	if (dev->dev_capa & STIO_DEV_CAPA_STREAM)
	{
		/* use the do..while() loop to be able to send a zero-length data */
		do
		{
			ulen = urem;
			x = dev->dev_mth->write (dev, data, &ulen, dstadr);
			if (x <= -1) return -1;
			else if (x == 0) 
			{
				/* [NOTE] 
				 * the write queue is empty at this moment. a zero-length 
				 * request for a stream device can still get enqueued  if the
				 * write callback returns 0 though i can't figure out if there
				 * is a compelling reason to do so 
				 */
				goto enqueue_data; /* enqueue remaining data */
			}
			else 
			{
				urem -= ulen;
				uptr += ulen;
			}
		}
		while (urem > 0);

		if (len <= 0) /* original length */
		{
			/* a zero-length writing request is to close the writing end */
			dev->dev_capa |= STIO_DEV_CAPA_OUT_CLOSED;
		}

		if (dev->dev_evcb->on_write (dev, len, wrctx, dstadr) <= -1) return -1;
	}
	else
	{
		ulen = urem;

		x = dev->dev_mth->write (dev, data, &ulen, dstadr);
		if (x <= -1) return -1;
		else if (x == 0) goto enqueue_data;

		/* partial writing is still considered ok for a non-stream device */
		if (dev->dev_evcb->on_write (dev, ulen, wrctx, dstadr) <= -1) return -1;
	}

	return 1; /* written immediately and called on_write callback */

enqueue_data:
	if (!(dev->dev_capa & STIO_DEV_CAPA_OUT_QUEUED)) 
	{
		/* writing queuing is not requested. so return failure */
		return -1;
	}

	/* queue the remaining data*/
	q = (stio_wq_t*)STIO_MMGR_ALLOC (dev->stio->mmgr, STIO_SIZEOF(*q) + (dstadr? dstadr->len: 0) + urem);
	if (!q)
	{
		dev->stio->errnum = STIO_ENOMEM;
		return -1;
	}

	q->tmridx = STIO_TMRIDX_INVALID;
	q->dev = dev;
	q->ctx = wrctx;

	if (dstadr)
	{
		q->dstadr.ptr = (stio_uint8_t*)(q + 1);
		q->dstadr.len = dstadr->len;
		STIO_MEMCPY (q->dstadr.ptr, dstadr->ptr, dstadr->len);
	}
	else
	{
		q->dstadr.len = 0;
	}

	q->ptr = (stio_uint8_t*)(q + 1) + q->dstadr.len;
	q->len = urem;
	q->olen = len;
	STIO_MEMCPY (q->ptr, uptr, urem);


	if (tmout && !stio_isnegtime(tmout))
	{
		stio_tmrjob_t tmrjob;

		STIO_MEMSET (&tmrjob, 0, STIO_SIZEOF(tmrjob));
		tmrjob.ctx = q;
		stio_gettime (&tmrjob.when);
		stio_addtime (&tmrjob.when, tmout, &tmrjob.when);
		tmrjob.handler = on_write_timeout;
		tmrjob.idxptr = &q->tmridx;

		q->tmridx = stio_instmrjob (dev->stio, &tmrjob);
		if (q->tmridx == STIO_TMRIDX_INVALID) 
		{
			STIO_MMGR_FREE (dev->stio->mmgr, q);
			return -1;
		}
	}

	STIO_WQ_ENQ (&dev->wq, q);
	if (!dev->stio->in_exec && !(dev->dev_capa & STIO_DEV_CAPA_OUT_WATCHED))
	{
		/* if output is not being watched, arrange to do so */
		if (stio_dev_watch (dev, STIO_DEV_WATCH_RENEW, 0) <= -1)
		{
			unlink_wq (dev->stio, q);
			STIO_MMGR_FREE (dev->stio->mmgr, q);
			return -1;
		}
	}
	else
	{
		dev->stio->renew_watch = 1;
	}

	return 0; /* request pused to a write queue. */
}

int stio_dev_write (stio_dev_t* dev, const void* data, stio_iolen_t len, void* wrctx, const stio_devadr_t* dstadr)
{
	return __dev_write (dev, data, len, STIO_NULL, wrctx, dstadr);
}

int stio_dev_timedwrite (stio_dev_t* dev, const void* data, stio_iolen_t len, const stio_ntime_t* tmout, void* wrctx, const stio_devadr_t* dstadr)
{
	return __dev_write (dev, data, len, tmout, wrctx, dstadr);
}

int stio_makesyshndasync (stio_t* stio, stio_syshnd_t hnd)
{
#if defined(F_GETFL) && defined(F_SETFL) && defined(O_NONBLOCK)
	int flags;

	if ((flags = fcntl (hnd, F_GETFL)) <= -1 ||
	    (flags = fcntl (hnd, F_SETFL, flags | O_NONBLOCK)) <= -1)
	{
		stio->errnum = stio_syserrtoerrnum (errno);
		return -1;
	}

	return 0;
#else
	stio->errnum = STIO_ENOSUP;
	return -1;
#endif
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

	#if defined(EAGAIN)
		case EAGAIN:
			return STIO_EAGAIN;
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

stio_mchar_t* stio_mbsdup (stio_t* stio, const stio_mchar_t* src)
{
	stio_mchar_t* dst;
	stio_size_t len;

	dst = (stio_mchar_t*)src;
	while (*dst != STIO_MT('\0')) dst++;
	len = dst - src;

	dst = STIO_MMGR_ALLOC (stio->mmgr, (len + 1) * STIO_SIZEOF(*src));
	if (!dst)
	{
		stio->errnum = STIO_ENOMEM;
		return STIO_NULL;
	}

	STIO_MEMCPY (dst, src, (len + 1) * STIO_SIZEOF(*src));
	return dst;
}

stio_size_t stio_mbscpy (stio_mchar_t* buf, const stio_mchar_t* str)
{
	stio_mchar_t* org = buf;
	while ((*buf++ = *str++) != STIO_MT('\0'));
	return buf - org - 1;
}

int stio_mbsspltrn (
	stio_mchar_t* s, const stio_mchar_t* delim,
	stio_mchar_t lquote, stio_mchar_t rquote, 
	stio_mchar_t escape, const stio_mchar_t* trset)
{
	stio_mchar_t* p = s, *d;
	stio_mchar_t* sp = STIO_NULL, * ep = STIO_NULL;
	int delim_mode;
	int cnt = 0;

	if (delim == STIO_NULL) delim_mode = 0;
	else 
	{
		delim_mode = 1;
		for (d = (stio_mchar_t*)delim; *d != STIO_MT('\0'); d++)
			if (!IS_MSPACE(*d)) delim_mode = 2;
	}

	if (delim_mode == 0) 
	{
		/* skip preceding space characters */
		while (IS_MSPACE(*p)) p++;

		/* when 0 is given as "delim", it has an effect of cutting
		   preceding and trailing space characters off "s". */
		if (lquote != STIO_MT('\0') && *p == lquote) 
		{
			stio_mbscpy (p, p + 1);

			for (;;) 
			{
				if (*p == STIO_MT('\0')) return -1;

				if (escape != STIO_MT('\0') && *p == escape) 
				{
					if (trset != STIO_NULL && p[1] != STIO_MT('\0'))
					{
						const stio_mchar_t* ep = trset;
						while (*ep != STIO_MT('\0'))
						{
							if (p[1] == *ep++) 
							{
								p[1] = *ep;
								break;
							}
						}
					}

					stio_mbscpy (p, p + 1);
				}
				else 
				{
					if (*p == rquote) 
					{
						p++;
						break;
					}
				}

				if (sp == 0) sp = p;
				ep = p;
				p++;
			}
			while (IS_MSPACE(*p)) p++;
			if (*p != STIO_MT('\0')) return -1;

			if (sp == 0 && ep == 0) s[0] = STIO_MT('\0');
			else 
			{
				ep[1] = STIO_MT('\0');
				if (s != (stio_mchar_t*)sp) stio_mbscpy (s, sp);
				cnt++;
			}
		}
		else 
		{
			while (*p) 
			{
				if (!IS_MSPACE(*p)) 
				{
					if (sp == 0) sp = p;
					ep = p;
				}
				p++;
			}

			if (sp == 0 && ep == 0) s[0] = STIO_MT('\0');
			else 
			{
				ep[1] = STIO_MT('\0');
				if (s != (stio_mchar_t*)sp) stio_mbscpy (s, sp);
				cnt++;
			}
		}
	}
	else if (delim_mode == 1) 
	{
		stio_mchar_t* o;

		while (*p) 
		{
			o = p;
			while (IS_MSPACE(*p)) p++;
			if (o != p) { stio_mbscpy (o, p); p = o; }

			if (lquote != STIO_MT('\0') && *p == lquote) 
			{
				stio_mbscpy (p, p + 1);

				for (;;) 
				{
					if (*p == STIO_MT('\0')) return -1;

					if (escape != STIO_MT('\0') && *p == escape) 
					{
						if (trset != STIO_NULL && p[1] != STIO_MT('\0'))
						{
							const stio_mchar_t* ep = trset;
							while (*ep != STIO_MT('\0'))
							{
								if (p[1] == *ep++) 
								{
									p[1] = *ep;
									break;
								}
							}
						}
						stio_mbscpy (p, p + 1);
					}
					else 
					{
						if (*p == rquote) 
						{
							*p++ = STIO_MT('\0');
							cnt++;
							break;
						}
					}
					p++;
				}
			}
			else 
			{
				o = p;
				for (;;) 
				{
					if (*p == STIO_MT('\0')) 
					{
						if (o != p) cnt++;
						break;
					}
					if (IS_MSPACE (*p)) 
					{
						*p++ = STIO_MT('\0');
						cnt++;
						break;
					}
					p++;
				}
			}
		}
	}
	else /* if (delim_mode == 2) */
	{
		stio_mchar_t* o;
		int ok;

		while (*p != STIO_MT('\0')) 
		{
			o = p;
			while (IS_MSPACE(*p)) p++;
			if (o != p) { stio_mbscpy (o, p); p = o; }

			if (lquote != STIO_MT('\0') && *p == lquote) 
			{
				stio_mbscpy (p, p + 1);

				for (;;) 
				{
					if (*p == STIO_MT('\0')) return -1;

					if (escape != STIO_MT('\0') && *p == escape) 
					{
						if (trset != STIO_NULL && p[1] != STIO_MT('\0'))
						{
							const stio_mchar_t* ep = trset;
							while (*ep != STIO_MT('\0'))
							{
								if (p[1] == *ep++) 
								{
									p[1] = *ep;
									break;
								}
							}
						}

						stio_mbscpy (p, p + 1);
					}
					else 
					{
						if (*p == rquote) 
						{
							*p++ = STIO_MT('\0');
							cnt++;
							break;
						}
					}
					p++;
				}

				ok = 0;
				while (IS_MSPACE(*p)) p++;
				if (*p == STIO_MT('\0')) ok = 1;
				for (d = (stio_mchar_t*)delim; *d != STIO_MT('\0'); d++) 
				{
					if (*p == *d) 
					{
						ok = 1;
						stio_mbscpy (p, p + 1);
						break;
					}
				}
				if (ok == 0) return -1;
			}
			else 
			{
				o = p; sp = ep = 0;

				for (;;) 
				{
					if (*p == STIO_MT('\0')) 
					{
						if (ep) 
						{
							ep[1] = STIO_MT('\0');
							p = &ep[1];
						}
						cnt++;
						break;
					}
					for (d = (stio_mchar_t*)delim; *d != STIO_MT('\0'); d++) 
					{
						if (*p == *d)  
						{
							if (sp == STIO_NULL) 
							{
								stio_mbscpy (o, p); p = o;
								*p++ = STIO_MT('\0');
							}
							else 
							{
								stio_mbscpy (&ep[1], p);
								stio_mbscpy (o, sp);
								o[ep - sp + 1] = STIO_MT('\0');
								p = &o[ep - sp + 2];
							}
							cnt++;
							/* last empty field after delim */
							if (*p == STIO_MT('\0')) cnt++;
							goto exit_point;
						}
					}

					if (!IS_MSPACE (*p)) 
					{
						if (sp == STIO_NULL) sp = p;
						ep = p;
					}
					p++;
				}
exit_point:
				;
			}
		}
	}

	return cnt;
}

int stio_mbsspl (
	stio_mchar_t* s, const stio_mchar_t* delim,
	stio_mchar_t lquote, stio_mchar_t rquote, stio_mchar_t escape)
{
	return stio_mbsspltrn (s, delim, lquote, rquote, escape, STIO_NULL);
}
