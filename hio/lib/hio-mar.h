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

#ifndef _HIO_MAR_H_
#define _HIO_MAR_H_

#include <hio.h>

typedef struct hio_dev_mar_t hio_dev_mar_t;

enum hio_dev_mar_progress_t
{
	HIO_DEV_MAR_INITIAL,
	HIO_DEV_MAR_CONNECTING,
	HIO_DEV_MAR_CONNECTED,
	HIO_DEV_MAR_QUERY_STARTING,
	HIO_DEV_MAR_QUERY_STARTED,
	HIO_DEV_MAR_ROW_FETCHING,
	HIO_DEV_MAR_ROW_FETCHED
};
typedef enum hio_dev_mar_progress_t hio_dev_mar_progress_t;

#define HIO_DEV_MAR_SET_PROGRESS(dev,value) ((dev)->progress = (value))
#define HIO_DEV_MAR_GET_PROGRESS(dev) ((dev)->progress)


typedef int (*hio_dev_mar_on_read_t) (
	hio_dev_mar_t*    dev,
	const void*       data,
	hio_iolen_t       len
);

typedef int (*hio_dev_mar_on_write_t) (
	hio_dev_mar_t*    dev,
	hio_iolen_t       wrlen,
	void*             wrctx
);

typedef void (*hio_dev_mar_on_connect_t) (
	hio_dev_mar_t*    dev
);

typedef void (*hio_dev_mar_on_disconnect_t) (
	hio_dev_mar_t*    dev
);

typedef void (*hio_dev_mar_on_query_started_t) (
	hio_dev_mar_t*    dev,
	int               mar_ret,
	const hio_bch_t*  mar_errmsg
);

typedef void (*hio_dev_mar_on_row_fetched_t) (
	hio_dev_mar_t*    dev,
	void*             row_data
);

struct hio_dev_mar_t
{
	HIO_DEV_HEADER;

	void* hnd;
	void* res;
	hio_dev_mar_progress_t progress;

	unsigned int connected: 1;
	unsigned int connected_deferred: 1;
	unsigned int query_started_deferred: 1;
	/*unsigned int query_started: 1;*/
	unsigned int row_fetched_deferred: 1;
	unsigned int broken: 1;
	hio_syshnd_t broken_syshnd;

	int row_wstatus;
	void* row;

	hio_dev_mar_on_read_t on_read;
	hio_dev_mar_on_write_t on_write;
	hio_dev_mar_on_connect_t on_connect;
	hio_dev_mar_on_disconnect_t on_disconnect;
	hio_dev_mar_on_query_started_t on_query_started;
	hio_dev_mar_on_row_fetched_t on_row_fetched;
};

enum hio_dev_mar_make_flag_t
{
	HIO_DEV_MAR_USE_TMOUT = (1 << 0)
};
typedef enum hio_dev_mar_make_flag_t hio_dev_mar_make_flag_t;


typedef struct hio_dev_mar_tmout_t hio_dev_mar_tmout_t;
struct hio_dev_mar_tmout_t
{
	hio_ntime_t c;
	hio_ntime_t r;
	hio_ntime_t w;
};

typedef struct hio_dev_mar_make_t hio_dev_mar_make_t;
struct hio_dev_mar_make_t
{
	int flags;
	hio_dev_mar_tmout_t tmout;
	const hio_bch_t* default_group;

	hio_dev_mar_on_write_t on_write; /* mandatory */
	hio_dev_mar_on_read_t on_read; /* mandatory */
	hio_dev_mar_on_connect_t on_connect; /* optional */
	hio_dev_mar_on_disconnect_t on_disconnect; /* optional */
	hio_dev_mar_on_query_started_t on_query_started;
	hio_dev_mar_on_row_fetched_t on_row_fetched;
};

typedef struct hio_dev_mar_connect_t hio_dev_mar_connect_t;
struct hio_dev_mar_connect_t
{
	const hio_bch_t* host;
	const hio_bch_t* username;
	const hio_bch_t* password;
	const hio_bch_t* dbname;
	hio_uint16_t port;
};

enum hio_dev_mar_ioctl_cmd_t
{
	HIO_DEV_MAR_CONNECT,
	HIO_DEV_MAR_QUERY_WITH_BCS,
	HIO_DEV_MAR_FETCH_ROW
};
typedef enum hio_dev_mar_ioctl_cmd_t hio_dev_mar_ioctl_cmd_t;


/* -------------------------------------------------------------- */

typedef struct hio_svc_marc_t hio_svc_marc_t;
typedef hio_dev_mar_connect_t hio_svc_marc_connect_t;
typedef hio_dev_mar_tmout_t hio_svc_marc_tmout_t;

enum hio_svc_marc_qtype_t
{
	HIO_SVC_MARC_QTYPE_SELECT, /* SELECT, SHOW, ... */
	HIO_SVC_MARC_QTYPE_ACTION /* UPDATE, INSERT, DELETE, ALTER ... */
};
typedef enum hio_svc_marc_qtype_t hio_svc_marc_qtype_t;

enum hio_svc_marc_rcode_t
{
	HIO_SVC_MARC_RCODE_ROW, /* has row *- data is MYSQL_ROW */
	HIO_SVC_MARC_RCODE_DONE, /* completed or no more row  - data is NULL */
	HIO_SVC_MARC_RCODE_ERROR /* query error - data is hio_sv_marc_dev_error_t* */
};
typedef enum hio_svc_marc_rcode_t hio_svc_marc_rcode_t;

