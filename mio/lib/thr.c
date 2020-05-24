/*
 * $Id$
 *
    Copyright (c) 2016-2020 Chung, Hyung-Hwan. All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted thrvided that the following conditions
    are met:
    1. Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.
    2. Redistributions in binary form must rethrduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials thrvided with the distribution.

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

#include "mio-thr.h"
#include "mio-prv.h"

#include <unistd.h>
#include <errno.h>
#include <sys/uio.h>
#include <pthread.h>

/* ========================================================================= */

struct mio_dev_thr_info_t
{
	MIO_CFMB_HEADER;

	mio_dev_thr_func_t thr_func;
	mio_dev_thr_iopair_t thr_iop;
	void* thr_ctx;
	pthread_t thr_hnd;
	int thr_done;
};

struct slave_info_t
{
	mio_dev_thr_make_t* mi;
	mio_syshnd_t pfd;
	int dev_cap;
	mio_dev_thr_sid_t id;
};

typedef struct slave_info_t slave_info_t;

static mio_dev_thr_slave_t* make_slave (mio_t* mio, slave_info_t* si);

/* ========================================================================= */


static void free_thr_info_resources (mio_t* mio, mio_dev_thr_info_t* ti)
{
	if (ti->thr_iop.rfd != MIO_SYSHND_INVALID) 
	{
		close (ti->thr_iop.rfd);
		ti->thr_iop.rfd = MIO_SYSHND_INVALID;
	}
	if (ti->thr_iop.wfd != MIO_SYSHND_INVALID) 
	{
		close (ti->thr_iop.wfd);
		ti->thr_iop.wfd = MIO_SYSHND_INVALID;
	}
}

static int ready_to_free_thr_info (mio_t* mio, mio_cfmb_t* cfmb)
{
	mio_dev_thr_info_t* ti = (mio_dev_thr_info_t*)cfmb;

#if 1
	if (MIO_UNLIKELY(mio->_fini_in_progress))
	{
		pthread_join (ti->thr_hnd, MIO_NULL); /* BAD. blocking call in a non-blocking library. not useful to call pthread_tryjoin_np() here. */
		free_thr_info_resources (mio, ti);
		return 1; /* free me */
	}
#endif

	if (ti->thr_done)
	{
		free_thr_info_resources (mio, ti);
#if defined(HAVE_PTHREAD_TRYJOIN_NP)
		if (pthread_tryjoin_np(ti->thr_hnd) != 0) /* not terminated yet - however, this isn't necessary. z*/
#endif
			pthread_detach (ti->thr_hnd); /* just detach it */
		return 1; /* free me */
	}

	return 0; /* not freeed */
}

static void mark_thr_done (void* ctx)
{
	mio_dev_thr_info_t* ti = (mio_dev_thr_info_t*)ctx;
	ti->thr_done = 1;
}

static void* run_thr_func (void* ctx)
{
	mio_dev_thr_info_t* ti = (mio_dev_thr_info_t*)ctx;

	/* i assume the thread is cancellable, and of the deferred cancellation type by default */
	/*int dummy;
	pthread_setcancelstate (PTHREAD_CANCEL_ENABLE, &dummy);
	pthread_setcanceltype (PTHREAD_CANCEL_DEFERRED, &dummy);*/

	pthread_cleanup_push (mark_thr_done, ti);

	ti->thr_func (ti->mio, &ti->thr_iop, ti->thr_ctx);

	/* This part may get partially executed or not executed if the thread is cancelled */
	free_thr_info_resources (ti->mio, ti); /* TODO: check if the close() call inside this call completes when it becomes a cancellation point. if so, the code must get changed */
	/* ---------------------------------------------------------- */

	pthread_cleanup_pop (1);
	pthread_exit (MIO_NULL);
	return MIO_NULL;
}

