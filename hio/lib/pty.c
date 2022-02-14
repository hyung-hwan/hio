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

#include <hio-pty.h>
#include "hio-prv.h"

#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>

#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>

#if defined(HAVE_PTY_H)
#	include <pty.h>
#endif

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

	if (flags & HIO_DEV_PTY_SHELL)
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

static pid_t standard_fork_and_exec (hio_dev_pty_t* dev, int pfds[], hio_dev_pty_make_t* mi, param_t* param)
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
		/* child */
		close (pfds[0]);  /* close the pty master */
		pfds[0] = HIO_SYSHND_INVALID;

/*TODO: close all open file descriptors */
		if (mi->on_fork) mi->on_fork (dev, mi->fork_ctx);

		setsid (); /* TODO: error check? */
		setpgid (0, 0);
		if (ioctl(pfds[1], TIOCSCTTY, HIO_NULL) == -1) goto slave_oops;

		if (dup2(pfds[1], 0) == -1 || dup2(pfds[1], 1) == -1 || dup2(pfds[1], 2) == -1) goto slave_oops;

		close (pfds[1]);
		pfds[1] = HIO_SYSHND_INVALID;

/* TODO: pass environment like TERM */
		execv (param->argv[0], param->argv);

		/* if exec fails, free 'param' parameter which is an inherited pointer */
		free_param (hio, param); 

	slave_oops:
		if (pfds[1] != HIO_SYSHND_INVALID) close(pfds[1]);
		_exit (128);
	}

	/* parent process */
	return pid;
}


static int dev_pty_make (hio_dev_t* dev, void* ctx)
{
	hio_t* hio = dev->hio;
	hio_dev_pty_t* rdev = (hio_dev_pty_t*)dev;
	hio_dev_pty_make_t* info = (hio_dev_pty_make_t*)ctx;
	hio_syshnd_t pfds[2] = { HIO_SYSHND_INVALID, HIO_SYSHND_INVALID };
	int i, fd;
	param_t param;
	pid_t pid;

#if defined(HAVE_POSIX_OPENPT)

	/* open a pty master unused */
	fd = posix_openpt(O_RDWR | O_NOCTTY);
	if (fd == -1)
	{
		hio_seterrwithsyserr (hio, 0, errno);
		goto oops;
	}

	pfds[0] = fd;

	if (grantpt(pfds[0]) == -1 || unlockpt(pfds[0]) == -1)
	{
		hio_seterrwithsyserr (hio, 0, errno);
		goto oops;
	}
	else
	{
		char pts_name_buf[128];
		char* ptr = pts_name_buf;
		hio_oow_t capa = HIO_COUNTOF(pts_name_buf);

		/* open a pty slave device name */
		if (ptsname_r(pfds[0], ptr, capa) != 0)
		{
			if (errno == ERANGE) 
			{
				char* tmp;
				tmp = hio_reallocmem(hio, (ptr == pts_name_buf? HIO_NULL: ptr), capa + 128);
				if (HIO_UNLIKELY(!tmp))
				{
					if (ptr != pts_name_buf) hio_freemem (hio, ptr);
					goto oops;
				}
				ptr = tmp;
				capa += 128;
			}
			hio_seterrwithsyserr (hio, 0, errno);
			goto oops;
		}

		/* open a pty slave */
		pfds[1] = open(ptr, O_RDWR | O_NOCTTY);
		if (pfds[1] == -1)
		{
			hio_seterrwithsyserr (hio, 0, errno);
			if (ptr != pts_name_buf) hio_freemem (hio, ptr);
			goto oops;
		}

		if (ptr != pts_name_buf) hio_freemem (hio, ptr);
	}

#elif defined(HAVE_OPENPTY)
	if (openpty(&pfds[0], &pfds[1], HIO_NULL, HIO_NULL, HIO_NULL) == -1)
	{
		hio_seterrwithsyserr (hio, 0, errno);
		goto oops;
	}
#else
#	error NOT IMPLEMENTED YET
#endif

	if (hio_makesyshndcloexec(hio, pfds[0]) <= -1 ||
	    hio_makesyshndcloexec(hio, pfds[1]) <= -1) goto oops;

	if (make_param(hio, info->cmd, info->flags, &param) <= -1) goto oops;
	pid = standard_fork_and_exec(rdev, pfds, info, &param);
	free_param (hio, &param);
	if (pid <= -1) goto oops;

	close (pfds[1]); /* close the pty slave */
	pfds[1] = HIO_SYSHND_INVALID;

	if (hio_makesyshndasync(hio, pfds[0]) <= -1) goto oops;

	rdev->pfd = pfds[0];
	rdev->child_pid = pid;
	rdev->flags = info->flags;
	rdev->dev_cap = HIO_DEV_CAP_OUT | HIO_DEV_CAP_IN | HIO_DEV_CAP_STREAM;
	rdev->on_read = info->on_read;
	rdev->on_write = info->on_write;
	rdev->on_close = info->on_close;
	return 0;

oops:
	if (pfds[0] != HIO_SYSHND_INVALID) close (pfds[0]);
	if (pfds[1] != HIO_SYSHND_INVALID) close (pfds[1]);
	return -1;
}

