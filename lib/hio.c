/*
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
    IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
    OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
    IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
    INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
    NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
    THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "hio-prv.h"
#include <hio-fmt.h>
#include <stdlib.h> /* malloc, free, etc */

#define DEV_CAP_ALL_WATCHED (HIO_DEV_CAP_IN_WATCHED | HIO_DEV_CAP_OUT_WATCHED | HIO_DEV_CAP_PRI_WATCHED)

static void clear_unneeded_cfmbs (hio_t* hio);
static int schedule_kill_zombie_job (hio_dev_t* dev);
static int kill_and_free_device (hio_dev_t* dev, int force);

static void on_read_timeout (hio_t* hio, const hio_ntime_t* now, hio_tmrjob_t* job);
static void on_write_timeout (hio_t* hio, const hio_ntime_t* now, hio_tmrjob_t* job);

struct wq_sendfile_data_t
{
	hio_syshnd_t in_fd;
	hio_foff_t foff;
};
typedef struct wq_sendfile_data_t wq_sendfile_data_t;

/* ========================================================================= */

static void* mmgr_alloc (hio_mmgr_t* mmgr, hio_oow_t size)
{
	return malloc(size);
}

static void* mmgr_realloc (hio_mmgr_t* mmgr, void* ptr, hio_oow_t size)
{
	return realloc(ptr, size);
}

static void mmgr_free (hio_mmgr_t* mmgr, void* ptr)
{
	return free (ptr);
}

static hio_mmgr_t default_mmgr =
{
	mmgr_alloc,
	mmgr_realloc,
	mmgr_free,
	HIO_NULL
};

/* ========================================================================= */

hio_t* hio_open (hio_mmgr_t* mmgr, hio_oow_t xtnsize, hio_cmgr_t* cmgr, hio_bitmask_t features, hio_oow_t tmrcapa, hio_errinf_t* errinfo)
{
	hio_t* hio;

	if (!mmgr) mmgr = &default_mmgr;
	if (!cmgr) cmgr = hio_get_utf8_cmgr();

	hio = (hio_t*)HIO_MMGR_ALLOC(mmgr, HIO_SIZEOF(hio_t) + xtnsize);
	if (hio)
	{
		if (hio_init(hio, mmgr, cmgr, features, tmrcapa) <= -1)
		{
			if (errinfo) hio_geterrinf (hio, errinfo);
			HIO_MMGR_FREE (mmgr, hio);
			hio = HIO_NULL;
		}
		else HIO_MEMSET (hio + 1, 0, xtnsize);
	}
	else if (errinfo)
	{
		errinfo->num = HIO_ESYSMEM;
		hio_copy_oocstr (errinfo->msg, HIO_COUNTOF(errinfo->msg), hio_errnum_to_errstr(HIO_ESYSMEM));
	}

	return hio;
}

void hio_close (hio_t* hio)
{
	hio_fini (hio);
	HIO_MMGR_FREE (hio->_mmgr, hio);
}

int hio_init (hio_t* hio, hio_mmgr_t* mmgr, hio_cmgr_t* cmgr, hio_bitmask_t features, hio_oow_t tmrcapa)
{
	int sys_inited = 0;

	HIO_MEMSET (hio, 0, HIO_SIZEOF(*hio));
	hio->_instsize = HIO_SIZEOF(*hio);
	hio->_mmgr = mmgr;
	hio->_cmgr = cmgr;
	hio->_features = features;

	/* initialize data for logging support */
	hio->option.log_mask = HIO_LOG_ALL_LEVELS | HIO_LOG_ALL_TYPES;
	hio->log.capa = HIO_ALIGN_POW2(1, HIO_LOG_CAPA_ALIGN); /* TODO: is this a good initial size? */
	/* alloate the log buffer in advance though it may get reallocated
	 * in put_oocs and put_ooch in fmtout.c. this is to let the logging
	 * routine still function despite some side-effects when
	 * reallocation fails */
	/* +1 required for consistency with put_oocs and put_ooch in fmtout.c */
	hio->log.ptr = hio_allocmem(hio, (hio->log.capa + 1) * HIO_SIZEOF(*hio->log.ptr));
	if (HIO_UNLIKELY(!hio->log.ptr)) goto oops;

	/* inititalize the system-side logging */
	if (HIO_UNLIKELY(hio_sys_init(hio) <= -1)) goto oops;
	sys_inited = 1;

	/* initialize the timer object */
	if (tmrcapa <= 0) tmrcapa = 1;
	hio->tmr.jobs = hio_allocmem(hio, tmrcapa * HIO_SIZEOF(hio_tmrjob_t));
	if (HIO_UNLIKELY(!hio->tmr.jobs)) goto oops;

	hio->tmr.capa = tmrcapa;

	HIO_CFMBL_INIT (&hio->cfmb);
	HIO_DEVL_INIT (&hio->actdev);
	HIO_DEVL_INIT (&hio->hltdev);
	HIO_DEVL_INIT (&hio->zmbdev);
	HIO_CWQ_INIT (&hio->cwq);
	HIO_SVCL_INIT (&hio->actsvc);

	hio_sys_gettime (hio, &hio->init_time);
	return 0;

oops:
	if (hio->tmr.jobs) hio_freemem (hio, hio->tmr.jobs);

	if (sys_inited) hio_sys_fini (hio);

	if (hio->log.ptr) hio_freemem (hio, hio->log.ptr);
	hio->log.capa = 0;
	return -1;
}

void hio_fini (hio_t* hio)
{
	hio_dev_t* dev, * next_dev;
	hio_dev_t diehard;
	hio_oow_t i;
	hio_oow_t nactdevs = 0, nhltdevs = 0, nzmbdevs = 0, ndieharddevs = 0; /* statistics */

	hio->_fini_in_progress = 1;

	/* clean up free cwq list */
	for (i = 0; i < HIO_COUNTOF(hio->cwqfl); i++)
	{
		hio_cwq_t* cwq;
		while ((cwq = hio->cwqfl[i]))
		{
			hio->cwqfl[i] = cwq->q_next;
			hio_freemem (hio, cwq);
		}
	}

	/* clean up unfired cwq entries - calling fire_cwq_handlers() might not be good here. */
	while (!HIO_CWQ_IS_EMPTY(&hio->cwq))
	{
		hio_cwq_t* cwq;
		cwq = HIO_CWQ_HEAD(&hio->cwq);
		HIO_CWQ_UNLINK (cwq);
		hio_freemem (hio, cwq);
	}

	/* kill services before killing devices */
	while (!HIO_SVCL_IS_EMPTY(&hio->actsvc))
	{
		hio_svc_t* svc;

		svc = HIO_SVCL_FIRST_SVC(&hio->actsvc);
		if (svc->svc_stop)
		{
			/* the stop callback must unregister itself */
			svc->svc_stop (svc);
		}
		else
		{
			/* unregistration only if no stop callback is designated */
			HIO_SVCL_UNLINK_SVC (svc);
		}
	}

	/* kill all registered devices */
	while (!HIO_DEVL_IS_EMPTY(&hio->actdev))
	{
		hio_dev_kill (HIO_DEVL_FIRST_DEV(&hio->actdev));
		nactdevs++;
	}

	/* kill all halted devices */
	while (!HIO_DEVL_IS_EMPTY(&hio->hltdev))
	{
		hio_dev_kill (HIO_DEVL_FIRST_DEV(&hio->hltdev));
		nhltdevs++;
	}

	/* clean up all zombie devices */
	HIO_DEVL_INIT (&diehard);
	for (dev = HIO_DEVL_FIRST_DEV(&hio->zmbdev); !HIO_DEVL_IS_NIL_DEV(&hio->zmbdev, dev); )
	{
		kill_and_free_device (dev, 1);
		if (HIO_DEVL_FIRST_DEV(&hio->zmbdev) == dev)
		{
			/* the device has not been freed. go on to the next one */
			next_dev = dev->dev_next;

			/* remove the device from the zombie device list */
			HIO_DEVL_UNLINK_DEV (dev);
			dev->dev_cap &= ~HIO_DEV_CAP_ZOMBIE;

			/* put it to a private list for aborting */
			HIO_DEVL_APPEND_DEV (&diehard, dev);

			dev = next_dev;
		}
		else
		{
			nzmbdevs++;
			dev = HIO_DEVL_FIRST_DEV(&hio->zmbdev);
		}
	}

	while (!HIO_DEVL_IS_EMPTY(&diehard))
	{
		/* if the kill method returns failure, it can leak some resource
		 * because the device is freed regardless of the failure when 2
		 * is given to kill_and_free_device(). */
		dev = HIO_DEVL_FIRST_DEV(&diehard);
		HIO_ASSERT (hio, !(dev->dev_cap & (HIO_DEV_CAP_ACTIVE | HIO_DEV_CAP_HALTED | HIO_DEV_CAP_ZOMBIE)));
		HIO_DEVL_UNLINK_DEV (dev);
		kill_and_free_device (dev, 2);
		ndieharddevs++;
	}

	/* purge scheduled timer jobs and kill the timer */
	hio_cleartmrjobs (hio);
	hio_freemem (hio, hio->tmr.jobs);

	/* clear unneeded cfmbs insistently - a misbehaving checker will make this cleaning step loop forever*/
	while (!HIO_CFMBL_IS_EMPTY(&hio->cfmb)) clear_unneeded_cfmbs (hio);

	hio_sys_fini (hio); /* finalize the system dependent data */

	if (hio->log.ptr)
	{
		hio_freemem (hio, hio->log.ptr);
		hio->log.ptr = HIO_NULL;
	}

	if (hio->option.log_target_u)
	{
		hio_freemem (hio, hio->option.log_target_u);
		hio->option.log_target_u = HIO_NULL;
	}

	if (hio->option.log_target_b)
	{
		hio_freemem (hio, hio->option.log_target_b);
		hio->option.log_target_b = HIO_NULL;
	}
}