static int dev_thr_make_master (mio_dev_t* dev, void* ctx)
{
	mio_t* mio = dev->mio;
	mio_dev_thr_t* rdev = (mio_dev_thr_t*)dev;
	mio_dev_thr_make_t* info = (mio_dev_thr_make_t*)ctx;
	mio_syshnd_t pfds[4] = { MIO_SYSHND_INVALID, MIO_SYSHND_INVALID, MIO_SYSHND_INVALID, MIO_SYSHND_INVALID };
	slave_info_t si;
	int i;

	if (pipe(&pfds[0]) == -1 || pipe(&pfds[2]) == -1)
	{
		mio_seterrwithsyserr (mio, 0, errno);
		goto oops;
	}

	if (mio_makesyshndasync(mio, pfds[1]) <= -1 ||
	    mio_makesyshndasync(mio, pfds[2]) <= -1) goto oops;

	si.mi = info;
	si.pfd = pfds[1];
	si.dev_cap = MIO_DEV_CAP_OUT | MIO_DEV_CAP_STREAM;
	si.id = MIO_DEV_THR_IN;

	rdev->slave[MIO_DEV_THR_IN] = make_slave(mio, &si);
	if (!rdev->slave[MIO_DEV_THR_IN]) goto oops;

	pfds[1] = MIO_SYSHND_INVALID;
	rdev->slave_count++;

	si.mi = info;
	si.pfd = pfds[2];
	si.dev_cap = MIO_DEV_CAP_IN | MIO_DEV_CAP_STREAM;
	si.id = MIO_DEV_THR_OUT;

	rdev->slave[MIO_DEV_THR_OUT] = make_slave(mio, &si);
	if (!rdev->slave[MIO_DEV_THR_OUT]) goto oops;

	pfds[2] = MIO_SYSHND_INVALID;
	rdev->slave_count++;

	for (i = 0; i < MIO_COUNTOF(rdev->slave); i++) 
	{
		if (rdev->slave[i]) rdev->slave[i]->master = rdev;
	}

	rdev->dev_cap = MIO_DEV_CAP_VIRTUAL; /* the master device doesn't perform I/O */
	rdev->on_read = info->on_read;
	rdev->on_write = info->on_write;
	rdev->on_close = info->on_close;
	
	/* ---------------------------------------------------------- */
	{
		int n;
		mio_dev_thr_info_t* ti;

		ti = mio_callocmem(mio, MIO_SIZEOF(*ti));
		if (MIO_UNLIKELY(!ti)) goto oops;

		ti->mio = mio;
		ti->thr_iop.rfd = pfds[0];
		ti->thr_iop.wfd = pfds[3];
		ti->thr_func = info->thr_func;
		ti->thr_ctx = info->thr_ctx;

		rdev->thr_info = ti;
		n = pthread_create(&ti->thr_hnd, MIO_NULL, run_thr_func, ti);
		if (n != 0) 
		{
			rdev->thr_info = MIO_NULL;
			mio_freemem (mio, ti);
			goto oops;
		}
	}
	/* ---------------------------------------------------------- */


	return 0;

oops:
	for (i = 0; i < MIO_COUNTOF(pfds); i++)
	{
		if (pfds[i] != MIO_SYSHND_INVALID) close (pfds[i]);
	}

	for (i = MIO_COUNTOF(rdev->slave); i > 0; )
	{
		i--;
		if (rdev->slave[i])
		{
			mio_dev_kill ((mio_dev_t*)rdev->slave[i]);
			rdev->slave[i] = MIO_NULL;
		}
	}
	rdev->slave_count = 0;

	return -1;
}

static int dev_thr_make_slave (mio_dev_t* dev, void* ctx)
{
	mio_dev_thr_slave_t* rdev = (mio_dev_thr_slave_t*)dev;
	slave_info_t* si = (slave_info_t*)ctx;

	rdev->dev_cap = si->dev_cap;
	rdev->id = si->id;
	rdev->pfd = si->pfd;
	/* keep rdev->master to MIO_NULL. it's set to the right master
	 * device in dev_thr_make() */

	return 0;
}

