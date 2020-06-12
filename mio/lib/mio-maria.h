/*
 * $Id$
 *
    Copyright (c) 2016-2020 Chung, Hyung-Hwan. All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted mariavided that the following conditions
    are met:
    1. Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.
    2. Redistributions in binary form must remariaduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials mariavided with the distribution.

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

typedef struct mio_dev_maria_t mio_dev_maria_t;

enum mio_dev_maria_state_t
{
	/* the following items(progress bits) are mutually exclusive */
	MIO_DEV_MARIA_CONNECTING      = (1 << 0),
	MIO_DEV_MARIA_CONNECTED       = (1 << 1),
	MIO_DEV_MARIA_QUERY_STARTING  = (1 << 2),
	MIO_DEV_MARIA_QUERY_STARTED   = (1 << 3),
	MIO_DEV_MARIA_ROW_FETCHING    = (1 << 4),
	MIO_DEV_MARIA_ROW_FETCHED     = (1 << 5),


#if 0
	/* the following items can be bitwise-ORed with an exclusive item above */
	MIO_DEV_MARIA_LENIENT        = (1 << 14),
	MIO_DEV_MARIA_INTERCEPTED    = (1 << 15),
#endif

	/* convenience bit masks */
	MIO_DEV_MARIA_ALL_PROGRESS_BITS = (MIO_DEV_MARIA_CONNECTING |
	                                   MIO_DEV_MARIA_CONNECTED |
	                                   MIO_DEV_MARIA_QUERY_STARTING |
	                                   MIO_DEV_MARIA_QUERY_STARTED |
	                                   MIO_DEV_MARIA_ROW_FETCHING |
	                                   MIO_DEV_MARIA_ROW_FETCHED)
};
typedef enum mio_dev_maria_state_t mio_dev_maria_state_t;

#define MIO_DEV_MARIA_SET_PROGRESS(dev,bit) do { \
	(dev)->state &= ~MIO_DEV_MARIA_ALL_PROGRESS_BITS; \
	(dev)->state |= (bit); \
} while(0)

#define MIO_DEV_MARIA_GET_PROGRESS(dev) ((dev)->state & MIO_DEV_MARIA_ALL_PROGRESS_BITS)


typedef int (*mio_dev_maria_on_read_t) (
	mio_dev_maria_t*    dev,
	const void*         data,
	mio_iolen_t         len
);

typedef int (*mio_dev_maria_on_write_t) (
	mio_dev_maria_t*    dev,
	mio_iolen_t         wrlen,
	void*               wrctx
);

typedef void (*mio_dev_maria_on_connect_t) (
	mio_dev_maria_t*    dev
);

typedef void (*mio_dev_maria_on_disconnect_t) (
	mio_dev_maria_t*    dev
);

typedef void (*mio_dev_maria_on_query_started_t) (
	mio_dev_maria_t*    dev,
	int                 maria_ret
);

typedef void (*mio_dev_maria_on_row_fetched_t) (
	mio_dev_maria_t*    dev,
	void*               row_data
);

struct mio_dev_maria_t
{
	MIO_DEV_HEADER;

	void* hnd;
	void* res;
	int state;

	unsigned int connected;
	unsigned int query_started;
	unsigned int row_fetched;

	int query_ret;
	int row_wstatus;
	void* row;

	mio_dev_maria_on_read_t on_read;
	mio_dev_maria_on_write_t on_write;
	mio_dev_maria_on_connect_t on_connect;
	mio_dev_maria_on_disconnect_t on_disconnect;
	mio_dev_maria_on_query_started_t on_query_started;
	mio_dev_maria_on_row_fetched_t on_row_fetched;
};

typedef struct mio_dev_maria_make_t mio_dev_maria_make_t;
struct mio_dev_maria_make_t
{
	mio_dev_maria_on_write_t on_write; /* mandatory */
	mio_dev_maria_on_read_t on_read; /* mandatory */
	mio_dev_maria_on_connect_t on_connect; /* optional */
	mio_dev_maria_on_disconnect_t on_disconnect; /* optional */
	mio_dev_maria_on_query_started_t on_query_started;
	mio_dev_maria_on_row_fetched_t on_row_fetched;
};

typedef struct mio_dev_maria_connect_t mio_dev_maria_connect_t;
struct mio_dev_maria_connect_t
{
	const mio_bch_t* host;
	const mio_bch_t* username;
	const mio_bch_t* password;
	const mio_bch_t* dbname;
	mio_uint16_t port;
};

enum mio_dev_maria_ioctl_cmd_t
{
	MIO_DEV_MARIA_CONNECT,
	MIO_DEV_MARIA_QUERY_WITH_BCS,
	MIO_DEV_MARIA_FETCH_ROW
};
typedef enum mio_dev_maria_ioctl_cmd_t mio_dev_maria_ioctl_cmd_t;

#ifdef __cplusplus
extern "C" {
#endif

MIO_EXPORT  mio_dev_maria_t* mio_dev_maria_make (
	mio_t*                      mio,
	mio_oow_t                   xtnsize,
	const mio_dev_maria_make_t* data
);

#if defined(MIO_HAVE_INLINE)
static MIO_INLINE mio_t* mio_dev_maria_getmio (mio_dev_maria_t* maria) { return mio_dev_getmio((mio_dev_t*)maria); }
#else
#	define mio_dev_maria_getmio(maria) mio_dev_getmio(maria)
#endif

#if defined(MIO_HAVE_INLINE)
static MIO_INLINE void* mio_dev_maria_getxtn (mio_dev_maria_t* maria) { return (void*)(maria + 1); }
#else
#	define mio_dev_maria_getxtn(maria) ((void*)(((mio_dev_maria_t*)maria) + 1))
#endif


MIO_EXPORT int mio_dev_maria_connect (
	mio_dev_maria_t*     maria,
	mio_dev_maria_connect_t* ci
);

MIO_EXPORT int mio_dev_maria_querywithbchars (
	mio_dev_maria_t*     maria,
	const mio_bch_t*     qstr,
	mio_oow_t            qlen
);

MIO_EXPORT int mio_dev_maria_fetchrows (
	mio_dev_maria_t*     maria
);

#if defined(MIO_HAVE_INLINE)
static MIO_INLINE void mio_dev_maria_kill (mio_dev_maria_t* maria) { mio_dev_kill ((mio_dev_t*)maria); }
static MIO_INLINE void mio_dev_maria_halt (mio_dev_maria_t* maria) { mio_dev_halt ((mio_dev_t*)maria); }
#else
#	define mio_dev_maria_kill(maria) mio_dev_kill((mio_dev_t*)maria)
#	define mio_dev_maria_halt(maria) mio_dev_halt((mio_dev_t*)maria)
#endif


#ifdef __cplusplus
}
#endif

#endif
