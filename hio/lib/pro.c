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

#include <hio-pro.h>
#include "hio-prv.h"

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/uio.h>

/* ========================================================================= */

struct slave_info_t
{
	hio_dev_pro_make_t* mi;
	hio_syshnd_t pfd;
	int dev_cap;
	hio_dev_pro_sid_t id;
};

typedef struct slave_info_t slave_info_t;

static hio_dev_pro_slave_t* make_slave (hio_t* hio, slave_info_t* si);

/* ========================================================================= */

struct param_t
{
	hio_bch_t* mcmd;
	hio_bch_t* fixed_argv[4];
	hio_bch_t** argv;
};
typedef struct param_t param_t;

static void free_param (hio_t* hio, param_t* param)
{
	if (param->argv && param->argv != param->fixed_argv) 
		hio_freemem (hio, param->argv);
	if (param->mcmd) hio_freemem (hio, param->mcmd);
	HIO_MEMSET (param, 0, HIO_SIZEOF(*param));
}

static int make_param (hio_t* hio, const hio_bch_t* cmd, int flags, param_t* param)
{
	int fcnt = 0;
	hio_bch_t* mcmd = HIO_NULL;

	HIO_MEMSET (param, 0, HIO_SIZEOF(*param));

	if (flags & HIO_DEV_PRO_SHELL)
	{
		mcmd = (hio_bch_t*)cmd;

		param->argv = param->fixed_argv;
		param->argv[0] = "/bin/sh";
		param->argv[1] = "-c";
		param->argv[2] = mcmd;
		param->argv[3] = HIO_NULL;
	}
	else
	{
		int i;
		hio_bch_t** argv;
		hio_bch_t* mcmdptr;

		mcmd = hio_dupbcstr(hio, cmd, HIO_NULL);
		if (HIO_UNLIKELY(!mcmd)) goto oops;

		fcnt = hio_split_bcstr(mcmd, "", '\"', '\"', '\\'); 
		if (fcnt <= 0) 
		{
			/* no field or an error */
			hio_seterrnum (hio, HIO_EINVAL);
			goto oops;
		}

		if (fcnt < HIO_COUNTOF(param->fixed_argv))
		{
			param->argv = param->fixed_argv;
		}
		else
		{
			param->argv = hio_allocmem(hio, (fcnt + 1) * HIO_SIZEOF(argv[0]));
			if (HIO_UNLIKELY(!param->argv)) goto oops;
		}

		mcmdptr = mcmd;
		for (i = 0; i < fcnt; i++)
		{
			param->argv[i] = mcmdptr;
			while (*mcmdptr != '\0') mcmdptr++;
			mcmdptr++;
		}
		param->argv[i] = HIO_NULL;
	}

	if (mcmd && mcmd != (hio_bch_t*)cmd) param->mcmd = mcmd;
	return 0;

oops:
	if (mcmd && mcmd != cmd) hio_freemem (hio, mcmd);
	return -1;
}

