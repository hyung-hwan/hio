/*
 * $Id$
 *
    Copyright (c) 2016-2020 Chung, Hyung-Hwan. All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted pipevided that the following conditions
    are met:
    1. Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.
    2. Redistributions in binary form must repipeduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials pipevided with the distribution.

    THIS SOFTWARE IS pipeVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR
    IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WAfRRANTIES
    OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
    IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
    INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
    NOT LIMITED TO, pipeCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR pipeFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
    THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "mio-pipe.h"
#include "mio-prv.h"

#include <unistd.h>
#include <errno.h>
#include <sys/uio.h>

/* ========================================================================= */

struct slave_info_t
{
	mio_dev_pipe_make_t* mi;
	mio_syshnd_t pfd;
	int dev_cap;
	mio_dev_pipe_sid_t id;
};

typedef struct slave_info_t slave_info_t;

static mio_dev_pipe_slave_t* make_slave (mio_t* mio, slave_info_t* si);

/* ========================================================================= */

static int dev_pipe_make_master (mio_dev_t* dev, void* ctx)
{
	mio_t* mio = dev->mio;
	mio_dev_pipe_t* rdev = (mio_dev_pipe_t*)dev;
	mio_dev_pipe_make_t* info = (mio_dev_pipe_make_t*)ctx;
	mio_syshnd_t pfds[2] = { MIO_SYSHND_INVALID, MIO_SYSHND_INVALID };
	slave_info_t si;
	int i;

/* TODO: support a named pipe. use mkfifo()?
 *       support socketpair */
	if (pipe(pfds) == -1)
	{
		mio_seterrwithsyserr (mio, 0, errno);
		goto oops;
	}

	if (mio_makesyshndasync(mio, pfds[0]) <= -1 ||
	    mio_makesyshndasync(mio, pfds[1]) <= -1) goto oops;

	si.mi = info;
	si.pfd = pfds[0];
	si.dev_cap = MIO_DEV_CAP_IN | MIO_DEV_CAP_STREAM;
	si.id = MIO_DEV_PIPE_IN;

	rdev->slave[MIO_DEV_PIPE_IN] = make_slave(mio, &si);
	if (!rdev->slave[MIO_DEV_PIPE_IN]) goto oops;
	rdev->slave_count++;

	si.mi = info;
	si.pfd = pfds[1];
	si.dev_cap = MIO_DEV_CAP_OUT | MIO_DEV_CAP_STREAM;
	si.id = MIO_DEV_PIPE_OUT;

	rdev->slave[MIO_DEV_PIPE_OUT] = make_slave(mio, &si);
	if (!rdev->slave[MIO_DEV_PIPE_OUT]) goto oops;
	rdev->slave_count++;

	for (i = 0; i < MIO_COUNTOF(rdev->slave); i++) 
	{
		if (rdev->slave[i]) rdev->slave[i]->master = rdev;
	}

	rdev->dev_cap = MIO_DEV_CAP_VIRTUAL; /* the master device doesn't perform I/O */
	rdev->on_read = info->on_read;
	rdev->on_write = info->on_write;
	rdev->on_close = info->on_close;
	return 0;

oops:
	for (i = 0; i < MIO_COUNTOF(rdev->slave); i++)
	{
		if (rdev->slave[i])
		{
			mio_dev_kill ((mio_dev_t*)rdev->slave[i]);
			rdev->slave[i] = MIO_NULL;
		}
		else if (pfds[i] != MIO_SYSHND_INVALID) 
		{
			close (pfds[i]);
		}
	}
	rdev->slave_count = 0;

	return -1;
}

static int dev_pipe_make_slave (mio_dev_t* dev, void* ctx)
{
	mio_dev_pipe_slave_t* rdev = (mio_dev_pipe_slave_t*)dev;
	slave_info_t* si = (slave_info_t*)ctx;

	rdev->dev_cap = si->dev_cap;
	rdev->id = si->id;
	rdev->pfd = si->pfd;
	/* keep rdev->master to MIO_NULL. it's set to the right master
	 * device in dev_pipe_make() */

	return 0;
}

static int dev_pipe_kill_master (mio_dev_t* dev, int force)
{
	/*mio_t* mio = dev->mio;*/
	mio_dev_pipe_t* rdev = (mio_dev_pipe_t*)dev;
	int i;

	if (rdev->slave_count > 0)
	{
		for (i = 0; i < MIO_COUNTOF(rdev->slave); i++)
		{
			if (rdev->slave[i])
			{
				mio_dev_pipe_slave_t* sdev = rdev->slave[i];

				/* nullify the pointer to the slave device
				 * before calling mio_dev_kill() on the slave device.
				 * the slave device can check this pointer to tell from
				 * self-initiated termination or master-driven termination */
				rdev->slave[i] = MIO_NULL;

				mio_dev_kill ((mio_dev_t*)sdev);
			}
		}
	}

	if (rdev->on_close) rdev->on_close (rdev, MIO_DEV_PIPE_MASTER);
	return 0;
}

