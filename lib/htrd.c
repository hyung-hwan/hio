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

#include <hio-htrd.h>
#include <hio-chr.h>
#include <hio-path.h>
#include "hio-prv.h"

static const hio_bch_t NUL = '\0';

/* for htrd->fed.s.flags */
#define CONSUME_UNTIL_CLOSE (1 << 0)

/* for htrd->flags */
#define FEEDING_SUSPENDED   (1 << 0)
#define FEEDING_DUMMIFIED   (1 << 1)

static HIO_INLINE int is_whspace_octet (hio_bch_t c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static HIO_INLINE int is_space_octet (hio_bch_t c)
{
	return c == ' ' || c == '\t' || c == '\r';
}

static HIO_INLINE int is_purespace_octet (hio_bch_t c)
{
	return c == ' ' || c == '\t';
}

static HIO_INLINE int is_upalpha_octet (hio_bch_t c)
{
	return c >= 'A' && c <= 'Z';
}

static HIO_INLINE int is_loalpha_octet (hio_bch_t c)
{
	return c >= 'a' && c <= 'z';
}

static HIO_INLINE int is_alpha_octet (hio_bch_t c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static HIO_INLINE int is_digit_octet (hio_bch_t c)
{
	return c >= '0' && c <= '9';
}

static HIO_INLINE int is_xdigit_octet (hio_bch_t c)
{
	return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

static HIO_INLINE int digit_to_num (hio_bch_t c)
{
	return HIO_DIGIT_TO_NUM(c);
}

static HIO_INLINE int xdigit_to_num (hio_bch_t c)
{
	return HIO_XDIGIT_TO_NUM(c);
}

static HIO_INLINE int push_to_buffer (hio_htrd_t* htrd, hio_becs_t* octb, const hio_bch_t* ptr, hio_oow_t len)
{
	if (hio_becs_ncat(octb, ptr, len) == (hio_oow_t)-1) 
	{
		htrd->errnum = HIO_HTRD_ENOMEM;
		return -1;
	}
	return 0;
}

static HIO_INLINE int push_content (hio_htrd_t* htrd, const hio_bch_t* ptr, hio_oow_t len)
{
	HIO_ASSERT (htrd->hio, len > 0);

	if (htrd->recbs.push_content) return htrd->recbs.push_content(htrd, &htrd->re, ptr, len);

	if (hio_htre_addcontent(&htrd->re, ptr, len) <= -1) 
	{
		htrd->errnum = HIO_HTRD_ENOMEM;
		return -1;
	}

	/* hio_htre_addcontent() returns 1 on full success and 0 if adding is 
	 * skipped. i treat both as success */
	return 0;
}

static HIO_INLINE void clear_feed (hio_htrd_t* htrd)
{
	/* clear necessary part of the request/response before 
	 * reading the next request/response */
	htrd->clean = 1;
	hio_htre_clear (&htrd->re);

	hio_becs_clear (&htrd->fed.b.tra);
	hio_becs_clear (&htrd->fed.b.raw);

	HIO_MEMSET (&htrd->fed.s, 0, HIO_SIZEOF(htrd->fed.s));
}

hio_htrd_t* hio_htrd_open (hio_t* hio, hio_oow_t xtnsize)
{
	hio_htrd_t* htrd;

	htrd = (hio_htrd_t*)hio_allocmem(hio, HIO_SIZEOF(hio_htrd_t) + xtnsize);
	if (HIO_LIKELY(htrd))
	{
		if (HIO_UNLIKELY(hio_htrd_init(htrd, hio) <= -1))
		{
			hio_freemem (hio, htrd);
			return HIO_NULL;
		}
		else HIO_MEMSET (htrd + 1, 0, xtnsize);
	}
	return htrd;
}

void hio_htrd_close (hio_htrd_t* htrd)
{
	hio_htrd_fini (htrd);
	hio_freemem (htrd->hio, htrd);
}

int hio_htrd_init (hio_htrd_t* htrd, hio_t* hio)
{
	HIO_MEMSET (htrd, 0, HIO_SIZEOF(*htrd));
	htrd->hio = hio;
	htrd->option = HIO_HTRD_REQUEST | HIO_HTRD_RESPONSE;

#if 0
	hio_becs_init (&htrd->tmp.qparam, htrd->hio, 0);
#endif
	hio_becs_init (&htrd->fed.b.raw, htrd->hio, 0);
	hio_becs_init (&htrd->fed.b.tra, htrd->hio, 0);

	if (hio_htre_init(&htrd->re, hio) <= -1)
	{
		hio_becs_fini (&htrd->fed.b.tra);
		hio_becs_fini (&htrd->fed.b.raw);
#if 0
		hio_becs_fini (&htrd->tmp.qparam);
#endif
		return -1;
	}

	htrd->clean = 1;
	return 0;
}

void hio_htrd_fini (hio_htrd_t* htrd)
{
	hio_htre_fini (&htrd->re);

	hio_becs_fini (&htrd->fed.b.tra);
	hio_becs_fini (&htrd->fed.b.raw);
#if 0
	hio_becs_fini (&htrd->tmp.qparam);
#endif
}

static hio_bch_t* parse_initial_line (hio_htrd_t* htrd, hio_bch_t* line)
{
	hio_bch_t* p = line;
	hio_bcs_t tmp;

#if 0
	/* ignore leading spaces excluding crlf */
	while (is_space_octet(*p)) p++;
#endif

	/* the method should start with an alphabet */
	if (!is_alpha_octet(*p)) goto badre;

	/* get the method name */
	tmp.ptr = p;
	do { p++; } while (is_alpha_octet(*p));
	tmp.len = p - tmp.ptr;

	htrd->re.type = HIO_HTRE_Q;
	if (htrd->option & HIO_HTRD_REQUEST)
	{
		/* method name must be followed by space */
		if (!is_space_octet(*p)) goto badre;

		*p = '\0'; /* null-terminate the method name */

		htrd->re.u.q.method.type = hio_bchars_to_http_method(tmp.ptr, tmp.len);
		htrd->re.u.q.method.name = tmp.ptr;
	}
	else if ((htrd->option & HIO_HTRD_RESPONSE) && hio_comp_bchars_bcstr(tmp.ptr, tmp.len, "HTTP", 1) == 0)
	{
		/* it begins with HTTP. it may be a response */
		htrd->re.type = HIO_HTRE_S;
	}
	else goto badre;

	if (htrd->re.type == HIO_HTRE_S)
	{
		/* response */
		int n, status;

		if (*p == '/' && p[1] != '\0' && p[2] == '.')
		{
			int q = digit_to_num(p[1]);
			int w = digit_to_num(p[3]);
			if (q >= 0 && w >= 0)
			{
				htrd->re.version.major = q;
				htrd->re.version.minor = w;
				p += 4;
			}
			else goto badre;
		}
		else goto badre;

		/* version must be followed by space */
		if (!is_space_octet(*p)) goto badre;

		*p = '\0'; /* null-terminate version string */
		htrd->re.verstr = tmp.ptr;

		/* skip spaces */
		do p++; while (is_space_octet(*p));
		
		n = digit_to_num(*p);
		if (n <= -1) goto badre;

		tmp.ptr = p;
		status = 0;
		do
		{
			status = status * 10 + n;
			p++;
		} 
		while ((n = digit_to_num(*p)) >= 0);

		if (!is_space_octet(*p)) goto badre;
		*p = '\0'; /* null-terminate the status code */

		htrd->re.u.s.code.val = status;
		htrd->re.u.s.code.str = tmp.ptr;

		/* i don't treat the following weird messages as bad message:
		 *    no status message follows the status code
		 */

		/* skip spaces */
		do p++; while (is_space_octet(*p));
	
		tmp.ptr = p;
		tmp.len = 0;
		while (*p != '\0' && *p != '\n') 
		{
			if (!is_space_octet(*p)) tmp.len = p - tmp.ptr + 1;
			p++;
		}
	
		/* if the line does not end with a new line, it is a bad request */
		if (*p != '\n') goto badre;

		/* null-terminate the message */
		((hio_bch_t*)tmp.ptr)[tmp.len] = '\0';
		htrd->re.u.s.mesg = tmp.ptr;
	}
	else
	{
		hio_bcs_t param;
		hio_bcs_t anchor;

		/* skip spaces */
		do p++; while (is_space_octet(*p));

		/* process the url part */
		tmp.ptr = p; tmp.len = 0;
		param.ptr = HIO_NULL; param.len = 0;
		anchor.ptr = HIO_NULL; anchor.len = 0;

		while (1)
		{
			if (HIO_UNLIKELY(*p == '\0')) goto badre;
			else if (is_space_octet(*p) || *p == '?' || *p == '#')
			{
				tmp.len = p - tmp.ptr; 
				if (tmp.len <= 0) goto badre;
				break;
			}
			else p++;
		}

		if (*p == '?')
		{
			param.ptr = ++p;
			while (1)
			{
				if (HIO_UNLIKELY(*p == '\0')) goto badre;
				else if (is_space_octet(*p) || *p == '#')
				{
					param.len = p - param.ptr;
					break;
				}
				else p++;
			}
		}

		if (*p == '#')
		{
			anchor.ptr = ++p;
			while (1)
			{
				if (HIO_UNLIKELY(*p == '\0')) goto badre;
				else if (is_space_octet(*p))
				{
					anchor.len = p - anchor.ptr;
					break;
				}
				else p++;
			}
		}

		tmp.ptr[tmp.len] = '\0';
		if (param.ptr) param.ptr[param.len] = '\0';
		if (anchor.ptr) anchor.ptr[anchor.len] = '\0';

		htrd->re.u.q.path = tmp;
		htrd->re.u.q.param = param;
		htrd->re.u.q.anchor = anchor;

		if (htrd->option & HIO_HTRD_CANONQPATH)
		{
			hio_bch_t* qpath = htrd->re.u.q.path.ptr;

			/* if the url begins with xxx://,
			 * skip xxx:/ and canonicalize from the second slash */
			while (is_alpha_octet(*qpath)) qpath++;
			if (hio_comp_bcstr_limited(qpath, "://", 3, 1) == 0)
			{
				qpath = qpath + 2; /* set the position to the second / in :// */
				htrd->re.u.q.path.len = hio_canon_bcstr_path(qpath, qpath, 0);
				htrd->re.u.q.path.len += qpath - htrd->re.u.q.path.ptr;
			}
			else
			{
				qpath = htrd->re.u.q.path.ptr;
				htrd->re.u.q.path.len = hio_canon_bcstr_path(qpath, qpath, 0);
			}
		}
	
		/* skip spaces after the url part */
		do { p++; } while (is_space_octet(*p));
	
		tmp.ptr = p;
		/* check protocol version */
		if ((p[0] == 'H' || p[0] == 'h') &&
		    (p[1] == 'T' || p[1] == 't') &&
		    (p[2] == 'T' || p[2] == 't') &&
		    (p[3] == 'P' || p[3] == 'p') &&
		    p[4] == '/' && p[6] == '.')
		{
			int q = digit_to_num(p[5]);
			int w = digit_to_num(p[7]);
			if (q >= 0 && w >= 0)
			{
				htrd->re.version.major = q;
				htrd->re.version.minor = w;
				p += 8;
			}
			else goto badre;
		}
		else goto badre;
	
		tmp.len = p - tmp.ptr;

		/* skip trailing spaces on the line */
		while (is_space_octet(*p)) p++;

		/* if the line does not end with a new line, it is a bad request */
		if (*p != '\n') goto badre;

		((hio_bch_t*)tmp.ptr)[tmp.len] = '\0';
		htrd->re.verstr = tmp.ptr;
	}
	
	/* adjust Connection: Keep-Alive for HTTP 1.1 or later.
	 * this is initial. it can be adjusted further in capture_connection(). */
	if (htrd->re.version.major > 1 || 
	    (htrd->re.version.major == 1 && htrd->re.version.minor >= 1))
	{
		htrd->re.flags |= HIO_HTRE_ATTR_KEEPALIVE;
	}

	return ++p;

badre:
	htrd->errnum = HIO_HTRD_EBADRE;
	return HIO_NULL;
}

void hio_htrd_clear (hio_htrd_t* htrd)
{
	clear_feed (htrd);
	htrd->flags = 0;
}

hio_htrd_errnum_t hio_htrd_geterrnum (hio_htrd_t* htrd)
{
	return htrd->errnum;
}

hio_bitmask_t hio_htrd_getoption (hio_htrd_t* htrd)
{
	return htrd->option;
}

void hio_htrd_setoption (hio_htrd_t* htrd, hio_bitmask_t mask)
{
	htrd->option = mask;
}

const hio_htrd_recbs_t* hio_htrd_getrecbs (hio_htrd_t* htrd)
{
	return &htrd->recbs;
}

void hio_htrd_setrecbs (hio_htrd_t* htrd, const hio_htrd_recbs_t* recbs)
{
	htrd->recbs = *recbs;
}

static int capture_connection (hio_htrd_t* htrd, hio_htb_pair_t* pair)
{
	hio_htre_hdrval_t* val;

	val = HIO_HTB_VPTR(pair);
	while (val->next) val = val->next;

	/* The value for Connection: may get comma-separated. 
	 * so use hio_find_bcstr_word_in_bcstr() instead of hio_comp_bcstr(). */

	if (hio_find_bcstr_word_in_bcstr(val->ptr, "close", ',', 1))
	{
		htrd->re.flags &= ~HIO_HTRE_ATTR_KEEPALIVE;
		return 0;
	}

	if (hio_find_bcstr_word_in_bcstr(val->ptr, "keep-alive", ',', 1))
	{
		htrd->re.flags |= HIO_HTRE_ATTR_KEEPALIVE;
		return 0;
	}

	/* Basically i don't care about other values.
	 * but for HTTP 1.0, other values will set connection to 'close'.
	 *
	 * Other values include even Keep-Alive specified multiple times.
	 *  Connection: Keep-Alive
	 *  Connection: Keep-Alive
	 * For the second Keep-Alive, this function sees 'Keep-Alive,Keep-Alive'
	 * That's because values of the same keys are concatenated.
	 */
	if (htrd->re.version.major < 1  || (htrd->re.version.major == 1 && htrd->re.version.minor <= 0))
	{
		htrd->re.flags &= ~HIO_HTRE_ATTR_KEEPALIVE;
	}
	return 0;
}

static int capture_content_length (hio_htrd_t* htrd, hio_htb_pair_t* pair)
{
	hio_oow_t len = 0, off = 0, tmp;
	const hio_bch_t* ptr;
	hio_htre_hdrval_t* val;

	/* get the last content_length */
	val = HIO_HTB_VPTR(pair);
	while (val->next) val = val->next;

	ptr = val->ptr;
	while (off < val->len)
	{
		int num = digit_to_num(ptr[off]);
		if (num <= -1)
		{
			/* the length contains a non-digit */
			htrd->errnum = HIO_HTRD_EBADRE;
			return -1;
		}

		tmp = len * 10 + num;
		if (tmp < len)
		{
			/* the length has overflown */
			htrd->errnum = HIO_HTRD_EBADRE;
			return -1;
		}

		len = tmp;
		off++;
	}

	if (off == 0)
	{
		/* no length was provided */
		htrd->errnum = HIO_HTRD_EBADRE;
		return -1;
	}

	if ((htrd->re.flags & HIO_HTRE_ATTR_CHUNKED) && len > 0)
	{
		/* content-length is greater than 0 
		 * while transfer-encoding: chunked is specified. */
		htrd->errnum = HIO_HTRD_EBADRE;
		return -1;
	}

	htrd->re.flags |= HIO_HTRE_ATTR_LENGTH;
	htrd->re.attr.content_length = len;
	return 0;
}

static int capture_expect (hio_htrd_t* htrd, hio_htb_pair_t* pair)
{
	hio_htre_hdrval_t* val;

	/* Expect is included */
	htrd->re.flags |= HIO_HTRE_ATTR_EXPECT; 

	val = HIO_HTB_VPTR(pair);
	while (val) 
	{
		/* Expect: 100-continue is included */
		if (hio_comp_bcstr(val->ptr, "100-continue", 1) == 0) htrd->re.flags |= HIO_HTRE_ATTR_EXPECT100; 
		val = val->next;
	}

	return 0;
}

static int capture_status (hio_htrd_t* htrd, hio_htb_pair_t* pair)
{
	hio_htre_hdrval_t* val;

	val = HIO_HTB_VPTR(pair);
	while (val->next) val = val->next;

	htrd->re.attr.status = val->ptr;
	return 0;
}

static int capture_transfer_encoding (hio_htrd_t* htrd, hio_htb_pair_t* pair)
{
	int n;
	hio_htre_hdrval_t* val;

	val = HIO_HTB_VPTR(pair);
	while (val->next) val = val->next;

	n = hio_comp_bcstr(val->ptr, "chunked", 1);
	if (n == 0)
	{
		/* if (htrd->re.attr.content_length > 0) */
		if (htrd->re.flags & HIO_HTRE_ATTR_LENGTH)
		{
			/* both content-length and 'transfer-encoding: chunked' are specified. */
			goto badre;
		}

		htrd->re.flags |= HIO_HTRE_ATTR_CHUNKED;
		return 0;
	}

	/* other encoding type not supported yet */
badre:
	htrd->errnum = HIO_HTRD_EBADRE;
	return -1;
}

static HIO_INLINE int capture_key_header (hio_htrd_t* htrd, hio_htb_pair_t* pair)
{
	static struct
	{
		const hio_bch_t* ptr;
		hio_oow_t        len;
		int (*handler) (hio_htrd_t*, hio_htb_pair_t*);
	} hdrtab[] = 
	{
		{ "Connection",         10, capture_connection },
		{ "Content-Length",     14, capture_content_length },
		{ "Expect",             6,  capture_expect },
		{ "Status",             6,  capture_status },
		{ "Transfer-Encoding",  17, capture_transfer_encoding  }
	};

	int n;
	hio_oow_t mid, count, base = 0;

	/* perform binary search */
	for (count = HIO_COUNTOF(hdrtab); count > 0; count /= 2)
	{
		mid = base + count / 2;

		n = hio_comp_bchars(HIO_HTB_KPTR(pair), HIO_HTB_KLEN(pair), hdrtab[mid].ptr, hdrtab[mid].len, 1);
		if (n == 0)
		{
			/* bingo! */
			return hdrtab[mid].handler(htrd, pair);
		}

		if (n > 0) { base = mid + 1; count--; }
	}

	/* No callback functions were interested in this header field. */
	return 0;
}

struct hdr_cbserter_ctx_t
{
	hio_htrd_t* htrd;
	void*       vptr;
	hio_oow_t  vlen;
};

static hio_htb_pair_t* hdr_cbserter (
	hio_htb_t* htb, hio_htb_pair_t* pair, 
	void* kptr, hio_oow_t klen, void* ctx)
{
	struct hdr_cbserter_ctx_t* tx = (struct hdr_cbserter_ctx_t*)ctx;

	if (pair == HIO_NULL)
	{
		/* the key is new. let's create a new pair. */
		hio_htb_pair_t* p; 
		hio_htre_hdrval_t *val;

		val = hio_allocmem(htb->hio, HIO_SIZEOF(*val));
		if (HIO_UNLIKELY(!val))
		{
			tx->htrd->errnum = HIO_HTRD_ENOMEM;
			return HIO_NULL;
		}

		HIO_MEMSET (val, 0, HIO_SIZEOF(*val));
		val->ptr = tx->vptr;
		val->len = tx->vlen;
		val->next = HIO_NULL;

		p = hio_htb_allocpair(htb, kptr, klen, val, 0);
		if (HIO_UNLIKELY(!p)) 
		{
			hio_freemem (htb->hio, val);
			tx->htrd->errnum = HIO_HTRD_ENOMEM;
		}
		else 
		{
			if (capture_key_header(tx->htrd, p) <= -1)
			{
				/* Destroy the pair created here
				 * as it is not added to the hash table yet */
				hio_htb_freepair (htb, p);
				p = HIO_NULL;
			}
		}

		return p;
	}
	else
	{
		/* RFC2616 
		 * Multiple message-header fields with the same field-name 
		 * MAY be present in a message if and only if the entire 
		 * field-value for that header field is defined as a 
		 * comma-separated list [i.e., #(values)]. It MUST be possible 
		 * to combine the multiple header fields into one 
		 * "field-name: field-value" pair, without changing the semantics
		 * of the message, by appending each subsequent field-value 
		 * to the first, each separated by a comma. The order in which 
		 * header fields with the same field-name are received is therefore
		 * significant to the interpretation of the combined field value,
		 * and thus a proxy MUST NOT change the order of these field values 
		 * when a message is forwarded. 

		 * RFC6265 defines the syntax for Set-Cookie and Cookie.
		 * this seems to be conflicting with RFC2616.
		 * 
		 * Origin servers SHOULD NOT fold multiple Set-Cookie header fields 
		 * into a single header field. The usual mechanism for folding HTTP 
		 * headers fields (i.e., as defined in [RFC2616]) might change the 
		 * semantics of the Set-Cookie header field because the %x2C (",")
		 * character is used by Set-Cookie in a way that conflicts with 
		 * such folding.
		 * 	
		 * So i just maintain the list of values for a key instead of
		 * folding them.
		 */

		hio_htre_hdrval_t* val;
		hio_htre_hdrval_t* tmp;

		val = (hio_htre_hdrval_t*)hio_allocmem(tx->htrd->hio, HIO_SIZEOF(*val));
		if (HIO_UNLIKELY(!val))
		{
			tx->htrd->errnum = HIO_HTRD_ENOMEM;
			return HIO_NULL;
		}

		HIO_MEMSET (val, 0, HIO_SIZEOF(*val));
		val->ptr = tx->vptr;
		val->len = tx->vlen;
		val->next = HIO_NULL;

/* TODO: doubly linked list for speed-up??? */
		tmp = HIO_HTB_VPTR(pair);
		HIO_ASSERT (tx->htrd->hio, tmp != HIO_NULL);

		/* find the tail */
		while (tmp->next) tmp = tmp->next;
		/* append it to the list*/
		tmp->next = val; 

		if (capture_key_header(tx->htrd, pair) <= -1) return HIO_NULL;
		return pair;
	}
}

hio_bch_t* parse_header_field (hio_htrd_t* htrd, hio_bch_t* line, hio_htb_t* tab)
{
	hio_bch_t* p = line, * last;
	struct
	{
		hio_bch_t* ptr;
		hio_oow_t   len;
	} name, value;

#if 0
	/* ignore leading spaces excluding crlf */
	while (is_space_octet(*p)) p++;
#endif

	HIO_ASSERT (htrd->hio, !is_whspace_octet(*p));

	/* check the field name */
	name.ptr = last = p;
	while (*p != '\0' && *p != '\n' && *p != ':')
	{
		if (!is_space_octet(*p++)) last = p;
	}
	name.len = last - name.ptr;

	if (*p != ':') 
	{
		if (!(htrd->option & HIO_HTRD_STRICT))
		{
			while (is_space_octet(*p)) p++;
			if (*p == '\n') 
			{
				/* ignore a line without a colon */
				p++;
				return p;
			}
		}
		goto badhdr;
	}
	*last = '\0';

	/* skip the colon and spaces after it */
	do { p++; } while (is_space_octet(*p));

	value.ptr = last = p;
	while (*p != '\0' && *p != '\n')
	{
		if (!is_space_octet(*p++)) last = p;
	}

	value.len = last - value.ptr;
	if (*p != '\n') goto badhdr; /* not ending with a new line */

	/* peep at the beginning of the next line to check if it is 
	 * the continuation */
	if (is_purespace_octet (*++p))
	{
		/* RFC: HTTP/1.0 headers may be folded onto multiple lines if 
		 * each continuation line begins with a space or horizontal tab. 
		 * All linear whitespace, including folding, has the same semantics 
		 * as SP. */
		hio_bch_t* cpydst;

		cpydst = p - 1;
		if (*(cpydst-1) == '\r') cpydst--;

		/* process all continued lines */
		do 
		{
			while (*p != '\0' && *p != '\n')
			{
				*cpydst = *p++; 
				if (!is_space_octet(*cpydst++)) last = cpydst;
			} 
	
			value.len = last - value.ptr;
			if (*p != '\n') goto badhdr;

			if (*(cpydst-1) == '\r') cpydst--;
		}
		while (is_purespace_octet(*++p));
	}
	*last = '\0';

	/* insert the new field to the header table */
	{
		struct hdr_cbserter_ctx_t ctx;

		ctx.htrd = htrd;
		ctx.vptr = value.ptr;
		ctx.vlen = value.len;

		htrd->errnum = HIO_HTRD_ENOERR;
		if (hio_htb_cbsert (
			tab, name.ptr, name.len, 
			hdr_cbserter, &ctx) == HIO_NULL)
		{
			if (htrd->errnum == HIO_HTRD_ENOERR) 
				htrd->errnum = HIO_HTRD_ENOMEM;
			return HIO_NULL;
		}
	}

	return p;

badhdr:
	htrd->errnum = HIO_HTRD_EBADHDR;
	return HIO_NULL;
}

static HIO_INLINE int parse_initial_line_and_headers (hio_htrd_t* htrd, const hio_bch_t* req, hio_oow_t rlen)
{
	hio_bch_t* p;

	/* add the actual request */
	if (push_to_buffer (htrd, &htrd->fed.b.raw, req, rlen) <= -1) return -1;

	/* add the terminating null for easier parsing */
	if (push_to_buffer (htrd, &htrd->fed.b.raw, &NUL, 1) <= -1) return -1;

	p = HIO_BECS_PTR(&htrd->fed.b.raw);

#if 0
	if (htrd->option & HIO_HTRD_SKIP_EMPTY_LINES)
		while (is_whspace_octet(*p)) p++;
	else
#endif
		while (is_space_octet(*p)) p++;
	
	HIO_ASSERT (htrd->hio, *p != '\0');

	/* parse the initial line */
	if (!(htrd->option & HIO_HTRD_SKIP_INITIAL_LINE))
	{
		p = parse_initial_line(htrd, p);
		if (HIO_UNLIKELY(!p)) return -1;
	}

	/* parse header fields */
	do
	{
		while (is_whspace_octet(*p)) p++;
		if (*p == '\0') break;

		/* TODO: return error if protocol is 0.9.
		 * HTTP/0.9 must not get headers... */

		p = parse_header_field(htrd, p, &htrd->re.hdrtab);
		if (HIO_UNLIKELY(!p)) return -1;
	}
	while (1);

	return 0;
}

/* chunk parsing phases */
#define GET_CHUNK_DONE     0
#define GET_CHUNK_LEN      1
#define GET_CHUNK_DATA     2
#define GET_CHUNK_CRLF     3
#define GET_CHUNK_TRAILERS 4

static const hio_bch_t* getchunklen (hio_htrd_t* htrd, const hio_bch_t* ptr, hio_oow_t len)
{
	const hio_bch_t* end = ptr + len;

	/* this function must be called in the GET_CHUNK_LEN context */
	HIO_ASSERT (htrd->hio, htrd->fed.s.chunk.phase == GET_CHUNK_LEN);

	if (htrd->fed.s.chunk.count <= 0)
	{
		/* skip leading spaces if the first character of
		 * the chunk length has not been read yet */
		while (ptr < end && is_space_octet(*ptr)) ptr++;
	}

	while (ptr < end)
	{
		int n = xdigit_to_num (*ptr);
		if (n <= -1) break;

		htrd->fed.s.chunk.len = htrd->fed.s.chunk.len * 16 + n;
		htrd->fed.s.chunk.count++;
		ptr++;
	}

	/* skip trailing spaces if the length has been read */
	while (ptr < end && is_space_octet(*ptr)) ptr++;

	if (ptr < end)
	{
		if (*ptr == '\n') 
		{
			/* the chunk length line ended properly */

			if (htrd->fed.s.chunk.count <= 0)
			{
				/* empty line - no more chunk */
				htrd->fed.s.chunk.phase = GET_CHUNK_DONE;
			}
			else if (htrd->fed.s.chunk.len <= 0)
			{
				/* length explicity specified to 0
				   get trailing headers .... */
				htrd->fed.s.chunk.phase = GET_CHUNK_TRAILERS;
			}
			else
			{
				/* ready to read the chunk data... */
				htrd->fed.s.chunk.phase = GET_CHUNK_DATA;
			}

			htrd->fed.s.need = htrd->fed.s.chunk.len;
			ptr++;
		}
		else
		{
			htrd->errnum = HIO_HTRD_EBADRE;
			return HIO_NULL;
		}
	}

	return ptr;
}

static const hio_bch_t* get_trailing_headers (hio_htrd_t* htrd, const hio_bch_t* req, const hio_bch_t* end)
{
	const hio_bch_t* ptr = req;

	while (ptr < end)
	{
		register hio_bch_t b = *ptr++;
		switch (b)
		{
			case '\0':
				/* guarantee that the request does not contain a null 
				 * character */
				htrd->errnum = HIO_HTRD_EBADRE;
				return HIO_NULL;

			case '\n':
				if (htrd->fed.s.crlf <= 1) 
				{
					htrd->fed.s.crlf = 2;
					break;
				}
				else
				{
					hio_bch_t* p;

					HIO_ASSERT (htrd->hio, htrd->fed.s.crlf <= 3);
					htrd->fed.s.crlf = 0;

					if (push_to_buffer(htrd, &htrd->fed.b.tra, req, ptr - req) <= -1 ||
					    push_to_buffer(htrd, &htrd->fed.b.tra, &NUL, 1) <= -1) return HIO_NULL;

					p = HIO_BECS_PTR(&htrd->fed.b.tra);

					do
					{
						while (is_whspace_octet(*p)) p++;
						if (*p == '\0') break;

						/* TODO: return error if protocol is 0.9.
						 * HTTP/0.9 must not get headers... */

						p = parse_header_field(htrd, p, ((htrd->option & HIO_HTRD_TRAILERS)? &htrd->re.trailers: &htrd->re.hdrtab));
						if (HIO_UNLIKELY(!p)) return HIO_NULL;
					}
					while (1);

					htrd->fed.s.chunk.phase = GET_CHUNK_DONE;
					goto done;
				}

			case '\r':
				if (htrd->fed.s.crlf == 0 || htrd->fed.s.crlf == 2) 
					htrd->fed.s.crlf++;
				else htrd->fed.s.crlf = 1;
				break;

			default:
				/* mark that neither CR nor LF was seen */
				htrd->fed.s.crlf = 0;
				break;
		}
	}

	if (push_to_buffer (htrd, &htrd->fed.b.tra, req, ptr - req) <= -1) 
		return HIO_NULL;

done:
	return ptr;
}

/* feed the percent encoded string */
int hio_htrd_feed (hio_htrd_t* htrd, const hio_bch_t* req, hio_oow_t len, hio_oow_t* rem)
{
	const hio_bch_t* end = req + len;
	const hio_bch_t* ptr = req;
	int header_completed_during_this_feed = 0;
	hio_oow_t avail;

	HIO_ASSERT (htrd->hio, len > 0);

	if (htrd->flags & FEEDING_SUSPENDED)
	{
		htrd->errnum = HIO_HTRD_ESUSPENDED;
		return -1;
	}

	/*if (htrd->option & HIO_HTRD_DUMMY)*/
	if (htrd->flags & FEEDING_DUMMIFIED)
	{
		/* treat everything as contents.
		 * i don't care about headers or whatsoever. */
		return push_content(htrd, req, len);
	}

	/* does this goto drop code maintainability? */
	if (htrd->fed.s.need > 0) 
	{
		/* we're in need of as many octets as htrd->fed.s.need 
		 * for contents body. make a proper jump to resume
		 * content handling */
		goto content_resume;
	}

	switch (htrd->fed.s.chunk.phase)
	{
		case GET_CHUNK_LEN:
			goto dechunk_resume;

		case GET_CHUNK_DATA:
			/* this won't be reached as htrd->fed.s.need 
			 * is greater than 0 if GET_CHUNK_DATA is true */
			goto content_resume;

		case GET_CHUNK_CRLF:
			goto dechunk_crlf;

		case GET_CHUNK_TRAILERS:
			goto dechunk_get_trailers;
	}

	htrd->clean = 0; /* mark that htrd is in need of some data */

	while (ptr < end)
	{
		register hio_bch_t b = *ptr++;

#if 0
		if (htrd->option & HIO_HTRD_SKIP_EMPTY_LINES &&
		    htrd->fed.s.plen <= 0 && is_whspace_octet(b)) 
		{
			/* let's drop leading whitespaces across multiple
			 * lines */
			req++;
			continue;
		}
#endif

		switch (b)
		{
			case '\0':
				/* guarantee that the request does not contain
				 * a null character */
				htrd->errnum = HIO_HTRD_EBADRE;
				return -1;

			case '\n':
			{
				if (htrd->fed.s.crlf <= 1) 
				{
					/* htrd->fed.s.crlf == 0
					 *   => CR was not seen
					 * htrd->fed.s.crlf == 1
					 *   => CR was seen 
					 * whatever the current case is, 
					 * mark the first LF is seen here.
					 */
					htrd->fed.s.crlf = 2;
				}
				else
				{
					/* htrd->fed.s.crlf == 2
					 *   => no 2nd CR before LF
					 * htrd->fed.s.crlf == 3
					 *   => 2nd CR before LF
					 */

					/* we got a complete request header. */
					HIO_ASSERT (htrd->hio, htrd->fed.s.crlf <= 3);

					/* reset the crlf state */
					htrd->fed.s.crlf = 0;
					/* reset the raw request length */
					htrd->fed.s.plen = 0;

					if (parse_initial_line_and_headers(htrd, req, ptr - req) <= -1) 
					{
						return -1;
					}

					/* compelete request header is received */
					header_completed_during_this_feed = 1;

////////////////////////////////////////////////////////////////////////////////////////////////////
					if (htrd->recbs.peek(htrd, &htrd->re) <= -1)
					{
						/* need to clear request on error? 
						clear_feed (htrd); */
						return -1;
					}
////////////////////////////////////////////////////////////////////////////////////////////////////

					/* carry on processing content body fed together with the header */
					if (htrd->re.flags & HIO_HTRE_ATTR_CHUNKED)
					{
						/* transfer-encoding: chunked */
						/*HIO_ASSERT (htrd->hio, !(htrd->re.flags & HIO_HTRE_ATTR_LENGTH)); <- this assertion is wrong. non-conforming client may include content-length while transfer-encoding is chunked*/

					dechunk_start:
						htrd->fed.s.chunk.phase = GET_CHUNK_LEN;
						htrd->fed.s.chunk.len = 0;
						htrd->fed.s.chunk.count = 0;

					dechunk_resume:
						ptr = getchunklen(htrd, ptr, end - ptr);
						if (HIO_UNLIKELY(!ptr)) return -1;

						if (htrd->fed.s.chunk.phase == GET_CHUNK_LEN)
						{
							/* still in the GET_CHUNK_LEN state.
							 * the length has been partially read. */
							goto feedme_more;
						}
						else if (htrd->fed.s.chunk.phase == GET_CHUNK_TRAILERS)
						{
							/* this state is reached after the
							 * last chunk length 0 is read. The next
							 * empty line immediately completes 
							 * a content body. so i need to adjust
							 * this crlf status to 2 as if a trailing
							 * header line has been read. */
							htrd->fed.s.crlf = 2;

						dechunk_get_trailers:
							ptr = get_trailing_headers(htrd, ptr, end);
							if (!HIO_UNLIKELY(ptr)) return -1;

							if (htrd->fed.s.chunk.phase == GET_CHUNK_TRAILERS)
							{
								/* still in the same state.
								 * the trailers have not been processed fully */
								goto feedme_more;
							}
						}
					}
					else
					{
						/* we need to read as many octets as
						 * Content-Length */
						if ((htrd->option & HIO_HTRD_RESPONSE) && 
						    !(htrd->re.flags & HIO_HTRE_ATTR_LENGTH) &&
						    !(htrd->re.flags & HIO_HTRE_ATTR_KEEPALIVE))
						{
							/* for a response, no content-length and 
							 * no chunk are specified and 'connection' 
							 * is to close. i must read until the 
							 * connection is closed. however, there isn't 
							 * any good way to know when to stop from 
							 * within this function. so the caller
							 * can call hio_htrd_halt() for this. */

							/* set this to the maximum in a type safe way
							 * assuming it's unsigned. the problem of
							 * the current implementation is that 
							 * it can't receive more than  */
							htrd->fed.s.need = 0;
							htrd->fed.s.need = ~htrd->fed.s.need; 
							htrd->fed.s.flags |= CONSUME_UNTIL_CLOSE;
						}
						else if ((htrd->option & HIO_HTRD_RESPONSE) &&
						         !(htrd->re.flags & HIO_HTRE_ATTR_LENGTH) &&
						          (htrd->re.flags & HIO_HTRE_ATTR_KEEPALIVE))
						{
							/* 
							 * what the hell! 
							 * no content-length, but keep-alive and not chunked.
							 * there's no way to know how large the contents is.
							 *
							 * For a request 'http://php.net/manual/en/function.curl-strerror.php' containing the following header fields:
							 *    If-Modified-Since: Fri, 31 Oct 2014 11:12:47 GMT
							 *    Accept-Encoding: gzip, deflate
							 *
							 * the service gave this response as of this writing:
							 * 
HTTP/1.1 304 Not Modified
Server: nginx/1.6.2
Date: Tue, 04 Nov 2014 15:45:46 GMT
Connection: keep-alive
Vary: Accept-Encoding
Set-Cookie: LAST_LANG=en; expires=Wed, 04-Nov-2015 15:45:46 GMT; Max-Age=31536000; path=/; domain=.php.net
Set-Cookie: COUNTRY=KOR%2C220.121.110.171; expires=Tue, 11-Nov-2014 15:45:46 GMT; Max-Age=604800; path=/; domain=.php.net

XXXXXXXX
							 * 
							 * XXXXXXX is some compressed garbage included in the contents-body.
							 * why does the service behave this way? is it a server bug or am i doing anything wrong?
							 *
							 * <<WORKAROUND>>
							 * i decided to drop whatever trailing data avaiable
							 * after the header fields for this feeding.
							 * if more contents are fed in later, it will still
							 * end up with a bad request error. */
							ptr = end;
							htrd->fed.s.need = 0;
						}
						else
						{
							htrd->fed.s.need = htrd->re.attr.content_length;
						}
					}

					if (htrd->fed.s.need > 0)
					{
						/* content-length or chunked data length 
						 * specified */
					content_resume:
						avail = end - ptr;
						if (avail <= 0)
						{
							/* we didn't get a complete content yet */

							/* avail can be 0 if data fed ends with
							 * a chunk length withtout actual data. 
							 * so i check if avail is greater than 0
							 * in order not to push empty content. */
							goto feedme_more; 
						}
						else if (avail < htrd->fed.s.need)
						{
							/* the data is not as large as needed */
							if (push_content(htrd, ptr, avail) <= -1) 
							{
								return -1;
							}

							if (!(htrd->fed.s.flags & CONSUME_UNTIL_CLOSE)) 
							{
								/* i don't decrement htrd->fed.s.need
								 * if i should read until connection is closed.
								 * well, unless set your own callback,
								 * push_content() above will fail 
								 * if too much has been received already */
								htrd->fed.s.need -= avail;
							}

							/* we didn't get a complete content yet */
							goto feedme_more; 
						}
						else 
						{
							/* we got all or more than needed */
							if (push_content (htrd, ptr, htrd->fed.s.need) <= -1) 
							{
								return -1;
							}
							ptr += htrd->fed.s.need;
							if (!(htrd->fed.s.flags & CONSUME_UNTIL_CLOSE)) 
								htrd->fed.s.need = 0;
						}
					}

					if (htrd->fed.s.chunk.phase == GET_CHUNK_DATA)
					{
						HIO_ASSERT (htrd->hio, htrd->fed.s.need == 0);
						htrd->fed.s.chunk.phase = GET_CHUNK_CRLF;

					dechunk_crlf:
						while (ptr < end && is_space_octet(*ptr)) ptr++;
						if (ptr < end)
						{
							if (*ptr == '\n') 
							{
								/* end of chunk data. */
								ptr++;

								/* more octets still available. 
								 * let it decode the next chunk 
								 */
								if (ptr < end) goto dechunk_start; 

								/* no more octets available after 
								 * chunk data. the chunk state variables
								 * need to be reset when a jump is made
								 * to dechunk_resume upon the next call
								 */
								htrd->fed.s.chunk.phase = GET_CHUNK_LEN;
								htrd->fed.s.chunk.len = 0;
								htrd->fed.s.chunk.count = 0;

								goto feedme_more;
							}
							else
							{
								/* redundant character ... */
								htrd->errnum = HIO_HTRD_EBADRE;
								return -1;
							}
						}
						else
						{
							/* data not enough */
							goto feedme_more;
						}
					}

					/* the content has been received fully */
					hio_htre_completecontent (&htrd->re);

#if 0 // XXXX
					if (header_completed_during_this_feed && htrd->recbs.peek)
					{
						/* the peek handler has not been executed.
						 * this can happen if this function is fed with
						 * at least the ending part of a complete header
						 * plus complete content body and the header 
						 * of the next request. */
						int n;
						n = htrd->recbs.peek(htrd, &htrd->re);
						if (n <= -1)
						{
							/* need to clear request on error? 
							clear_feed (htrd); */
							return -1;
						}

						header_completed_during_this_feed = 0;
					}
#endif

					if (htrd->recbs.poke)
					{
						int n;
						n = htrd->recbs.poke(htrd, &htrd->re);
						if (n <= -1)
						{
							/* need to clear request on error? 
							clear_feed (htrd); */
							return -1;
						}
					}

#if 0
hio_printf (HIO_T("CONTENT_LENGTH %d, RAW HEADER LENGTH %d\n"), 
	(int)HIO_BECS_LEN(&htrd->re.content),
	(int)HIO_BECS_LEN(&htrd->fed.b.raw));
#endif
					clear_feed (htrd);

					if (rem) 
					{
						/* stop even if there are fed data left */
						*rem = end - ptr; /* remaining feeds */
						return 0; /* to indicate completed when the 'rem' is not NULL */
					}
					else if (ptr >= end)
					{
						/* no more feeds to handle */
						return 0;
					}

					if (htrd->flags & FEEDING_SUSPENDED) /* in case the callback called hio_htrd_suspend() */
					{
						htrd->errnum = HIO_HTRD_ESUSPENDED;
						return -1;
					}

					/*if (htrd->option & HIO_HTRD_DUMMY)*/
					if (htrd->flags & FEEDING_DUMMIFIED) /* in case the callback called hio_htrd_dummify() */
					{
						/* once the mode changes to RAW in a callback,
						 * left-over is pushed as contents */
						if (ptr < end)
							return push_content(htrd, ptr, end - ptr);
						else
							return 0;
					}

					/* let ptr point to the next character to LF or 
					 * the optional contents */
					req = ptr; 

					/* since there are more to handle, i mark that
					 * htrd is in need of some data. this may
					 * not be really compatible with SKIP_EMPTY_LINES. 
					 * SHOULD I simply remove the option? */
					htrd->clean = 0; 
				}
				break;
			}

			case '\r':
				if (htrd->fed.s.crlf == 0 || htrd->fed.s.crlf == 2) 
					htrd->fed.s.crlf++;
				else htrd->fed.s.crlf = 1;
				break;

			default:
				/* increment length of a request in raw 
				 * excluding crlf */
				htrd->fed.s.plen++; 
				/* mark that neither CR nor LF was seen */
				htrd->fed.s.crlf = 0;
		}
	}

	if (ptr > req)
	{
		/* enbuffer the incomplete request */
		if (push_to_buffer(htrd, &htrd->fed.b.raw, req, ptr - req) <= -1) 
		{
			return -1;
		}
	}

feedme_more:
#if 0 //XXXX
	if (header_completed_during_this_feed && htrd->recbs.peek)
	{
		int n;
		n = htrd->recbs.peek(htrd, &htrd->re);
		if (n <= -1)
		{
			/* need to clear request on error? 
			clear_feed (htrd); */
			return -1;
		}
	}
#endif

	if (rem) *rem = 0;
	return 0;
}

int hio_htrd_halt (hio_htrd_t* htrd)
{
	if ((htrd->fed.s.flags & CONSUME_UNTIL_CLOSE) || !htrd->clean)
	{
		hio_htre_completecontent (&htrd->re);

		if (htrd->recbs.poke)
		{
			int n;
			n = htrd->recbs.poke(htrd, &htrd->re);
			if (n <= -1)
			{
				/* need to clear request on error? 
				clear_feed (htrd); */
				return -1;
			}
		}

		clear_feed (htrd);
	}

	return 0;
}

void hio_htrd_suspend (hio_htrd_t* htrd)
{
	htrd->flags |= FEEDING_SUSPENDED;
}

void hio_htrd_resume (hio_htrd_t* htrd)
{
	htrd->flags &= ~FEEDING_SUSPENDED;
}

void hio_htrd_dummify (hio_htrd_t* htrd)
{
	htrd->flags |= FEEDING_DUMMIFIED;
}

void hio_htrd_undummify (hio_htrd_t* htrd)
{
	htrd->flags &= ~FEEDING_DUMMIFIED;
}