static int dev_thr_kill_master (mio_dev_t* dev, int force)
{
	mio_t* mio = dev->mio;
	mio_dev_thr_t* rdev = (mio_dev_thr_t*)dev;
	mio_dev_thr_info_t* ti;
	int i;

	ti = rdev->thr_info;
	pthread_cancel (ti->thr_hnd);

	if (rdev->slave_count > 0)
	{
		for (i = 0; i < MIO_COUNTOF(rdev->slave); i++)
		{
			if (rdev->slave[i])
			{
				mio_dev_thr_slave_t* sdev = rdev->slave[i];

				/* nullify the pointer to the slave device
				 * before calling mio_dev_kill() on the slave device.
				 * the slave device can check this pointer to tell from
				 * self-initiated termination or master-driven termination */
				rdev->slave[i] = MIO_NULL;

				mio_dev_kill ((mio_dev_t*)sdev);
			}
		}
	}

	if (ti->thr_done) 
	{
printf ("THREAD DONE>...111\n");
		pthread_detach (ti->thr_hnd); /* pthread_join() may be blocking. */
		free_thr_info_resources (mio, ti);
		mio_freemem (mio, ti);
	}
	else
	{
printf ("THREAD NOT DONE>...111\n");
		mio_addcfmb (mio, ti, ready_to_free_thr_info);
	}
	rdev->thr_info = MIO_NULL;

	if (rdev->on_close) rdev->on_close (rdev, MIO_DEV_THR_MASTER);
	return 0;
}

static int dev_thr_kill_slave (mio_dev_t* dev, int force)
{
	mio_t* mio = dev->mio;
	mio_dev_thr_slave_t* rdev = (mio_dev_thr_slave_t*)dev;

	if (rdev->master)
	{
		mio_dev_thr_t* master;

		master = rdev->master;
		rdev->master = MIO_NULL;

		/* indicate EOF */
		if (master->on_close) master->on_close (master, rdev->id);

		MIO_ASSERT (mio, master->slave_count > 0);
		master->slave_count--;

		if (master->slave[rdev->id])
		{
			/* this call is started by the slave device itself. */
			if (master->slave_count <= 0) 
			{
				/* if this is the last slave, kill the master also */
				mio_dev_kill ((mio_dev_t*)master);
				/* the master pointer is not valid from this point onwards
				 * as the actual master device object is freed in mio_dev_kill() */
			}
			else
			{
				/* this call is initiated by this slave device itself.
				 * if it were by the master device, it would be MIO_NULL as
				 * nullified by the dev_thr_kill() */
				master->slave[rdev->id] = MIO_NULL;
			}
		}
	}

	if (rdev->pfd != MIO_SYSHND_INVALID)
	{
		close (rdev->pfd);
		rdev->pfd = MIO_SYSHND_INVALID;
	}

	return 0;
}

static int dev_thr_read_slave (mio_dev_t* dev, void* buf, mio_iolen_t* len, mio_devaddr_t* srcaddr)
{
	mio_dev_thr_slave_t* thr = (mio_dev_thr_slave_t*)dev;
	ssize_t x;

	/* the read and write operation happens on different slave devices.
	 * the write EOF indication doesn't affect this device 
	if (MIO_UNLIKELY(thr->pfd == MIO_SYSHND_INVALID))
	{
		mio_seterrnum (thr->mio, MIO_EBADHND);
		return -1;
	}*/
	MIO_ASSERT (thr->mio, thr->pfd != MIO_SYSHND_INVALID); /* use this assertion to check if my claim above is right */

	x = read(thr->pfd, buf, *len);
	if (x <= -1)
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data available */
		if (errno == EINTR) return 0;
		mio_seterrwithsyserr (thr->mio, 0, errno);
		return -1;
	}

	*len = x;
	return 1;
}

