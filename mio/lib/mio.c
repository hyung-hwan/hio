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

#include "mio-prv.h"
  
#if defined(HAVE_SYS_EPOLL_H)
#	include <sys/epoll.h>
#	define USE_EPOLL
#elif defined(HAVE_SYS_POLL_H)
#	include <sys/poll.h>
#	define USE_POLL
#else
#	error NO SUPPORTED MULTIPLEXER
#endif

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#define DEV_CAPA_ALL_WATCHED (MIO_DEV_CAPA_IN_WATCHED | MIO_DEV_CAPA_OUT_WATCHED | MIO_DEV_CAPA_PRI_WATCHED)

static int schedule_kill_zombie_job (mio_dev_t* dev);
static int kill_and_free_device (mio_dev_t* dev, int force);

#define APPEND_DEVICE_TO_LIST(list,dev) do { \
	if ((list)->tail) (list)->tail->dev_next = (dev); \
	else (list)->head = (dev); \
	(dev)->dev_prev = (list)->tail; \
	(dev)->dev_next = MIO_NULL; \
	(list)->tail = (dev); \
} while(0)

#define UNLINK_DEVICE_FROM_LIST(list,dev) do { \
	if ((dev)->dev_prev) (dev)->dev_prev->dev_next = (dev)->dev_next; \
	else (list)->head = (dev)->dev_next; \
	if ((dev)->dev_next) (dev)->dev_next->dev_prev = (dev)->dev_prev; \
	else (list)->tail = (dev)->dev_prev; \
} while (0)



/* ========================================================================= */
#if defined(USE_POLL)

#define MUX_CMD_INSERT 1
#define MUX_CMD_UPDATE 2
#define MUX_CMD_DELETE 3

#define MUX_INDEX_INVALID MIO_TYPE_MAX(mio_oow_t)

struct mio_mux_t
{
	struct
	{
		mio_oow_t* ptr;
		mio_oow_t  size;
		mio_oow_t  capa;
	} map; /* handle to index */

	struct
	{
		struct pollfd* pfd;
		mio_dev_t** dptr;
		mio_oow_t size;
		mio_oow_t capa;
	} pd; /* poll data */
};


static int mux_open (mio_t* mio)
{
	mio_mux_t* mux;

	mux = MIO_MMGR_ALLOC (mio->mmgr, MIO_SIZEOF(*mux));
	if (!mux)
	{
		mio->errnum = MIO_ENOMEM;
		return -1;
	}

	MIO_MEMSET (mux, 0, MIO_SIZEOF(*mux));

	mio->mux = mux;
	return 0;
}

static void mux_close (mio_t* mio)
{
	if (mio->mux)
	{
		MIO_MMGR_FREE (mio->mmgr, mio->mux);
		mio->mux = MIO_NULL;
	}
}

static int mux_control (mio_dev_t* dev, int cmd, mio_syshnd_t hnd, int dev_capa)
{
	mio_t* mio;
	mio_mux_t* mux;
	mio_oow_t idx;

	mio = dev->mio;
	mux = (mio_mux_t*)mio->mux;

	if (hnd >= mux->map.capa)
	{
		mio_oow_t new_capa;
		mio_oow_t* tmp;

		if (cmd != MUX_CMD_INSERT)
		{
			mio->errnum = MIO_ENOENT;
			return -1;
		}

		new_capa = MIO_ALIGN_POW2((hnd + 1), 256);

		tmp = MIO_MMGR_REALLOC(mio->mmgr, mux->map.ptr, new_capa * MIO_SIZEOF(*tmp));
		if (!tmp)
		{
			mio->errnum = MIO_ENOMEM;
			return -1;
		}

		for (idx = mux->map.capa; idx < new_capa; idx++) 
			tmp[idx] = MUX_INDEX_INVALID;

		mux->map.ptr = tmp;
		mux->map.capa = new_capa;
	}

	idx = mux->map.ptr[hnd];
	if (idx != MUX_INDEX_INVALID)
	{
		if (cmd == MUX_CMD_INSERT)
		{
			mio->errnum = MIO_EEXIST;
			return -1;
		}
	}
	else
	{
		if (cmd != MUX_CMD_INSERT)
		{
			mio->errnum = MIO_ENOENT;
			return -1;
		}
	}

	switch (cmd)
	{
		case MUX_CMD_INSERT:

			if (mux->pd.size >= mux->pd.capa)
			{
				mio_oow_t new_capa;
				struct pollfd* tmp1;
				mio_dev_t** tmp2;

				new_capa = MIO_ALIGN_POW2(mux->pd.size + 1, 256);

				tmp1 = MIO_MMGR_REALLOC(mio->mmgr, mux->pd.pfd, new_capa * MIO_SIZEOF(*tmp1));
				if (!tmp1)
				{
					mio->errnum = MIO_ENOMEM;
					return -1;
				}

				tmp2 = MIO_MMGR_REALLOC (mio->mmgr, mux->pd.dptr, new_capa * MIO_SIZEOF(*tmp2));
				if (!tmp2)
				{
					MIO_MMGR_FREE (mio->mmgr, tmp1);
					mio->errnum = MIO_ENOMEM;
					return -1;
				}

				mux->pd.pfd = tmp1;
				mux->pd.dptr = tmp2;
				mux->pd.capa = new_capa;
			}

			idx = mux->pd.size++;

			mux->pd.pfd[idx].fd = hnd;
			mux->pd.pfd[idx].events = 0;
			if (dev_capa & MIO_DEV_CAPA_IN_WATCHED) mux->pd.pfd[idx].events |= POLLIN;
			if (dev_capa & MIO_DEV_CAPA_OUT_WATCHED) mux->pd.pfd[idx].events |= POLLOUT;
			mux->pd.pfd[idx].revents = 0;
			mux->pd.dptr[idx] = dev;

			mux->map.ptr[hnd] = idx;

			return 0;

		case MUX_CMD_UPDATE:
			MIO_ASSERT (mux->pd.dptr[idx] == dev);
			mux->pd.pfd[idx].events = 0;
			if (dev_capa & MIO_DEV_CAPA_IN_WATCHED) mux->pd.pfd[idx].events |= POLLIN;
			if (dev_capa & MIO_DEV_CAPA_OUT_WATCHED) mux->pd.pfd[idx].events |= POLLOUT;
			return 0;

		case MUX_CMD_DELETE:
			MIO_ASSERT (mux->pd.dptr[idx] == dev);
			mux->map.ptr[hnd] = MUX_INDEX_INVALID;

			/* TODO: speed up deletion. allow a hole in the array.
			 *       delay array compaction if there is a hole.
			 *       set fd for the hole to -1 such that poll()
			 *       ignores it. compact the array if another deletion 
			 *       is requested when there is an existing hole. */
			idx++;
			while (idx < mux->pd.size)
			{
				int fd;

				mux->pd.pfd[idx - 1] = mux->pd.pfd[idx];
				mux->pd.dptr[idx - 1] = mux->pd.dptr[idx];

				fd = mux->pd.pfd[idx].fd;
				mux->map.ptr[fd] = idx - 1;

				idx++;
			}

			mux->pd.size--;

			return 0;

		default:
			mio->errnum = MIO_EINVAL;
			return -1;
	}
}

