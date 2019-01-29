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

#ifndef _MIO_PRV_H_
#define _MIO_PRV_H_

#include "mio.h"
#include "mio-utl.h"
#include <stdarg.h>


/*TODO: redefine and remove these */
#include <string.h>
#include <stdio.h>
/*TODO: redefine these */
#define MIO_MEMSET(dst,byte,count) memset(dst,byte,count)
#define MIO_MEMCPY(dst,src,count) memcpy(dst,src,count)
#define MIO_MEMMOVE(dst,src,count) memmove(dst,src,count)
#define MIO_MEMCMP(dst,src,count) memcmp(dst,src,count)

/* =========================================================================
 * MIO ASSERTION
 * ========================================================================= */
#if defined(MIO_BUILD_RELEASE)
#	define MIO_ASSERT(mio,expr) ((void)0)
#else
#	define MIO_ASSERT(mio,expr) ((void)((expr) || (mio_sys_assertfail(mio, #expr, __FILE__, __LINE__), 0)))
#endif


/* i don't want an error raised inside the callback to override 
 * the existing error number and message. */
#define prim_write_log(mio,mask,ptr,len) do { \
		int shuterr = (mio)->shuterr; \
		(mio)->shuterr = 1; \
		mio_sys_writelog (mio, mask, ptr, len); \
		(mio)->shuterr = shuterr; \
	} while(0)

#ifdef __cplusplus
extern "C" {
#endif

int mio_makesyshndasync (
	mio_t*       mio,
	mio_syshnd_t hnd
);


mio_bch_t* mio_mbsdup (
	mio_t*             mio,
	const mio_bch_t* src
);

mio_oow_t mio_mbscpy (
	mio_bch_t*       buf,
	const mio_bch_t* str
);

int mio_mbsspltrn (
	mio_bch_t*       s,
	const mio_bch_t* delim,
	mio_bch_t        lquote,
	mio_bch_t        rquote, 
	mio_bch_t        escape,
	const mio_bch_t* trset
);

int mio_mbsspl (
	mio_bch_t*       s,
	const mio_bch_t* delim,
	mio_bch_t        lquote,
	mio_bch_t        rquote,
	mio_bch_t        escape
);

void mio_cleartmrjobs (
	mio_t* mio
);

void mio_firetmrjobs (
	mio_t*             mio,
	const mio_ntime_t* tmbase,
	mio_oow_t*        firecnt
);


int mio_gettmrtmout (
	mio_t*             mio,
	const mio_ntime_t* tmbase,
	mio_ntime_t*       tmout
);

/* ========================================================================= */
/* err.c                                                                     */
/* ========================================================================= */

void mio_seterrbfmtv (
	mio_t*           mio,
	mio_errnum_t     errnum,
	const mio_bch_t* fmt,
	va_list          ap
);

void mio_seterrufmtv (
	mio_t*           mio,
	mio_errnum_t     errnum,
	const mio_uch_t* fmt,
	va_list          ap
);

/* ========================================================================== */
/* system intefaces                                                           */
/* ========================================================================== */

int mio_sys_init (
	mio_t* mio
);

void mio_sys_fini (
	mio_t* mio
);

void mio_sys_assertfail (
	mio_t*           mio, 
	const mio_bch_t* expr,
	const mio_bch_t* file,
	mio_oow_t        line
);

mio_errnum_t mio_sys_syserrstrb (
	mio_t*            mio,
	int               syserr_type,
	int               syserr_code,
	mio_bch_t*        buf,
	mio_oow_t         len
);

void mio_sys_writelog (
	mio_t*            mio,
	mio_bitmask_t     mask,
	const mio_ooch_t* msg,
	mio_oow_t         len
);

int mio_sys_ctrlmux (
	mio_t*            mio,
	mio_sys_mux_cmd_t cmd,
	mio_dev_t*        dev,
	int               dev_capa
);

int mio_sys_waitmux (
	mio_t*              mio,
	const mio_ntime_t*  tmout,
	mio_sys_mux_evtcb_t event_handler
);

#ifdef __cplusplus
}
#endif


#endif