static int dev_thr_write_slave (mio_dev_t* dev, const void* data, mio_iolen_t* len, const mio_devaddr_t* dstaddr)
{
	mio_dev_thr_slave_t* thr = (mio_dev_thr_slave_t*)dev;
	ssize_t x;

	/* this check is not needed because MIO_DEV_CAP_OUT_CLOSED is set on the device by the core
	 * when EOF indication is successful(return value 1 and *iovcnt 0).
	 * If MIO_DEV_CAP_OUT_CLOSED, the core doesn't invoke the write method 
	if (MIO_UNLIKELY(thr->pfd == MIO_SYSHND_INVALID))
	{
		mio_seterrnum (thr->mio, MIO_EBADHND);
		return -1;
	}*/
	MIO_ASSERT (thr->mio, thr->pfd != MIO_SYSHND_INVALID); /* use this assertion to check if my claim above is right */

	if (MIO_UNLIKELY(*len <= 0))
	{
		/* this is an EOF indicator */
		/* It isn't apthrpriate to call mio_dev_halt(thr) or mio_dev_thr_close(thr->master, MIO_DEV_THR_IN)
		 * as those functions destroy the device itself */
		if (MIO_LIKELY(thr->pfd != MIO_SYSHND_INVALID))
		{
			mio_dev_watch (dev, MIO_DEV_WATCH_STOP, 0);
			close (thr->pfd);
			thr->pfd = MIO_SYSHND_INVALID;
		}
		return 1; /* indicate that the operation got successful. the core will execute on_write() with the write length of 0. */
	}

	x = write(thr->pfd, data, *len);
	if (x <= -1)
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data can be written */
		if (errno == EINTR) return 0;
		mio_seterrwithsyserr (thr->mio, 0, errno);
		return -1;
	}

	*len = x;
	return 1;
}

static int dev_thr_writev_slave (mio_dev_t* dev, const mio_iovec_t* iov, mio_iolen_t* iovcnt, const mio_devaddr_t* dstaddr)
{
	mio_dev_thr_slave_t* thr = (mio_dev_thr_slave_t*)dev;
	ssize_t x;

	/* this check is not needed because MIO_DEV_CAP_OUT_CLOSED is set on the device by the core
	 * when EOF indication is successful(return value 1 and *iovcnt 0).
	 * If MIO_DEV_CAP_OUT_CLOSED, the core doesn't invoke the write method 
	if (MIO_UNLIKELY(thr->pfd == MIO_SYSHND_INVALID))
	{
		mio_seterrnum (thr->mio, MIO_EBADHND);
		return -1;
	}*/
	MIO_ASSERT (thr->mio, thr->pfd != MIO_SYSHND_INVALID); /* use this assertion to check if my claim above is right */

	if (MIO_UNLIKELY(*iovcnt <= 0))
	{
		/* this is an EOF indicator */
		/* It isn't apthrpriate to call mio_dev_halt(thr) or mio_dev_thr_close(thr->master, MIO_DEV_THR_IN)
		 * as those functions destroy the device itself */
		if (MIO_LIKELY(thr->pfd != MIO_SYSHND_INVALID))
		{
			mio_dev_watch (dev, MIO_DEV_WATCH_STOP, 0);
			close (thr->pfd);
			thr->pfd = MIO_SYSHND_INVALID;
		}
		return 1; /* indicate that the operation got successful. the core will execute on_write() with 0. */
	}

	x = writev(thr->pfd, iov, *iovcnt);
	if (x <= -1)
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data can be written */
		if (errno == EINTR) return 0;
		mio_seterrwithsyserr (thr->mio, 0, errno);
		return -1;
	}

	*iovcnt = x;
	return 1;
}

static mio_syshnd_t dev_thr_getsyshnd (mio_dev_t* dev)
{
	return MIO_SYSHND_INVALID;
}

static mio_syshnd_t dev_thr_getsyshnd_slave (mio_dev_t* dev)
{
	mio_dev_thr_slave_t* thr = (mio_dev_thr_slave_t*)dev;
	return (mio_syshnd_t)thr->pfd;
}

static int dev_thr_ioctl (mio_dev_t* dev, int cmd, void* arg)
{
	mio_t* mio = dev->mio;
	mio_dev_thr_t* rdev = (mio_dev_thr_t*)dev;

	switch (cmd)
	{
		case MIO_DEV_THR_CLOSE:
		{
			mio_dev_thr_sid_t sid = *(mio_dev_thr_sid_t*)arg;

			if (MIO_UNLIKELY(sid != MIO_DEV_THR_IN && sid != MIO_DEV_THR_OUT))
			{
				mio_seterrnum (mio, MIO_EINVAL);
				return -1;
			}

			if (rdev->slave[sid])
			{
				/* unlike dev_thr_kill_master(), i don't nullify rdev->slave[sid].
				 * so i treat the closing ioctl as if it's a kill request 
				 * initiated by the slave device itself. */
				mio_dev_kill ((mio_dev_t*)rdev->slave[sid]);

				/* if this is the last slave, the master is destroyed as well. 
				 * therefore, using rdev is unsafe in the assertion below is unsafe.
				 *MIO_ASSERT (mio, rdev->slave[sid] == MIO_NULL); */
			}

			return 0;
		}

#if 0
		case MIO_DEV_THR_KILL_CHILD:
			if (rdev->child_pid >= 0)
			{
				if (kill(rdev->child_pid, SIGKILL) == -1)
				{
					mio_seterrwithsyserr (mio, 0, errno);
					return -1;
				}
			}
#endif

			return 0;

		default:
			mio_seterrnum (mio, MIO_EINVAL);
			return -1;
	}
}

