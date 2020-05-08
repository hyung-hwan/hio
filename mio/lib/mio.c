/*
 * $Id$
 *
    Copyright (c) 2016-2020 Chung, Hyung-Hwan. All rights reserved.

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
#include "mio-fmt.h"
#include <stdlib.h>
  
#define DEV_CAP_ALL_WATCHED (MIO_DEV_CAP_IN_WATCHED | MIO_DEV_CAP_OUT_WATCHED | MIO_DEV_CAP_PRI_WATCHED)

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


static void on_read_timeout (mio_t* mio, const mio_ntime_t* now, mio_tmrjob_t* job);
static void on_write_timeout (mio_t* mio, const mio_ntime_t* now, mio_tmrjob_t* job);

/* ========================================================================= */

static void* mmgr_alloc (mio_mmgr_t* mmgr, mio_oow_t size)
{
	return malloc(size);
}

static void* mmgr_realloc (mio_mmgr_t* mmgr, void* ptr, mio_oow_t size)
{
	return realloc(ptr, size);
}

static void mmgr_free (mio_mmgr_t* mmgr, void* ptr)
{
	return free (ptr);
}

static mio_mmgr_t default_mmgr =
{
	mmgr_alloc,
	mmgr_realloc,
	mmgr_free,
	MIO_NULL
};

/* ========================================================================= */

mio_t* mio_open (mio_mmgr_t* mmgr, mio_oow_t xtnsize, mio_cmgr_t* cmgr, mio_oow_t tmrcapa, mio_errinf_t* errinfo)
{
	mio_t* mio;

	if (!mmgr) mmgr = &default_mmgr;
	if (!cmgr) cmgr = mio_get_utf8_cmgr();

	mio = (mio_t*)MIO_MMGR_ALLOC(mmgr, MIO_SIZEOF(mio_t) + xtnsize);
	if (mio)
	{
		if (mio_init(mio, mmgr, cmgr, tmrcapa) <= -1)
		{
			if (errinfo) mio_geterrinf (mio, errinfo);
			MIO_MMGR_FREE (mmgr, mio);
			mio = MIO_NULL;
		}
		else MIO_MEMSET (mio + 1, 0, xtnsize);
	}
	else if (errinfo)
	{
		errinfo->num = MIO_ESYSMEM;
		mio_copy_oocstr (errinfo->msg, MIO_COUNTOF(errinfo->msg), mio_errnum_to_errstr(MIO_ESYSMEM));
	}

	return mio;
}

void mio_close (mio_t* mio)
{
	mio_fini (mio);
	MIO_MMGR_FREE (mio->_mmgr, mio);
}

int mio_init (mio_t* mio, mio_mmgr_t* mmgr, mio_cmgr_t* cmgr, mio_oow_t tmrcapa)
{
	int sys_inited = 0;

	MIO_MEMSET (mio, 0, MIO_SIZEOF(*mio));
	mio->_instsize = MIO_SIZEOF(*mio);
	mio->_mmgr = mmgr;
	mio->_cmgr = cmgr;

	/* initialize data for logging support */
	mio->option.log_mask = MIO_LOG_ALL_LEVELS | MIO_LOG_ALL_TYPES;
	mio->log.capa = MIO_ALIGN_POW2(1, MIO_LOG_CAPA_ALIGN); /* TODO: is this a good initial size? */
	/* alloate the log buffer in advance though it may get reallocated
	 * in put_oocs and put_ooch in fmtout.c. this is to let the logging
	 * routine still function despite some side-effects when
	 * reallocation fails */
	/* +1 required for consistency with put_oocs and put_ooch in fmtout.c */
	mio->log.ptr = mio_allocmem(mio, (mio->log.capa + 1) * MIO_SIZEOF(*mio->log.ptr)); 
	if (MIO_UNLIKELY(!mio->log.ptr)) goto oops;

	/* inititalize the system-side logging */
	if (MIO_UNLIKELY(mio_sys_init(mio) <= -1)) goto oops;
	sys_inited = 1;

	/* initialize the timer object */
	if (tmrcapa <= 0) tmrcapa = 1;
	mio->tmr.jobs = mio_allocmem(mio, tmrcapa * MIO_SIZEOF(mio_tmrjob_t));
	if (MIO_UNLIKELY(!mio->tmr.jobs)) goto oops;

	mio->tmr.capa = tmrcapa;

	MIO_DEVL_INIT (&mio->actdev);
	MIO_DEVL_INIT (&mio->hltdev);
	MIO_DEVL_INIT (&mio->zmbdev);
	MIO_CWQ_INIT (&mio->cwq);
	MIO_SVCL_INIT (&mio->actsvc);

	mio_sys_gettime (mio, &mio->init_time);
	return 0;

oops:
	if (mio->tmr.jobs) mio_freemem (mio, mio->tmr.jobs);

	if (sys_inited) mio_sys_fini (mio);

	if (mio->log.ptr) mio_freemem (mio, mio->log.ptr);
	mio->log.capa = 0;
	return -1;
}

void mio_fini (mio_t* mio)
{
	mio_dev_t* dev, * next_dev;
	mio_dev_t diehard;
	mio_oow_t i;

	/* clean up free cwq list */
	for (i = 0; i < MIO_COUNTOF(mio->cwqfl); i++)
	{
		mio_cwq_t* cwq;
		while ((cwq = mio->cwqfl[i]))
		{
			mio->cwqfl[i] = cwq->q_next;
			mio_freemem (mio, cwq);
		}
	}

	/* kill services before killing devices */
	while (!MIO_SVCL_IS_EMPTY(&mio->actsvc))
	{
		mio_svc_t* svc;

		svc = MIO_SVCL_FIRST_SVC(&mio->actsvc);
		if (svc->svc_stop) 
		{
			/* the stop callback must unregister itself */
			svc->svc_stop (svc);
		}
		else
		{
			/* unregistration only if no stop callback is designated */
			MIO_SVCL_UNLINK_SVC (svc);
		}
	}

	/* kill all registered devices */
	while (!MIO_DEVL_IS_EMPTY(&mio->actdev))
	{
		mio_dev_kill (MIO_DEVL_FIRST_DEV(&mio->actdev));
	}

	/* kill all halted devices */
	while (!MIO_DEVL_IS_EMPTY(&mio->hltdev))
	{
		mio_dev_kill (MIO_DEVL_FIRST_DEV(&mio->hltdev));
	}

	/* clean up all zombie devices */
	MIO_DEVL_INIT (&diehard);
	for (dev = MIO_DEVL_FIRST_DEV(&mio->zmbdev); !MIO_DEVL_IS_NIL_DEV(&mio->zmbdev, dev); )
	{
		kill_and_free_device (dev, 1);
		if (MIO_DEVL_FIRST_DEV(&mio->zmbdev) == dev) 
		{
			/* the deive has not been freed. go on to the next one */
			next_dev = dev->dev_next;

			/* remove the device from the zombie device list */
			MIO_DEVL_UNLINK_DEV (dev);
			dev->dev_cap &= ~MIO_DEV_CAP_ZOMBIE;

			/* put it to a private list for aborting */
			MIO_DEVL_APPEND_DEV (&diehard, dev);

			dev = next_dev;
		}
		else dev = MIO_DEVL_FIRST_DEV(&mio->zmbdev);
	}

	while (!MIO_DEVL_IS_EMPTY(&diehard))
	{
		/* if the kill method returns failure, it can leak some resource
		 * because the device is freed regardless of the failure when 2 
		 * is given to kill_and_free_device(). */
		dev = MIO_DEVL_FIRST_DEV(&diehard);
		MIO_ASSERT (mio, !(dev->dev_cap & (MIO_DEV_CAP_ACTIVE | MIO_DEV_CAP_HALTED | MIO_DEV_CAP_ZOMBIE)));
		MIO_DEVL_UNLINK_DEV (dev);
		kill_and_free_device (dev, 2);
	}

	/* purge scheduled timer jobs and kill the timer */
	mio_cleartmrjobs (mio);
	mio_freemem (mio, mio->tmr.jobs);

	mio_sys_fini (mio); /* finalize the system dependent data */

	mio_freemem (mio, mio->log.ptr);
}

