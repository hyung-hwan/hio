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


static stio_dev_pro_t* make_sibling (stio_t* stio, stio_syshnd_t handle);

/* ========================================================================= */

static pid_t standard_fork_and_exec (stio_dev_pro_t* rdev, int pfds[], stio_dev_pro_make_t* info)
{
	pid_t pid;

	pid = fork ();
	if (fork() == -1) 
	{
		rdev->stio->errnum = stio_syserrtoerrnum(errno);
		return -1;
	}

	if (pid == 0)
	{
		stio_syshnd_t devnull = STIO_SYSHND_INVALID;

		/* TODO: close all uneeded fds */
		if (info->flags & STIO_DEV_PRO_WRITEIN)
		{
			/* child should read */
			close (pfds[1]);
			pfds[1] = STIO_SYSHND_INVALID;

			/* let the pipe be standard input */
			if (dup2 (pfds[0], 0) <= -1) goto child_oops;

			close (pfds[0]);
			pfds[0] = STIO_SYSHND_INVALID;
		}

		if (info->flags & STIO_DEV_PRO_READOUT)
		{
			/* child should write */
			close (pfds[2]);
			pfds[2] = STIO_SYSHND_INVALID;

			if (dup2(pfds[3], 1) == -1) goto child_oops;

			if (info->flags & STIO_DEV_PRO_ERRTOOUT)
			{
				if (dup2(pfds[3], 2) == -1) goto child_oops;
			}

			close (pfds[3]);
			pfds[3] = STIO_SYSHND_INVALID;
		}

		if (info->flags & STIO_DEV_PRO_READERR)
		{
			close (pfds[4]);
			pfds[4] = STIO_SYSHND_INVALID;

			if (dup2(pfds[5], 2) == -1) goto child_oops;

			if (info->flags & STIO_DEV_PRO_OUTTOERR)
			{
				if (dup2(pfds[5], 1) == -1) goto child_oops;
			}

			close (pfds[5]);
			pfds[5] = STIO_SYSHND_INVALID;
		}

		if ((info->flags & STIO_DEV_PRO_INTONUL) ||
		    (info->flags & STIO_DEV_PRO_OUTTONUL) ||
		    (info->flags & STIO_DEV_PRO_ERRTONUL))
		{
		#if defined(O_LARGEFILE)
			devnull = open ("/dev/null", O_RDWR | O_LARGEFILE, 0);
		#else
			devnull = open ("/dev/null", O_RDWR, 0);
		#endif
			if (devnull == STIO_SYSHND_INVALID) goto child_oops;
		}

		//execv (param->argv[0], param->argv);

	child_oops:
		if (devnull != STIO_SYSHND_INVALID) close(devnull);
		_exit (128);
	}

	return pid;

}

