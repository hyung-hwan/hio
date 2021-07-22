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

#ifndef _HIO_THR_H_
#define _HIO_THR_H_

#include <hio.h>

enum hio_dev_thr_sid_t
{
	HIO_DEV_THR_MASTER = -1, /* no io occurs on this. used only in on_close() */
	HIO_DEV_THR_IN     =  0, /* input to the thread */
	HIO_DEV_THR_OUT    =  1, /* output from the thread */
};
typedef enum hio_dev_thr_sid_t hio_dev_thr_sid_t;

typedef struct hio_dev_thr_t hio_dev_thr_t;
typedef struct hio_dev_thr_slave_t hio_dev_thr_slave_t;

typedef int (*hio_dev_thr_on_read_t) (
	hio_dev_thr_t*    dev,
	const void*       data,
	hio_iolen_t       len
);

typedef int (*hio_dev_thr_on_write_t) (
	hio_dev_thr_t*    dev,
	hio_iolen_t       wrlen,
	void*             wrctx
);

typedef void (*hio_dev_thr_on_close_t) (
	hio_dev_thr_t*    dev,
	hio_dev_thr_sid_t sid
);

struct hio_dev_thr_iopair_t
{
	hio_syshnd_t rfd;
	hio_syshnd_t wfd;
};
typedef struct hio_dev_thr_iopair_t hio_dev_thr_iopair_t;

typedef void (*hio_dev_thr_func_t) (
	hio_t*                hio,
	hio_dev_thr_iopair_t* iop,
	void*                 ctx
);

typedef struct hio_dev_thr_info_t hio_dev_thr_info_t;

struct hio_dev_thr_t
{
	HIO_DEV_HEADER;

	hio_dev_thr_slave_t* slave[2];
	int slave_count;

	hio_dev_thr_info_t* thr_info;

	hio_dev_thr_on_read_t on_read;
	hio_dev_thr_on_write_t on_write;
	hio_dev_thr_on_close_t on_close;
};

struct hio_dev_thr_slave_t
{
	HIO_DEV_HEADER;
	hio_dev_thr_sid_t id;
	hio_syshnd_t pfd;
	hio_dev_thr_t* master; /* parent device */
};

typedef struct hio_dev_thr_make_t hio_dev_thr_make_t;
struct hio_dev_thr_make_t
{
	hio_dev_thr_func_t thr_func;
	void* thr_ctx;
	hio_dev_thr_on_write_t on_write; /* mandatory */
	hio_dev_thr_on_read_t on_read; /* mandatory */
	hio_dev_thr_on_close_t on_close; /* optional */
};

enum hio_dev_thr_ioctl_cmd_t
{
	HIO_DEV_THR_CLOSE,
	HIO_DEV_THR_KILL_CHILD
};
typedef enum hio_dev_thr_ioctl_cmd_t hio_dev_thr_ioctl_cmd_t;

#ifdef __cplusplus
extern "C" {
#endif

HIO_EXPORT  hio_dev_thr_t* hio_dev_thr_make (
	hio_t*                    hio,
	hio_oow_t                 xtnsize,
	const hio_dev_thr_make_t* data
);

#if defined(HIO_HAVE_INLINE)
static HIO_INLINE hio_t* hio_dev_thr_gethio (hio_dev_thr_t* thr) { return hio_dev_gethio((hio_dev_t*)thr); }
#else
#	define hio_dev_thr_gethio(thr) hio_dev_gethio(thr)
#endif

#if defined(HIO_HAVE_INLINE)
static HIO_INLINE void* hio_dev_thr_getxtn (hio_dev_thr_t* thr) { return (void*)(thr + 1); }
#else
#	define hio_dev_thr_getxtn(thr) ((void*)(((hio_dev_thr_t*)thr) + 1))
#endif

HIO_EXPORT void hio_dev_thr_kill (
	hio_dev_thr_t* thr
);

HIO_EXPORT void hio_dev_thr_halt (
	hio_dev_thr_t* thr
);

HIO_EXPORT int hio_dev_thr_read (
	hio_dev_thr_t*     thr,
	int                enabled
);

HIO_EXPORT int hio_dev_thr_timedread (
	hio_dev_thr_t*     thr,
	int                enabled,
	const hio_ntime_t* tmout
);

HIO_EXPORT int hio_dev_thr_write (
	hio_dev_thr_t*     thr,
	const void*        data,
	hio_iolen_t        len,
	void*              wrctx
);

HIO_EXPORT int hio_dev_thr_timedwrite (
	hio_dev_thr_t*     thr,
	const void*        data,
	hio_iolen_t        len,
	const hio_ntime_t* tmout,
	void*              wrctx
);

HIO_EXPORT int hio_dev_thr_close (
	hio_dev_thr_t*     thr,
	hio_dev_thr_sid_t  sid
);

void hio_dev_thr_haltslave (
	hio_dev_thr_t*     dev,
	hio_dev_thr_sid_t  sid
);

#ifdef __cplusplus
}
#endif

#endif
