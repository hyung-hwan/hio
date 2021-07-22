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
    IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WAfRRANTIES
    OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
    IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
    INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
    NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
    THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "hio-prv.h"
#include <hio-fmt.h>

static hio_ooch_t errstr_0[] = {'n', 'o', ' ', 'e', 'r', 'r', 'o', 'r', '\0' };
static hio_ooch_t errstr_1[] = {'g', 'e', 'n', 'e', 'r', 'i', 'c', ' ', 'e', 'r', 'r', 'o', 'r', '\0' };
static hio_ooch_t errstr_2[] = {'n', 'o', 't', ' ', 'i', 'm', 'p', 'l', 'e', 'm', 'e', 'n', 't', 'e', 'd', '\0' };
static hio_ooch_t errstr_3[] = {'s', 'y', 's', 't', 'e', 'm', ' ', 'e', 'r', 'r', 'o', 'r', '\0' };
static hio_ooch_t errstr_4[] = {'i', 'n', 't', 'e', 'r', 'n', 'a', 'l', ' ', 'e', 'r', 'r', 'o', 'r', '\0' };
static hio_ooch_t errstr_5[] = {'i', 'n', 's', 'u', 'f', 'f', 'i', 'c', 'i', 'e', 'n', 't', ' ', 's', 'y', 's', 't', 'e', 'm', ' ', 'm', 'e', 'm', 'o', 'r', 'y', '\0' };
static hio_ooch_t errstr_6[] = {'i', 'n', 'v', 'a', 'l', 'i', 'd', ' ', 'p', 'a', 'r', 'a', 'm', 'e', 't', 'e', 'r', ' ', 'o', 'r', ' ', 'd', 'a', 't', 'a', '\0' };
static hio_ooch_t errstr_7[] = {'d', 'a', 't', 'a', ' ', 'n', 'o', 't', ' ', 'f', 'o', 'u', 'n', 'd', '\0' };
static hio_ooch_t errstr_8[] = {'e', 'x', 'i', 's', 't', 'i', 'n', 'g', '/', 'd', 'u', 'p', 'l', 'i', 'c', 'a', 't', 'e', ' ', 'd', 'a', 't', 'a', '\0' };
static hio_ooch_t errstr_9[] = {'s', 'y', 's', 't', 'e', 'm', ' ', 'b', 'u', 's', 'y', '\0' };
static hio_ooch_t errstr_10[] = {'a', 'c', 'c', 'e', 's', 's', ' ', 'd', 'e', 'n', 'i', 'e', 'd', '\0' };
static hio_ooch_t errstr_11[] = {'o', 'p', 'e', 'r', 'a', 't', 'i', 'o', 'n', ' ', 'n', 'o', 't', ' ', 'p', 'e', 'r', 'm', 'i', 't', 't', 'e', 'd', '\0' };
static hio_ooch_t errstr_12[] = {'n', 'o', 't', ' ', 'd', 'i', 'r', 'e', 'c', 't', 'o', 'r', 'y', '\0' };
static hio_ooch_t errstr_13[] = {'i', 'n', 't', 'e', 'r', 'r', 'u', 'p', 't', 'e', 'd', '\0' };
static hio_ooch_t errstr_14[] = {'p', 'i', 'p', 'e', ' ', 'e', 'r', 'r', 'o', 'r', '\0' };
static hio_ooch_t errstr_15[] = {'r', 'e', 's', 'o', 'u', 'r', 'c', 'e', ' ', 't', 'e', 'm', 'p', 'o', 'r', 'a', 'r', 'i', 'l', 'y', ' ', 'u', 'n', 'a', 'v', 'a', 'i', 'l', 'a', 'b', 'l', 'e', '\0' };
static hio_ooch_t errstr_16[] = {'b', 'a', 'd', ' ', 's', 'y', 's', 't', 'e', 'm', ' ', 'h', 'a', 'n', 'd', 'l', 'e', '\0' };
static hio_ooch_t errstr_17[] = {'b', 'a', 'd', ' ', 'r', 'e', 'q', 'u', 'e', 's', 't', ' ', 'o', 'r', ' ', 'r', 'e', 's', 'p', 'o', 'n', 's', 'e', '\0' };
static hio_ooch_t errstr_18[] = {'t', 'o', 'o', ' ', 'm', 'a', 'n', 'y', ' ', 'o', 'p', 'e', 'n', ' ', 'f', 'i', 'l', 'e', 's', '\0' };
static hio_ooch_t errstr_19[] = {'t', 'o', 'o', ' ', 'm', 'a', 'n', 'y', ' ', 'o', 'p', 'e', 'n', ' ', 'f', 'i', 'l', 'e', 's', '\0' };
static hio_ooch_t errstr_20[] = {'I', '/', 'O', ' ', 'e', 'r', 'r', 'o', 'r', '\0' };
static hio_ooch_t errstr_21[] = {'e', 'n', 'c', 'o', 'd', 'i', 'n', 'g', ' ', 'c', 'o', 'n', 'v', 'e', 'r', 's', 'i', 'o', 'n', ' ', 'e', 'r', 'r', 'o', 'r', '\0' };
static hio_ooch_t errstr_22[] = {'i', 'n', 's', 'u', 'f', 'f', 'i', 'c', 'i', 'e', 'n', 't', ' ', 'd', 'a', 't', 'a', ' ', 'f', 'o', 'r', ' ', 'e', 'n', 'c', 'o', 'd', 'i', 'n', 'g', ' ', 'c', 'o', 'n', 'v', 'e', 'r', 's', 'i', 'o', 'n', '\0' };
static hio_ooch_t errstr_23[] = {'b', 'u', 'f', 'f', 'e', 'r', ' ', 'f', 'u', 'l', 'l', '\0' };
static hio_ooch_t errstr_24[] = {'c', 'o', 'n', 'n', 'e', 'c', 't', 'i', 'o', 'n', ' ', 'r', 'e', 'f', 'u', 's', 'e', 'd', '\0' };
static hio_ooch_t errstr_25[] = {'c', 'o', 'n', 'n', 'e', 'c', 't', 'i', 'o', 'n', ' ', 'r', 'e', 's', 'e', 't', '\0' };
static hio_ooch_t errstr_26[] = {'n', 'o', ' ', 'c', 'a', 'p', 'a', 'b', 'i', 'l', 'i', 't', 'y', '\0' };
static hio_ooch_t errstr_27[] = {'t', 'i', 'm', 'e', 'd', ' ', 'o', 'u', 't', '\0' };
static hio_ooch_t errstr_28[] = {'n', 'o', ' ', 'r', 'e', 's', 'p', 'o', 'n', 's', 'e', '\0' };
static hio_ooch_t errstr_29[] = {'u', 'n', 'a', 'b', 'l', 'e', ' ', 't', 'o', ' ', 'm', 'a', 'k', 'e', ' ', 'd', 'e', 'v', 'i', 'c', 'e', '\0' };
static hio_ooch_t errstr_30[] = {'d', 'e', 'v', 'i', 'c', 'e', ' ', 'e', 'r', 'r', 'o', 'r', '\0' };
static hio_ooch_t errstr_31[] = {'d', 'e', 'v', 'i', 'c', 'e', ' ', 'h', 'a', 'n', 'g', '-', 'u', 'p', '\0' };
static hio_ooch_t* errstr[] =
{
	errstr_0, errstr_1, errstr_2, errstr_3, errstr_4,
	errstr_5, errstr_6, errstr_7, errstr_8, errstr_9,
	errstr_10, errstr_11, errstr_12, errstr_13, errstr_14,
	errstr_15, errstr_16, errstr_17, errstr_18, errstr_19,
	errstr_20, errstr_21, errstr_22, errstr_23, errstr_24,
	errstr_25, errstr_26, errstr_27, errstr_28, errstr_29,
	errstr_30, errstr_31
};


