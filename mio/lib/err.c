/*
 * $Id$
 *
    Copyright (c) 2015-2016 Chung, Hyung-Hwan. All rights reserved.

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

#include "mio-prv.h"

static mio_ooch_t errstr_0[] = {'n', 'o', ' ', 'e', 'r', 'r', 'o', 'r', '\0' };
static mio_ooch_t errstr_1[] = {'g', 'e', 'n', 'e', 'r', 'i', 'c', ' ', 'e', 'r', 'r', 'o', 'r', '\0' };
static mio_ooch_t errstr_2[] = {'n', 'o', 't', ' ', 'i', 'm', 'p', 'l', 'e', 'm', 'e', 'n', 't', 'e', 'd', '\0' };
static mio_ooch_t errstr_3[] = {'s', 'y', 's', 't', 'e', 'm', ' ', 'e', 'r', 'r', 'o', 'r', '\0' };
static mio_ooch_t errstr_4[] = {'i', 'n', 't', 'e', 'r', 'n', 'a', 'l', ' ', 'e', 'r', 'r', 'o', 'r', '\0' };
static mio_ooch_t errstr_5[] = {'i', 'n', 's', 'u', 'f', 'f', 'i', 'c', 'i', 'e', 'n', 't', ' ', 's', 'y', 's', 't', 'e', 'm', ' ', 'm', 'e', 'm', 'o', 'r', 'y', '\0' };
static mio_ooch_t errstr_6[] = {'i', 'n', 's', 'u', 'f', 'f', 'i', 'c', 'i', 'e', 'n', 't', ' ', 'o', 'b', 'j', 'e', 'c', 't', ' ', 'm', 'e', 'm', 'o', 'r', 'y', '\0' };
static mio_ooch_t errstr_7[] = {'i', 'n', 'v', 'a', 'l', 'i', 'd', ' ', 'p', 'a', 'r', 'a', 'm', 'e', 't', 'e', 'r', ' ', 'o', 'r', ' ', 'd', 'a', 't', 'a', '\0' };
static mio_ooch_t errstr_8[] = {'d', 'a', 't', 'a', ' ', 'n', 'o', 't', ' ', 'f', 'o', 'u', 'n', 'd', '\0' };
static mio_ooch_t errstr_9[] = {'e', 'x', 'i', 's', 't', 'i', 'n', 'g', '/', 'd', 'u', 'p', 'l', 'i', 'c', 'a', 't', 'e', ' ', 'd', 'a', 't', 'a', '\0' };
static mio_ooch_t errstr_10[] = {'s', 'y', 's', 't', 'e', 'm', ' ', 'b', 'u', 's', 'y', '\0' };
static mio_ooch_t errstr_11[] = {'a', 'c', 'c', 'e', 's', 's', ' ', 'd', 'e', 'n', 'i', 'e', 'd', '\0' };
static mio_ooch_t errstr_12[] = {'o', 'p', 'e', 'r', 'a', 't', 'i', 'o', 'n', ' ', 'n', 'o', 't', ' ', 'p', 'e', 'r', 'm', 'i', 't', 't', 'e', 'd', '\0' };
static mio_ooch_t errstr_13[] = {'n', 'o', 't', ' ', 'd', 'i', 'r', 'e', 'c', 't', 'o', 'r', 'y', '\0' };
static mio_ooch_t errstr_14[] = {'i', 'n', 't', 'e', 'r', 'r', 'u', 'p', 't', 'e', 'd', '\0' };
static mio_ooch_t errstr_15[] = {'p', 'i', 'p', 'e', ' ', 'e', 'r', 'r', 'o', 'r', '\0' };
static mio_ooch_t errstr_16[] = {'r', 'e', 's', 'o', 'u', 'r', 'c', 'e', ' ', 't', 'e', 'm', 'p', 'o', 'r', 'a', 'r', 'i', 'l', 'y', ' ', 'u', 'n', 'a', 'v', 'a', 'i', 'l', 'a', 'b', 'l', 'e', '\0' };
static mio_ooch_t errstr_17[] = {'b', 'a', 'd', ' ', 's', 'y', 's', 't', 'e', 'm', ' ', 'h', 'a', 'n', 'd', 'l', 'e', '\0' };
static mio_ooch_t errstr_18[] = {'t', 'o', 'o', ' ', 'm', 'a', 'n', 'y', ' ', 'o', 'p', 'e', 'n', ' ', 'f', 'i', 'l', 'e', 's', '\0' };
static mio_ooch_t errstr_19[] = {'t', 'o', 'o', ' ', 'm', 'a', 'n', 'y', ' ', 'o', 'p', 'e', 'n', ' ', 'f', 'i', 'l', 'e', 's', '\0' };
static mio_ooch_t errstr_20[] = {'I', '/', 'O', ' ', 'e', 'r', 'r', 'o', 'r', '\0' };
static mio_ooch_t errstr_21[] = {'e', 'n', 'c', 'o', 'd', 'i', 'n', 'g', ' ', 'c', 'o', 'n', 'v', 'e', 'r', 's', 'i', 'o', 'n', ' ', 'e', 'r', 'r', 'o', 'r', '\0' };
static mio_ooch_t errstr_22[] = {'i', 'n', 's', 'u', 'f', 'f', 'i', 'c', 'i', 'e', 'n', 't', ' ', 'd', 'a', 't', 'a', ' ', 'f', 'o', 'r', ' ', 'e', 'n', 'c', 'o', 'd', 'i', 'n', 'g', ' ', 'c', 'o', 'n', 'v', 'e', 'r', 's', 'i', 'o', 'n', '\0' };
static mio_ooch_t errstr_23[] = {'b', 'u', 'f', 'f', 'e', 'r', ' ', 'f', 'u', 'l', 'l', '\0' };
static mio_ooch_t errstr_24[] = {'c', 'o', 'n', 'n', 'e', 'c', 't', 'i', 'o', 'n', ' ', 'r', 'e', 'f', 'u', 's', 'e', 'd', '\0' };
static mio_ooch_t errstr_25[] = {'c', 'o', 'n', 'n', 'e', 'c', 't', 'i', 'o', 'n', ' ', 'r', 'e', 's', 'e', 't', '\0' };
static mio_ooch_t errstr_26[] = {'n', 'o', ' ', 'c', 'a', 'p', 'a', 'b', 'i', 'l', 'i', 't', 'y', '\0' };
static mio_ooch_t errstr_27[] = {'t', 'i', 'm', 'e', 'd', ' ', 'o', 'u', 't', '\0' };
static mio_ooch_t errstr_28[] = {'u', 'n', 'a', 'b', 'l', 'e', ' ', 't', 'o', ' ', 'm', 'a', 'k', 'e', ' ', 'd', 'e', 'v', 'i', 'c', 'e', '\0' };
static mio_ooch_t errstr_29[] = {'d', 'e', 'v', 'i', 'c', 'e', ' ', 'e', 'r', 'r', 'o', 'r', '\0' };
static mio_ooch_t* errstr[] =
{
	errstr_0, errstr_1, errstr_2, errstr_3, errstr_4,
	errstr_5, errstr_6, errstr_7, errstr_8, errstr_9,
	errstr_10, errstr_11, errstr_12, errstr_13, errstr_14,
	errstr_15, errstr_16, errstr_17, errstr_18, errstr_19,
	errstr_20, errstr_21, errstr_22, errstr_23, errstr_24,
	errstr_25, errstr_26, errstr_27, errstr_28, errstr_29
};

/* -------------------------------------------------------------------------- 
 * ERROR NUMBER TO STRING CONVERSION
 * -------------------------------------------------------------------------- */
