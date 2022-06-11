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

#include <hio-pipe.h>
#include "hio-prv.h"

#include <unistd.h>
#include <errno.h>
#include <sys/uio.h>

/* ========================================================================= */

struct slave_info_t
{
	hio_dev_pipe_make_t* mi;
	hio_syshnd_t pfd;
	int dev_cap;
	hio_dev_pipe_sid_t id;
};

typedef struct slave_info_t slave_info_t;

static hio_dev_pipe_slave_t* make_slave (hio_t* hio, slave_info_t* si);

/* ========================================================================= */

static int dev_pipe_make_master (hio_dev_t* dev, void* ctx)
{
	hio_t* hio = dev->hio;
	hio_dev_pipe_t* rdev = (hio_dev_pipe_t*)dev;
	hio_dev_pipe_make_t* info = (hio_dev_pipe_make_t*)ctx;
	hio_syshnd_t pfds[2] = { HIO_SYSHND_INVALID, HIO_SYSHND_INVALID };
	slave_info_t si;
	int i;

/* TODO: support a named pipe. use mkfifo()?
 *       support socketpair */

#if defined(HAVE_PIPE2) && defined(O_CLOEXEC) && defined(O_NONBLOCK)
	if (pipe2(pfds, O_CLOEXEC | O_NONBLOCK) == -1)
	{
		if (errno != ENOSYS) goto pipe_error;
	}
	else goto pipe_done;
#endif
	if (pipe(pfds) == -1)
	{
#if defined(HAVE_PIPE2) && defined(O_CLOEXEC) && defined(O_NONBLOCK)
	pipe_error:
#endif
		hio_seterrwithsyserr (hio, 0, errno);
		goto oops;
	}

	if (hio_makesyshndasync(hio, pfds[0]) <= -1 ||
	    hio_makesyshndasync(hio, pfds[1]) <= -1) goto oops;

	if (hio_makesyshndcloexec(hio, pfds[0]) <= -1 ||
	    hio_makesyshndcloexec(hio, pfds[1]) <= -1) goto oops;

#if defined(HAVE_PIPE2) && defined(O_CLOEXEC) && defined(O_NONBLOCK)
pipe_done:
#endif
	si.mi = info;
	si.pfd = pfds[0];
	si.dev_cap = HIO_DEV_CAP_IN | HIO_DEV_CAP_STREAM;
	si.id = HIO_DEV_PIPE_IN;
	pfds[0] = HIO_SYSHND_INVALID;
	rdev->slave[HIO_DEV_PIPE_IN] = make_slave(hio, &si);
	if (!rdev->slave[HIO_DEV_PIPE_IN]) goto oops;
	rdev->slave_count++;

	si.mi = info;
	si.pfd = pfds[1];
	si.dev_cap = HIO_DEV_CAP_OUT | HIO_DEV_CAP_STREAM;
	si.id = HIO_DEV_PIPE_OUT;
	pfds[1] = HIO_SYSHND_INVALID;
	rdev->slave[HIO_DEV_PIPE_OUT] = make_slave(hio, &si);
	if (!rdev->slave[HIO_DEV_PIPE_OUT]) goto oops;
	rdev->slave_count++;

	for (i = 0; i < HIO_COUNTOF(rdev->slave); i++) 
	{
		if (rdev->slave[i]) rdev->slave[i]->master = rdev;
	}

	rdev->dev_cap = HIO_DEV_CAP_VIRTUAL; /* the master device doesn't perform I/O */
	rdev->on_read = info->on_read;
	rdev->on_write = info->on_write;
	rdev->on_close = info->on_close;
	return 0;

oops:
	if (pfds[0] != HIO_SYSHND_INVALID) close (pfds[0]);
	if (pfds[1] != HIO_SYSHND_INVALID) close (pfds[0]);

	if (rdev->slave[0])
	{
		hio_dev_kill ((hio_dev_t*)rdev->slave[0]);
		rdev->slave[0] = HIO_NULL;
	}
	if (rdev->slave[1])
	{
		hio_dev_kill ((hio_dev_t*)rdev->slave[1]);
		rdev->slave[1] = HIO_NULL;
	}

	rdev->slave_count = 0;
	return -1;
}