/* -------------------------------------------------------------------------- 
 * ERROR NUMBER TO STRING CONVERSION
 * -------------------------------------------------------------------------- */
const hio_ooch_t* hio_errnum_to_errstr (hio_errnum_t errnum)
{
	static hio_ooch_t e_unknown[] = {'u','n','k','n','o','w','n',' ','e','r','r','o','r','\0'};
	return (errnum >= 0 && errnum < HIO_COUNTOF(errstr))? errstr[errnum]: e_unknown;
}

/* -------------------------------------------------------------------------- 
 * ERROR NUMBER/MESSAGE HANDLING
 * -------------------------------------------------------------------------- */
const hio_ooch_t* hio_geterrstr (hio_t* hio)
{
	return hio_errnum_to_errstr(hio->errnum);
}

const hio_ooch_t* hio_geterrmsg (hio_t* hio)
{
	if (hio->errmsg.len <= 0) return hio_errnum_to_errstr(hio->errnum);
	return hio->errmsg.buf;
}

void hio_geterrinf (hio_t* hio, hio_errinf_t* info)
{
	info->num = hio_geterrnum(hio);
	hio_copy_oocstr (info->msg, HIO_COUNTOF(info->msg), hio_geterrmsg(hio));
}

const hio_ooch_t* hio_backuperrmsg (hio_t* hio)
{
	hio_copy_oocstr (hio->errmsg.tmpbuf.ooch, HIO_COUNTOF(hio->errmsg.tmpbuf.ooch), hio_geterrmsg(hio));
	return hio->errmsg.tmpbuf.ooch;
}

