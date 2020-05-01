/*
 * $Id$
 *
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

#include "mio-pro.h"
#include "mio-prv.h"

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/uio.h>

/* ========================================================================= */

struct slave_info_t
{
	mio_dev_pro_make_t* mi;
	mio_syshnd_t pfd;
	int dev_cap;
	mio_dev_pro_sid_t id;
};

typedef struct slave_info_t slave_info_t;

static mio_dev_pro_slave_t* make_slave (mio_t* mio, slave_info_t* si);

/* ========================================================================= */

struct param_t
{
	mio_bch_t* mcmd;
	mio_bch_t* fixed_argv[4];
	mio_bch_t** argv;
};
typedef struct param_t param_t;

static void free_param (mio_t* mio, param_t* param)
{
	if (param->argv && param->argv != param->fixed_argv) 
		mio_freemem (mio, param->argv);
	if (param->mcmd) mio_freemem (mio, param->mcmd);
	MIO_MEMSET (param, 0, MIO_SIZEOF(*param));
}

static int make_param (mio_t* mio, const mio_bch_t* cmd, int flags, param_t* param)
{
	int fcnt = 0;
	mio_bch_t* mcmd = MIO_NULL;

	MIO_MEMSET (param, 0, MIO_SIZEOF(*param));

	if (flags & MIO_DEV_PRO_SHELL)
	{
		mcmd = (mio_bch_t*)cmd;

		param->argv = param->fixed_argv;
		param->argv[0] = "/bin/sh";
		param->argv[1] = "-c";
		param->argv[2] = mcmd;
		param->argv[3] = MIO_NULL;
	}
	else
	{
		int i;
		mio_bch_t** argv;
		mio_bch_t* mcmdptr;

		mcmd = mio_dupbcstr(mio, cmd, MIO_NULL);
		if (!mcmd) goto oops;
		
		fcnt = mio_split_bcstr(mcmd, "", '\"', '\"', '\\'); 
		if (fcnt <= 0) 
		{
			/* no field or an error */
			mio_seterrnum (mio, MIO_EINVAL);
			goto oops;
		}

		if (fcnt < MIO_COUNTOF(param->fixed_argv))
		{
			param->argv = param->fixed_argv;
		}
		else
		{
			param->argv = mio_allocmem(mio, (fcnt + 1) * MIO_SIZEOF(argv[0]));
			if (!param->argv) goto oops;
		}

		mcmdptr = mcmd;
		for (i = 0; i < fcnt; i++)
		{
			param->argv[i] = mcmdptr;
			while (*mcmdptr != '\0') mcmdptr++;
			mcmdptr++;
		}
		param->argv[i] = MIO_NULL;
	}

	if (mcmd && mcmd != (mio_bch_t*)cmd) param->mcmd = mcmd;
	return 0;

oops:
	if (mcmd && mcmd != cmd) mio_freemem (mio, mcmd);
	return -1;
}

static pid_t standard_fork_and_exec (mio_t* mio, int pfds[], int flags, param_t* param)
{
	pid_t pid;

	pid = fork ();
	if (pid == -1) 
	{
		mio_seterrwithsyserr (mio, 0, errno);
		return -1;
	}

	if (pid == 0)
	{
		/* slave process */

		mio_syshnd_t devnull = MIO_SYSHND_INVALID;

/* TODO: close all uneeded fds */

		if (flags & MIO_DEV_PRO_WRITEIN)
		{
			/* slave should read */
			close (pfds[1]);
			pfds[1] = MIO_SYSHND_INVALID;

			/* let the pipe be standard input */
			if (dup2(pfds[0], 0) <= -1) goto slave_oops;

			close (pfds[0]);
			pfds[0] = MIO_SYSHND_INVALID;
		}

		if (flags & MIO_DEV_PRO_READOUT)
		{
			/* slave should write */
			close (pfds[2]);
			pfds[2] = MIO_SYSHND_INVALID;

			if (dup2(pfds[3], 1) == -1) goto slave_oops;

			if (flags & MIO_DEV_PRO_ERRTOOUT)
			{
				if (dup2(pfds[3], 2) == -1) goto slave_oops;
			}

			close (pfds[3]);
			pfds[3] = MIO_SYSHND_INVALID;
		}

		if (flags & MIO_DEV_PRO_READERR)
		{
			close (pfds[4]);
			pfds[4] = MIO_SYSHND_INVALID;

			if (dup2(pfds[5], 2) == -1) goto slave_oops;

			if (flags & MIO_DEV_PRO_OUTTOERR)
			{
				if (dup2(pfds[5], 1) == -1) goto slave_oops;
			}

			close (pfds[5]);
			pfds[5] = MIO_SYSHND_INVALID;
		}

		if ((flags & MIO_DEV_PRO_INTONUL) ||
		    (flags & MIO_DEV_PRO_OUTTONUL) ||
		    (flags & MIO_DEV_PRO_ERRTONUL))
		{
		#if defined(O_LARGEFILE)
			devnull = open ("/dev/null", O_RDWR | O_LARGEFILE, 0);
		#else
			devnull = open ("/dev/null", O_RDWR, 0);
		#endif
			if (devnull == MIO_SYSHND_INVALID) goto slave_oops;
		}

		execv (param->argv[0], param->argv);

		/* if exec fails, free 'param' parameter which is an inherited pointer */
		free_param (mio, param); 

	slave_oops:
		if (devnull != MIO_SYSHND_INVALID) close(devnull);
		_exit (128);
	}

	/* parent process */
	return pid;
}

