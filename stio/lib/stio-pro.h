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

#ifndef _STIO_PRO_H_
#define _STIO_PRO_H_

#include <stio.h>

enum stio_dev_pro_sid_t
{
	STIO_DEV_PRO_MASTER = -1,
	STIO_DEV_PRO_IN     =  0,
	STIO_DEV_PRO_OUT    =  1,
	STIO_DEV_PRO_ERR    =  2
};
typedef enum stio_dev_pro_sid_t stio_dev_pro_sid_t;

typedef struct stio_dev_pro_t stio_dev_pro_t;
typedef struct stio_dev_pro_slave_t stio_dev_pro_slave_t;

typedef int (*stio_dev_pro_on_read_t) (stio_dev_pro_t* dev, const void* data, stio_iolen_t len, stio_dev_pro_sid_t sid);
typedef int (*stio_dev_pro_on_write_t) (stio_dev_pro_t* dev, stio_iolen_t wrlen, void* wrctx);
typedef void (*stio_dev_pro_on_close_t) (stio_dev_pro_t* dev, stio_dev_pro_sid_t sid);

struct stio_dev_pro_t
{
	STIO_DEV_HEADERS;

	int flags;
	stio_intptr_t child_pid;
	stio_dev_pro_slave_t* slave[3];
	int slave_count;

	stio_dev_pro_on_read_t on_read;
	stio_dev_pro_on_write_t on_write;
	stio_dev_pro_on_close_t on_close;

	stio_mchar_t* mcmd;
};

struct stio_dev_pro_slave_t
{
	STIO_DEV_HEADERS;
	stio_dev_pro_sid_t id;
	stio_syshnd_t pfd;
	stio_dev_pro_t* master; /* parent device */
};

enum stio_dev_pro_make_flag_t
{
	STIO_DEV_PRO_WRITEIN  = (1 << 0),
	STIO_DEV_PRO_READOUT  = (1 << 1),
	STIO_DEV_PRO_READERR  = (1 << 2),

	STIO_DEV_PRO_ERRTOOUT = (1 << 3),
	STIO_DEV_PRO_OUTTOERR = (1 << 4),

	STIO_DEV_PRO_INTONUL  = (1 << 5),
	STIO_DEV_PRO_OUTTONUL = (1 << 6),
	STIO_DEV_PRO_ERRTONUL = (1 << 7),

	STUO_DEV_PRO_DROPIN   = (1 << 8),
	STUO_DEV_PRO_DROPOUT  = (1 << 9),
	STUO_DEV_PRO_DROPERR  = (1 << 10),


	STIO_DEV_PRO_SHELL                = (1 << 13),

	/* perform no waitpid() on a child process upon device destruction.
	 * you should set this flag if your application has automatic child 
	 * process reaping enabled. for instance, SIGCHLD is set to SIG_IGN
	 * on POSIX.1-2001 compliant systems */
	STIO_DEV_PRO_FORGET_CHILD         = (1 << 14),


	STIO_DEV_PRO_FORGET_DIEHARD_CHILD = (1 << 15)
};
typedef enum stio_dev_pro_make_flag_t stio_dev_pro_make_flag_t;

typedef struct stio_dev_pro_make_t stio_dev_pro_make_t;
struct stio_dev_pro_make_t
{
	int flags; /**< bitwise-ORed of stio_dev_pro_make_flag_t enumerators */
	const void* cmd;
	stio_dev_pro_on_write_t on_write; /* mandatory */
	stio_dev_pro_on_read_t on_read; /* mandatory */
	stio_dev_pro_on_close_t on_close; /* optional */
};


enum stio_dev_pro_ioctl_cmd_t
{
	STIO_DEV_PRO_CLOSE,
	STIO_DEV_PRO_KILL_CHILD
};
typedef enum stio_dev_pro_ioctl_cmd_t stio_dev_pro_ioctl_cmd_t;

#ifdef __cplusplus
extern "C" {
#endif

STIO_EXPORT  stio_dev_pro_t* stio_dev_pro_make (
	stio_t*                    stio,
	stio_size_t                xtnsize,
	const stio_dev_pro_make_t* data
);

STIO_EXPORT void stio_dev_pro_kill (
	stio_dev_pro_t* pro
);

STIO_EXPORT int stio_dev_pro_write (
	stio_dev_pro_t*     pro,
	const void*         data,
	stio_iolen_t        len,
	void*               wrctx
);

STIO_EXPORT int stio_dev_pro_timedwrite (
	stio_dev_pro_t*     pro,
	const void*         data,
	stio_iolen_t        len,
	const stio_ntime_t* tmout,
	void*               wrctx
);

STIO_EXPORT int stio_dev_pro_close (
	stio_dev_pro_t*     pro,
	stio_dev_pro_sid_t  sid
);


STIO_EXPORT int stio_dev_pro_killchild (
	stio_dev_pro_t*     pro
);

#ifdef __cplusplus
}
#endif

#endif
