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

#include <hio-htre.h>
#include <hio-http.h>
#include "hio-prv.h"

static void free_hdrval (hio_htb_t* htb, void* vptr, hio_oow_t vlen)
{
	hio_htre_hdrval_t* val;
	hio_htre_hdrval_t* tmp;

	val = vptr;
	while (val)
	{
		tmp = val;
		val = val->next;
		hio_freemem (htb->hio, tmp);
	}
}

int hio_htre_init (hio_htre_t* re, hio_t* hio)
{
	static hio_htb_style_t style =
	{
		{
			HIO_HTB_COPIER_DEFAULT,
			HIO_HTB_COPIER_DEFAULT
		},
		{
			HIO_HTB_FREEER_DEFAULT,
			free_hdrval
		},
		HIO_HTB_COMPER_DEFAULT,
		HIO_HTB_KEEPER_DEFAULT,
		HIO_HTB_SIZER_DEFAULT,
		HIO_HTB_HASHER_DEFAULT
	};

	HIO_MEMSET (re, 0, HIO_SIZEOF(*re));
	re->hio = hio;

	if (hio_htb_init(&re->hdrtab, hio, 60, 70, 1, 1) <= -1) return -1;
	if (hio_htb_init(&re->trailers, hio, 20, 70, 1, 1) <= -1) return -1;

	hio_htb_setstyle (&re->hdrtab, &style);
	hio_htb_setstyle (&re->trailers, &style);

	hio_becs_init (&re->content, hio, 0);
#if 0
	hio_becs_init (&re->iniline, hio, 0);
#endif

	return 0;
}

void hio_htre_fini (hio_htre_t* re)
{
#if 0
	hio_becs_fini (&re->iniline);
#endif
	hio_becs_fini (&re->content);
	hio_htb_fini (&re->trailers);
	hio_htb_fini (&re->hdrtab);

	if (re->orgqpath.buf) 
	{
		hio_freemem (re->hio, re->orgqpath.buf);
		re->orgqpath.buf = HIO_NULL;
		re->orgqpath.capa = 0;
		re->orgqpath.ptr = HIO_NULL;
		re->orgqpath.len = 0;
	}
}

void hio_htre_clear (hio_htre_t* re)
{
	if (!(re->state & HIO_HTRE_COMPLETED) && 
	    !(re->state & HIO_HTRE_DISCARDED))
	{
		if (re->concb)
		{
			re->concb (re, HIO_NULL, 0, re->concb_ctx); /* indicate end of content */
			hio_htre_unsetconcb (re);
		}
	}

	re->state = 0;
	re->flags = 0;

	re->orgqpath.ptr = HIO_NULL;
	re->orgqpath.len = 0;

	HIO_MEMSET (&re->version, 0, HIO_SIZEOF(re->version));
	HIO_MEMSET (&re->attr, 0, HIO_SIZEOF(re->attr));

	hio_htb_clear (&re->hdrtab);
	hio_htb_clear (&re->trailers);

	hio_becs_clear (&re->content);
#if 0 
	hio_becs_clear (&re->iniline);
#endif
}

const hio_htre_hdrval_t* hio_htre_getheaderval (const hio_htre_t* re, const hio_bch_t* name)
{
	hio_htb_pair_t* pair;
	pair = hio_htb_search(&re->hdrtab, name, hio_count_bcstr(name));
	if (pair == HIO_NULL) return HIO_NULL;
	return HIO_HTB_VPTR(pair);
}

const hio_htre_hdrval_t* hio_htre_gettrailerval (const hio_htre_t* re, const hio_bch_t* name)
{
	hio_htb_pair_t* pair;
	pair = hio_htb_search(&re->trailers, name, hio_count_bcstr(name));
	if (pair == HIO_NULL) return HIO_NULL;
	return HIO_HTB_VPTR(pair);
}

struct header_walker_ctx_t
{
	hio_htre_t* re;
	hio_htre_header_walker_t walker;
	void* ctx;
	int ret;
};

static hio_htb_walk_t walk_headers (hio_htb_t* htb, hio_htb_pair_t* pair, void* ctx)
{
	struct header_walker_ctx_t* hwctx = (struct header_walker_ctx_t*)ctx;
	if (hwctx->walker (hwctx->re, HIO_HTB_KPTR(pair), HIO_HTB_VPTR(pair), hwctx->ctx) <= -1) 
	{
		hwctx->ret = -1;
		return HIO_HTB_WALK_STOP;
	}
	return HIO_HTB_WALK_FORWARD;
}

int hio_htre_walkheaders (hio_htre_t* re, hio_htre_header_walker_t walker, void* ctx)
{
	struct header_walker_ctx_t hwctx;
	hwctx.re = re;
	hwctx.walker = walker;
	hwctx.ctx = ctx;
	hwctx.ret = 0;
	hio_htb_walk (&re->hdrtab, walk_headers, &hwctx);
	return hwctx.ret;
}

int hio_htre_walktrailers (hio_htre_t* re, hio_htre_header_walker_t walker, void* ctx)
{
	struct header_walker_ctx_t hwctx;
	hwctx.re = re;
	hwctx.walker = walker;
	hwctx.ctx = ctx;
	hwctx.ret = 0;
	hio_htb_walk (&re->trailers, walk_headers, &hwctx);
	return hwctx.ret;
}