static pid_t standard_fork_and_exec (hio_dev_pro_t* dev, int pfds[], hio_dev_pro_make_t* mi, param_t* param)
{
	hio_t* hio = dev->hio;
	pid_t pid;

	pid = fork();
	if (pid == -1) 
	{
		hio_seterrwithsyserr (hio, 0, errno);
		return -1;
	}

	if (pid == 0)
	{
		/* slave process */

		hio_syshnd_t devnull = HIO_SYSHND_INVALID;

/* TODO: close all uneeded fds */
		if (mi->on_fork) mi->on_fork (dev, mi->fork_ctx);

		if (mi->flags & HIO_DEV_PRO_WRITEIN)
		{
			/* slave should read */
			close (pfds[1]);
			pfds[1] = HIO_SYSHND_INVALID;

			/* let the pipe be standard input */
			if (dup2(pfds[0], 0) <= -1) goto slave_oops;

			close (pfds[0]);
			pfds[0] = HIO_SYSHND_INVALID;
		}

		if (mi->flags & HIO_DEV_PRO_READOUT)
		{
			/* slave should write */
			close (pfds[2]);
			pfds[2] = HIO_SYSHND_INVALID;

			if (dup2(pfds[3], 1) == -1) goto slave_oops;

			if (mi->flags & HIO_DEV_PRO_ERRTOOUT)
			{
				if (dup2(pfds[3], 2) == -1) goto slave_oops;
			}

			close (pfds[3]);
			pfds[3] = HIO_SYSHND_INVALID;
		}

		if (mi->flags & HIO_DEV_PRO_READERR)
		{
			close (pfds[4]);
			pfds[4] = HIO_SYSHND_INVALID;

			if (dup2(pfds[5], 2) == -1) goto slave_oops;

			if (mi->flags & HIO_DEV_PRO_OUTTOERR)
			{
				if (dup2(pfds[5], 1) == -1) goto slave_oops;
			}

			close (pfds[5]);
			pfds[5] = HIO_SYSHND_INVALID;
		}

		if ((mi->flags & HIO_DEV_PRO_INTONUL) ||
		    (mi->flags & HIO_DEV_PRO_OUTTONUL) ||
		    (mi->flags & HIO_DEV_PRO_ERRTONUL))
		{
		#if defined(O_LARGEFILE)
			devnull = open("/dev/null", O_RDWR | O_LARGEFILE, 0);
		#else
			devnull = open("/dev/null", O_RDWR, 0);
		#endif
			if (devnull == HIO_SYSHND_INVALID) goto slave_oops;

			if ((mi->flags & HIO_DEV_PRO_INTONUL) && dup2(devnull, 0) == -1) goto slave_oops;
			if ((mi->flags & HIO_DEV_PRO_OUTTONUL) && dup2(devnull, 1) == -1) goto slave_oops;
			if ((mi->flags & HIO_DEV_PRO_ERRTONUL) && dup2(devnull, 2) == -1) goto slave_oops;

			close (devnull); 
			devnull = HIO_SYSHND_INVALID;
		}

		if (mi->flags & HIO_DEV_PRO_DROPIN) close (0);
		if (mi->flags & HIO_DEV_PRO_DROPOUT) close (1);
		if (mi->flags & HIO_DEV_PRO_DROPERR) close (2);

		execv (param->argv[0], param->argv);

		/* if exec fails, free 'param' parameter which is an inherited pointer */
		free_param (hio, param); 

	slave_oops:
		if (devnull != HIO_SYSHND_INVALID) close(devnull);
		_exit (128);
	}

	/* parent process */
	return pid;
}