static int dev_pipe_make_slave (hio_dev_t* dev, void* ctx)
{
	hio_dev_pipe_slave_t* rdev = (hio_dev_pipe_slave_t*)dev;
	slave_info_t* si = (slave_info_t*)ctx;

	rdev->dev_cap = si->dev_cap;
	rdev->id = si->id;
	rdev->pfd = si->pfd;
	/* keep rdev->master to HIO_NULL. it's set to the right master
	 * device in dev_pipe_make() */

	return 0;
}

static int dev_pipe_kill_master (hio_dev_t* dev, int force)
{
	/*hio_t* hio = dev->hio;*/
	hio_dev_pipe_t* rdev = (hio_dev_pipe_t*)dev;
	int i;

	if (rdev->slave_count > 0)
	{
		for (i = 0; i < HIO_COUNTOF(rdev->slave); i++)
		{
			if (rdev->slave[i])
			{
				hio_dev_pipe_slave_t* sdev = rdev->slave[i];

				/* nullify the pointer to the slave device
				 * before calling hio_dev_kill() on the slave device.
				 * the slave device can check this pointer to tell from
				 * self-initiated termination or master-driven termination */
				rdev->slave[i] = HIO_NULL;

				hio_dev_kill ((hio_dev_t*)sdev);
			}
		}
	}

	if (rdev->on_close) rdev->on_close (rdev, HIO_DEV_PIPE_MASTER);
	return 0;
}

