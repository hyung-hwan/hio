/*
 * $Id$
 *
    Copyright (c) 2016-2020 Chung, Hyung-Hwan. All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions
    are met:
    1. Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright
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

#ifndef _MIO_MAR_H_
#define _MIO_MAR_H_

#include <mio.h>

typedef struct mio_dev_mar_t mio_dev_mar_t;

enum mio_dev_mar_state_t
{
	/* the following items(progress bits) are mutually exclusive */
	MIO_DEV_MAR_CONNECTING      = (1 << 0),
	MIO_DEV_MAR_CONNECTED       = (1 << 1),
	MIO_DEV_MAR_QUERY_STARTING  = (1 << 2),
	MIO_DEV_MAR_QUERY_STARTED   = (1 << 3),
	MIO_DEV_MAR_ROW_FETCHING    = (1 << 4),
	MIO_DEV_MAR_ROW_FETCHED     = (1 << 5),


#if 0
	/* the following items can be bitwise-ORed with an exclusive item above */
	MIO_DEV_MAR_LENIENT        = (1 << 14),
	MIO_DEV_MAR_INTERCEPTED    = (1 << 15),
#endif

	/* convenience bit masks */
	MIO_DEV_MAR_ALL_PROGRESS_BITS = (MIO_DEV_MAR_CONNECTING |
	                                   MIO_DEV_MAR_CONNECTED |
	                                   MIO_DEV_MAR_QUERY_STARTING |
	                                   MIO_DEV_MAR_QUERY_STARTED |
	                                   MIO_DEV_MAR_ROW_FETCHING |
	                                   MIO_DEV_MAR_ROW_FETCHED)
};
typedef enum mio_dev_mar_state_t mio_dev_mar_state_t;

#define MIO_DEV_MAR_SET_PROGRESS(dev,bit) do { \
	(dev)->state &= ~MIO_DEV_MAR_ALL_PROGRESS_BITS; \
	(dev)->state |= (bit); \
} while(0)

#define MIO_DEV_MAR_GET_PROGRESS(dev) ((dev)->state & MIO_DEV_MAR_ALL_PROGRESS_BITS)


typedef int (*mio_dev_mar_on_read_t) (
	mio_dev_mar_t*    dev,
	const void*         data,
	mio_iolen_t         len
);

typedef int (*mio_dev_mar_on_write_t) (
	mio_dev_mar_t*    dev,
	mio_iolen_t         wrlen,
	void*               wrctx
);

typedef void (*mio_dev_mar_on_connect_t) (
	mio_dev_mar_t*    dev
);

typedef void (*mio_dev_mar_on_disconnect_t) (
	mio_dev_mar_t*    dev
);

typedef void (*mio_dev_mar_on_query_started_t) (
	mio_dev_mar_t*    dev,
	int                 mar_ret
);

typedef void (*mio_dev_mar_on_row_fetched_t) (
	mio_dev_mar_t*    dev,
	void*               row_data
);

struct mio_dev_mar_t
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

	mio_dev_mar_on_read_t on_read;
	mio_dev_mar_on_write_t on_write;
	mio_dev_mar_on_connect_t on_connect;
	mio_dev_mar_on_disconnect_t on_disconnect;
	mio_dev_mar_on_query_started_t on_query_started;
	mio_dev_mar_on_row_fetched_t on_row_fetched;
};

typedef struct mio_dev_mar_make_t mio_dev_mar_make_t;
struct mio_dev_mar_make_t
{
	mio_dev_mar_on_write_t on_write; /* mandatory */
	mio_dev_mar_on_read_t on_read; /* mandatory */
	mio_dev_mar_on_connect_t on_connect; /* optional */
	mio_dev_mar_on_disconnect_t on_disconnect; /* optional */
	mio_dev_mar_on_query_started_t on_query_started;
	mio_dev_mar_on_row_fetched_t on_row_fetched;
};