#elif defined(USE_EPOLL)

#define MUX_CMD_INSERT EPOLL_CTL_ADD
#define MUX_CMD_UPDATE EPOLL_CTL_MOD
#define MUX_CMD_DELETE EPOLL_CTL_DEL

struct mio_mux_t
{
	int hnd;
	struct epoll_event revs[100]; /* TODO: is it a good size? */
};

static int mux_open (mio_t* mio)
{
	mio_mux_t* mux;

	mux = MIO_MMGR_ALLOC (mio->mmgr, MIO_SIZEOF(*mux));
	if (!mux)
	{
		mio->errnum = MIO_ENOMEM;
		return -1;
	}

	MIO_MEMSET (mux, 0, MIO_SIZEOF(*mux));

	mux->hnd = epoll_create (1000);
	if (mux->hnd == -1)
	{
		mio->errnum = mio_syserrtoerrnum(errno);
		MIO_MMGR_FREE (mio->mmgr, mux);
		return -1;
	}

	mio->mux = mux;
	return 0;
}

static void mux_close (mio_t* mio)
{
	if (mio->mux)
	{
		close (mio->mux->hnd);
		MIO_MMGR_FREE (mio->mmgr, mio->mux);
		mio->mux = MIO_NULL;
	}
}


static MIO_INLINE int mux_control (mio_dev_t* dev, int cmd, mio_syshnd_t hnd, int dev_capa)
{
	struct epoll_event ev;

	ev.data.ptr = dev;
	ev.events = EPOLLHUP | EPOLLERR /*| EPOLLET*/;

	if (dev_capa & MIO_DEV_CAPA_IN_WATCHED) 
	{
		ev.events |= EPOLLIN;
	#if defined(EPOLLRDHUP)
		ev.events |= EPOLLRDHUP;
	#endif
		if (dev_capa & MIO_DEV_CAPA_PRI_WATCHED) ev.events |= EPOLLPRI;
	}

	if (dev_capa & MIO_DEV_CAPA_OUT_WATCHED) ev.events |= EPOLLOUT;

	if (epoll_ctl (dev->mio->mux->hnd, cmd, hnd, &ev) == -1)
	{
		dev->mio->errnum = mio_syserrtoerrnum(errno);
		return -1;
	}

	return 0;
}
#endif

/* ========================================================================= */

mio_t* mio_open (mio_mmgr_t* mmgr, mio_oow_t xtnsize, mio_oow_t tmrcapa, mio_errnum_t* errnum)
{
	mio_t* mio;

	mio = MIO_MMGR_ALLOC (mmgr, MIO_SIZEOF(mio_t) + xtnsize);
	if (mio)
	{
		if (mio_init (mio, mmgr, tmrcapa) <= -1)
		{
			if (errnum) *errnum = mio->errnum;
			MIO_MMGR_FREE (mmgr, mio);
			mio = MIO_NULL;
		}
		else MIO_MEMSET (mio + 1, 0, xtnsize);
	}
	else
	{
		if (errnum) *errnum = MIO_ENOMEM;
	}

	return mio;
}

void mio_close (mio_t* mio)
{
	mio_fini (mio);
	MIO_MMGR_FREE (mio->mmgr, mio);
}

int mio_init (mio_t* mio, mio_mmgr_t* mmgr, mio_oow_t tmrcapa)
{
	MIO_MEMSET (mio, 0, MIO_SIZEOF(*mio));
	mio->mmgr = mmgr;

	/* intialize the multiplexer object */

	if (mux_open (mio) <= -1) return -1;

	/* initialize the timer object */
	if (tmrcapa <= 0) tmrcapa = 1;
	mio->tmr.jobs = MIO_MMGR_ALLOC (mio->mmgr, tmrcapa * MIO_SIZEOF(mio_tmrjob_t));
	if (!mio->tmr.jobs) 
	{
		mio->errnum = MIO_ENOMEM;
		mux_close (mio);
		return -1;
	}
	mio->tmr.capa = tmrcapa;

	return 0;
}