static int dev_pro_make (stio_dev_t* dev, void* ctx)
{
	stio_dev_pro_t* rdev = (stio_dev_pro_t*)dev;
	stio_dev_pro_make_t* info = (stio_dev_pro_make_t*)ctx;
	stio_syshnd_t pfds[6];
	int i, minidx = -1, maxidx = -1;
	pid_t pid;

	if (info->flags & STIO_DEV_PRO_WRITEIN)
	{
		if (pipe(&pfds[0]) == -1)
		{
			dev->stio->errnum = stio_syserrtoerrnum(errno);
			goto oops;
		}
		minidx = 0; maxidx = 1;
	}

	if (info->flags & STIO_DEV_PRO_READOUT)
	{
		if (pipe(&pfds[2]) == -1)
		{
			dev->stio->errnum = stio_syserrtoerrnum(errno);
			goto oops;
		}
		if (minidx == -1) minidx = 2;
		maxidx = 3;
	}

	if (info->flags & STIO_DEV_PRO_READERR)
	{
		if (pipe(&pfds[4]) == -1)
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

/* TODO: more advanced fork and exec .. */
	pid = standard_fork_and_exec (rdev, pfds, info);
	if (pid <= -1) goto oops;

	rdev->child_pid = pid;

	/* this is the parent process */
	if (info->flags & STIO_DEV_PRO_WRITEIN)
	{
		/*
		 * 012345
		 * rw----
		 * X
		 * WRITE => 1
		 */
		close (pfds[0]);
		pfds[0] = STIO_SYSHND_INVALID;
	}

	if (info->flags & STIO_DEV_PRO_READOUT)
	{
		/*
		 * 012345
		 * --rw--
		 *    X
		 * READ => 2
		 */
		close (pfds[3]);
		pfds[3] = STIO_SYSHND_INVALID;
	}

	if (info->flags & STIO_DEV_PRO_READERR)
	{
		/*
		 * 012345
		 * ----rw
		 *      X
		 * READ => 4
		 */
		close (pfds[5]);
		pfds[5] = STIO_SYSHND_INVALID;
	}

	if (stio_makesyshndasync (dev->stio, pfds[1]) <= -1 ||
	    stio_makesyshndasync (dev->stio, pfds[2]) <= -1 ||
	    stio_makesyshndasync (dev->stio, pfds[4]) <= -1) goto oops;

	if (pfds[2] != STIO_SYSHND_INVALID)
	{
		rdev->sibling[0] = make_sibling (dev->stio, pfds[2]);
		if (!rdev->sibling[0]) goto oops;
		pfds[2] = STIO_SYSHND_INVALID;
	}

	if (pfds[4] != STIO_SYSHND_INVALID)
	{
		rdev->sibling[1] = make_sibling (dev->stio, pfds[4]);
		if (!rdev->sibling[1])
		{
			if (rdev->sibling[0])
			{
				stio_dev_pro_kill (rdev->sibling[0]);
				rdev->sibling[0] = STIO_NULL;
			}
			goto oops;
		}
		pfds[4] = STIO_SYSHND_INVALID;
	}

	rdev->pfd = pfds[1];
	return 0;

oops:
	for (i = minidx; i < maxidx; i++)
	{
		if (pfds[i] != STIO_SYSHND_INVALID) close (pfds[i]);
	}

	return -1;
}

static int dev_pro_make_sibling (stio_dev_t* dev, void* ctx)
{
	stio_dev_pro_t* rdev = (stio_dev_pro_t*)dev;
	stio_syshnd_t* handle = (stio_syshnd_t*)ctx;

	rdev->pfd = *handle;
	if (stio_makesyshndasync (rdev->stio, rdev->pfd) <= -1) return -1;

	return 0;
}


static void dev_pro_kill (stio_dev_t* dev)
{
	stio_dev_pro_t* rdev = (stio_dev_pro_t*)dev;

	if (rdev->pfd != STIO_SYSHND_INVALID)
	{
		close (rdev->pfd);
		rdev->pfd = STIO_SYSHND_INVALID;
	}

	if (rdev->sibling[0])
	{
		stio_dev_pro_kill (rdev->sibling[0]);
		rdev->sibling[0] = STIO_NULL;
	}

	if (rdev->sibling[1])
	{
		stio_dev_pro_kill (rdev->sibling[1]);
		rdev->sibling[1] = STIO_NULL;
	}
}

static int dev_pro_read (stio_dev_t* dev, void* buf, stio_iolen_t* len, stio_devadr_t* srcadr)
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

static int dev_pro_write (stio_dev_t* dev, const void* data, stio_iolen_t* len, const stio_devadr_t* dstadr)
{
	return -1;
}

static stio_syshnd_t dev_pro_getsyshnd (stio_dev_t* dev)
{
	stio_dev_pro_t* pro = (stio_dev_pro_t*)dev;
	return (stio_syshnd_t)pro->pfd;
}


static int dev_pro_ioctl (stio_dev_t* dev, int cmd, void* arg)
{

/*
	switch (cmd)
	{
		case STIO_DEV_PRO_KILL
		case STIO_DEV_PRO_CLOSEIN:
		case STIO_DEV_PRO_CLOSEOUT:
		case STIO_DEV_PROC_CLOSEERR: 
	}
*/

	return 0;
}

static stio_dev_mth_t dev_pro_methods = 
{
	dev_pro_make,
	dev_pro_kill,
	dev_pro_getsyshnd,

	dev_pro_read,
	dev_pro_write,
	dev_pro_ioctl
};



static stio_dev_mth_t dev_pro_methods_sibling =
{
	dev_pro_make_sibling,
	dev_pro_kill,
	dev_pro_getsyshnd,

	dev_pro_read,
	dev_pro_write,
	dev_pro_ioctl
};

/* ========================================================================= */


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

static int pro_on_read (stio_dev_t* dev, const void* data, stio_iolen_t len, const stio_devadr_t* srcadr)
{
	stio_dev_pro_t* pro = (stio_dev_pro_t*)dev;
	return pro->on_read (pro, data, len);
}

static int pro_on_write (stio_dev_t* dev, stio_iolen_t wrlen, void* wrctx, const stio_devadr_t* dstadr)
{
	stio_dev_pro_t* pro = (stio_dev_pro_t*)dev;
	return pro->on_write (pro, wrctx);
}

static stio_dev_evcb_t dev_pro_event_callbacks =
{
	pro_ready,
	pro_on_read,
	pro_on_write
};


/* ========================================================================= */

static stio_dev_pro_t* make_sibling (stio_t* stio, stio_syshnd_t handle)
{
	return (stio_dev_pro_t*)stio_makedev (
		stio, STIO_SIZEOF(stio_dev_pro_t), 
		&dev_pro_methods_sibling, &dev_pro_event_callbacks, (void*)&handle);

}

stio_dev_pro_t* stio_dev_pro_make (stio_t* stio, stio_size_t xtnsize, const stio_dev_pro_make_t* data)
{
	return (stio_dev_pro_t*)stio_makedev (
		stio, STIO_SIZEOF(stio_dev_pro_t) + xtnsize, 
		&dev_pro_methods, &dev_pro_event_callbacks, (void*)data);
}

void stio_dev_pro_kill (stio_dev_pro_t* pro)
{
	stio_killdev (pro->stio, (stio_dev_t*)pro);
}

#if 0
int stio_dev_pro_write (stio_dev_pro_t* pro, const void* data, stio_iolen_t len, void* wrctx)
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
