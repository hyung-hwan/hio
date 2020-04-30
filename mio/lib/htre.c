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

#include <mio-htre.h>
#include "mio-prv.h"

static void free_hdrval (mio_htb_t* htb, void* vptr, mio_oow_t vlen)
{
	mio_htre_hdrval_t* val;
	mio_htre_hdrval_t* tmp;

	val = vptr;
	while (val)
	{
		tmp = val;
		val = val->next;
		mio_freemem (htb->mio, tmp);
	}
}

int mio_htre_init (mio_htre_t* re, mio_t* mio)
{
	static mio_htb_style_t style =
	{
		{
			MIO_HTB_COPIER_DEFAULT,
			MIO_HTB_COPIER_DEFAULT
		},
		{
			MIO_HTB_FREEER_DEFAULT,
			free_hdrval
		},
		MIO_HTB_COMPER_DEFAULT,
		MIO_HTB_KEEPER_DEFAULT,
		MIO_HTB_SIZER_DEFAULT,
		MIO_HTB_HASHER_DEFAULT
	};

	MIO_MEMSET (re, 0, MIO_SIZEOF(*re));
	re->mio = mio;

	if (mio_htb_init(&re->hdrtab, mio, 60, 70, 1, 1) <= -1) return -1;
	if (mio_htb_init(&re->trailers, mio, 20, 70, 1, 1) <= -1) return -1;

	mio_htb_setstyle (&re->hdrtab, &style);
	mio_htb_setstyle (&re->trailers, &style);

	mio_becs_init (&re->content, mio, 0);
#if 0
	mio_becs_init (&re->iniline, mio, 0);
#endif

	return 0;
}

void mio_htre_fini (mio_htre_t* re)
{
#if 0
	mio_becs_fini (&re->iniline);
#endif
	mio_becs_fini (&re->content);
	mio_htb_fini (&re->trailers);
	mio_htb_fini (&re->hdrtab);

	if (re->orgqpath.buf) mio_freemem (re->mio, re->orgqpath.buf);
}

void mio_htre_clear (mio_htre_t* re)
{
	if (!(re->state & MIO_HTRE_COMPLETED) && 
	    !(re->state & MIO_HTRE_DISCARDED))
	{
		if (re->concb)
		{
			re->concb (re, MIO_NULL, 0, re->concb_ctx); /* indicate end of content */
			mio_htre_unsetconcb (re);
		}
	}

	re->state = 0;
	re->flags = 0;

	re->orgqpath.ptr = MIO_NULL;
	re->orgqpath.len = 0;

	MIO_MEMSET (&re->version, 0, MIO_SIZEOF(re->version));
	MIO_MEMSET (&re->attr, 0, MIO_SIZEOF(re->attr));

	mio_htb_clear (&re->hdrtab);
	mio_htb_clear (&re->trailers);

	mio_becs_clear (&re->content);
#if 0 
	mio_becs_clear (&re->iniline);
#endif
}

const mio_htre_hdrval_t* mio_htre_getheaderval (const mio_htre_t* re, const mio_bch_t* name)
{
	mio_htb_pair_t* pair;
	pair = mio_htb_search(&re->hdrtab, name, mio_count_bcstr(name));
	if (pair == MIO_NULL) return MIO_NULL;
	return MIO_HTB_VPTR(pair);
}

const mio_htre_hdrval_t* mio_htre_gettrailerval (const mio_htre_t* re, const mio_bch_t* name)
{
	mio_htb_pair_t* pair;
	pair = mio_htb_search(&re->trailers, name, mio_count_bcstr(name));
	if (pair == MIO_NULL) return MIO_NULL;
	return MIO_HTB_VPTR(pair);
}

struct header_walker_ctx_t
{
	mio_htre_t* re;
	mio_htre_header_walker_t walker;
	void* ctx;
	int ret;
};

static mio_htb_walk_t walk_headers (mio_htb_t* htb, mio_htb_pair_t* pair, void* ctx)
{
	struct header_walker_ctx_t* hwctx = (struct header_walker_ctx_t*)ctx;
	if (hwctx->walker (hwctx->re, MIO_HTB_KPTR(pair), MIO_HTB_VPTR(pair), hwctx->ctx) <= -1) 
	{
		hwctx->ret = -1;
		return MIO_HTB_WALK_STOP;
	}
	return MIO_HTB_WALK_FORWARD;
}

int mio_htre_walkheaders (mio_htre_t* re, mio_htre_header_walker_t walker, void* ctx)
{
	struct header_walker_ctx_t hwctx;
	hwctx.re = re;
	hwctx.walker = walker;
	hwctx.ctx = ctx;
	hwctx.ret = 0;
	mio_htb_walk (&re->hdrtab, walk_headers, &hwctx);
	return hwctx.ret;
}