static int dev_pty_kill (hio_dev_t* dev, int force)
{
	hio_t* hio = dev->hio;
	hio_dev_pty_t* rdev = (hio_dev_pty_t*)dev;

	if (rdev->child_pid >= 0)
	{
		if (!(rdev->flags & HIO_DEV_PTY_FORGET_CHILD))
		{
			int killed = 0;
			int status;
			pid_t wpid;

		await_child:
			wpid = waitpid(rdev->child_pid, &status, WNOHANG);
			if (wpid == 0)
			{
				if (force && !killed)
				{
					if (!(rdev->flags & HIO_DEV_PTY_FORGET_DIEHARD_CHILD))
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

		HIO_DEBUG1 (hio, "PTY >>>>>>>>>>>>>>>>>>> REAPED CHILD %d\n", (int)rdev->child_pid);
		rdev->child_pid = -1;
	}

	if (rdev->on_close) rdev->on_close (rdev);

	if (rdev->pfd != HIO_SYSHND_INVALID)
	{
		close (rdev->pfd);
		rdev->pfd = HIO_SYSHND_INVALID;
	}
	return 0;
}

static int dev_pty_read (hio_dev_t* dev, void* buf, hio_iolen_t* len, hio_devaddr_t* srcaddr)
{
	hio_dev_pty_t* pty = (hio_dev_pty_t*)dev;
	ssize_t x;

	if (HIO_UNLIKELY(pty->pfd == HIO_SYSHND_INVALID))
	{
		hio_seterrnum (pty->hio, HIO_EBADHND);
		return -1;
	}

	x = read(pty->pfd, buf, *len);
	if (x <= -1)
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data available */
		if (errno == EINTR) return 0;
		hio_seterrwithsyserr (pty->hio, 0, errno);
		return -1;
	}

	*len = x;
	return 1;
}

static int dev_pty_write (hio_dev_t* dev, const void* data, hio_iolen_t* len, const hio_devaddr_t* dstaddr)
{
	hio_dev_pty_t* pty = (hio_dev_pty_t*)dev;
	ssize_t x;

	if (HIO_UNLIKELY(pty->pfd == HIO_SYSHND_INVALID))
	{
		hio_seterrnum (pty->hio, HIO_EBADHND);
		return -1;
	}

	if (HIO_UNLIKELY(*len <= 0))
	{
		/* this is an EOF indicator */
		/*hio_dev_halt (dev);*/ /* halt this slave device to indicate EOF on the lower-level handle */
		if (HIO_LIKELY(pty->pfd != HIO_SYSHND_INVALID)) /* halt() doesn't close the pty immediately. so close the underlying pty */
		{
			hio_dev_watch (dev, HIO_DEV_WATCH_STOP, 0);
			close (pty->pfd);
			pty->pfd = HIO_SYSHND_INVALID;
		}
		return 1; /* indicate that the operation got successful. the core will execute on_write() with 0. */
	}

	x = write(pty->pfd, data, *len);
	if (x <= -1)
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data can be written */
		if (errno == EINTR) return 0;
		hio_seterrwithsyserr (pty->hio, 0, errno);
		return -1;
	}

	*len = x;
	return 1;
}

static int dev_pty_writev (hio_dev_t* dev, const hio_iovec_t* iov, hio_iolen_t* iovcnt, const hio_devaddr_t* dstaddr)
{
	hio_dev_pty_t* pty = (hio_dev_pty_t*)dev;
	ssize_t x;

	if (HIO_UNLIKELY(pty->pfd == HIO_SYSHND_INVALID))
	{
		hio_seterrnum (pty->hio, HIO_EBADHND);
		return -1;
	}

	if (HIO_UNLIKELY(*iovcnt <= 0))
	{
		/* this is an EOF indicator */
		/*hio_dev_halt (dev);*/ /* halt this slave device to indicate EOF on the lower-level handle  */
		if (HIO_LIKELY(pty->pfd != HIO_SYSHND_INVALID)) /* halt() doesn't close the pty immediately. so close the underlying pty */
		{
			hio_dev_watch (dev, HIO_DEV_WATCH_STOP, 0);
			close (pty->pfd);
			pty->pfd = HIO_SYSHND_INVALID;
		}
		return 1; /* indicate that the operation got successful. the core will execute on_write() with 0. */
	}

	x = writev(pty->pfd, iov, *iovcnt);
	if (x <= -1)
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data can be written */
		if (errno == EINTR) return 0;
		hio_seterrwithsyserr (pty->hio, 0, errno);
		return -1;
	}

	*iovcnt = x;
	return 1;
}

