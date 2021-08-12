/*
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

#include <hio-thr.h>
#include "hio-prv.h"

#include <unistd.h>
#include <errno.h>
#include <sys/uio.h>
#include <pthread.h>

#include <stdio.h>
/* ========================================================================= */

struct hio_dev_thr_info_t
{
	HIO_CFMB_HEADER;

	hio_dev_thr_func_t thr_func;
	hio_dev_thr_iopair_t thr_iop;
	void* thr_ctx;
	pthread_t thr_hnd;
	int thr_done;
};

struct slave_info_t
{
	hio_dev_thr_make_t* mi;
	hio_syshnd_t pfd;
	int dev_cap;
	hio_dev_thr_sid_t id;
};

typedef struct slave_info_t slave_info_t;

static hio_dev_thr_slave_t* make_slave (hio_t* hio, slave_info_t* si);

/* ========================================================================= */


static void free_thr_info_resources (hio_t* hio, hio_dev_thr_info_t* ti)
{
	if (ti->thr_iop.rfd != HIO_SYSHND_INVALID) 
	{
		/* this function is called at the end of run_thr_func() and
		 * close() can be a thread cancellation point.
		 *
		 * i must invalidate ti->thr_iop.rfd calling close() with it. 
		 * if resetting is done after close() and close() becomes a cancellation point, 
		 * the invalidation operation gets skipped. */
		hio_syshnd_t tmp = ti->thr_iop.rfd;
		ti->thr_iop.rfd = HIO_SYSHND_INVALID;  
		close (tmp);
	}
	if (ti->thr_iop.wfd != HIO_SYSHND_INVALID) 
	{
		hio_syshnd_t tmp = ti->thr_iop.wfd;
		ti->thr_iop.wfd = HIO_SYSHND_INVALID;
		close (tmp);
	}
}

