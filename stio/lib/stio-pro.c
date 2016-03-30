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

#include "stio-pro.h"
#include "stio-prv.h"

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

static int pro_make (stio_dev_t* dev, void* ctx)
{
	stio_dev_pro_t* pro = (stio_dev_pro_t*)dev;
	stio_dev_pro_make_t* arg = (stio_dev_pro_make_t*)ctx;
	stio_syshnd_t hnd[6];
	int i, minidx = -1, maxidx = -1;

	if (arg->flags & STIO_DEV_PRO_WRITEIN)
	{
		if (pipe(&hnd[0]) == -1)
		{
			dev->stio->errnum = stio_syserrtoerrnum(errno);
			goto oops;
		}
		minidx = 0; maxidx = 1;
	}

	if (arg->flags & STIO_DEV_PRO_READOUT)
	{
		if (pipe(&hnd[2]) == -1)
		{
			dev->stio->errnum = stio_syserrtoerrnum(errno);
			goto oops;
		}
		if (minidx == -1) minidx = 2;
		maxidx = 3;
	}

	if (arg->flags & STIO_DEV_PRO_READERR)
	{
		if (pipe(&hnd[4]) == -1)
		{
			dev->stio->errnum = stio_syserrtoerrnum(errno);
			goto oops;
		}
		if (minidx == -1) minidx = 4;
		maxidx = 5;
	}

	if (maxidx == -1)
	{
		dev->stio->errnum = STIO_EINVAL;
		goto oops;
	}

	/* TODO: fork and exec... */

	if (arg->flags & STIO_DEV_PRO_WRITEIN)
	{
		/*
		 * 012345
		 * rw----
		 * X
		 * WRITE => 1
		 */
		close (hnd[0]);
		hnd[0] = STIO_SYSHND_INVALID;
	}

	if (arg->flags & STIO_DEV_PRO_READOUT)
	{
		/*
		 * 012345
		 * --rw--
		 *    X
		 * READ => 2
		 */
		close (hnd[3]);
		hnd[3] = STIO_SYSHND_INVALID;
	}

	if (arg->flags & STIO_DEV_PRO_READERR)
	{
		/*
		 * 012345
		 * ----rw
		 *      X
		 * READ => 4
		 */
		close (hnd[5]);
		hnd[5] = STIO_SYSHND_INVALID;
	}

	if (stio_makesyshndasync (pro->stio, hnd[1]) <= -1 ||
	    stio_makesyshndasync (pro->stio, hnd[2]) <= -1 ||
	    stio_makesyshndasync (pro->stio, hnd[4]) <= -1) goto oops;

	return 0;

oops:
	for (i = minidx; i < maxidx; i++)
	{
		if (hnd[i] != STIO_SYSHND_INVALID) close (hnd[i]);
	}

	return -1;
}


static void pro_kill (stio_dev_t* dev)
{
}

static int pro_read (stio_dev_t* dev, void* buf, stio_len_t* len)
{
	stio_dev_pro_t* pro = (stio_dev_pro_t*)dev;
	ssize_t x;

	x = write (pro->pfd, buf, *len);
	if (x <= -1)
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK) return 0;  /* no data available */
		if (errno == EINTR) return 0;
		pro->stio->errnum = stio_syserrtoerrnum(errno);
		return -1;
	}

	*len = x;
	return 1;
}

static int pro_write (stio_dev_t* dev, const void* data, stio_len_t* len)
{
	return -1;
}

static stio_syshnd_t pro_getsyshnd (stio_dev_t* dev)
{
	stio_dev_pro_t* pro = (stio_dev_pro_t*)dev;
	return (stio_syshnd_t)pro->pfd;
}


static int pro_ioctl (stio_dev_t* dev, int cmd, void* arg)
{
	return 0;
}

static stio_dev_mth_t pro_mth = 
{
	pro_make,
	pro_kill,
	pro_getsyshnd,
	pro_read,
	pro_write,
	pro_ioctl
};

static int pro_ready (stio_dev_t* dev, int events)
{
	stio_dev_pro_t* pro = (stio_dev_pro_t*)dev;
printf ("PRO READY...%p\n", dev);

	if (events & STIO_DEV_EVENT_ERR)
	{
		pro->stio->errnum = STIO_EDEVERR;
		return -1;
	}

	if (events & STIO_DEV_EVENT_HUP)
	{
		if (events & (STIO_DEV_EVENT_PRI | STIO_DEV_EVENT_IN | STIO_DEV_EVENT_OUT)) 
		{
			/* probably half-open? */
			return 1;
		}

		pro->stio->errnum = STIO_EDEVHUP;
		return -1;
	}

	return 1; /* the device is ok. carry on reading or writing */
}

static int pro_on_read (stio_dev_t* dev, const void* data, stio_len_t len, const stio_devadr_t* srcadr)
{
	stio_dev_pro_t* pro = (stio_dev_pro_t*)dev;
	return pro->on_read (pro, data, len);
}

static int pro_on_write (stio_dev_t* dev, void* wrctx, const stio_devadr_t* dstadr)
{
	stio_dev_pro_t* pro = (stio_dev_pro_t*)dev;
	return pro->on_write (pro, wrctx);
}

static stio_dev_evcb_t pro_evcb =
{
	pro_ready,
	pro_on_read,
	pro_on_write
};

stio_dev_pro_t* stio_dev_pro_make (stio_t* stio, stio_size_t xtnsize, const stio_dev_pro_make_t* data)
{
	return (stio_dev_pro_t*)stio_makedev (stio, STIO_SIZEOF(stio_dev_pro_t) + xtnsize, &pro_mth, &pro_evcb, (void*)data);
}

void stio_dev_pro_kill (stio_dev_pro_t* pro)
{
	stio_killdev (pro->stio, (stio_dev_t*)pro);
}

#if 0
int stio_dev_pro_write (stio_dev_pro_t* pro, const void* data, stio_len_t len, void* wrctx)
{
	return stio_dev_write ((stio_dev_t*)pro, data, len, wrctx);
}


stio_dev_pro_t* stio_dev_pro_getdev (stio_dev_pro_t* pro, stio_dev_pro_type_t type)
{
	switch (type)
	{
		case STIO_DEV_PRO_IN:
			return XXX;

		case STIO_DEV_PRO_OUT:
			return XXX;

		case STIO_DEV_PRO_ERR:
			return XXX;
	}

	pro->dev->stio = STIO_EINVAL;
	return STIO_NULL;
}
#endif
