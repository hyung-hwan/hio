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

#ifndef _HIO_PRO_H_
#define _HIO_PRO_H_

#include <hio.h>

enum hio_dev_pro_sid_t
{
	HIO_DEV_PRO_MASTER = -1, /* no io occurs on this. used only in on_close() */
	HIO_DEV_PRO_IN     =  0, /* input of the child process */
	HIO_DEV_PRO_OUT    =  1, /* output of the child process */
	HIO_DEV_PRO_ERR    =  2  /* error output of the child process */
};
typedef enum hio_dev_pro_sid_t hio_dev_pro_sid_t;

typedef struct hio_dev_pro_t hio_dev_pro_t;
typedef struct hio_dev_pro_slave_t hio_dev_pro_slave_t;

typedef int (*hio_dev_pro_on_read_t) (
	hio_dev_pro_t*    dev,
	hio_dev_pro_sid_t sid,
	const void*       data,
	hio_iolen_t       len
);

typedef int (*hio_dev_pro_on_write_t) (
	hio_dev_pro_t*    dev,
	hio_iolen_t       wrlen,
	void*             wrctx
);

typedef void (*hio_dev_pro_on_close_t) (
	hio_dev_pro_t*    dev,
	hio_dev_pro_sid_t sid
);

typedef int (*hio_dev_pro_on_fork_t) (
	hio_dev_pro_t*    dev,
	void*             fork_ctx
);

struct hio_dev_pro_t
{
	HIO_DEV_HEADER;

	int flags;
	hio_intptr_t child_pid;
	hio_dev_pro_slave_t* slave[3];
	int slave_count;

	hio_dev_pro_on_read_t on_read;
	hio_dev_pro_on_write_t on_write;
	hio_dev_pro_on_close_t on_close;

	hio_bch_t* mcmd;
};

struct hio_dev_pro_slave_t
{
	HIO_DEV_HEADER;
	hio_dev_pro_sid_t id;
	hio_syshnd_t pfd;
	hio_dev_pro_t* master; /* parent device */
};

enum hio_dev_pro_make_flag_t
{
	HIO_DEV_PRO_WRITEIN = (1 << 0),
	HIO_DEV_PRO_READOUT = (1 << 1),
	HIO_DEV_PRO_READERR = (1 << 2),

	HIO_DEV_PRO_ERRTOOUT = (1 << 3),
	HIO_DEV_PRO_OUTTOERR = (1 << 4),

	HIO_DEV_PRO_INTONUL = (1 << 5),
	HIO_DEV_PRO_OUTTONUL = (1 << 6),
	HIO_DEV_PRO_ERRTONUL = (1 << 7),

	HIO_DEV_PRO_DROPIN  = (1 << 8),
	HIO_DEV_PRO_DROPOUT = (1 << 9),
	HIO_DEV_PRO_DROPERR = (1 << 10),

	HIO_DEV_PRO_UCMD = (1 << 12), /* cmd is hio_uch_t* */
	HIO_DEV_PRO_SHELL = (1 << 13),

	/* perform no waitpid() on a child process upon device destruction.
	 * you should set this flag if your application has automatic child 
	 * process reaping enabled. for instance, SIGCHLD is set to SIG_IGN
	 * on POSIX.1-2001 compliant systems */
	HIO_DEV_PRO_FORGET_CHILD = (1 << 14),


	HIO_DEV_PRO_FORGET_DIEHARD_CHILD = (1 << 15)
};
typedef enum hio_dev_pro_make_flag_t hio_dev_pro_make_flag_t;

typedef struct hio_dev_pro_make_t hio_dev_pro_make_t;
struct hio_dev_pro_make_t
{
	int flags; /**< bitwise-ORed of hio_dev_pro_make_flag_t enumerators */
	const void* cmd; /* the actual type is determined by HIO_DEV_PRO_UCMD */
	hio_dev_pro_on_write_t on_write; /* mandatory */
	hio_dev_pro_on_read_t on_read; /* mandatory */
	hio_dev_pro_on_close_t on_close; /* optional */
	hio_dev_pro_on_fork_t on_fork; /* optional */
	void* fork_ctx;
};


enum hio_dev_pro_ioctl_cmd_t
{
	HIO_DEV_PRO_CLOSE,
	HIO_DEV_PRO_KILL_CHILD
};
typedef enum hio_dev_pro_ioctl_cmd_t hio_dev_pro_ioctl_cmd_t;

#if defined(__cplusplus)
extern "C" {
#endif

HIO_EXPORT  hio_dev_pro_t* hio_dev_pro_make (
	hio_t*                    hio,
	hio_oow_t                 xtnsize,
	const hio_dev_pro_make_t* data
);

#if defined(HIO_HAVE_INLINE)
static HIO_INLINE hio_t* hio_dev_pro_gethio (hio_dev_pro_t* pro) { return hio_dev_gethio((hio_dev_t*)pro); }
#else
#	define hio_dev_pro_gethio(pro) hio_dev_gethio(pro)
#endif

#if defined(HIO_HAVE_INLINE)
static HIO_INLINE void* hio_dev_pro_getxtn (hio_dev_pro_t* pro) { return (void*)(pro + 1); }
#else
#	define hio_dev_pro_getxtn(pro) ((void*)(((hio_dev_pro_t*)pro) + 1))
#endif

HIO_EXPORT void hio_dev_pro_kill (
	hio_dev_pro_t* pro
);

HIO_EXPORT void hio_dev_pro_halt (
	hio_dev_pro_t* pro
);

HIO_EXPORT int hio_dev_pro_read (
	hio_dev_pro_t*     pro,
	hio_dev_pro_sid_t  sid, /**< either #HIO_DEV_PRO_OUT or #HIO_DEV_PRO_ERR */
	int                enabled
);

HIO_EXPORT int hio_dev_pro_timedread (
	hio_dev_pro_t*     pro,
	hio_dev_pro_sid_t  sid, /**< either #HIO_DEV_PRO_OUT or #HIO_DEV_PRO_ERR */
	int                enabled,
	const hio_ntime_t* tmout
);

HIO_EXPORT int hio_dev_pro_write (
	hio_dev_pro_t*     pro,
	const void*        data,
	hio_iolen_t        len,
	void*              wrctx
);

HIO_EXPORT int hio_dev_pro_timedwrite (
	hio_dev_pro_t*     pro,
	const void*        data,
	hio_iolen_t        len,
	const hio_ntime_t* tmout,
	void*              wrctx
);

HIO_EXPORT int hio_dev_pro_close (
	hio_dev_pro_t*     pro,
	hio_dev_pro_sid_t  sid
);

HIO_EXPORT int hio_dev_pro_killchild (
	hio_dev_pro_t*     pro
);

#if defined(__cplusplus)
}
#endif

#endif
