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
#include <sys/wait.h>

/* ========================================================================= */

struct slave_info_t
{
	stio_dev_pro_make_t* mi;
	stio_syshnd_t pfd;
	int dev_capa;
	int id;
};

typedef struct slave_info_t slave_info_t;

static stio_dev_pro_slave_t* make_slave (stio_t* stio, slave_info_t* si);

/* ========================================================================= */

struct param_t
{
	stio_mchar_t* mcmd;
	stio_mchar_t* fixed_argv[4];
	stio_mchar_t** argv;
};
typedef struct param_t param_t;

static void free_param (stio_t* stio, param_t* param)
{
	if (param->argv && param->argv != param->fixed_argv) 
		STIO_MMGR_FREE (stio->mmgr, param->argv);
	if (param->mcmd) STIO_MMGR_FREE (stio->mmgr, param->mcmd);
	STIO_MEMSET (param, 0, STIO_SIZEOF(*param));
}

static int make_param (stio_t* stio, const stio_mchar_t* cmd, int flags, param_t* param)
{
	int fcnt = 0;
	stio_mchar_t* mcmd = STIO_NULL;

	STIO_MEMSET (param, 0, STIO_SIZEOF(*param));

	if (flags & STIO_DEV_PRO_SHELL)
	{
		mcmd = (stio_mchar_t*)cmd;

		param->argv = param->fixed_argv;
		param->argv[0] = STIO_MT("/bin/sh");
		param->argv[1] = STIO_MT("-c");
		param->argv[2] = mcmd;
		param->argv[3] = STIO_NULL;
	}
	else
	{
		int i;
		stio_mchar_t** argv;
		stio_mchar_t* mcmdptr;

		mcmd = stio_mbsdup (stio, cmd);
		if (!mcmd) goto oops;
		
		fcnt = stio_mbsspl (mcmd, STIO_MT(""), STIO_MT('\"'), STIO_MT('\"'), STIO_MT('\\')); 
		if (fcnt <= 0) 
		{
			/* no field or an error */
			stio->errnum = STIO_EINVAL;
			goto oops;
		}

		if (fcnt < STIO_COUNTOF(param->fixed_argv))
		{
			param->argv = param->fixed_argv;
		}
		else
		{
			param->argv = STIO_MMGR_ALLOC (stio->mmgr, (fcnt + 1) * STIO_SIZEOF(argv[0]));
			if (param->argv == STIO_NULL) 
			{
				stio->errnum = STIO_ENOMEM;
				goto oops;
			}
		}

		mcmdptr = mcmd;
		for (i = 0; i < fcnt; i++)
		{
			param->argv[i] = mcmdptr;
			while (*mcmdptr != STIO_MT('\0')) mcmdptr++;
			mcmdptr++;
		}
		param->argv[i] = STIO_NULL;
	}

	if (mcmd && mcmd != (stio_mchar_t*)cmd) param->mcmd = mcmd;
	return 0;

oops:
	if (mcmd && mcmd != cmd) STIO_MMGR_FREE (stio->mmgr, mcmd);
	return -1;
}