static int dev_pro_make_master (hio_dev_t* dev, void* ctx)
{
	hio_t* hio = dev->hio;
	hio_dev_pro_t* rdev = (hio_dev_pro_t*)dev;
	hio_dev_pro_make_t* info = (hio_dev_pro_make_t*)ctx;
	hio_syshnd_t pfds[6] = { HIO_SYSHND_INVALID, HIO_SYSHND_INVALID, HIO_SYSHND_INVALID, HIO_SYSHND_INVALID, HIO_SYSHND_INVALID, HIO_SYSHND_INVALID };
	int i, minidx = -1, maxidx = -1;
	param_t param;
	pid_t pid;

	if (info->flags & HIO_DEV_PRO_WRITEIN)
	{
		if (pipe(&pfds[0]) == -1)
		{
			hio_seterrwithsyserr (hio, 0, errno);
			goto oops;
		}
		minidx = 0; maxidx = 1;
	}

	if (info->flags & HIO_DEV_PRO_READOUT)
	{
		if (pipe(&pfds[2]) == -1)
		{
			hio_seterrwithsyserr (hio, 0, errno);
			goto oops;
		}
		if (minidx == -1) minidx = 2;
		maxidx = 3;
	}

	if (info->flags & HIO_DEV_PRO_READERR)
	{
		if (pipe(&pfds[4]) == -1)
		{
			hio_seterrwithsyserr (hio, 0, errno);
			goto oops;
		}
		if (minidx == -1) minidx = 4;
		maxidx = 5;
	}

	if (maxidx == -1)
	{
		hio_seterrnum (hio, HIO_EINVAL);
		goto oops;
	}

	if (make_param(hio, info->cmd, info->flags, &param) <= -1) goto oops;

/* TODO: more advanced fork and exec .. */
	pid = standard_fork_and_exec(rdev, pfds, info, &param);
	if (pid <= -1) 
	{
		free_param (hio, &param);
		goto oops;
	}

	free_param (hio, &param);
	rdev->child_pid = pid;

	/* this is the parent process */
	if (info->flags & HIO_DEV_PRO_WRITEIN)
	{
		/*
		 * 012345
		 * rw----
		 * X
		 * WRITE => 1
		 */
		close (pfds[0]);
		pfds[0] = HIO_SYSHND_INVALID;

		if (hio_makesyshndasync(hio, pfds[1]) <= -1) goto oops;
	}

	if (info->flags & HIO_DEV_PRO_READOUT)
	{
		/*
		 * 012345
		 * --rw--
		 *    X
		 * READ => 2
		 */
		close (pfds[3]);
		pfds[3] = HIO_SYSHND_INVALID;

		if (hio_makesyshndasync(hio, pfds[2]) <= -1) goto oops;
	}

	if (info->flags & HIO_DEV_PRO_READERR)
	{
		/*
		 * 012345
		 * ----rw
		 *      X
		 * READ => 4
		 */
		close (pfds[5]);
		pfds[5] = HIO_SYSHND_INVALID;

		if (hio_makesyshndasync(hio, pfds[4]) <= -1) goto oops;
	}

	if (pfds[1] != HIO_SYSHND_INVALID)
	{
		/* hand over pfds[2] to the first slave device */
		slave_info_t si;

		si.mi = info;
		si.pfd = pfds[1];
		si.dev_cap = HIO_DEV_CAP_OUT | HIO_DEV_CAP_STREAM;
		si.id = HIO_DEV_PRO_IN;

		pfds[1] = HIO_SYSHND_INVALID;
		rdev->slave[HIO_DEV_PRO_IN] = make_slave(hio, &si);
		if (!rdev->slave[HIO_DEV_PRO_IN]) goto oops;

		rdev->slave_count++;
	}

	if (pfds[2] != HIO_SYSHND_INVALID)
	{
		/* hand over pfds[2] to the first slave device */
		slave_info_t si;

		si.mi = info;
		si.pfd = pfds[2];
		si.dev_cap = HIO_DEV_CAP_IN | HIO_DEV_CAP_STREAM;
		si.id = HIO_DEV_PRO_OUT;

		pfds[2] = HIO_SYSHND_INVALID;
		rdev->slave[HIO_DEV_PRO_OUT] = make_slave(hio, &si);
		if (!rdev->slave[HIO_DEV_PRO_OUT]) goto oops;

		rdev->slave_count++;
	}

	if (pfds[4] != HIO_SYSHND_INVALID)
	{
		/* hand over pfds[4] to the second slave device */
		slave_info_t si;

		si.mi = info;
		si.pfd = pfds[4];
		si.dev_cap = HIO_DEV_CAP_IN | HIO_DEV_CAP_STREAM;
		si.id = HIO_DEV_PRO_ERR;

		pfds[4] = HIO_SYSHND_INVALID;
		rdev->slave[HIO_DEV_PRO_ERR] = make_slave(hio, &si);
		if (!rdev->slave[HIO_DEV_PRO_ERR]) goto oops;

		rdev->slave_count++;
	}

	for (i = 0; i < HIO_COUNTOF(rdev->slave); i++) 
	{
		if (rdev->slave[i]) rdev->slave[i]->master = rdev;
	}

	rdev->dev_cap = HIO_DEV_CAP_VIRTUAL; /* the master device doesn't perform I/O */
	rdev->flags = info->flags;
	rdev->on_read = info->on_read;
	rdev->on_write = info->on_write;
	rdev->on_close = info->on_close;
	return 0;

oops:
	for (i = minidx; i < maxidx; i++)
	{
		if (pfds[i] != HIO_SYSHND_INVALID) close (pfds[i]);
	}

	if (rdev->mcmd) 
	{
		hio_freemem (hio, rdev->mcmd);
		free_param (hio, &param);
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

static int dev_pro_kill_master (hio_dev_t* dev, int force)
{
	hio_t* hio = dev->hio;
	hio_dev_pro_t* rdev = (hio_dev_pro_t*)dev;
	int i, status;
	pid_t wpid;

	if (rdev->slave_count > 0)
	{
		for (i = 0; i < HIO_COUNTOF(rdev->slave); i++)
		{
			if (rdev->slave[i])
			{
				hio_dev_pro_slave_t* sdev = rdev->slave[i];

				/* nullify the pointer to the slave device
				 * before calling hio_dev_kill() on the slave device.
				 * the slave device can check this pointer to tell from
				 * self-initiated termination or master-driven termination */
				rdev->slave[i] = HIO_NULL;

				hio_dev_kill ((hio_dev_t*)sdev);
			}
		}
	}

	if (rdev->child_pid >= 0)
	{
		if (!(rdev->flags & HIO_DEV_PRO_FORGET_CHILD))
		{
			int killed = 0;

		await_child:
			wpid = waitpid(rdev->child_pid, &status, WNOHANG);
			if (wpid == 0)
			{
				if (force && !killed)
				{
					if (!(rdev->flags & HIO_DEV_PRO_FORGET_DIEHARD_CHILD))
					{
						kill (rdev->child_pid, SIGKILL);
						killed = 1;
						goto await_child;
					}
				}
				else
				{
					/* child process is still alive */
					hio_seterrnum (hio, HIO_EAGAIN);
					return -1;  /* call me again */
				}
			}

			/* wpid == rdev->child_pid => full success
			 * wpid == -1 && errno == ECHILD => no such process. it's waitpid()'ed by some other part of the program?
			 * other cases ==> can't really handle properly. forget it by returning success
			 * no need not worry about EINTR because errno can't have the value when WNOHANG is set.
			 */
		}

		HIO_DEBUG1 (hio, ">>>>>>>>>>>>>>>>>>> REAPED CHILD %d\n", (int)rdev->child_pid);
		rdev->child_pid = -1;
	}

	if (rdev->on_close) rdev->on_close (rdev, HIO_DEV_PRO_MASTER);
	return 0;
}

static int dev_pro_make_slave (hio_dev_t* dev, void* ctx)
{
	hio_dev_pro_slave_t* rdev = (hio_dev_pro_slave_t*)dev;
	slave_info_t* si = (slave_info_t*)ctx;

	rdev->dev_cap = si->dev_cap;
	rdev->id = si->id;
	rdev->pfd = si->pfd;
	/* keep rdev->master to HIO_NULL. it's set to the right master
	 * device in dev_pro_make() */

	return 0;
}

static int dev_pro_kill_slave (hio_dev_t* dev, int force)
{
	hio_t* hio = dev->hio;
	hio_dev_pro_slave_t* rdev = (hio_dev_pro_slave_t*)dev;

	if (rdev->master)
	{
		hio_dev_pro_t* master;

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
				 * nullified by the dev_pro_kill() */
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

static void dev_pro_fail_before_make_slave (void* ctx)
{
	slave_info_t* si = (slave_info_t*)ctx;
	close (si->pfd);
}

static int dev_pro_read_slave (hio_dev_t* dev, void* buf, hio_iolen_t* len, hio_devaddr_t* srcaddr)
{
	hio_dev_pro_slave_t* pro = (hio_dev_pro_slave_t*)dev;
	ssize_t x;

	/* the read and write operation happens on different slave devices.
	 * the write EOF indication doesn't affect this device 
	if (HIO_UNLIKELY(pro->pfd == HIO_SYSHND_INVALID))
	{
		hio_seterrnum (pro->hio, HIO_EBADHND);
		return -1;
	}*/
	HIO_ASSERT (pro->hio, pro->pfd != HIO_SYSHND_INVALID); /* use this assertion to check if my claim above is right */

	x = read(pro->pfd, buf, *len);
	if (x <= -1)
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data available */
		if (errno == EINTR) return 0;
		hio_seterrwithsyserr (pro->hio, 0, errno);
		return -1;
	}

	*len = x;
	return 1;
}

static int dev_pro_write_slave (hio_dev_t* dev, const void* data, hio_iolen_t* len, const hio_devaddr_t* dstaddr)
{
	hio_dev_pro_slave_t* pro = (hio_dev_pro_slave_t*)dev;
	ssize_t x;

	/* this check is not needed because HIO_DEV_CAP_OUT_CLOSED is set on the device by the core
	 * when EOF indication is successful(return value 1 and *iovcnt 0).
	 * If HIO_DEV_CAP_OUT_CLOSED, the core doesn't invoke the write method 
	if (HIO_UNLIKELY(pro->pfd == HIO_SYSHND_INVALID))
	{
		hio_seterrnum (pro->hio, HIO_EBADHND);
		return -1;
	}*/
	HIO_ASSERT (pro->hio, pro->pfd != HIO_SYSHND_INVALID); /* use this assertion to check if my claim above is right */

	if (HIO_UNLIKELY(*len <= 0))
	{
		/* this is an EOF indicator */
		/* It isn't appropriate to call hio_dev_halt(pro) or hio_dev_pro_close(pro->master, HIO_DEV_PRO_IN)
		 * as those functions destroy the device itself */
		if (HIO_LIKELY(pro->pfd != HIO_SYSHND_INVALID))
		{
			hio_dev_watch (dev, HIO_DEV_WATCH_STOP, 0);
			close (pro->pfd);
			pro->pfd = HIO_SYSHND_INVALID;
		}
		return 1; /* indicate that the operation got successful. the core will execute on_write() with the write length of 0. */
	}

	x = write(pro->pfd, data, *len);
	if (x <= -1)
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data can be written */
		if (errno == EINTR) return 0;
		hio_seterrwithsyserr (pro->hio, 0, errno);
		return -1;
	}

	*len = x;
	return 1;
}

static int dev_pro_writev_slave (hio_dev_t* dev, const hio_iovec_t* iov, hio_iolen_t* iovcnt, const hio_devaddr_t* dstaddr)
{
	hio_dev_pro_slave_t* pro = (hio_dev_pro_slave_t*)dev;
	ssize_t x;

	/* this check is not needed because HIO_DEV_CAP_OUT_CLOSED is set on the device by the core
	 * when EOF indication is successful(return value 1 and *iovcnt 0).
	 * If HIO_DEV_CAP_OUT_CLOSED, the core doesn't invoke the write method 
	if (HIO_UNLIKELY(pro->pfd == HIO_SYSHND_INVALID))
	{
		hio_seterrnum (pro->hio, HIO_EBADHND);
		return -1;
	}*/
	HIO_ASSERT (pro->hio, pro->pfd != HIO_SYSHND_INVALID); /* use this assertion to check if my claim above is right */

	if (HIO_UNLIKELY(*iovcnt <= 0))
	{
		/* this is an EOF indicator */
		/* It isn't appropriate to call hio_dev_halt(pro) or hio_dev_pro_close(pro->master, HIO_DEV_PRO_IN)
		 * as those functions destroy the device itself */
		if (HIO_LIKELY(pro->pfd != HIO_SYSHND_INVALID))
		{
			hio_dev_watch (dev, HIO_DEV_WATCH_STOP, 0);
			close (pro->pfd);
			pro->pfd = HIO_SYSHND_INVALID;
		}
		return 1; /* indicate that the operation got successful. the core will execute on_write() with 0. */
	}

	x = writev(pro->pfd, iov, *iovcnt);
	if (x <= -1)
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data can be written */
		if (errno == EINTR) return 0;
		hio_seterrwithsyserr (pro->hio, 0, errno);
		return -1;
	}

	*iovcnt = x;
	return 1;
}

static hio_syshnd_t dev_pro_getsyshnd (hio_dev_t* dev)
{
	return HIO_SYSHND_INVALID;
}

static hio_syshnd_t dev_pro_getsyshnd_slave (hio_dev_t* dev)
{
	hio_dev_pro_slave_t* pro = (hio_dev_pro_slave_t*)dev;
	return (hio_syshnd_t)pro->pfd;
}

static int dev_pro_ioctl (hio_dev_t* dev, int cmd, void* arg)
{
	hio_t* hio = dev->hio;
	hio_dev_pro_t* rdev = (hio_dev_pro_t*)dev;

	switch (cmd)
	{
		case HIO_DEV_PRO_CLOSE:
		{
			hio_dev_pro_sid_t sid = *(hio_dev_pro_sid_t*)arg;

			if (sid < HIO_DEV_PRO_IN || sid > HIO_DEV_PRO_ERR)
			{
				hio_seterrnum (hio, HIO_EINVAL);
				return -1;
			}

			if (rdev->slave[sid])
			{
				/* unlike dev_pro_kill_master(), i don't nullify rdev->slave[sid].
				 * so i treat the closing ioctl as if it's a kill request 
				 * initiated by the slave device itself. */
				hio_dev_kill ((hio_dev_t*)rdev->slave[sid]);

				/* if this is the last slave, the master is destroyed as well. 
				 * therefore, using rdev is unsafe in the assertion below is unsafe.
				 *HIO_ASSERT (hio, rdev->slave[sid] == HIO_NULL); */
			}

			return 0;
		}

		case HIO_DEV_PRO_KILL_CHILD:
			if (rdev->child_pid >= 0)
			{
				if (kill(rdev->child_pid, SIGKILL) == -1)
				{
					hio_seterrwithsyserr (hio, 0, errno);
					return -1;
				}
			}

			return 0;

		default:
			hio_seterrnum (hio, HIO_EINVAL);
			return -1;
	}
}

static hio_dev_mth_t dev_pro_methods = 
{
	dev_pro_make_master,
	dev_pro_kill_master,
	HIO_NULL,
	dev_pro_getsyshnd,
	HIO_NULL,
	dev_pro_ioctl,

	HIO_NULL, /* read */
	HIO_NULL, /* write */
	HIO_NULL, /* writev */
	HIO_NULL, /* sendfile */
};

static hio_dev_mth_t dev_pro_methods_slave =
{
	dev_pro_make_slave,
	dev_pro_kill_slave,
	HIO_NULL,
	dev_pro_getsyshnd_slave,
	HIO_NULL,
	dev_pro_ioctl,

	dev_pro_read_slave,
	dev_pro_write_slave,
	dev_pro_writev_slave,
	HIO_NULL, /* sendfile */
};

/* ========================================================================= */

static int pro_ready (hio_dev_t* dev, int events)
{
	/* virtual device. no I/O */
	hio_seterrnum (dev->hio, HIO_EINTERN);
	return -1;
}

static int pro_on_read (hio_dev_t* dev, const void* data, hio_iolen_t len, const hio_devaddr_t* srcaddr)
{
	/* virtual device. no I/O */
	hio_seterrnum (dev->hio, HIO_EINTERN);
	return -1;
}

static int pro_on_write (hio_dev_t* dev, hio_iolen_t wrlen, void* wrctx, const hio_devaddr_t* dstaddr)
{
	/* virtual device. no I/O */
	hio_seterrnum (dev->hio, HIO_EINTERN);
	return -1;
}

static hio_dev_evcb_t dev_pro_event_callbacks =
{
	pro_ready,
	pro_on_read,
	pro_on_write
};

/* ========================================================================= */

static int pro_ready_slave (hio_dev_t* dev, int events)
{
	hio_t* hio = dev->hio;
	/*hio_dev_pro_t* pro = (hio_dev_pro_t*)dev;*/

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
			return 1;
		}

		hio_seterrnum (hio, HIO_EDEVHUP);
		return -1;
	}

	return 1; /* the device is ok. carry on reading or writing */
}