const mio_ooch_t* mio_errnum_to_errstr (mio_errnum_t errnum)
{
	static mio_ooch_t e_unknown[] = {'u','n','k','n','o','w','n',' ','e','r','r','o','r','\0'};
	return (errnum >= 0 && errnum < MIO_COUNTOF(errstr))? errstr[errnum]: e_unknown;
}

/* -------------------------------------------------------------------------- 
 * ERROR NUMBER/MESSAGE HANDLING
 * -------------------------------------------------------------------------- */
const mio_ooch_t* mio_geterrstr (mio_t* mio)
{
	return mio_errnum_to_errstr(mio->errnum);
}

const mio_ooch_t* mio_geterrmsg (mio_t* mio)
{
	if (mio->errmsg.len <= 0) return mio_errnum_to_errstr(mio->errnum);
	return mio->errmsg.buf;
}

void mio_geterrinf (mio_t* mio, mio_errinf_t* info)
{
	info->num = mio_geterrnum(mio);
	mio_copy_oocstr (info->msg, MIO_COUNTOF(info->msg), mio_geterrmsg(mio));
}

const mio_ooch_t* mio_backuperrmsg (mio_t* mio)
{
	mio_copy_oocstr (mio->errmsg.tmpbuf.ooch, MIO_COUNTOF(mio->errmsg.tmpbuf.ooch), mio_geterrmsg(mio));
	return mio->errmsg.tmpbuf.ooch;
}