int mio_setoption (mio_t* mio, mio_option_t id, const void* value)
{
	switch (id)
	{
		case MIO_TRAIT:
			mio->option.trait = *(mio_bitmask_t*)value;
			return 0;

		case MIO_LOG_MASK:
			mio->option.log_mask = *(mio_bitmask_t*)value;
			return 0;

		case MIO_LOG_MAXCAPA:
			mio->option.log_maxcapa = *(mio_oow_t*)value;
			return 0;
	}

	mio_seterrnum (mio, MIO_EINVAL);
	return -1;
}

int mio_getoption (mio_t* mio, mio_option_t id, void* value)
{
	switch  (id)
	{
		case MIO_TRAIT:
			*(mio_bitmask_t*)value = mio->option.trait;
			return 0;

		case MIO_LOG_MASK:
			*(mio_bitmask_t*)value = mio->option.log_mask;
			return 0;

		case MIO_LOG_MAXCAPA:
			*(mio_oow_t*)value = mio->option.log_maxcapa;
			return 0;
	};

	mio_seterrnum (mio, MIO_EINVAL);
	return -1;
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
		MIO_ASSERT (mio, q->tmridx == MIO_TMRIDX_INVALID);
	}
	MIO_WQ_UNLINK (q);
}

static MIO_INLINE void handle_event (mio_t* mio, mio_dev_t* dev, int events, int rdhup)
{
	MIO_ASSERT (mio, mio == dev->mio);

	dev->dev_cap &= ~MIO_DEV_CAP_RENEW_REQUIRED;

	MIO_ASSERT (mio, mio == dev->mio);

	if (dev->dev_evcb->ready)
	{
		int x, xevents;

		xevents = events;
		if (rdhup) xevents |= MIO_DEV_EVENT_HUP;

		/* return value of ready()
		 *   <= -1 - failure. kill the device.
		 *   == 0 - ok. but don't invoke recv() or send().
		 *   >= 1 - everything is ok. */
		x = dev->dev_evcb->ready(dev, xevents);
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
		/*x = dev->dev_mth->urgread(dev, mio->bugbuf, &len);*/
	}

	if (dev && (events & MIO_DEV_EVENT_OUT))
	{
		/* write pending requests */
		while (!MIO_WQ_IS_EMPTY(&dev->wq))
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
			x = dev->dev_mth->write(dev, uptr, &ulen, &q->dstaddr);
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

					if (q->len <= 0 && (dev->dev_cap & MIO_DEV_CAP_STREAM)) 
					{
						/* it was a zero-length write request. 
						 * for a stream, it is to close the output. */
						dev->dev_cap |= MIO_DEV_CAP_OUT_CLOSED;
						dev->dev_cap |= MIO_DEV_CAP_RENEW_REQUIRED;
						out_closed = 1;
					}

					unlink_wq (mio, q);
					y = dev->dev_evcb->on_write(dev, q->olen, q->ctx, &q->dstaddr);
					mio_freemem (mio, q);

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
						while (!MIO_WQ_IS_EMPTY(&dev->wq))
						{
							q = MIO_WQ_HEAD(&dev->wq);
							unlink_wq (mio, q);
							mio_freemem (mio, q);
						}
						break;
					}
				}
				else goto send_leftover;
			}
		}

		if (dev && MIO_WQ_IS_EMPTY(&dev->wq))
		{
			/* no pending request to write */
			if ((dev->dev_cap & MIO_DEV_CAP_IN_CLOSED) &&
			    (dev->dev_cap & MIO_DEV_CAP_OUT_CLOSED))
			{
				mio_dev_halt (dev);
				dev = MIO_NULL;
			}
			else
			{
				dev->dev_cap |= MIO_DEV_CAP_RENEW_REQUIRED;
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
			x = dev->dev_mth->read(dev, mio->bigbuf, &len, &srcaddr);
			if (x <= -1)
			{
				mio_dev_halt (dev);
				dev = MIO_NULL;
				break;
			}

			if (dev->rtmridx != MIO_TMRIDX_INVALID)
			{
				/* delete the read timeout job on the device as the
				 * read operation will be reported below. */
				mio_tmrjob_t tmrjob;

				MIO_MEMSET (&tmrjob, 0, MIO_SIZEOF(tmrjob));
				tmrjob.ctx = dev;
				mio_gettime (mio, &tmrjob.when);
				MIO_ADD_NTIME (&tmrjob.when, &tmrjob.when, &dev->rtmout);
				tmrjob.handler = on_read_timeout;
				tmrjob.idxptr = &dev->rtmridx;

				mio_updtmrjob (mio, dev->rtmridx, &tmrjob);

				/*mio_deltmrjob (mio, dev->rtmridx);
				dev->rtmridx = MIO_TMRIDX_INVALID;*/
			}

			if (x == 0)
			{
				/* no data is available - EWOULDBLOCK or something similar */
				break;
			}
			else /*if (x >= 1) */
			{
				if (len <= 0 && (dev->dev_cap & MIO_DEV_CAP_STREAM)) 
				{
					/* EOF received. for a stream device, a zero-length 
					 * read is interpreted as EOF. */
					dev->dev_cap |= MIO_DEV_CAP_IN_CLOSED;
					dev->dev_cap |= MIO_DEV_CAP_RENEW_REQUIRED;

					/* call the on_read callback to report EOF */
					if (dev->dev_evcb->on_read(dev, mio->bigbuf, len, &srcaddr) <= -1 ||
					    (dev->dev_cap & MIO_DEV_CAP_OUT_CLOSED))
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
					y = dev->dev_evcb->on_read(dev, mio->bigbuf, len, &srcaddr);
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
			dev->dev_cap |= MIO_DEV_CAP_IN_CLOSED | MIO_DEV_CAP_OUT_CLOSED;
			dev->dev_cap |= MIO_DEV_CAP_RENEW_REQUIRED;
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
				dev->dev_cap |= MIO_DEV_CAP_IN_CLOSED | MIO_DEV_CAP_OUT_CLOSED;
				dev->dev_cap |= MIO_DEV_CAP_RENEW_REQUIRED;
			}
		}

		if ((dev->dev_cap & MIO_DEV_CAP_IN_CLOSED) &&
		    (dev->dev_cap & MIO_DEV_CAP_OUT_CLOSED))
		{
			mio_dev_halt (dev);
			dev = MIO_NULL;
		}
	}

skip_evcb:
	if (dev && (dev->dev_cap & MIO_DEV_CAP_RENEW_REQUIRED) && mio_dev_watch(dev, MIO_DEV_WATCH_RENEW, MIO_DEV_EVENT_IN) <= -1)
	{
		mio_dev_halt (dev);
		dev = MIO_NULL;
	}
}