static int pro_on_read_slave_out (hio_dev_t* dev, const void* data, hio_iolen_t len, const hio_devaddr_t* srcaddr)
{
	hio_dev_pro_slave_t* pro = (hio_dev_pro_slave_t*)dev;
	return pro->master->on_read(pro->master, HIO_DEV_PRO_OUT, data, len);
}

static int pro_on_read_slave_err (hio_dev_t* dev, const void* data, hio_iolen_t len, const hio_devaddr_t* srcaddr)
{
	hio_dev_pro_slave_t* pro = (hio_dev_pro_slave_t*)dev;
	return pro->master->on_read(pro->master, HIO_DEV_PRO_ERR, data, len);
}

static int pro_on_write_slave (hio_dev_t* dev, hio_iolen_t wrlen, void* wrctx, const hio_devaddr_t* dstaddr)
{
	hio_dev_pro_slave_t* pro = (hio_dev_pro_slave_t*)dev;
	return pro->master->on_write(pro->master, wrlen, wrctx);
}

static hio_dev_evcb_t dev_pro_event_callbacks_slave_in =
{
	pro_ready_slave,
	HIO_NULL,
	pro_on_write_slave
};

static hio_dev_evcb_t dev_pro_event_callbacks_slave_out =
{
	pro_ready_slave,
	pro_on_read_slave_out,
	HIO_NULL
};

