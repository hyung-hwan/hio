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

#ifndef _HIO_PIPE_H_
#define _HIO_PIPE_H_

#include <hio.h>

enum hio_dev_pipe_sid_t
{
	HIO_DEV_PIPE_MASTER = -1, /* no io occurs on this. used only in on_close() */
	HIO_DEV_PIPE_IN     =  0, /* input */
	HIO_DEV_PIPE_OUT    =  1 /* output */
};
typedef enum hio_dev_pipe_sid_t hio_dev_pipe_sid_t;

typedef struct hio_dev_pipe_t hio_dev_pipe_t;
typedef struct hio_dev_pipe_slave_t hio_dev_pipe_slave_t;

typedef int (*hio_dev_pipe_on_read_t) (
	hio_dev_pipe_t*    dev,
	const void*        data,
	hio_iolen_t        len
);

typedef int (*hio_dev_pipe_on_write_t) (
	hio_dev_pipe_t*    dev,
	hio_iolen_t        wrlen,
	void*              wrctx
);

typedef void (*hio_dev_pipe_on_close_t) (
	hio_dev_pipe_t*    dev,
	hio_dev_pipe_sid_t sid
);

struct hio_dev_pipe_t
{
	HIO_DEV_HEADER;

	hio_dev_pipe_slave_t* slave[2];
	int slave_count;

	hio_dev_pipe_on_read_t on_read;
	hio_dev_pipe_on_write_t on_write;
	hio_dev_pipe_on_close_t on_close;
};


struct hio_dev_pipe_slave_t
{
	HIO_DEV_HEADER;
	hio_dev_pipe_sid_t id;
	hio_syshnd_t pfd;
	hio_dev_pipe_t* master; /* parent device */
};

typedef struct hio_dev_pipe_make_t hio_dev_pipe_make_t;
struct hio_dev_pipe_make_t
{
	hio_dev_pipe_on_write_t on_write; /* mandatory */
	hio_dev_pipe_on_read_t on_read; /* mandatory */
	hio_dev_pipe_on_close_t on_close; /* optional */
};

enum hio_dev_pipe_ioctl_cmd_t
{
	HIO_DEV_PIPE_CLOSE
};
typedef enum hio_dev_pipe_ioctl_cmd_t hio_dev_pipe_ioctl_cmd_t;

#ifdef __cplusplus
extern "C" {
#endif

HIO_EXPORT  hio_dev_pipe_t* hio_dev_pipe_make (
	hio_t*                     hio,
	hio_oow_t                  xtnsize,
	const hio_dev_pipe_make_t* data
);

#if defined(HIO_HAVE_INLINE)
static HIO_INLINE hio_t* hio_dev_pipe_gethio (hio_dev_pipe_t* pipe) { return hio_dev_gethio((hio_dev_t*)pipe); }
#else
#	define hio_dev_pipe_gethio(pipe) hio_dev_gethio(pipe)
#endif

#if defined(HIO_HAVE_INLINE)
static HIO_INLINE void* hio_dev_pipe_getxtn (hio_dev_pipe_t* pipe) { return (void*)(pipe + 1); }
#else
#	define hio_dev_pipe_getxtn(pipe) ((void*)(((hio_dev_pipe_t*)pipe) + 1))
#endif

HIO_EXPORT void hio_dev_pipe_kill (
	hio_dev_pipe_t* pipe
);

HIO_EXPORT void hio_dev_pipe_halt (
	hio_dev_pipe_t* pipe
);

HIO_EXPORT int hio_dev_pipe_read (
	hio_dev_pipe_t*     pipe,
	int                 enabled
);

HIO_EXPORT int hio_dev_pipe_timedread (
	hio_dev_pipe_t*     pipe,
	int                 enabled,
	const hio_ntime_t*  tmout
);

HIO_EXPORT int hio_dev_pipe_write (
	hio_dev_pipe_t*     pipe,
	const void*         data,
	hio_iolen_t         len,
	void*               wrctx
);

HIO_EXPORT int hio_dev_pipe_timedwrite (
	hio_dev_pipe_t*     pipe,
	const void*         data,
	hio_iolen_t         len,
	const hio_ntime_t*  tmout,
	void*               wrctx
);

HIO_EXPORT int hio_dev_pipe_close (
	hio_dev_pipe_t*     pipe,
	hio_dev_pipe_sid_t  sid
);

#ifdef __cplusplus
}
#endif

#endif