void hio_seterrnum (hio_t* hio, hio_errnum_t errnum)
{
	if (hio->_shuterr) return;
	hio->errnum = errnum; 
	hio->errmsg.len = 0; 
}

static int err_bcs (hio_fmtout_t* fmtout, const hio_bch_t* ptr, hio_oow_t len)
{
	hio_t* hio = (hio_t*)fmtout->ctx;
	hio_oow_t max;

	max = HIO_COUNTOF(hio->errmsg.buf) - hio->errmsg.len - 1;

#if defined(HIO_OOCH_IS_UCH)
	if (max <= 0) return 1;
	hio_conv_bchars_to_uchars_with_cmgr (ptr, &len, &hio->errmsg.buf[hio->errmsg.len], &max, hio_getcmgr(hio), 1);
	hio->errmsg.len += max;
#else
	if (len > max) len = max;
	if (len <= 0) return 1;
	HIO_MEMCPY (&hio->errmsg.buf[hio->errmsg.len], ptr, len * HIO_SIZEOF(*ptr));
	hio->errmsg.len += len;
#endif

	hio->errmsg.buf[hio->errmsg.len] = '\0';

	return 1; /* success */
}

static int err_ucs (hio_fmtout_t* fmtout, const hio_uch_t* ptr, hio_oow_t len)
{
	hio_t* hio = (hio_t*)fmtout->ctx;
	hio_oow_t max;

	max = HIO_COUNTOF(hio->errmsg.buf) - hio->errmsg.len - 1;

#if defined(HIO_OOCH_IS_UCH)
	if (len > max) len = max;
	if (len <= 0) return 1;
	HIO_MEMCPY (&hio->errmsg.buf[hio->errmsg.len], ptr, len * HIO_SIZEOF(*ptr));
	hio->errmsg.len += len;
#else
	if (max <= 0) return 1;
	hio_conv_uchars_to_bchars_with_cmgr (ptr, &len, &hio->errmsg.buf[hio->errmsg.len], &max, hio_getcmgr(hio));
	hio->errmsg.len += max;
#endif
	hio->errmsg.buf[hio->errmsg.len] = '\0';
	return 1; /* success */
}

void hio_seterrbfmt (hio_t* hio, hio_errnum_t errnum, const hio_bch_t* fmt, ...)
{
	va_list ap;
	hio_fmtout_t fo;

	if (hio->_shuterr) return;
	hio->errmsg.len = 0;

	HIO_MEMSET (&fo, 0, HIO_SIZEOF(fo));
	fo.putbchars = err_bcs;
	fo.putuchars = err_ucs;
	fo.ctx = hio;

	va_start (ap, fmt);
	hio_bfmt_outv (&fo, fmt, ap);
	va_end (ap);

	hio->errnum = errnum;
}

void hio_seterrufmt (hio_t* hio, hio_errnum_t errnum, const hio_uch_t* fmt, ...)
{
	va_list ap;
	hio_fmtout_t fo;

	if (hio->_shuterr) return;
	hio->errmsg.len = 0;

	HIO_MEMSET (&fo, 0, HIO_SIZEOF(fo));
	fo.putbchars = err_bcs;
	fo.putuchars = err_ucs;
	fo.ctx = hio;

	va_start (ap, fmt);
	hio_ufmt_outv (&fo, fmt, ap);
	va_end (ap);

	hio->errnum = errnum;
}


