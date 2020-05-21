/*
 * $Id$
 *
    Copyright (c) 2016-2020 Chung, Hyung-Hwan. All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted pipevided that the following conditions
    are met:
    1. Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.
    2. Redistributions in binary form must repipeduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials pipevided with the distribution.

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

#ifndef _MIO_PIPE_H_
#define _MIO_PIPE_H_

#include <mio.h>

typedef struct mio_dev_pipe_t mio_dev_pipe_t;

typedef int (*mio_dev_pipe_on_read_t) (
	mio_dev_pipe_t*    dev,
	const void*        data,
	mio_iolen_t        len
);

typedef int (*mio_dev_pipe_on_write_t) (
	mio_dev_pipe_t*    dev,
	mio_iolen_t        wrlen,
	void*              wrctx
);

typedef void (*mio_dev_pipe_on_close_t) (
	mio_dev_pipe_t*    dev
);

struct mio_dev_pipe_t
{
	MIO_DEV_HEADER;

	int pfd[2];

	mio_dev_pipe_on_read_t on_read;
	mio_dev_pipe_on_write_t on_write;
	mio_dev_pipe_on_close_t on_close;
};

typedef struct mio_dev_pipe_make_t mio_dev_pipe_make_t;
struct mio_dev_pipe_make_t
{
	mio_dev_pipe_on_write_t on_write; /* mandatory */
	mio_dev_pipe_on_read_t on_read; /* mandatory */
	mio_dev_pipe_on_close_t on_close; /* optional */
};

enum mio_dev_pipe_ioctl_cmd_t
{
	MIO_DEV_PIPE_CLOSE,
	MIO_DEV_PIPE_KILL_CHILD
};
typedef enum mio_dev_pipe_ioctl_cmd_t mio_dev_pipe_ioctl_cmd_t;

#ifdef __cplusplus
extern "C" {
#endif

MIO_EXPORT  mio_dev_pipe_t* mio_dev_pipe_make (
	mio_t*                     mio,
	mio_oow_t                  xtnsize,
	const mio_dev_pipe_make_t* data
);

#if defined(MIO_HAVE_INLINE)
static MIO_INLINE mio_t* mio_dev_pipe_getmio (mio_dev_pipe_t* pipe) { return mio_dev_getmio((mio_dev_t*)pipe); }
#else
#	define mio_dev_pipe_getmio(pipe) mio_dev_getmio(pipe)
#endif

#if defined(MIO_HAVE_INLINE)
static MIO_INLINE void* mio_dev_pipe_getxtn (mio_dev_pipe_t* pipe) { return (void*)(pipe + 1); }
#else
#	define mio_dev_pipe_getxtn(pipe) ((void*)(((mio_dev_pipe_t*)pipe) + 1))
#endif

MIO_EXPORT void mio_dev_pipe_kill (
	mio_dev_pipe_t* pipe
);

MIO_EXPORT void mio_dev_pipe_halt (
	mio_dev_pipe_t* pipe
);

MIO_EXPORT int mio_dev_pipe_read (
	mio_dev_pipe_t*     pipe,
	int                 enabled
);

MIO_EXPORT int mio_dev_pipe_timedread (
	mio_dev_pipe_t*     pipe,
	int                 enabled,
	const mio_ntime_t*  tmout
);

MIO_EXPORT int mio_dev_pipe_write (
	mio_dev_pipe_t*     pipe,
	const void*         data,
	mio_iolen_t         len,
	void*               wrctx
);

MIO_EXPORT int mio_dev_pipe_timedwrite (
	mio_dev_pipe_t*     pipe,
	const void*         data,
	mio_iolen_t         len,
	const mio_ntime_t*  tmout,
	void*               wrctx
);

MIO_EXPORT int mio_dev_pipe_close (
	mio_dev_pipe_t*     pipe,
	mio_dev_pipe_sid_t  sid
);

#ifdef __cplusplus
}
#endif

#endif
