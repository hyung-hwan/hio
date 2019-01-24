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

static mio_ooch_t errstr_0[] = {'n','o',' ','e','r','r','o','r','\0'};
static mio_ooch_t errstr_1[] = {'g','e','n','e','r','i','c',' ','e','r','r','o','r','\0'};
static mio_ooch_t errstr_2[] = {'n','o','t',' ','i','m','p','l','e','m','e','n','t','e','d','\0'};
static mio_ooch_t errstr_3[] = {'s','u','b','s','y','s','t','e','m',' ','e','r','r','o','r','\0'};
static mio_ooch_t errstr_4[] = {'i','n','t','e','r','n','a','l',' ','e','r','r','o','r',' ','t','h','a','t',' ','s','h','o','u','l','d',' ','n','e','v','e','r',' ','h','a','v','e',' ','h','a','p','p','e','n','e','d','\0'};
static mio_ooch_t errstr_5[] = {'i','n','s','u','f','f','i','c','i','e','n','t',' ','s','y','s','t','e','m',' ','m','e','m','o','r','y','\0'};
static mio_ooch_t errstr_6[] = {'i','n','s','u','f','f','i','c','i','e','n','t',' ','o','b','j','e','c','t',' ','m','e','m','o','r','y','\0'};
static mio_ooch_t errstr_7[] = {'i','n','v','a','l','i','d',' ','c','l','a','s','s','/','t','y','p','e','\0'};
static mio_ooch_t errstr_8[] = {'i','n','v','a','l','i','d',' ','p','a','r','a','m','e','t','e','r',' ','o','r',' ','a','r','g','u','m','e','n','t','\0'};
static mio_ooch_t errstr_9[] = {'d','a','t','a',' ','n','o','t',' ','f','o','u','n','d','\0'};
static mio_ooch_t errstr_10[] = {'e','x','i','s','t','i','n','g','/','d','u','p','l','i','c','a','t','e',' ','d','a','t','a','\0'};
static mio_ooch_t errstr_11[] = {'b','u','s','y','\0'};
static mio_ooch_t errstr_12[] = {'a','c','c','e','s','s',' ','d','e','n','i','e','d','\0'};
static mio_ooch_t errstr_13[] = {'o','p','e','r','a','t','i','o','n',' ','n','o','t',' ','p','e','r','m','i','t','t','e','d','\0'};
static mio_ooch_t errstr_14[] = {'n','o','t',' ','a',' ','d','i','r','e','c','t','o','r','y','\0'};
static mio_ooch_t errstr_15[] = {'i','n','t','e','r','r','u','p','t','e','d','\0'};
static mio_ooch_t errstr_16[] = {'p','i','p','e',' ','e','r','r','o','r','\0'};
static mio_ooch_t errstr_17[] = {'r','e','s','o','u','r','c','e',' ','t','e','m','p','o','r','a','r','i','l','y',' ','u','n','a','v','a','i','l','a','b','l','e','\0'};
static mio_ooch_t errstr_18[] = {'b','a','d',' ','s','y','s','t','e','m',' ','h','a','n','d','l','e','\0'};
static mio_ooch_t errstr_19[] = {'*','*','*',' ','u','n','d','e','f','i','n','e','d',' ','e','r','r','o','r',' ','*','*','*','\0'};
static mio_ooch_t errstr_20[] = {'m','e','s','s','a','g','e',' ','r','e','c','e','i','v','e','r',' ','e','r','r','o','r','\0'};
static mio_ooch_t errstr_21[] = {'m','e','s','s','a','g','e',' ','s','e','n','d','i','n','g',' ','e','r','r','o','r','\0'};
static mio_ooch_t errstr_22[] = {'w','r','o','n','g',' ','n','u','m','b','e','r',' ','o','f',' ','a','r','g','u','m','e','n','t','s','\0'};
static mio_ooch_t errstr_23[] = {'r','a','n','g','e',' ','e','r','r','o','r','\0'};
static mio_ooch_t errstr_24[] = {'b','y','t','e','-','c','o','d','e',' ','f','u','l','l','\0'};
static mio_ooch_t errstr_25[] = {'d','i','c','t','i','o','n','a','r','y',' ','f','u','l','l','\0'};
static mio_ooch_t errstr_26[] = {'p','r','o','c','e','s','s','o','r',' ','f','u','l','l','\0'};
static mio_ooch_t errstr_27[] = {'t','o','o',' ','m','a','n','y',' ','s','e','m','a','p','h','o','r','e','s','\0'};
static mio_ooch_t errstr_28[] = {'*','*','*',' ','u','n','d','e','f','i','n','e','d',' ','e','r','r','o','r',' ','*','*','*','\0'};
static mio_ooch_t errstr_29[] = {'d','i','v','i','d','e',' ','b','y',' ','z','e','r','o','\0'};
static mio_ooch_t errstr_30[] = {'I','/','O',' ','e','r','r','o','r','\0'};
static mio_ooch_t errstr_31[] = {'e','n','c','o','d','i','n','g',' ','c','o','n','v','e','r','s','i','o','n',' ','e','r','r','o','r','\0'};
static mio_ooch_t errstr_32[] = {'i','n','s','u','f','f','i','c','i','e','n','t',' ','d','a','t','a',' ','f','o','r',' ','e','n','c','o','d','i','n','g',' ','c','o','n','v','e','r','s','i','o','n','\0'};
static mio_ooch_t errstr_33[] = {'b','u','f','f','e','r',' ','f','u','l','l','\0'};
static mio_ooch_t* errstr[] =
{
	errstr_0, errstr_1, errstr_2, errstr_3, errstr_4, errstr_5, errstr_6, errstr_7,
	errstr_8, errstr_9, errstr_10, errstr_11, errstr_12, errstr_13, errstr_14, errstr_15,
	errstr_16, errstr_17, errstr_18, errstr_19, errstr_20, errstr_21, errstr_22, errstr_23,
	errstr_24, errstr_25, errstr_26, errstr_27, errstr_28, errstr_29, errstr_30, errstr_31,
	errstr_32, errstr_33 
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
			mio_convbtoucstr (mio, mio->errmsg.tmpbuf.bch, &bcslen, &mio->errmsg.buf[mio->errmsg.len], &ucslen);
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
			mio_convbtoucstr (mio, mio->errmsg.tmpbuf.bch, &bcslen, &mio->errmsg.buf[mio->errmsg.len], &ucslen);
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