static hio_dev_evcb_t dev_pro_event_callbacks_slave_err =
{
	pro_ready_slave,
	pro_on_read_slave_err,
	HIO_NULL
};

/* ========================================================================= */

static hio_dev_pro_slave_t* make_slave (hio_t* hio, slave_info_t* si)
{
	switch (si->id)
	{
		case HIO_DEV_PRO_IN:
			return (hio_dev_pro_slave_t*)hio_dev_make(
				hio, HIO_SIZEOF(hio_dev_pro_t), 
				&dev_pro_methods_slave, &dev_pro_event_callbacks_slave_in, si);

		case HIO_DEV_PRO_OUT:
			return (hio_dev_pro_slave_t*)hio_dev_make(
				hio, HIO_SIZEOF(hio_dev_pro_t), 
				&dev_pro_methods_slave, &dev_pro_event_callbacks_slave_out, si);

		case HIO_DEV_PRO_ERR:
			return (hio_dev_pro_slave_t*)hio_dev_make(
				hio, HIO_SIZEOF(hio_dev_pro_t), 
				&dev_pro_methods_slave, &dev_pro_event_callbacks_slave_err, si);

		default:
			hio_seterrnum (hio, HIO_EINVAL);
			return HIO_NULL;
	}
}

hio_dev_pro_t* hio_dev_pro_make (hio_t* hio, hio_oow_t xtnsize, const hio_dev_pro_make_t* info)
{
	return (hio_dev_pro_t*)hio_dev_make(
		hio, HIO_SIZEOF(hio_dev_pro_t) + xtnsize, 
		&dev_pro_methods, &dev_pro_event_callbacks, (void*)info);
}

