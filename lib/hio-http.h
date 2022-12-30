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

#ifndef _HIO_HTTP_H_
#define _HIO_HTTP_H_

#include <hio-ecs.h>
#include <hio-sck.h>
#include <hio-htre.h>
#include <hio-thr.h>
#include <hio-fcgi.h>

/** \file
 * This file provides basic data types and functions for the http protocol.
 */

enum hio_http_range_type_t
{
	HIO_HTTP_RANGE_PROPER,
	HIO_HTTP_RANGE_PREFIX,
	HIO_HTTP_RANGE_SUFFIX
};
typedef enum hio_http_range_type_t hio_http_range_type_t;
/**
 * The hio_http_range_t type defines a structure that can represent
 * a value for the \b Range: http header. 
 *
 * If type is #HIO_HTTP_RANGE_PREFIX, 'to' is meaningless and 'from' indicates 
 * the number of bytes from the start. 
 *  - 500-    => from the 501st bytes all the way to the back.
 * 
 * If type is #HIO_HTTP_RANGE_SUFFIX, 'from' is meaningless and 'to' indicates 
 * the number of bytes from the back. 
 *  - -500    => last 500 bytes
 *
 * If type is #HIO_HTTP_RANGE_PROPER, 'from' and 'to' represents a proper range
 * where the value of 0 indicates the first byte. This doesn't require any 
 * adjustment.
 *  - 0-999   => first 1000 bytes
 *  - 99-     => from the 100th bytes to the end.
 */
struct hio_http_range_t
{
	hio_http_range_type_t type; /**< type indicator */
	hio_foff_t            from;  /**< starting offset */
	hio_foff_t            to;    /**< ending offset */
};
typedef struct hio_http_range_t hio_http_range_t;

enum hio_perenc_http_opt_t
{
	HIO_PERENC_HTTP_KEEP_SLASH = (1 << 0)
};
typedef enum hio_perenc_http_opt_t hio_perenc_bcstr_opt_t;


/* -------------------------------------------------------------- */
typedef struct hio_svc_htts_t hio_svc_htts_t;
typedef struct hio_svc_httc_t hio_svc_httc_t;

/* -------------------------------------------------------------- */

typedef struct hio_svc_htts_rsrc_t hio_svc_htts_rsrc_t;

typedef void (*hio_svc_htts_rsrc_on_kill_t) (
	hio_svc_htts_rsrc_t* rsrc
);

#define HIO_SVC_HTTS_RSRC_HEADER \
	hio_svc_htts_t* htts; \
	hio_oow_t rsrc_size; \
	hio_oow_t rsrc_refcnt; \
	hio_svc_htts_rsrc_on_kill_t rsrc_on_kill

struct hio_svc_htts_rsrc_t
{
	HIO_SVC_HTTS_RSRC_HEADER;
};

#define HIO_SVC_HTTS_RSRC_ATTACH(rsrc, var) do { (var) = (rsrc); ++(rsrc)->rsrc_refcnt; } while(0)
#define HIO_SVC_HTTS_RSRC_DETACH(rsrc_var) do { if (--(rsrc_var)->rsrc_refcnt == 0) { hio_svc_htts_rsrc_t* __rsrc_tmp = (rsrc_var); (rsrc_var) = HIO_NULL; hio_svc_htts_rsrc_kill(__rsrc_tmp); } else { (rsrc_var) = HIO_NULL; } } while(0)


/* -------------------------------------------------------------- */

typedef int (*hio_svc_htts_proc_req_t) (
	hio_svc_htts_t* htts,
	hio_dev_sck_t*  sck,
	hio_htre_t*     req
);

/* -------------------------------------------------------------- */
struct hio_svc_htts_thr_func_info_t
{
	hio_svc_htts_t*    htts;

	hio_http_method_t  req_method;
	hio_http_version_t req_version;
	hio_bch_t*         req_path;
	hio_bch_t*         req_param;
	int                req_x_http_method_override; /* -1 or hio_http_method_t */

	/* TODO: header table */

	hio_skad_t         client_addr;
	hio_skad_t         server_addr;
};
typedef struct hio_svc_htts_thr_func_info_t hio_svc_htts_thr_func_info_t;

typedef void (*hio_svc_htts_thr_func_t) (
	hio_t*                        hio,
	hio_dev_thr_iopair_t*         iop,
	hio_svc_htts_thr_func_info_t* tfi,
	void*                         ctx
);

/* -------------------------------------------------------------- */

enum hio_svc_htts_cgi_option_t
{
	HIO_SVC_HTTS_CGI_NO_100_CONTINUE = (1 << 0)
};