static mio_dev_mth_t dev_thr_methods = 
{
	dev_thr_make_master,
	dev_thr_kill_master,
	dev_thr_getsyshnd,

	MIO_NULL,
	MIO_NULL,
	MIO_NULL,
	dev_thr_ioctl
};

static mio_dev_mth_t dev_thr_methods_slave =
{
	dev_thr_make_slave,
	dev_thr_kill_slave,
	dev_thr_getsyshnd_slave,

	dev_thr_read_slave,
	dev_thr_write_slave,
	dev_thr_writev_slave,
	dev_thr_ioctl
};

/* ========================================================================= */

static int thr_ready (mio_dev_t* dev, int events)
{
	/* virtual device. no I/O */
	mio_seterrnum (dev->mio, MIO_EINTERN);
	return -1;
}

static int thr_on_read (mio_dev_t* dev, const void* data, mio_iolen_t len, const mio_devaddr_t* srcaddr)
{
	/* virtual device. no I/O */
	mio_seterrnum (dev->mio, MIO_EINTERN);
	return -1;
}

static int thr_on_write (mio_dev_t* dev, mio_iolen_t wrlen, void* wrctx, const mio_devaddr_t* dstaddr)
{
	/* virtual device. no I/O */
	mio_seterrnum (dev->mio, MIO_EINTERN);
	return -1;
}

static mio_dev_evcb_t dev_thr_event_callbacks =
{
	thr_ready,
	thr_on_read,
	thr_on_write
};

/* ========================================================================= */

static int thr_ready_slave (mio_dev_t* dev, int events)
{
	mio_t* mio = dev->mio;
	/*mio_dev_thr_t* thr = (mio_dev_thr_t*)dev;*/

	if (events & MIO_DEV_EVENT_ERR)
	{
		mio_seterrnum (mio, MIO_EDEVERR);
		return -1;
	}

	if (events & MIO_DEV_EVENT_HUP)
	{
		if (events & (MIO_DEV_EVENT_PRI | MIO_DEV_EVENT_IN | MIO_DEV_EVENT_OUT)) 
		{
			/* thrbably half-open? */
			return 1;
		}

		mio_seterrnum (mio, MIO_EDEVHUP);
		return -1;
	}

	return 1; /* the device is ok. carry on reading or writing */
}


static int thr_on_read_slave (mio_dev_t* dev, const void* data, mio_iolen_t len, const mio_devaddr_t* srcaddr)
{
	mio_dev_thr_slave_t* thr = (mio_dev_thr_slave_t*)dev;
	return thr->master->on_read(thr->master, data, len);
}

static int thr_on_write_slave (mio_dev_t* dev, mio_iolen_t wrlen, void* wrctx, const mio_devaddr_t* dstaddr)
{
	mio_dev_thr_slave_t* thr = (mio_dev_thr_slave_t*)dev;
	return thr->master->on_write(thr->master, wrlen, wrctx);
}

static mio_dev_evcb_t dev_thr_event_callbacks_slave_in =
{
	thr_ready_slave,
	MIO_NULL,
	thr_on_write_slave
};

static mio_dev_evcb_t dev_thr_event_callbacks_slave_out =
{
	thr_ready_slave,
	thr_on_read_slave,
	MIO_NULL
};

/* ========================================================================= */