void hio_dev_pro_kill (hio_dev_pro_t* dev)
{
	hio_dev_kill ((hio_dev_t*)dev);
}

void hio_dev_pro_halt (hio_dev_pro_t* dev)
{
	hio_dev_halt ((hio_dev_t*)dev);
}

int hio_dev_pro_read (hio_dev_pro_t* dev, hio_dev_pro_sid_t sid, int enabled)
{
	hio_t* hio = dev->hio;

	HIO_ASSERT (hio, sid == HIO_DEV_PRO_OUT || sid == HIO_DEV_PRO_ERR);

	if (dev->slave[sid])
	{
		return hio_dev_read((hio_dev_t*)dev->slave[sid], enabled);
	}
	else
	{
		hio_seterrnum (dev->hio, HIO_ENOCAPA); /* TODO: is it the right error number? */
		return -1;
	}
}

int hio_dev_pro_timedread (hio_dev_pro_t* dev, hio_dev_pro_sid_t sid, int enabled, const hio_ntime_t* tmout)
{
	hio_t* hio = dev->hio;

	HIO_ASSERT (hio, sid == HIO_DEV_PRO_OUT || sid == HIO_DEV_PRO_ERR);

	if (dev->slave[sid])
	{
		return hio_dev_timedread((hio_dev_t*)dev->slave[sid], enabled, tmout);
	}
	else
	{
		hio_seterrnum (hio, HIO_ENOCAPA); /* TODO: is it the right error number? */
		return -1;
	}
}

