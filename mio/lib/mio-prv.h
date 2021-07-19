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

#ifndef _MIO_PRV_H_
#define _MIO_PRV_H_

#include <mio.h>
#include <mio-utl.h>

/* enable floating-point support in basic formatting functions */
#define MIO_ENABLE_FLTFMT

#if defined(__has_builtin)

#	if (!__has_builtin(__builtin_memset) || !__has_builtin(__builtin_memcpy) || !__has_builtin(__builtin_memmove) || !__has_builtin(__builtin_memcmp))
#	include <string.h>
#	endif

#	if __has_builtin(__builtin_memset)
#		define MIO_MEMSET(dst,src,size)  __builtin_memset(dst,src,size)
#	else
#		define MIO_MEMSET(dst,src,size)  memset(dst,src,size)
#	endif
#	if __has_builtin(__builtin_memcpy)
#		define MIO_MEMCPY(dst,src,size)  __builtin_memcpy(dst,src,size)
#	else
#		define MIO_MEMCPY(dst,src,size)  memcpy(dst,src,size)
#	endif
#	if __has_builtin(__builtin_memmove)
#		define MIO_MEMMOVE(dst,src,size)  __builtin_memmove(dst,src,size)
#	else
#		define MIO_MEMMOVE(dst,src,size)  memmove(dst,src,size)
#	endif
#	if __has_builtin(__builtin_memcmp)
#		define MIO_MEMCMP(dst,src,size)  __builtin_memcmp(dst,src,size)
#	else
#		define MIO_MEMCMP(dst,src,size)  memcmp(dst,src,size)
#	endif

#else

#	if !defined(HAVE___BUILTIN_MEMSET) || !defined(HAVE___BUILTIN_MEMCPY) || !defined(HAVE___BUILTIN_MEMMOVE) || !defined(HAVE___BUILTIN_MEMCMP)
#	include <string.h>
#	endif

#	if defined(HAVE___BUILTIN_MEMSET)
#		define MIO_MEMSET(dst,src,size)  __builtin_memset(dst,src,size)
#	else
#		define MIO_MEMSET(dst,src,size)  memset(dst,src,size)
#	endif
#	if defined(HAVE___BUILTIN_MEMCPY)
#		define MIO_MEMCPY(dst,src,size)  __builtin_memcpy(dst,src,size)
#	else
#		define MIO_MEMCPY(dst,src,size)  memcpy(dst,src,size)
#	endif
#	if defined(HAVE___BUILTIN_MEMMOVE)
#		define MIO_MEMMOVE(dst,src,size)  __builtin_memmove(dst,src,size)
#	else
#		define MIO_MEMMOVE(dst,src,size)  memmove(dst,src,size)
#	endif
#	if defined(HAVE___BUILTIN_MEMCMP)
#		define MIO_MEMCMP(dst,src,size)  __builtin_memcmp(dst,src,size)
#	else
#		define MIO_MEMCMP(dst,src,size)  memcmp(dst,src,size)
#	endif

#endif

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
#define MIO_SYS_WRITE_LOG(mio,mask,ptr,len) do { \
		int __shuterr = (mio)->_shuterr; \
		(mio)->_shuterr = 1; \
		mio_sys_writelog (mio, mask, ptr, len); \
		(mio)->_shuterr = __shuterr; \
	} while(0)

#ifdef __cplusplus
extern "C" {
#endif

int mio_makesyshndasync (
	mio_t*       mio,
	mio_syshnd_t hnd
);

int mio_makesyshndcloexec (
	mio_t*       mio,
	mio_syshnd_t hnd
);

void mio_cleartmrjobs (
	mio_t* mio
);

void mio_firetmrjobs (
	mio_t*             mio,
	const mio_ntime_t* tmbase,
	mio_oow_t*        firecnt
);


/**
 * The mio_gettmrtmout() function gets the remaining time until the first 
 * scheduled job is to be triggered. It stores in \a tmout the difference between 
 * the given time \a tm and the scheduled time and returns 1. If there is no
 * job scheduled, it returns 0.
 */
int mio_gettmrtmout (
	mio_t*             mio,
	const mio_ntime_t* tm,
	mio_ntime_t*       tmout
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

void mio_sys_resetlog (
	mio_t*            mio
);

void mio_sys_writelog (
	mio_t*            mio,
	mio_bitmask_t     mask,
	const mio_ooch_t* msg,
	mio_oow_t         len
);

void mio_sys_intrmux  (
	mio_t*            mio
);

int mio_sys_ctrlmux (
	mio_t*            mio,
	mio_sys_mux_cmd_t cmd,
	mio_dev_t*        dev,
	int               dev_cap
);

int mio_sys_waitmux (
	mio_t*              mio,
	const mio_ntime_t*  tmout,
	mio_sys_mux_evtcb_t event_handler
);

void mio_sys_gettime (
	mio_t*       mio,
	mio_ntime_t* now
);

void mio_sys_getrealtime (
	mio_t*       mio,
	mio_ntime_t* now
);

#ifdef __cplusplus
}
#endif


#endif