void mio_fini (mio_t* mio)
{
	mio_dev_t* dev, * next_dev;
	struct
	{
		mio_dev_t* head;
		mio_dev_t* tail;
	} diehard;

	/* kill all registered devices */
	while (mio->actdev.head)
	{
		mio_killdev (mio, mio->actdev.head);
	}

	while (mio->hltdev.head)
	{
		mio_killdev (mio, mio->hltdev.head);
	}

	/* clean up all zombie devices */
	MIO_MEMSET (&diehard, 0, MIO_SIZEOF(diehard));
	for (dev = mio->zmbdev.head; dev; )
	{
		kill_and_free_device (dev, 1);
		if (mio->zmbdev.head == dev) 
		{
			/* the deive has not been freed. go on to the next one */
			next_dev = dev->dev_next;

			/* remove the device from the zombie device list */
			UNLINK_DEVICE_FROM_LIST (&mio->zmbdev, dev);
			dev->dev_capa &= ~MIO_DEV_CAPA_ZOMBIE;

			/* put it to a private list for aborting */
			APPEND_DEVICE_TO_LIST (&diehard, dev);

			dev = next_dev;
		}
		else dev = mio->zmbdev.head;
	}

	while (diehard.head)
	{
		/* if the kill method returns failure, it can leak some resource
		 * because the device is freed regardless of the failure when 2 
		 * is given to kill_and_free_device(). */
		dev = diehard.head;
		MIO_ASSERT (!(dev->dev_capa & (MIO_DEV_CAPA_ACTIVE | MIO_DEV_CAPA_HALTED | MIO_DEV_CAPA_ZOMBIE)));
		UNLINK_DEVICE_FROM_LIST (&diehard, dev);
		kill_and_free_device (dev, 2);
	}

	/* purge scheduled timer jobs and kill the timer */
	mio_cleartmrjobs (mio);
	MIO_MMGR_FREE (mio->mmgr, mio->tmr.jobs);

	/* close the multiplexer */
	mux_close (mio);
}


int mio_prologue (mio_t* mio)
{
	/* TODO: */
	return 0;
}

void mio_epilogue (mio_t* mio)
{
	/* TODO: */
}

static MIO_INLINE void unlink_wq (mio_t* mio, mio_wq_t* q)
{
	if (q->tmridx != MIO_TMRIDX_INVALID)
	{
		mio_deltmrjob (mio, q->tmridx);
		MIO_ASSERT (q->tmridx == MIO_TMRIDX_INVALID);
	}
	MIO_WQ_UNLINK (q);
}

