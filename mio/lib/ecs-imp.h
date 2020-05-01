/*
 * $Id$
 *
    Copyright (c) 2006-2020 Chung, Hyung-Hwan. All rights reserved.

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


str_t* FN(open) (mio_t* mio, mio_oow_t xtnsize, mio_oow_t capa)
{
	str_t* str;

	str = (str_t*)mio_allocmem(mio, MIO_SIZEOF(str_t) + xtnsize);
	if (str)
	{
		if (FN(init)(str, mio, capa) <= -1)
		{
			mio_freemem (mio, str);
			str = MIO_NULL;
		}
		else
		{
			MIO_MEMSET (str + 1, 0, xtnsize);
		}
	}
	return str;
}

void FN(close) (str_t* str)
{
	FN(fini) (str);
	mio_freemem (str->mio, str);
}

int FN(init) (str_t* str, mio_t* mio, mio_oow_t capa)
{
	MIO_MEMSET (str, 0, MIO_SIZEOF(str_t));

	str->mio = mio;
	str->sizer = MIO_NULL;

	if (capa == 0) str->val.ptr = MIO_NULL;
	else
	{
		str->val.ptr = (char_t*)mio_allocmem(mio, MIO_SIZEOF(char_t) * (capa + 1));
		if (!str->val.ptr) return -1;
		str->val.ptr[0] = '\0';
	}

	str->val.len = 0;
	str->capa = capa;

	return 0;
}

void FN(fini) (str_t* str)
{
	if (str->val.ptr) mio_freemem (str->mio, str->val.ptr);
}

int FN(yield) (str_t* str, cstr_t* buf, mio_oow_t newcapa)
{
	char_t* tmp;

	if (newcapa == 0) tmp = MIO_NULL;
	else
	{
		tmp = (char_t*)mio_allocmem(str->mio, MIO_SIZEOF(char_t) * (newcapa + 1));
		if (!tmp) return -1;
		tmp[0] = '\0';
	}

	if (buf) *buf = str->val;

	str->val.ptr = tmp;
	str->val.len = 0;
	str->capa = newcapa;

	return 0;
}

char_t* FN(yieldptr) (str_t* str, mio_oow_t newcapa)
{
	cstr_t mx;
	if (FN(yield)(str, &mx, newcapa) <= -1) return MIO_NULL;
	return mx.ptr;
}


mio_oow_t FN(setcapa) (str_t* str, mio_oow_t capa)
{
	char_t* tmp;

	if (capa == str->capa) return capa;

	tmp = (char_t*)mio_reallocmem(str->mio, str->val.ptr, MIO_SIZEOF(char_t) * (capa+1));
	if (!tmp) return (mio_oow_t)-1;

	if (capa < str->val.len)
	{
		str->val.len = capa;
		tmp[capa] = '\0';
	}

	str->capa = capa;
	str->val.ptr = tmp;

	return str->capa;
}

mio_oow_t FN(setlen) (str_t* str, mio_oow_t len)
{
	if (len == str->val.len) return len;
	if (len < str->val.len) 
	{
		str->val.len = len;
		str->val.ptr[len] = '\0';
		return len;
	}

	if (len > str->capa)
	{
		if (FN(setcapa)(str, len) == (mio_oow_t)-1) return (mio_oow_t)-1;
	}

	while (str->val.len < len) str->val.ptr[str->val.len++] = ' ';
	str->val.ptr[str->val.len] = '\0';
	return str->val.len;
}

void FN(clear) (str_t* str)
{
	str->val.len = 0;
	if (str->val.ptr)
	{
		MIO_ASSERT (str->mio, str->capa >= 1);
		str->val.ptr[0] = '\0';
	}
}

void FN(swap) (str_t* str, str_t* str1)
{
	str_t tmp;

	tmp.val.ptr = str->val.ptr;
	tmp.val.len = str->val.len;
	tmp.capa = str->capa;
	tmp.mio = str->mio;

	str->val.ptr = str1->val.ptr;
	str->val.len = str1->val.len;
	str->capa = str1->capa;
	str->mio = str1->mio;

	str1->val.ptr = tmp.val.ptr;
	str1->val.len = tmp.val.len;
	str1->capa = tmp.capa;
	str1->mio = tmp.mio;
}


mio_oow_t FN(cpy) (str_t* str, const char_t* s)
{
	/* TODO: improve it */
	return FN(ncpy)(str, s, count_chars(s));
}