int mio_exec (mio_t* mio)
{
	int ret = 0;

	/* execute callbacks for completed write operations */
	while (!MIO_CWQ_IS_EMPTY(&mio->cwq))
	{
		mio_cwq_t* cwq;
		mio_oow_t cwqfl_index;

		cwq = MIO_CWQ_HEAD(&mio->cwq);
		if (cwq->dev->dev_evcb->on_write(cwq->dev, cwq->olen, cwq->ctx, &cwq->dstaddr) <= -1) return -1;
		cwq->dev->cw_count--;
		MIO_CWQ_UNLINK (cwq);

		cwqfl_index = MIO_ALIGN_POW2(cwq->dstaddr.len, MIO_CWQFL_ALIGN) / MIO_CWQFL_SIZE;
		if (cwqfl_index < MIO_COUNTOF(mio->cwqfl))
		{
			/* reuse the cwq object if dstaddr is 0 in size. chain it to the free list */
			cwq->q_next = mio->cwqfl[cwqfl_index];
			mio->cwqfl[cwqfl_index] = cwq;
		}
		else
		{
			/* TODO: more reuse of objects of different size? */
			mio_freemem (mio, cwq);
		}
	}

	/* execute the scheduled jobs before checking devices with the 
	 * multiplexer. the scheduled jobs can safely destroy the devices */
	mio_firetmrjobs (mio, MIO_NULL, MIO_NULL);

	if (!MIO_DEVL_IS_EMPTY(&mio->actdev))
	{
		/* wait on the multiplexer only if there is at least 1 active device */
		mio_ntime_t tmout;

		if (mio_gettmrtmout(mio, MIO_NULL, &tmout) <= -1)
		{
			/* defaults to 1 second if timeout can't be acquired */
			tmout.sec = 1; /* TODO: make the default timeout configurable */
			tmout.nsec = 0;
		}

		if (mio_sys_waitmux(mio, &tmout, handle_event) <= -1) 
		{
			MIO_DEBUG0 (mio, "WARNING - Failed to wait on mutiplexer\n");
			ret = -1;
		}
	}

	/* kill all halted devices */
	while (!MIO_DEVL_IS_EMPTY(&mio->hltdev)) 
	{
		mio_dev_t* dev = MIO_DEVL_FIRST_DEV(&mio->hltdev);
		MIO_DEBUG1 (mio, "Killing HALTED device %p\n", dev);
		mio_dev_kill (dev);
	}

	return ret;
}

void mio_stop (mio_t* mio, mio_stopreq_t stopreq)
{
	mio->stopreq = stopreq;
}

int mio_loop (mio_t* mio)
{
	if (MIO_DEVL_IS_EMPTY(&mio->actdev)) return 0;

	mio->stopreq = MIO_STOPREQ_NONE;

	if (mio_prologue(mio) <= -1) return -1;

	while (mio->stopreq == MIO_STOPREQ_NONE && !MIO_DEVL_IS_EMPTY(&mio->actdev))
	{
		if (mio_exec(mio) <= -1) break;
		/* you can do other things here */
	}

	mio_epilogue(mio);
	return 0;
}

