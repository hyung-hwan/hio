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

    THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR
    IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
    OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
    IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
    INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
    NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
    THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _MIO_HTTP_H_
#define _MIO_HTTP_H_

#include <mio-ecs.h>
#include <mio-sck.h>
#include <mio-htre.h>
#include <mio-thr.h>

/** \file
 * This file provides basic data types and functions for the http protocol.
 */

enum mio_http_range_type_t
{
	MIO_HTTP_RANGE_PROPER,
	MIO_HTTP_RANGE_PREFIX,
	MIO_HTTP_RANGE_SUFFIX
};
typedef enum mio_http_range_type_t mio_http_range_type_t;
/**
 * The mio_http_range_t type defines a structure that can represent
 * a value for the \b Range: http header. 
 *
 * If type is #MIO_HTTP_RANGE_PREFIX, 'to' is meaningless and 'from' indicates 
 * the number of bytes from the start. 
 *  - 500-    => from the 501st bytes all the way to the back.
 * 
 * If type is #MIO_HTTP_RANGE_SUFFIX, 'from' is meaningless and 'to' indicates 
 * the number of bytes from the back. 
 *  - -500    => last 500 bytes
 *
 * If type is #MIO_HTTP_RANGE_PROPER, 'from' and 'to' represents a proper range
 * where the value of 0 indicates the first byte. This doesn't require any 
 * adjustment.
 *  - 0-999   => first 1000 bytes
 *  - 99-     => from the 100th bytes to the end.
 */
struct mio_http_range_t
{
	mio_http_range_type_t type; /**< type indicator */
	mio_foff_t            from;  /**< starting offset */
	mio_foff_t            to;    /**< ending offset */
};
typedef struct mio_http_range_t mio_http_range_t;

enum mio_perenc_http_opt_t
{
	MIO_PERENC_HTTP_KEEP_SLASH = (1 << 0)
};
typedef enum mio_perenc_http_opt_t mio_perenc_bcstr_opt_t;


/* -------------------------------------------------------------- */
typedef struct mio_svc_htts_t mio_svc_htts_t;
typedef struct mio_svc_httc_t mio_svc_httc_t;

/* -------------------------------------------------------------- */

typedef struct mio_svc_htts_rsrc_t mio_svc_htts_rsrc_t;

typedef void (*mio_svc_htts_rsrc_on_kill_t) (
	mio_svc_htts_rsrc_t* rsrc
);

#define MIO_SVC_HTTS_RSRC_HEADER \
	mio_svc_htts_t* htts; \
	mio_oow_t rsrc_size; \
	mio_oow_t rsrc_refcnt; \
	mio_svc_htts_rsrc_on_kill_t rsrc_on_kill

struct mio_svc_htts_rsrc_t
{
	MIO_SVC_HTTS_RSRC_HEADER;
};

#define MIO_SVC_HTTS_RSRC_ATTACH(rsrc, var) do { (var) = (rsrc); ++(rsrc)->rsrc_refcnt; } while(0)
#define MIO_SVC_HTTS_RSRC_DETACH(rsrc_var) do { if (--(rsrc_var)->rsrc_refcnt == 0) { mio_svc_htts_rsrc_t* __rsrc_tmp = (rsrc_var); (rsrc_var) = MIO_NULL; mio_svc_htts_rsrc_kill(__rsrc_tmp); } else { (rsrc_var) = MIO_NULL; } } while(0)


/* -------------------------------------------------------------- */

typedef int (*mio_svc_htts_proc_req_t) (
	mio_svc_htts_t* htts,
	mio_dev_sck_t*  sck,
	mio_htre_t*     req
);

/* -------------------------------------------------------------- */
struct mio_svc_htts_thr_func_info_t
{
	mio_t*             mio;

	mio_http_method_t  req_method;
	mio_http_version_t req_version;
	mio_bch_t*         req_path;
	mio_bch_t*         req_param;
	int                req_x_http_method_override; /* -1 or mio_http_method_t */

	/* TODO: header table */

	mio_skad_t         client_addr;
	mio_skad_t         server_addr;
};
typedef struct mio_svc_htts_thr_func_info_t mio_svc_htts_thr_func_info_t;

typedef void (*mio_svc_htts_thr_func_t) (
	mio_t*                        mio,
	mio_dev_thr_iopair_t*         iop,
	mio_svc_htts_thr_func_info_t* info,
	void*                         ctx
);

/* -------------------------------------------------------------- */