static int ready_to_free_thr_info (hio_t* hio, hio_cfmb_t* cfmb)
{
	hio_dev_thr_info_t* ti = (hio_dev_thr_info_t*)cfmb;

#if 1
	if (HIO_UNLIKELY(hio->_fini_in_progress))
	{
		pthread_join (ti->thr_hnd, HIO_NULL); /* BAD. blocking call in a non-blocking library. not useful to call pthread_tryjoin_np() here. */
		free_thr_info_resources (hio, ti);
		return 1; /* free me */
	}
#endif

	if (ti->thr_done)
	{
		free_thr_info_resources (hio, ti);
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
	hio_dev_thr_info_t* ti = (hio_dev_thr_info_t*)ctx;
	ti->thr_done = 1;
}

static void* run_thr_func (void* ctx)
{
	hio_dev_thr_info_t* ti = (hio_dev_thr_info_t*)ctx;

	/* i assume the thread is cancellable, and of the deferred cancellation type by default */
	/*int dummy;
	pthread_setcancelstate (PTHREAD_CANCEL_ENABLE, &dummy);
	pthread_setcanceltype (PTHREAD_CANCEL_DEFERRED, &dummy);*/

	pthread_cleanup_push (mark_thr_done, ti);

	ti->thr_func (ti->hio, &ti->thr_iop, ti->thr_ctx);

	free_thr_info_resources (ti->hio, ti); 

	pthread_cleanup_pop (1);
	pthread_exit (HIO_NULL);
	return HIO_NULL;
}

static int dev_thr_make_master (hio_dev_t* dev, void* ctx)
{
	hio_t* hio = dev->hio;
	hio_dev_thr_t* rdev = (hio_dev_thr_t*)dev;
	hio_dev_thr_make_t* info = (hio_dev_thr_make_t*)ctx;
	hio_syshnd_t pfds[4] = { HIO_SYSHND_INVALID, HIO_SYSHND_INVALID, HIO_SYSHND_INVALID, HIO_SYSHND_INVALID };
	slave_info_t si;
	int i;

#if defined(HAVE_PIPE2) && defined(O_CLOEXEC) && defined(O_NONBLOCK)
	if (pipe2(&pfds[0], O_CLOEXEC | O_NONBLOCK) == -1 ||
	    pipe2(&pfds[2], O_CLOEXEC | O_NONBLOCK) == -1)
	{
		if (errno != ENOSYS) goto pipe_error;
	}
	else goto pipe_done;
#endif

	if (pipe(&pfds[0]) == -1 || pipe(&pfds[2]) == -1)
	{
#if defined(HAVE_PIPE2) && defined(O_CLOEXEC) && defined(O_NONBLOCK)
	pipe_error:
#endif
		hio_seterrwithsyserr (hio, 0, errno);
		goto oops;
	}

	if (hio_makesyshndasync(hio, pfds[1]) <= -1 ||
	    hio_makesyshndasync(hio, pfds[2]) <= -1) goto oops;

	if (hio_makesyshndcloexec(hio, pfds[0]) <= -1 ||
	    hio_makesyshndcloexec(hio, pfds[1]) <= -1 ||
	    hio_makesyshndcloexec(hio, pfds[2]) <= -1 ||
	    hio_makesyshndcloexec(hio, pfds[1]) <= -1) goto oops;

#if defined(HAVE_PIPE2) && defined(O_CLOEXEC) && defined(O_NONBLOCK)
pipe_done:
#endif
	si.mi = info;
	si.pfd = pfds[1];
	si.dev_cap = HIO_DEV_CAP_OUT | HIO_DEV_CAP_STREAM;
	si.id = HIO_DEV_THR_IN;

	/* invalidate pfds[1] before calling make_slave() because when it fails, the 
	 * fail_before_make(dev_thr_fail_before_make_slave) and kill(dev_thr_kill_slave) callbacks close si.pfd */
	pfds[1] = HIO_SYSHND_INVALID;
					
	rdev->slave[HIO_DEV_THR_IN] = make_slave(hio, &si);
	if (!rdev->slave[HIO_DEV_THR_IN]) goto oops;
	rdev->slave_count++;

	si.mi = info;
	si.pfd = pfds[2];
	si.dev_cap = HIO_DEV_CAP_IN | HIO_DEV_CAP_STREAM;
	si.id = HIO_DEV_THR_OUT;
	/* invalidate pfds[2] before calling make_slave() because when it fails, the 
	 * fail_before_make(dev_thr_fail_before_make_slave) and kill(dev_thr_kill_slave) callbacks close si.pfd */
	pfds[2] = HIO_SYSHND_INVALID;
	rdev->slave[HIO_DEV_THR_OUT] = make_slave(hio, &si);
	if (!rdev->slave[HIO_DEV_THR_OUT]) goto oops;
	rdev->slave_count++;

	for (i = 0; i < HIO_COUNTOF(rdev->slave); i++) 
	{
		if (rdev->slave[i]) rdev->slave[i]->master = rdev;
	}

	rdev->dev_cap = HIO_DEV_CAP_VIRTUAL; /* the master device doesn't perform I/O */
	rdev->on_read = info->on_read;
	rdev->on_write = info->on_write;
	rdev->on_close = info->on_close;
	
	/* ---------------------------------------------------------- */
	{
		int n;
		hio_dev_thr_info_t* ti;

		ti = hio_callocmem(hio, HIO_SIZEOF(*ti));
		if (HIO_UNLIKELY(!ti)) goto oops;

		ti->hio = hio;
		ti->thr_iop.rfd = pfds[0];
		ti->thr_iop.wfd = pfds[3];
		ti->thr_func = info->thr_func;
		ti->thr_ctx = info->thr_ctx;

		rdev->thr_info = ti;
		n = pthread_create(&ti->thr_hnd, HIO_NULL, run_thr_func, ti);
		if (n != 0) 
		{
			rdev->thr_info = HIO_NULL;
			hio_freemem (hio, ti);
			goto oops;
		}

		/* the thread function is in charge of these two file descriptors */
		pfds[0] = HIO_SYSHND_INVALID;
		pfds[3] = HIO_SYSHND_INVALID;
	}
	/* ---------------------------------------------------------- */


	return 0;

oops:
	for (i = 0; i < HIO_COUNTOF(pfds); i++)
	{
		if (pfds[i] != HIO_SYSHND_INVALID) 
		{
			close (pfds[i]);
		}
	}

	for (i = HIO_COUNTOF(rdev->slave); i > 0; )
	{
		i--;
		if (rdev->slave[i])
		{
			hio_dev_kill ((hio_dev_t*)rdev->slave[i]);
			rdev->slave[i] = HIO_NULL;
		}
	}
	rdev->slave_count = 0;

	return -1;
}

static int dev_thr_make_slave (hio_dev_t* dev, void* ctx)
{
	hio_dev_thr_slave_t* rdev = (hio_dev_thr_slave_t*)dev;
	slave_info_t* si = (slave_info_t*)ctx;

	rdev->dev_cap = si->dev_cap;
	rdev->id = si->id;
	rdev->pfd = si->pfd;
	/* keep rdev->master to HIO_NULL. it's set to the right master
	 * device in dev_thr_make() */

	return 0;
}

static int dev_thr_kill_master (hio_dev_t* dev, int force)
{
	hio_t* hio = dev->hio;
	hio_dev_thr_t* rdev = (hio_dev_thr_t*)dev;
	hio_dev_thr_info_t* ti;
	int i;

	ti = rdev->thr_info;
	/* pthread_cancel() seems to create some dangling file descriptors not closed properly.
	 * i don't seem to get it working correctly as of now. proper cancellation point management
	 * is very difficult. without pthread_cancel() here, higher pressure on cfmb is expected */
	/*pthread_cancel (ti->thr_hnd); */

	if (rdev->slave_count > 0)
	{
		for (i = 0; i < HIO_COUNTOF(rdev->slave); i++)
		{
			if (rdev->slave[i])
			{
				hio_dev_thr_slave_t* sdev = rdev->slave[i];

				/* nullify the pointer to the slave device
				 * before calling hio_dev_kill() on the slave device.
				 * the slave device can check this pointer to tell from
				 * self-initiated termination or master-driven termination */
				rdev->slave[i] = HIO_NULL;

				hio_dev_kill ((hio_dev_t*)sdev);
			}
		}
	}

	rdev->thr_info = HIO_NULL;
	if (ti->thr_done) 
	{
		pthread_detach (ti->thr_hnd); /* pthread_join() may be blocking. detach the thread instead */
		free_thr_info_resources (hio, ti);
		hio_freemem (hio, ti);
	}
	else
	{
	#if 0
		/* since pthread_join can be blocking, i'd schedule a resource destroyer with hio_addcfmb(). 
		 * see after #else */
		pthread_join (ti->thr_hnd, HIO_NULL);
		free_thr_info_resources (hio, ti);
		hio_freemem (hio, ti);
	#else
		/* schedule a resource destroyer */
		hio_addcfmb (hio, ti, ready_to_free_thr_info);
	#endif
	}

	if (rdev->on_close) rdev->on_close (rdev, HIO_DEV_THR_MASTER);
	return 0;
}

static int dev_thr_kill_slave (hio_dev_t* dev, int force)
{
	hio_t* hio = dev->hio;
	hio_dev_thr_slave_t* rdev = (hio_dev_thr_slave_t*)dev;

	if (rdev->master)
	{
		hio_dev_thr_t* master;

		master = rdev->master;
		rdev->master = HIO_NULL;

		/* indicate EOF */
		if (master->on_close) master->on_close (master, rdev->id);

		HIO_ASSERT (hio, master->slave_count > 0);
		master->slave_count--;

		if (master->slave[rdev->id])
		{
			/* this call is started by the slave device itself. */
			if (master->slave_count <= 0) 
			{
				/* if this is the last slave, kill the master also */
				hio_dev_kill ((hio_dev_t*)master);
				/* the master pointer is not valid from this point onwards
				 * as the actual master device object is freed in hio_dev_kill() */
			}
			else
			{
				/* this call is initiated by this slave device itself.
				 * if it were by the master device, it would be HIO_NULL as
				 * nullified by the dev_thr_kill() */
				master->slave[rdev->id] = HIO_NULL;
			}
		}
	}

	if (rdev->pfd != HIO_SYSHND_INVALID)
	{
		close (rdev->pfd);
		rdev->pfd = HIO_SYSHND_INVALID;
	}

	return 0;
}

static void dev_thr_fail_before_make_slave (void* ctx)
{
	slave_info_t* si = (slave_info_t*)ctx;
	/* hio_dev_make() failed before it called the make() callback.
	 * i will close the pipe fd here instead of in the caller of hio_dev_make() */
	close (si->pfd);
}

static int dev_thr_read_slave (hio_dev_t* dev, void* buf, hio_iolen_t* len, hio_devaddr_t* srcaddr)
{
	hio_dev_thr_slave_t* thr = (hio_dev_thr_slave_t*)dev;
	ssize_t x;

	/* the read and write operation happens on different slave devices.
	 * the write EOF indication doesn't affect this device 
	if (HIO_UNLIKELY(thr->pfd == HIO_SYSHND_INVALID))
	{
		hio_seterrnum (thr->hio, HIO_EBADHND);
		return -1;
	}*/
	HIO_ASSERT (thr->hio, thr->pfd != HIO_SYSHND_INVALID); /* use this assertion to check if my claim above is right */

	x = read(thr->pfd, buf, *len);
	if (x <= -1)
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data available */
		if (errno == EINTR) return 0;
		hio_seterrwithsyserr (thr->hio, 0, errno);
		return -1;
	}

	*len = x;
	return 1;
}