int hio_setoption (hio_t* hio, hio_option_t id, const void* value)
{
	switch (id)
	{
		case HIO_TRAIT:
			hio->option.trait = *(hio_bitmask_t*)value;
			break;

		case HIO_LOG_MASK:
			hio->option.log_mask = *(hio_bitmask_t*)value;
			break;

		case HIO_LOG_MAXCAPA:
			hio->option.log_maxcapa = *(hio_oow_t*)value;
			break;

		case HIO_LOG_TARGET_BCSTR:
		{
			hio_bch_t* v1;
			hio_uch_t* v2;

			v1 = hio_dupbcstr(hio, value, HIO_NULL);
			if (HIO_UNLIKELY(!v1)) return -1;

			v2 = hio_dupbtoucstr(hio, value, HIO_NULL, 1);
			if (HIO_UNLIKELY(!v2))
			{
				hio_freemem (hio, v1);
				return -1;
			}

			hio->option.log_target_u = v2;
			hio->option.log_target_b = v1;

			hio_sys_resetlog (hio);
			break;
		}

		case HIO_LOG_TARGET_UCSTR:
		{
			hio_uch_t* v1;
			hio_bch_t* v2;

			v1 = hio_dupucstr(hio, value, HIO_NULL);
			if (HIO_UNLIKELY(!v1)) return -1;

			v2 = hio_duputobcstr(hio, value, HIO_NULL);
			if (HIO_UNLIKELY(!v2))
			{
				hio_freemem (hio, v1);
				return -1;
			}

			hio->option.log_target_u = v1;
			hio->option.log_target_b = v2;

			hio_sys_resetlog (hio);
			break;
		}

		case HIO_LOG_TARGET_BCS:
		{
			hio_bch_t* v1;
			hio_uch_t* v2;
			hio_bcs_t* v = (hio_bcs_t*)value;

			v1 = hio_dupbchars(hio, v->ptr, v->len);
			if (HIO_UNLIKELY(!v1)) return -1;

			v2 = hio_dupbtouchars(hio, v->ptr, v->len, HIO_NULL, 0);
			if (HIO_UNLIKELY(!v2))
			{
				hio_freemem (hio, v1);
				return -1;
			}

			hio->option.log_target_u = v2;
			hio->option.log_target_b = v1;

			hio_sys_resetlog (hio);
			break;
		}

		case HIO_LOG_TARGET_UCS:
		{
			hio_uch_t* v1;
			hio_bch_t* v2;
			hio_ucs_t* v = (hio_ucs_t*)value;

			v1 = hio_dupuchars(hio, v->ptr, v->len);
			if (HIO_UNLIKELY(!v1)) return -1;

			v2 = hio_duputobchars(hio, v->ptr, v->len, HIO_NULL);
			if (HIO_UNLIKELY(!v2))
			{
				hio_freemem (hio, v1);
				return -1;
			}

			hio->option.log_target_u = v1;
			hio->option.log_target_b = v2;

			hio_sys_resetlog (hio);
			break;
		}

		case HIO_LOG_WRITER:
			hio->option.log_writer = (hio_log_writer_t)value;
			break;

		default:
			goto einval;
	}

	return 0;

einval:
	hio_seterrnum (hio, HIO_EINVAL);
	return -1;
}

int hio_getoption (hio_t* hio, hio_option_t id, void* value)
{
	switch  (id)
	{
		case HIO_TRAIT:
			*(hio_bitmask_t*)value = hio->option.trait;
			return 0;

		case HIO_LOG_MASK:
			*(hio_bitmask_t*)value = hio->option.log_mask;
			return 0;

		case HIO_LOG_MAXCAPA:
			*(hio_oow_t*)value = hio->option.log_maxcapa;
			return 0;

		case HIO_LOG_TARGET_BCSTR:
			*(hio_bch_t**)value = hio->option.log_target_b;
			return 0;

		case HIO_LOG_TARGET_UCSTR:
			*(hio_uch_t**)value = hio->option.log_target_u;
			return 0;

		case HIO_LOG_TARGET_BCS:
			((hio_bcs_t*)value)->ptr = hio->option.log_target_b;
			((hio_bcs_t*)value)->len = hio_count_bcstr(hio->option.log_target_b);
			return 0;

		case HIO_LOG_TARGET_UCS:
			((hio_ucs_t*)value)->ptr = hio->option.log_target_u;
			((hio_ucs_t*)value)->len = hio_count_ucstr(hio->option.log_target_u);
			return 0;

		case HIO_LOG_WRITER:
			*(hio_log_writer_t*)value = hio->option.log_writer;
			return 0;
	};

	hio_seterrnum (hio, HIO_EINVAL);
	return -1;
}

int hio_prologue (hio_t* hio)
{
	/* TODO: */
	return 0;
}

void hio_epilogue (hio_t* hio)
{
	/* TODO: */
}

static HIO_INLINE void unlink_wq (hio_t* hio, hio_wq_t* q)
{
	if (q->tmridx != HIO_TMRIDX_INVALID)
	{
		hio_deltmrjob (hio, q->tmridx);
		HIO_ASSERT (hio, q->tmridx == HIO_TMRIDX_INVALID);
	}
	HIO_WQ_UNLINK (q);
}

static void fire_cwq_handlers (hio_t* hio)
{
	/* execute callbacks for completed write operations */
	while (!HIO_CWQ_IS_EMPTY(&hio->cwq))
	{
		hio_cwq_t* cwq;
		hio_oow_t cwqfl_index;
		hio_dev_t* dev_to_halt;

		cwq = HIO_CWQ_HEAD(&hio->cwq);
		if (cwq->dev->dev_evcb->on_write(cwq->dev, cwq->olen, cwq->ctx, &cwq->dstaddr) <= -1)
		{
			dev_to_halt = cwq->dev;
		}
		else
		{
			dev_to_halt = HIO_NULL;
		}
		cwq->dev->cw_count--;
		HIO_CWQ_UNLINK (cwq);

		cwqfl_index = HIO_ALIGN_POW2(cwq->dstaddr.len, HIO_CWQFL_ALIGN) / HIO_CWQFL_SIZE;
		if (cwqfl_index < HIO_COUNTOF(hio->cwqfl))
		{
			/* reuse the cwq object if dstaddr is 0 in size. chain it to the free list */
			cwq->q_next = hio->cwqfl[cwqfl_index];
			hio->cwqfl[cwqfl_index] = cwq;
		}
		else
		{
			/* TODO: more reuse of objects of different size? */
			hio_freemem (hio, cwq);
		}

		if (dev_to_halt)
		{
			HIO_DEBUG2 (hio, "DEV(%p) - halting a device for on_write error upon write completion[1] - %js\n", dev_to_halt, hio_geterrmsg(hio));
			hio_dev_halt (dev_to_halt);
		}
	}
}

static void fire_cwq_handlers_for_dev (hio_t* hio, hio_dev_t* dev, int for_kill)
{
	hio_cwq_t* cwq, * next;

	HIO_ASSERT (hio, dev->cw_count > 0);  /* Ensure to check dev->cw_count before calling this function */

	cwq = HIO_CWQ_HEAD(&hio->cwq);
	while (cwq != &hio->cwq)
	{
		next = HIO_CWQ_NEXT(cwq);
		if (cwq->dev == dev) /* TODO: THIS LOOP TOO INEFFICIENT??? MAINTAIN PER-DEVICE LIST OF CWQ? */
		{
			hio_dev_t* dev_to_halt;
			hio_oow_t cwqfl_index;

			if (cwq->dev->dev_evcb->on_write(cwq->dev, cwq->olen, cwq->ctx, &cwq->dstaddr) <= -1)
			{
				dev_to_halt = cwq->dev;
			}
			else
			{
				dev_to_halt = HIO_NULL;
			}

			cwq->dev->cw_count--;
			HIO_CWQ_UNLINK (cwq);

			cwqfl_index = HIO_ALIGN_POW2(cwq->dstaddr.len, HIO_CWQFL_ALIGN) / HIO_CWQFL_SIZE;
			if (cwqfl_index < HIO_COUNTOF(hio->cwqfl))
			{
				/* reuse the cwq object if dstaddr is 0 in size. chain it to the free list */
				cwq->q_next = hio->cwqfl[cwqfl_index];
				hio->cwqfl[cwqfl_index] = cwq;
			}
			else
			{
				/* TODO: more reuse of objects of different size? */
				hio_freemem (hio, cwq);
			}

			if (!for_kill && dev_to_halt)
			{
				HIO_DEBUG2 (hio, "DEV(%p) - halting a device for on_write error upon write completion[2] - %js\n", dev_to_halt, hio_geterrmsg(hio));
			       	hio_dev_halt (dev_to_halt);
			}
		}
		cwq = next;
	}

}