int hio_dev_pro_write (hio_dev_pro_t* dev, const void* data, hio_iolen_t dlen, void* wrctx)
{
	if (dev->slave[HIO_DEV_PRO_IN])
	{
		return hio_dev_write((hio_dev_t*)dev->slave[HIO_DEV_PRO_IN], data, dlen, wrctx, HIO_NULL);
	}
	else
	{
		hio_seterrnum (dev->hio, HIO_ENOCAPA); /* TODO: is it the right error number? */
		return -1;
	}
}

int hio_dev_pro_timedwrite (hio_dev_pro_t* dev, const void* data, hio_iolen_t dlen, const hio_ntime_t* tmout, void* wrctx)
{
	if (dev->slave[HIO_DEV_PRO_IN])
	{
		return hio_dev_timedwrite((hio_dev_t*)dev->slave[HIO_DEV_PRO_IN], data, dlen, tmout, wrctx, HIO_NULL);
	}
	else
	{
		hio_seterrnum (dev->hio, HIO_ENOCAPA); /* TODO: is it the right error number? */
		return -1;
	}
}

int hio_dev_pro_close (hio_dev_pro_t* dev, hio_dev_pro_sid_t sid)
{
	return hio_dev_ioctl((hio_dev_t*)dev, HIO_DEV_PRO_CLOSE, &sid);
}

int hio_dev_pro_killchild (hio_dev_pro_t* dev)
{
	return hio_dev_ioctl((hio_dev_t*)dev, HIO_DEV_PRO_KILL_CHILD, HIO_NULL);
}

#if 0
hio_dev_pro_t* hio_dev_pro_getdev (hio_dev_pro_t* pro, hio_dev_pro_sid_t sid)
{
	switch (type)
	{
		case HIO_DEV_PRO_IN:
			return XXX;

		case HIO_DEV_PRO_OUT:
			return XXX;

		case HIO_DEV_PRO_ERR:
			return XXX;
	}

	pro->dev->hio = HIO_EINVAL;
	return HIO_NULL;
}
#endif
