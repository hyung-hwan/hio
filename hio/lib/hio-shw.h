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
    IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
    OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
    IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
    INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
    NOT LIMITED TO, PIPECUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PIPEFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
    THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _HIO_SHW_H_
#define _HIO_SHW_H_

/* system handle wrapper - 
 *   turn a raw system handle/file descript to a device object
 */
#include <hio.h>

typedef struct hio_dev_shw_t hio_dev_shw_t;


typedef int (*hio_dev_shw_on_ready_t) (
	hio_dev_shw_t*     dev,
	int                events
);


typedef int (*hio_dev_shw_on_read_t) (
	hio_dev_shw_t*     dev,
	const void*        data,
	hio_iolen_t        len
);

typedef int (*hio_dev_shw_on_write_t) (
	hio_dev_shw_t*     dev,
	hio_iolen_t        wrlen,
	void*              wrctx
);

typedef void (*hio_dev_shw_on_close_t) (
	hio_dev_shw_t*    dev
);

struct hio_dev_shw_t
{
	HIO_DEV_HEADER;

	hio_syshnd_t hnd;
	int flags;

	hio_dev_shw_on_ready_t on_ready;
	hio_dev_shw_on_read_t on_read;
	hio_dev_shw_on_write_t on_write;
	hio_dev_shw_on_close_t on_close;
};

enum hio_dev_shw_make_flag_t
{
	HIO_DEV_SHW_KEEP_OPEN_ON_CLOSE = (1 << 0),
	HIO_DEV_SHW_DISABLE_OUT = (1 << 1),
	HIO_DEV_SHW_DISABLE_IN = (1 << 2),
	HIO_DEV_SHW_DISABLE_STREAM = (1 << 3)
};
typedef enum hio_dev_shw_make_flag_t hio_dev_shw_make_flag_t;

typedef struct hio_dev_shw_make_t hio_dev_shw_make_t;
struct hio_dev_shw_make_t
{
	hio_syshnd_t hnd;
	int flags; /**< bitwise-ORed of hio_dev_shw_make_flag_t enumerators */

	hio_dev_shw_on_write_t on_write; /* mandatory */
	hio_dev_shw_on_read_t on_read; /* mandatory */
	hio_dev_shw_on_close_t on_close; /* optional */
	hio_dev_shw_on_ready_t on_ready; /* optional */
};

#if defined(__cplusplus)
extern "C" {
#endif

HIO_EXPORT  hio_dev_shw_t* hio_dev_shw_make (
	hio_t*                     hio,
	hio_oow_t                  xtnsize,
	const hio_dev_shw_make_t*  data
);

#if defined(HIO_HAVE_INLINE)
static HIO_INLINE hio_t* hio_dev_shw_gethio (hio_dev_shw_t* shw) { return hio_dev_gethio((hio_dev_t*)shw); }
static HIO_INLINE void* hio_dev_shw_getxtn (hio_dev_shw_t* shw) { return (void*)(shw + 1); }
static HIO_INLINE hio_syshnd_t hio_dev_shw_getsyshnd (hio_dev_shw_t* shw) { return shw->hnd; }
#else
#	define hio_dev_shw_gethio(shw) hio_dev_gethio(shw)
#	define hio_dev_shw_getxtn(shw) ((void*)(((hio_dev_shw_t*)shw) + 1))
#	define hio_dev_shw_getsyshnd(shw) (((hio_dev_shw_t*)shw)->hnd)
#endif

HIO_EXPORT void hio_dev_shw_kill (
	hio_dev_shw_t* shw
);

HIO_EXPORT void hio_dev_shw_halt (
	hio_dev_shw_t* shw
);

HIO_EXPORT int hio_dev_shw_read (
	hio_dev_shw_t*      shw,
	int                 enabled
);

HIO_EXPORT int hio_dev_shw_timedread (
	hio_dev_shw_t*      shw,
	int                 enabled,
	const hio_ntime_t*  tmout
);

HIO_EXPORT int hio_dev_shw_write (
	hio_dev_shw_t*      shw,
	const void*         data,
	hio_iolen_t         len,
	void*               wrctx
);

HIO_EXPORT int hio_dev_shw_timedwrite (
	hio_dev_shw_t*      shw,
	const void*         data,
	hio_iolen_t         len,
	const hio_ntime_t*  tmout,
	void*               wrctx
);

#if defined(__cplusplus)
}
#endif

#endif