int mio_htre_walktrailers (mio_htre_t* re, mio_htre_header_walker_t walker, void* ctx)
{
	struct header_walker_ctx_t hwctx;
	hwctx.re = re;
	hwctx.walker = walker;
	hwctx.ctx = ctx;
	hwctx.ret = 0;
	mio_htb_walk (&re->trailers, walk_headers, &hwctx);
	return hwctx.ret;
}

int mio_htre_addcontent (mio_htre_t* re, const mio_bch_t* ptr, mio_oow_t len)
{
	/* see comments in mio_htre_discardcontent() */

	if (re->state & (MIO_HTRE_COMPLETED | MIO_HTRE_DISCARDED)) return 0; /* skipped */

	if (re->concb) 
	{
		/* if the callback is set, the content goes to the callback. */
		if (re->concb(re, ptr, len, re->concb_ctx) <= -1) return -1;
	}
	else
	{
		/* if the callback is not set, the contents goes to the internal buffer */
		if (mio_becs_ncat(&re->content, ptr, len) == (mio_oow_t)-1) return -1;
	}

	return 1; /* added successfully */
}

void mio_htre_completecontent (mio_htre_t* re)
{
	/* see comments in mio_htre_discardcontent() */

	if (!(re->state & MIO_HTRE_COMPLETED) && 
	    !(re->state & MIO_HTRE_DISCARDED))
	{
		re->state |= MIO_HTRE_COMPLETED;
		if (re->concb)
		{
			/* indicate end of content */
			re->concb (re, MIO_NULL, 0, re->concb_ctx); 
		}
	}
}

void mio_htre_discardcontent (mio_htre_t* re)
{
	/* you can't discard this if it's completed.
	 * you can't complete this if it's discarded 
	 * you can't add contents to this if it's completed or discarded
	 */

	if (!(re->state & MIO_HTRE_COMPLETED) && !(re->state & MIO_HTRE_DISCARDED))
	{
		re->state |= MIO_HTRE_DISCARDED;

		/* mio_htre_addcontent()...
		 * mio_thre_setconcb()...
		 * mio_htre_discardcontent()... <-- POINT A.
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
		mio_becs_clear (&re->content);
		if (re->concb)
		{
			/* indicate end of content */
			re->concb (re, MIO_NULL, 0, re->concb_ctx); 
		}
	}
}

void mio_htre_unsetconcb (mio_htre_t* re)
{
	re->concb = MIO_NULL;
	re->concb_ctx = MIO_NULL;
}

void mio_htre_setconcb (mio_htre_t* re, mio_htre_concb_t concb, void* ctx)
{
	re->concb = concb;
	re->concb_ctx = ctx;
}

int mio_htre_perdecqpath (mio_htre_t* re)
{
	mio_oow_t dec_count;

	/* percent decode the query path*/

	if (re->type != MIO_HTRE_Q || (re->flags & MIO_HTRE_QPATH_PERDEC)) return -1;

	MIO_ASSERT (re->mio, re->orgqpath.len <= 0);
	MIO_ASSERT (re->mio, re->orgqpath.ptr == MIO_NULL);

	if (mio_is_perenced_http_bcstr(re->u.q.path.ptr))
	{
		/* the string is percent-encoded. keep the original request
		 * in a separately allocated buffer */

		if (re->orgqpath.buf && re->u.q.path.len <= re->orgqpath.capa)
		{
			re->orgqpath.len = mio_copy_bcstr_unlimited(re->orgqpath.buf, re->u.q.path.ptr);
			re->orgqpath.ptr = re->orgqpath.buf;
		}
		else
		{
			if (re->orgqpath.buf)
			{
				mio_freemem (re->mio, re->orgqpath.buf);
				re->orgqpath.capa = 0;
			}

			re->orgqpath.buf = mio_mbsxdup(re->u.q.path.ptr, re->u.q.path.len, re->mio);
			if (!re->orgqpath.buf) return -1;
			re->orgqpath.capa = re->u.q.path.len;

			re->orgqpath.ptr = re->orgqpath.buf;
			re->orgqpath.len = re->orgqpath.capa;

			/* orgqpath.buf and orgqpath.ptr are the same here. the caller
			 * is free to change orgqpath.ptr to point to a differnt position
			 * in the buffer. */
		}
	}

	re->u.q.path.len = mio_perdechttpstr (re->u.q.path.ptr, re->u.q.path.ptr, &dec_count);
	if (dec_count > 0) 
	{
		/* this assertion is to ensure that mio_is_perenced_http_bstr() 
		 * returned true when dec_count is greater than 0 */
		MIO_ASSERT (re->mio, re->orgqpath.buf != MIO_NULL);
		MIO_ASSERT (re->mio, re->orgqpath.ptr != MIO_NULL);
		re->flags |= MIO_HTRE_QPATH_PERDEC;
	}

	return 0;
}