void mio_seterrnum (mio_t* mio, mio_errnum_t errnum)
{
	if (mio->shuterr) return;
	mio->errnum = errnum; 
	mio->errmsg.len = 0; 
}

static int err_bcs (mio_fmtout_t* fmtout, const mio_bch_t* ptr, mio_oow_t len)
{
	mio_t* mio = (mio_t*)fmtout->ctx;
	mio_oow_t max;

	max = MIO_COUNTOF(mio->errmsg.buf) - mio->errmsg.len - 1;

#if defined(MIO_OOCH_IS_UCH)
	if (max <= 0) return 1;
	mio_conv_bchars_to_uchars_with_cmgr (ptr, &len, &mio->errmsg.buf[mio->errmsg.len], &max, mio_getcmgr(mio), 1);
	mio->errmsg.len += max;
#else
	if (len > max) len = max;
	if (len <= 0) return 1;
	MIO_MEMCPY (&mio->errmsg.buf[mio->errmsg.len], ptr, len * MIO_SIZEOF(*ptr));
	mio->errmsg.len += len;
#endif

	mio->errmsg.buf[mio->errmsg.len] = '\0';

	return 1; /* success */
}

static int err_ucs (mio_fmtout_t* fmtout, const mio_uch_t* ptr, mio_oow_t len)
{
	mio_t* mio = (mio_t*)fmtout->ctx;
	mio_oow_t max;

	max = MIO_COUNTOF(mio->errmsg.buf) - mio->errmsg.len - 1;

#if defined(MIO_OOCH_IS_UCH)
	if (len > max) len = max;
	if (len <= 0) return 1;
	MIO_MEMCPY (&mio->errmsg.buf[mio->errmsg.len], ptr, len * MIO_SIZEOF(*ptr));
	mio->errmsg.len += len;
#else
	if (max <= 0) return 1;
	mio_conv_uchars_to_bchars_with_cmgr (ptr, &len, &mio->errmsg.buf[mio->errmsg.len], &max, mio_getcmgr(mio));
	mio->errmsg.len += max;
#endif
	mio->errmsg.buf[mio->errmsg.len] = '\0';
	return 1; /* success */
}

void mio_seterrbfmt (mio_t* mio, mio_errnum_t errnum, const mio_bch_t* fmt, ...)
{
	va_list ap;
	mio_fmtout_t fo;

	if (mio->shuterr) return;
	mio->errmsg.len = 0;

	MIO_MEMSET (&fo, 0, MIO_SIZEOF(fo));
	fo.putbcs = err_bcs;
	fo.putucs = err_ucs;
	fo.ctx = mio;

	va_start (ap, fmt);
	mio_bfmt_outv (&fo, fmt, ap);
	va_end (ap);

	mio->errnum = errnum;
}

void mio_seterrufmt (mio_t* mio, mio_errnum_t errnum, const mio_uch_t* fmt, ...)
{
	va_list ap;
	mio_fmtout_t fo;

	if (mio->shuterr) return;
	mio->errmsg.len = 0;

	MIO_MEMSET (&fo, 0, MIO_SIZEOF(fo));
	fo.putbcs = err_bcs;
	fo.putucs = err_ucs;
	fo.ctx = mio;

	va_start (ap, fmt);
	mio_ufmt_outv (&fo, fmt, ap);
	va_end (ap);

	mio->errnum = errnum;
}


