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

#ifndef _HIO_PRV_H_
#define _HIO_PRV_H_

#include <hio.h>
#include <hio-utl.h>

/* enable floating-point support in basic formatting functions */
#define HIO_ENABLE_FLTFMT

#if defined(__has_builtin)

#	if (!__has_builtin(__builtin_memset) || !__has_builtin(__builtin_memcpy) || !__has_builtin(__builtin_memmove) || !__has_builtin(__builtin_memcmp))
#	include <string.h>
#	endif

#	if __has_builtin(__builtin_memset)
#		define HIO_MEMSET(dst,src,size)  __builtin_memset(dst,src,size)
#	else
#		define HIO_MEMSET(dst,src,size)  memset(dst,src,size)
#	endif
#	if __has_builtin(__builtin_memcpy)
#		define HIO_MEMCPY(dst,src,size)  __builtin_memcpy(dst,src,size)
#	else
#		define HIO_MEMCPY(dst,src,size)  memcpy(dst,src,size)
#	endif
#	if __has_builtin(__builtin_memmove)
#		define HIO_MEMMOVE(dst,src,size)  __builtin_memmove(dst,src,size)
#	else
#		define HIO_MEMMOVE(dst,src,size)  memmove(dst,src,size)
#	endif
#	if __has_builtin(__builtin_memcmp)
#		define HIO_MEMCMP(dst,src,size)  __builtin_memcmp(dst,src,size)
#	else
#		define HIO_MEMCMP(dst,src,size)  memcmp(dst,src,size)
#	endif

#else

#	if !defined(HAVE___BUILTIN_MEMSET) || !defined(HAVE___BUILTIN_MEMCPY) || !defined(HAVE___BUILTIN_MEMMOVE) || !defined(HAVE___BUILTIN_MEMCMP)
#	include <string.h>
#	endif

#	if defined(HAVE___BUILTIN_MEMSET)
#		define HIO_MEMSET(dst,src,size)  __builtin_memset(dst,src,size)
#	else
#		define HIO_MEMSET(dst,src,size)  memset(dst,src,size)
#	endif
#	if defined(HAVE___BUILTIN_MEMCPY)
#		define HIO_MEMCPY(dst,src,size)  __builtin_memcpy(dst,src,size)
#	else
#		define HIO_MEMCPY(dst,src,size)  memcpy(dst,src,size)
#	endif
#	if defined(HAVE___BUILTIN_MEMMOVE)
#		define HIO_MEMMOVE(dst,src,size)  __builtin_memmove(dst,src,size)
#	else
#		define HIO_MEMMOVE(dst,src,size)  memmove(dst,src,size)
#	endif
#	if defined(HAVE___BUILTIN_MEMCMP)
#		define HIO_MEMCMP(dst,src,size)  __builtin_memcmp(dst,src,size)
#	else
#		define HIO_MEMCMP(dst,src,size)  memcmp(dst,src,size)
#	endif

#endif

/* =========================================================================
 * MIO ASSERTION
 * ========================================================================= */
#if defined(HIO_BUILD_RELEASE)
#	define HIO_ASSERT(hio,expr) ((void)0)
#else
#	define HIO_ASSERT(hio,expr) ((void)((expr) || (hio_sys_assertfail(hio, #expr, __FILE__, __LINE__), 0)))
#endif


/* i don't want an error raised inside the callback to override
 * the existing error number and message. */
#define HIO_SYS_WRITE_LOG(hio,mask,ptr,len) do { \
		int __shuterr = (hio)->_shuterr; \
		(hio)->_shuterr = 1; \
		hio_sys_writelog (hio, mask, ptr, len); \
		(hio)->_shuterr = __shuterr; \
	} while(0)

#if defined(__cplusplus)
extern "C" {
#endif

int hio_makesyshndasync (
	hio_t*       hio,
	hio_syshnd_t hnd
);

int hio_makesyshndcloexec (
	hio_t*       hio,
	hio_syshnd_t hnd
);

void hio_cleartmrjobs (
	hio_t* hio
);

void hio_firetmrjobs (
	hio_t*             hio,
	const hio_ntime_t* tmbase,
	hio_oow_t*        firecnt
);


/**
 * The hio_gettmrtmout() function gets the remaining time until the first
 * scheduled job is to be triggered. It stores in \a tmout the difference between
 * the given time \a tm and the scheduled time and returns 1. If there is no
 * job scheduled, it returns 0.
 */
int hio_gettmrtmout (
	hio_t*             hio,
	const hio_ntime_t* tm,
	hio_ntime_t*       tmout
);

/* ========================================================================== */
/* system intefaces                                                           */
/* ========================================================================== */

int hio_sys_init (
	hio_t* hio
);

void hio_sys_fini (
	hio_t* hio
);

void hio_sys_assertfail (
	hio_t*           hio,
	const hio_bch_t* expr,
	const hio_bch_t* file,
	hio_oow_t        line
);

hio_errnum_t hio_sys_syserrstrb (
	hio_t*            hio,
	int               syserr_type,
	int               syserr_code,
	hio_bch_t*        buf,
	hio_oow_t         len
);

void hio_sys_resetlog (
	hio_t*            hio
);

void hio_sys_locklog (
	hio_t*            hio
);

void hio_sys_unlocklog (
	hio_t*            hio
);

void hio_sys_writelog (
	hio_t*            hio,
	hio_bitmask_t     mask,
	const hio_ooch_t* msg,
	hio_oow_t         len
);

void hio_sys_intrmux  (
	hio_t*            hio
);

int hio_sys_ctrlmux (
	hio_t*            hio,
	hio_sys_mux_cmd_t cmd,
	hio_dev_t*        dev,
	int               dev_cap
);

int hio_sys_waitmux (
	hio_t*              hio,
	const hio_ntime_t*  tmout,
	hio_sys_mux_evtcb_t event_handler
);

void hio_sys_gettime (
	hio_t*       hio,
	hio_ntime_t* now
);

void hio_sys_getrealtime (
	hio_t*       hio,
	hio_ntime_t* now
);

#if defined(__cplusplus)
}
#endif


#endif