mio_oow_t FN(ncpy) (str_t* str, const char_t* s, mio_oow_t len)
{
	if (len > str->capa || str->capa <= 0)
	{
		mio_oow_t tmp;

		/* if the current capacity is 0 and the string len to copy is 0
		 * we can't simply pass 'len' as the new capapcity.
		 * ecs_setcapa() won't do anything the current capacity of 0
		 * is the same as new capacity required. note that when str->capa 
		 * is 0, str->val.ptr is MIO_NULL. However, this is copying operation.
		 * Copying a zero-length string may indicate that str->val.ptr must
		 * not be MIO_NULL. so I simply pass 1 as the new capacity */
		tmp = FN(setcapa)(str, ((str->capa <= 0 && len <= 0)? 1: len));
		if (tmp == (mio_oow_t)-1) return (mio_oow_t)-1;
	}

	MIO_MEMCPY (&str->val.ptr[0], s, len * MIO_SIZEOF(*s));
	str->val.ptr[len] = '\0';
	str->val.len = len;
	return len;
}

mio_oow_t FN(cat) (str_t* str, const char_t* s)
{
	/* TODO: improve it. no counting */
	return FN(ncat)(str, s, count_chars(s));
}

static int FN(resize_for_ncat) (str_t* str, mio_oow_t len)
{
	if (len > str->capa - str->val.len) 
	{
		mio_oow_t ncapa, mincapa;

		/* let the minimum capacity be as large as 
		 * to fit in the new substring */
		mincapa = str->val.len + len;

		if (!str->sizer)
		{
			/* increase the capacity by the length to add */
			ncapa = mincapa;
			/* if the new capacity is less than the double,
			 * just double it */
			if (ncapa < str->capa * 2) ncapa = str->capa * 2;
		}
		else
		{
			/* let the user determine the new capacity.
			 * pass the minimum capacity required as a hint */
			ncapa = str->sizer(str, mincapa);
			/* if no change in capacity, return current length */
			if (ncapa == str->capa) return 0;
		}

		/* change the capacity */
		do
		{
			if (FN(setcapa)(str, ncapa) != (mio_oow_t)-1) break;
			if (ncapa <= mincapa) return -1;
			ncapa--;
		}
		while (1);
	}
	else if (str->capa <= 0 && len <= 0)
	{
		MIO_ASSERT (str->mio, str->val.ptr == MIO_NULL);
		MIO_ASSERT (str->mio, str->val.len <= 0);
		if (FN(setcapa)(str, 1) == (mio_oow_t)-1) return -1;
	}

	return 1;
}

mio_oow_t FN(ncat) (str_t* str, const char_t* s, mio_oow_t len)
{
	int n;
	mio_oow_t i, j;

	n = FN(resize_for_ncat)(str, len);
	if (n <= -1) return (mio_oow_t)-1;
	if (n == 0) return str->val.len;

	if (len > str->capa - str->val.len) 
	{
		/* copy as many characters as the number of cells available.
		 * if the capacity has been decreased, len is adjusted here */
		len = str->capa - str->val.len;
	}

	/*
	MIO_MEMCPY (&str->val.ptr[str->val.len], s, len*MIO_SIZEOF(*s));
	str->val.len += len;
	str->val.ptr[str->val.len] = T('\0');
	*/
	for (i = 0, j = str->val.len ; i < len; j++, i++) str->val.ptr[j] = s[i];
	str->val.ptr[j] = '\0';
	str->val.len = j;

	return str->val.len;
}

mio_oow_t FN(nrcat) (str_t* str, const char_t* s, mio_oow_t len)
{
	int n;
	mio_oow_t i, j;

	n = FN(resize_for_ncat)(str, len);
	if (n <= -1) return (mio_oow_t)-1;
	if (n == 0) return str->val.len;

	if (len > str->capa - str->val.len) len = str->capa - str->val.len;

	for (i = len, j = str->val.len ; i > 0; j++) str->val.ptr[j] = s[--i];
	str->val.ptr[j] = '\0';
	str->val.len = j;

	return str->val.len;
}

mio_oow_t FN(ccat) (str_t* str, char_t c)
{
	return FN(ncat)(str, &c, 1);
}

mio_oow_t FN(nccat) (str_t* str, char_t c, mio_oow_t len)
{
	while (len > 0)
	{
		if (FN(ncat)(str, &c, 1) == (mio_oow_t)-1) return (mio_oow_t)-1;
		len--;
	}
	return str->val.len;
}