static mio_dev_thr_slave_t* make_slave (mio_t* mio, slave_info_t* si)
{
	switch (si->id)
	{
		case MIO_DEV_THR_IN:
			return (mio_dev_thr_slave_t*)mio_dev_make(
				mio, MIO_SIZEOF(mio_dev_thr_t), 
				&dev_thr_methods_slave, &dev_thr_event_callbacks_slave_in, si);

		case MIO_DEV_THR_OUT:
			return (mio_dev_thr_slave_t*)mio_dev_make(
				mio, MIO_SIZEOF(mio_dev_thr_t), 
				&dev_thr_methods_slave, &dev_thr_event_callbacks_slave_out, si);

		default:
			mio_seterrnum (mio, MIO_EINVAL);
			return MIO_NULL;
	}
}

mio_dev_thr_t* mio_dev_thr_make (mio_t* mio, mio_oow_t xtnsize, const mio_dev_thr_make_t* info)
{
	return (mio_dev_thr_t*)mio_dev_make(
		mio, MIO_SIZEOF(mio_dev_thr_t) + xtnsize, 
		&dev_thr_methods, &dev_thr_event_callbacks, (void*)info);
}

void mio_dev_thr_kill (mio_dev_thr_t* dev)
{
	mio_dev_kill ((mio_dev_t*)dev);
}

void mio_dev_thr_halt (mio_dev_thr_t* dev)
{
	mio_dev_halt ((mio_dev_t*)dev);
}

int mio_dev_thr_read (mio_dev_thr_t* dev, int enabled)
{
	if (dev->slave[MIO_DEV_THR_OUT])
	{
		return mio_dev_read((mio_dev_t*)dev->slave[MIO_DEV_THR_OUT], enabled);
	}
	else
	{
		mio_seterrnum (dev->mio, MIO_ENOCAPA); /* TODO: is it the right error number? */
		return -1;
	}
}

int mio_dev_thr_timedread (mio_dev_thr_t* dev, int enabled, const mio_ntime_t* tmout)
{
	if (dev->slave[MIO_DEV_THR_OUT])
	{
		return mio_dev_timedread((mio_dev_t*)dev->slave[MIO_DEV_THR_OUT], enabled, tmout);
	}
	else
	{
		mio_seterrnum (dev->mio, MIO_ENOCAPA); /* TODO: is it the right error number? */
		return -1;
	}
}

int mio_dev_thr_write (mio_dev_thr_t* dev, const void* data, mio_iolen_t dlen, void* wrctx)
{
	if (dev->slave[MIO_DEV_THR_IN])
	{
		return mio_dev_write((mio_dev_t*)dev->slave[MIO_DEV_THR_IN], data, dlen, wrctx, MIO_NULL);
	}
	else
	{
		mio_seterrnum (dev->mio, MIO_ENOCAPA); /* TODO: is it the right error number? */
		return -1;
	}
}

int mio_dev_thr_timedwrite (mio_dev_thr_t* dev, const void* data, mio_iolen_t dlen, const mio_ntime_t* tmout, void* wrctx)
{
	if (dev->slave[MIO_DEV_THR_IN])
	{
		return mio_dev_timedwrite((mio_dev_t*)dev->slave[MIO_DEV_THR_IN], data, dlen, tmout, wrctx, MIO_NULL);
	}
	else
	{
		mio_seterrnum (dev->mio, MIO_ENOCAPA); /* TODO: is it the right error number? */
		return -1;
	}
}

int mio_dev_thr_close (mio_dev_thr_t* dev, mio_dev_thr_sid_t sid)
{
	return mio_dev_ioctl((mio_dev_t*)dev, MIO_DEV_THR_CLOSE, &sid);
}

void mio_dev_thr_haltslave (mio_dev_thr_t* dev, mio_dev_thr_sid_t sid)
{
	if (sid >= 0 && sid < MIO_COUNTOF(dev->slave) && dev->slave[sid])
		mio_dev_halt((mio_dev_t*)dev->slave[sid]);
}

#if 0
mio_dev_thr_t* mio_dev_thr_getdev (mio_dev_thr_t* thr, mio_dev_thr_sid_t sid)
{
	switch (type)
	{
		case MIO_DEV_THR_IN:
			return XXX;

		case MIO_DEV_THR_OUT:
			return XXX;

		case MIO_DEV_THR_ERR:
			return XXX;
	}

	thr->dev->mio = MIO_EINVAL;
	return MIO_NULL;
}
#endif
