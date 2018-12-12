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
	stio_dev_pro_sid_t id;
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

static int dev_pro_make_master (stio_dev_t* dev, void* ctx)
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
		si.id = STIO_DEV_PRO_IN;

		rdev->slave[STIO_DEV_PRO_IN] = make_slave (dev->stio, &si);
		if (!rdev->slave[STIO_DEV_PRO_IN]) goto oops;

		pfds[1] = STIO_SYSHND_INVALID;
		rdev->slave_count++;
	}

	if (pfds[2] != STIO_SYSHND_INVALID)
	{
		/* hand over pfds[2] to the first slave device */
		slave_info_t si;

		si.mi = info;
		si.pfd = pfds[2];
		si.dev_capa = STIO_DEV_CAPA_IN | STIO_DEV_CAPA_STREAM;
		si.id = STIO_DEV_PRO_OUT;

		rdev->slave[STIO_DEV_PRO_OUT] = make_slave (dev->stio, &si);
		if (!rdev->slave[STIO_DEV_PRO_OUT]) goto oops;

		pfds[2] = STIO_SYSHND_INVALID;
		rdev->slave_count++;
	}

	if (pfds[4] != STIO_SYSHND_INVALID)
	{
		/* hand over pfds[4] to the second slave device */
		slave_info_t si;

		si.mi = info;
		si.pfd = pfds[4];
		si.dev_capa = STIO_DEV_CAPA_IN | STIO_DEV_CAPA_STREAM;
		si.id = STIO_DEV_PRO_ERR;

		rdev->slave[STIO_DEV_PRO_ERR] = make_slave (dev->stio, &si);
		if (!rdev->slave[STIO_DEV_PRO_ERR]) goto oops;

		pfds[4] = STIO_SYSHND_INVALID;
		rdev->slave_count++;
	}

	for (i = 0; i < STIO_COUNTOF(rdev->slave); i++) 
	{
		if (rdev->slave[i]) rdev->slave[i]->master = rdev;
	}

	rdev->dev_capa = STIO_DEV_CAPA_VIRTUAL; /* the master device doesn't perform I/O */
	rdev->flags = info->flags;
	rdev->on_read = info->on_read;
	rdev->on_write = info->on_write;
	rdev->on_close = info->on_close;
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

	for (i = STIO_COUNTOF(rdev->slave); i > 0; )
	{
		i--;
		if (rdev->slave[i])
		{
			stio_killdev (rdev->stio, (stio_dev_t*)rdev->slave[i]);
			rdev->slave[i] = STIO_NULL;
		}
	}
	rdev->slave_count = 0;

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

static int dev_pro_kill_master (stio_dev_t* dev, int force)
{
	stio_dev_pro_t* rdev = (stio_dev_pro_t*)dev;
	int i, status;
	pid_t wpid;

	if (rdev->slave_count > 0)
	{
		for (i = 0; i < STIO_COUNTOF(rdev->slave); i++)
		{
			if (rdev->slave[i])
			{
				stio_dev_pro_slave_t* sdev = rdev->slave[i];

				/* nullify the pointer to the slave device
				 * before calling stio_killdev() on the slave device.
				 * the slave device can check this pointer to tell from
				 * self-initiated termination or master-driven termination */
				rdev->slave[i] = STIO_NULL;

				stio_killdev (rdev->stio, (stio_dev_t*)sdev);
			}
		}
	}

	if (rdev->child_pid >= 0)
	{
		if (!(rdev->flags & STIO_DEV_PRO_FORGET_CHILD))
		{
			int killed = 0;

		await_child:
			wpid = waitpid (rdev->child_pid, &status, WNOHANG);
			if (wpid == 0)
			{
				if (force && !killed)
				{
					if (!(rdev->flags & STIO_DEV_PRO_FORGET_DIEHARD_CHILD))
					{
						kill (rdev->child_pid, SIGKILL);
						killed = 1;
						goto await_child;
					}
				}
				else
				{
					/* child process is still alive */
					rdev->stio->errnum = STIO_EAGAIN;
					return -1;  /* call me again */
				}
			}

			/* wpid == rdev->child_pid => full success
			 * wpid == -1 && errno == ECHILD => no such process. it's waitpid()'ed by some other part of the program?
			 * other cases ==> can't really handle properly. forget it by returning success
			 * no need not worry about EINTR because errno can't have the value when WNOHANG is set.
			 */
		}

printf (">>>>>>>>>>>>>>>>>>> REAPED CHILD %d\n", (int)rdev->child_pid);
		rdev->child_pid = -1;
	}

	if (rdev->on_close) rdev->on_close (rdev, STIO_DEV_PRO_MASTER);
	return 0;
}

