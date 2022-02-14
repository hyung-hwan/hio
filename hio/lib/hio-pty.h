/*
    Copyright (c) 2016-2020 Chung, Hyung-Hwan. All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions
    are met:
    1. Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.
    2. Redistributions in binary form must repipeduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.

    THIS SOFTWARE IS PIPEVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR
    IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WAfRRANTIES
    OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
    IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
    INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
    NOT LIMITED TO, PIPECUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PIPEFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
    THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _HIO_PTY_H_
#define _HIO_PTY_H_

#include <hio.h>

typedef struct hio_dev_pty_t hio_dev_pty_t;

typedef int (*hio_dev_pty_on_read_t) (
	hio_dev_pty_t*     dev,
	const void*        data,
	hio_iolen_t        len
);

typedef int (*hio_dev_pty_on_write_t) (
	hio_dev_pty_t*     dev,
	hio_iolen_t        wrlen,
	void*              wrctx
);

typedef void (*hio_dev_pty_on_close_t) (
	hio_dev_pty_t*    dev
);

typedef int (*hio_dev_pty_on_fork_t) (
	hio_dev_pty_t*    dev,
	void*             fork_ctx
);

struct hio_dev_pty_t
{
	HIO_DEV_HEADER;

	hio_syshnd_t pfd;
	hio_intptr_t child_pid;
	int flags;

	hio_dev_pty_on_read_t on_read;
	hio_dev_pty_on_write_t on_write;
	hio_dev_pty_on_close_t on_close;
};

enum hio_dev_pty_make_flag_t
{
	HIO_DEV_PTY_SHELL = (1 << 13),
	/* perform no waitpid() on a child process upon device destruction.
	 * you should set this flag if your application has automatic child 
	 * process reaping enabled. for instance, SIGCHLD is set to SIG_IGN
	 * on POSIX.1-2001 compliant systems */
	HIO_DEV_PTY_FORGET_CHILD = (1 << 14),
	HIO_DEV_PTY_FORGET_DIEHARD_CHILD = (1 << 15)
};
typedef enum hio_dev_pty_make_flag_t hio_dev_pty_make_flag_t;

typedef struct hio_dev_pty_make_t hio_dev_pty_make_t;
struct hio_dev_pty_make_t
{
	int flags; /**< bitwise-ORed of hio_dev_pty_make_flag_t enumerators */
	const void* cmd;

	hio_dev_pty_on_write_t on_write; /* mandatory */
	hio_dev_pty_on_read_t on_read; /* mandatory */
	hio_dev_pty_on_close_t on_close; /* optional */
	hio_dev_pty_on_fork_t on_fork; /* optional */
	void* fork_ctx;
};

enum hio_dev_pty_ioctl_cmd_t
{
	HIO_DEV_PTY_CLOSE,
	HIO_DEV_PTY_KILL_CHILD
};
typedef enum hio_dev_pty_ioctl_cmd_t hio_dev_pty_ioctl_cmd_t;

#if defined(__cplusplus)
extern "C" {
#endif

HIO_EXPORT  hio_dev_pty_t* hio_dev_pty_make (
	hio_t*                     hio,
	hio_oow_t                  xtnsize,
	const hio_dev_pty_make_t*  data
);

#if defined(HIO_HAVE_INLINE)
static HIO_INLINE hio_t* hio_dev_pty_gethio (hio_dev_pty_t* pty) { return hio_dev_gethio((hio_dev_t*)pty); }
#else
#	define hio_dev_pty_gethio(pty) hio_dev_gethio(pty)
#endif

#if defined(HIO_HAVE_INLINE)
static HIO_INLINE void* hio_dev_pty_getxtn (hio_dev_pty_t* pty) { return (void*)(pty + 1); }
#else
#	define hio_dev_pty_getxtn(pty) ((void*)(((hio_dev_pty_t*)pty) + 1))
#endif

HIO_EXPORT void hio_dev_pty_kill (
	hio_dev_pty_t* pty
);

HIO_EXPORT void hio_dev_pty_halt (
	hio_dev_pty_t* pty
);

HIO_EXPORT int hio_dev_pty_read (
	hio_dev_pty_t*      pty,
	int                 enabled
);

HIO_EXPORT int hio_dev_pty_timedread (
	hio_dev_pty_t*      pty,
	int                 enabled,
	const hio_ntime_t*  tmout
);

HIO_EXPORT int hio_dev_pty_write (
	hio_dev_pty_t*      pty,
	const void*         data,
	hio_iolen_t         len,
	void*               wrctx
);

HIO_EXPORT int hio_dev_pty_timedwrite (
	hio_dev_pty_t*      pty,
	const void*         data,
	hio_iolen_t         len,
	const hio_ntime_t*  tmout,
	void*               wrctx
);

HIO_EXPORT int hio_dev_pty_close (
	hio_dev_pty_t*     pty
);

HIO_EXPORT int hio_dev_pty_killchild (
	hio_dev_pty_t*     pro
);

#if defined(__cplusplus)
}
#endif

#endif