enum hio_svc_htts_file_option_t
{
	HIO_SVC_HTTS_FILE_NO_100_CONTINUE  = (1 << 0),
	HIO_SVC_HTTS_FILE_READ_ONLY        = (1 << 1),
	HIO_SVC_HTTS_FILE_LIST_DIR         = (1 << 2)
};

enum hio_svc_htts_thr_option_t
{
	HIO_SVC_HTTS_THR_NO_100_CONTINUE = (1 << 0)
};

#if 0
enum hio_svc_htts_txt_option_t
{
	/* no option yet */
};
#endif

enum hio_svc_htts_file_bfmt_dir_type_t
{
	HIO_SVC_HTTS_FILE_BFMT_DIR_HEADER,
	HIO_SVC_HTTS_FILE_BFMT_DIR_ENTRY,
	HIO_SVC_HTTS_FILE_BFMT_DIR_FOOTER
};
typedef enum hio_svc_htts_file_bfmt_dir_type_t hio_svc_htts_file_bfmt_dir_type_t;

struct hio_svc_htts_file_cbs_t
{
	int (*bfmt_dir) (hio_svc_htts_t* htts, int fd, const hio_bch_t* qpath, hio_svc_htts_file_bfmt_dir_type_t type, const hio_bch_t* name, void* ctx);
	void *ctx;
};
typedef struct hio_svc_htts_file_cbs_t hio_svc_htts_file_cbs_t;