static int dev_thr_write_slave (hio_dev_t* dev, const void* data, hio_iolen_t* len, const hio_devaddr_t* dstaddr)
{
	hio_dev_thr_slave_t* thr = (hio_dev_thr_slave_t*)dev;
	ssize_t x;

	/* this check is not needed because HIO_DEV_CAP_OUT_CLOSED is set on the device by the core
	 * when EOF indication is successful(return value 1 and *iovcnt 0).
	 * If HIO_DEV_CAP_OUT_CLOSED, the core doesn't invoke the write method 
	if (HIO_UNLIKELY(thr->pfd == HIO_SYSHND_INVALID))
	{
		hio_seterrnum (thr->hio, HIO_EBADHND);
		return -1;
	}*/
	HIO_ASSERT (thr->hio, thr->pfd != HIO_SYSHND_INVALID); /* use this assertion to check if my claim above is right */

	if (HIO_UNLIKELY(*len <= 0))
	{
		/* this is an EOF indicator */
		/* It isn't appropriate to call hio_dev_halt(thr) or hio_dev_thr_close(thr->master, HIO_DEV_THR_IN)
		 * as those functions destroy the device itself */
		if (HIO_LIKELY(thr->pfd != HIO_SYSHND_INVALID))
		{
			hio_dev_watch (dev, HIO_DEV_WATCH_STOP, 0);
			close (thr->pfd);
			thr->pfd = HIO_SYSHND_INVALID;
		}
		return 1; /* indicate that the operation got successful. the core will execute on_write() with the write length of 0. */
	}

	x = write(thr->pfd, data, *len);
	if (x <= -1)
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data can be written */
		if (errno == EINTR) return 0;
		hio_seterrwithsyserr (thr->hio, 0, errno);
		return -1;
	}

	*len = x;
	return 1;
}