static MIO_INLINE void handle_event (mio_dev_t* dev, int events, int rdhup)
{
	mio_t* mio;

	mio = dev->mio;
	mio->renew_watch = 0;

	MIO_ASSERT (mio == dev->mio);

	if (dev->dev_evcb->ready)
	{
		int x, xevents;

		xevents = events;
		if (rdhup) xevents |= MIO_DEV_EVENT_HUP;

		/* return value of ready()
		 *   <= -1 - failure. kill the device.
		 *   == 0 - ok. but don't invoke recv() or send().
		 *   >= 1 - everything is ok. */
		x = dev->dev_evcb->ready (dev, xevents);
		if (x <= -1)
		{
			mio_dev_halt (dev);
			return;
		}
		else if (x == 0) goto skip_evcb;
	}

	if (dev && (events & MIO_DEV_EVENT_PRI))
	{
		/* urgent data */
		/* TODO: implement urgent data handling */
		/*x = dev->dev_mth->urgread (dev, mio->bugbuf, &len);*/
	}

	if (dev && (events & MIO_DEV_EVENT_OUT))
	{
		/* write pending requests */
		while (!MIO_WQ_ISEMPTY(&dev->wq))
		{
			mio_wq_t* q;
			const mio_uint8_t* uptr;
			mio_iolen_t urem, ulen;
			int x;

			q = MIO_WQ_HEAD(&dev->wq);

			uptr = q->ptr;
			urem = q->len;

		send_leftover:
			ulen = urem;
			x = dev->dev_mth->write (dev, uptr, &ulen, &q->dstaddr);
			if (x <= -1)
			{
				mio_dev_halt (dev);
				dev = MIO_NULL;
				break;
			}
			else if (x == 0)
			{
				/* keep the left-over */
				MIO_MEMMOVE (q->ptr, uptr, urem);
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

					if (q->len <= 0 && (dev->dev_capa & MIO_DEV_CAPA_STREAM)) 
					{
						/* it was a zero-length write request. 
						 * for a stream, it is to close the output. */
						dev->dev_capa |= MIO_DEV_CAPA_OUT_CLOSED;
						mio->renew_watch = 1;
						out_closed = 1;
					}

					unlink_wq (mio, q);
					y = dev->dev_evcb->on_write (dev, q->olen, q->ctx, &q->dstaddr);
					MIO_MMGR_FREE (mio->mmgr, q);

					if (y <= -1)
					{
						mio_dev_halt (dev);
						dev = MIO_NULL;
						break;
					}

					if (out_closed)
					{
						/* drain all pending requests. 
						 * callbacks are skipped for drained requests */
						while (!MIO_WQ_ISEMPTY(&dev->wq))
						{
							q = MIO_WQ_HEAD(&dev->wq);
							unlink_wq (mio, q);
							MIO_MMGR_FREE (dev->mio->mmgr, q);
						}
						break;
					}
				}
				else goto send_leftover;
			}
		}

		if (dev && MIO_WQ_ISEMPTY(&dev->wq))
		{
			/* no pending request to write */
			if ((dev->dev_capa & MIO_DEV_CAPA_IN_CLOSED) &&
			    (dev->dev_capa & MIO_DEV_CAPA_OUT_CLOSED))
			{
				mio_dev_halt (dev);
				dev = MIO_NULL;
			}
			else
			{
				mio->renew_watch = 1;
			}
		}
	}

	if (dev && (events & MIO_DEV_EVENT_IN))
	{
		mio_devaddr_t srcaddr;
		mio_iolen_t len;
		int x;

		/* the devices are all non-blocking. read as much as possible
		 * if on_read callback returns 1 or greater. read only once
		 * if the on_read calllback returns 0. */
		while (1)
		{
			len = MIO_COUNTOF(mio->bigbuf);
			x = dev->dev_mth->read (dev, mio->bigbuf, &len, &srcaddr);
			if (x <= -1)
			{
				mio_dev_halt (dev);
				dev = MIO_NULL;
				break;
			}
			else if (x == 0)
			{
				/* no data is available - EWOULDBLOCK or something similar */
				break;
			}
			else if (x >= 1)
			{
				if (len <= 0 && (dev->dev_capa & MIO_DEV_CAPA_STREAM)) 
				{
					/* EOF received. for a stream device, a zero-length 
					 * read is interpreted as EOF. */
					dev->dev_capa |= MIO_DEV_CAPA_IN_CLOSED;
					mio->renew_watch = 1;

					/* call the on_read callback to report EOF */
					if (dev->dev_evcb->on_read (dev, mio->bigbuf, len, &srcaddr) <= -1 ||
					    (dev->dev_capa & MIO_DEV_CAPA_OUT_CLOSED))
					{
						/* 1. input ended and its reporting failed or 
						 * 2. input ended and no writing is possible */
						mio_dev_halt (dev);
						dev = MIO_NULL;
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
					y = dev->dev_evcb->on_read (dev, mio->bigbuf, len, &srcaddr);
					if (y <= -1)
					{
						mio_dev_halt (dev);
						dev = MIO_NULL;
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
		if (events & (MIO_DEV_EVENT_ERR | MIO_DEV_EVENT_HUP))
		{ 
			/* if error or hangup has been reported on the device,
			 * halt the device. this check is performed after
			 * EPOLLIN or EPOLLOUT check because EPOLLERR or EPOLLHUP
			 * can be set together with EPOLLIN or EPOLLOUT. */
			dev->dev_capa |= MIO_DEV_CAPA_IN_CLOSED | MIO_DEV_CAPA_OUT_CLOSED;
			mio->renew_watch = 1;
		}
		else if (dev && rdhup) 
		{
			if (events & (MIO_DEV_EVENT_IN | MIO_DEV_EVENT_OUT | MIO_DEV_EVENT_PRI))
			{
				/* it may be a half-open state. don't do anything here
				 * to let the next read detect EOF */
			}
			else
			{
				dev->dev_capa |= MIO_DEV_CAPA_IN_CLOSED | MIO_DEV_CAPA_OUT_CLOSED;
				mio->renew_watch = 1;
			}
		}

		if ((dev->dev_capa & MIO_DEV_CAPA_IN_CLOSED) &&
		    (dev->dev_capa & MIO_DEV_CAPA_OUT_CLOSED))
		{
			mio_dev_halt (dev);
			dev = MIO_NULL;
		}
	}

skip_evcb:
	if (dev && mio->renew_watch && mio_dev_watch (dev, MIO_DEV_WATCH_RENEW, 0) <= -1)
	{
		mio_dev_halt (dev);
		dev = MIO_NULL;
	}
}

static MIO_INLINE int __exec (mio_t* mio)
{
	mio_ntime_t tmout;

#if defined(_WIN32)
	ULONG nentries, i;
#else
	int nentries, i;
	mio_mux_t* mux;
#endif

	/*if (!mio->actdev.head) return 0;*/

	/* execute the scheduled jobs before checking devices with the 
	 * multiplexer. the scheduled jobs can safely destroy the devices */
	mio_firetmrjobs (mio, MIO_NULL, MIO_NULL);

	if (mio_gettmrtmout (mio, MIO_NULL, &tmout) <= -1)
	{
		/* defaults to 1 second if timeout can't be acquired */
		tmout.sec = 1; /* TODO: make the default timeout configurable */
		tmout.nsec = 0;
	}

#if defined(_WIN32)
/*
	if (GetQueuedCompletionStatusEx (mio->iocp, mio->ovls, MIO_COUNTOF(mio->ovls), &nentries, timeout, FALSE) == FALSE)
	{
		// TODO: set errnum 
		return -1;
	}

	for (i = 0; i < nentries; i++)
	{
	}
*/
#elif defined(USE_POLL)

	mux = (mio_mux_t*)mio->mux;

	nentries = poll (mux->pd.pfd, mux->pd.size, MIO_SECNSEC_TO_MSEC(tmout.sec, tmout.nsec));
	if (nentries == -1)
	{
		if (errno == EINTR) return 0;
		mio->errnum = mio_syserrtoerrnum(errno);
		return -1;
	}

	for (i = 0; i < mux->pd.size; i++)
	{
		if (mux->pd.pfd[i].fd >= 0 && mux->pd.pfd[i].revents)
		{
			int events = 0;
			mio_dev_t* dev;

			dev = mux->pd.dptr[i];

			MIO_ASSERT (!(mux->pd.pfd[i].revents & POLLNVAL));
			if (mux->pd.pfd[i].revents & POLLIN) events |= MIO_DEV_EVENT_IN;
			if (mux->pd.pfd[i].revents & POLLOUT) events |= MIO_DEV_EVENT_OUT;
			if (mux->pd.pfd[i].revents & POLLPRI) events |= MIO_DEV_EVENT_PRI;
			if (mux->pd.pfd[i].revents & POLLERR) events |= MIO_DEV_EVENT_ERR;
			if (mux->pd.pfd[i].revents & POLLHUP) events |= MIO_DEV_EVENT_HUP;

			handle_event (dev, events, 0);
		}
	}

#elif defined(USE_EPOLL)

	mux = (mio_mux_t*)mio->mux;

	nentries = epoll_wait (mux->hnd, mux->revs, MIO_COUNTOF(mux->revs), MIO_SECNSEC_TO_MSEC(tmout.sec, tmout.nsec));
	if (nentries == -1)
	{
		if (errno == EINTR) return 0; /* it's actually ok */
		/* other errors are critical - EBADF, EFAULT, EINVAL */
		mio->errnum = mio_syserrtoerrnum(errno);
		return -1;
	}

	/* TODO: merge events??? for the same descriptor */
	
	for (i = 0; i < nentries; i++)
	{
		int events = 0, rdhup = 0;
		mio_dev_t* dev;

		dev = mux->revs[i].data.ptr;

		if (mux->revs[i].events & EPOLLIN) events |= MIO_DEV_EVENT_IN;
		if (mux->revs[i].events & EPOLLOUT) events |= MIO_DEV_EVENT_OUT;
		if (mux->revs[i].events & EPOLLPRI) events |= MIO_DEV_EVENT_PRI;
		if (mux->revs[i].events & EPOLLERR) events |= MIO_DEV_EVENT_ERR;
		if (mux->revs[i].events & EPOLLHUP) events |= MIO_DEV_EVENT_HUP;
	#if defined(EPOLLRDHUP)
		else if (mux->revs[i].events & EPOLLRDHUP) rdhup = 1;
	#endif
		handle_event (dev, events, rdhup);
	}

#else

#	error NO SUPPORTED MULTIPLEXER
#endif

	/* kill all halted devices */
	while (mio->hltdev.head) 
	{
printf (">>>>>>>>>>>>>> KILLING HALTED DEVICE %p\n", mio->hltdev.head);
		mio_killdev (mio, mio->hltdev.head);
	}
	MIO_ASSERT (mio->hltdev.tail == MIO_NULL);

	return 0;
}

int mio_exec (mio_t* mio)
{
	int n;

	mio->in_exec = 1;
	n = __exec (mio);
	mio->in_exec = 0;

	return n;
}

void mio_stop (mio_t* mio, mio_stopreq_t stopreq)
{
	mio->stopreq = stopreq;
}

int mio_loop (mio_t* mio)
{
	if (!mio->actdev.head) return 0;

	mio->stopreq = MIO_STOPREQ_NONE;
	mio->renew_watch = 0;

	if (mio_prologue (mio) <= -1) return -1;

	while (mio->stopreq == MIO_STOPREQ_NONE && mio->actdev.head)
	{
		if (mio_exec (mio) <= -1) break;
		/* you can do other things here */
	}

	mio_epilogue (mio);
	return 0;
}

mio_dev_t* mio_makedev (mio_t* mio, mio_oow_t dev_size, mio_dev_mth_t* dev_mth, mio_dev_evcb_t* dev_evcb, void* make_ctx)
{
	mio_dev_t* dev;

	if (dev_size < MIO_SIZEOF(mio_dev_t)) 
	{
		mio->errnum = MIO_EINVAL;
		return MIO_NULL;
	}

	dev = MIO_MMGR_ALLOC (mio->mmgr, dev_size);
	if (!dev)
	{
		mio->errnum = MIO_ENOMEM;
		return MIO_NULL;
	}

	MIO_MEMSET (dev, 0, dev_size);
	dev->mio = mio;
	dev->dev_size = dev_size;
	/* default capability. dev->dev_mth->make() can change this.
	 * mio_dev_watch() is affected by the capability change. */
	dev->dev_capa = MIO_DEV_CAPA_IN | MIO_DEV_CAPA_OUT;
	dev->dev_mth = dev_mth;
	dev->dev_evcb = dev_evcb;
	MIO_WQ_INIT(&dev->wq);

	/* call the callback function first */
	mio->errnum = MIO_ENOERR;
	if (dev->dev_mth->make (dev, make_ctx) <= -1)
	{
		if (mio->errnum == MIO_ENOERR) mio->errnum = MIO_EDEVMAKE;
		goto oops;
	}

	/* the make callback must not change these fields */
	MIO_ASSERT (dev->dev_mth == dev_mth);
	MIO_ASSERT (dev->dev_evcb == dev_evcb);
	MIO_ASSERT (dev->dev_prev == MIO_NULL);
	MIO_ASSERT (dev->dev_next == MIO_NULL);

	/* set some internal capability bits according to the capabilities 
	 * removed by the device making callback for convenience sake. */
	if (!(dev->dev_capa & MIO_DEV_CAPA_IN)) dev->dev_capa |= MIO_DEV_CAPA_IN_CLOSED;
	if (!(dev->dev_capa & MIO_DEV_CAPA_OUT)) dev->dev_capa |= MIO_DEV_CAPA_OUT_CLOSED;

#if defined(_WIN32)
	if (CreateIoCompletionPort ((HANDLE)dev->dev_mth->getsyshnd(dev), mio->iocp, MIO_IOCP_KEY, 0) == NULL)
	{
		/* TODO: set errnum from GetLastError()... */
		goto oops_after_make;
	}
#else
	if (mio_dev_watch (dev, MIO_DEV_WATCH_START, 0) <= -1) goto oops_after_make;
#endif

	/* and place the new device object at the back of the active device list */
	APPEND_DEVICE_TO_LIST (&mio->actdev, dev);
	dev->dev_capa |= MIO_DEV_CAPA_ACTIVE;

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
				if (mio->stopreq != MIO_STOPREQ_NONE) 
				{
					/* i can't wait until destruction attempt gets
					 * fully successful. there is a chance that some
					 * resources can leak inside the device */
					kill_and_free_device (dev, 2);
					break;
				}
			}
		}

		return MIO_NULL;
	}

oops:
	MIO_MMGR_FREE (mio->mmgr, dev);
	return MIO_NULL;
}

static int kill_and_free_device (mio_dev_t* dev, int force)
{
	mio_t* mio;

	MIO_ASSERT (!(dev->dev_capa & MIO_DEV_CAPA_ACTIVE));
	MIO_ASSERT (!(dev->dev_capa & MIO_DEV_CAPA_HALTED));

	mio = dev->mio;

	if (dev->dev_mth->kill(dev, force) <= -1) 
	{
		if (force >= 2) goto free_device;

		if (!(dev->dev_capa & MIO_DEV_CAPA_ZOMBIE))
		{
			APPEND_DEVICE_TO_LIST (&mio->zmbdev, dev);
			dev->dev_capa |= MIO_DEV_CAPA_ZOMBIE;
		}

		return -1;
	}

free_device:
	if (dev->dev_capa & MIO_DEV_CAPA_ZOMBIE)
	{
		/* detach it from the zombie device list */
		UNLINK_DEVICE_FROM_LIST (&mio->zmbdev, dev);
		dev->dev_capa &= ~MIO_DEV_CAPA_ZOMBIE;
	}

	MIO_MMGR_FREE (mio->mmgr, dev);
	return 0;
}

static void kill_zombie_job_handler (mio_t* mio, const mio_ntime_t* now, mio_tmrjob_t* job)
{
	mio_dev_t* dev = (mio_dev_t*)job->ctx;

	MIO_ASSERT (dev->dev_capa & MIO_DEV_CAPA_ZOMBIE);

	if (kill_and_free_device (dev, 0) <= -1)
	{
		if (schedule_kill_zombie_job (dev) <= -1)
		{
			/* i have to choice but to free up the devide by force */
			while (kill_and_free_device (dev, 1) <= -1)
			{
				if (mio->stopreq  != MIO_STOPREQ_NONE) 
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

static int schedule_kill_zombie_job (mio_dev_t* dev)
{
	mio_tmrjob_t kill_zombie_job;
	mio_ntime_t tmout;

	mio_inittime (&tmout, 3, 0); /* TODO: take it from configuration */

	MIO_MEMSET (&kill_zombie_job, 0, MIO_SIZEOF(kill_zombie_job));
	kill_zombie_job.ctx = dev;
	mio_gettime (&kill_zombie_job.when);
	mio_addtime (&kill_zombie_job.when, &tmout, &kill_zombie_job.when);
	kill_zombie_job.handler = kill_zombie_job_handler;
	/*kill_zombie_job.idxptr = &rdev->tmridx_kill_zombie;*/

	return mio_instmrjob (dev->mio, &kill_zombie_job) == MIO_TMRIDX_INVALID? -1: 0;
}

void mio_killdev (mio_t* mio, mio_dev_t* dev)
{
	MIO_ASSERT (mio == dev->mio);

	if (dev->dev_capa & MIO_DEV_CAPA_ZOMBIE)
	{
		MIO_ASSERT (MIO_WQ_ISEMPTY(&dev->wq));
		goto kill_device;
	}

	/* clear pending send requests */
	while (!MIO_WQ_ISEMPTY(&dev->wq))
	{
		mio_wq_t* q;
		q = MIO_WQ_HEAD(&dev->wq);
		unlink_wq (mio, q);
		MIO_MMGR_FREE (mio->mmgr, q);
	}

	if (dev->dev_capa & MIO_DEV_CAPA_HALTED)
	{
		/* this device is in the halted state.
		 * unlink it from the halted device list */
		UNLINK_DEVICE_FROM_LIST (&mio->hltdev, dev);
		dev->dev_capa &= ~MIO_DEV_CAPA_HALTED;
	}
	else
	{
		MIO_ASSERT (dev->dev_capa & MIO_DEV_CAPA_ACTIVE);
		UNLINK_DEVICE_FROM_LIST (&mio->actdev, dev);
		dev->dev_capa &= ~MIO_DEV_CAPA_ACTIVE;
	}

	mio_dev_watch (dev, MIO_DEV_WATCH_STOP, 0);

kill_device:
	if (kill_and_free_device(dev, 0) <= -1)
	{
		MIO_ASSERT (dev->dev_capa & MIO_DEV_CAPA_ZOMBIE);
		if (schedule_kill_zombie_job (dev) <= -1)
		{
			/* i have to choice but to free up the devide by force */
			while (kill_and_free_device (dev, 1) <= -1)
			{
				if (mio->stopreq  != MIO_STOPREQ_NONE) 
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

void mio_dev_halt (mio_dev_t* dev)
{
	if (dev->dev_capa & MIO_DEV_CAPA_ACTIVE)
	{
		mio_t* mio;

		mio = dev->mio;

		/* delink the device object from the active device list */
		UNLINK_DEVICE_FROM_LIST (&mio->actdev, dev);
		dev->dev_capa &= ~MIO_DEV_CAPA_ACTIVE;

		/* place it at the back of the halted device list */
		APPEND_DEVICE_TO_LIST (&mio->hltdev, dev);
		dev->dev_capa |= MIO_DEV_CAPA_HALTED;
	}
}

int mio_dev_ioctl (mio_dev_t* dev, int cmd, void* arg)
{
	if (dev->dev_mth->ioctl) return dev->dev_mth->ioctl (dev, cmd, arg);
	dev->mio->errnum = MIO_ENOSUP; /* TODO: different error code ? */
	return -1;
}

int mio_dev_watch (mio_dev_t* dev, mio_dev_watch_cmd_t cmd, int events)
{
	int mux_cmd;
	int dev_capa;

	/* the virtual device doesn't perform actual I/O.
	 * it's different from not hanving MIO_DEV_CAPA_IN and MIO_DEV_CAPA_OUT.
	 * a non-virtual device without the capabilities still gets attention
	 * of the system multiplexer for hangup and error. */
	if (dev->dev_capa & MIO_DEV_CAPA_VIRTUAL) return 0;

	/*ev.data.ptr = dev;*/
	switch (cmd)
	{
		case MIO_DEV_WATCH_START:
			/* upon start, only input watching is requested */
			events = MIO_DEV_EVENT_IN; 
			mux_cmd = MUX_CMD_INSERT;
			break;

		case MIO_DEV_WATCH_RENEW:
			/* auto-renwal mode. input watching is requested all the time.
			 * output watching is requested only if there're enqueued 
			 * data for writing. */
			events = MIO_DEV_EVENT_IN;
			if (!MIO_WQ_ISEMPTY(&dev->wq)) events |= MIO_DEV_EVENT_OUT;
			/* fall through */
		case MIO_DEV_WATCH_UPDATE:
			/* honor event watching requests as given by the caller */
			mux_cmd = MUX_CMD_UPDATE;
			break;

		case MIO_DEV_WATCH_STOP:
			events = 0; /* override events */
			mux_cmd = MUX_CMD_DELETE;
			break;

		default:
			dev->mio->errnum = MIO_EINVAL;
			return -1;
	}

	dev_capa = dev->dev_capa;
	dev_capa &= ~(DEV_CAPA_ALL_WATCHED);

	/* this function honors MIO_DEV_EVENT_IN and MIO_DEV_EVENT_OUT only
	 * as valid input event bits. it intends to provide simple abstraction
	 * by reducing the variety of event bits that the caller has to handle. */

	if ((events & MIO_DEV_EVENT_IN) && !(dev->dev_capa & (MIO_DEV_CAPA_IN_CLOSED | MIO_DEV_CAPA_IN_DISABLED)))
	{
		if (dev->dev_capa & MIO_DEV_CAPA_IN) 
		{
			if (dev->dev_capa & MIO_DEV_CAPA_PRI) dev_capa |= MIO_DEV_CAPA_PRI_WATCHED;
			dev_capa |= MIO_DEV_CAPA_IN_WATCHED;
		}
	}

	if ((events & MIO_DEV_EVENT_OUT) && !(dev->dev_capa & MIO_DEV_CAPA_OUT_CLOSED))
	{
		if (dev->dev_capa & MIO_DEV_CAPA_OUT) dev_capa |= MIO_DEV_CAPA_OUT_WATCHED;
	}

	if (mux_cmd == MUX_CMD_UPDATE && (dev_capa & DEV_CAPA_ALL_WATCHED) == (dev->dev_capa & DEV_CAPA_ALL_WATCHED))
	{
		/* no change in the device capacity. skip calling epoll_ctl */
	}
	else
	{
		if (mux_control (dev, mux_cmd, dev->dev_mth->getsyshnd(dev), dev_capa) <= -1) return -1;
	}

	dev->dev_capa = dev_capa;
	return 0;
}

int mio_dev_read (mio_dev_t* dev, int enabled)
{
	if (dev->dev_capa & MIO_DEV_CAPA_IN_CLOSED)
	{
		dev->mio->errnum = MIO_ENOCAPA;
		return -1;
	}

	if (enabled)
	{
		dev->dev_capa &= ~MIO_DEV_CAPA_IN_DISABLED;
		if (!dev->mio->in_exec && (dev->dev_capa & MIO_DEV_CAPA_IN_WATCHED)) goto renew_watch_now;
	}
	else
	{
		dev->dev_capa |= MIO_DEV_CAPA_IN_DISABLED;
		if (!dev->mio->in_exec && !(dev->dev_capa & MIO_DEV_CAPA_IN_WATCHED)) goto renew_watch_now;
	}

	dev->mio->renew_watch = 1;
	return 0;

renew_watch_now:
	if (mio_dev_watch (dev, MIO_DEV_WATCH_RENEW, 0) <= -1) return -1;
	return 0;
}

static void on_write_timeout (mio_t* mio, const mio_ntime_t* now, mio_tmrjob_t* job)
{
	mio_wq_t* q;
	mio_dev_t* dev;
	int x;

	q = (mio_wq_t*)job->ctx;
	dev = q->dev;

	dev->mio->errnum = MIO_ETMOUT;
	x = dev->dev_evcb->on_write (dev, -1, q->ctx, &q->dstaddr); 

	MIO_ASSERT (q->tmridx == MIO_TMRIDX_INVALID);
	MIO_WQ_UNLINK(q);
	MIO_MMGR_FREE (mio->mmgr, q);

	if (x <= -1) mio_dev_halt (dev);
}

static int __dev_write (mio_dev_t* dev, const void* data, mio_iolen_t len, const mio_ntime_t* tmout, void* wrctx, const mio_devaddr_t* dstaddr)
{
	const mio_uint8_t* uptr;
	mio_iolen_t urem, ulen;
	mio_wq_t* q;
	int x;

	if (dev->dev_capa & MIO_DEV_CAPA_OUT_CLOSED)
	{
		dev->mio->errnum = MIO_ENOCAPA;
		return -1;
	}

	uptr = data;
	urem = len;

	if (!MIO_WQ_ISEMPTY(&dev->wq)) 
	{
		/* the writing queue is not empty. 
		 * enqueue this request immediately */
		goto enqueue_data;
	}

	if (dev->dev_capa & MIO_DEV_CAPA_STREAM)
	{
		/* use the do..while() loop to be able to send a zero-length data */
		do
		{
			ulen = urem;
			x = dev->dev_mth->write (dev, data, &ulen, dstaddr);
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
			dev->dev_capa |= MIO_DEV_CAPA_OUT_CLOSED;
		}

		if (dev->dev_evcb->on_write (dev, len, wrctx, dstaddr) <= -1) return -1;
	}
	else
	{
		ulen = urem;

		x = dev->dev_mth->write (dev, data, &ulen, dstaddr);
		if (x <= -1) return -1;
		else if (x == 0) goto enqueue_data;

		/* partial writing is still considered ok for a non-stream device */
		if (dev->dev_evcb->on_write (dev, ulen, wrctx, dstaddr) <= -1) return -1;
	}

	return 1; /* written immediately and called on_write callback */

enqueue_data:
	if (!(dev->dev_capa & MIO_DEV_CAPA_OUT_QUEUED)) 
	{
		/* writing queuing is not requested. so return failure */
		dev->mio->errnum = MIO_ENOCAPA;
		return -1;
	}

	/* queue the remaining data*/
	q = (mio_wq_t*)MIO_MMGR_ALLOC (dev->mio->mmgr, MIO_SIZEOF(*q) + (dstaddr? dstaddr->len: 0) + urem);
	if (!q)
	{
		dev->mio->errnum = MIO_ENOMEM;
		return -1;
	}

	q->tmridx = MIO_TMRIDX_INVALID;
	q->dev = dev;
	q->ctx = wrctx;

	if (dstaddr)
	{
		q->dstaddr.ptr = (mio_uint8_t*)(q + 1);
		q->dstaddr.len = dstaddr->len;
		MIO_MEMCPY (q->dstaddr.ptr, dstaddr->ptr, dstaddr->len);
	}
	else
	{
		q->dstaddr.len = 0;
	}

	q->ptr = (mio_uint8_t*)(q + 1) + q->dstaddr.len;
	q->len = urem;
	q->olen = len;
	MIO_MEMCPY (q->ptr, uptr, urem);

	if (tmout && mio_ispostime(tmout))
	{
		mio_tmrjob_t tmrjob;

		MIO_MEMSET (&tmrjob, 0, MIO_SIZEOF(tmrjob));
		tmrjob.ctx = q;
		mio_gettime (&tmrjob.when);
		mio_addtime (&tmrjob.when, tmout, &tmrjob.when);
		tmrjob.handler = on_write_timeout;
		tmrjob.idxptr = &q->tmridx;

		q->tmridx = mio_instmrjob (dev->mio, &tmrjob);
		if (q->tmridx == MIO_TMRIDX_INVALID) 
		{
			MIO_MMGR_FREE (dev->mio->mmgr, q);
			return -1;
		}
	}

	MIO_WQ_ENQ (&dev->wq, q);
	if (!dev->mio->in_exec && !(dev->dev_capa & MIO_DEV_CAPA_OUT_WATCHED))
	{
		/* if output is not being watched, arrange to do so */
		if (mio_dev_watch (dev, MIO_DEV_WATCH_RENEW, 0) <= -1)
		{
			unlink_wq (dev->mio, q);
			MIO_MMGR_FREE (dev->mio->mmgr, q);
			return -1;
		}
	}
	else
	{
		dev->mio->renew_watch = 1;
	}

	return 0; /* request pused to a write queue. */
}

int mio_dev_write (mio_dev_t* dev, const void* data, mio_iolen_t len, void* wrctx, const mio_devaddr_t* dstaddr)
{
	return __dev_write (dev, data, len, MIO_NULL, wrctx, dstaddr);
}

int mio_dev_timedwrite (mio_dev_t* dev, const void* data, mio_iolen_t len, const mio_ntime_t* tmout, void* wrctx, const mio_devaddr_t* dstaddr)
{
	return __dev_write (dev, data, len, tmout, wrctx, dstaddr);
}

int mio_makesyshndasync (mio_t* mio, mio_syshnd_t hnd)
{
#if defined(F_GETFL) && defined(F_SETFL) && defined(O_NONBLOCK)
	int flags;

	if ((flags = fcntl (hnd, F_GETFL)) <= -1 ||
	    (flags = fcntl (hnd, F_SETFL, flags | O_NONBLOCK)) <= -1)
	{
		mio->errnum = mio_syserrtoerrnum (errno);
		return -1;
	}

	return 0;
#else
	mio->errnum = MIO_ENOSUP;
	return -1;
#endif
}

mio_errnum_t mio_syserrtoerrnum (int no)
{
	switch (no)
	{
		case ENOMEM:
			return MIO_ENOMEM;

		case EINVAL:
			return MIO_EINVAL;

		case EEXIST:
			return MIO_EEXIST;

		case ENOENT:
			return MIO_ENOENT;

		case EMFILE:
			return MIO_EMFILE;

	#if defined(ENFILE)
		case ENFILE:
			return MIO_ENFILE;
	#endif

	#if defined(EWOULDBLOCK) && defined(EAGAIN) && (EWOULDBLOCK != EAGAIN)
		case EAGAIN:
		case EWOULDBLOCK:
			return MIO_EAGAIN;
	#elif defined(EAGAIN)
		case EAGAIN:
			return MIO_EAGAIN;
	#elif defined(EWOULDBLOCK)
		case EWOULDBLOCK:
			return MIO_EAGAIN;
	#endif

	#if defined(ECONNREFUSED)
		case ECONNREFUSED:
			return MIO_ECONRF;
	#endif

	#if defined(ECONNRESETD)
		case ECONNRESET:
			return MIO_ECONRS;
	#endif

	#if defined(EPERM)
		case EPERM:
			return MIO_EPERM;
	#endif

		default:
			return MIO_ESYSERR;
	}
}