void hio_seterrbfmtv (hio_t* hio, hio_errnum_t errnum, const hio_bch_t* fmt, va_list ap)
{
	hio_fmtout_t fo;

	if (hio->_shuterr) return;

	hio->errmsg.len = 0;

	HIO_MEMSET (&fo, 0, HIO_SIZEOF(fo));
	fo.putbchars = err_bcs;
	fo.putuchars = err_ucs;
	fo.ctx = hio;

	hio_bfmt_outv (&fo, fmt, ap);
	hio->errnum = errnum;
}

void hio_seterrufmtv (hio_t* hio, hio_errnum_t errnum, const hio_uch_t* fmt, va_list ap)
{
	hio_fmtout_t fo;

	if (hio->_shuterr) return;

	hio->errmsg.len = 0;

	HIO_MEMSET (&fo, 0, HIO_SIZEOF(fo));
	fo.putbchars = err_bcs;
	fo.putuchars = err_ucs;
	fo.ctx = hio;

	hio_ufmt_outv (&fo, fmt, ap);
	hio->errnum = errnum;
}



void hio_seterrwithsyserr (hio_t* hio, int syserr_type, int syserr_code)
{
	hio_errnum_t errnum;

	if (hio->_shuterr) return;

	/*if (hio->vmprim.syserrstrb)
	{*/
		errnum = /*hio->vmprim.*/hio_sys_syserrstrb(hio, syserr_type, syserr_code, hio->errmsg.tmpbuf.bch, HIO_COUNTOF(hio->errmsg.tmpbuf.bch));
		hio_seterrbfmt (hio, errnum, "%hs", hio->errmsg.tmpbuf.bch);
	/*
	}
	else
	{
		HIO_ASSERT (hio, hio->vmprim.syserrstru != HIO_NULL);
		errnum = hio->vmprim.syserrstru(hio, syserr_type, syserr_code, hio->errmsg.tmpbuf.uch, HIO_COUNTOF(hio->errmsg.tmpbuf.uch));
		hio_seterrbfmt (hio, errnum, "%ls", hio->errmsg.tmpbuf.uch);
	}*/
}

void hio_seterrbfmtwithsyserr (hio_t* hio, int syserr_type, int syserr_code, const hio_bch_t* fmt, ...)
{
	hio_errnum_t errnum;
	hio_oow_t ucslen, bcslen;
	va_list ap;

	if (hio->_shuterr) return;
	
	/*
	if (hio->vmprim.syserrstrb)
	{*/
		errnum = hio_sys_syserrstrb(hio, syserr_type, syserr_code, hio->errmsg.tmpbuf.bch, HIO_COUNTOF(hio->errmsg.tmpbuf.bch));
		
		va_start (ap, fmt);
		hio_seterrbfmtv (hio, errnum, fmt, ap);
		va_end (ap);

		if (HIO_COUNTOF(hio->errmsg.buf) - hio->errmsg.len >= 5)
		{
			hio->errmsg.buf[hio->errmsg.len++] = ' ';
			hio->errmsg.buf[hio->errmsg.len++] = '-';
			hio->errmsg.buf[hio->errmsg.len++] = ' ';

		#if defined(HIO_OOCH_IS_BCH)
			hio->errmsg.len += hio_copy_bcstr(&hio->errmsg.buf[hio->errmsg.len], HIO_COUNTOF(hio->errmsg.buf) - hio->errmsg.len, hio->errmsg.tmpbuf.bch);
		#else
			ucslen = HIO_COUNTOF(hio->errmsg.buf) - hio->errmsg.len;
			hio_convbtoucstr (hio, hio->errmsg.tmpbuf.bch, &bcslen, &hio->errmsg.buf[hio->errmsg.len], &ucslen, 1);
			hio->errmsg.len += ucslen;
		#endif
		}
	/*}
	else
	{
		HIO_ASSERT (hio, hio->vmprim.syserrstru != HIO_NULL);
		errnum = hio_sys_syserrstru(hio, syserr_type, syserr_code, hio->errmsg.tmpbuf.uch, HIO_COUNTOF(hio->errmsg.tmpbuf.uch));

		va_start (ap, fmt);
		hio_seterrbfmtv (hio, errnum, fmt, ap);
		va_end (ap);

		if (HIO_COUNTOF(hio->errmsg.buf) - hio->errmsg.len >= 5)
		{
			hio->errmsg.buf[hio->errmsg.len++] = ' ';
			hio->errmsg.buf[hio->errmsg.len++] = '-';
			hio->errmsg.buf[hio->errmsg.len++] = ' ';

		#if defined(HIO_OOCH_IS_BCH)
			bcslen = HIO_COUNTOF(hio->errmsg.buf) - hio->errmsg.len;
			hio_convutobcstr (hio, hio->errmsg.tmpbuf.uch, &ucslen, &hio->errmsg.buf[hio->errmsg.len], &bcslen);
			hio->errmsg.len += bcslen;
		#else
			hio->errmsg.len += hio_copy_ucstr(&hio->errmsg.buf[hio->errmsg.len], HIO_COUNTOF(hio->errmsg.buf) - hio->errmsg.len, hio->errmsg.tmpbuf.uch);
		#endif
		}
	}*/
}