static HIO_INLINE void handle_event (hio_t* hio, hio_dev_t* dev, int events, int rdhup)
{
	HIO_ASSERT (hio, hio == dev->hio);

	dev->dev_cap &= ~HIO_DEV_CAP_RENEW_REQUIRED;

	HIO_ASSERT (hio, hio == dev->hio);

	if (dev->dev_evcb->ready)
	{
		int x, xevents;

		xevents = events;
		if (rdhup) xevents |= HIO_DEV_EVENT_HUP;

		/* return value of ready()
		 *   <= -1 - failure. kill the device.
		 *   == 0 - ok. but don't invoke recv() or send().
		 *   >= 1 - everything is ok. */
		x = dev->dev_evcb->ready(dev, xevents);
		if (x <= -1)
		{
			HIO_DEBUG2 (hio, "DEV(%p) - halting a device for ready callback error - %js\n", dev, hio_geterrmsg(hio));
			hio_dev_halt (dev);
			return;
		}
		else if (x == 0) goto skip_evcb;
	}

	if (dev && (events & HIO_DEV_EVENT_PRI))
	{
		/* urgent data */
		/* TODO: implement urgent data handling */
		/*x = dev->dev_mth->urgread(dev, hio->bugbuf, &len);*/
	}

	if (dev && (events & HIO_DEV_EVENT_OUT))
	{
		/* write pending requests */
		while (!HIO_WQ_IS_EMPTY(&dev->wq))
		{
			hio_wq_t* q;
			const hio_uint8_t* uptr;
			hio_iolen_t urem, ulen;
			int x;

			q = HIO_WQ_HEAD(&dev->wq);

			uptr = q->ptr;
			urem = q->len;

		send_leftover:
			ulen = urem;
			if (q->sendfile)
			{
				x = dev->dev_mth->sendfile(dev, ((wq_sendfile_data_t*)uptr)->in_fd, ((wq_sendfile_data_t*)uptr)->foff, &ulen);
			}
			else
			{
				x = dev->dev_mth->write(dev, uptr, &ulen, &q->dstaddr);
			}
			if (x <= -1)
			{
				HIO_DEBUG2 (hio, "DEV(%p) - halting a device for write failure - %js\n", dev, hio_geterrmsg(hio));
				hio_dev_halt (dev);
				dev = HIO_NULL;
				break;
			}
			else if (x == 0)
			{
				/* keep the left-over */
				if (!q->sendfile) HIO_MEMMOVE (q->ptr, uptr, urem);
				q->len = urem;
				break;
			}
			else
			{
				if (q->sendfile)
				{
					((wq_sendfile_data_t*)(q->ptr))->foff += ulen;
				}
				else
				{
					uptr += ulen;
				}
				urem -= ulen;

				if (urem <= 0)
				{
					/* finished writing a single write request */
					int y, out_closed = 0;

					if (q->len <= 0 && (dev->dev_cap & HIO_DEV_CAP_STREAM))
					{
						/* it was a zero-length write request.
						 * for a stream, it is to close the output. */
						dev->dev_cap |= HIO_DEV_CAP_OUT_CLOSED;
						dev->dev_cap |= HIO_DEV_CAP_RENEW_REQUIRED;
						out_closed = 1;
					}

					unlink_wq (hio, q);
					y = dev->dev_evcb->on_write(dev, q->olen, q->ctx, &q->dstaddr);
					hio_freemem (hio, q);

					if (y <= -1)
					{
						HIO_DEBUG2 (hio, "DEV(%p) - halting a device for on_write error - %js\n", dev, hio_geterrmsg(hio));
						hio_dev_halt (dev);
						dev = HIO_NULL;
						break;
					}

					if (out_closed)
					{
						/* drain all pending requests.
						 * callbacks are skipped for drained requests */
						while (!HIO_WQ_IS_EMPTY(&dev->wq))
						{
							q = HIO_WQ_HEAD(&dev->wq);
							unlink_wq (hio, q);
							hio_freemem (hio, q);
						}
						break;
					}
				}
				else goto send_leftover;
			}
		}

		if (dev && HIO_WQ_IS_EMPTY(&dev->wq))
		{
			/* no pending request to write */
			if ((dev->dev_cap & HIO_DEV_CAP_IN_CLOSED) && (dev->dev_cap & HIO_DEV_CAP_OUT_CLOSED))
			{
				HIO_DEBUG1 (hio, "DEV(%p) - halting a device for closed input and output in output handler\n", dev);
				hio_dev_halt (dev);
				dev = HIO_NULL;
			}
			else
			{
				dev->dev_cap |= HIO_DEV_CAP_RENEW_REQUIRED;
			}
		}
	}

	if (dev && (events & HIO_DEV_EVENT_IN))
	{
		hio_devaddr_t srcaddr;
		hio_iolen_t len;
		int x;

		/* the devices are all non-blocking. read as much as possible
		 * if on_read callback returns 1 or greater. read only once
		 * if the on_read calllback returns 0. */
		while (1)
		{
			len = HIO_COUNTOF(hio->bigbuf);
			x = dev->dev_mth->read(dev, hio->bigbuf, &len, &srcaddr);
			if (x <= -1)
			{
				HIO_DEBUG2 (hio, "DEV(%p) - halting a device for read failure - %js\n", dev, hio_geterrmsg(hio));
				hio_dev_halt (dev);
				dev = HIO_NULL;
				break;
			}

			if (dev->rtmridx != HIO_TMRIDX_INVALID)
			{
				/* delete the read timeout job on the device as the
				 * read operation will be reported below. */
				hio_tmrjob_t tmrjob;

				HIO_MEMSET (&tmrjob, 0, HIO_SIZEOF(tmrjob));
				tmrjob.ctx = dev;
				hio_gettime (hio, &tmrjob.when);
				HIO_ADD_NTIME (&tmrjob.when, &tmrjob.when, &dev->rtmout);
				tmrjob.handler = on_read_timeout;
				tmrjob.idxptr = &dev->rtmridx;

				hio_updtmrjob (hio, dev->rtmridx, &tmrjob);

				/*hio_deltmrjob (hio, dev->rtmridx);
				dev->rtmridx = HIO_TMRIDX_INVALID;*/
			}

			if (x == 0)
			{
				/* no data is available - EWOULDBLOCK or something similar */
				break;
			}
			else /*if (x >= 1) */
			{
				/* call on_write() callbacks enqueued from the device before calling on_read().
				 * if on_write() callback is delayed, there can be out-of-order execution
				 * between on_read() and on_write() callbacks. for instance, if a write request
				 * is started from within on_read() callback, and the input data is available
				 * in the next iteration of this loop, the on_read() callback is triggered
				 * before the on_write() callbacks scheduled before that on_read() callback. */
			#if 0
				if (dev->cw_count > 0)
				{
					fire_cwq_handlers_for_dev (hio, dev);
					/* it will still invoke the on_read() callbak below even if
					 * the device gets halted inside fire_cwq_handlers_for_dev() */
				}
			#else
				/* currently fire_cwq_handlers_for_dev() scans the entire cwq list.
				 * i might as well triggger handlers for all devices */
				fire_cwq_handlers (hio);
			#endif

				if (len <= 0 && (dev->dev_cap & HIO_DEV_CAP_STREAM))
				{
					/* EOF received. for a stream device, a zero-length
					 * read is interpreted as EOF. */
					dev->dev_cap |= HIO_DEV_CAP_IN_CLOSED;
					dev->dev_cap |= HIO_DEV_CAP_RENEW_REQUIRED;

					/* call the on_read callback to report EOF */
					if (dev->dev_evcb->on_read(dev, hio->bigbuf, len, &srcaddr) <= -1 ||
					    (dev->dev_cap & HIO_DEV_CAP_OUT_CLOSED))
					{
						/* 1. input ended and its reporting failed or
						 * 2. input ended and no writing is possible */
						if (dev->dev_cap & HIO_DEV_CAP_OUT_CLOSED)
							HIO_DEBUG1 (hio, "DEV(%p) - halting a stream device on input EOF as output is also closed\n", dev);
						else
							HIO_DEBUG2 (hio, "DEV(%p) - halting a stream device for on_read failure while output is closed - %js\n", dev, hio_geterrmsg(hio));
						hio_dev_halt (dev);
						dev = HIO_NULL;
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
					y = dev->dev_evcb->on_read(dev, hio->bigbuf, len, &srcaddr);
					if (y <= -1)
					{
						HIO_DEBUG2 (hio, "DEV(%p) - halting a non-stream device for on_read failure while output is closed - %js\n", dev, hio_geterrmsg(hio));
						hio_dev_halt (dev);
						dev = HIO_NULL;
						break;
					}
					else if (y == 0)
					{
						/* don't be greedy. read only once for this loop iteration */
						break;
					}
				}
			}
		}
	}

	if (dev)
	{
		if (events & (HIO_DEV_EVENT_ERR | HIO_DEV_EVENT_HUP))
		{
			/* if error or hangup has been reported on the device,
			 * halt the device. this check is performed after
			 * EPOLLIN or EPOLLOUT check because EPOLLERR or EPOLLHUP
			 * can be set together with EPOLLIN or EPOLLOUT. */
			if (!(dev->dev_cap & HIO_DEV_CAP_IN_CLOSED))
			{
				/* this is simulated EOF. the INPUT side has not been closed on the device
				 * but there is the hangup/error event. */
				dev->dev_evcb->on_read (dev, HIO_NULL, -!!(events & HIO_DEV_EVENT_ERR), HIO_NULL);
				/* i don't care about the return value since the device will be halted below
				 * if both HIO_DEV_CAP_IN_CLOSE and HIO_DEV_CAP_OUT_CLOSED are set */
			}

			dev->dev_cap |= HIO_DEV_CAP_IN_CLOSED | HIO_DEV_CAP_OUT_CLOSED;
			dev->dev_cap |= HIO_DEV_CAP_RENEW_REQUIRED;
		}
		else if (dev && rdhup)
		{
			if (events & (HIO_DEV_EVENT_IN | HIO_DEV_EVENT_OUT | HIO_DEV_EVENT_PRI))
			{
				/* it may be a half-open state. don't do anything here
				 * to let the next read detect EOF */
			}
			else
			{
				dev->dev_cap |= HIO_DEV_CAP_IN_CLOSED | HIO_DEV_CAP_OUT_CLOSED;
				dev->dev_cap |= HIO_DEV_CAP_RENEW_REQUIRED;
			}
		}

		if ((dev->dev_cap & HIO_DEV_CAP_IN_CLOSED) && (dev->dev_cap & HIO_DEV_CAP_OUT_CLOSED))
		{
			HIO_DEBUG1 (hio, "DEV(%p) - halting a device for closed input and output\n", dev);
			hio_dev_halt (dev);
			dev = HIO_NULL;
		}
	}

skip_evcb:
	if (dev && (dev->dev_cap & HIO_DEV_CAP_RENEW_REQUIRED) && hio_dev_watch(dev, HIO_DEV_WATCH_RENEW, HIO_DEV_EVENT_IN) <= -1)
	{
		HIO_DEBUG1 (hio, "DEV(%p) - halting a device for wathcer renewal failure\n", dev);
		hio_dev_halt (dev);
		dev = HIO_NULL;
	}
}

static void clear_unneeded_cfmbs (hio_t* hio)
{
	hio_cfmb_t* cur, * next;

	cur = HIO_CFMBL_FIRST_CFMB(&hio->cfmb);
	while (!HIO_CFMBL_IS_NIL_CFMB(&hio->cfmb, cur))
	{
		next = HIO_CFMBL_NEXT_CFMB(cur);
		if (!cur->cfmb_checker || cur->cfmb_checker(hio, cur))
		{
			HIO_CFMBL_UNLINK_CFMB (cur);
			/*hio_freemem (hio, cur);*/
			cur->cfmb_freeer (hio, cur);
		}
		cur = next;
	}
}

static void kill_all_halted_devices (hio_t* hio)
{
	/* kill all halted devices */
	while (!HIO_DEVL_IS_EMPTY(&hio->hltdev))
	{
		hio_dev_t* dev = HIO_DEVL_FIRST_DEV(&hio->hltdev);
		HIO_DEBUG1 (hio, "MIO - Killing HALTED device %p\n", dev);
		hio_dev_kill (dev);
		HIO_DEBUG1 (hio, "MIO - Killed HALTED device %p\n", dev);
	}
}

static HIO_INLINE int __exec (hio_t* hio)
{
	int ret = 0;

	/* clear unneeded cfmbs - i hate to do this. TODO: should i do this less frequently? if less frequent, would it accumulate too many blocks? */
	if (!HIO_CFMBL_IS_EMPTY(&hio->cfmb)) clear_unneeded_cfmbs (hio);

	/* execute callbacks for completed write operations */
	fire_cwq_handlers (hio);

	/* execute the scheduled jobs before checking devices with the
	 * multiplexer. the scheduled jobs can safely destroy the devices */
	hio_firetmrjobs (hio, HIO_NULL, HIO_NULL);

	/* execute callbacks for completed write operations again in case
	 * some works initiated in the timer jobs have complted and added to CWQ.
	 * e.g. write() in a timer job gets completed immediately. */
	fire_cwq_handlers (hio);

	if (HIO_LIKELY(!HIO_DEVL_IS_EMPTY(&hio->actdev) || hio->tmr.size > 0))
	{
		/* wait on the multiplexer only if there is at least 1 active device */
		hio_ntime_t tmout;

		kill_all_halted_devices (hio);

		if (hio_gettmrtmout(hio, HIO_NULL, &tmout) <= 0)
		{
			/* defaults to 0 or 1 second if timeout can't be acquired.
			 * if this timeout affects how fast the halted device will get killed.
			 * if there is a halted device, set timeout to 0. otherwise set it to 1*/
			tmout.sec = !!HIO_DEVL_IS_EMPTY(&hio->hltdev); /* TODO: don't use 1. make this longer value configurable */
			tmout.nsec = 0;
		}

		if (hio_sys_waitmux(hio, &tmout, handle_event) <= -1)
		{
			HIO_DEBUG0 (hio, "MIO - WARNING - Failed to wait on mutiplexer\n");
			ret = -1;
		}
	}

	kill_all_halted_devices (hio);
	return ret;
}

int hio_exec (hio_t* hio)
{
	/* never call this if you disabled this feature */
	HIO_ASSERT (hio, (hio->_features & HIO_FEATURE_MUX));
	return __exec(hio);
}

void hio_stop (hio_t* hio, hio_stopreq_t stopreq)
{
	/* never call this if you disabled this feature */
	HIO_ASSERT (hio, (hio->_features & HIO_FEATURE_MUX));
	hio->stopreq = stopreq;
	hio_sys_intrmux (hio);
}

int hio_loop (hio_t* hio)
{
	int ret = 0;

	/* never call this if you disabled this feature */
	HIO_ASSERT (hio, (hio->_features & HIO_FEATURE_MUX));

	if (HIO_UNLIKELY(HIO_DEVL_IS_EMPTY(&hio->actdev) && hio->tmr.size <= 0)) return 0;

	hio->stopreq = HIO_STOPREQ_NONE;

	if (hio_prologue(hio) <= -1)
	{
		ret = -1;
	}
	else
	{
		while (HIO_LIKELY(hio->stopreq == HIO_STOPREQ_NONE))
		{
			if (HIO_UNLIKELY(HIO_DEVL_IS_EMPTY(&hio->actdev) && hio->tmr.size <= 0)) break;

			ret = __exec(hio);
			if (ret <= -1) break;
			if (HIO_UNLIKELY(hio->stopreq == HIO_STOPREQ_WATCHER_ERROR))
			{
				const hio_ooch_t* prev_errmsg;

				prev_errmsg = hio_backuperrmsg(hio);

				/* this previous error message may be off the error context.
				 * try the best to capture the message */
				hio_seterrbfmt (hio, HIO_ESYSERR, "watcher error detected - %js", prev_errmsg);

				ret = -2;
				break;
			}

			/* you can do other things here */
		}

		hio_epilogue(hio);
	}
	return ret;
}

hio_dev_t* hio_dev_make (hio_t* hio, hio_oow_t dev_size, hio_dev_mth_t* dev_mth, hio_dev_evcb_t* dev_evcb, void* make_ctx)
{
	hio_dev_t* dev = HIO_NULL;

	if (dev_size < HIO_SIZEOF(hio_dev_t))
	{
		hio_seterrnum (hio, HIO_EINVAL);
		if (dev_mth->fail_before_make) dev_mth->fail_before_make (make_ctx);
		goto oops;
	}

	dev = (hio_dev_t*)hio_callocmem(hio, dev_size);
	if (HIO_UNLIKELY(!dev))
	{
		if (dev_mth->fail_before_make) dev_mth->fail_before_make (make_ctx);
		goto oops;
	}

	dev->hio = hio;
	dev->dev_size = dev_size;
	/* default capability. dev->dev_mth->make() can change this.
	 * hio_dev_watch() is affected by the capability change. */
	dev->dev_cap = HIO_DEV_CAP_IN | HIO_DEV_CAP_OUT;
	dev->dev_mth = dev_mth;
	dev->dev_evcb = dev_evcb;
	HIO_INIT_NTIME (&dev->rtmout, 0, 0);
	dev->rtmridx = HIO_TMRIDX_INVALID;
	HIO_WQ_INIT (&dev->wq);
	dev->cw_count = 0;

	/* call the callback function first */
	if (dev->dev_mth->make(dev, make_ctx) <= -1) goto oops;

	/* the make callback must not change these fields */
	HIO_ASSERT (hio, dev->dev_mth == dev_mth);
	HIO_ASSERT (hio, dev->dev_evcb == dev_evcb);
	HIO_ASSERT (hio, dev->dev_prev == HIO_NULL);
	HIO_ASSERT (hio, dev->dev_next == HIO_NULL);

	/* set some internal capability bits according to the capabilities
	 * removed by the device making callback for convenience sake. */
	dev->dev_cap &= HIO_DEV_CAP_ALL_MASK; /* keep valid capability bits only. drop all internal-use bits */
	if (!(dev->dev_cap & HIO_DEV_CAP_IN)) dev->dev_cap |= HIO_DEV_CAP_IN_CLOSED;
	if (!(dev->dev_cap & HIO_DEV_CAP_OUT)) dev->dev_cap |= HIO_DEV_CAP_OUT_CLOSED;

	if (hio_dev_watch(dev, HIO_DEV_WATCH_START, 0) <= -1) goto oops_after_make;

	/* and place the new device object at the back of the active device list */
	HIO_DEVL_APPEND_DEV (&hio->actdev, dev);
	dev->dev_cap |= HIO_DEV_CAP_ACTIVE;
	HIO_DEBUG1 (hio, "MIO - Set ACTIVE on device %p\n", dev);

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
				if (hio->stopreq != HIO_STOPREQ_NONE)
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

	return HIO_NULL;

oops:
	if (dev) hio_freemem (hio, dev);
	return HIO_NULL;
}

static int kill_and_free_device (hio_dev_t* dev, int force)
{
	hio_t* hio = dev->hio;

	HIO_ASSERT (hio, !(dev->dev_cap & HIO_DEV_CAP_ACTIVE));
	HIO_ASSERT (hio, !(dev->dev_cap & HIO_DEV_CAP_HALTED));

	HIO_DEBUG1 (hio, "MIO - Calling kill method on device %p\n", dev);
	if (dev->dev_mth->kill(dev, force) <= -1)
	{
		HIO_DEBUG1 (hio, "MIO - Failure by kill method on device %p\n", dev);

		if (force >= 2) goto free_device;

		if (!(dev->dev_cap & HIO_DEV_CAP_ZOMBIE))
		{
			HIO_DEBUG1 (hio, "MIO - Set ZOMBIE on device %p for kill method failure\n", dev);
			HIO_DEVL_APPEND_DEV (&hio->zmbdev, dev);
			dev->dev_cap |= HIO_DEV_CAP_ZOMBIE;
		}

		return -1;
	}
	HIO_DEBUG1 (hio, "MIO - Success by kill method on device %p\n", dev);

free_device:
	if (dev->dev_cap & HIO_DEV_CAP_ZOMBIE)
	{
		/* detach it from the zombie device list */
		HIO_DEVL_UNLINK_DEV (dev);
		dev->dev_cap &= ~HIO_DEV_CAP_ZOMBIE;
		HIO_DEBUG1 (hio, "MIO - Unset ZOMBIE on device %p\n", dev);
	}

	HIO_DEBUG1 (hio, "MIO - Freeed device %p\n", dev);
	hio_freemem (hio, dev);
	return 0;
}

static void kill_zombie_job_handler (hio_t* hio, const hio_ntime_t* now, hio_tmrjob_t* job)
{
	hio_dev_t* dev = (hio_dev_t*)job->ctx;

	HIO_ASSERT (hio, dev->dev_cap & HIO_DEV_CAP_ZOMBIE);

	if (kill_and_free_device(dev, 0) <= -1)
	{
		if (schedule_kill_zombie_job(dev) <= -1)
		{
			/* i have to choice but to free up the devide by force */
			while (kill_and_free_device(dev, 1) <= -1)
			{
				if (hio->stopreq != HIO_STOPREQ_NONE)
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

static int schedule_kill_zombie_job (hio_dev_t* dev)
{
	hio_t* hio = dev->hio;
	hio_tmrjob_t kill_zombie_job;
	hio_ntime_t tmout;

	HIO_INIT_NTIME (&tmout, 3, 0); /* TODO: take it from configuration */

	HIO_MEMSET (&kill_zombie_job, 0, HIO_SIZEOF(kill_zombie_job));
	kill_zombie_job.ctx = dev;
	hio_gettime (hio, &kill_zombie_job.when);
	HIO_ADD_NTIME (&kill_zombie_job.when, &kill_zombie_job.when, &tmout);
	kill_zombie_job.handler = kill_zombie_job_handler;
	/*kill_zombie_job.idxptr = &rdev->tmridx_kill_zombie;*/

	return hio_instmrjob(hio, &kill_zombie_job) == HIO_TMRIDX_INVALID? -1: 0;
}

void hio_dev_kill (hio_dev_t* dev)
{
	hio_t* hio = dev->hio;

	if (dev->dev_cap & HIO_DEV_CAP_ZOMBIE)
	{
		HIO_ASSERT (hio, HIO_WQ_IS_EMPTY(&dev->wq));
		HIO_ASSERT (hio, dev->cw_count == 0);
		HIO_ASSERT (hio, dev->rtmridx == HIO_TMRIDX_INVALID);
		goto kill_device;
	}

	if (!(dev->dev_cap & (HIO_DEV_CAP_HALTED | HIO_DEV_CAP_ACTIVE)))
	{
		/* neither HALTED nor ACTIVE set on the device.
		 * a call to this function is probably made again from a
		 * disconnect callback executed in kill_and_free_device() below ... */
		HIO_DEBUG1 (hio, "MIO - Duplicate kill on device %p\n", dev);
		return;
	}

	if (dev->rtmridx != HIO_TMRIDX_INVALID)
	{
		hio_deltmrjob (hio, dev->rtmridx);
		dev->rtmridx = HIO_TMRIDX_INVALID;
	}

	/* clear completed write event queues */
	if (dev->cw_count > 0) fire_cwq_handlers_for_dev (hio, dev, 1);

	/* clear pending write requests - won't fire on_write for pending write requests */
	while (!HIO_WQ_IS_EMPTY(&dev->wq))
	{
		hio_wq_t* q;
		q = HIO_WQ_HEAD(&dev->wq);
		unlink_wq (hio, q);
		hio_freemem (hio, q);
	}

	if (dev->dev_cap & HIO_DEV_CAP_HALTED)
	{
		/* this device is in the halted state.
		 * unlink it from the halted device list */
		HIO_DEVL_UNLINK_DEV (dev);
		dev->dev_cap &= ~HIO_DEV_CAP_HALTED;
		HIO_DEBUG1 (hio, "MIO - Unset HALTED on device %p\n", dev);
	}
	else
	{
		HIO_ASSERT (hio, dev->dev_cap & HIO_DEV_CAP_ACTIVE);
		HIO_DEVL_UNLINK_DEV (dev);
		dev->dev_cap &= ~HIO_DEV_CAP_ACTIVE;
		HIO_DEBUG1 (hio, "MIO - Unset ACTIVE on device %p\n", dev);
	}

	hio_dev_watch (dev, HIO_DEV_WATCH_STOP, 0);

kill_device:
	if (kill_and_free_device(dev, 0) <= -1)
	{
		HIO_ASSERT (hio, dev->dev_cap & HIO_DEV_CAP_ZOMBIE);
		if (schedule_kill_zombie_job (dev) <= -1)
		{
			/* i have no choice but to free up the devide by force */
			while (kill_and_free_device(dev, 1) <= -1)
			{
				if (hio->stopreq  != HIO_STOPREQ_NONE)
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

void hio_dev_halt (hio_dev_t* dev)
{
	hio_t* hio = dev->hio;

	if (dev->dev_cap & HIO_DEV_CAP_ACTIVE)
	{
		HIO_DEBUG1 (hio, "MIO - Halting device %p\n", dev);

		/* delink the device object from the active device list */
		HIO_DEVL_UNLINK_DEV (dev);
		dev->dev_cap &= ~HIO_DEV_CAP_ACTIVE;
		HIO_DEBUG1 (hio, "MIO - Unset ACTIVE on device %p\n", dev);

		/* place it at the back of the halted device list */
		HIO_DEVL_APPEND_DEV (&hio->hltdev, dev);
		dev->dev_cap |= HIO_DEV_CAP_HALTED;
		HIO_DEBUG1 (hio, "MIO - Set HALTED on device %p\n", dev);
	}
}

int hio_dev_ioctl (hio_dev_t* dev, int cmd, void* arg)
{
	hio_t* hio = dev->hio;

	if (HIO_UNLIKELY(!dev->dev_mth->ioctl))
	{
		hio_seterrnum (hio, HIO_ENOIMPL);  /* TODO: different error code ? */
		return -1;
	}

	return dev->dev_mth->ioctl(dev, cmd, arg);
}

int hio_dev_watch (hio_dev_t* dev, hio_dev_watch_cmd_t cmd, int events)
{
	hio_t* hio = dev->hio;
	int mux_cmd;
	int dev_cap;

	/* the virtual device doesn't perform actual I/O.
	 * it's different from not hanving HIO_DEV_CAP_IN and HIO_DEV_CAP_OUT.
	 * a non-virtual device without the capabilities still gets attention
	 * of the system multiplexer for hangup and error. */
	if (dev->dev_cap & HIO_DEV_CAP_VIRTUAL)
	{
		/* UGLY HACK - you may start a device with VIRTUAL set upon creation when START is attempted.
		 *             later, if you mask off VIRTUAL, you may perform normal IO and call
		 *             hio_dev_watch() with UPDATE. if SUSPENDED is set, UPDATE works */
		if (cmd == HIO_DEV_WATCH_START) dev->dev_cap |= HIO_DEV_CAP_WATCH_SUSPENDED;
		/* END UGLY HACK */
		return 0;
	}

	/*ev.data.ptr = dev;*/
	dev_cap = dev->dev_cap & ~(DEV_CAP_ALL_WATCHED | HIO_DEV_CAP_WATCH_SUSPENDED | HIO_DEV_CAP_WATCH_REREG_REQUIRED); /* UGLY to use HIO_DEV_CAP_WATCH_SUSPENDED and HIO_DEV_CAP_WATCH_REREG_REQUIRED here */

	switch (cmd)
	{
		case HIO_DEV_WATCH_START:
			/* request input watching when a device is started.
			 * if the device is set with HIO_DEV_CAP_IN_DISABLED and/or
			 * is not set with HIO_DEV_CAP_IN, input wathcing is excluded
			 * after this 'switch' block */
			events = HIO_DEV_EVENT_IN;
			mux_cmd = HIO_SYS_MUX_CMD_INSERT;
			dev_cap |= HIO_DEV_CAP_WATCH_STARTED;
			break;

		case HIO_DEV_WATCH_RENEW:
			/* auto-renwal mode. input watching is taken from the events make passed in.
			 * output watching is requested only if there're enqueued data for writing.
			 * if you want to enable input watching while renewing, call this function like this.
			 *  hio_dev_wtach (dev, HIO_DEV_WATCH_RENEW, HIO_DEV_EVENT_IN);
			 * if you want input watching disabled while renewing, call this function like this.
			 *  hio_dev_wtach (dev, HIO_DEV_WATCH_RENEW, 0); */
			if (HIO_WQ_IS_EMPTY(&dev->wq)) events &= ~HIO_DEV_EVENT_OUT;
			else events |= HIO_DEV_EVENT_OUT;

			/* fall through */
		case HIO_DEV_WATCH_UPDATE:
			/* honor event watching requests as given by the caller */
			mux_cmd = HIO_SYS_MUX_CMD_UPDATE;
			break;

		case HIO_DEV_WATCH_STOP:
			if (!(dev_cap & HIO_DEV_CAP_WATCH_STARTED)) return 0; /* the device is not being watched */
			events = 0; /* override events */
			mux_cmd = HIO_SYS_MUX_CMD_DELETE;
			dev_cap &= ~HIO_DEV_CAP_WATCH_STARTED;
			goto ctrl_mux;

		default:
			hio_seterrnum (dev->hio, HIO_EINVAL);
			return -1;
	}


	/* this function honors HIO_DEV_EVENT_IN and HIO_DEV_EVENT_OUT only
	 * as valid input event bits. it intends to provide simple abstraction
	 * by reducing the variety of event bits that the caller has to handle. */
	if ((events & HIO_DEV_EVENT_IN) && !(dev->dev_cap & (HIO_DEV_CAP_IN_CLOSED | HIO_DEV_CAP_IN_DISABLED)))
	{
		if (dev->dev_cap & HIO_DEV_CAP_IN)
		{
			if (dev->dev_cap & HIO_DEV_CAP_PRI) dev_cap |= HIO_DEV_CAP_PRI_WATCHED;
			dev_cap |= HIO_DEV_CAP_IN_WATCHED;
		}
	}

	if ((events & HIO_DEV_EVENT_OUT) && !(dev->dev_cap & HIO_DEV_CAP_OUT_CLOSED))
	{
		if (dev->dev_cap & HIO_DEV_CAP_OUT) dev_cap |= HIO_DEV_CAP_OUT_WATCHED;
	}

	if (mux_cmd == HIO_SYS_MUX_CMD_UPDATE && (dev_cap & DEV_CAP_ALL_WATCHED) == (dev->dev_cap & DEV_CAP_ALL_WATCHED))
	{
		/* no change in the device capacity. skip calling epoll_ctl */
	}
	else
	{
	ctrl_mux:
		if (hio_sys_ctrlmux(hio, mux_cmd, dev, dev_cap) <= -1) return -1;
	}

	/* UGLY. HIO_DEV_CAP_WATCH_SUSPENDED may be set/unset by hio_sys_ctrlmux. I need this to reflect it */
	dev->dev_cap = dev_cap | (dev->dev_cap & (HIO_DEV_CAP_WATCH_SUSPENDED | HIO_DEV_CAP_WATCH_REREG_REQUIRED));
	return 0;
}

static void on_read_timeout (hio_t* hio, const hio_ntime_t* now, hio_tmrjob_t* job)
{
	hio_dev_t* dev;
	int x;

	dev = (hio_dev_t*)job->ctx;

	hio_seterrnum (hio, HIO_ETMOUT);
	x = dev->dev_evcb->on_read(dev, HIO_NULL, -1, HIO_NULL);

	HIO_ASSERT (hio, dev->rtmridx == HIO_TMRIDX_INVALID);

	if (x <= -1)
	{
		HIO_DEBUG2 (hio, "DEV(%p) - halting a device for on_read error upon timeout - %js\n", dev, hio_geterrmsg(hio));
		hio_dev_halt (dev);
	}
}

static int __dev_read (hio_dev_t* dev, int enabled, const hio_ntime_t* tmout, void* rdctx)
{
	hio_t* hio = dev->hio;

	/* __dev_read() doesn't perform 'read' unlike __dev_write() that actually writes.
	 * it only changes the multiplexer state. it may be ok to allow this state change
	 * request even if HIO_DEV_CAP_IN_CLOSED is set. therefore, the condtion like this
	 * is not implemented in this function.
	if (dev->dev_cap & HIO_DEV_CAP_IN_CLOSED)
	{
		hio_seterrbfmt (hio, HIO_ENOCAPA, "unable to read closed device");
		return -1;
	}
	*/

	if (enabled)
	{
		dev->dev_cap &= ~HIO_DEV_CAP_IN_DISABLED;
		if (!(dev->dev_cap & HIO_DEV_CAP_IN_WATCHED)) goto renew_watch_now;
	}
	else
	{
		dev->dev_cap |= HIO_DEV_CAP_IN_DISABLED;
		if ((dev->dev_cap & HIO_DEV_CAP_IN_WATCHED)) goto renew_watch_now;
	}

	dev->dev_cap |= HIO_DEV_CAP_RENEW_REQUIRED;
	goto update_timer;

renew_watch_now:
	if (hio_dev_watch(dev, HIO_DEV_WATCH_RENEW, HIO_DEV_EVENT_IN) <= -1) return -1;
	goto update_timer;

update_timer:
	if (dev->rtmridx != HIO_TMRIDX_INVALID)
	{
		/* read timeout already on the socket. remove it first */
		hio_deltmrjob (hio, dev->rtmridx);
		dev->rtmridx = HIO_TMRIDX_INVALID;
	}

	if (tmout && HIO_IS_POS_NTIME(tmout))
	{
		hio_tmrjob_t tmrjob;

		HIO_MEMSET (&tmrjob, 0, HIO_SIZEOF(tmrjob));
		tmrjob.ctx = dev;
		hio_gettime (hio, &tmrjob.when);
		HIO_ADD_NTIME (&tmrjob.when, &tmrjob.when, tmout);
		tmrjob.handler = on_read_timeout;
		tmrjob.idxptr = &dev->rtmridx;

		dev->rtmridx = hio_instmrjob(hio, &tmrjob);
		if (dev->rtmridx == HIO_TMRIDX_INVALID)
		{
			/* if timer registration fails, timeout will never be triggered */
			return -1;
		}
		dev->rtmout = *tmout;
	}
	return 0;
}

int hio_dev_read (hio_dev_t* dev, int enabled)
{
	return __dev_read(dev, enabled, HIO_NULL, HIO_NULL);
}

int hio_dev_timedread (hio_dev_t* dev, int enabled, const hio_ntime_t* tmout)
{
	return __dev_read(dev, enabled, tmout, HIO_NULL);
}

static void on_write_timeout (hio_t* hio, const hio_ntime_t* now, hio_tmrjob_t* job)
{
	hio_wq_t* q;
	hio_dev_t* dev;
	int x;

	q = (hio_wq_t*)job->ctx;
	dev = q->dev;

	hio_seterrnum (hio, HIO_ETMOUT);
	x = dev->dev_evcb->on_write(dev, -1, q->ctx, &q->dstaddr);

	HIO_ASSERT (hio, q->tmridx == HIO_TMRIDX_INVALID);
	HIO_WQ_UNLINK(q);
	hio_freemem (hio, q);

	if (x <= -1)
	{
		HIO_DEBUG2 (hio, "DEV(%p) - halting a device for on_write error upon timeout - %js\n", dev, hio_geterrmsg(hio));
		hio_dev_halt (dev);
	}
}

static HIO_INLINE int __enqueue_completed_write (hio_dev_t* dev, hio_iolen_t len, void* wrctx, const hio_devaddr_t* dstaddr)
{
	hio_t* hio = dev->hio;
	hio_cwq_t* cwq;
	hio_oow_t cwq_extra_aligned, cwqfl_index;

	cwq_extra_aligned = (dstaddr? dstaddr->len: 0);
	cwq_extra_aligned = HIO_ALIGN_POW2(cwq_extra_aligned, HIO_CWQFL_ALIGN);
	cwqfl_index = cwq_extra_aligned / HIO_CWQFL_SIZE;

	if (cwqfl_index < HIO_COUNTOF(hio->cwqfl) && hio->cwqfl[cwqfl_index])
	{
		/* take an available cwq object from the free cwq list */
		cwq = dev->hio->cwqfl[cwqfl_index];
		dev->hio->cwqfl[cwqfl_index] = cwq->q_next;
	}
	else
	{
		cwq = (hio_cwq_t*)hio_allocmem(hio, HIO_SIZEOF(*cwq) + cwq_extra_aligned);
		if (HIO_UNLIKELY(!cwq)) return -1;
	}

	HIO_MEMSET (cwq, 0, HIO_SIZEOF(*cwq));
	cwq->dev = dev;
	cwq->ctx = wrctx;
	if (dstaddr)
	{
		cwq->dstaddr.ptr = (hio_uint8_t*)(cwq + 1);
		cwq->dstaddr.len = dstaddr->len;
		HIO_MEMCPY (cwq->dstaddr.ptr, dstaddr->ptr, dstaddr->len);
	}
	else
	{
		cwq->dstaddr.len = 0;
	}

	cwq->olen = len;

	HIO_CWQ_ENQ (&dev->hio->cwq, cwq);
	dev->cw_count++; /* increment the number of complete write operations */
	return 0;
}

static HIO_INLINE int __enqueue_pending_write (hio_dev_t* dev, hio_iolen_t olen, hio_iolen_t urem, hio_iovec_t* iov, hio_iolen_t iov_cnt, hio_iolen_t iov_index, const hio_ntime_t* tmout, void* wrctx, const hio_devaddr_t* dstaddr)
{
	hio_t* hio = dev->hio;
	hio_wq_t* q;
	hio_iolen_t i, j;

	if (dev->dev_cap & HIO_DEV_CAP_OUT_UNQUEUEABLE)
	{
		/* writing queuing is not requested. so return failure */
		hio_seterrbfmt (hio, HIO_ENOCAPA, "device incapable of queuing");
		return -1;
	}

	/* queue the remaining data*/
	q = (hio_wq_t*)hio_allocmem(hio, HIO_SIZEOF(*q) + (dstaddr? dstaddr->len: 0) + urem);
	if (HIO_UNLIKELY(!q)) return -1;

	q->sendfile = 0;
	q->tmridx = HIO_TMRIDX_INVALID;
	q->dev = dev;
	q->ctx = wrctx;

	if (dstaddr)
	{
		q->dstaddr.ptr = (hio_uint8_t*)(q + 1);
		q->dstaddr.len = dstaddr->len;
		HIO_MEMCPY (q->dstaddr.ptr, dstaddr->ptr, dstaddr->len);
	}
	else
	{
		q->dstaddr.len = 0;
	}

	q->ptr = (hio_uint8_t*)(q + 1) + q->dstaddr.len;
	q->len = urem;
	q->olen = olen; /* original length to use when invoking on_write() */
	for (i = iov_index, j = 0; i < iov_cnt; i++)
	{
		HIO_MEMCPY (&q->ptr[j], iov[i].iov_ptr, iov[i].iov_len);
		j += iov[i].iov_len;
	}

	if (tmout && HIO_IS_POS_NTIME(tmout))
	{
		hio_tmrjob_t tmrjob;

		HIO_MEMSET (&tmrjob, 0, HIO_SIZEOF(tmrjob));
		tmrjob.ctx = q;
		hio_gettime (hio, &tmrjob.when);
		HIO_ADD_NTIME (&tmrjob.when, &tmrjob.when, tmout);
		tmrjob.handler = on_write_timeout;
		tmrjob.idxptr = &q->tmridx;

		q->tmridx = hio_instmrjob(hio, &tmrjob);
		if (q->tmridx == HIO_TMRIDX_INVALID)
		{
			hio_freemem (hio, q);
			return -1;
		}
	}

	HIO_WQ_ENQ (&dev->wq, q);
	if (!(dev->dev_cap & HIO_DEV_CAP_OUT_WATCHED))
	{
		/* if output is not being watched, arrange to do so */
		if (hio_dev_watch(dev, HIO_DEV_WATCH_RENEW, HIO_DEV_EVENT_IN) <= -1)
		{
			unlink_wq (hio, q);
			hio_freemem (hio, q);
			return -1;
		}
	}

	return 0; /* request pused to a write queue. */
}

static HIO_INLINE int __enqueue_pending_sendfile (hio_dev_t* dev, hio_iolen_t olen, hio_iolen_t urem, hio_foff_t foff, hio_syshnd_t in_fd, const hio_ntime_t* tmout, void* wrctx, const hio_devaddr_t* dstaddr)
{
	hio_t* hio = dev->hio;
	hio_wq_t* q;

	if (dev->dev_cap & HIO_DEV_CAP_OUT_UNQUEUEABLE)
	{
		/* writing queuing is not requested. so return failure */
		hio_seterrbfmt (hio, HIO_ENOCAPA, "device incapable of queuing");
		return -1;
	}

	/* queue the remaining data*/
	q = (hio_wq_t*)hio_allocmem(hio, HIO_SIZEOF(*q) + (dstaddr? dstaddr->len: 0) + HIO_SIZEOF(wq_sendfile_data_t));
	if (HIO_UNLIKELY(!q)) return -1;

	q->sendfile = 1;
	q->tmridx = HIO_TMRIDX_INVALID;
	q->dev = dev;
	q->ctx = wrctx;

	if (dstaddr)
	{
		q->dstaddr.ptr = (hio_uint8_t*)(q + 1);
		q->dstaddr.len = dstaddr->len;
		HIO_MEMCPY (q->dstaddr.ptr, dstaddr->ptr, dstaddr->len);
	}
	else
	{
		q->dstaddr.len = 0;
	}

	q->ptr = (hio_uint8_t*)(q + 1) + q->dstaddr.len;
	q->len = urem;
	q->olen = olen; /* original length to use when invoking on_write() */

	((wq_sendfile_data_t*)q->ptr)->in_fd = in_fd;
	((wq_sendfile_data_t*)q->ptr)->foff = foff;

	if (tmout && HIO_IS_POS_NTIME(tmout))
	{
		hio_tmrjob_t tmrjob;

		HIO_MEMSET (&tmrjob, 0, HIO_SIZEOF(tmrjob));
		tmrjob.ctx = q;
		hio_gettime (hio, &tmrjob.when);
		HIO_ADD_NTIME (&tmrjob.when, &tmrjob.when, tmout);
		tmrjob.handler = on_write_timeout;
		tmrjob.idxptr = &q->tmridx;

		q->tmridx = hio_instmrjob(hio, &tmrjob);
		if (q->tmridx == HIO_TMRIDX_INVALID)
		{
			hio_freemem (hio, q);
			return -1;
		}
	}

	HIO_WQ_ENQ (&dev->wq, q);
	if (!(dev->dev_cap & HIO_DEV_CAP_OUT_WATCHED))
	{
		/* if output is not being watched, arrange to do so */
		if (hio_dev_watch(dev, HIO_DEV_WATCH_RENEW, HIO_DEV_EVENT_IN) <= -1)
		{
			unlink_wq (hio, q);
			hio_freemem (hio, q);
			return -1;
		}
	}

	return 0; /* request pused to a write queue. */
}


static HIO_INLINE int __dev_write (hio_dev_t* dev, const void* data, hio_iolen_t len, const hio_ntime_t* tmout, void* wrctx, const hio_devaddr_t* dstaddr)
{
	hio_t* hio = dev->hio;
	const hio_uint8_t* uptr;
	hio_iolen_t urem, ulen;
	hio_iovec_t iov;
	int x;

	if (dev->dev_cap & HIO_DEV_CAP_OUT_CLOSED)
	{
		hio_seterrbfmt (hio, HIO_ENOCAPA, "unable to write to closed device");
		return -1;
	}

	uptr = data;
	urem = len;

	if (!HIO_WQ_IS_EMPTY(&dev->wq))
	{
		/* the writing queue is not empty.
		 * enqueue this request immediately */
		goto enqueue_data;
	}

	if (dev->dev_cap & HIO_DEV_CAP_STREAM)
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
			dev->dev_cap |= HIO_DEV_CAP_OUT_CLOSED;
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

	return 1; /* written immediately and called on_write callback. but this line will never be reached */

enqueue_data:
	iov.iov_ptr = (void*)uptr;
	iov.iov_len = urem;
	return __enqueue_pending_write(dev, len, urem, &iov, 1, 0, tmout, wrctx, dstaddr);

enqueue_completed_write:
	return __enqueue_completed_write(dev, len, wrctx, dstaddr);
}

static HIO_INLINE int __dev_writev (hio_dev_t* dev, hio_iovec_t* iov, hio_iolen_t iovcnt, const hio_ntime_t* tmout, void* wrctx, const hio_devaddr_t* dstaddr)
{
	hio_t* hio = dev->hio;
	hio_iolen_t urem, len;
	hio_iolen_t index = 0, i;
	int x;

	if (dev->dev_cap & HIO_DEV_CAP_OUT_CLOSED)
	{
		hio_seterrbfmt (hio, HIO_ENOCAPA, "unable to write to closed device");
		return -1;
	}

	len = 0;
	for (i = 0; i < iovcnt; i++) len += iov[i].iov_len;
	urem = len;

	if (!HIO_WQ_IS_EMPTY(&dev->wq))
	{
		/* if the writing queue is not empty, enqueue this request immediately */
		goto enqueue_data;
	}

	if (dev->dev_cap & HIO_DEV_CAP_STREAM)
	{
		/* use the do..while() loop to be able to send a zero-length data */
		hio_iolen_t backup_index = -1, dcnt;
		hio_iovec_t backup;

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
			while (index < iovcnt && (hio_oow_t)dcnt >= iov[index].iov_len)
				dcnt -= iov[index++].iov_len;

			if (index == iovcnt) break;

			if (backup_index != index)
			{
				if (backup_index >= 0) iov[backup_index] = backup;
				backup = iov[index];
				backup_index = index;
			}

			iov[index].iov_ptr = (void*)((hio_uint8_t*)iov[index].iov_ptr + dcnt);
			iov[index].iov_len -= dcnt;
		}
		while (1);

		if (backup_index >= 0) iov[backup_index] = backup;

		if (iovcnt <= 0) /* original vector count */
		{
			/* a zero-length writing request is to close the writing end. this causes further write request to fail */
			dev->dev_cap |= HIO_DEV_CAP_OUT_CLOSED;
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
		hio_iolen_t dcnt;

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

	return 1; /* written immediately and called on_write callback. but this line will never be reached */

enqueue_data:
	return __enqueue_pending_write(dev, len, urem, iov, iovcnt, index, tmout, wrctx, dstaddr);

enqueue_completed_write:
	return __enqueue_completed_write(dev, len, wrctx, dstaddr);
}

static int __dev_sendfile (hio_dev_t* dev, hio_syshnd_t in_fd, hio_foff_t foff, hio_iolen_t len, const hio_ntime_t* tmout, void* wrctx)
{
	hio_t* hio = dev->hio;
	hio_foff_t uoff;
	hio_iolen_t urem, ulen;
	int x;

	if (HIO_UNLIKELY(dev->dev_cap & HIO_DEV_CAP_OUT_CLOSED))
	{
		hio_seterrbfmt (hio, HIO_ENOCAPA, "unable to sendfile to closed device");
		return -1;
	}

	if (HIO_UNLIKELY(!dev->dev_mth->sendfile))
	{
		hio_seterrbfmt (hio, HIO_ENOCAPA, "unable to senfile over unsupported device");
		return -1;
	}

	uoff = foff;
	urem = len;

	if (!HIO_WQ_IS_EMPTY(&dev->wq))
	{
		/* the writing queue is not empty.
		 * enqueue this request immediately */
		goto enqueue_data;
	}

	if (dev->dev_cap & HIO_DEV_CAP_STREAM)
	{
		/* use the do..while() loop to be able to send a zero-length data */
		do
		{
			ulen = urem;
			x = dev->dev_mth->sendfile(dev, in_fd, uoff, &ulen);
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
				uoff += ulen;
			}
		}
		while (urem > 0);

		if (len <= 0) /* original length */
		{
			/* a zero-length writing request is to close the writing end. this causes further write request to fail */
			dev->dev_cap |= HIO_DEV_CAP_OUT_CLOSED;
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
		hio_seterrbfmt (hio, HIO_ENOCAPA, "unable to sendfile over a non-stream device");
		return -1;
	}

	return 1; /* written immediately and called on_write callback. but this line will never be reached */

enqueue_data:
	return __enqueue_pending_sendfile(dev, len, urem, uoff, in_fd, tmout, wrctx, HIO_NULL);

enqueue_completed_write:
	return __enqueue_completed_write(dev, len, wrctx, HIO_NULL);
}

int hio_dev_write (hio_dev_t* dev, const void* data, hio_iolen_t len, void* wrctx, const hio_devaddr_t* dstaddr)
{
	return __dev_write(dev, data, len, HIO_NULL, wrctx, dstaddr);
}

int hio_dev_writev (hio_dev_t* dev, hio_iovec_t* iov, hio_iolen_t iovcnt, void* wrctx, const hio_devaddr_t* dstaddr)
{
	return __dev_writev(dev, iov, iovcnt, HIO_NULL, wrctx, dstaddr);
}

int hio_dev_sendfile (hio_dev_t* dev, hio_syshnd_t in_fd, hio_foff_t foff, hio_iolen_t len, void* wrctx)
{
	return __dev_sendfile(dev,in_fd, foff, len, HIO_NULL, wrctx);
}

int hio_dev_timedwrite (hio_dev_t* dev, const void* data, hio_iolen_t len, const hio_ntime_t* tmout, void* wrctx, const hio_devaddr_t* dstaddr)
{
	return __dev_write(dev, data, len, tmout, wrctx, dstaddr);
}

int hio_dev_timedwritev (hio_dev_t* dev, hio_iovec_t* iov, hio_iolen_t iovcnt, const hio_ntime_t* tmout, void* wrctx, const hio_devaddr_t* dstaddr)
{
	return __dev_writev(dev, iov, iovcnt, tmout, wrctx, dstaddr);
}

int hio_dev_timedsendfile (hio_dev_t* dev, hio_syshnd_t in_fd, hio_foff_t foff, hio_iolen_t len, const hio_ntime_t* tmout, void* wrctx)
{
	return __dev_sendfile(dev,in_fd, foff, len, tmout, wrctx);
}

/* -------------------------------------------------------------------------- */

void hio_gettime (hio_t* hio, hio_ntime_t* now)
{
	hio_sys_gettime (hio, now);
	/* in hio_init(), hio->init_time has been set to the initialization time.
	 * the time returned here gets offset by hio->init_time and
	 * thus becomes relative to it. this way, it is kept small such that it
	 * can be represented in a small integer with leaving almost zero chance
	 * of overflow. */
	HIO_SUB_NTIME (now, now, &hio->init_time);  /* now = now - init_time */
}

/* -------------------------------------------------------------------------- */
void* hio_allocmem (hio_t* hio, hio_oow_t size)
{
	void* ptr;
	ptr = HIO_MMGR_ALLOC(hio->_mmgr, size);
	if (!ptr) hio_seterrnum (hio, HIO_ESYSMEM);
	return ptr;
}

void* hio_callocmem (hio_t* hio, hio_oow_t size)
{
	void* ptr;
	ptr = HIO_MMGR_ALLOC(hio->_mmgr, size);
	if (!ptr) hio_seterrnum (hio, HIO_ESYSMEM);
	else HIO_MEMSET (ptr, 0, size);
	return ptr;
}

void* hio_reallocmem (hio_t* hio, void* ptr, hio_oow_t size)
{
	ptr = HIO_MMGR_REALLOC(hio->_mmgr, ptr, size);
	if (!ptr) hio_seterrnum (hio, HIO_ESYSMEM);
	return ptr;
}

void hio_freemem (hio_t* hio, void* ptr)
{
	HIO_MMGR_FREE (hio->_mmgr, ptr);
}
/* ------------------------------------------------------------------------ */

void hio_addcfmb (hio_t* hio, hio_cfmb_t* cfmb, hio_cfmb_checker_t checker, hio_cfmb_freeer_t freeer)
{
	cfmb->cfmb_checker = checker;
	cfmb->cfmb_freeer = freeer? freeer: hio_freemem;
	HIO_CFMBL_APPEND_CFMB (&hio->cfmb, cfmb);
}

/* ------------------------------------------------------------------------ */

struct fmt_uch_buf_t
{
	hio_t* hio;
	hio_uch_t* ptr;
	hio_oow_t len;
	hio_oow_t capa;
};
typedef struct fmt_uch_buf_t fmt_uch_buf_t;

static int fmt_put_bchars_to_uch_buf (hio_fmtout_t* fmtout, const hio_bch_t* ptr, hio_oow_t len)
{
	fmt_uch_buf_t* b = (fmt_uch_buf_t*)fmtout->ctx;
	hio_oow_t bcslen, ucslen;
	int n;

	bcslen = len;
	ucslen = b->capa - b->len;
	n = hio_conv_bchars_to_uchars_with_cmgr(ptr, &bcslen, &b->ptr[b->len], &ucslen, (b->hio? b->hio->_cmgr: hio_get_utf8_cmgr()), 1);
	b->len += ucslen;
	if (n <= -1)
	{
		if (n == -2)
		{
			return 0; /* buffer full. stop */
		}
		else
		{
			hio_seterrnum (b->hio, HIO_EECERR);
			return -1;
		}
	}

	return 1; /* success. carry on */
}

static int fmt_put_uchars_to_uch_buf (hio_fmtout_t* fmtout, const hio_uch_t* ptr, hio_oow_t len)
{
	fmt_uch_buf_t* b = (fmt_uch_buf_t*)fmtout->ctx;
	hio_oow_t n;

	/* this function null-terminates the destination. so give the restored buffer size */
	n = hio_copy_uchars_to_ucstr(&b->ptr[b->len], b->capa - b->len + 1, ptr, len);
	b->len += n;
	if (n < len)
	{
		if (b->hio) hio_seterrnum (b->hio, HIO_EBUFFULL);
		return 0; /* stop. insufficient buffer */
	}

	return 1; /* success */
}

hio_oow_t hio_vfmttoucstr (hio_t* hio, hio_uch_t* buf, hio_oow_t bufsz, const hio_uch_t* fmt, va_list ap)
{
	hio_fmtout_t fo;
	fmt_uch_buf_t fb;

	if (bufsz <= 0) return 0;

	HIO_MEMSET (&fo, 0, HIO_SIZEOF(fo));
	fo.putbchars = fmt_put_bchars_to_uch_buf;
	fo.putuchars = fmt_put_uchars_to_uch_buf;
	fo.ctx = &fb;

	HIO_MEMSET (&fb, 0, HIO_SIZEOF(fb));
	fb.hio = hio;
	fb.ptr = buf;
	fb.capa = bufsz - 1;

	if (hio_ufmt_outv(&fo, fmt, ap) <= -1) return -1;

	buf[fb.len] = '\0';
	return fb.len;
}

hio_oow_t hio_fmttoucstr (hio_t* hio, hio_uch_t* buf, hio_oow_t bufsz, const hio_uch_t* fmt, ...)
{
	hio_oow_t x;
	va_list ap;

	va_start (ap, fmt);
	x = hio_vfmttoucstr(hio, buf, bufsz, fmt, ap);
	va_end (ap);

	return x;
}

/* ------------------------------------------------------------------------ */

struct fmt_bch_buf_t
{
	hio_t* hio;
	hio_bch_t* ptr;
	hio_oow_t len;
	hio_oow_t capa;
};
typedef struct fmt_bch_buf_t fmt_bch_buf_t;


static int fmt_put_bchars_to_bch_buf (hio_fmtout_t* fmtout, const hio_bch_t* ptr, hio_oow_t len)
{
	fmt_bch_buf_t* b = (fmt_bch_buf_t*)fmtout->ctx;
	hio_oow_t n;

	/* this function null-terminates the destination. so give the restored buffer size */
	n = hio_copy_bchars_to_bcstr(&b->ptr[b->len], b->capa - b->len + 1, ptr, len);
	b->len += n;
	if (n < len)
	{
		if (b->hio) hio_seterrnum (b->hio, HIO_EBUFFULL);
		return 0; /* stop. insufficient buffer */
	}

	return 1; /* success */
}

static int fmt_put_uchars_to_bch_buf (hio_fmtout_t* fmtout, const hio_uch_t* ptr, hio_oow_t len)
{
	fmt_bch_buf_t* b = (fmt_bch_buf_t*)fmtout->ctx;
	hio_oow_t bcslen, ucslen;
	int n;

	bcslen = b->capa - b->len;
	ucslen = len;
	n = hio_conv_uchars_to_bchars_with_cmgr(ptr, &ucslen, &b->ptr[b->len], &bcslen, (b->hio? b->hio->_cmgr: hio_get_utf8_cmgr()));
	b->len += bcslen;
	if (n <= -1)
	{
		if (n == -2)
		{
			return 0; /* buffer full. stop */
		}
		else
		{
			hio_seterrnum (b->hio, HIO_EECERR);
			return -1;
		}
	}

	return 1; /* success. carry on */
}

hio_oow_t hio_vfmttobcstr (hio_t* hio, hio_bch_t* buf, hio_oow_t bufsz, const hio_bch_t* fmt, va_list ap)
{
	hio_fmtout_t fo;
	fmt_bch_buf_t fb;

	if (bufsz <= 0) return 0;

	HIO_MEMSET (&fo, 0, HIO_SIZEOF(fo));
	fo.putbchars = fmt_put_bchars_to_bch_buf;
	fo.putuchars = fmt_put_uchars_to_bch_buf;
	fo.ctx = &fb;

	HIO_MEMSET (&fb, 0, HIO_SIZEOF(fb));
	fb.hio = hio;
	fb.ptr = buf;
	fb.capa = bufsz - 1;
	if (hio_bfmt_outv(&fo, fmt, ap) <= -1) return -1;

	buf[fb.len] = '\0';
	return fb.len;
}

hio_oow_t hio_fmttobcstr (hio_t* hio, hio_bch_t* buf, hio_oow_t bufsz, const hio_bch_t* fmt, ...)
{
	hio_oow_t x;
	va_list ap;

	va_start (ap, fmt);
	x = hio_vfmttobcstr(hio, buf, bufsz, fmt, ap);
	va_end (ap);

	return x;
}

/* ------------------------------------------------------------------------ */

hio_oow_t hio_dev_cap_to_bcstr (hio_bitmask_t cap, hio_bch_t* buf, hio_oow_t size)
{
	hio_oow_t len = 0;

	if (size <= 0) return 0;
	buf[len] = '\0';

	if (cap & HIO_DEV_CAP_VIRTUAL) len += hio_copy_bcstr(&buf[len], size - len, "virtual|");
	if (cap & HIO_DEV_CAP_IN) len += hio_copy_bcstr(&buf[len], size - len, "in|");
	if (cap & HIO_DEV_CAP_OUT) len += hio_copy_bcstr(&buf[len], size - len, "out|");
	if (cap & HIO_DEV_CAP_PRI) len += hio_copy_bcstr(&buf[len], size - len, "pri|");
	if (cap & HIO_DEV_CAP_STREAM) len += hio_copy_bcstr(&buf[len], size - len, "stream|");
	if (cap & HIO_DEV_CAP_IN_DISABLED) len += hio_copy_bcstr(&buf[len], size - len, "in_disabled|");
	if (cap & HIO_DEV_CAP_OUT_UNQUEUEABLE) len += hio_copy_bcstr(&buf[len], size - len, "out_unqueueable|");

	if (cap & HIO_DEV_CAP_IN_CLOSED) len += hio_copy_bcstr(&buf[len], size - len, "in_closed|");
	if (cap & HIO_DEV_CAP_OUT_CLOSED) len += hio_copy_bcstr(&buf[len], size - len, "out_closed|");
	if (cap & HIO_DEV_CAP_IN_WATCHED) len += hio_copy_bcstr(&buf[len], size - len, "in_watched|");
	if (cap & HIO_DEV_CAP_OUT_WATCHED) len += hio_copy_bcstr(&buf[len], size - len, "out_watched|");
	if (cap & HIO_DEV_CAP_PRI_WATCHED) len += hio_copy_bcstr(&buf[len], size - len, "pri_watched|");
	if (cap & HIO_DEV_CAP_ACTIVE) len += hio_copy_bcstr(&buf[len], size - len, "active|");
	if (cap & HIO_DEV_CAP_HALTED) len += hio_copy_bcstr(&buf[len], size - len, "halted|");
	if (cap & HIO_DEV_CAP_ZOMBIE) len += hio_copy_bcstr(&buf[len], size - len, "zombie|");
	if (cap & HIO_DEV_CAP_RENEW_REQUIRED) len += hio_copy_bcstr(&buf[len], size - len, "renew_required|");
	if (cap & HIO_DEV_CAP_WATCH_STARTED) len += hio_copy_bcstr(&buf[len], size - len, "watch_started|");
	if (cap & HIO_DEV_CAP_WATCH_SUSPENDED) len += hio_copy_bcstr(&buf[len], size - len, "watch_suspended|");
	if (cap & HIO_DEV_CAP_WATCH_REREG_REQUIRED) len += hio_copy_bcstr(&buf[len], size - len, "watch_rereg_required|");

	if (buf[len - 1] == '|') buf[--len] = '\0';
	return len;
}


