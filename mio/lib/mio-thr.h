/*
 * $Id$
 *
    Copyright (c) 2016-2020 Chung, Hyung-Hwan. All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted thrvided that the following conditions
    are met:
    1. Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.
    2. Redistributions in binary form must rethrduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials thrvided with the distribution.

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

#ifndef _MIO_THR_H_
#define _MIO_THR_H_

#include <mio.h>

enum mio_dev_thr_sid_t
{
	MIO_DEV_THR_MASTER = -1, /* no io occurs on this. used only in on_close() */
	MIO_DEV_THR_IN     =  0, /* input to the thread */
	MIO_DEV_THR_OUT    =  1, /* output from the thread */
};
typedef enum mio_dev_thr_sid_t mio_dev_thr_sid_t;

typedef struct mio_dev_thr_t mio_dev_thr_t;
typedef struct mio_dev_thr_slave_t mio_dev_thr_slave_t;

typedef int (*mio_dev_thr_on_read_t) (
	mio_dev_thr_t*    dev,
	const void*       data,
	mio_iolen_t       len
);

typedef int (*mio_dev_thr_on_write_t) (
	mio_dev_thr_t*    dev,
	mio_iolen_t       wrlen,
	void*             wrctx
);

typedef void (*mio_dev_thr_on_close_t) (
	mio_dev_thr_t*    dev,
	mio_dev_thr_sid_t sid
);

struct mio_dev_thr_iopair_t
{
	mio_syshnd_t rfd;
	mio_syshnd_t wfd;
};
typedef struct mio_dev_thr_iopair_t mio_dev_thr_iopair_t;

typedef void (*mio_dev_thr_func_t) (
	mio_t*                mio,
	mio_dev_thr_iopair_t* iop,
	void*                 ctx
);

typedef struct mio_dev_thr_info_t mio_dev_thr_info_t;

struct mio_dev_thr_t
{
	MIO_DEV_HEADER;

	mio_dev_thr_slave_t* slave[2];
	int slave_count;

	mio_dev_thr_info_t* thr_info;

	mio_dev_thr_on_read_t on_read;
	mio_dev_thr_on_write_t on_write;
	mio_dev_thr_on_close_t on_close;
};

struct mio_dev_thr_slave_t
{
	MIO_DEV_HEADER;
	mio_dev_thr_sid_t id;
	mio_syshnd_t pfd;
	mio_dev_thr_t* master; /* parent device */
};

typedef struct mio_dev_thr_make_t mio_dev_thr_make_t;
struct mio_dev_thr_make_t
{
	mio_dev_thr_func_t thr_func;
	void* thr_ctx;
	mio_dev_thr_on_write_t on_write; /* mandatory */
	mio_dev_thr_on_read_t on_read; /* mandatory */
	mio_dev_thr_on_close_t on_close; /* optional */
};

enum mio_dev_thr_ioctl_cmd_t
{
	MIO_DEV_THR_CLOSE,
	MIO_DEV_THR_KILL_CHILD
};
typedef enum mio_dev_thr_ioctl_cmd_t mio_dev_thr_ioctl_cmd_t;

#ifdef __cplusplus
extern "C" {
#endif

MIO_EXPORT  mio_dev_thr_t* mio_dev_thr_make (
	mio_t*                    mio,
	mio_oow_t                 xtnsize,
	const mio_dev_thr_make_t* data
);

#if defined(MIO_HAVE_INLINE)
static MIO_INLINE mio_t* mio_dev_thr_getmio (mio_dev_thr_t* thr) { return mio_dev_getmio((mio_dev_t*)thr); }
#else
#	define mio_dev_thr_getmio(thr) mio_dev_getmio(thr)
#endif

#if defined(MIO_HAVE_INLINE)
static MIO_INLINE void* mio_dev_thr_getxtn (mio_dev_thr_t* thr) { return (void*)(thr + 1); }
#else
#	define mio_dev_thr_getxtn(thr) ((void*)(((mio_dev_thr_t*)thr) + 1))
#endif

MIO_EXPORT void mio_dev_thr_kill (
	mio_dev_thr_t* thr
);

MIO_EXPORT void mio_dev_thr_halt (
	mio_dev_thr_t* thr
);

MIO_EXPORT int mio_dev_thr_read (
	mio_dev_thr_t*     thr,
	int                enabled
);

MIO_EXPORT int mio_dev_thr_timedread (
	mio_dev_thr_t*     thr,
	int                enabled,
	const mio_ntime_t* tmout
);

MIO_EXPORT int mio_dev_thr_write (
	mio_dev_thr_t*     thr,
	const void*        data,
	mio_iolen_t        len,
	void*              wrctx
);

MIO_EXPORT int mio_dev_thr_timedwrite (
	mio_dev_thr_t*     thr,
	const void*        data,
	mio_iolen_t        len,
	const mio_ntime_t* tmout,
	void*              wrctx
);

MIO_EXPORT int mio_dev_thr_close (
	mio_dev_thr_t*     thr,
	mio_dev_thr_sid_t  sid
);

void mio_dev_thr_haltslave (
	mio_dev_thr_t*     dev,
	mio_dev_thr_sid_t  sid
);

#ifdef __cplusplus
}
#endif

#endif