mio_oow_t FN(del) (str_t* str, mio_oow_t index, mio_oow_t size)
{
	if (str->val.ptr && index < str->val.len && size > 0)
	{
		mio_oow_t nidx = index + size;
		if (nidx >= str->val.len)
		{
			str->val.ptr[index] = '\0';
			str->val.len = index;
		}
		else
		{
			MIO_MEMMOVE (&str->val.ptr[index], &str->val.ptr[nidx], MIO_SIZEOF(*str->val.ptr) * (str->val.len - nidx + 1));
			str->val.len -= size;
		}
	}

	return str->val.len;
}

mio_oow_t FN(amend) (str_t* str, mio_oow_t pos, mio_oow_t len, const char_t* repl)
{
	mio_oow_t max_len;
	mio_oow_t repl_len = count_chars(repl);

	if (pos >= str->val.len) pos = str->val.len;
	max_len = str->val.len - pos;
	if (len > max_len) len = max_len;

	if (len > repl_len)
	{
		FN(del) (str, pos, len - repl_len);
	}
	else if (len < repl_len)
	{
		mio_oow_t old_ecs_len = str->val.len;
		if (FN(setlen)(str, str->val.len + repl_len - len) == (mio_oow_t)-1) return (mio_oow_t)-1;
		MIO_MEMMOVE (&str->val.ptr[pos + repl_len], &str->val.ptr[pos + len], MIO_SIZEOF(*repl) * (old_ecs_len - (pos + len)));
	}

	if (repl_len > 0) MIO_MEMMOVE (&str->val.ptr[pos], repl, MIO_SIZEOF(*repl) * repl_len); 
	return str->val.len;
}

static int FN(put_bchars) (mio_fmtout_t* fmtout, const mio_bch_t* ptr, mio_oow_t len)
{
#if defined(BUILD_UECS)
	mio_uecs_t* uecs = (mio_uecs_t*)fmtout->ctx;
	if (mio_uecs_ncatbchars(uecs, ptr, len, uecs->mio->_cmgr, 1) == (mio_oow_t)-1) return -1;
#else
	mio_becs_t* becs = (mio_becs_t*)fmtout->ctx;
	if (mio_becs_ncat(becs, ptr, len) == (mio_oow_t)-1) return -1;
#endif
	return 1; /* success. carry on */
}

static int FN(put_uchars) (mio_fmtout_t* fmtout, const mio_uch_t* ptr, mio_oow_t len)
{
#if defined(BUILD_UECS)
	mio_uecs_t* uecs = (mio_uecs_t*)fmtout->ctx;
	if (mio_uecs_ncat(uecs, ptr, len) == (mio_oow_t)-1) return -1;
#else
	mio_becs_t* becs = (mio_becs_t*)fmtout->ctx;
	if (mio_becs_ncatuchars(becs, ptr, len, becs->mio->_cmgr) == (mio_oow_t)-1) return -1;
#endif
	return 1; /* success. carry on */
}

mio_oow_t FN(vfcat) (str_t* str, const char_t* fmt, va_list ap)
{
	mio_fmtout_t fo;

	MIO_MEMSET (&fo, 0, MIO_SIZEOF(fo));
	//fo.mmgr = str->mio->_mmgr;
	fo.putbchars = FN(put_bchars);
	fo.putuchars = FN(put_uchars);
	fo.ctx = str;

#if defined(BUILD_UECS)
	if (mio_ufmt_outv(&fo, fmt, ap) <= -1) return -1;
#else
	if (mio_bfmt_outv(&fo, fmt, ap) <= -1) return -1;
#endif
	return str->val.len;
}

mio_oow_t FN(fcat) (str_t* str, const char_t* fmt, ...)
{
	mio_oow_t x;
	va_list ap;

	va_start (ap, fmt);
	x = FN(vfcat)(str, fmt, ap);
	va_end (ap);

	return x;
}

mio_oow_t FN(vfmt) (str_t* str, const char_t* fmt, va_list ap)
{
	mio_fmtout_t fo;

	MIO_MEMSET (&fo, 0, MIO_SIZEOF(fo));
	//fo.mmgr = str->mio->_mmgr;
	fo.putbchars = FN(put_bchars);
	fo.putuchars = FN(put_uchars);
	fo.ctx = str;

	FN(clear) (str);

#if defined(BUILD_UECS)
	if (mio_ufmt_outv(&fo, fmt, ap) <= -1) return -1;
#else
	if (mio_bfmt_outv(&fo, fmt, ap) <= -1) return -1;
#endif
	return str->val.len;
}

mio_oow_t FN(fmt) (str_t* str, const char_t* fmt, ...)
{
	mio_oow_t x;
	va_list ap;

	va_start (ap, fmt);
	x = FN(vfmt)(str, fmt, ap);
	va_end (ap);

	return x;
}