mio_dev_t* mio_dev_make (mio_t* mio, mio_oow_t dev_size, mio_dev_mth_t* dev_mth, mio_dev_evcb_t* dev_evcb, void* make_ctx)
{
	mio_dev_t* dev;

	if (dev_size < MIO_SIZEOF(mio_dev_t)) 
	{
		mio_seterrnum (mio, MIO_EINVAL);
		return MIO_NULL;
	}

	dev = (mio_dev_t*)mio_callocmem(mio, dev_size);
	if (MIO_UNLIKELY(!dev)) return MIO_NULL;

	dev->mio = mio;
	dev->dev_size = dev_size;
	/* default capability. dev->dev_mth->make() can change this.
	 * mio_dev_watch() is affected by the capability change. */
	dev->dev_cap = MIO_DEV_CAP_IN | MIO_DEV_CAP_OUT;
	dev->dev_mth = dev_mth;
	dev->dev_evcb = dev_evcb;
	MIO_INIT_NTIME (&dev->rtmout, 0, 0); 
	dev->rtmridx = MIO_TMRIDX_INVALID;
	MIO_WQ_INIT (&dev->wq);
	dev->cw_count = 0;

	/* call the callback function first */
	mio_seterrnum (mio, MIO_ENOERR);
	if (dev->dev_mth->make(dev, make_ctx) <= -1)
	{
		if (mio->errnum == MIO_ENOERR) mio_seterrnum (mio, MIO_EDEVMAKE);
		goto oops;
	}

	/* the make callback must not change these fields */
	MIO_ASSERT (mio, dev->dev_mth == dev_mth);
	MIO_ASSERT (mio, dev->dev_evcb == dev_evcb);
	MIO_ASSERT (mio, dev->dev_prev == MIO_NULL);
	MIO_ASSERT (mio, dev->dev_next == MIO_NULL);

	/* set some internal capability bits according to the capabilities 
	 * removed by the device making callback for convenience sake. */
	dev->dev_cap &= MIO_DEV_CAP_ALL_MASK; /* keep valid capability bits only. drop all internal-use bits */
	if (!(dev->dev_cap & MIO_DEV_CAP_IN)) dev->dev_cap |= MIO_DEV_CAP_IN_CLOSED;
	if (!(dev->dev_cap & MIO_DEV_CAP_OUT)) dev->dev_cap |= MIO_DEV_CAP_OUT_CLOSED;

	if (mio_dev_watch(dev, MIO_DEV_WATCH_START, 0) <= -1) goto oops_after_make;
	/* and place the new device object at the back of the active device list */
	MIO_DEVL_APPEND_DEV (&mio->actdev, dev);
	dev->dev_cap |= MIO_DEV_CAP_ACTIVE;

	return dev;

oops_after_make:
	if (kill_and_free_device(dev, 0) <= -1)
	{
		/* schedule a timer job that reattempts to destroy the device */
		if (schedule_kill_zombie_job(dev) <= -1) 
		{
			/* job scheduling failed. i have no choice but to
			 * destroy the device now.
			 * 
			 * NOTE: this while loop can block the process
			 *       if the kill method keep returning failure */
			while (kill_and_free_device(dev, 1) <= -1)
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
	mio_freemem (mio, dev);
	return MIO_NULL;
}

static int kill_and_free_device (mio_dev_t* dev, int force)
{
	mio_t* mio = dev->mio;

	MIO_ASSERT (mio, !(dev->dev_cap & MIO_DEV_CAP_ACTIVE));
	MIO_ASSERT (mio, !(dev->dev_cap & MIO_DEV_CAP_HALTED));

	if (dev->dev_mth->kill(dev, force) <= -1) 
	{
		if (force >= 2) goto free_device;

		if (!(dev->dev_cap & MIO_DEV_CAP_ZOMBIE))
		{
			MIO_DEVL_APPEND_DEV (&mio->zmbdev, dev);
			dev->dev_cap |= MIO_DEV_CAP_ZOMBIE;
		}

		return -1;
	}

free_device:
	if (dev->dev_cap & MIO_DEV_CAP_ZOMBIE)
	{
		/* detach it from the zombie device list */
		MIO_DEVL_UNLINK_DEV (dev);
		dev->dev_cap &= ~MIO_DEV_CAP_ZOMBIE;
	}

	mio_freemem (mio, dev);
	return 0;
}

static void kill_zombie_job_handler (mio_t* mio, const mio_ntime_t* now, mio_tmrjob_t* job)
{
	mio_dev_t* dev = (mio_dev_t*)job->ctx;

	MIO_ASSERT (mio, dev->dev_cap & MIO_DEV_CAP_ZOMBIE);

	if (kill_and_free_device(dev, 0) <= -1)
	{
		if (schedule_kill_zombie_job(dev) <= -1)
		{
			/* i have to choice but to free up the devide by force */
			while (kill_and_free_device(dev, 1) <= -1)
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
	}
}

static int schedule_kill_zombie_job (mio_dev_t* dev)
{
	mio_t* mio = dev->mio;
	mio_tmrjob_t kill_zombie_job;
	mio_ntime_t tmout;

	MIO_INIT_NTIME (&tmout, 3, 0); /* TODO: take it from configuration */

	MIO_MEMSET (&kill_zombie_job, 0, MIO_SIZEOF(kill_zombie_job));
	kill_zombie_job.ctx = dev;
	mio_gettime (mio, &kill_zombie_job.when);
	MIO_ADD_NTIME (&kill_zombie_job.when, &kill_zombie_job.when, &tmout);
	kill_zombie_job.handler = kill_zombie_job_handler;
	/*kill_zombie_job.idxptr = &rdev->tmridx_kill_zombie;*/

	return mio_instmrjob(mio, &kill_zombie_job) == MIO_TMRIDX_INVALID? -1: 0;
}

void mio_dev_kill (mio_dev_t* dev)
{
	mio_t* mio = dev->mio;

	if (dev->dev_cap & MIO_DEV_CAP_ZOMBIE)
	{
		MIO_ASSERT (mio, MIO_WQ_IS_EMPTY(&dev->wq));
		MIO_ASSERT (mio, dev->cw_count == 0);
		MIO_ASSERT (mio, dev->rtmridx == MIO_TMRIDX_INVALID);
		goto kill_device;
	}

	if (dev->rtmridx != MIO_TMRIDX_INVALID)
	{
		mio_deltmrjob (mio, dev->rtmridx);
		dev->rtmridx = MIO_TMRIDX_INVALID;
	}

	/* clear completed write event queues - TODO: do i need to fire these? */
	if (dev->cw_count > 0)
	{
		mio_cwq_t* cwq, * next;
		cwq = MIO_CWQ_HEAD(&mio->cwq);
		while (cwq != &mio->cwq)
		{
			next = MIO_CWQ_NEXT(cwq);
			if (cwq->dev == dev)
			{
				MIO_CWQ_UNLINK (cwq);
				mio_freemem (mio, cwq);
			}
			cwq = next;
		}
	}

	/* clear pending send requests */
	while (!MIO_WQ_IS_EMPTY(&dev->wq))
	{
		mio_wq_t* q;
		q = MIO_WQ_HEAD(&dev->wq);
		unlink_wq (mio, q);
		mio_freemem (mio, q);
	}

	if (dev->dev_cap & MIO_DEV_CAP_HALTED)
	{
		/* this device is in the halted state.
		 * unlink it from the halted device list */
		MIO_DEVL_UNLINK_DEV (dev);
		dev->dev_cap &= ~MIO_DEV_CAP_HALTED;
	}
	else
	{
		MIO_ASSERT (mio, dev->dev_cap & MIO_DEV_CAP_ACTIVE);
		MIO_DEVL_UNLINK_DEV (dev);
		dev->dev_cap &= ~MIO_DEV_CAP_ACTIVE;
	}

	mio_dev_watch (dev, MIO_DEV_WATCH_STOP, 0);

kill_device:
	if (kill_and_free_device(dev, 0) <= -1)
	{
		MIO_ASSERT (mio, dev->dev_cap & MIO_DEV_CAP_ZOMBIE);
		if (schedule_kill_zombie_job (dev) <= -1)
		{
			/* i have no choice but to free up the devide by force */
			while (kill_and_free_device(dev, 1) <= -1)
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
	mio_t* mio = dev->mio;

	if (dev->dev_cap & MIO_DEV_CAP_ACTIVE)
	{
		MIO_DEBUG1 (mio, "HALTING DEVICE %p\n", dev);

		/* delink the device object from the active device list */
		MIO_DEVL_UNLINK_DEV (dev);
		dev->dev_cap &= ~MIO_DEV_CAP_ACTIVE;

		/* place it at the back of the halted device list */
		MIO_DEVL_APPEND_DEV (&mio->hltdev, dev);
		dev->dev_cap |= MIO_DEV_CAP_HALTED;
	}
}

int mio_dev_ioctl (mio_dev_t* dev, int cmd, void* arg)
{
	mio_t* mio = dev->mio;

	if (MIO_UNLIKELY(!dev->dev_mth->ioctl))
	{
		mio_seterrnum (mio, MIO_ENOIMPL);  /* TODO: different error code ? */
		return -1;
	}

	return dev->dev_mth->ioctl(dev, cmd, arg);
}

int mio_dev_watch (mio_dev_t* dev, mio_dev_watch_cmd_t cmd, int events)
{
	mio_t* mio = dev->mio;
	int mux_cmd;
	int dev_cap;

	/* the virtual device doesn't perform actual I/O.
	 * it's different from not hanving MIO_DEV_CAP_IN and MIO_DEV_CAP_OUT.
	 * a non-virtual device without the capabilities still gets attention
	 * of the system multiplexer for hangup and error. */
	if (dev->dev_cap & MIO_DEV_CAP_VIRTUAL) return 0;

	/*ev.data.ptr = dev;*/
	switch (cmd)
	{
		case MIO_DEV_WATCH_START:
			/* request input watching when a device is started.
			 * if the device is set with MIO_DEV_CAP_IN_DISABLED and/or 
			 * is not set with MIO_DEV_CAP_IN, input wathcing is excluded 
			 * after this 'switch' block */
			events = MIO_DEV_EVENT_IN;
			mux_cmd = MIO_SYS_MUX_CMD_INSERT;
			break;

		case MIO_DEV_WATCH_RENEW:
			/* auto-renwal mode. input watching is taken from the events make passed in.
			 * output watching is requested only if there're enqueued data for writing.
			 * if you want to enable input watching while renewing, call this function like this.
			 *  mio_dev_wtach (dev, MIO_DEV_WATCH_RENEW, MIO_DEV_EVENT_IN);
			 * if you want input whatching disabled while renewing, call this function like this. 
			 *  mio_dev_wtach (dev, MIO_DEV_WATCH_RENEW, 0); */
			if (MIO_WQ_IS_EMPTY(&dev->wq)) events &= ~MIO_DEV_EVENT_OUT;
			else events |= MIO_DEV_EVENT_OUT;
			
			/* fall through */
		case MIO_DEV_WATCH_UPDATE:
			/* honor event watching requests as given by the caller */
			mux_cmd = MIO_SYS_MUX_CMD_UPDATE;
			break;

		case MIO_DEV_WATCH_STOP:
			events = 0; /* override events */
			mux_cmd = MIO_SYS_MUX_CMD_DELETE;
			break;

		default:
			mio_seterrnum (dev->mio, MIO_EINVAL);
			return -1;
	}

	dev_cap = dev->dev_cap;
	dev_cap &= ~(DEV_CAP_ALL_WATCHED);

	/* this function honors MIO_DEV_EVENT_IN and MIO_DEV_EVENT_OUT only
	 * as valid input event bits. it intends to provide simple abstraction
	 * by reducing the variety of event bits that the caller has to handle. */

	if ((events & MIO_DEV_EVENT_IN) && !(dev->dev_cap & (MIO_DEV_CAP_IN_CLOSED | MIO_DEV_CAP_IN_DISABLED)))
	{
		if (dev->dev_cap & MIO_DEV_CAP_IN) 
		{
			if (dev->dev_cap & MIO_DEV_CAP_PRI) dev_cap |= MIO_DEV_CAP_PRI_WATCHED;
			dev_cap |= MIO_DEV_CAP_IN_WATCHED;
		}
	}

	if ((events & MIO_DEV_EVENT_OUT) && !(dev->dev_cap & MIO_DEV_CAP_OUT_CLOSED))
	{
		if (dev->dev_cap & MIO_DEV_CAP_OUT) dev_cap |= MIO_DEV_CAP_OUT_WATCHED;
	}

	if (mux_cmd == MIO_SYS_MUX_CMD_UPDATE && (dev_cap & DEV_CAP_ALL_WATCHED) == (dev->dev_cap & DEV_CAP_ALL_WATCHED))
	{
		/* no change in the device capacity. skip calling epoll_ctl */
	}
	else
	{
		if (mio_sys_ctrlmux(mio, mux_cmd, dev, dev_cap) <= -1) return -1;
	}

	dev->dev_cap = dev_cap;
	return 0;
}

static void on_read_timeout (mio_t* mio, const mio_ntime_t* now, mio_tmrjob_t* job)
{
	mio_dev_t* dev;
	int x;

	dev = (mio_dev_t*)job->ctx;

	mio_seterrnum (mio, MIO_ETMOUT);
	x = dev->dev_evcb->on_read(dev, MIO_NULL, -1, MIO_NULL); 

	MIO_ASSERT (mio, dev->rtmridx == MIO_TMRIDX_INVALID);

	if (x <= -1) mio_dev_halt (dev);
}

static int __dev_read (mio_dev_t* dev, int enabled, const mio_ntime_t* tmout, void* rdctx)
{
	mio_t* mio = dev->mio;

	if (dev->dev_cap & MIO_DEV_CAP_IN_CLOSED)
	{
		mio_seterrbfmt (mio, MIO_ENOCAPA, "unable to read closed device");
		return -1;
	}

	if (enabled)
	{
		dev->dev_cap &= ~MIO_DEV_CAP_IN_DISABLED;
		if (!(dev->dev_cap & MIO_DEV_CAP_IN_WATCHED)) goto renew_watch_now;
	}
	else
	{
		dev->dev_cap |= MIO_DEV_CAP_IN_DISABLED;
		if ((dev->dev_cap & MIO_DEV_CAP_IN_WATCHED)) goto renew_watch_now;
	}

	dev->dev_cap |= MIO_DEV_CAP_RENEW_REQUIRED;
	goto update_timer;

renew_watch_now:
	if (mio_dev_watch(dev, MIO_DEV_WATCH_RENEW, MIO_DEV_EVENT_IN) <= -1) return -1;
	goto update_timer;

update_timer:
	if (dev->rtmridx != MIO_TMRIDX_INVALID)
	{
		/* read timeout already on the socket. remove it first */
		mio_deltmrjob (mio, dev->rtmridx);
		dev->rtmridx = MIO_TMRIDX_INVALID;
	}

	if (tmout && MIO_IS_POS_NTIME(tmout))
	{
		mio_tmrjob_t tmrjob;

		MIO_MEMSET (&tmrjob, 0, MIO_SIZEOF(tmrjob));
		tmrjob.ctx = dev;
		mio_gettime (mio, &tmrjob.when);
		MIO_ADD_NTIME (&tmrjob.when, &tmrjob.when, tmout);
		tmrjob.handler = on_read_timeout;
		tmrjob.idxptr = &dev->rtmridx;

		dev->rtmridx = mio_instmrjob(mio, &tmrjob);
		if (dev->rtmridx == MIO_TMRIDX_INVALID) 
		{
			/* if timer registration fails, timeout will never be triggered */
			return -1;
		}
		dev->rtmout = *tmout;
	}
	return 0;
}

int mio_dev_read (mio_dev_t* dev, int enabled)
{
	return __dev_read(dev, enabled, MIO_NULL, MIO_NULL);
}

int mio_dev_timedread (mio_dev_t* dev, int enabled, const mio_ntime_t* tmout)
{
	return __dev_read(dev, enabled, tmout, MIO_NULL);
}

static void on_write_timeout (mio_t* mio, const mio_ntime_t* now, mio_tmrjob_t* job)
{
	mio_wq_t* q;
	mio_dev_t* dev;
	int x;

	q = (mio_wq_t*)job->ctx;
	dev = q->dev;

	mio_seterrnum (mio, MIO_ETMOUT);
	x = dev->dev_evcb->on_write(dev, -1, q->ctx, &q->dstaddr); 

	MIO_ASSERT (mio, q->tmridx == MIO_TMRIDX_INVALID);
	MIO_WQ_UNLINK(q);
	mio_freemem (mio, q);

	if (x <= -1) mio_dev_halt (dev);
}

static int __dev_write (mio_dev_t* dev, const void* data, mio_iolen_t len, const mio_ntime_t* tmout, void* wrctx, const mio_devaddr_t* dstaddr)
{
	mio_t* mio = dev->mio;
	const mio_uint8_t* uptr;
	mio_iolen_t urem, ulen;
	mio_wq_t* q;
	mio_cwq_t* cwq;
	mio_oow_t cwq_extra_aligned, cwqfl_index;
	int x;

	if (dev->dev_cap & MIO_DEV_CAP_OUT_CLOSED)
	{
		mio_seterrbfmt (mio, MIO_ENOCAPA, "unable to write to closed device");
		return -1;
	}

	uptr = data;
	urem = len;

	if (!MIO_WQ_IS_EMPTY(&dev->wq)) 
	{
		/* the writing queue is not empty. 
		 * enqueue this request immediately */
		goto enqueue_data;
	}

	if (dev->dev_cap & MIO_DEV_CAP_STREAM)
	{
		/* use the do..while() loop to be able to send a zero-length data */
		do
		{
			ulen = urem;
			x = dev->dev_mth->write(dev, data, &ulen, dstaddr);
			if (x <= -1) return -1;
			else if (x == 0) 
			{
				/* [NOTE] 
				 * the write queue is empty at this moment. a zero-length 
				 * request for a stream device can still get enqueued if the
				 * write callback returns 0 though i can't figure out if there
				 * is a compelling reason to do so 
				 */
				goto enqueue_data; /* enqueue remaining data */
			}
			else 
			{
				/* the write callback should return at most the number of requested
				 * bytes. but returning more is harmless as urem is of a signed type.
				 * for a zero-length request, it's necessary to return at least 1
				 * to indicate successful acknowlegement. otherwise, it gets enqueued
				 * as shown in the 'if' block right above. */
				urem -= ulen;
				uptr += ulen;
			}
		}
		while (urem > 0);

		if (len <= 0) /* original length */
		{
			/* a zero-length writing request is to close the writing end. this causes further write request to fail */
			dev->dev_cap |= MIO_DEV_CAP_OUT_CLOSED;
		}

		/* if i trigger the write completion callback here, the performance
		 * may increase, but there can be annoying recursion issues if the 
		 * callback requests another writing operation. it's imperative to
		 * delay the callback until this write function is finished.
		 * ---> if (dev->dev_evcb->on_write(dev, len, wrctx, dstaddr) <= -1) return -1; */
		goto enqueue_completed_write;
	}
	else
	{
		ulen = urem;

		x = dev->dev_mth->write(dev, data, &ulen, dstaddr);
		if (x <= -1) return -1;
		else if (x == 0) goto enqueue_data;

		/* partial writing is still considered ok for a non-stream device. */

		/* read the comment in the 'if' block above for why i enqueue the write completion event 
		 * instead of calling the event callback here...
		 *  ---> if (dev->dev_evcb->on_write(dev, ulen, wrctx, dstaddr) <= -1) return -1; */
		goto enqueue_completed_write;
	}

	return 1; /* written immediately and called on_write callback */

enqueue_data:
	if (dev->dev_cap & MIO_DEV_CAP_OUT_UNQUEUEABLE)
	{
		/* writing queuing is not requested. so return failure */
		mio_seterrbfmt (mio, MIO_ENOCAPA, "device incapable of queuing");
		return -1;
	}

	/* queue the remaining data*/
	q = (mio_wq_t*)mio_allocmem(mio, MIO_SIZEOF(*q) + (dstaddr? dstaddr->len: 0) + urem);
	if (!q) return -1;

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

	if (tmout && MIO_IS_POS_NTIME(tmout))
	{
		mio_tmrjob_t tmrjob;

		MIO_MEMSET (&tmrjob, 0, MIO_SIZEOF(tmrjob));
		tmrjob.ctx = q;
		mio_gettime (mio, &tmrjob.when);
		MIO_ADD_NTIME (&tmrjob.when, &tmrjob.when, tmout);
		tmrjob.handler = on_write_timeout;
		tmrjob.idxptr = &q->tmridx;

		q->tmridx = mio_instmrjob(mio, &tmrjob);
		if (q->tmridx == MIO_TMRIDX_INVALID) 
		{
			mio_freemem (mio, q);
			return -1;
		}
	}

	MIO_WQ_ENQ (&dev->wq, q);
	if (!(dev->dev_cap & MIO_DEV_CAP_OUT_WATCHED))
	{
		/* if output is not being watched, arrange to do so */
		if (mio_dev_watch(dev, MIO_DEV_WATCH_RENEW, MIO_DEV_EVENT_IN) <= -1)
		{
			unlink_wq (mio, q);
			mio_freemem (mio, q);
			return -1;
		}
	}

	return 0; /* request pused to a write queue. */

enqueue_completed_write:
	/* queue the remaining data*/
	cwq_extra_aligned = (dstaddr? dstaddr->len: 0);
	cwq_extra_aligned = MIO_ALIGN_POW2(cwq_extra_aligned, MIO_CWQFL_ALIGN);
	cwqfl_index = cwq_extra_aligned / MIO_CWQFL_SIZE;

	if (cwqfl_index < MIO_COUNTOF(mio->cwqfl) && mio->cwqfl[cwqfl_index])
	{
		/* take an available cwq object from the free cwq list */
		cwq = dev->mio->cwqfl[cwqfl_index];
		dev->mio->cwqfl[cwqfl_index] = cwq->q_next;
	}
	else
	{
		cwq = (mio_cwq_t*)mio_allocmem(mio, MIO_SIZEOF(*cwq) + cwq_extra_aligned);
		if (MIO_UNLIKELY(!cwq)) return -1;
	}

	MIO_MEMSET (cwq, 0, MIO_SIZEOF(*cwq));
	cwq->dev = dev;
	cwq->ctx = wrctx;
	if (dstaddr)
	{
		cwq->dstaddr.ptr = (mio_uint8_t*)(cwq + 1);
		cwq->dstaddr.len = dstaddr->len;
		MIO_MEMCPY (cwq->dstaddr.ptr, dstaddr->ptr, dstaddr->len);
	}
	else
	{
		cwq->dstaddr.len = 0;
	}

	cwq->olen = len;

	MIO_CWQ_ENQ (&dev->mio->cwq, cwq);
	dev->cw_count++; /* increment the number of complete write operations */
	return 0;
}

static int __dev_writev (mio_dev_t* dev, mio_iovec_t* iov, mio_iolen_t iovcnt, const mio_ntime_t* tmout, void* wrctx, const mio_devaddr_t* dstaddr)
{
	mio_t* mio = dev->mio;
	mio_iolen_t urem, len;
	mio_iolen_t index = 0, i, j;
	mio_wq_t* q;
	mio_cwq_t* cwq;
	mio_oow_t cwq_extra_aligned, cwqfl_index;
	int x;

	if (dev->dev_cap & MIO_DEV_CAP_OUT_CLOSED)
	{
		mio_seterrbfmt (mio, MIO_ENOCAPA, "unable to write to closed device");
		return -1;
	}

	len = 0;
	for (i = 0; i < iovcnt; i++) len += iov[i].iov_len;

	if (!MIO_WQ_IS_EMPTY(&dev->wq)) 
	{
		/* the writing queue is not empty. 
		 * enqueue this request immediately */
		urem = len;
		goto enqueue_data;
	}

	if (dev->dev_cap & MIO_DEV_CAP_STREAM)
	{
		/* use the do..while() loop to be able to send a zero-length data */
		mio_iolen_t backup_index = -1, dcnt;
		mio_iovec_t backup;

		do
		{
			dcnt = iovcnt - index;
			x = dev->dev_mth->writev(dev, &iov[index], &dcnt, dstaddr);
			if (x <= -1) return -1;
			else if (x == 0) 
			{
				/* [NOTE] 
				 * the write queue is empty at this moment. a zero-length 
				 * request for a stream device can still get enqueued if the
				 * write callback returns 0 though i can't figure out if there
				 * is a compelling reason to do so 
				 */
				goto enqueue_data; /* enqueue remaining data */
			}

			urem -= dcnt;
			while (index < iovcnt && (mio_oow_t)dcnt >= iov[index].iov_len)
				dcnt -= iov[index++].iov_len;

			if (index == iovcnt) break;

			if (backup_index != index)
			{
				if (backup_index >= 0) iov[backup_index] = backup;
				backup = iov[index];
				backup_index = index;
			}

			iov[index].iov_ptr = (void*)((mio_uint8_t*)iov[index].iov_ptr + dcnt);
			iov[index].iov_len -= dcnt;
		}
		while (1);

		if (backup_index >= 0) iov[backup_index] = backup;

		if (iovcnt <= 0) /* original vector count */
		{
			/* a zero-length writing request is to close the writing end. this causes further write request to fail */
			dev->dev_cap |= MIO_DEV_CAP_OUT_CLOSED;
		}

		/* if i trigger the write completion callback here, the performance
		 * may increase, but there can be annoying recursion issues if the 
		 * callback requests another writing operation. it's imperative to
		 * delay the callback until this write function is finished.
		 * ---> if (dev->dev_evcb->on_write(dev, len, wrctx, dstaddr) <= -1) return -1; */
		goto enqueue_completed_write;
	}
	else
	{
		mio_iolen_t dcnt;

		dcnt = iovcnt;
		x = dev->dev_mth->writev(dev, iov, &dcnt, dstaddr);
		if (x <= -1) return -1;
		else if (x == 0) goto enqueue_data;

		urem -= dcnt;
		/* partial writing is still considered ok for a non-stream device. */

		/* read the comment in the 'if' block above for why i enqueue the write completion event 
		 * instead of calling the event callback here...
		 *  ---> if (dev->dev_evcb->on_write(dev, ulen, wrctx, dstaddr) <= -1) return -1; */
		goto enqueue_completed_write;
	}

	return 1; /* written immediately and called on_write callback */

enqueue_data:
	if (dev->dev_cap & MIO_DEV_CAP_OUT_UNQUEUEABLE)
	{
		/* writing queuing is not requested. so return failure */
		mio_seterrbfmt (mio, MIO_ENOCAPA, "device incapable of queuing");
		return -1;
	}

	/* queue the remaining data*/
	q = (mio_wq_t*)mio_allocmem(mio, MIO_SIZEOF(*q) + (dstaddr? dstaddr->len: 0) + urem);
	if (!q) return -1;

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
	for (i = index, j = 0; i < iovcnt; i++)
	{
		MIO_MEMCPY (&q->ptr[j], iov[i].iov_ptr, iov[i].iov_len);
		j += iov[i].iov_len;
	}

	if (tmout && MIO_IS_POS_NTIME(tmout))
	{
		mio_tmrjob_t tmrjob;

		MIO_MEMSET (&tmrjob, 0, MIO_SIZEOF(tmrjob));
		tmrjob.ctx = q;
		mio_gettime (mio, &tmrjob.when);
		MIO_ADD_NTIME (&tmrjob.when, &tmrjob.when, tmout);
		tmrjob.handler = on_write_timeout;
		tmrjob.idxptr = &q->tmridx;

		q->tmridx = mio_instmrjob(mio, &tmrjob);
		if (q->tmridx == MIO_TMRIDX_INVALID) 
		{
			mio_freemem (mio, q);
			return -1;
		}
	}

	MIO_WQ_ENQ (&dev->wq, q);
	if (!(dev->dev_cap & MIO_DEV_CAP_OUT_WATCHED))
	{
		/* if output is not being watched, arrange to do so */
		if (mio_dev_watch(dev, MIO_DEV_WATCH_RENEW, MIO_DEV_EVENT_IN) <= -1)
		{
			unlink_wq (mio, q);
			mio_freemem (mio, q);
			return -1;
		}
	}

	return 0; /* request pused to a write queue. */

enqueue_completed_write:
	/* queue the remaining data*/
	cwq_extra_aligned = (dstaddr? dstaddr->len: 0);
	cwq_extra_aligned = MIO_ALIGN_POW2(cwq_extra_aligned, MIO_CWQFL_ALIGN);
	cwqfl_index = cwq_extra_aligned / MIO_CWQFL_SIZE;

	if (cwqfl_index < MIO_COUNTOF(mio->cwqfl) && mio->cwqfl[cwqfl_index])
	{
		/* take an available cwq object from the free cwq list */
		cwq = dev->mio->cwqfl[cwqfl_index];
		dev->mio->cwqfl[cwqfl_index] = cwq->q_next;
	}
	else
	{
		cwq = (mio_cwq_t*)mio_allocmem(mio, MIO_SIZEOF(*cwq) + cwq_extra_aligned);
		if (!cwq) return -1;
	}

	MIO_MEMSET (cwq, 0, MIO_SIZEOF(*cwq));
	cwq->dev = dev;
	cwq->ctx = wrctx;
	if (dstaddr)
	{
		cwq->dstaddr.ptr = (mio_uint8_t*)(cwq + 1);
		cwq->dstaddr.len = dstaddr->len;
		MIO_MEMCPY (cwq->dstaddr.ptr, dstaddr->ptr, dstaddr->len);
	}
	else
	{
		cwq->dstaddr.len = 0;
	}

	cwq->olen = len;

	MIO_CWQ_ENQ (&dev->mio->cwq, cwq);
	dev->cw_count++; /* increment the number of complete write operations */
	return 0;
}


int mio_dev_write (mio_dev_t* dev, const void* data, mio_iolen_t len, void* wrctx, const mio_devaddr_t* dstaddr)
{
	return __dev_write(dev, data, len, MIO_NULL, wrctx, dstaddr);
}

int mio_dev_writev (mio_dev_t* dev, mio_iovec_t* iov, mio_iolen_t iovcnt, void* wrctx, const mio_devaddr_t* dstaddr)
{
	return __dev_writev(dev, iov, iovcnt, MIO_NULL, wrctx, dstaddr);
}

int mio_dev_timedwrite (mio_dev_t* dev, const void* data, mio_iolen_t len, const mio_ntime_t* tmout, void* wrctx, const mio_devaddr_t* dstaddr)
{
	return __dev_write(dev, data, len, tmout, wrctx, dstaddr);
}

int mio_dev_timedwritev (mio_dev_t* dev, mio_iovec_t* iov, mio_iolen_t iovcnt, const mio_ntime_t* tmout, void* wrctx, const mio_devaddr_t* dstaddr)
{
	return __dev_writev(dev, iov, iovcnt, tmout, wrctx, dstaddr);
}

/* -------------------------------------------------------------------------- */

void mio_gettime (mio_t* mio, mio_ntime_t* now)
{
	mio_sys_gettime (mio, now);
	/* in mio_init(), mio->init_time has been set to the initialization time. 
	 * the time returned here gets offset by mio->init_time and 
	 * thus becomes relative to it. this way, it is kept small such that it
	 * can be represented in a small integer with leaving almost zero chance
	 * of overflow. */
	MIO_SUB_NTIME (now, now, &mio->init_time);  /* now = now - init_time */
}

/* -------------------------------------------------------------------------- */
void* mio_allocmem (mio_t* mio, mio_oow_t size)
{
	void* ptr;
	ptr = MIO_MMGR_ALLOC (mio->_mmgr, size);
	if (!ptr) mio_seterrnum (mio, MIO_ESYSMEM);
	return ptr;
}

void* mio_callocmem (mio_t* mio, mio_oow_t size)
{
	void* ptr;
	ptr = MIO_MMGR_ALLOC (mio->_mmgr, size);
	if (!ptr) mio_seterrnum (mio, MIO_ESYSMEM);
	else MIO_MEMSET (ptr, 0, size);
	return ptr;
}

void* mio_reallocmem (mio_t* mio, void* ptr, mio_oow_t size)
{
	ptr = MIO_MMGR_REALLOC (mio->_mmgr, ptr, size);
	if (!ptr) mio_seterrnum (mio, MIO_ESYSMEM);
	return ptr;
}

void mio_freemem (mio_t* mio, void* ptr)
{
	MIO_MMGR_FREE (mio->_mmgr, ptr);
}

/* ------------------------------------------------------------------------ */

struct fmt_uch_buf_t
{
	mio_t* mio;
	mio_uch_t* ptr;
	mio_oow_t len;
	mio_oow_t capa;
};
typedef struct fmt_uch_buf_t fmt_uch_buf_t;

static int fmt_put_bchars_to_uch_buf (mio_fmtout_t* fmtout, const mio_bch_t* ptr, mio_oow_t len)
{
	fmt_uch_buf_t* b = (fmt_uch_buf_t*)fmtout->ctx;
	mio_oow_t bcslen, ucslen;
	int n;

	bcslen = len;
	ucslen = b->capa - b->len;
	n = mio_conv_bchars_to_uchars_with_cmgr(ptr, &bcslen, &b->ptr[b->len], &ucslen, (b->mio? b->mio->_cmgr: mio_get_utf8_cmgr()), 1);
	b->len += ucslen;
	if (n <= -1) 
	{
		if (n == -2) 
		{
			return 0; /* buffer full. stop */
		}
		else
		{
			mio_seterrnum (b->mio, MIO_EECERR);
			return -1;
		}
	}

	return 1; /* success. carry on */
}

static int fmt_put_uchars_to_uch_buf (mio_fmtout_t* fmtout, const mio_uch_t* ptr, mio_oow_t len)
{
	fmt_uch_buf_t* b = (fmt_uch_buf_t*)fmtout->ctx;
	mio_oow_t n;

	/* this function null-terminates the destination. so give the restored buffer size */
	n = mio_copy_uchars_to_ucstr(&b->ptr[b->len], b->capa - b->len + 1, ptr, len);
	b->len += n;
	if (n < len)
	{
		if (b->mio) mio_seterrnum (b->mio, MIO_EBUFFULL);
		return 0; /* stop. insufficient buffer */
	}

	return 1; /* success */
}

mio_oow_t mio_vfmttoucstr (mio_t* mio, mio_uch_t* buf, mio_oow_t bufsz, const mio_uch_t* fmt, va_list ap)
{
	mio_fmtout_t fo;
	fmt_uch_buf_t fb;

	if (bufsz <= 0) return 0;

	MIO_MEMSET (&fo, 0, MIO_SIZEOF(fo));
	//fo.mmgr = mio->mmgr;
	fo.putbchars = fmt_put_bchars_to_uch_buf;
	fo.putuchars = fmt_put_uchars_to_uch_buf;
	fo.ctx = &fb;

	MIO_MEMSET (&fb, 0, MIO_SIZEOF(fb));
	fb.mio = mio;
	fb.ptr = buf;
	fb.capa = bufsz - 1;

	if (mio_ufmt_outv(&fo, fmt, ap) <= -1) return -1;

	buf[fb.len] = '\0';
	return fb.len;
}

mio_oow_t mio_fmttoucstr (mio_t* mio, mio_uch_t* buf, mio_oow_t bufsz, const mio_uch_t* fmt, ...)
{
	mio_oow_t x;
	va_list ap;

	va_start (ap, fmt);
	x = mio_vfmttoucstr(mio, buf, bufsz, fmt, ap);
	va_end (ap);

	return x;
}

/* ------------------------------------------------------------------------ */

struct fmt_bch_buf_t
{
	mio_t* mio;
	mio_bch_t* ptr;
	mio_oow_t len;
	mio_oow_t capa;
};
typedef struct fmt_bch_buf_t fmt_bch_buf_t;


static int fmt_put_bchars_to_bch_buf (mio_fmtout_t* fmtout, const mio_bch_t* ptr, mio_oow_t len)
{
	fmt_bch_buf_t* b = (fmt_bch_buf_t*)fmtout->ctx;
	mio_oow_t n;

	/* this function null-terminates the destination. so give the restored buffer size */
	n = mio_copy_bchars_to_bcstr(&b->ptr[b->len], b->capa - b->len + 1, ptr, len);
	b->len += n;
	if (n < len)
	{
		if (b->mio) mio_seterrnum (b->mio, MIO_EBUFFULL);
		return 0; /* stop. insufficient buffer */
	}

	return 1; /* success */
}


static int fmt_put_uchars_to_bch_buf (mio_fmtout_t* fmtout, const mio_uch_t* ptr, mio_oow_t len)
{
	fmt_bch_buf_t* b = (fmt_bch_buf_t*)fmtout->ctx;
	mio_oow_t bcslen, ucslen;
	int n;

	bcslen = b->capa - b->len;
	ucslen = len;
	n = mio_conv_uchars_to_bchars_with_cmgr(ptr, &ucslen, &b->ptr[b->len], &bcslen, (b->mio? b->mio->_cmgr: mio_get_utf8_cmgr()));
	b->len += bcslen;
	if (n <= -1)
	{
		if (n == -2)
		{
			return 0; /* buffer full. stop */
		}
		else
		{
			mio_seterrnum (b->mio, MIO_EECERR);
			return -1;
		}
	}

	return 1; /* success. carry on */
}

mio_oow_t mio_vfmttobcstr (mio_t* mio, mio_bch_t* buf, mio_oow_t bufsz, const mio_bch_t* fmt, va_list ap)
{
	mio_fmtout_t fo;
	fmt_bch_buf_t fb;

	if (bufsz <= 0) return 0;

	MIO_MEMSET (&fo, 0, MIO_SIZEOF(fo));
	//fo.mmgr = mio->mmgr;
	fo.putbchars = fmt_put_bchars_to_bch_buf;
	fo.putuchars = fmt_put_uchars_to_bch_buf;
	fo.ctx = &fb;

	MIO_MEMSET (&fb, 0, MIO_SIZEOF(fb));
	fb.mio = mio;
	fb.ptr = buf;
	fb.capa = bufsz - 1;
	if (mio_bfmt_outv(&fo, fmt, ap) <= -1) return -1;

	buf[fb.len] = '\0';
	return fb.len;
}

mio_oow_t mio_fmttobcstr (mio_t* mio, mio_bch_t* buf, mio_oow_t bufsz, const mio_bch_t* fmt, ...)
{
	mio_oow_t x;
	va_list ap;

	va_start (ap, fmt);
	x = mio_vfmttobcstr(mio, buf, bufsz, fmt, ap);
	va_end (ap);

	return x;
}

/* ------------------------------------------------------------------------ */