static hio_syshnd_t dev_pty_getsyshnd (hio_dev_t* dev)
{
	hio_dev_pty_t* rdev = (hio_dev_pty_t*)dev;
	return rdev->pfd;
}

static int dev_pty_ioctl (hio_dev_t* dev, int cmd, void* arg)
{
	hio_t* hio = dev->hio;
	hio_dev_pty_t* rdev = (hio_dev_pty_t*)dev;

	switch (cmd)
	{
		case HIO_DEV_PTY_CLOSE:
			hio_dev_kill ((hio_dev_t*)rdev);
			return 0;

		case HIO_DEV_PTY_KILL_CHILD:
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

static hio_dev_mth_t dev_pty_methods = 
{
	dev_pty_make,
	dev_pty_kill,
	HIO_NULL,
	dev_pty_getsyshnd,
	HIO_NULL,
	dev_pty_ioctl,

	dev_pty_read,
	dev_pty_write,
	dev_pty_writev,
	HIO_NULL, /* sendfile */
};

/* ========================================================================= */

static int pty_ready (hio_dev_t* dev, int events)
{
	hio_t* hio = dev->hio;
	/*hio_dev_pty_t* pty = (hio_dev_pty_t*)dev;*/

	if (events & HIO_DEV_EVENT_ERR)
	{
		hio_seterrnum (hio, HIO_EDEVERR);
		return -1;
	}

	if (events & HIO_DEV_EVENT_HUP)
	{
		if (events & (HIO_DEV_EVENT_PRI | HIO_DEV_EVENT_IN | HIO_DEV_EVENT_OUT)) 
		{
			/* ptybably half-open? */
			return 1;
		}

		hio_seterrnum (hio, HIO_EDEVHUP);
		return -1;
	}

	return 1; /* the device is ok. carry on reading or writing */
}

static int pty_on_read (hio_dev_t* dev, const void* data, hio_iolen_t len, const hio_devaddr_t* srcaddr)
{
	hio_dev_pty_t* pty = (hio_dev_pty_t*)dev;
	return pty->on_read(pty, data, len);
}

static int pty_on_write (hio_dev_t* dev, hio_iolen_t wrlen, void* wrctx, const hio_devaddr_t* dstaddr)
{
	hio_dev_pty_t* pty = (hio_dev_pty_t*)dev;
	return pty->on_write(pty, wrlen, wrctx);
}


static hio_dev_evcb_t dev_pty_event_callbacks =
{
	pty_ready,
	pty_on_read,
	pty_on_write
};

/* ========================================================================= */

hio_dev_pty_t* hio_dev_pty_make (hio_t* hio, hio_oow_t xtnsize, const hio_dev_pty_make_t* info)
{
	return (hio_dev_pty_t*)hio_dev_make(
		hio, HIO_SIZEOF(hio_dev_pty_t) + xtnsize, 
		&dev_pty_methods, &dev_pty_event_callbacks, (void*)info);
}

void hio_dev_pty_kill (hio_dev_pty_t* dev)
{
	hio_dev_kill ((hio_dev_t*)dev);
}

void hio_dev_pty_halt (hio_dev_pty_t* dev)
{
	hio_dev_halt ((hio_dev_t*)dev);
}

int hio_dev_pty_read (hio_dev_pty_t* dev, int enabled)
{
	return hio_dev_read((hio_dev_t*)dev, enabled);
}

int hio_dev_pty_timedread (hio_dev_pty_t* dev, int enabled, const hio_ntime_t* tmout)
{
	return hio_dev_timedread((hio_dev_t*)dev, enabled, tmout);
}

int hio_dev_pty_write (hio_dev_pty_t* dev, const void* data, hio_iolen_t dlen, void* wrctx)
{
	return hio_dev_write((hio_dev_t*)dev, data, dlen, wrctx, HIO_NULL);
}

int hio_dev_pty_timedwrite (hio_dev_pty_t* dev, const void* data, hio_iolen_t dlen, const hio_ntime_t* tmout, void* wrctx)
{
	return hio_dev_timedwrite((hio_dev_t*)dev, data, dlen, tmout, wrctx, HIO_NULL);
}

int hio_dev_pty_close (hio_dev_pty_t* dev)
{
	return hio_dev_ioctl((hio_dev_t*)dev, HIO_DEV_PTY_CLOSE, HIO_NULL);
}

int hio_dev_pty_killchild (hio_dev_pty_t* dev)
{
	return hio_dev_ioctl((hio_dev_t*)dev, HIO_DEV_PTY_KILL_CHILD, HIO_NULL);
}