static pid_t standard_fork_and_exec (stio_t* stio, int pfds[], int flags, param_t* param)
{
	pid_t pid;

	pid = fork ();
	if (pid == -1) 
	{
		stio->errnum = stio_syserrtoerrnum(errno);
		return -1;
	}

	if (pid == 0)
	{
		/* slave process */

		stio_syshnd_t devnull = STIO_SYSHND_INVALID;

/* TODO: close all uneeded fds */

		if (flags & STIO_DEV_PRO_WRITEIN)
		{
			/* slave should read */
			close (pfds[1]);
			pfds[1] = STIO_SYSHND_INVALID;

			/* let the pipe be standard input */
			if (dup2 (pfds[0], 0) <= -1) goto slave_oops;

			close (pfds[0]);
			pfds[0] = STIO_SYSHND_INVALID;
		}

		if (flags & STIO_DEV_PRO_READOUT)
		{
			/* slave should write */
			close (pfds[2]);
			pfds[2] = STIO_SYSHND_INVALID;

			if (dup2(pfds[3], 1) == -1) goto slave_oops;

			if (flags & STIO_DEV_PRO_ERRTOOUT)
			{
				if (dup2(pfds[3], 2) == -1) goto slave_oops;
			}

			close (pfds[3]);
			pfds[3] = STIO_SYSHND_INVALID;
		}

		if (flags & STIO_DEV_PRO_READERR)
		{
			close (pfds[4]);
			pfds[4] = STIO_SYSHND_INVALID;

			if (dup2(pfds[5], 2) == -1) goto slave_oops;

			if (flags & STIO_DEV_PRO_OUTTOERR)
			{
				if (dup2(pfds[5], 1) == -1) goto slave_oops;
			}

			close (pfds[5]);
			pfds[5] = STIO_SYSHND_INVALID;
		}

		if ((flags & STIO_DEV_PRO_INTONUL) ||
		    (flags & STIO_DEV_PRO_OUTTONUL) ||
		    (flags & STIO_DEV_PRO_ERRTONUL))
		{
		#if defined(O_LARGEFILE)
			devnull = open (STIO_MT("/dev/null"), O_RDWR | O_LARGEFILE, 0);
		#else
			devnull = open (STIO_MT("/dev/null"), O_RDWR, 0);
		#endif
			if (devnull == STIO_SYSHND_INVALID) goto slave_oops;
		}

		execv (param->argv[0], param->argv);

		/* if exec fails, free 'param' parameter which is an inherited pointer */
		free_param (stio, param); 

	slave_oops:
		if (devnull != STIO_SYSHND_INVALID) close(devnull);
		_exit (128);
	}

	/* parent process */
	return pid;
}

static int dev_pro_make (stio_dev_t* dev, void* ctx)
{
	stio_dev_pro_t* rdev = (stio_dev_pro_t*)dev;
	stio_dev_pro_make_t* info = (stio_dev_pro_make_t*)ctx;
	stio_syshnd_t pfds[6];
	int i, minidx = -1, maxidx = -1;
	param_t param;
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

	if (make_param (rdev->stio, info->cmd, info->flags, &param) <= -1) goto oops;

/* TODO: more advanced fork and exec .. */
	pid = standard_fork_and_exec (rdev->stio, pfds, info->flags, &param);
	if (pid <= -1) 
	{
		free_param (rdev->stio, &param);
		goto oops;
	}

	free_param (rdev->stio, &param);
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

		if (stio_makesyshndasync (dev->stio, pfds[1]) <= -1) goto oops;
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

		if (stio_makesyshndasync (dev->stio, pfds[2]) <= -1) goto oops;
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

		if (stio_makesyshndasync (dev->stio, pfds[4]) <= -1) goto oops;
	}

	if (pfds[1] != STIO_SYSHND_INVALID)
	{
		/* hand over pfds[2] to the first slave device */
		slave_info_t si;

		si.mi = info;
		si.pfd = pfds[1];
		si.dev_capa = STIO_DEV_CAPA_OUT | STIO_DEV_CAPA_OUT_QUEUED | STIO_DEV_CAPA_STREAM;
		si.id = 0;

		rdev->slave[0] = make_slave (dev->stio, &si);
		if (!rdev->slave[0]) goto oops;

		pfds[1] = STIO_SYSHND_INVALID;
	}

	if (pfds[2] != STIO_SYSHND_INVALID)
	{
		/* hand over pfds[2] to the first slave device */
		slave_info_t si;

		si.mi = info;
		si.pfd = pfds[2];
		si.dev_capa = STIO_DEV_CAPA_IN | STIO_DEV_CAPA_STREAM;
		si.id = 1;

		rdev->slave[1] = make_slave (dev->stio, &si);
		if (!rdev->slave[1]) goto oops;

		pfds[2] = STIO_SYSHND_INVALID;
	}

	if (pfds[4] != STIO_SYSHND_INVALID)
	{
		/* hand over pfds[4] to the second slave device */
		slave_info_t si;

		si.mi = info;
		si.pfd = pfds[4];
		si.dev_capa = STIO_DEV_CAPA_IN | STIO_DEV_CAPA_STREAM;
		si.id = 2;

		rdev->slave[2] = make_slave (dev->stio, &si);
		if (!rdev->slave[2]) goto oops;

		pfds[4] = STIO_SYSHND_INVALID;
	}

	for (i = 0; i < 3; i++) 
	{
		if (rdev->slave[i]) rdev->slave[i]->master = rdev;
	}

	rdev->dev_capa = STIO_DEV_CAPA_VIRTUAL;
	rdev->on_read = info->on_read;
	rdev->on_write = info->on_write;
	return 0;