void mio_seterrbfmtv (mio_t* mio, mio_errnum_t errnum, const mio_bch_t* fmt, va_list ap)
{
	mio_fmtout_t fo;

	if (mio->shuterr) return;

	mio->errmsg.len = 0;

	MIO_MEMSET (&fo, 0, MIO_SIZEOF(fo));
	fo.putbcs = err_bcs;
	fo.putucs = err_ucs;
	fo.ctx = mio;

	mio_bfmt_outv (&fo, fmt, ap);
	mio->errnum = errnum;
}

void mio_seterrufmtv (mio_t* mio, mio_errnum_t errnum, const mio_uch_t* fmt, va_list ap)
{
	mio_fmtout_t fo;

	if (mio->shuterr) return;

	mio->errmsg.len = 0;

	MIO_MEMSET (&fo, 0, MIO_SIZEOF(fo));
	fo.putbcs = err_bcs;
	fo.putucs = err_ucs;
	fo.ctx = mio;

	mio_ufmt_outv (&fo, fmt, ap);
	mio->errnum = errnum;
}



void mio_seterrwithsyserr (mio_t* mio, int syserr_type, int syserr_code)
{
	mio_errnum_t errnum;

	if (mio->shuterr) return;

	/*if (mio->vmprim.syserrstrb)
	{*/
		errnum = /*mio->vmprim.*/mio_sys_syserrstrb(mio, syserr_type, syserr_code, mio->errmsg.tmpbuf.bch, MIO_COUNTOF(mio->errmsg.tmpbuf.bch));
		mio_seterrbfmt (mio, errnum, "%hs", mio->errmsg.tmpbuf.bch);
	/*
	}
	else
	{
		MIO_ASSERT (mio, mio->vmprim.syserrstru != MIO_NULL);
		errnum = mio->vmprim.syserrstru(mio, syserr_type, syserr_code, mio->errmsg.tmpbuf.uch, MIO_COUNTOF(mio->errmsg.tmpbuf.uch));
		mio_seterrbfmt (mio, errnum, "%ls", mio->errmsg.tmpbuf.uch);
	}*/
}

void mio_seterrbfmtwithsyserr (mio_t* mio, int syserr_type, int syserr_code, const mio_bch_t* fmt, ...)
{
	mio_errnum_t errnum;
	mio_oow_t ucslen, bcslen;
	va_list ap;

	if (mio->shuterr) return;
	
	/*
	if (mio->vmprim.syserrstrb)
	{*/
		errnum = mio_sys_syserrstrb(mio, syserr_type, syserr_code, mio->errmsg.tmpbuf.bch, MIO_COUNTOF(mio->errmsg.tmpbuf.bch));
		
		va_start (ap, fmt);
		mio_seterrbfmtv (mio, errnum, fmt, ap);
		va_end (ap);

		if (MIO_COUNTOF(mio->errmsg.buf) - mio->errmsg.len >= 5)
		{
			mio->errmsg.buf[mio->errmsg.len++] = ' ';
			mio->errmsg.buf[mio->errmsg.len++] = '-';
			mio->errmsg.buf[mio->errmsg.len++] = ' ';

		#if defined(MIO_OOCH_IS_BCH)
			mio->errmsg.len += mio_copy_bcstr(&mio->errmsg.buf[mio->errmsg.len], MIO_COUNTOF(mio->errmsg.buf) - mio->errmsg.len, mio->errmsg.tmpbuf.bch);
		#else
			ucslen = MIO_COUNTOF(mio->errmsg.buf) - mio->errmsg.len;
			mio_convbtoucstr (mio, mio->errmsg.tmpbuf.bch, &bcslen, &mio->errmsg.buf[mio->errmsg.len], &ucslen, 1);
			mio->errmsg.len += ucslen;
		#endif
		}
	/*}
	else
	{
		MIO_ASSERT (mio, mio->vmprim.syserrstru != MIO_NULL);
		errnum = mio_sys_syserrstru(mio, syserr_type, syserr_code, mio->errmsg.tmpbuf.uch, MIO_COUNTOF(mio->errmsg.tmpbuf.uch));

		va_start (ap, fmt);
		mio_seterrbfmtv (mio, errnum, fmt, ap);
		va_end (ap);

		if (MIO_COUNTOF(mio->errmsg.buf) - mio->errmsg.len >= 5)
		{
			mio->errmsg.buf[mio->errmsg.len++] = ' ';
			mio->errmsg.buf[mio->errmsg.len++] = '-';
			mio->errmsg.buf[mio->errmsg.len++] = ' ';

		#if defined(MIO_OOCH_IS_BCH)
			bcslen = MIO_COUNTOF(mio->errmsg.buf) - mio->errmsg.len;
			mio_convutobcstr (mio, mio->errmsg.tmpbuf.uch, &ucslen, &mio->errmsg.buf[mio->errmsg.len], &bcslen);
			mio->errmsg.len += bcslen;
		#else
			mio->errmsg.len += mio_copy_ucstr(&mio->errmsg.buf[mio->errmsg.len], MIO_COUNTOF(mio->errmsg.buf) - mio->errmsg.len, mio->errmsg.tmpbuf.uch);
		#endif
		}
	}*/
}