void hio_seterrufmtwithsyserr (hio_t* hio, int syserr_type, int syserr_code, const hio_uch_t* fmt, ...)
{
	hio_errnum_t errnum;
	hio_oow_t ucslen, bcslen;
	va_list ap;

	if (hio->_shuterr) return;
	
	/*if (hio->vmprim.syserrstrb)
	{*/
		errnum = hio_sys_syserrstrb(hio, syserr_type, syserr_code, hio->errmsg.tmpbuf.bch, HIO_COUNTOF(hio->errmsg.tmpbuf.bch));

		va_start (ap, fmt);
		hio_seterrufmtv (hio, errnum, fmt, ap);
		va_end (ap);

		if (HIO_COUNTOF(hio->errmsg.buf) - hio->errmsg.len >= 5)
		{
			hio->errmsg.buf[hio->errmsg.len++] = ' ';
			hio->errmsg.buf[hio->errmsg.len++] = '-';
			hio->errmsg.buf[hio->errmsg.len++] = ' ';

		#if defined(HIO_OOCH_IS_BCH)
			hio->errmsg.len += hio_copy_bcstr(&hio->errmsg.buf[hio->errmsg.len], HIO_COUNTOF(hio->errmsg.buf) - hio->errmsg.len, hio->errmsg.tmpbuf.bch);
		#else
			ucslen = HIO_COUNTOF(hio->errmsg.buf) - hio->errmsg.len;
			hio_convbtoucstr (hio, hio->errmsg.tmpbuf.bch, &bcslen, &hio->errmsg.buf[hio->errmsg.len], &ucslen, 1);
			hio->errmsg.len += ucslen;
		#endif
		}
	/*}
	else
	{
		HIO_ASSERT (hio, hio->vmprim.syserrstru != HIO_NULL);
		errnum = hio_sys_syserrstru(hio, syserr_type, syserr_code, hio->errmsg.tmpbuf.uch, HIO_COUNTOF(hio->errmsg.tmpbuf.uch));

		va_start (ap, fmt);
		hio_seterrufmtv (hio, errnum, fmt, ap);
		va_end (ap);

		if (HIO_COUNTOF(hio->errmsg.buf) - hio->errmsg.len >= 5)
		{
			hio->errmsg.buf[hio->errmsg.len++] = ' ';
			hio->errmsg.buf[hio->errmsg.len++] = '-';
			hio->errmsg.buf[hio->errmsg.len++] = ' ';

		#if defined(HIO_OOCH_IS_BCH)
			bcslen = HIO_COUNTOF(hio->errmsg.buf) - hio->errmsg.len;
			hio_convutobcstr (hio, hio->errmsg.tmpbuf.uch, &ucslen, &hio->errmsg.buf[hio->errmsg.len], &bcslen);
			hio->errmsg.len += bcslen;
		#else
			hio->errmsg.len += hio_copy_ucstr(&hio->errmsg.buf[hio->errmsg.len], HIO_COUNTOF(hio->errmsg.buf) - hio->errmsg.len, hio->errmsg.tmpbuf.uch);
		#endif
		}
	}*/
}

