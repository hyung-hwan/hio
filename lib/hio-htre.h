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

#ifndef _HIO_HTRE_H_
#define _HIO_HTRE_H_

#include <hio-htb.h>
#include <hio-ecs.h>

/**
 * The hio_http_version_t type defines http version.
 */
struct hio_http_version_t
{
	short major; /**< major version */
	short minor; /**< minor version */
};

typedef struct hio_http_version_t hio_http_version_t;

/**
 * The hio_http_method_t type defines http methods .
 */
enum hio_http_method_t
{
	HIO_HTTP_OTHER,

	/* rfc 2616 */
	HIO_HTTP_HEAD,
	HIO_HTTP_GET,
	HIO_HTTP_POST,
	HIO_HTTP_PUT,
	HIO_HTTP_DELETE,
	HIO_HTTP_PATCH,
	HIO_HTTP_OPTIONS,
	HIO_HTTP_TRACE,
	HIO_HTTP_CONNECT

#if 0
	/* rfc 2518 */
	HIO_HTTP_PROPFIND,
	HIO_HTTP_PROPPATCH,
	HIO_HTTP_MKCOL,
	HIO_HTTP_COPY,
	HIO_HTTP_MOVE,
	HIO_HTTP_LOCK,
	HIO_HTTP_UNLOCK,

	/* rfc 3253 */
	HIO_HTTP_VERSION_CONTROL,
	HIO_HTTP_REPORT,
	HIO_HTTP_CHECKOUT,
	HIO_HTTP_CHECKIN,
	HIO_HTTP_UNCHECKOUT,
	HIO_HTTP_MKWORKSPACE,
	HIO_HTTP_UPDATE,
	HIO_HTTP_LABEL,
	HIO_HTTP_MERGE,
	HIO_HTTP_BASELINE_CONTROL,
	HIO_HTTP_MKACTIVITY,
	
	/* microsoft */
	HIO_HTTP_BPROPFIND,
	HIO_HTTP_BPROPPATCH,
	HIO_HTTP_BCOPY,
	HIO_HTTP_BDELETE,
	HIO_HTTP_BMOVE,
	HIO_HTTP_NOTIFY,
	HIO_HTTP_POLL,
	HIO_HTTP_SUBSCRIBE,
	HIO_HTTP_UNSUBSCRIBE,
#endif
};
typedef enum hio_http_method_t hio_http_method_t;

enum hio_http_status_t
{
	HIO_HTTP_STATUS_CONTINUE              = 100,
	HIO_HTTP_STATUS_SWITCH_PROTOCOL       = 101,

	HIO_HTTP_STATUS_OK                    = 200,
	HIO_HTTP_STATUS_CREATED               = 201,
	HIO_HTTP_STATUS_ACCEPTED              = 202,
	HIO_HTTP_STATUS_NON_AUTHORITATIVE     = 203,
	HIO_HTTP_STATUS_NO_CONTENT            = 204,
	HIO_HTTP_STATUS_RESET_CONTENT         = 205,
	HIO_HTTP_STATUS_PARTIAL_CONTENT       = 206,

	HIO_HTTP_STATUS_MOVED_PERMANENTLY     = 301,
	HIO_HTTP_STATUS_NOT_MODIFIED          = 304,

	HIO_HTTP_STATUS_BAD_REQUEST           = 400,
	HIO_HTTP_STATUS_FORBIDDEN             = 403,
	HIO_HTTP_STATUS_NOT_FOUND             = 404,
	HIO_HTTP_STATUS_METHOD_NOT_ALLOWED    = 405,
	HIO_HTTP_STATUS_LENGTH_REQUIRED       = 411,
	HIO_HTTP_STATUS_RANGE_NOT_SATISFIABLE = 416,
	HIO_HTTP_STATUS_EXPECTATION_FAILED    = 417,

	HIO_HTTP_STATUS_INTERNAL_SERVER_ERROR = 500,
};
typedef enum hio_http_status_t hio_http_status_t;

/* 
 * You should not manipulate an object of the #hio_htre_t 
 * type directly since it's complex. Use #hio_htrd_t to 
 * create an object of the hio_htre_t type.
 */

/* header and contents of request/response */
typedef struct hio_htre_t hio_htre_t;
typedef struct hio_htre_hdrval_t hio_htre_hdrval_t;

enum hio_htre_state_t
{
	HIO_HTRE_DISCARDED = (1 << 0), /** content has been discarded */
	HIO_HTRE_COMPLETED = (1 << 1)  /** complete content has been seen */
};
typedef enum hio_htre_state_t hio_htre_state_t;

typedef int (*hio_htre_concb_t) (
	hio_htre_t*        re,
	const hio_bch_t*   ptr,
	hio_oow_t          len,
	void*              ctx
);

struct hio_htre_hdrval_t
{
	const hio_bch_t*   ptr;
	hio_oow_t          len;
	hio_htre_hdrval_t* next;
};

struct hio_htre_t 
{
	hio_t* hio;

	enum
	{
		HIO_HTRE_Q,
		HIO_HTRE_S
	} type;

	/* version */
	hio_http_version_t version;
	const hio_bch_t* verstr; /* version string include HTTP/ */