void mio_seterrufmtwithsyserr (mio_t* mio, int syserr_type, int syserr_code, const mio_uch_t* fmt, ...)
{
	mio_errnum_t errnum;
	mio_oow_t ucslen, bcslen;
	va_list ap;

	if (mio->shuterr) return;
	
	/*if (mio->vmprim.syserrstrb)
	{*/
		errnum = mio_sys_syserrstrb(mio, syserr_type, syserr_code, mio->errmsg.tmpbuf.bch, MIO_COUNTOF(mio->errmsg.tmpbuf.bch));

		va_start (ap, fmt);
		mio_seterrufmtv (mio, errnum, fmt, ap);
		va_end (ap);

		if (MIO_COUNTOF(mio->errmsg.buf) - mio->errmsg.len >= 5)
		{
			mio->errmsg.buf[mio->errmsg.len++] = ' ';
			mio->errmsg.buf[mio->errmsg.len++] = '-';
			mio->errmsg.buf[mio->errmsg.len++] = ' ';

		#if defined(MIO_OOCH_IS_BCH)
			mio->errmsg.len += mio_copy_bcstr(&mio->errmsg.buf[mio->errmsg.len], MIO_COUNTOF(mio->errmsg.buf) - mio->errmsg.len, mio->errmsg.tmpbuf.bch);
		#else
			ucslen = MIO_COUNTOF(mio->errmsg.buf) - mio->errmsg.len;
			mio_convbtoucstr (mio, mio->errmsg.tmpbuf.bch, &bcslen, &mio->errmsg.buf[mio->errmsg.len], &ucslen, 1);
			mio->errmsg.len += ucslen;
		#endif
		}
	/*}
	else
	{
		MIO_ASSERT (mio, mio->vmprim.syserrstru != MIO_NULL);
		errnum = mio_sys_syserrstru(mio, syserr_type, syserr_code, mio->errmsg.tmpbuf.uch, MIO_COUNTOF(mio->errmsg.tmpbuf.uch));

		va_start (ap, fmt);
		mio_seterrufmtv (mio, errnum, fmt, ap);
		va_end (ap);

		if (MIO_COUNTOF(mio->errmsg.buf) - mio->errmsg.len >= 5)
		{
			mio->errmsg.buf[mio->errmsg.len++] = ' ';
			mio->errmsg.buf[mio->errmsg.len++] = '-';
			mio->errmsg.buf[mio->errmsg.len++] = ' ';

		#if defined(MIO_OOCH_IS_BCH)
			bcslen = MIO_COUNTOF(mio->errmsg.buf) - mio->errmsg.len;
			mio_convutobcstr (mio, mio->errmsg.tmpbuf.uch, &ucslen, &mio->errmsg.buf[mio->errmsg.len], &bcslen);
			mio->errmsg.len += bcslen;
		#else
			mio->errmsg.len += mio_copy_ucstr(&mio->errmsg.buf[mio->errmsg.len], MIO_COUNTOF(mio->errmsg.buf) - mio->errmsg.len, mio->errmsg.tmpbuf.uch);
		#endif
		}
	}*/
}