static int dev_pipe_kill_slave (hio_dev_t* dev, int force)
{
	hio_t* hio = dev->hio;
	hio_dev_pipe_slave_t* rdev = (hio_dev_pipe_slave_t*)dev;

	if (rdev->master)
	{
		hio_dev_pipe_t* master;

		master = rdev->master;
		rdev->master = HIO_NULL;

		/* indicate EOF */
		if (master->on_close) master->on_close (master, rdev->id);

		HIO_ASSERT (hio, master->slave_count > 0);
		master->slave_count--;

		if (master->slave[rdev->id])
		{
			/* this call is started by the slave device itself.
			 * if this is the last slave, kill the master also */
			if (master->slave_count <= 0) 
			{
				hio_dev_kill ((hio_dev_t*)master);
				/* the master pointer is not valid from this point onwards
				 * as the actual master device object is freed in hio_dev_kill() */
			}
			else
			{
				/* this call is initiated by this slave device itself.
				 * if it were by the master device, it would be HIO_NULL as
				 * nullified by the dev_pipe_kill() */
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

static void dev_pipe_fail_before_make_slave (void* ctx)
{
	slave_info_t* si = (slave_info_t*)ctx;
	close (si->pfd);
}

static int dev_pipe_read_slave (hio_dev_t* dev, void* buf, hio_iolen_t* len, hio_devaddr_t* srcaddr)
{
	hio_dev_pipe_slave_t* pipe = (hio_dev_pipe_slave_t*)dev;
	ssize_t x;

	if (HIO_UNLIKELY(pipe->pfd == HIO_SYSHND_INVALID))
	{
		hio_seterrnum (pipe->hio, HIO_EBADHND);
		return -1;
	}

	x = read(pipe->pfd, buf, *len);
	if (x <= -1)
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data available */
		if (errno == EINTR) return 0;
		hio_seterrwithsyserr (pipe->hio, 0, errno);
		return -1;
	}

	*len = x;
	return 1;
}

static int dev_pipe_write_slave (hio_dev_t* dev, const void* data, hio_iolen_t* len, const hio_devaddr_t* dstaddr)
{
	hio_dev_pipe_slave_t* pipe = (hio_dev_pipe_slave_t*)dev;
	ssize_t x;

	if (HIO_UNLIKELY(pipe->pfd == HIO_SYSHND_INVALID))
	{
		hio_seterrnum (pipe->hio, HIO_EBADHND);
		return -1;
	}

	if (HIO_UNLIKELY(*len <= 0))
	{
		/* this is an EOF indicator */
		/*hio_dev_halt (dev);*/ /* halt this slave device to indicate EOF on the lower-level handle */
		if (HIO_LIKELY(pipe->pfd != HIO_SYSHND_INVALID)) /* halt() doesn't close the pipe immediately. so close the underlying pipe */
		{
			hio_dev_watch (dev, HIO_DEV_WATCH_STOP, 0);
			close (pipe->pfd);
			pipe->pfd = HIO_SYSHND_INVALID;
		}
		return 1; /* indicate that the operation got successful. the core will execute on_write() with 0. */
	}

	x = write(pipe->pfd, data, *len);
	if (x <= -1)
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data can be written */
		if (errno == EINTR) return 0;
		hio_seterrwithsyserr (pipe->hio, 0, errno);
		return -1;
	}

	*len = x;
	return 1;
}

static int dev_pipe_writev_slave (hio_dev_t* dev, const hio_iovec_t* iov, hio_iolen_t* iovcnt, const hio_devaddr_t* dstaddr)
{
	hio_dev_pipe_slave_t* pipe = (hio_dev_pipe_slave_t*)dev;
	ssize_t x;

	if (HIO_UNLIKELY(pipe->pfd == HIO_SYSHND_INVALID))
	{
		hio_seterrnum (pipe->hio, HIO_EBADHND);
		return -1;
	}

	if (HIO_UNLIKELY(*iovcnt <= 0))
	{
		/* this is an EOF indicator */
		/*hio_dev_halt (dev);*/ /* halt this slave device to indicate EOF on the lower-level handle  */
		if (HIO_LIKELY(pipe->pfd != HIO_SYSHND_INVALID)) /* halt() doesn't close the pipe immediately. so close the underlying pipe */
		{
			hio_dev_watch (dev, HIO_DEV_WATCH_STOP, 0);
			close (pipe->pfd);
			pipe->pfd = HIO_SYSHND_INVALID;
		}
		return 1; /* indicate that the operation got successful. the core will execute on_write() with 0. */
	}

	x = writev(pipe->pfd, iov, *iovcnt);
	if (x <= -1)
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data can be written */
		if (errno == EINTR) return 0;
		hio_seterrwithsyserr (pipe->hio, 0, errno);
		return -1;
	}

	*iovcnt = x;
	return 1;
}

static hio_syshnd_t dev_pipe_getsyshnd (hio_dev_t* dev)
{
	return HIO_SYSHND_INVALID;
}

static hio_syshnd_t dev_pipe_getsyshnd_slave (hio_dev_t* dev)
{
	hio_dev_pipe_slave_t* pipe = (hio_dev_pipe_slave_t*)dev;
	return (hio_syshnd_t)pipe->pfd;
}

static int dev_pipe_ioctl (hio_dev_t* dev, int cmd, void* arg)
{
	hio_t* hio = dev->hio;
	hio_dev_pipe_t* rdev = (hio_dev_pipe_t*)dev;

	switch (cmd)
	{
		case HIO_DEV_PIPE_CLOSE:
		{
			hio_dev_pipe_sid_t sid = *(hio_dev_pipe_sid_t*)arg;

			if (sid != HIO_DEV_PIPE_IN && sid != HIO_DEV_PIPE_OUT)
			{
				hio_seterrnum (hio, HIO_EINVAL);
				return -1;
			}

			if (rdev->slave[sid])
			{
				/* unlike dev_pipe_kill_master(), i don't nullify rdev->slave[sid].
				 * so i treat the closing ioctl as if it's a kill request 
				 * initiated by the slave device itself. */
				hio_dev_kill ((hio_dev_t*)rdev->slave[sid]);
			}
			return 0;
		}

		default:
			hio_seterrnum (hio, HIO_EINVAL);
			return -1;
	}
}

static hio_dev_mth_t dev_pipe_methods = 
{
	dev_pipe_make_master,
	dev_pipe_kill_master,
	HIO_NULL,
	dev_pipe_getsyshnd,
	HIO_NULL,
	dev_pipe_ioctl,

	HIO_NULL,
	HIO_NULL,
	HIO_NULL,
	HIO_NULL, /* sendfile */
};

static hio_dev_mth_t dev_pipe_methods_slave =
{
	dev_pipe_make_slave,
	dev_pipe_kill_slave,
	dev_pipe_fail_before_make_slave,
	dev_pipe_getsyshnd_slave,
	HIO_NULL,
	dev_pipe_ioctl,

	dev_pipe_read_slave,
	dev_pipe_write_slave,
	dev_pipe_writev_slave,
	HIO_NULL, /* sendfile */
};

/* ========================================================================= */

static int pipe_ready (hio_dev_t* dev, int events)
{
	/* virtual device. no I/O */
	hio_seterrnum (dev->hio, HIO_EINTERN);
	return -1;
}

static int pipe_on_read (hio_dev_t* dev, const void* data, hio_iolen_t len, const hio_devaddr_t* srcaddr)
{
	/* virtual device. no I/O */
	hio_seterrnum (dev->hio, HIO_EINTERN);
	return -1;
}

static int pipe_on_write (hio_dev_t* dev, hio_iolen_t wrlen, void* wrctx, const hio_devaddr_t* dstaddr)
{
	/* virtual device. no I/O */
	hio_seterrnum (dev->hio, HIO_EINTERN);
	return -1;
}

static hio_dev_evcb_t dev_pipe_event_callbacks =
{
	pipe_ready,
	pipe_on_read,
	pipe_on_write
};

/* ========================================================================= */

static int pipe_ready_slave (hio_dev_t* dev, int events)
{
	hio_t* hio = dev->hio;
	/*hio_dev_pipe_t* pipe = (hio_dev_pipe_t*)dev;*/

	if (events & HIO_DEV_EVENT_ERR)
	{
		hio_seterrnum (hio, HIO_EDEVERR);
		return -1;
	}

	if (events & HIO_DEV_EVENT_HUP)
	{
		if (events & (HIO_DEV_EVENT_PRI | HIO_DEV_EVENT_IN | HIO_DEV_EVENT_OUT)) 
		{
			/* pipebably half-open? */
			return 1;
		}

		hio_seterrnum (hio, HIO_EDEVHUP);
		return -1;
	}

	return 1; /* the device is ok. carry on reading or writing */
}

static int pipe_on_read_slave (hio_dev_t* dev, const void* data, hio_iolen_t len, const hio_devaddr_t* srcaddr)
{
	hio_dev_pipe_slave_t* pipe = (hio_dev_pipe_slave_t*)dev;
	return pipe->master->on_read(pipe->master, data, len);
}

static int pipe_on_write_slave (hio_dev_t* dev, hio_iolen_t wrlen, void* wrctx, const hio_devaddr_t* dstaddr)
{
	hio_dev_pipe_slave_t* pipe = (hio_dev_pipe_slave_t*)dev;
	return pipe->master->on_write(pipe->master, wrlen, wrctx);
}

static hio_dev_evcb_t dev_pipe_event_callbacks_slave_in =
{
	pipe_ready_slave,
	pipe_on_read_slave,
	HIO_NULL
};

static hio_dev_evcb_t dev_pipe_event_callbacks_slave_out =
{
	pipe_ready_slave,
	HIO_NULL,
	pipe_on_write_slave
};

/* ========================================================================= */

static hio_dev_pipe_slave_t* make_slave (hio_t* hio, slave_info_t* si)
{
	switch (si->id)
	{
		case HIO_DEV_PIPE_IN:
			return (hio_dev_pipe_slave_t*)hio_dev_make(
				hio, HIO_SIZEOF(hio_dev_pipe_t), 
				&dev_pipe_methods_slave, &dev_pipe_event_callbacks_slave_in, si);

		case HIO_DEV_PIPE_OUT:
			return (hio_dev_pipe_slave_t*)hio_dev_make(
				hio, HIO_SIZEOF(hio_dev_pipe_t), 
				&dev_pipe_methods_slave, &dev_pipe_event_callbacks_slave_out, si);

		default:
			hio_seterrnum (hio, HIO_EINVAL);
			return HIO_NULL;
	}
}

hio_dev_pipe_t* hio_dev_pipe_make (hio_t* hio, hio_oow_t xtnsize, const hio_dev_pipe_make_t* info)
{
	return (hio_dev_pipe_t*)hio_dev_make(
		hio, HIO_SIZEOF(hio_dev_pipe_t) + xtnsize, 
		&dev_pipe_methods, &dev_pipe_event_callbacks, (void*)info);
}

void hio_dev_pipe_kill (hio_dev_pipe_t* dev)
{
	hio_dev_kill ((hio_dev_t*)dev);
}

void hio_dev_pipe_halt (hio_dev_pipe_t* dev)
{
	hio_dev_halt ((hio_dev_t*)dev);
}


int hio_dev_pipe_read (hio_dev_pipe_t* dev, int enabled)
{
	if (dev->slave[HIO_DEV_PIPE_IN])
	{
		return hio_dev_read((hio_dev_t*)dev->slave[HIO_DEV_PIPE_IN], enabled);
	}
	else
	{
		hio_seterrnum (dev->hio, HIO_ENOCAPA); /* TODO: is it the right error number? */
		return -1;
	}
}

int hio_dev_pipe_timedread (hio_dev_pipe_t* dev, int enabled, const hio_ntime_t* tmout)
{
	if (dev->slave[HIO_DEV_PIPE_IN])
	{
		return hio_dev_timedread((hio_dev_t*)dev->slave[HIO_DEV_PIPE_IN], enabled, tmout);
	}
	else
	{
		hio_seterrnum (dev->hio, HIO_ENOCAPA); /* TODO: is it the right error number? */
		return -1;
	}
}

int hio_dev_pipe_write (hio_dev_pipe_t* dev, const void* data, hio_iolen_t dlen, void* wrctx)
{
	if (dev->slave[HIO_DEV_PIPE_OUT])
	{
		return hio_dev_write((hio_dev_t*)dev->slave[HIO_DEV_PIPE_OUT], data, dlen, wrctx, HIO_NULL);
	}
	else
	{
		hio_seterrnum (dev->hio, HIO_ENOCAPA); /* TODO: is it the right error number? */
		return -1;
	}
}

int hio_dev_pipe_timedwrite (hio_dev_pipe_t* dev, const void* data, hio_iolen_t dlen, const hio_ntime_t* tmout, void* wrctx)
{
	if (dev->slave[HIO_DEV_PIPE_OUT])
	{
		return hio_dev_timedwrite((hio_dev_t*)dev->slave[HIO_DEV_PIPE_OUT], data, dlen, tmout, wrctx, HIO_NULL);
	}
	else
	{
		hio_seterrnum (dev->hio, HIO_ENOCAPA); /* TODO: is it the right error number? */
		return -1;
	}
}

int hio_dev_pipe_close (hio_dev_pipe_t* dev, hio_dev_pipe_sid_t sid)
{
	return hio_dev_ioctl((hio_dev_t*)dev, HIO_DEV_PIPE_CLOSE, &sid);
}