#if defined(__cplusplus)
extern "C" {
#endif

HIO_EXPORT int hio_comp_http_versions (
	const hio_http_version_t* v1,
	const hio_http_version_t* v2
);

HIO_EXPORT int hio_comp_http_version_numbers (
	const hio_http_version_t* v1,
	int                       v2_major,
	int                       v2_minor
);

HIO_EXPORT const hio_bch_t* hio_http_status_to_bcstr (
	int code
);

HIO_EXPORT const hio_bch_t* hio_http_method_to_bcstr (
	hio_http_method_t type
);

HIO_EXPORT hio_http_method_t hio_bcstr_to_http_method (
	const hio_bch_t* name
);

HIO_EXPORT hio_http_method_t hio_bchars_to_http_method (
	const hio_bch_t* nameptr,
	hio_oow_t        namelen
);

HIO_EXPORT int hio_parse_http_range_bcstr (
	const hio_bch_t*  str,
	hio_http_range_t* range
);

HIO_EXPORT int hio_parse_http_time_bcstr (
	const hio_bch_t* str,
	hio_ntime_t*     nt
);

HIO_EXPORT hio_bch_t* hio_fmt_http_time_to_bcstr (
	const hio_ntime_t* nt,
	hio_bch_t*         buf,
	hio_oow_t          bufsz
);

/**
 * The hio_is_perenced_http_bcstr() function determines if the given string
 * contains a valid percent-encoded sequence.
 */
HIO_EXPORT int hio_is_perenced_http_bcstr (
	const hio_bch_t* str
);

/**
 * The hio_perdec_http_bcstr() function performs percent-decoding over a string.
 * The caller must ensure that the output buffer \a buf is large enough.
 * If \a ndecs is not #HIO_NULL, it is set to the number of characters
 * decoded.  0 means no characters in the input string required decoding
 * \return the length of the output string.
 */
HIO_EXPORT hio_oow_t hio_perdec_http_bcstr (
	const hio_bch_t* str, 
	hio_bch_t*       buf,
	hio_oow_t*       ndecs
);

/**
 * The hio_perdec_http_bcstr() function performs percent-decoding over a length-bound string.
 * It doesn't insert the terminating null.
 */
HIO_EXPORT hio_oow_t hio_perdec_http_bcs (
	const hio_bcs_t* str, 
	hio_bch_t*       buf,
	hio_oow_t*       ndecs
);

/**
 * The hio_perenc_http_bcstr() function performs percent-encoding over a string.
 * The caller must ensure that the output buffer \a buf is large enough.
 * If \a nencs is not #HIO_NULL, it is set to the number of characters
 * encoded.  0 means no characters in the input string required encoding.
 * \return the length of the output string.
 */
HIO_EXPORT hio_oow_t hio_perenc_http_bcstr (
	int              opt, /**< 0 or bitwise-OR'ed of #hio_perenc_http_bcstr_opt_t */
	const hio_bch_t* str, 
	hio_bch_t*       buf,
	hio_oow_t*       nencs
);

#if 0
/* TODO: rename this function according to the naming convension */
HIO_EXPORT hio_bch_t* hio_perenc_http_bcstrdup (
	int                opt, /**< 0 or bitwise-OR'ed of #hio_perenc_http_bcstr_opt_t */
	const hio_bch_t*   str, 
	hio_mmgr_t*        mmgr
);
#endif

HIO_EXPORT int hio_scan_http_qparam (
	hio_bch_t*      qparam,
	int (*qparamcb) (hio_bcs_t* key, hio_bcs_t* val, void* ctx),
	void*           ctx
);

/* ------------------------------------------------------------------------- */
/* HTTP SERVER SERVICE                                                       */
/* ------------------------------------------------------------------------- */

HIO_EXPORT hio_svc_htts_t* hio_svc_htts_start (
	hio_t*                    hio,
	hio_oow_t                 xtnsize,
	hio_dev_sck_bind_t*       binds,
	hio_oow_t                 nbinds,
	hio_svc_htts_proc_req_t   proc_req
);

HIO_EXPORT void hio_svc_htts_stop (
	hio_svc_htts_t* htts
);

HIO_EXPORT void* hio_svc_htts_getxtn (
	hio_svc_htts_t* htts
);

#if defined(HIO_HAVE_INLINE)
static HIO_INLINE hio_t* hio_svc_htts_gethio(hio_svc_htts_t* svc) { return hio_svc_gethio((hio_svc_t*)svc); }
#else
#	define hio_svc_htts_gethio(svc) hio_svc_gethio(svc)
#endif

HIO_EXPORT int hio_svc_htts_writetosidechan (
	hio_svc_htts_t* htts,
	hio_oow_t       idx, /* listener index */
	const void*     dptr,
	hio_oow_t       dlen
);

HIO_EXPORT int hio_svc_htts_setservernamewithbcstr (
	hio_svc_htts_t*  htts,
	const hio_bch_t* server_name
);

/* return the listening device at the given position.
 * not all devices may be up and running */
HIO_EXPORT hio_dev_sck_t* hio_svc_htts_getlistendev (
	hio_svc_htts_t* htts,
	hio_oow_t       idx
);

/* return the total number of listening devices requested to start.
 * not all devices may be up and running */
HIO_EXPORT hio_oow_t hio_sv_htts_getnlistendevs (
	hio_svc_htts_t* htts
);

HIO_EXPORT int hio_svc_htts_getsockaddr (
	hio_svc_htts_t*  htts,
	hio_oow_t        idx, /* listener index */
	hio_skad_t*      skad
);

HIO_EXPORT int hio_svc_htts_docgi (
	hio_svc_htts_t*  htts,
	hio_dev_sck_t*   csck,
	hio_htre_t*      req,
	const hio_bch_t* docroot,
	const hio_bch_t* script,
	int              options
);

HIO_EXPORT int hio_svc_htts_dofcgi (
	hio_svc_htts_t*   htts,
	hio_dev_sck_t*    csck,
	hio_htre_t*       req,
	const hio_skad_t* fcgis_addr,
	int               options /**< 0 or bitwise-Ored of #hio_svc_htts_file_option_t enumerators */
);

HIO_EXPORT int hio_svc_htts_dofile (
	hio_svc_htts_t*          htts,
	hio_dev_sck_t*           csck,
	hio_htre_t*              req,
	const hio_bch_t*         docroot,
	const hio_bch_t*         filepath,
	const hio_bch_t*         mime_type,
	int                      options,
	hio_svc_htts_file_cbs_t* cbs
);

HIO_EXPORT int hio_svc_htts_dothr (
	hio_svc_htts_t*         htts,
	hio_dev_sck_t*          csck,
	hio_htre_t*             req,
	hio_svc_htts_thr_func_t func,
	void*                   ctx,
	int                     options
);

HIO_EXPORT int hio_svc_htts_dotxt (
	hio_svc_htts_t*     htts,
	hio_dev_sck_t*      csck,
	hio_htre_t*         req,
	int                 status_code,
	const hio_bch_t*    content_type,
	const hio_bch_t*    content_text,
	int                 options
);

HIO_EXPORT hio_svc_htts_rsrc_t* hio_svc_htts_rsrc_make (
	hio_svc_htts_t*              htts,
	hio_oow_t                    rsrc_size,
	hio_svc_htts_rsrc_on_kill_t  on_kill
);

HIO_EXPORT void hio_svc_htts_rsrc_kill (
	hio_svc_htts_rsrc_t*         rsrc
);


HIO_EXPORT void hio_svc_htts_fmtgmtime (
	hio_svc_htts_t*    htts, 
	const hio_ntime_t* nt,
	hio_bch_t*         buf,
	hio_oow_t          len
);

HIO_EXPORT hio_bch_t* hio_svc_htts_dupmergepaths (
	hio_svc_htts_t*    htts,
	const hio_bch_t*   base,
	const hio_bch_t*   path
);

#if defined(__cplusplus)
}
#endif


#endif