oops:
	for (i = minidx; i < maxidx; i++)
	{
		if (pfds[i] != STIO_SYSHND_INVALID) close (pfds[i]);
	}

	if (rdev->mcmd) 
	{
		STIO_MMGR_FREE (rdev->stio->mmgr, rdev->mcmd);
		free_param (rdev->stio, &param);
	}

	for (i = 3; i > 0; )
	{
		i--;
		if (rdev->slave[i])
		{
			stio_killdev (rdev->stio, (stio_dev_t*)rdev->slave[i]);
			rdev->slave[i] = STIO_NULL;
		}
	}

	return -1;
}

static int dev_pro_make_slave (stio_dev_t* dev, void* ctx)
{
	stio_dev_pro_slave_t* rdev = (stio_dev_pro_slave_t*)dev;
	slave_info_t* si = (slave_info_t*)ctx;

	rdev->dev_capa = si->dev_capa;
	rdev->id = si->id;
	rdev->pfd = si->pfd;
	/* keep rdev->master to STIO_NULL. it's set to the right master
	 * device in dev_pro_make() */

	return 0;
}

static void dev_pro_kill (stio_dev_t* dev)
{
	stio_dev_pro_t* rdev = (stio_dev_pro_t*)dev;
	int i, status;

	for (i = 0; i < 3; i++)
	{
		if (rdev->slave[i])
		{
			stio_dev_pro_slave_t* sdev = rdev->slave[i];
			rdev->slave[i] = STIO_NULL;
			stio_killdev (rdev->stio, (stio_dev_t*)sdev);
		}
	}

#if 0
	x = waitpid (rdev->child_pid, &status, WNOHANG);
	if (x == rdev->child_pid)
	{
		/* child process reclaimed successfully */
	}
	else if (x == 0)
	{
		/* child still alive */
		/* TODO: schedule a timer job... */
	}
	else
	{
		kill (rdev->child_pid, SIGKILL);
		x = waitpid (rdev->child_pid, &i, WNOHANG);
		if (x == -1)
	}
#endif
}

static void dev_pro_kill_slave (stio_dev_t* dev)
{
	stio_dev_pro_slave_t* rdev = (stio_dev_pro_slave_t*)dev;

	if (rdev->pfd != STIO_SYSHND_INVALID)
	{
		close (rdev->pfd);
		rdev->pfd = STIO_SYSHND_INVALID;
	}

	if (rdev->master)
	{
		/* let the master know that this slave device has been killed */
		rdev->master->slave[rdev->id] = STIO_NULL;
		rdev->master = STIO_NULL; 
	}
}

static int dev_pro_read_slave (stio_dev_t* dev, void* buf, stio_iolen_t* len, stio_devadr_t* srcadr)
{
	stio_dev_pro_slave_t* pro = (stio_dev_pro_slave_t*)dev;
	ssize_t x;

	x = read (pro->pfd, buf, *len);
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

static int dev_pro_write_slave (stio_dev_t* dev, const void* data, stio_iolen_t* len, const stio_devadr_t* dstadr)
{
	stio_dev_pro_slave_t* pro = (stio_dev_pro_slave_t*)dev;
	ssize_t x;

	x = write (pro->pfd, data, *len);
	if (x <= -1)
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK) return 0;  /* no data can be written */
		if (errno == EINTR) return 0;
		pro->stio->errnum = stio_syserrtoerrnum(errno);
		return -1;
	}

	*len = x;
	return 1;
}

static stio_syshnd_t dev_pro_getsyshnd (stio_dev_t* dev)
{
	return STIO_SYSHND_INVALID;
}

static stio_syshnd_t dev_pro_getsyshnd_slave (stio_dev_t* dev)
{
	stio_dev_pro_slave_t* pro = (stio_dev_pro_slave_t*)dev;
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
		case STIO_DEV_PRO_CLOSEERR:
		case STIO_DEV_PRO_WAIT:
	}