static int dev_thr_writev_slave (hio_dev_t* dev, const hio_iovec_t* iov, hio_iolen_t* iovcnt, const hio_devaddr_t* dstaddr)
{
	hio_dev_thr_slave_t* thr = (hio_dev_thr_slave_t*)dev;
	ssize_t x;

	/* this check is not needed because HIO_DEV_CAP_OUT_CLOSED is set on the device by the core
	 * when EOF indication is successful(return value 1 and *iovcnt 0).
	 * If HIO_DEV_CAP_OUT_CLOSED, the core doesn't invoke the write method 
	if (HIO_UNLIKELY(thr->pfd == HIO_SYSHND_INVALID))
	{
		hio_seterrnum (thr->hio, HIO_EBADHND);
		return -1;
	}*/
	HIO_ASSERT (thr->hio, thr->pfd != HIO_SYSHND_INVALID); /* use this assertion to check if my claim above is right */

	if (HIO_UNLIKELY(*iovcnt <= 0))
	{
		/* this is an EOF indicator */
		/* It isn't apthrpriate to call hio_dev_halt(thr) or hio_dev_thr_close(thr->master, HIO_DEV_THR_IN)
		 * as those functions destroy the device itself */
		if (HIO_LIKELY(thr->pfd != HIO_SYSHND_INVALID))
		{
			hio_dev_watch (dev, HIO_DEV_WATCH_STOP, 0);
			close (thr->pfd);
			thr->pfd = HIO_SYSHND_INVALID;
		}
		return 1; /* indicate that the operation got successful. the core will execute on_write() with 0. */
	}

	x = writev(thr->pfd, iov, *iovcnt);
	if (x <= -1)
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data can be written */
		if (errno == EINTR) return 0;
		hio_seterrwithsyserr (thr->hio, 0, errno);
		return -1;
	}

	*iovcnt = x;
	return 1;
}