static int dev_pro_kill_slave (stio_dev_t* dev, int force)
{
	stio_dev_pro_slave_t* rdev = (stio_dev_pro_slave_t*)dev;

	if (rdev->master)
	{
		stio_dev_pro_t* master;

		master = rdev->master;
		rdev->master = STIO_NULL;

		/* indicate EOF */
		if (master->on_close) master->on_close (master, rdev->id);

		STIO_ASSERT (master->slave_count > 0);
		master->slave_count--;

		if (master->slave[rdev->id])
		{
			/* this call is started by the slave device itself.
			 * if this is the last slave, kill the master also */
			if (master->slave_count <= 0) 
			{
				stio_killdev (rdev->stio, (stio_dev_t*)master);
				/* the master pointer is not valid from this point onwards
				 * as the actual master device object is freed in stio_killdev() */
			}
		}
		else
		{
			/* this call is initiated by this slave device itself.
			 * if it were by the master device, it would be STIO_NULL as
			 * nullified by the dev_pro_kill() */
			master->slave[rdev->id] = STIO_NULL;
		}
	}

	if (rdev->pfd != STIO_SYSHND_INVALID)
	{
		close (rdev->pfd);
		rdev->pfd = STIO_SYSHND_INVALID;
	}

	return 0;
}

static int dev_pro_read_slave (stio_dev_t* dev, void* buf, stio_iolen_t* len, stio_devaddr_t* srcaddr)
{
	stio_dev_pro_slave_t* pro = (stio_dev_pro_slave_t*)dev;
	ssize_t x;

	x = read (pro->pfd, buf, *len);
	if (x <= -1)
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data available */
		if (errno == EINTR) return 0;
		pro->stio->errnum = stio_syserrtoerrnum(errno);
		return -1;
	}

	*len = x;
	return 1;
}

static int dev_pro_write_slave (stio_dev_t* dev, const void* data, stio_iolen_t* len, const stio_devaddr_t* dstaddr)
{
	stio_dev_pro_slave_t* pro = (stio_dev_pro_slave_t*)dev;
	ssize_t x;

	x = write (pro->pfd, data, *len);
	if (x <= -1)
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data can be written */
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
	stio_dev_pro_t* rdev = (stio_dev_pro_t*)dev;

	switch (cmd)
	{
		case STIO_DEV_PRO_CLOSE:
		{
			stio_dev_pro_sid_t sid = *(stio_dev_pro_sid_t*)arg;

			if (sid < STIO_DEV_PRO_IN || sid > STIO_DEV_PRO_ERR)
			{
				rdev->stio->errnum = STIO_EINVAL;
				return -1;
			}

			if (rdev->slave[sid])
			{
				/* unlike dev_pro_kill_master(), i don't nullify rdev->slave[sid].
				 * so i treat the closing ioctl as if it's a kill request 
				 * initiated by the slave device itself. */
				stio_killdev (rdev->stio, (stio_dev_t*)rdev->slave[sid]);
			}
			return 0;
		}

		case STIO_DEV_PRO_KILL_CHILD:
			if (rdev->child_pid >= 0)
			{
				if (kill (rdev->child_pid, SIGKILL) == -1)
				{
					rdev->stio->errnum = stio_syserrtoerrnum(errno);
					return -1;
				}
			}

			return 0;

		default:
			dev->stio->errnum = STIO_EINVAL;
			return -1;
	}
}