enum hio_svc_marc_sid_flag_t
{
	HIO_SVC_MARC_SID_FLAG_AUTO_BOUNDED = ((hio_oow_t)1 << (HIO_SIZEOF_OOW_T - 1)),
	HIO_SVC_MARC_SID_FLAG_RESERVED_1 = ((hio_oow_t)1 << (HIO_SIZEOF_OOW_T - 2)),
	HIO_SVC_MARC_SID_FLAG_RESERVED_2 = ((hio_oow_t)1 << (HIO_SIZEOF_OOW_T - 3)),

	HIO_SVC_MARC_SID_FLAG_ALL = (HIO_SVC_MARC_SID_FLAG_AUTO_BOUNDED | HIO_SVC_MARC_SID_FLAG_RESERVED_1 | HIO_SVC_MARC_SID_FLAG_RESERVED_2)
};
typedef enum hio_svc_marc_sid_flag_t hio_svc_marc_sid_flag_t;

struct hio_svc_marc_dev_error_t
{
	int mar_errcode;
	const hio_bch_t* mar_errmsg;
};
typedef struct hio_svc_marc_dev_error_t hio_svc_marc_dev_error_t;

typedef void (*hio_svc_marc_on_result_t) (
	hio_svc_marc_t*      marc,
	hio_oow_t            sid,
	hio_svc_marc_rcode_t rcode,
	void*                data,
	void*                qctx
);


/* -------------------------------------------------------------- */

#if defined(__cplusplus)
extern "C" {
#endif

HIO_EXPORT  hio_dev_mar_t* hio_dev_mar_make (
	hio_t*                    hio,
	hio_oow_t                 xtnsize,
	const hio_dev_mar_make_t* data
);

#if defined(HIO_HAVE_INLINE)
static HIO_INLINE hio_t* hio_dev_mar_gethio (hio_dev_mar_t* mar) { return hio_dev_gethio((hio_dev_t*)mar); }
#else
#	define hio_dev_mar_gethio(mar) hio_dev_gethio(mar)
#endif

#if defined(HIO_HAVE_INLINE)
static HIO_INLINE void* hio_dev_mar_getxtn (hio_dev_mar_t* mar) { return (void*)(mar + 1); }
#else
#	define hio_dev_mar_getxtn(mar) ((void*)(((hio_dev_mar_t*)mar) + 1))
#endif


HIO_EXPORT int hio_dev_mar_connect (
	hio_dev_mar_t*         mar,
	hio_dev_mar_connect_t* ci
);

HIO_EXPORT int hio_dev_mar_querywithbchars (
	hio_dev_mar_t*       mar,
	const hio_bch_t*     qstr,
	hio_oow_t            qlen
);

HIO_EXPORT int hio_dev_mar_fetchrows (
	hio_dev_mar_t*       mar
);

#if defined(HIO_HAVE_INLINE)
static HIO_INLINE void hio_dev_mar_kill (hio_dev_mar_t* mar) { hio_dev_kill ((hio_dev_t*)mar); }
static HIO_INLINE void hio_dev_mar_halt (hio_dev_mar_t* mar) { hio_dev_halt ((hio_dev_t*)mar); }
#else
#	define hio_dev_mar_kill(mar) hio_dev_kill((hio_dev_t*)mar)
#	define hio_dev_mar_halt(mar) hio_dev_halt((hio_dev_t*)mar)
#endif

HIO_EXPORT hio_oow_t hio_dev_mar_escapebchars (
	hio_dev_mar_t*     dev,
	const hio_bch_t*   qstr,
	hio_oow_t          qlen,
	hio_bch_t*         buf
);

/* ------------------------------------------------------------------------- */
/* MARDB CLIENT SERVICE                                                    */
/* ------------------------------------------------------------------------- */

HIO_EXPORT hio_svc_marc_t* hio_svc_marc_start (
	hio_t*                        hio,
	const hio_svc_marc_connect_t* ci,
	const hio_svc_marc_tmout_t*   tmout,
	const hio_bch_t*              default_group
);

HIO_EXPORT void hio_svc_marc_stop (
	hio_svc_marc_t* marc
);

#if defined(HIO_HAVE_INLINE)
static HIO_INLINE hio_t* hio_svc_marc_gethio(hio_svc_marc_t* svc) { return hio_svc_gethio((hio_svc_t*)svc); }
#else
#	define hio_svc_marc_gethio(svc) hio_svc_gethio(svc)
#endif

HIO_EXPORT int hio_svc_marc_querywithbchars (
	hio_svc_marc_t*            marc,
	hio_oow_t                  flagged_sid,
	hio_svc_marc_qtype_t       qtype,
	const hio_bch_t*           qptr,
	hio_oow_t                  qlen,
	hio_svc_marc_on_result_t   on_result,
	void*                      qctx
);

HIO_EXPORT hio_oow_t hio_svc_marc_escapebchars (
	hio_svc_marc_t*     marc,
	const hio_bch_t*    qstr,
	hio_oow_t           qlen,
	hio_bch_t*          buf
);

#if defined(__cplusplus)
}
#endif

#endif