#if defined(__cplusplus)
extern "C" {
#endif

MIO_EXPORT int mio_comp_http_versions (
	const mio_http_version_t* v1,
	const mio_http_version_t* v2
);

MIO_EXPORT int mio_comp_http_version_numbers (
	const mio_http_version_t* v1,
	int                       v2_major,
	int                       v2_minor
);

MIO_EXPORT const mio_bch_t* mio_http_status_to_bcstr (
	int code
);

MIO_EXPORT const mio_bch_t* mio_http_method_to_bcstr (
	mio_http_method_t type
);

MIO_EXPORT mio_http_method_t mio_bcstr_to_http_method (
	const mio_bch_t* name
);

MIO_EXPORT mio_http_method_t mio_bchars_to_http_method (
	const mio_bch_t* nameptr,
	mio_oow_t        namelen
);

MIO_EXPORT int mio_parse_http_range_bcstr (
	const mio_bch_t*  str,
	mio_http_range_t* range
);

MIO_EXPORT int mio_parse_http_time_bcstr (
	const mio_bch_t* str,
	mio_ntime_t*     nt
);

MIO_EXPORT mio_bch_t* mio_fmt_http_time_to_bcstr (
	const mio_ntime_t* nt,
	mio_bch_t*         buf,
	mio_oow_t          bufsz
);

/**
 * The mio_is_perenced_http_bcstr() function determines if the given string
 * contains a valid percent-encoded sequence.
 */
MIO_EXPORT int mio_is_perenced_http_bcstr (
	const mio_bch_t* str
);

/**
 * The mio_perdec_http_bcstr() function performs percent-decoding over a string.
 * The caller must ensure that the output buffer \a buf is large enough.
 * If \a ndecs is not #MIO_NULL, it is set to the number of characters
 * decoded.  0 means no characters in the input string required decoding
 * \return the length of the output string.
 */
MIO_EXPORT mio_oow_t mio_perdec_http_bcstr (
	const mio_bch_t* str, 
	mio_bch_t*       buf,
	mio_oow_t*       ndecs
);

/**
 * The mio_perdec_http_bcstr() function performs percent-decoding over a length-bound string.
 * It doesn't insert the terminating null.
 */
MIO_EXPORT mio_oow_t mio_perdec_http_bcs (
	const mio_bcs_t* str, 
	mio_bch_t*       buf,
	mio_oow_t*       ndecs
);

/**
 * The mio_perenc_http_bcstr() function performs percent-encoding over a string.
 * The caller must ensure that the output buffer \a buf is large enough.
 * If \a nencs is not #MIO_NULL, it is set to the number of characters
 * encoded.  0 means no characters in the input string required encoding.
 * \return the length of the output string.
 */
MIO_EXPORT mio_oow_t mio_perenc_http_bcstr (
	int              opt, /**< 0 or bitwise-OR'ed of #mio_perenc_http_bcstr_opt_t */
	const mio_bch_t* str, 
	mio_bch_t*       buf,
	mio_oow_t*       nencs
);

#if 0
/* TODO: rename this function according to the naming convension */
MIO_EXPORT mio_bch_t* mio_perenc_http_bcstrdup (
	int                opt, /**< 0 or bitwise-OR'ed of #mio_perenc_http_bcstr_opt_t */
	const mio_bch_t*   str, 
	mio_mmgr_t*        mmgr
);
#endif

MIO_EXPORT int mio_scan_http_qparam (
	mio_bch_t*      qparam,
	int (*qparamcb) (mio_bcs_t* key, mio_bcs_t* val, void* ctx),
	void*           ctx
);

/* ------------------------------------------------------------------------- */
/* HTTP SERVER SERVICE                                                       */
/* ------------------------------------------------------------------------- */

MIO_EXPORT mio_svc_htts_t* mio_svc_htts_start (
	mio_t*                   mio,
	const mio_skad_t*        bind_addr,
	mio_svc_htts_proc_req_t  proc_req
);

MIO_EXPORT void mio_svc_htts_stop (
	mio_svc_htts_t* htts
);

#if defined(MIO_HAVE_INLINE)
static MIO_INLINE mio_t* mio_svc_htts_getmio(mio_svc_htts_t* svc) { return mio_svc_getmio((mio_svc_t*)svc); }
#else
#	define mio_svc_htts_getmio(svc) mio_svc_getmio(svc)
#endif

MIO_EXPORT int mio_svc_htts_setservernamewithbcstr (
	mio_svc_htts_t*  htts,
	const mio_bch_t* server_name
);

MIO_EXPORT int mio_svc_htts_docgi (
	mio_svc_htts_t*  htts,
	mio_dev_sck_t*   csck,
	mio_htre_t*      req,
	const mio_bch_t* docroot,
	const mio_bch_t* script
);

MIO_EXPORT int mio_svc_htts_dofile (
	mio_svc_htts_t*  htts,
	mio_dev_sck_t*   csck,
	mio_htre_t*      req,
	const mio_bch_t* docroot,
	const mio_bch_t* script
);

MIO_EXPORT int mio_svc_htts_dothr (
	mio_svc_htts_t*         htts,
	mio_dev_sck_t*          csck,
	mio_htre_t*             req,
	mio_svc_htts_thr_func_t func,
	void*                   ctx
);

MIO_EXPORT int mio_svc_htts_dotxt (
	mio_svc_htts_t*     htts,
	mio_dev_sck_t*      csck,
	mio_htre_t*         req,
	int                 status_code,
	const mio_bch_t*    content_type,
	const mio_bch_t*    content_text
);

MIO_EXPORT mio_svc_htts_rsrc_t* mio_svc_htts_rsrc_make (
	mio_svc_htts_t*              htts,
	mio_oow_t                    rsrc_size,
	mio_svc_htts_rsrc_on_kill_t  on_kill
);

MIO_EXPORT void mio_svc_htts_rsrc_kill (
	mio_svc_htts_rsrc_t*         rsrc
);


MIO_EXPORT void mio_svc_htts_fmtgmtime (
	mio_svc_htts_t*    htts, 
	const mio_ntime_t* nt,
	mio_bch_t*         buf,
	mio_oow_t          len
);

MIO_EXPORT mio_bch_t* mio_svc_htts_dupmergepaths (
	mio_svc_htts_t*    htts,
	const mio_bch_t*   base,
	const mio_bch_t*   path
);

#if defined(__cplusplus)
}
#endif


#endif
