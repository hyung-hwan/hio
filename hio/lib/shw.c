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

#include <hio-shw.h>
#include "hio-prv.h"

#include <unistd.h>
#include <errno.h>
#include <sys/uio.h>

static int dev_shw_make (hio_dev_t* dev, void* ctx)
{
	hio_t* hio = dev->hio;
	hio_dev_shw_t* rdev = (hio_dev_shw_t*)dev;
	hio_dev_shw_make_t* info = (hio_dev_shw_make_t*)ctx;

	rdev->hnd = info->hnd;
	rdev->flags = info->flags;

	rdev->dev_cap = HIO_DEV_CAP_OUT | HIO_DEV_CAP_IN | HIO_DEV_CAP_STREAM;
	if (info->flags & HIO_DEV_SHW_DISABLE_OUT) rdev->dev_cap &= ~HIO_DEV_CAP_OUT;
	if (info->flags & HIO_DEV_SHW_DISABLE_IN) rdev->dev_cap &= ~HIO_DEV_CAP_IN;
	if (info->flags & HIO_DEV_SHW_DISABLE_STREAM) rdev->dev_cap &= ~HIO_DEV_CAP_STREAM;

	rdev->on_ready = info->on_ready;
	rdev->on_read = info->on_read;
	rdev->on_write = info->on_write;
	rdev->on_close = info->on_close;
	return 0;

oops:
	/* don't close the handle regardless of HIO_DEV_SHW_KEEP_OPEN_ON_CLOSE.
	 * the device is not created. so no ownership of the handle is passed to this wrapper device */
	return -1;
}

static int dev_shw_kill (hio_dev_t* dev, int force)
{
	hio_t* hio = dev->hio;
	hio_dev_shw_t* rdev = (hio_dev_shw_t*)dev;

	if (rdev->on_close) rdev->on_close (rdev);

	if (rdev->hnd != HIO_SYSHND_INVALID)
	{
		if (!(rdev->flags & HIO_DEV_SHW_KEEP_OPEN_ON_CLOSE))
		{
			close (rdev->hnd);
			rdev->hnd = HIO_SYSHND_INVALID;
		}
	}
	return 0;
}

static int dev_shw_read (hio_dev_t* dev, void* buf, hio_iolen_t* len, hio_devaddr_t* srcaddr)
{
	hio_dev_shw_t* shw = (hio_dev_shw_t*)dev;
	ssize_t x;

	if (HIO_UNLIKELY(shw->hnd == HIO_SYSHND_INVALID))
	{
		hio_seterrnum (shw->hio, HIO_EBADHND);
		return -1;
	}

	x = read(shw->hnd, buf, *len);
	if (x <= -1)
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data available */
		if (errno == EINTR) return 0;
		hio_seterrwithsyserr (shw->hio, 0, errno);
		return -1;
	}

	*len = x;
	return 1;
}

static int dev_shw_write (hio_dev_t* dev, const void* data, hio_iolen_t* len, const hio_devaddr_t* dstaddr)
{
	hio_dev_shw_t* shw = (hio_dev_shw_t*)dev;
	ssize_t x;

	if (HIO_UNLIKELY(shw->hnd == HIO_SYSHND_INVALID))
	{
		hio_seterrnum (shw->hio, HIO_EBADHND);
		return -1;
	}

	if (HIO_UNLIKELY(*len <= 0))
	{
		/* this is an EOF indicator */
		/*hio_dev_halt (dev);*/ /* halt this slave device to indicate EOF on the lower-level handle */
		if (HIO_LIKELY(shw->hnd != HIO_SYSHND_INVALID)) /* halt() doesn't close the handle immediately. so close the underlying handle */
		{
			hio_dev_watch (dev, HIO_DEV_WATCH_STOP, 0);
			if (!(shw->flags & HIO_DEV_SHW_KEEP_OPEN_ON_CLOSE))
			{
				close (shw->hnd);
				shw->hnd = HIO_SYSHND_INVALID;
			}
		}
		return 1; /* indicate that the operation got successful. the core will execute on_write() with 0. */
	}

	x = write(shw->hnd, data, *len);
	if (x <= -1)
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data can be written */
		if (errno == EINTR) return 0;
		hio_seterrwithsyserr (shw->hio, 0, errno);
		return -1;
	}

	*len = x;
	return 1;
}