static int dev_pipe_kill_slave (mio_dev_t* dev, int force)
{
	mio_t* mio = dev->mio;
	mio_dev_pipe_slave_t* rdev = (mio_dev_pipe_slave_t*)dev;

	if (rdev->master)
	{
		mio_dev_pipe_t* master;

		master = rdev->master;
		rdev->master = MIO_NULL;

		/* indicate EOF */
		if (master->on_close) master->on_close (master, rdev->id);

		MIO_ASSERT (mio, master->slave_count > 0);
		master->slave_count--;

		if (master->slave[rdev->id])
		{
			/* this call is started by the slave device itself.
			 * if this is the last slave, kill the master also */
			if (master->slave_count <= 0) 
			{
				mio_dev_kill ((mio_dev_t*)master);
				/* the master pointer is not valid from this point onwards
				 * as the actual master device object is freed in mio_dev_kill() */
			}
			else
			{
				/* this call is initiated by this slave device itself.
				 * if it were by the master device, it would be MIO_NULL as
				 * nullified by the dev_pipe_kill() */
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

static int dev_pipe_read_slave (mio_dev_t* dev, void* buf, mio_iolen_t* len, mio_devaddr_t* srcaddr)
{
	mio_dev_pipe_slave_t* pipe = (mio_dev_pipe_slave_t*)dev;
	ssize_t x;

	if (MIO_UNLIKELY(pipe->pfd == MIO_SYSHND_INVALID))
	{
		mio_seterrnum (pipe->mio, MIO_EBADHND);
		return -1;
	}

	x = read(pipe->pfd, buf, *len);
	if (x <= -1)
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data available */
		if (errno == EINTR) return 0;
		mio_seterrwithsyserr (pipe->mio, 0, errno);
		return -1;
	}

	*len = x;
	return 1;
}

static int dev_pipe_write_slave (mio_dev_t* dev, const void* data, mio_iolen_t* len, const mio_devaddr_t* dstaddr)
{
	mio_dev_pipe_slave_t* pipe = (mio_dev_pipe_slave_t*)dev;
	ssize_t x;

	if (MIO_UNLIKELY(pipe->pfd == MIO_SYSHND_INVALID))
	{
		mio_seterrnum (pipe->mio, MIO_EBADHND);
		return -1;
	}

	if (MIO_UNLIKELY(*len <= 0))
	{
		/* this is an EOF indicator */
		//mio_dev_halt (dev); /* halt this slave device to indicate EOF on the lower-level handle */*
		if (MIO_LIKELY(pipe->pfd != MIO_SYSHND_INVALID)) /* halt() doesn't close the pipe immediately. so close the underlying pipe */
		{
			mio_dev_watch (dev, MIO_DEV_WATCH_STOP, 0);
			close (pipe->pfd);
			pipe->pfd = MIO_SYSHND_INVALID;
		}
		return 1; /* indicate that the operation got successful. the core will execute on_write() with 0. */
	}

	x = write(pipe->pfd, data, *len);
	if (x <= -1)
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data can be written */
		if (errno == EINTR) return 0;
		mio_seterrwithsyserr (pipe->mio, 0, errno);
		return -1;
	}

	*len = x;
	return 1;
}

static int dev_pipe_writev_slave (mio_dev_t* dev, const mio_iovec_t* iov, mio_iolen_t* iovcnt, const mio_devaddr_t* dstaddr)
{
	mio_dev_pipe_slave_t* pipe = (mio_dev_pipe_slave_t*)dev;
	ssize_t x;

	if (MIO_UNLIKELY(pipe->pfd == MIO_SYSHND_INVALID))
	{
		mio_seterrnum (pipe->mio, MIO_EBADHND);
		return -1;
	}

	if (MIO_UNLIKELY(*iovcnt <= 0))
	{
		/* this is an EOF indicator */
		/*mio_dev_halt (dev);*/ /* halt this slave device to indicate EOF on the lower-level handle  */
		if (MIO_LIKELY(pipe->pfd != MIO_SYSHND_INVALID)) /* halt() doesn't close the pipe immediately. so close the underlying pipe */
		{
			mio_dev_watch (dev, MIO_DEV_WATCH_STOP, 0);
			close (pipe->pfd);
			pipe->pfd = MIO_SYSHND_INVALID;
		}
		return 1; /* indicate that the operation got successful. the core will execute on_write() with 0. */
	}

	x = writev(pipe->pfd, iov, *iovcnt);
	if (x <= -1)
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data can be written */
		if (errno == EINTR) return 0;
		mio_seterrwithsyserr (pipe->mio, 0, errno);
		return -1;
	}

	*iovcnt = x;
	return 1;
}

static mio_syshnd_t dev_pipe_getsyshnd (mio_dev_t* dev)
{
	return MIO_SYSHND_INVALID;
}

static mio_syshnd_t dev_pipe_getsyshnd_slave (mio_dev_t* dev)
{
	mio_dev_pipe_slave_t* pipe = (mio_dev_pipe_slave_t*)dev;
	return (mio_syshnd_t)pipe->pfd;
}

static int dev_pipe_ioctl (mio_dev_t* dev, int cmd, void* arg)
{
	mio_t* mio = dev->mio;
	mio_dev_pipe_t* rdev = (mio_dev_pipe_t*)dev;

	switch (cmd)
	{
		case MIO_DEV_PIPE_CLOSE:
		{
			mio_dev_pipe_sid_t sid = *(mio_dev_pipe_sid_t*)arg;

			if (sid != MIO_DEV_PIPE_IN && sid != MIO_DEV_PIPE_OUT)
			{
				mio_seterrnum (mio, MIO_EINVAL);
				return -1;
			}

			if (rdev->slave[sid])
			{
				/* unlike dev_pipe_kill_master(), i don't nullify rdev->slave[sid].
				 * so i treat the closing ioctl as if it's a kill request 
				 * initiated by the slave device itself. */
				mio_dev_kill ((mio_dev_t*)rdev->slave[sid]);
			}
			return 0;
		}

		default:
			mio_seterrnum (mio, MIO_EINVAL);
			return -1;
	}
}

static mio_dev_mth_t dev_pipe_methods = 
{
	dev_pipe_make_master,
	dev_pipe_kill_master,
	dev_pipe_getsyshnd,

	MIO_NULL,
	MIO_NULL,
	MIO_NULL,
	dev_pipe_ioctl
};

static mio_dev_mth_t dev_pipe_methods_slave =
{
	dev_pipe_make_slave,
	dev_pipe_kill_slave,
	dev_pipe_getsyshnd_slave,

	dev_pipe_read_slave,
	dev_pipe_write_slave,
	dev_pipe_writev_slave,
	dev_pipe_ioctl
};

/* ========================================================================= */

static int pipe_ready (mio_dev_t* dev, int events)
{
	/* virtual device. no I/O */
	mio_seterrnum (dev->mio, MIO_EINTERN);
	return -1;
}

static int pipe_on_read (mio_dev_t* dev, const void* data, mio_iolen_t len, const mio_devaddr_t* srcaddr)
{
	/* virtual device. no I/O */
	mio_seterrnum (dev->mio, MIO_EINTERN);
	return -1;
}

static int pipe_on_write (mio_dev_t* dev, mio_iolen_t wrlen, void* wrctx, const mio_devaddr_t* dstaddr)
{
	/* virtual device. no I/O */
	mio_seterrnum (dev->mio, MIO_EINTERN);
	return -1;
}

static mio_dev_evcb_t dev_pipe_event_callbacks =
{
	pipe_ready,
	pipe_on_read,
	pipe_on_write
};

/* ========================================================================= */

static int pipe_ready_slave (mio_dev_t* dev, int events)
{
	mio_t* mio = dev->mio;
	/*mio_dev_pipe_t* pipe = (mio_dev_pipe_t*)dev;*/

	if (events & MIO_DEV_EVENT_ERR)
	{
		mio_seterrnum (mio, MIO_EDEVERR);
		return -1;
	}

	if (events & MIO_DEV_EVENT_HUP)
	{
		if (events & (MIO_DEV_EVENT_PRI | MIO_DEV_EVENT_IN | MIO_DEV_EVENT_OUT)) 
		{
			/* pipebably half-open? */
			return 1;
		}

		mio_seterrnum (mio, MIO_EDEVHUP);
		return -1;
	}

	return 1; /* the device is ok. carry on reading or writing */
}

static int pipe_on_read_slave (mio_dev_t* dev, const void* data, mio_iolen_t len, const mio_devaddr_t* srcaddr)
{
	mio_dev_pipe_slave_t* pipe = (mio_dev_pipe_slave_t*)dev;
	return pipe->master->on_read(pipe->master, data, len);
}

static int pipe_on_write_slave (mio_dev_t* dev, mio_iolen_t wrlen, void* wrctx, const mio_devaddr_t* dstaddr)
{
	mio_dev_pipe_slave_t* pipe = (mio_dev_pipe_slave_t*)dev;
	return pipe->master->on_write(pipe->master, wrlen, wrctx);
}

static mio_dev_evcb_t dev_pipe_event_callbacks_slave_in =
{
	pipe_ready_slave,
	pipe_on_read_slave,
	MIO_NULL
};

static mio_dev_evcb_t dev_pipe_event_callbacks_slave_out =
{
	pipe_ready_slave,
	MIO_NULL,
	pipe_on_write_slave
};

/* ========================================================================= */

static mio_dev_pipe_slave_t* make_slave (mio_t* mio, slave_info_t* si)
{
	switch (si->id)
	{
		case MIO_DEV_PIPE_IN:
			return (mio_dev_pipe_slave_t*)mio_dev_make(
				mio, MIO_SIZEOF(mio_dev_pipe_t), 
				&dev_pipe_methods_slave, &dev_pipe_event_callbacks_slave_in, si);

		case MIO_DEV_PIPE_OUT:
			return (mio_dev_pipe_slave_t*)mio_dev_make(
				mio, MIO_SIZEOF(mio_dev_pipe_t), 
				&dev_pipe_methods_slave, &dev_pipe_event_callbacks_slave_out, si);

		default:
			mio_seterrnum (mio, MIO_EINVAL);
			return MIO_NULL;
	}
}

mio_dev_pipe_t* mio_dev_pipe_make (mio_t* mio, mio_oow_t xtnsize, const mio_dev_pipe_make_t* info)
{
	return (mio_dev_pipe_t*)mio_dev_make(
		mio, MIO_SIZEOF(mio_dev_pipe_t) + xtnsize, 
		&dev_pipe_methods, &dev_pipe_event_callbacks, (void*)info);
}

void mio_dev_pipe_kill (mio_dev_pipe_t* dev)
{
	mio_dev_kill ((mio_dev_t*)dev);
}

void mio_dev_pipe_halt (mio_dev_pipe_t* dev)
{
	mio_dev_halt ((mio_dev_t*)dev);
}


int mio_dev_pipe_read (mio_dev_pipe_t* dev, int enabled)
{
	if (dev->slave[MIO_DEV_PIPE_IN])
	{
		return mio_dev_read((mio_dev_t*)dev->slave[MIO_DEV_PIPE_IN], enabled);
	}
	else
	{
		mio_seterrnum (dev->mio, MIO_ENOCAPA); /* TODO: is it the right error number? */
		return -1;
	}
}

int mio_dev_pipe_timedread (mio_dev_pipe_t* dev, int enabled, const mio_ntime_t* tmout)
{
	if (dev->slave[MIO_DEV_PIPE_IN])
	{
		return mio_dev_timedread((mio_dev_t*)dev->slave[MIO_DEV_PIPE_IN], enabled, tmout);
	}
	else
	{
		mio_seterrnum (dev->mio, MIO_ENOCAPA); /* TODO: is it the right error number? */
		return -1;
	}
}

int mio_dev_pipe_write (mio_dev_pipe_t* dev, const void* data, mio_iolen_t dlen, void* wrctx)
{
	if (dev->slave[MIO_DEV_PIPE_OUT])
	{
		return mio_dev_write((mio_dev_t*)dev->slave[MIO_DEV_PIPE_OUT], data, dlen, wrctx, MIO_NULL);
	}
	else
	{
		mio_seterrnum (dev->mio, MIO_ENOCAPA); /* TODO: is it the right error number? */
		return -1;
	}
}

int mio_dev_pipe_timedwrite (mio_dev_pipe_t* dev, const void* data, mio_iolen_t dlen, const mio_ntime_t* tmout, void* wrctx)
{
	if (dev->slave[MIO_DEV_PIPE_OUT])
	{
		return mio_dev_timedwrite((mio_dev_t*)dev->slave[MIO_DEV_PIPE_OUT], data, dlen, tmout, wrctx, MIO_NULL);
	}
	else
	{
		mio_seterrnum (dev->mio, MIO_ENOCAPA); /* TODO: is it the right error number? */
		return -1;
	}
}

int mio_dev_pipe_close (mio_dev_pipe_t* dev, mio_dev_pipe_sid_t sid)
{
	return mio_dev_ioctl((mio_dev_t*)dev, MIO_DEV_PIPE_CLOSE, &sid);
}