	union
	{
		struct 
		{
			struct
			{
				hio_http_method_t type;
				const hio_bch_t* name;
				hio_oow_t len;
			} method;
			hio_bcs_t path;
			hio_bcs_t param;
			hio_bcs_t anchor;
		} q;
		struct
		{
			struct
			{
				int val;
				hio_bch_t* str;
			} code;
			hio_bch_t* mesg;
		} s;
	} u;

#define HIO_HTRE_ATTR_CHUNKED   (1 << 0)
#define HIO_HTRE_ATTR_LENGTH    (1 << 1)
#define HIO_HTRE_ATTR_KEEPALIVE (1 << 2)
#define HIO_HTRE_ATTR_EXPECT    (1 << 3)
#define HIO_HTRE_ATTR_EXPECT100 (1 << 4)
#define HIO_HTRE_ATTR_PROXIED   (1 << 5)
#define HIO_HTRE_QPATH_PERDEC   (1 << 6) /* the qpath has been percent-decoded */
	int flags;

	/* original query path for a request.
	 * meaningful if HIO_HTRE_QPATH_PERDEC is set in the flags */
	struct
	{
		hio_bch_t* buf; /* buffer pointer */
		hio_oow_t capa; /* buffer capacity */

		hio_bch_t* ptr;
		hio_oow_t len;
	} orgqpath;

	/* special attributes derived from the header */
	struct
	{
		hio_oow_t content_length;
		const hio_bch_t* status; /* for cgi */
	} attr;

	/* header table */
	hio_htb_t hdrtab;
	hio_htb_t trailers;
	
	/* content octets */
	hio_becs_t content;

	/* content callback */
	hio_htre_concb_t concb;
	void* concb_ctx;

	/* bitwise-ORed of hio_htre_state_t */
	int state;
};

#define hio_htre_getversion(re) (&((re)->version))
#define hio_htre_getmajorversion(re) ((re)->version.major)
#define hio_htre_getminorversion(re) ((re)->version.minor)
#define hio_htre_getverstr(re) ((re)->verstr)

#define hio_htre_getqmethodtype(re) ((re)->u.q.method.type)
#define hio_htre_getqmethodname(re) ((re)->u.q.method.name)
#define hio_htre_getqmethodlen(re) ((re)->u.q.method.len)

#define hio_htre_getqpath(re) ((re)->u.q.path.ptr)
#define hio_htre_getqpathlen(re) ((re)->u.q.path.len)
#define hio_htre_getqparam(re) ((re)->u.q.param.ptr)
#define hio_htre_getqparamlen(re) ((re)->u.q.param.len)
#define hio_htre_getqanchor(re) ((re)->u.q.anchor.ptr)
#define hio_htre_getqanchorlen(re) ((re)->u.q.anchor.len)
#define hio_htre_getorgqpath(re) ((re)->orgqpath.ptr)
#define hio_htre_getorgqpathlen(re) ((re)->orgqpath.ptr)

#define hio_htre_getscodeval(re) ((re)->u.s.code.val)
#define hio_htre_getscodestr(re) ((re)->u.s.code.str)
#define hio_htre_getsmesg(re) ((re)->u.s.mesg)

#define hio_htre_getcontent(re)     (&(re)->content)
#define hio_htre_getcontentbcs(re)  HIO_BECS_BCS(&(re)->content)
#define hio_htre_getcontentptr(re)  HIO_BECS_PTR(&(re)->content)
#define hio_htre_getcontentlen(re)  HIO_BECS_LEN(&(re)->content)

typedef int (*hio_htre_header_walker_t) (
	hio_htre_t*              re,
	const hio_bch_t*       key,
	const hio_htre_hdrval_t* val,
	void*                    ctx
);

#if defined(__cplusplus)
extern "C" {
#endif

HIO_EXPORT int hio_htre_init (
	hio_htre_t* re,
	hio_t*      hio
);

HIO_EXPORT void hio_htre_fini (
	hio_htre_t* re
);

HIO_EXPORT void hio_htre_clear (
	hio_htre_t* re
);

HIO_EXPORT const hio_htre_hdrval_t* hio_htre_getheaderval (
	const hio_htre_t*  re, 
	const hio_bch_t* key
);

HIO_EXPORT const hio_htre_hdrval_t* hio_htre_gettrailerval (
	const hio_htre_t*  re, 
	const hio_bch_t* key
);

HIO_EXPORT int hio_htre_walkheaders (
	hio_htre_t*              re,
	hio_htre_header_walker_t walker,
	void*                    ctx
);

HIO_EXPORT int hio_htre_walktrailers (
	hio_htre_t*              re,
	hio_htre_header_walker_t walker,
	void*                    ctx
);

/**
 * The hio_htre_addcontent() function adds a content semgnet pointed to by
 * @a ptr of @a len bytes to the content buffer. If @a re is already completed
 * or discarded, this function returns 0 without adding the segment to the 
 * content buffer. 
 * @return 1 on success, -1 on failure, 0 if adding is skipped.
 */
HIO_EXPORT int hio_htre_addcontent (
	hio_htre_t*        re,
	const hio_bch_t* ptr,
	hio_oow_t         len
);

HIO_EXPORT void hio_htre_completecontent (
	hio_htre_t*      re
);

HIO_EXPORT void hio_htre_discardcontent (
	hio_htre_t*      re
);

HIO_EXPORT void hio_htre_unsetconcb (
	hio_htre_t*      re
);

HIO_EXPORT void hio_htre_setconcb (
	hio_htre_t*      re,
	hio_htre_concb_t concb, 
	void*            ctx
);

HIO_EXPORT int hio_htre_perdecqpath (
	hio_htre_t*      req
);


HIO_EXPORT int hio_htre_getreqcontentlen (
	hio_htre_t*      req,
	hio_oow_t*       len
);

#if defined(__cplusplus)
}
#endif

#endif