static hio_syshnd_t dev_thr_getsyshnd (hio_dev_t* dev)
{
	return HIO_SYSHND_INVALID;
}

static hio_syshnd_t dev_thr_getsyshnd_slave (hio_dev_t* dev)
{
	hio_dev_thr_slave_t* thr = (hio_dev_thr_slave_t*)dev;
	return (hio_syshnd_t)thr->pfd;
}

static int dev_thr_ioctl (hio_dev_t* dev, int cmd, void* arg)
{
	hio_t* hio = dev->hio;
	hio_dev_thr_t* rdev = (hio_dev_thr_t*)dev;

	switch (cmd)
	{
		case HIO_DEV_THR_CLOSE:
		{
			hio_dev_thr_sid_t sid = *(hio_dev_thr_sid_t*)arg;

			if (HIO_UNLIKELY(sid != HIO_DEV_THR_IN && sid != HIO_DEV_THR_OUT))
			{
				hio_seterrnum (hio, HIO_EINVAL);
				return -1;
			}

			if (rdev->slave[sid])
			{
				/* unlike dev_thr_kill_master(), i don't nullify rdev->slave[sid].
				 * so i treat the closing ioctl as if it's a kill request 
				 * initiated by the slave device itself. */
				hio_dev_kill ((hio_dev_t*)rdev->slave[sid]);

				/* if this is the last slave, the master is destroyed as well. 
				 * therefore, using rdev is unsafe in the assertion below is unsafe.
				 *HIO_ASSERT (hio, rdev->slave[sid] == HIO_NULL); */
			}

			return 0;
		}

#if 0
		case HIO_DEV_THR_KILL_CHILD:
			if (rdev->child_pid >= 0)
			{
				if (kill(rdev->child_pid, SIGKILL) == -1)
				{
					hio_seterrwithsyserr (hio, 0, errno);
					return -1;
				}
			}
#endif

			return 0;

		default:
			hio_seterrnum (hio, HIO_EINVAL);
			return -1;
	}
}