static stio_dev_mth_t dev_pro_methods = 
{
	dev_pro_make_master,
	dev_pro_kill_master,
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

static int pro_on_read (stio_dev_t* dev, const void* data, stio_iolen_t len, const stio_devaddr_t* srcaddr)
{
	/* virtual device. no I/O */
	dev->stio->errnum = STIO_EINTERN;
	return -1;
}

static int pro_on_write (stio_dev_t* dev, stio_iolen_t wrlen, void* wrctx, const stio_devaddr_t* dstaddr)
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


static int pro_on_read_slave_out (stio_dev_t* dev, const void* data, stio_iolen_t len, const stio_devaddr_t* srcaddr)
{
	stio_dev_pro_slave_t* pro = (stio_dev_pro_slave_t*)dev;
	return pro->master->on_read (pro->master, data, len, STIO_DEV_PRO_OUT);
}

static int pro_on_read_slave_err (stio_dev_t* dev, const void* data, stio_iolen_t len, const stio_devaddr_t* srcaddr)
{
	stio_dev_pro_slave_t* pro = (stio_dev_pro_slave_t*)dev;
	return pro->master->on_read (pro->master, data, len, STIO_DEV_PRO_ERR);
}

static int pro_on_write_slave (stio_dev_t* dev, stio_iolen_t wrlen, void* wrctx, const stio_devaddr_t* dstaddr)
{
	stio_dev_pro_slave_t* pro = (stio_dev_pro_slave_t*)dev;
	return pro->master->on_write (pro->master, wrlen, wrctx);
}

static stio_dev_evcb_t dev_pro_event_callbacks_slave_in =
{
	pro_ready_slave,
	STIO_NULL,
	pro_on_write_slave
};

static stio_dev_evcb_t dev_pro_event_callbacks_slave_out =
{
	pro_ready_slave,
	pro_on_read_slave_out,
	STIO_NULL
};

static stio_dev_evcb_t dev_pro_event_callbacks_slave_err =
{
	pro_ready_slave,
	pro_on_read_slave_err,
	STIO_NULL
};

/* ========================================================================= */

static stio_dev_pro_slave_t* make_slave (stio_t* stio, slave_info_t* si)
{
	switch (si->id)
	{
		case STIO_DEV_PRO_IN:
			return (stio_dev_pro_slave_t*)stio_makedev (
				stio, STIO_SIZEOF(stio_dev_pro_t), 
				&dev_pro_methods_slave, &dev_pro_event_callbacks_slave_in, si);

		case STIO_DEV_PRO_OUT:
			return (stio_dev_pro_slave_t*)stio_makedev (
				stio, STIO_SIZEOF(stio_dev_pro_t), 
				&dev_pro_methods_slave, &dev_pro_event_callbacks_slave_out, si);

		case STIO_DEV_PRO_ERR:
			return (stio_dev_pro_slave_t*)stio_makedev (
				stio, STIO_SIZEOF(stio_dev_pro_t), 
				&dev_pro_methods_slave, &dev_pro_event_callbacks_slave_err, si);

		default:
			stio->errnum = STIO_EINVAL;
			return STIO_NULL;
	}
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

int stio_dev_pro_close (stio_dev_pro_t* dev, stio_dev_pro_sid_t sid)
{
	return stio_dev_ioctl ((stio_dev_t*)dev, STIO_DEV_PRO_CLOSE, &sid);
}

int stio_dev_pro_killchild (stio_dev_pro_t* dev)
{
	return stio_dev_ioctl ((stio_dev_t*)dev, STIO_DEV_PRO_KILL_CHILD, STIO_NULL);
}

#if 0
stio_dev_pro_t* stio_dev_pro_getdev (stio_dev_pro_t* pro, stio_dev_pro_sid_t sid)
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
