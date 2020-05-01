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

#ifndef _MIO_HTRE_H_
#define _MIO_HTRE_H_

#include <mio-http.h>
#include <mio-htb.h>

/* 
 * You should not manipulate an object of the #mio_htre_t 
 * type directly since it's complex. Use #mio_htrd_t to 
 * create an object of the mio_htre_t type.
 */

/* header and contents of request/response */
typedef struct mio_htre_t mio_htre_t;
typedef struct mio_htre_hdrval_t mio_htre_hdrval_t;

enum mio_htre_state_t
{
	MIO_HTRE_DISCARDED = (1 << 0), /** content has been discarded */
	MIO_HTRE_COMPLETED = (1 << 1)  /** complete content has been seen */
};
typedef enum mio_htre_state_t mio_htre_state_t;

typedef int (*mio_htre_concb_t) (
	mio_htre_t*        re,
	const mio_bch_t* ptr,
	mio_oow_t         len,
	void*              ctx
);

struct mio_htre_hdrval_t
{
	const mio_bch_t* ptr;
	mio_oow_t         len;
	mio_htre_hdrval_t* next;
};

struct mio_htre_t 
{
	mio_t* mio;

	enum
	{
		MIO_HTRE_Q,
		MIO_HTRE_S
	} type;

	/* version */
	mio_http_version_t version;
	const mio_bch_t* verstr; /* version string include HTTP/ */

	union
	{
		struct 
		{
			struct
			{
				mio_http_method_t type;
				const mio_bch_t* name;
			} method;
			mio_bcs_t path;
			mio_bcs_t param;
		} q;
		struct
		{
			struct
			{
				int val;
				mio_bch_t* str;
			} code;
			mio_bch_t* mesg;
		} s;
	} u;

#define MIO_HTRE_ATTR_CHUNKED   (1 << 0)
#define MIO_HTRE_ATTR_LENGTH    (1 << 1)
#define MIO_HTRE_ATTR_KEEPALIVE (1 << 2)
#define MIO_HTRE_ATTR_EXPECT    (1 << 3)
#define MIO_HTRE_ATTR_EXPECT100 (1 << 4)
#define MIO_HTRE_ATTR_PROXIED   (1 << 5)
#define MIO_HTRE_QPATH_PERDEC   (1 << 6) /* the qpath has been percent-decoded */
	int flags;

	/* original query path for a request.
	 * meaningful if MIO_HTRE_QPATH_PERDEC is set in the flags */
	struct
	{
		mio_bch_t* buf; /* buffer pointer */
		mio_oow_t capa; /* buffer capacity */

		mio_bch_t* ptr;
		mio_oow_t len;
	} orgqpath;

	/* special attributes derived from the header */
	struct
	{
		mio_oow_t content_length;
		const mio_bch_t* status; /* for cgi */
	} attr;

	/* header table */
	mio_htb_t hdrtab;
	mio_htb_t trailers;
	
	/* content octets */
	mio_becs_t content;

	/* content callback */
	mio_htre_concb_t concb;
	void* concb_ctx;

	/* bitwise-ORed of mio_htre_state_t */
	int state;
};

#define mio_htre_getversion(re) (&((re)->version))
#define mio_htre_getmajorversion(re) ((re)->version.major)
#define mio_htre_getminorversion(re) ((re)->version.minor)
#define mio_htre_getverstr(re) ((re)->verstr)

#define mio_htre_getqmethodtype(re) ((re)->u.q.method.type)
#define mio_htre_getqmethodname(re) ((re)->u.q.method.name)

#define mio_htre_getqpath(re) ((re)->u.q.path.ptr)
#define mio_htre_getqparam(re) ((re)->u.q.param.ptr)
#define mio_htre_getorgqpath(re) ((re)->orgqpath.ptr)

#define mio_htre_getscodeval(re) ((re)->u.s.code.val)
#define mio_htre_getscodestr(re) ((re)->u.s.code.str)
#define mio_htre_getsmesg(re) ((re)->u.s.mesg)

#define mio_htre_getcontent(re)     (&(re)->content)
#define mio_htre_getcontentxstr(re) MIO_MBS_XSTR(&(re)->content)
#define mio_htre_getcontentcstr(re) MIO_MBS_CSTR(&(re)->content)
#define mio_htre_getcontentptr(re)  MIO_MBS_PTR(&(re)->content)
#define mio_htre_getcontentlen(re)  MIO_MBS_LEN(&(re)->content)

typedef int (*mio_htre_header_walker_t) (
	mio_htre_t*              re,
	const mio_bch_t*       key,
	const mio_htre_hdrval_t* val,
	void*                    ctx
);

#if defined(__cplusplus)
extern "C" {
#endif

MIO_EXPORT int mio_htre_init (
	mio_htre_t* re,
	mio_t*      mio
);

MIO_EXPORT void mio_htre_fini (
	mio_htre_t* re
);

MIO_EXPORT void mio_htre_clear (
	mio_htre_t* re
);

MIO_EXPORT const mio_htre_hdrval_t* mio_htre_getheaderval (
	const mio_htre_t*  re, 
	const mio_bch_t* key
);

MIO_EXPORT const mio_htre_hdrval_t* mio_htre_gettrailerval (
	const mio_htre_t*  re, 
	const mio_bch_t* key
);

MIO_EXPORT int mio_htre_walkheaders (
	mio_htre_t*              re,
	mio_htre_header_walker_t walker,
	void*                    ctx
);

MIO_EXPORT int mio_htre_walktrailers (
	mio_htre_t*              re,
	mio_htre_header_walker_t walker,
	void*                    ctx
);

/**
 * The mio_htre_addcontent() function adds a content semgnet pointed to by
 * @a ptr of @a len bytes to the content buffer. If @a re is already completed
 * or discarded, this function returns 0 without adding the segment to the 
 * content buffer. 
 * @return 1 on success, -1 on failure, 0 if adding is skipped.
 */
MIO_EXPORT int mio_htre_addcontent (
	mio_htre_t*        re,
	const mio_bch_t* ptr,
	mio_oow_t         len
);

MIO_EXPORT void mio_htre_completecontent (
	mio_htre_t*      re
);

MIO_EXPORT void mio_htre_discardcontent (
	mio_htre_t*      re
);

MIO_EXPORT void mio_htre_unsetconcb (
	mio_htre_t*      re
);

MIO_EXPORT void mio_htre_setconcb (
	mio_htre_t*      re,
	mio_htre_concb_t concb, 
	void*            ctx
);

MIO_EXPORT int mio_htre_perdecqpath (
	mio_htre_t*      req
);

#if defined(__cplusplus)
}
#endif

#endif