int hio_htre_addcontent (hio_htre_t* re, const hio_bch_t* ptr, hio_oow_t len)
{
	/* see comments in hio_htre_discardcontent() */

	if (re->state & (HIO_HTRE_COMPLETED | HIO_HTRE_DISCARDED)) return 0; /* skipped */

	if (re->concb) 
	{
		/* if the callback is set, the content goes to the callback. */
		if (re->concb(re, ptr, len, re->concb_ctx) <= -1) return -1;
	}
	else
	{
		/* if the callback is not set, the contents goes to the internal buffer */
		if (hio_becs_ncat(&re->content, ptr, len) == (hio_oow_t)-1) return -1;
	}

	return 1; /* added successfully */
}

void hio_htre_completecontent (hio_htre_t* re)
{
	/* see comments in hio_htre_discardcontent() */

	if (!(re->state & HIO_HTRE_COMPLETED) && 
	    !(re->state & HIO_HTRE_DISCARDED))
	{
		re->state |= HIO_HTRE_COMPLETED;
		if (re->concb)
		{
			/* indicate end of content */
			re->concb (re, HIO_NULL, 0, re->concb_ctx); 
		}
	}
}

void hio_htre_discardcontent (hio_htre_t* re)
{
	/* you can't discard this if it's completed.
	 * you can't complete this if it's discarded 
	 * you can't add contents to this if it's completed or discarded
	 */

	if (!(re->state & HIO_HTRE_COMPLETED) && !(re->state & HIO_HTRE_DISCARDED))
	{
		re->state |= HIO_HTRE_DISCARDED;

		/* hio_htre_addcontent()...
		 * hio_thre_setconcb()...
		 * hio_htre_discardcontent()... <-- POINT A.
		 *
		 * at point A, the content must contain something
		 * and concb is also set. for simplicity, 
		 * clear the content buffer and invoke the callback 
		 *
		 * likewise, you may produce many weird combinations
		 * of these functions. however, these functions are
		 * designed to serve a certain usage pattern not including
		 * weird combinations.
		 */
		hio_becs_clear (&re->content);
		if (re->concb)
		{
			/* indicate end of content */
			re->concb (re, HIO_NULL, 0, re->concb_ctx); 
		}
	}
}

void hio_htre_unsetconcb (hio_htre_t* re)
{
	re->concb = HIO_NULL;
	re->concb_ctx = HIO_NULL;
}

void hio_htre_setconcb (hio_htre_t* re, hio_htre_concb_t concb, void* ctx)
{
	re->concb = concb;
	re->concb_ctx = ctx;
}

int hio_htre_perdecqpath (hio_htre_t* re)
{
	hio_oow_t dec_count;

	/* percent decode the query path*/

	if (re->type != HIO_HTRE_Q || (re->flags & HIO_HTRE_QPATH_PERDEC)) return -1;

	HIO_ASSERT (re->hio, re->orgqpath.len <= 0);
	HIO_ASSERT (re->hio, re->orgqpath.ptr == HIO_NULL);

	if (hio_is_perenced_http_bcstr(re->u.q.path.ptr))
	{
		/* the string is percent-encoded. keep the original request
		 * in a separately allocated buffer */

		if (re->orgqpath.buf && re->u.q.path.len <= re->orgqpath.capa)
		{
			re->orgqpath.len = hio_copy_bcstr_unlimited(re->orgqpath.buf, re->u.q.path.ptr);
			re->orgqpath.ptr = re->orgqpath.buf;
		}
		else
		{
			if (re->orgqpath.buf)
			{
				hio_freemem (re->hio, re->orgqpath.buf);
				re->orgqpath.capa = 0;
				re->orgqpath.ptr = HIO_NULL;
				re->orgqpath.len = 0;
			}

			re->orgqpath.buf = hio_dupbchars(re->hio, re->u.q.path.ptr, re->u.q.path.len);
			if (HIO_UNLIKELY(!re->orgqpath.buf)) return -1;
			re->orgqpath.capa = re->u.q.path.len;

			re->orgqpath.ptr = re->orgqpath.buf;
			re->orgqpath.len = re->orgqpath.capa;

			/* orgqpath.buf and orgqpath.ptr are the same here. the caller
			 * is free to change orgqpath.ptr to point to a differnt position
			 * in the buffer. */
		}
	}

	re->u.q.path.len = hio_perdec_http_bcstr(re->u.q.path.ptr, re->u.q.path.ptr, &dec_count);
	if (dec_count > 0) 
	{
		/* this assertion is to ensure that hio_is_perenced_http_bstr() 
		 * returned true when dec_count is greater than 0 */
		HIO_ASSERT (re->hio, re->orgqpath.buf != HIO_NULL);
		HIO_ASSERT (re->hio, re->orgqpath.ptr != HIO_NULL);
		re->flags |= HIO_HTRE_QPATH_PERDEC;
	}

	return 0;
}

int hio_htre_getreqcontentlen (hio_htre_t* req, hio_oow_t* len)
{
	/* return the potential content length to expect to receive if used as a request */

	if (req->flags & HIO_HTRE_ATTR_CHUNKED)
	{
		/* "Transfer-Encoding: chunked" take precedence over "Content-Length: XXX". 
		 *
		 * [RFC7230]
		 *  If a message is received with both a Transfer-Encoding and a
		 *  Content-Length header field, the Transfer-Encoding overrides the
		 *  Content-Length. */
		return 1; /* unable to determine content-length in advance. unlimited */
	}

	if (req->flags & HIO_HTRE_ATTR_LENGTH)
	{
		*len = req->attr.content_length;
	}
	else
	{
		/* If no Content-Length is specified in a request, it's Content-Length: 0 */
		*len = 0;
	}

	return 0; /* limited to the length set in *len */
}