typedef struct mio_dev_mar_connect_t mio_dev_mar_connect_t;
struct mio_dev_mar_connect_t
{
	const mio_bch_t* host;
	const mio_bch_t* username;
	const mio_bch_t* password;
	const mio_bch_t* dbname;
	mio_uint16_t port;
};

enum mio_dev_mar_ioctl_cmd_t
{
	MIO_DEV_MAR_CONNECT,
	MIO_DEV_MAR_QUERY_WITH_BCS,
	MIO_DEV_MAR_FETCH_ROW
};
typedef enum mio_dev_mar_ioctl_cmd_t mio_dev_mar_ioctl_cmd_t;


/* -------------------------------------------------------------- */

typedef struct mio_svc_marc_t mio_svc_marc_t;
typedef mio_dev_mar_connect_t mio_svc_marc_connect_t;

typedef void (*mio_svc_marc_on_row_fetched) (
	mio_svc_marc_t* marc,
	mio_oow_t       sid,
	void*           data,
	void*           qctx
);

/* -------------------------------------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

MIO_EXPORT  mio_dev_mar_t* mio_dev_mar_make (
	mio_t*                    mio,
	mio_oow_t                 xtnsize,
	const mio_dev_mar_make_t* data
);

#if defined(MIO_HAVE_INLINE)
static MIO_INLINE mio_t* mio_dev_mar_getmio (mio_dev_mar_t* mar) { return mio_dev_getmio((mio_dev_t*)mar); }
#else
#	define mio_dev_mar_getmio(mar) mio_dev_getmio(mar)
#endif

#if defined(MIO_HAVE_INLINE)
static MIO_INLINE void* mio_dev_mar_getxtn (mio_dev_mar_t* mar) { return (void*)(mar + 1); }
#else
#	define mio_dev_mar_getxtn(mar) ((void*)(((mio_dev_mar_t*)mar) + 1))
#endif


MIO_EXPORT int mio_dev_mar_connect (
	mio_dev_mar_t*         mar,
	mio_dev_mar_connect_t* ci
);

MIO_EXPORT int mio_dev_mar_querywithbchars (
	mio_dev_mar_t*       mar,
	const mio_bch_t*     qstr,
	mio_oow_t            qlen
);

MIO_EXPORT int mio_dev_mar_fetchrows (
	mio_dev_mar_t*       mar
);

#if defined(MIO_HAVE_INLINE)
static MIO_INLINE void mio_dev_mar_kill (mio_dev_mar_t* mar) { mio_dev_kill ((mio_dev_t*)mar); }
static MIO_INLINE void mio_dev_mar_halt (mio_dev_mar_t* mar) { mio_dev_halt ((mio_dev_t*)mar); }
#else
#	define mio_dev_mar_kill(mar) mio_dev_kill((mio_dev_t*)mar)
#	define mio_dev_mar_halt(mar) mio_dev_halt((mio_dev_t*)mar)
#endif


/* ------------------------------------------------------------------------- */
/* MARDB CLIENT SERVICE                                                    */
/* ------------------------------------------------------------------------- */

MIO_EXPORT mio_svc_marc_t* mio_svc_marc_start (
	mio_t*                        mio,
	const mio_svc_marc_connect_t* ci
);

MIO_EXPORT void mio_svc_marc_stop (
	mio_svc_marc_t* marc
);

#if defined(MIO_HAVE_INLINE)
static MIO_INLINE mio_t* mio_svc_marc_getmio(mio_svc_marc_t* svc) { return mio_svc_getmio((mio_svc_t*)svc); }
#else
#       define mio_svc_marc_getmio(svc) mio_svc_getmio(svc)
#endif


MIO_EXPORT int mio_svc_mar_querywithbchars (
	mio_svc_marc_t*   marc,
	mio_oow_t         sid,
	const mio_bch_t*  qptr,
	mio_oow_t         qlen,
	void*             qctx
);

#ifdef __cplusplus
}
#endif

#endif
