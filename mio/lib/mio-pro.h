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

#ifndef _MIO_PRO_H_
#define _MIO_PRO_H_

#include <mio.h>

enum mio_dev_pro_sid_t
{
	MIO_DEV_PRO_MASTER = -1,
	MIO_DEV_PRO_IN     =  0,
	MIO_DEV_PRO_OUT    =  1,
	MIO_DEV_PRO_ERR    =  2
};
typedef enum mio_dev_pro_sid_t mio_dev_pro_sid_t;

typedef struct mio_dev_pro_t mio_dev_pro_t;
typedef struct mio_dev_pro_slave_t mio_dev_pro_slave_t;

typedef int (*mio_dev_pro_on_read_t) (mio_dev_pro_t* dev, const void* data, mio_iolen_t len, mio_dev_pro_sid_t sid);
typedef int (*mio_dev_pro_on_write_t) (mio_dev_pro_t* dev, mio_iolen_t wrlen, void* wrctx);
typedef void (*mio_dev_pro_on_close_t) (mio_dev_pro_t* dev, mio_dev_pro_sid_t sid);

struct mio_dev_pro_t
{
	MIO_DEV_HEADERS;

	int flags;
	mio_intptr_t child_pid;
	mio_dev_pro_slave_t* slave[3];
	int slave_count;

	mio_dev_pro_on_read_t on_read;
	mio_dev_pro_on_write_t on_write;
	mio_dev_pro_on_close_t on_close;

	mio_bch_t* mcmd;
};

struct mio_dev_pro_slave_t
{
	MIO_DEV_HEADERS;
	mio_dev_pro_sid_t id;
	mio_syshnd_t pfd;
	mio_dev_pro_t* master; /* parent device */
};

enum mio_dev_pro_make_flag_t
{
	MIO_DEV_PRO_WRITEIN  = (1 << 0),
	MIO_DEV_PRO_READOUT  = (1 << 1),
	MIO_DEV_PRO_READERR  = (1 << 2),

	MIO_DEV_PRO_ERRTOOUT = (1 << 3),
	MIO_DEV_PRO_OUTTOERR = (1 << 4),

	MIO_DEV_PRO_INTONUL  = (1 << 5),
	MIO_DEV_PRO_OUTTONUL = (1 << 6),
	MIO_DEV_PRO_ERRTONUL = (1 << 7),

	STUO_DEV_PRO_DROPIN   = (1 << 8),
	STUO_DEV_PRO_DROPOUT  = (1 << 9),
	STUO_DEV_PRO_DROPERR  = (1 << 10),


	MIO_DEV_PRO_SHELL                = (1 << 13),

	/* perform no waitpid() on a child process upon device destruction.
	 * you should set this flag if your application has automatic child 
	 * process reaping enabled. for instance, SIGCHLD is set to SIG_IGN
	 * on POSIX.1-2001 compliant systems */
	MIO_DEV_PRO_FORGET_CHILD         = (1 << 14),


	MIO_DEV_PRO_FORGET_DIEHARD_CHILD = (1 << 15)
};
typedef enum mio_dev_pro_make_flag_t mio_dev_pro_make_flag_t;

typedef struct mio_dev_pro_make_t mio_dev_pro_make_t;
struct mio_dev_pro_make_t
{
	int flags; /**< bitwise-ORed of mio_dev_pro_make_flag_t enumerators */
	const void* cmd;
	mio_dev_pro_on_write_t on_write; /* mandatory */
	mio_dev_pro_on_read_t on_read; /* mandatory */
	mio_dev_pro_on_close_t on_close; /* optional */
};


enum mio_dev_pro_ioctl_cmd_t
{
	MIO_DEV_PRO_CLOSE,
	MIO_DEV_PRO_KILL_CHILD
};
typedef enum mio_dev_pro_ioctl_cmd_t mio_dev_pro_ioctl_cmd_t;

#ifdef __cplusplus
extern "C" {
#endif

MIO_EXPORT  mio_dev_pro_t* mio_dev_pro_make (
	mio_t*                    mio,
	mio_oow_t                xtnsize,
	const mio_dev_pro_make_t* data
);

MIO_EXPORT void mio_dev_pro_kill (
	mio_dev_pro_t* pro
);

MIO_EXPORT int mio_dev_pro_write (
	mio_dev_pro_t*     pro,
	const void*         data,
	mio_iolen_t        len,
	void*               wrctx
);

MIO_EXPORT int mio_dev_pro_timedwrite (
	mio_dev_pro_t*     pro,
	const void*         data,
	mio_iolen_t        len,
	const mio_ntime_t* tmout,
	void*               wrctx
);

MIO_EXPORT int mio_dev_pro_close (
	mio_dev_pro_t*     pro,
	mio_dev_pro_sid_t  sid
);


MIO_EXPORT int mio_dev_pro_killchild (
	mio_dev_pro_t*     pro
);

#ifdef __cplusplus
}
#endif

#endif