static int dev_shw_writev (hio_dev_t* dev, const hio_iovec_t* iov, hio_iolen_t* iovcnt, const hio_devaddr_t* dstaddr)
{
	hio_dev_shw_t* shw = (hio_dev_shw_t*)dev;
	ssize_t x;

	if (HIO_UNLIKELY(shw->hnd == HIO_SYSHND_INVALID))
	{
		hio_seterrnum (shw->hio, HIO_EBADHND);
		return -1;
	}

	if (HIO_UNLIKELY(*iovcnt <= 0))
	{
		/* this is an EOF indicator */
		/*hio_dev_halt (dev);*/ /* halt this slave device to indicate EOF on the lower-level handle  */
		if (HIO_LIKELY(shw->hnd != HIO_SYSHND_INVALID)) /* halt() doesn't close the handle immediately. so close the underlying handle */
		{
			hio_dev_watch (dev, HIO_DEV_WATCH_STOP, 0);
			if (!(shw->flags & HIO_DEV_SHW_KEEP_OPEN_ON_CLOSE))
			{
				close (shw->hnd);
				shw->hnd = HIO_SYSHND_INVALID;
			}
		}
		return 1; /* indicate that the operation got successful. the core will execute on_write() with 0. */
	}

	x = writev(shw->hnd, iov, *iovcnt);
	if (x <= -1)
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data can be written */
		if (errno == EINTR) return 0;
		hio_seterrwithsyserr (shw->hio, 0, errno);
		return -1;
	}

	*iovcnt = x;
	return 1;
}

static hio_syshnd_t dev_shw_getsyshnd (hio_dev_t* dev)
{
	hio_dev_shw_t* rdev = (hio_dev_shw_t*)dev;
	return rdev->hnd;
}

static hio_dev_mth_t dev_shw_methods = 
{
	dev_shw_make,
	dev_shw_kill,
	HIO_NULL,
	dev_shw_getsyshnd,
	HIO_NULL,
	HIO_NULL, /* ioctl */

	dev_shw_read,
	dev_shw_write,
	dev_shw_writev,
	HIO_NULL, /* sendfile */
};

/* ========================================================================= */

static int shw_ready (hio_dev_t* dev, int events)
{
	hio_t* hio = dev->hio;
	hio_dev_shw_t* shw = (hio_dev_shw_t*)dev;

	if (events & HIO_DEV_EVENT_ERR)
	{
		hio_seterrnum (hio, HIO_EDEVERR);
		return -1;
	}

	if (events & HIO_DEV_EVENT_HUP)
	{
		if (events & (HIO_DEV_EVENT_PRI | HIO_DEV_EVENT_IN | HIO_DEV_EVENT_OUT)) 
		{
			/* probably half-open? */
			return shw->on_ready? shw->on_ready(shw, events): 1; 
		}

		hio_seterrnum (hio, HIO_EDEVHUP);
		return -1;
	}

	/* 1 - the device is ok. carry on reading or writing */
	return shw->on_ready? shw->on_ready(shw, events): 1;
}

static int shw_on_read (hio_dev_t* dev, const void* data, hio_iolen_t len, const hio_devaddr_t* srcaddr)
{
	hio_dev_shw_t* shw = (hio_dev_shw_t*)dev;
	return shw->on_read(shw, data, len);
}

static int shw_on_write (hio_dev_t* dev, hio_iolen_t wrlen, void* wrctx, const hio_devaddr_t* dstaddr)
{
	hio_dev_shw_t* shw = (hio_dev_shw_t*)dev;
	return shw->on_write(shw, wrlen, wrctx);
}


static hio_dev_evcb_t dev_shw_event_callbacks =
{
	shw_ready,
	shw_on_read,
	shw_on_write
};

/* ========================================================================= */

hio_dev_shw_t* hio_dev_shw_make (hio_t* hio, hio_oow_t xtnsize, const hio_dev_shw_make_t* info)
{
	return (hio_dev_shw_t*)hio_dev_make(
		hio, HIO_SIZEOF(hio_dev_shw_t) + xtnsize, 
		&dev_shw_methods, &dev_shw_event_callbacks, (void*)info);
}

void hio_dev_shw_kill (hio_dev_shw_t* dev)
{
	hio_dev_kill ((hio_dev_t*)dev);
}

void hio_dev_shw_halt (hio_dev_shw_t* dev)
{
	hio_dev_halt ((hio_dev_t*)dev);
}

int hio_dev_shw_read (hio_dev_shw_t* dev, int enabled)
{
	return hio_dev_read((hio_dev_t*)dev, enabled);
}

int hio_dev_shw_timedread (hio_dev_shw_t* dev, int enabled, const hio_ntime_t* tmout)
{
	return hio_dev_timedread((hio_dev_t*)dev, enabled, tmout);
}

int hio_dev_shw_write (hio_dev_shw_t* dev, const void* data, hio_iolen_t dlen, void* wrctx)
{
	return hio_dev_write((hio_dev_t*)dev, data, dlen, wrctx, HIO_NULL);
}

int hio_dev_shw_timedwrite (hio_dev_shw_t* dev, const void* data, hio_iolen_t dlen, const hio_ntime_t* tmout, void* wrctx)
{
	return hio_dev_timedwrite((hio_dev_t*)dev, data, dlen, tmout, wrctx, HIO_NULL);
}