static int dev_pro_make_master (mio_dev_t* dev, void* ctx)
{
	mio_t* mio = dev->mio;
	mio_dev_pro_t* rdev = (mio_dev_pro_t*)dev;
	mio_dev_pro_make_t* info = (mio_dev_pro_make_t*)ctx;
	mio_syshnd_t pfds[6];
	int i, minidx = -1, maxidx = -1;
	param_t param;
	pid_t pid;

	if (info->flags & MIO_DEV_PRO_WRITEIN)
	{
		if (pipe(&pfds[0]) == -1)
		{
			mio_seterrwithsyserr (mio, 0, errno);
			goto oops;
		}
		minidx = 0; maxidx = 1;
	}

	if (info->flags & MIO_DEV_PRO_READOUT)
	{
		if (pipe(&pfds[2]) == -1)
		{
			mio_seterrwithsyserr (mio, 0, errno);
			goto oops;
		}
		if (minidx == -1) minidx = 2;
		maxidx = 3;
	}

	if (info->flags & MIO_DEV_PRO_READERR)
	{
		if (pipe(&pfds[4]) == -1)
		{
			mio_seterrwithsyserr (mio, 0, errno);
			goto oops;
		}
		if (minidx == -1) minidx = 4;
		maxidx = 5;
	}

	if (maxidx == -1)
	{
		mio_seterrnum (mio, MIO_EINVAL);
		goto oops;
	}

	if (make_param(mio, info->cmd, info->flags, &param) <= -1) goto oops;

/* TODO: more advanced fork and exec .. */
	pid = standard_fork_and_exec(mio, pfds, info->flags, &param);
	if (pid <= -1) 
	{
		free_param (mio, &param);
		goto oops;
	}

	free_param (mio, &param);
	rdev->child_pid = pid;

	/* this is the parent process */
	if (info->flags & MIO_DEV_PRO_WRITEIN)
	{
		/*
		 * 012345
		 * rw----
		 * X
		 * WRITE => 1
		 */
		close (pfds[0]);
		pfds[0] = MIO_SYSHND_INVALID;

		if (mio_makesyshndasync(mio, pfds[1]) <= -1) goto oops;
	}

	if (info->flags & MIO_DEV_PRO_READOUT)
	{
		/*
		 * 012345
		 * --rw--
		 *    X
		 * READ => 2
		 */
		close (pfds[3]);
		pfds[3] = MIO_SYSHND_INVALID;

		if (mio_makesyshndasync(mio, pfds[2]) <= -1) goto oops;
	}

	if (info->flags & MIO_DEV_PRO_READERR)
	{
		/*
		 * 012345
		 * ----rw
		 *      X
		 * READ => 4
		 */
		close (pfds[5]);
		pfds[5] = MIO_SYSHND_INVALID;

		if (mio_makesyshndasync(mio, pfds[4]) <= -1) goto oops;
	}

	if (pfds[1] != MIO_SYSHND_INVALID)
	{
		/* hand over pfds[2] to the first slave device */
		slave_info_t si;

		si.mi = info;
		si.pfd = pfds[1];
		si.dev_cap = MIO_DEV_CAP_OUT | MIO_DEV_CAP_STREAM;
		si.id = MIO_DEV_PRO_IN;

		rdev->slave[MIO_DEV_PRO_IN] = make_slave(mio, &si);
		if (!rdev->slave[MIO_DEV_PRO_IN]) goto oops;

		pfds[1] = MIO_SYSHND_INVALID;
		rdev->slave_count++;
	}

	if (pfds[2] != MIO_SYSHND_INVALID)
	{
		/* hand over pfds[2] to the first slave device */
		slave_info_t si;

		si.mi = info;
		si.pfd = pfds[2];
		si.dev_cap = MIO_DEV_CAP_IN | MIO_DEV_CAP_STREAM;
		si.id = MIO_DEV_PRO_OUT;

		rdev->slave[MIO_DEV_PRO_OUT] = make_slave(mio, &si);
		if (!rdev->slave[MIO_DEV_PRO_OUT]) goto oops;

		pfds[2] = MIO_SYSHND_INVALID;
		rdev->slave_count++;
	}

	if (pfds[4] != MIO_SYSHND_INVALID)
	{
		/* hand over pfds[4] to the second slave device */
		slave_info_t si;

		si.mi = info;
		si.pfd = pfds[4];
		si.dev_cap = MIO_DEV_CAP_IN | MIO_DEV_CAP_STREAM;
		si.id = MIO_DEV_PRO_ERR;

		rdev->slave[MIO_DEV_PRO_ERR] = make_slave(mio, &si);
		if (!rdev->slave[MIO_DEV_PRO_ERR]) goto oops;

		pfds[4] = MIO_SYSHND_INVALID;
		rdev->slave_count++;
	}

	for (i = 0; i < MIO_COUNTOF(rdev->slave); i++) 
	{
		if (rdev->slave[i]) rdev->slave[i]->master = rdev;
	}

	rdev->dev_cap = MIO_DEV_CAP_VIRTUAL; /* the master device doesn't perform I/O */
	rdev->flags = info->flags;
	rdev->on_read = info->on_read;
	rdev->on_write = info->on_write;
	rdev->on_close = info->on_close;
	return 0;

oops:
	for (i = minidx; i < maxidx; i++)
	{
		if (pfds[i] != MIO_SYSHND_INVALID) close (pfds[i]);
	}

	if (rdev->mcmd) 
	{
		mio_freemem (mio, rdev->mcmd);
		free_param (mio, &param);
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

static int dev_pro_make_slave (mio_dev_t* dev, void* ctx)
{
	mio_dev_pro_slave_t* rdev = (mio_dev_pro_slave_t*)dev;
	slave_info_t* si = (slave_info_t*)ctx;

	rdev->dev_cap = si->dev_cap;
	rdev->id = si->id;
	rdev->pfd = si->pfd;
	/* keep rdev->master to MIO_NULL. it's set to the right master
	 * device in dev_pro_make() */

	return 0;
}

static int dev_pro_kill_master (mio_dev_t* dev, int force)
{
	mio_t* mio = dev->mio;
	mio_dev_pro_t* rdev = (mio_dev_pro_t*)dev;
	int i, status;
	pid_t wpid;

	if (rdev->slave_count > 0)
	{
		for (i = 0; i < MIO_COUNTOF(rdev->slave); i++)
		{
			if (rdev->slave[i])
			{
				mio_dev_pro_slave_t* sdev = rdev->slave[i];

				/* nullify the pointer to the slave device
				 * before calling mio_dev_kill() on the slave device.
				 * the slave device can check this pointer to tell from
				 * self-initiated termination or master-driven termination */
				rdev->slave[i] = MIO_NULL;

				mio_dev_kill ((mio_dev_t*)sdev);
			}
		}
	}

	if (rdev->child_pid >= 0)
	{
		if (!(rdev->flags & MIO_DEV_PRO_FORGET_CHILD))
		{
			int killed = 0;

		await_child:
			wpid = waitpid (rdev->child_pid, &status, WNOHANG);
			if (wpid == 0)
			{
				if (force && !killed)
				{
					if (!(rdev->flags & MIO_DEV_PRO_FORGET_DIEHARD_CHILD))
					{
						kill (rdev->child_pid, SIGKILL);
						killed = 1;
						goto await_child;
					}
				}
				else
				{
					/* child process is still alive */
					mio_seterrnum (mio, MIO_EAGAIN);
					return -1;  /* call me again */
				}
			}

			/* wpid == rdev->child_pid => full success
			 * wpid == -1 && errno == ECHILD => no such process. it's waitpid()'ed by some other part of the program?
			 * other cases ==> can't really handle properly. forget it by returning success
			 * no need not worry about EINTR because errno can't have the value when WNOHANG is set.
			 */
		}

		MIO_DEBUG1 (mio, ">>>>>>>>>>>>>>>>>>> REAPED CHILD %d\n", (int)rdev->child_pid);
		rdev->child_pid = -1;
	}

	if (rdev->on_close) rdev->on_close (rdev, MIO_DEV_PRO_MASTER);
	return 0;
}

static int dev_pro_kill_slave (mio_dev_t* dev, int force)
{
	mio_t* mio = dev->mio;
	mio_dev_pro_slave_t* rdev = (mio_dev_pro_slave_t*)dev;

	if (rdev->master)
	{
		mio_dev_pro_t* master;

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
				 * nullified by the dev_pro_kill() */
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

static int dev_pro_read_slave (mio_dev_t* dev, void* buf, mio_iolen_t* len, mio_devaddr_t* srcaddr)
{
	mio_dev_pro_slave_t* pro = (mio_dev_pro_slave_t*)dev;
	ssize_t x;

	x = read(pro->pfd, buf, *len);
	if (x <= -1)
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data available */
		if (errno == EINTR) return 0;
		mio_seterrwithsyserr (pro->mio, 0, errno);
		return -1;
	}

	*len = x;
	return 1;
}

static int dev_pro_write_slave (mio_dev_t* dev, const void* data, mio_iolen_t* len, const mio_devaddr_t* dstaddr)
{
	mio_dev_pro_slave_t* pro = (mio_dev_pro_slave_t*)dev;
	ssize_t x;

	x = write(pro->pfd, data, *len);
	if (x <= -1)
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data can be written */
		if (errno == EINTR) return 0;
		mio_seterrwithsyserr (pro->mio, 0, errno);
		return -1;
	}

	*len = x;
	return 1;
}

static int dev_pro_writev_slave (mio_dev_t* dev, const mio_iovec_t* iov, mio_iolen_t* iovcnt, const mio_devaddr_t* dstaddr)
{
	mio_dev_pro_slave_t* pro = (mio_dev_pro_slave_t*)dev;
	ssize_t x;

	x = writev(pro->pfd, iov, *iovcnt);
	if (x <= -1)
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data can be written */
		if (errno == EINTR) return 0;
		mio_seterrwithsyserr (pro->mio, 0, errno);
		return -1;
	}

	*iovcnt = x;
	return 1;
}

static mio_syshnd_t dev_pro_getsyshnd (mio_dev_t* dev)
{
	return MIO_SYSHND_INVALID;
}

static mio_syshnd_t dev_pro_getsyshnd_slave (mio_dev_t* dev)
{
	mio_dev_pro_slave_t* pro = (mio_dev_pro_slave_t*)dev;
	return (mio_syshnd_t)pro->pfd;
}

static int dev_pro_ioctl (mio_dev_t* dev, int cmd, void* arg)
{
	mio_t* mio = dev->mio;
	mio_dev_pro_t* rdev = (mio_dev_pro_t*)dev;

	switch (cmd)
	{
		case MIO_DEV_PRO_CLOSE:
		{
			mio_dev_pro_sid_t sid = *(mio_dev_pro_sid_t*)arg;

			if (sid < MIO_DEV_PRO_IN || sid > MIO_DEV_PRO_ERR)
			{
				mio_seterrnum (mio, MIO_EINVAL);
				return -1;
			}

			if (rdev->slave[sid])
			{
				/* unlike dev_pro_kill_master(), i don't nullify rdev->slave[sid].
				 * so i treat the closing ioctl as if it's a kill request 
				 * initiated by the slave device itself. */
				mio_dev_kill ((mio_dev_t*)rdev->slave[sid]);
			}
			return 0;
		}

		case MIO_DEV_PRO_KILL_CHILD:
			if (rdev->child_pid >= 0)
			{
				if (kill (rdev->child_pid, SIGKILL) == -1)
				{
					mio_seterrwithsyserr (mio, 0, errno);
					return -1;
				}
			}

			return 0;

		default:
			mio_seterrnum (mio, MIO_EINVAL);
			return -1;
	}
}

static mio_dev_mth_t dev_pro_methods = 
{
	dev_pro_make_master,
	dev_pro_kill_master,
	dev_pro_getsyshnd,

	MIO_NULL,
	MIO_NULL,
	MIO_NULL,
	dev_pro_ioctl
};

static mio_dev_mth_t dev_pro_methods_slave =
{
	dev_pro_make_slave,
	dev_pro_kill_slave,
	dev_pro_getsyshnd_slave,

	dev_pro_read_slave,
	dev_pro_write_slave,
	dev_pro_writev_slave,
	dev_pro_ioctl
};

/* ========================================================================= */

static int pro_ready (mio_dev_t* dev, int events)
{
	/* virtual device. no I/O */
	mio_seterrnum (dev->mio, MIO_EINTERN);
	return -1;
}

static int pro_on_read (mio_dev_t* dev, const void* data, mio_iolen_t len, const mio_devaddr_t* srcaddr)
{
	/* virtual device. no I/O */
	mio_seterrnum (dev->mio, MIO_EINTERN);
	return -1;
}

static int pro_on_write (mio_dev_t* dev, mio_iolen_t wrlen, void* wrctx, const mio_devaddr_t* dstaddr)
{
	/* virtual device. no I/O */
	mio_seterrnum (dev->mio, MIO_EINTERN);
	return -1;
}

static mio_dev_evcb_t dev_pro_event_callbacks =
{
	pro_ready,
	pro_on_read,
	pro_on_write
};

/* ========================================================================= */

static int pro_ready_slave (mio_dev_t* dev, int events)
{
	mio_t* mio = dev->mio;
	/*mio_dev_pro_t* pro = (mio_dev_pro_t*)dev;*/

	if (events & MIO_DEV_EVENT_ERR)
	{
		mio_seterrnum (mio, MIO_EDEVERR);
		return -1;
	}

	if (events & MIO_DEV_EVENT_HUP)
	{
		if (events & (MIO_DEV_EVENT_PRI | MIO_DEV_EVENT_IN | MIO_DEV_EVENT_OUT)) 
		{
			/* probably half-open? */
			return 1;
		}

		mio_seterrnum (mio, MIO_EDEVHUP);
		return -1;
	}

	return 1; /* the device is ok. carry on reading or writing */
}


static int pro_on_read_slave_out (mio_dev_t* dev, const void* data, mio_iolen_t len, const mio_devaddr_t* srcaddr)
{
	mio_dev_pro_slave_t* pro = (mio_dev_pro_slave_t*)dev;
	return pro->master->on_read(pro->master, MIO_DEV_PRO_OUT, data, len);
}

static int pro_on_read_slave_err (mio_dev_t* dev, const void* data, mio_iolen_t len, const mio_devaddr_t* srcaddr)
{
	mio_dev_pro_slave_t* pro = (mio_dev_pro_slave_t*)dev;
	return pro->master->on_read(pro->master, MIO_DEV_PRO_ERR, data, len);
}

static int pro_on_write_slave (mio_dev_t* dev, mio_iolen_t wrlen, void* wrctx, const mio_devaddr_t* dstaddr)
{
	mio_dev_pro_slave_t* pro = (mio_dev_pro_slave_t*)dev;
	return pro->master->on_write(pro->master, wrlen, wrctx);
}

static mio_dev_evcb_t dev_pro_event_callbacks_slave_in =
{
	pro_ready_slave,
	MIO_NULL,
	pro_on_write_slave
};

static mio_dev_evcb_t dev_pro_event_callbacks_slave_out =
{
	pro_ready_slave,
	pro_on_read_slave_out,
	MIO_NULL
};

static mio_dev_evcb_t dev_pro_event_callbacks_slave_err =
{
	pro_ready_slave,
	pro_on_read_slave_err,
	MIO_NULL
};

/* ========================================================================= */

static mio_dev_pro_slave_t* make_slave (mio_t* mio, slave_info_t* si)
{
	switch (si->id)
	{
		case MIO_DEV_PRO_IN:
			return (mio_dev_pro_slave_t*)mio_dev_make(
				mio, MIO_SIZEOF(mio_dev_pro_t), 
				&dev_pro_methods_slave, &dev_pro_event_callbacks_slave_in, si);

		case MIO_DEV_PRO_OUT:
			return (mio_dev_pro_slave_t*)mio_dev_make(
				mio, MIO_SIZEOF(mio_dev_pro_t), 
				&dev_pro_methods_slave, &dev_pro_event_callbacks_slave_out, si);

		case MIO_DEV_PRO_ERR:
			return (mio_dev_pro_slave_t*)mio_dev_make(
				mio, MIO_SIZEOF(mio_dev_pro_t), 
				&dev_pro_methods_slave, &dev_pro_event_callbacks_slave_err, si);

		default:
			mio_seterrnum (mio, MIO_EINVAL);
			return MIO_NULL;
	}
}

mio_dev_pro_t* mio_dev_pro_make (mio_t* mio, mio_oow_t xtnsize, const mio_dev_pro_make_t* info)
{
	return (mio_dev_pro_t*)mio_dev_make(
		mio, MIO_SIZEOF(mio_dev_pro_t) + xtnsize, 
		&dev_pro_methods, &dev_pro_event_callbacks, (void*)info);
}

void mio_dev_pro_kill (mio_dev_pro_t* dev)
{
	mio_dev_kill ((mio_dev_t*)dev);
}

void mio_dev_pro_halt (mio_dev_pro_t* dev)
{
	mio_dev_halt ((mio_dev_t*)dev);
}

int mio_dev_pro_read (mio_dev_pro_t* dev, mio_dev_pro_sid_t sid, int enabled)
{
	mio_t* mio = dev->mio;

	MIO_ASSERT (mio, sid == MIO_DEV_PRO_OUT || sid == MIO_DEV_PRO_ERR);

	if (dev->slave[sid])
	{
		return mio_dev_read((mio_dev_t*)dev->slave[sid], enabled);
	}
	else
	{
		mio_seterrnum (dev->mio, MIO_ENOCAPA); /* TODO: is it the right error number? */
		return -1;
	}
}

int mio_dev_pro_timedread (mio_dev_pro_t* dev, mio_dev_pro_sid_t sid, int enabled, const mio_ntime_t* tmout)
{
	mio_t* mio = dev->mio;

	MIO_ASSERT (mio, sid == MIO_DEV_PRO_OUT || sid == MIO_DEV_PRO_ERR);

	if (dev->slave[sid])
	{
		return mio_dev_timedread((mio_dev_t*)dev->slave[sid], enabled, tmout);
	}
	else
	{
		mio_seterrnum (mio, MIO_ENOCAPA); /* TODO: is it the right error number? */
		return -1;
	}
}
int mio_dev_pro_write (mio_dev_pro_t* dev, const void* data, mio_iolen_t dlen, void* wrctx)
{
	if (dev->slave[MIO_DEV_PRO_IN])
	{
		return mio_dev_write((mio_dev_t*)dev->slave[MIO_DEV_PRO_IN], data, dlen, wrctx, MIO_NULL);
	}
	else
	{
		mio_seterrnum (dev->mio, MIO_ENOCAPA); /* TODO: is it the right error number? */
		return -1;
	}
}

int mio_dev_pro_timedwrite (mio_dev_pro_t* dev, const void* data, mio_iolen_t dlen, const mio_ntime_t* tmout, void* wrctx)
{
	if (dev->slave[MIO_DEV_PRO_IN])
	{
		return mio_dev_timedwrite((mio_dev_t*)dev->slave[MIO_DEV_PRO_IN], data, dlen, tmout, wrctx, MIO_NULL);
	}
	else
	{
		mio_seterrnum (dev->mio, MIO_ENOCAPA); /* TODO: is it the right error number? */
		return -1;
	}
}

int mio_dev_pro_close (mio_dev_pro_t* dev, mio_dev_pro_sid_t sid)
{
	return mio_dev_ioctl((mio_dev_t*)dev, MIO_DEV_PRO_CLOSE, &sid);
}

int mio_dev_pro_killchild (mio_dev_pro_t* dev)
{
	return mio_dev_ioctl((mio_dev_t*)dev, MIO_DEV_PRO_KILL_CHILD, MIO_NULL);
}

#if 0
mio_dev_pro_t* mio_dev_pro_getdev (mio_dev_pro_t* pro, mio_dev_pro_sid_t sid)
{
	switch (type)
	{
		case MIO_DEV_PRO_IN:
			return XXX;

		case MIO_DEV_PRO_OUT:
			return XXX;

		case MIO_DEV_PRO_ERR:
			return XXX;
	}

	pro->dev->mio = MIO_EINVAL;
	return MIO_NULL;
}
#endif