*/

	return 0;
}

static stio_dev_mth_t dev_pro_methods = 
{
	dev_pro_make,
	dev_pro_kill,
	dev_pro_getsyshnd,

	STIO_NULL,
	STIO_NULL,
	dev_pro_ioctl
};

static stio_dev_mth_t dev_pro_methods_slave =
{
	dev_pro_make_slave,
	dev_pro_kill_slave,
	dev_pro_getsyshnd_slave,

	dev_pro_read_slave,
	dev_pro_write_slave,
	dev_pro_ioctl
};

/* ========================================================================= */

static int pro_ready (stio_dev_t* dev, int events)
{
	/* virtual device. no I/O */
	dev->stio->errnum = STIO_EINTERN;
	return -1;
}

static int pro_on_read (stio_dev_t* dev, const void* data, stio_iolen_t len, const stio_devadr_t* srcadr)
{
	/* virtual device. no I/O */
	dev->stio->errnum = STIO_EINTERN;
	return -1;
}

static int pro_on_write (stio_dev_t* dev, stio_iolen_t wrlen, void* wrctx, const stio_devadr_t* dstadr)
{
	/* virtual device. no I/O */
	dev->stio->errnum = STIO_EINTERN;
	return -1;
}

static stio_dev_evcb_t dev_pro_event_callbacks =
{
	pro_ready,
	pro_on_read,
	pro_on_write
};

/* ========================================================================= */

static int pro_ready_slave (stio_dev_t* dev, int events)
{
	stio_dev_pro_t* pro = (stio_dev_pro_t*)dev;

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

static int pro_on_read_slave (stio_dev_t* dev, const void* data, stio_iolen_t len, const stio_devadr_t* srcadr)
{
	stio_dev_pro_slave_t* pro = (stio_dev_pro_slave_t*)dev;
	return pro->master->on_read (pro->master, data, len);
}

static int pro_on_write_slave (stio_dev_t* dev, stio_iolen_t wrlen, void* wrctx, const stio_devadr_t* dstadr)
{
	stio_dev_pro_slave_t* pro = (stio_dev_pro_slave_t*)dev;
	return pro->master->on_write (pro->master, wrlen, wrctx);
}

static stio_dev_evcb_t dev_pro_event_callbacks_slave =
{
	pro_ready_slave,
	pro_on_read_slave,
	pro_on_write_slave
};

/* ========================================================================= */

static stio_dev_pro_slave_t* make_slave (stio_t* stio, slave_info_t* si)
{
	return (stio_dev_pro_slave_t*)stio_makedev (
		stio, STIO_SIZEOF(stio_dev_pro_t), 
		&dev_pro_methods_slave, &dev_pro_event_callbacks_slave, si);

}

stio_dev_pro_t* stio_dev_pro_make (stio_t* stio, stio_size_t xtnsize, const stio_dev_pro_make_t* info)
{
	return (stio_dev_pro_t*)stio_makedev (
		stio, STIO_SIZEOF(stio_dev_pro_t) + xtnsize, 
		&dev_pro_methods, &dev_pro_event_callbacks, (void*)info);
}

void stio_dev_pro_kill (stio_dev_pro_t* dev)
{
	stio_killdev (dev->stio, (stio_dev_t*)dev);
}

int stio_dev_pro_write (stio_dev_pro_t* dev, const void* data, stio_iolen_t dlen, void* wrctx)
{
	if (dev->slave[0])
	{
		return stio_dev_write ((stio_dev_t*)dev->slave[0], data, dlen, wrctx, STIO_NULL);
	}
	else
	{
		dev->stio->errnum = STIO_ENOCAPA; /* TODO: is it the right error number? */
		return -1;
	}
}

int stio_dev_pro_timedwrite (stio_dev_pro_t* dev, const void* data, stio_iolen_t dlen, const stio_ntime_t* tmout, void* wrctx)
{
	if (dev->slave[0])
	{
		return stio_dev_timedwrite ((stio_dev_t*)dev->slave[0], data, dlen, tmout, wrctx, STIO_NULL);
	}
	else
	{
		dev->stio->errnum = STIO_ENOCAPA; /* TODO: is it the right error number? */
		return -1;
	}
}

#if 0
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