static hio_dev_mth_t dev_thr_methods = 
{
	dev_thr_make_master,
	dev_thr_kill_master,
	HIO_NULL,
	dev_thr_getsyshnd,
	HIO_NULL,
	dev_thr_ioctl,

	HIO_NULL,
	HIO_NULL,
	HIO_NULL,
	HIO_NULL /* sendfile */
};

static hio_dev_mth_t dev_thr_methods_slave =
{
	dev_thr_make_slave,
	dev_thr_kill_slave,
	dev_thr_fail_before_make_slave,
	dev_thr_getsyshnd_slave,
	HIO_NULL,
	dev_thr_ioctl,

	dev_thr_read_slave,
	dev_thr_write_slave,
	dev_thr_writev_slave,
	HIO_NULL, /* sendfile */
};

/* ========================================================================= */

static int thr_ready (hio_dev_t* dev, int events)
{
	/* virtual device. no I/O */
	hio_seterrnum (dev->hio, HIO_EINTERN);
	return -1;
}

static int thr_on_read (hio_dev_t* dev, const void* data, hio_iolen_t len, const hio_devaddr_t* srcaddr)
{
	/* virtual device. no I/O */
	hio_seterrnum (dev->hio, HIO_EINTERN);
	return -1;
}

static int thr_on_write (hio_dev_t* dev, hio_iolen_t wrlen, void* wrctx, const hio_devaddr_t* dstaddr)
{
	/* virtual device. no I/O */
	hio_seterrnum (dev->hio, HIO_EINTERN);
	return -1;
}

static hio_dev_evcb_t dev_thr_event_callbacks =
{
	thr_ready,
	thr_on_read,
	thr_on_write
};

/* ========================================================================= */

static int thr_ready_slave (hio_dev_t* dev, int events)
{
	hio_t* hio = dev->hio;
	/*hio_dev_thr_t* thr = (hio_dev_thr_t*)dev;*/

	if (events & HIO_DEV_EVENT_ERR)
	{
		hio_seterrnum (hio, HIO_EDEVERR);
		return -1;
	}

	if (events & HIO_DEV_EVENT_HUP)
	{
		if (events & (HIO_DEV_EVENT_PRI | HIO_DEV_EVENT_IN | HIO_DEV_EVENT_OUT)) 
		{
			/* thrbably half-open? */
			return 1;
		}

		hio_seterrnum (hio, HIO_EDEVHUP);
		return -1;
	}

	return 1; /* the device is ok. carry on reading or writing */
}


static int thr_on_read_slave (hio_dev_t* dev, const void* data, hio_iolen_t len, const hio_devaddr_t* srcaddr)
{
	hio_dev_thr_slave_t* thr = (hio_dev_thr_slave_t*)dev;
	return thr->master->on_read(thr->master, data, len);
}

static int thr_on_write_slave (hio_dev_t* dev, hio_iolen_t wrlen, void* wrctx, const hio_devaddr_t* dstaddr)
{
	hio_dev_thr_slave_t* thr = (hio_dev_thr_slave_t*)dev;
	return thr->master->on_write(thr->master, wrlen, wrctx);
}

static hio_dev_evcb_t dev_thr_event_callbacks_slave_in =
{
	thr_ready_slave,
	HIO_NULL,
	thr_on_write_slave
};

static hio_dev_evcb_t dev_thr_event_callbacks_slave_out =
{
	thr_ready_slave,
	thr_on_read_slave,
	HIO_NULL
};

/* ========================================================================= */

static hio_dev_thr_slave_t* make_slave (hio_t* hio, slave_info_t* si)
{
	switch (si->id)
	{
		case HIO_DEV_THR_IN:
			return (hio_dev_thr_slave_t*)hio_dev_make(
				hio, HIO_SIZEOF(hio_dev_thr_t), 
				&dev_thr_methods_slave, &dev_thr_event_callbacks_slave_in, si);

		case HIO_DEV_THR_OUT:
			return (hio_dev_thr_slave_t*)hio_dev_make(
				hio, HIO_SIZEOF(hio_dev_thr_t), 
				&dev_thr_methods_slave, &dev_thr_event_callbacks_slave_out, si);

		default:
			hio_seterrnum (hio, HIO_EINVAL);
			return HIO_NULL;
	}
}

hio_dev_thr_t* hio_dev_thr_make (hio_t* hio, hio_oow_t xtnsize, const hio_dev_thr_make_t* info)
{
	return (hio_dev_thr_t*)hio_dev_make(
		hio, HIO_SIZEOF(hio_dev_thr_t) + xtnsize, 
		&dev_thr_methods, &dev_thr_event_callbacks, (void*)info);
}

void hio_dev_thr_kill (hio_dev_thr_t* dev)
{
	hio_dev_kill ((hio_dev_t*)dev);
}

void hio_dev_thr_halt (hio_dev_thr_t* dev)
{
	hio_dev_halt ((hio_dev_t*)dev);
}

int hio_dev_thr_read (hio_dev_thr_t* dev, int enabled)
{
	if (dev->slave[HIO_DEV_THR_OUT])
	{
		return hio_dev_read((hio_dev_t*)dev->slave[HIO_DEV_THR_OUT], enabled);
	}
	else
	{
		hio_seterrnum (dev->hio, HIO_ENOCAPA); /* TODO: is it the right error number? */
		return -1;
	}
}

int hio_dev_thr_timedread (hio_dev_thr_t* dev, int enabled, const hio_ntime_t* tmout)
{
	if (dev->slave[HIO_DEV_THR_OUT])
	{
		return hio_dev_timedread((hio_dev_t*)dev->slave[HIO_DEV_THR_OUT], enabled, tmout);
	}
	else
	{
		hio_seterrnum (dev->hio, HIO_ENOCAPA); /* TODO: is it the right error number? */
		return -1;
	}
}

int hio_dev_thr_write (hio_dev_thr_t* dev, const void* data, hio_iolen_t dlen, void* wrctx)
{
	if (dev->slave[HIO_DEV_THR_IN])
	{
		return hio_dev_write((hio_dev_t*)dev->slave[HIO_DEV_THR_IN], data, dlen, wrctx, HIO_NULL);
	}
	else
	{
		hio_seterrnum (dev->hio, HIO_ENOCAPA); /* TODO: is it the right error number? */
		return -1;
	}
}

int hio_dev_thr_timedwrite (hio_dev_thr_t* dev, const void* data, hio_iolen_t dlen, const hio_ntime_t* tmout, void* wrctx)
{
	if (dev->slave[HIO_DEV_THR_IN])
	{
		return hio_dev_timedwrite((hio_dev_t*)dev->slave[HIO_DEV_THR_IN], data, dlen, tmout, wrctx, HIO_NULL);
	}
	else
	{
		hio_seterrnum (dev->hio, HIO_ENOCAPA); /* TODO: is it the right error number? */
		return -1;
	}
}

int hio_dev_thr_close (hio_dev_thr_t* dev, hio_dev_thr_sid_t sid)
{
	return hio_dev_ioctl((hio_dev_t*)dev, HIO_DEV_THR_CLOSE, &sid);
}

void hio_dev_thr_haltslave (hio_dev_thr_t* dev, hio_dev_thr_sid_t sid)
{
	if (sid >= 0 && sid < HIO_COUNTOF(dev->slave) && dev->slave[sid])
		hio_dev_halt((hio_dev_t*)dev->slave[sid]);
}

#if 0
hio_dev_thr_t* hio_dev_thr_getdev (hio_dev_thr_t* thr, hio_dev_thr_sid_t sid)
{
	switch (type)
	{
		case HIO_DEV_THR_IN:
			return XXX;

		case HIO_DEV_THR_OUT:
			return XXX;

		case HIO_DEV_THR_ERR:
			return XXX;
	}

	thr->dev->hio = HIO_EINVAL;
	return HIO_NULL;
}
#endif
