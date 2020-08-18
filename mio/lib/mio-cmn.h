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

#ifndef _MIO_CMN_H_
#define _MIO_CMN_H_

/* WARNING: NEVER CHANGE/DELETE THE FOLLOWING MIO_HAVE_CFG_H DEFINITION. 
 *          IT IS USED FOR DEPLOYMENT BY MAKEFILE.AM */
/*#define MIO_HAVE_CFG_H*/

#if defined(MIO_HAVE_CFG_H)
#	include "mio-cfg.h"
#elif defined(_WIN32)
#	include "mio-msw.h"
#elif defined(__OS2__)
#	include "mio-os2.h"
#elif defined(__MSDOS__)
#	include "mio-dos.h"
#elif defined(macintosh)
#	include "mio-mac.h" /* class mac os */
#else
#	error UNSUPPORTED SYSTEM
#endif

/* =========================================================================
 * ARCHITECTURE/COMPILER TWEAKS
 * ========================================================================= */

#if defined(EMSCRIPTEN)
#	if defined(MIO_SIZEOF___INT128)
#		undef MIO_SIZEOF___INT128 
#		define MIO_SIZEOF___INT128 0
#	endif
#	if defined(MIO_SIZEOF_LONG) && defined(MIO_SIZEOF_INT) && (MIO_SIZEOF_LONG > MIO_SIZEOF_INT)
		/* autoconf doesn't seem to match actual emscripten */
#		undef MIO_SIZEOF_LONG
#		define MIO_SIZEOF_LONG MIO_SIZEOF_INT
#	endif
#endif

#if defined(__GNUC__) && defined(__arm__)  && !defined(__ARM_ARCH)
#	if defined(__ARM_ARCH_8__)
#		define __ARM_ARCH 8
#	elif defined(__ARM_ARCH_7__)
#		define __ARM_ARCH 7
#	elif defined(__ARM_ARCH_6__)
#		define __ARM_ARCH 6
#	elif defined(__ARM_ARCH_5__)
#		define __ARM_ARCH 5
#	elif defined(__ARM_ARCH_4__)
#		define __ARM_ARCH 4
#	endif
#endif

/* =========================================================================
 * PRIMITIVE TYPE DEFINTIONS
 * ========================================================================= */

/* mio_int8_t */
#if defined(MIO_SIZEOF_CHAR) && (MIO_SIZEOF_CHAR == 1)
#	define MIO_HAVE_UINT8_T
#	define MIO_HAVE_INT8_T
#	define MIO_SIZEOF_UINT8_T (MIO_SIZEOF_CHAR)
#	define MIO_SIZEOF_INT8_T (MIO_SIZEOF_CHAR)
	typedef unsigned char      mio_uint8_t;
	typedef signed char        mio_int8_t;
#elif defined(MIO_SIZEOF___INT8) && (MIO_SIZEOF___INT8 == 1)
#	define MIO_HAVE_UINT8_T
#	define MIO_HAVE_INT8_T
#	define MIO_SIZEOF_UINT8_T (MIO_SIZEOF___INT8)
#	define MIO_SIZEOF_INT8_T (MIO_SIZEOF___INT8)
	typedef unsigned __int8    mio_uint8_t;
	typedef signed __int8      mio_int8_t;
#elif defined(MIO_SIZEOF___INT8_T) && (MIO_SIZEOF___INT8_T == 1)
#	define MIO_HAVE_UINT8_T
#	define MIO_HAVE_INT8_T
#	define MIO_SIZEOF_UINT8_T (MIO_SIZEOF___INT8_T)
#	define MIO_SIZEOF_INT8_T (MIO_SIZEOF___INT8_T)
	typedef unsigned __int8_t  mio_uint8_t;
	typedef signed __int8_t    mio_int8_t;
#else
#	define MIO_HAVE_UINT8_T
#	define MIO_HAVE_INT8_T
#	define MIO_SIZEOF_UINT8_T (1)
#	define MIO_SIZEOF_INT8_T (1)
	typedef unsigned char      mio_uint8_t;
	typedef signed char        mio_int8_t;
#endif

/* mio_int16_t */
#if defined(MIO_SIZEOF_SHORT) && (MIO_SIZEOF_SHORT == 2)
#	define MIO_HAVE_UINT16_T
#	define MIO_HAVE_INT16_T
#	define MIO_SIZEOF_UINT16_T (MIO_SIZEOF_SHORT)
#	define MIO_SIZEOF_INT16_T (MIO_SIZEOF_SHORT)
	typedef unsigned short int  mio_uint16_t;
	typedef signed short int    mio_int16_t;
#elif defined(MIO_SIZEOF___INT16) && (MIO_SIZEOF___INT16 == 2)
#	define MIO_HAVE_UINT16_T
#	define MIO_HAVE_INT16_T
#	define MIO_SIZEOF_UINT16_T (MIO_SIZEOF___INT16)
#	define MIO_SIZEOF_INT16_T (MIO_SIZEOF___INT16)
	typedef unsigned __int16    mio_uint16_t;
	typedef signed __int16      mio_int16_t;
#elif defined(MIO_SIZEOF___INT16_T) && (MIO_SIZEOF___INT16_T == 2)
#	define MIO_HAVE_UINT16_T
#	define MIO_HAVE_INT16_T
#	define MIO_SIZEOF_UINT16_T (MIO_SIZEOF___INT16_T)
#	define MIO_SIZEOF_INT16_T (MIO_SIZEOF___INT16_T)
	typedef unsigned __int16_t  mio_uint16_t;
	typedef signed __int16_t    mio_int16_t;
#else
#	define MIO_HAVE_UINT16_T
#	define MIO_HAVE_INT16_T
#	define MIO_SIZEOF_UINT16_T (2)
#	define MIO_SIZEOF_INT16_T (2)
	typedef unsigned short int  mio_uint16_t;
	typedef signed short int    mio_int16_t;
#endif


/* mio_int32_t */
#if defined(MIO_SIZEOF_INT) && (MIO_SIZEOF_INT == 4)
#	define MIO_HAVE_UINT32_T
#	define MIO_HAVE_INT32_T
#	define MIO_SIZEOF_UINT32_T (MIO_SIZEOF_INT)
#	define MIO_SIZEOF_INT32_T (MIO_SIZEOF_INT)
	typedef unsigned int        mio_uint32_t;
	typedef signed int          mio_int32_t;
#elif defined(MIO_SIZEOF_LONG) && (MIO_SIZEOF_LONG == 4)
#	define MIO_HAVE_UINT32_T
#	define MIO_HAVE_INT32_T
#	define MIO_SIZEOF_UINT32_T (MIO_SIZEOF_LONG)
#	define MIO_SIZEOF_INT32_T (MIO_SIZEOF_LONG)
	typedef unsigned long int   mio_uint32_t;
	typedef signed long int     mio_int32_t;
#elif defined(MIO_SIZEOF___INT32) && (MIO_SIZEOF___INT32 == 4)
#	define MIO_HAVE_UINT32_T
#	define MIO_HAVE_INT32_T
#	define MIO_SIZEOF_UINT32_T (MIO_SIZEOF___INT32)
#	define MIO_SIZEOF_INT32_T (MIO_SIZEOF___INT32)
	typedef unsigned __int32    mio_uint32_t;
	typedef signed __int32      mio_int32_t;
#elif defined(MIO_SIZEOF___INT32_T) && (MIO_SIZEOF___INT32_T == 4)
#	define MIO_HAVE_UINT32_T
#	define MIO_HAVE_INT32_T
#	define MIO_SIZEOF_UINT32_T (MIO_SIZEOF___INT32_T)
#	define MIO_SIZEOF_INT32_T (MIO_SIZEOF___INT32_T)
	typedef unsigned __int32_t  mio_uint32_t;
	typedef signed __int32_t    mio_int32_t;
#elif defined(__DOS__)
#	define MIO_HAVE_UINT32_T
#	define MIO_HAVE_INT32_T
#	define MIO_SIZEOF_UINT32_T (4)
#	define MIO_SIZEOF_INT32_T (4)
	typedef unsigned long int   mio_uint32_t;
	typedef signed long int     mio_int32_t;
#else
#	define MIO_HAVE_UINT32_T
#	define MIO_HAVE_INT32_T
#	define MIO_SIZEOF_UINT32_T (4)
#	define MIO_SIZEOF_INT32_T (4)
	typedef unsigned int        mio_uint32_t;
	typedef signed int          mio_int32_t;
#endif

/* mio_int64_t */
#if defined(MIO_SIZEOF_INT) && (MIO_SIZEOF_INT == 8)
#	define MIO_HAVE_UINT64_T
#	define MIO_HAVE_INT64_T
#	define MIO_SIZEOF_UINT64_T (MIO_SIZEOF_INT)
#	define MIO_SIZEOF_INT64_T (MIO_SIZEOF_INT)
	typedef unsigned int        mio_uint64_t;
	typedef signed int          mio_int64_t;
#elif defined(MIO_SIZEOF_LONG) && (MIO_SIZEOF_LONG == 8)
#	define MIO_HAVE_UINT64_T
#	define MIO_HAVE_INT64_T
#	define MIO_SIZEOF_UINT64_T (MIO_SIZEOF_LONG)
#	define MIO_SIZEOF_INT64_T (MIO_SIZEOF_LONG)
	typedef unsigned long int  mio_uint64_t;
	typedef signed long int    mio_int64_t;
#elif defined(MIO_SIZEOF_LONG_LONG) && (MIO_SIZEOF_LONG_LONG == 8)
#	define MIO_HAVE_UINT64_T
#	define MIO_HAVE_INT64_T
#	define MIO_SIZEOF_UINT64_T (MIO_SIZEOF_LONG_LONG)
#	define MIO_SIZEOF_INT64_T (MIO_SIZEOF_LONG_LONG)
	typedef unsigned long long int  mio_uint64_t;
	typedef signed long long int    mio_int64_t;
#elif defined(MIO_SIZEOF___INT64) && (MIO_SIZEOF___INT64 == 8)
#	define MIO_HAVE_UINT64_T
#	define MIO_HAVE_INT64_T
#	define MIO_SIZEOF_UINT64_T (MIO_SIZEOF_LONG___INT64)
#	define MIO_SIZEOF_INT64_T (MIO_SIZEOF_LONG___INT64)
	typedef unsigned __int64    mio_uint64_t;
	typedef signed __int64      mio_int64_t;
#elif defined(MIO_SIZEOF___INT64_T) && (MIO_SIZEOF___INT64_T == 8)
#	define MIO_HAVE_UINT64_T
#	define MIO_HAVE_INT64_T
#	define MIO_SIZEOF_UINT64_T (MIO_SIZEOF_LONG___INT64_T)
#	define MIO_SIZEOF_INT64_T (MIO_SIZEOF_LONG___INT64_T)
	typedef unsigned __int64_t  mio_uint64_t;
	typedef signed __int64_t    mio_int64_t;
#else
	/* no 64-bit integer */
#endif

/* mio_int128_t */
#if defined(MIO_SIZEOF_INT) && (MIO_SIZEOF_INT == 16)
#	define MIO_HAVE_UINT128_T
#	define MIO_HAVE_INT128_T
#	define MIO_SIZEOF_UINT128_T (MIO_SIZEOF_INT)
#	define MIO_SIZEOF_INT128_T (MIO_SIZEOF_INT)
	typedef unsigned int        mio_uint128_t;
	typedef signed int          mio_int128_t;
#elif defined(MIO_SIZEOF_LONG) && (MIO_SIZEOF_LONG == 16)
#	define MIO_HAVE_UINT128_T
#	define MIO_HAVE_INT128_T
#	define MIO_SIZEOF_UINT128_T (MIO_SIZEOF_LONG)
#	define MIO_SIZEOF_INT128_T (MIO_SIZEOF_LONG)
	typedef unsigned long int   mio_uint128_t;
	typedef signed long int     mio_int128_t;
#elif defined(MIO_SIZEOF_LONG_LONG) && (MIO_SIZEOF_LONG_LONG == 16)
#	define MIO_HAVE_UINT128_T
#	define MIO_HAVE_INT128_T
#	define MIO_SIZEOF_UINT128_T (MIO_SIZEOF_LONG_LONG)
#	define MIO_SIZEOF_INT128_T (MIO_SIZEOF_LONG_LONG)
	typedef unsigned long long int mio_uint128_t;
	typedef signed long long int   mio_int128_t;
#elif defined(MIO_SIZEOF___INT128) && (MIO_SIZEOF___INT128 == 16)
#	define MIO_HAVE_UINT128_T
#	define MIO_HAVE_INT128_T
#	define MIO_SIZEOF_UINT128_T (MIO_SIZEOF___INT128)
#	define MIO_SIZEOF_INT128_T (MIO_SIZEOF___INT128)
	typedef unsigned __int128    mio_uint128_t;
	typedef signed __int128      mio_int128_t;
#elif defined(MIO_SIZEOF___INT128_T) && (MIO_SIZEOF___INT128_T == 16)
#	define MIO_HAVE_UINT128_T
#	define MIO_HAVE_INT128_T
#	define MIO_SIZEOF_UINT128_T (MIO_SIZEOF___INT128_T)
#	define MIO_SIZEOF_INT128_T (MIO_SIZEOF___INT128_T)
	#if defined(MIO_SIZEOF___UINT128_T) && (MIO_SIZEOF___UINT128_T == MIO_SIZEOF___INT128_T)
	typedef __uint128_t  mio_uint128_t;
	typedef __int128_t   mio_int128_t;
	#elif defined(__clang__)
	typedef __uint128_t  mio_uint128_t;
	typedef __int128_t   mio_int128_t;
	#else
	typedef unsigned __int128_t  mio_uint128_t;
	typedef signed __int128_t    mio_int128_t;
	#endif
#else
	/* no 128-bit integer */
#endif


#if defined(MIO_HAVE_UINT8_T) && (MIO_SIZEOF_VOID_P == 1)
#	error UNSUPPORTED POINTER SIZE
#elif defined(MIO_HAVE_UINT16_T) && (MIO_SIZEOF_VOID_P == 2)
	typedef mio_uint16_t mio_uintptr_t;
	typedef mio_int16_t mio_intptr_t;
	typedef mio_uint8_t mio_ushortptr_t;
	typedef mio_int8_t mio_shortptr_t;
#elif defined(MIO_HAVE_UINT32_T) && (MIO_SIZEOF_VOID_P == 4)
	typedef mio_uint32_t mio_uintptr_t;
	typedef mio_int32_t mio_intptr_t;
	typedef mio_uint16_t mio_ushortptr_t;
	typedef mio_int16_t mio_shortptr_t;
#elif defined(MIO_HAVE_UINT64_T) && (MIO_SIZEOF_VOID_P == 8)
	typedef mio_uint64_t mio_uintptr_t;
	typedef mio_int64_t mio_intptr_t;
	typedef mio_uint32_t mio_ushortptr_t;
	typedef mio_int32_t mio_shortptr_t;
#elif defined(MIO_HAVE_UINT128_T) && (MIO_SIZEOF_VOID_P == 16) 
	typedef mio_uint128_t mio_uintptr_t;
	typedef mio_int128_t mio_intptr_t;
	typedef mio_uint64_t mio_ushortptr_t;
	typedef mio_int64_t mio_shortptr_t;
#else
#	error UNKNOWN POINTER SIZE
#endif

#define MIO_SIZEOF_INTPTR_T MIO_SIZEOF_VOID_P
#define MIO_SIZEOF_UINTPTR_T MIO_SIZEOF_VOID_P
#define MIO_SIZEOF_SHORTPTR_T (MIO_SIZEOF_VOID_P / 2)
#define MIO_SIZEOF_USHORTPTR_T (MIO_SIZEOF_VOID_P / 2)

#if defined(MIO_HAVE_INT128_T)
#	define MIO_SIZEOF_INTMAX_T 16
#	define MIO_SIZEOF_UINTMAX_T 16
	typedef mio_int128_t mio_intmax_t;
	typedef mio_uint128_t mio_uintmax_t;
#elif defined(MIO_HAVE_INT64_T)
#	define MIO_SIZEOF_INTMAX_T 8
#	define MIO_SIZEOF_UINTMAX_T 8
	typedef mio_int64_t mio_intmax_t;
	typedef mio_uint64_t mio_uintmax_t;
#elif defined(MIO_HAVE_INT32_T)
#	define MIO_SIZEOF_INTMAX_T 4
#	define MIO_SIZEOF_UINTMAX_T 4
	typedef mio_int32_t mio_intmax_t;
	typedef mio_uint32_t mio_uintmax_t;
#elif defined(MIO_HAVE_INT16_T)
#	define MIO_SIZEOF_INTMAX_T 2
#	define MIO_SIZEOF_UINTMAX_T 2
	typedef mio_int16_t mio_intmax_t;
	typedef mio_uint16_t mio_uintmax_t;
#elif defined(MIO_HAVE_INT8_T)
#	define MIO_SIZEOF_INTMAX_T 1
#	define MIO_SIZEOF_UINTMAX_T 1
	typedef mio_int8_t mio_intmax_t;
	typedef mio_uint8_t mio_uintmax_t;
#else
#	error UNKNOWN INTMAX SIZE
#endif

/* =========================================================================
 * BASIC HARD-CODED DEFINES
 * ========================================================================= */
#define MIO_BITS_PER_BYTE (8)
/* the maximum number of bch charaters to represent a single uch character */
#define MIO_BCSIZE_MAX 6

/* =========================================================================
 * BASIC MIO TYPES
 * =========================================================================*/
typedef char                    mio_bch_t;
typedef int                     mio_bci_t;
typedef unsigned int            mio_bcu_t;
typedef unsigned char           mio_bchu_t; /* unsigned version of mio_bch_t for inner working */
#define MIO_SIZEOF_BCH_T MIO_SIZEOF_CHAR
#define MIO_SIZEOF_BCI_T MIO_SIZEOF_INT

#if defined(MIO_WIDE_CHAR_SIZE) && (MIO_WIDE_CHAR_SIZE >= 4)
#	if defined(__GNUC__) && defined(__CHAR32_TYPE__)
	typedef __CHAR32_TYPE__    mio_uch_t;
#	else
	typedef mio_uint32_t       mio_uch_t;
#	endif
	typedef mio_uint32_t       mio_uchu_t; /* same as mio_uch_t as it is already unsigned */
#	define MIO_SIZEOF_UCH_T 4

#elif defined(__GNUC__) && defined(__CHAR16_TYPE__)
	typedef __CHAR16_TYPE__    mio_uch_t; 
	typedef mio_uint16_t       mio_uchu_t; /* same as mio_uch_t as it is already unsigned */
#	define MIO_SIZEOF_UCH_T 2
#else
	typedef mio_uint16_t       mio_uch_t;
	typedef mio_uint16_t       mio_uchu_t; /* same as mio_uch_t as it is already unsigned */
#	define MIO_SIZEOF_UCH_T 2
#endif

typedef mio_int32_t             mio_uci_t;
typedef mio_uint32_t            mio_ucu_t;
#define MIO_SIZEOF_UCI_T 4

typedef mio_uint8_t             mio_oob_t;

/* NOTE: sizeof(mio_oop_t) must be equal to sizeof(mio_oow_t) */
typedef mio_uintptr_t           mio_oow_t;
typedef mio_intptr_t            mio_ooi_t;
#define MIO_SIZEOF_OOW_T MIO_SIZEOF_UINTPTR_T
#define MIO_SIZEOF_OOI_T MIO_SIZEOF_INTPTR_T
#define MIO_OOW_BITS  (MIO_SIZEOF_OOW_T * MIO_BITS_PER_BYTE)
#define MIO_OOI_BITS  (MIO_SIZEOF_OOI_T * MIO_BITS_PER_BYTE)

typedef mio_ushortptr_t         mio_oohw_t; /* half word - half word */
typedef mio_shortptr_t          mio_oohi_t; /* signed half word */
#define MIO_SIZEOF_OOHW_T MIO_SIZEOF_USHORTPTR_T
#define MIO_SIZEOF_OOHI_T MIO_SIZEOF_SHORTPTR_T
#define MIO_OOHW_BITS  (MIO_SIZEOF_OOHW_T * MIO_BITS_PER_BYTE)
#define MIO_OOHI_BITS  (MIO_SIZEOF_OOHI_T * MIO_BITS_PER_BYTE)

struct mio_ucs_t
{
	mio_uch_t* ptr;
	mio_oow_t  len;
};
typedef struct mio_ucs_t mio_ucs_t;

struct mio_bcs_t
{
	mio_bch_t* ptr;
	mio_oow_t  len;
};
typedef struct mio_bcs_t mio_bcs_t;

#if defined(MIO_ENABLE_WIDE_CHAR)
	typedef mio_uch_t               mio_ooch_t;
	typedef mio_uchu_t              mio_oochu_t;
	typedef mio_uci_t               mio_ooci_t;
	typedef mio_ucu_t               mio_oocu_t;
	typedef mio_ucs_t               mio_oocs_t;
#	define MIO_OOCH_IS_UCH
#	define MIO_SIZEOF_OOCH_T MIO_SIZEOF_UCH_T
#else
	typedef mio_bch_t               mio_ooch_t;
	typedef mio_bchu_t              mio_oochu_t;
	typedef mio_bci_t               mio_ooci_t;
	typedef mio_bcu_t               mio_oocu_t;
	typedef mio_bcs_t               mio_oocs_t;
#	define MIO_OOCH_IS_BCH
#	define MIO_SIZEOF_OOCH_T MIO_SIZEOF_BCH_T
#endif

/* the maximum number of bch charaters to represent a single uch character */
#define MIO_BCSIZE_MAX 6

typedef unsigned int mio_bitmask_t;

typedef struct mio_ptl_t mio_ptl_t;
struct mio_ptl_t
{
	void*     ptr;
	mio_oow_t len;
};

/* =========================================================================
 * TIME-RELATED TYPES
 * =========================================================================*/
#define MIO_MSECS_PER_SEC  (1000)
#define MIO_MSECS_PER_MIN  (MIO_MSECS_PER_SEC * MIO_SECS_PER_MIN)
#define MIO_MSECS_PER_HOUR (MIO_MSECS_PER_SEC * MIO_SECS_PER_HOUR)
#define MIO_MSECS_PER_DAY  (MIO_MSECS_PER_SEC * MIO_SECS_PER_DAY)

#define MIO_USECS_PER_MSEC (1000)
#define MIO_NSECS_PER_USEC (1000)
#define MIO_NSECS_PER_MSEC (MIO_NSECS_PER_USEC * MIO_USECS_PER_MSEC)
#define MIO_USECS_PER_SEC  (MIO_USECS_PER_MSEC * MIO_MSECS_PER_SEC)
#define MIO_NSECS_PER_SEC  (MIO_NSECS_PER_USEC * MIO_USECS_PER_MSEC * MIO_MSECS_PER_SEC)

#define MIO_SECNSEC_TO_MSEC(sec,nsec) \
        (((mio_intptr_t)(sec) * MIO_MSECS_PER_SEC) + ((mio_intptr_t)(nsec) / MIO_NSECS_PER_MSEC))

#define MIO_SECNSEC_TO_USEC(sec,nsec) \
        (((mio_intptr_t)(sec) * MIO_USECS_PER_SEC) + ((mio_intptr_t)(nsec) / MIO_NSECS_PER_USEC))

#define MIO_SECNSEC_TO_NSEC(sec,nsec) \
        (((mio_intptr_t)(sec) * MIO_NSECS_PER_SEC) + (mio_intptr_t)(nsec))

#define MIO_SEC_TO_MSEC(sec) ((sec) * MIO_MSECS_PER_SEC)
#define MIO_MSEC_TO_SEC(sec) ((sec) / MIO_MSECS_PER_SEC)

#define MIO_USEC_TO_NSEC(usec) ((usec) * MIO_NSECS_PER_USEC)
#define MIO_NSEC_TO_USEC(nsec) ((nsec) / MIO_NSECS_PER_USEC)

#define MIO_MSEC_TO_NSEC(msec) ((msec) * MIO_NSECS_PER_MSEC)
#define MIO_NSEC_TO_MSEC(nsec) ((nsec) / MIO_NSECS_PER_MSEC)

#define MIO_MSEC_TO_USEC(msec) ((msec) * MIO_USECS_PER_MSEC)
#define MIO_USEC_TO_MSEC(usec) ((usec) / MIO_USECS_PER_MSEC)

#define MIO_SEC_TO_NSEC(sec) ((sec) * MIO_NSECS_PER_SEC)
#define MIO_NSEC_TO_SEC(nsec) ((nsec) / MIO_NSECS_PER_SEC)

#define MIO_SEC_TO_USEC(sec) ((sec) * MIO_USECS_PER_SEC)
#define MIO_USEC_TO_SEC(usec) ((usec) / MIO_USECS_PER_SEC)

typedef struct mio_ntime_t mio_ntime_t;
struct mio_ntime_t
{
	mio_intptr_t  sec;
	mio_int32_t   nsec; /* nanoseconds */
};

#define MIO_INIT_NTIME(c,s,ns) (((c)->sec = (s)), ((c)->nsec = (ns)))
#define MIO_CLEAR_NTIME(c) MIO_INIT_NTIME(c, 0, 0)

#define MIO_ADD_NTIME(c,a,b) \
	do { \
		(c)->sec = (a)->sec + (b)->sec; \
		(c)->nsec = (a)->nsec + (b)->nsec; \
		while ((c)->nsec >= MIO_NSECS_PER_SEC) { (c)->sec++; (c)->nsec -= MIO_NSECS_PER_SEC; } \
	} while(0)

#define MIO_ADD_NTIME_SNS(c,a,s,ns) \
	do { \
		(c)->sec = (a)->sec + (s); \
		(c)->nsec = (a)->nsec + (ns); \
		while ((c)->nsec >= MIO_NSECS_PER_SEC) { (c)->sec++; (c)->nsec -= MIO_NSECS_PER_SEC; } \
	} while(0)

#define MIO_SUB_NTIME(c,a,b) \
	do { \
		(c)->sec = (a)->sec - (b)->sec; \
		(c)->nsec = (a)->nsec - (b)->nsec; \
		while ((c)->nsec < 0) { (c)->sec--; (c)->nsec += MIO_NSECS_PER_SEC; } \
	} while(0)

#define MIO_SUB_NTIME_SNS(c,a,s,ns) \
	do { \
		(c)->sec = (a)->sec - s; \
		(c)->nsec = (a)->nsec - ns; \
		while ((c)->nsec < 0) { (c)->sec--; (c)->nsec += MIO_NSECS_PER_SEC; } \
	} while(0)


#define MIO_CMP_NTIME(a,b) (((a)->sec == (b)->sec)? ((a)->nsec - (b)->nsec): ((a)->sec - (b)->sec))

/* if time has been normalized properly, nsec must be equal to or
 * greater than 0. */
#define MIO_IS_NEG_NTIME(x) ((x)->sec < 0)
#define MIO_IS_POS_NTIME(x) ((x)->sec > 0 || ((x)->sec == 0 && (x)->nsec > 0))
#define MIO_IS_ZERO_NTIME(x) ((x)->sec == 0 && (x)->nsec == 0)


/* =========================================================================
 * PRIMITIVE MACROS
 * ========================================================================= */
#define MIO_UCI_EOF ((mio_uci_t)-1)
#define MIO_BCI_EOF ((mio_bci_t)-1)
#define MIO_OOCI_EOF ((mio_ooci_t)-1)

#define MIO_SIZEOF(x) (sizeof(x))
#define MIO_COUNTOF(x) (sizeof(x) / sizeof(x[0]))
#define MIO_BITSOF(x) (sizeof(x) * MIO_BITS_PER_BYTE)

/**
 * The MIO_OFFSETOF() macro returns the offset of a field from the beginning
 * of a structure.
 */
#define MIO_OFFSETOF(type,member) ((mio_uintptr_t)&((type*)0)->member)

/**
 * The MIO_ALIGNOF() macro returns the alignment size of a structure.
 * Note that this macro may not work reliably depending on the type given.
 */
#define MIO_ALIGNOF(type) MIO_OFFSETOF(struct { mio_uint8_t d1; type d2; }, d2)
        /*(sizeof(struct { mio_uint8_t d1; type d2; }) - sizeof(type))*/

#if defined(__cplusplus)
#	if (__cplusplus >= 201103L) /* C++11 */
#		define MIO_NULL nullptr
#	else
#		define MIO_NULL (0)
#	endif
#else
#	define MIO_NULL ((void*)0)
#endif

/* make a bit mask that can mask off low n bits */
#define MIO_LBMASK(type,n) (~(~((type)0) << (n))) 
#define MIO_LBMASK_SAFE(type,n) (((n) < MIO_BITSOF(type))? MIO_LBMASK(type,n): ~(type)0)

/* make a bit mask that can mask off hig n bits */
#define MIO_HBMASK(type,n) (~(~((type)0) >> (n)))
#define MIO_HBMASK_SAFE(type,n) (((n) < MIO_BITSOF(type))? MIO_HBMASK(type,n): ~(type)0)

/* get 'length' bits starting from the bit at the 'offset' */
#define MIO_GETBITS(type,value,offset,length) \
	((((type)(value)) >> (offset)) & MIO_LBMASK(type,length))

#define MIO_CLEARBITS(type,value,offset,length) \
	(((type)(value)) & ~(MIO_LBMASK(type,length) << (offset)))

#define MIO_SETBITS(type,value,offset,length,bits) \
	(value = (MIO_CLEARBITS(type,value,offset,length) | (((bits) & MIO_LBMASK(type,length)) << (offset))))

#define MIO_FLIPBITS(type,value,offset,length) \
	(((type)(value)) ^ (MIO_LBMASK(type,length) << (offset)))

#define MIO_ORBITS(type,value,offset,length,bits) \
	(value = (((type)(value)) | (((bits) & MIO_LBMASK(type,length)) << (offset))))


/** 
 * The MIO_BITS_MAX() macros calculates the maximum value that the 'nbits'
 * bits of an unsigned integer of the given 'type' can hold.
 * \code
 * printf ("%u", MIO_BITS_MAX(unsigned int, 5));
 * \endcode
 */
/*#define MIO_BITS_MAX(type,nbits) ((((type)1) << (nbits)) - 1)*/
#define MIO_BITS_MAX(type,nbits) ((~(type)0) >> (MIO_BITSOF(type) - (nbits)))

/* =========================================================================
 * MMGR
 * ========================================================================= */
typedef struct mio_mmgr_t mio_mmgr_t;

/** 
 * allocate a memory chunk of the size \a n.
 * \return pointer to a memory chunk on success, #MIO_NULL on failure.
 */
typedef void* (*mio_mmgr_alloc_t)   (mio_mmgr_t* mmgr, mio_oow_t n);
/** 
 * resize a memory chunk pointed to by \a ptr to the size \a n.
 * \return pointer to a memory chunk on success, #MIO_NULL on failure.
 */
typedef void* (*mio_mmgr_realloc_t) (mio_mmgr_t* mmgr, void* ptr, mio_oow_t n);
/**
 * free a memory chunk pointed to by \a ptr.
 */
typedef void  (*mio_mmgr_free_t)    (mio_mmgr_t* mmgr, void* ptr);

/**
 * The mio_mmgr_t type defines the memory management interface.
 * As the type is merely a structure, it is just used as a single container
 * for memory management functions with a pointer to user-defined data. 
 * The user-defined data pointer \a ctx is passed to each memory management 
 * function whenever it is called. You can allocate, reallocate, and free 
 * a memory chunk.
 *
 * For example, a mio_xxx_open() function accepts a pointer of the mio_mmgr_t
 * type and the xxx object uses it to manage dynamic data within the object. 
 */
struct mio_mmgr_t
{
	mio_mmgr_alloc_t   alloc;   /**< allocation function */
	mio_mmgr_realloc_t realloc; /**< resizing function */
	mio_mmgr_free_t    free;    /**< disposal function */
	void*               ctx;     /**< user-defined data pointer */
};

/**
 * The MIO_MMGR_ALLOC() macro allocates a memory block of the \a size bytes
 * using the \a mmgr memory manager.
 */
#define MIO_MMGR_ALLOC(mmgr,size) ((mmgr)->alloc(mmgr,size))

/**
 * The MIO_MMGR_REALLOC() macro resizes a memory block pointed to by \a ptr 
 * to the \a size bytes using the \a mmgr memory manager.
 */
#define MIO_MMGR_REALLOC(mmgr,ptr,size) ((mmgr)->realloc(mmgr,ptr,size))

/** 
 * The MIO_MMGR_FREE() macro deallocates the memory block pointed to by \a ptr.
 */
#define MIO_MMGR_FREE(mmgr,ptr) ((mmgr)->free(mmgr,ptr))

/* =========================================================================
 * CMGR
 * =========================================================================*/

typedef struct mio_cmgr_t mio_cmgr_t;

typedef mio_oow_t (*mio_cmgr_bctouc_t) (
	const mio_bch_t*   mb, 
	mio_oow_t         size,
	mio_uch_t*         wc
);

typedef mio_oow_t (*mio_cmgr_uctobc_t) (
	mio_uch_t    wc,
	mio_bch_t*   mb,
	mio_oow_t   size
);

/**
 * The mio_cmgr_t type defines the character-level interface to 
 * multibyte/wide-character conversion. This interface doesn't 
 * provide any facility to store conversion state in a context
 * independent manner. This leads to the limitation that it can
 * handle a stateless multibyte encoding only.
 */
struct mio_cmgr_t
{
	mio_cmgr_bctouc_t bctouc;
	mio_cmgr_uctobc_t uctobc;
};

/* =========================================================================
 * MACROS THAT CHANGES THE BEHAVIORS OF THE C COMPILER/LINKER
 * =========================================================================*/

#if defined(_WIN32) || (defined(__WATCOMC__) && !defined(__WINDOWS_386__))
#	define MIO_IMPORT __declspec(dllimport)
#	define MIO_EXPORT __declspec(dllexport)
#	define MIO_PRIVATE 
#elif defined(__GNUC__) && (__GNUC__>=4)
#	define MIO_IMPORT __attribute__((visibility("default")))
#	define MIO_EXPORT __attribute__((visibility("default")))
#	define MIO_PRIVATE __attribute__((visibility("hidden")))
/*#	define MIO_PRIVATE __attribute__((visibility("internal")))*/
#else
#	define MIO_IMPORT
#	define MIO_EXPORT
#	define MIO_PRIVATE
#endif

#if defined(__cplusplus) || (defined(__STDC_VERSION__) && (__STDC_VERSION__>=199901L))
	/* C++/C99 */
#	define MIO_INLINE inline
#	define MIO_HAVE_INLINE
#elif defined(__GNUC__) && defined(__GNUC_GNU_INLINE__)
	/* gcc disables inline when -std=c89 or -ansi is used. 
	 * so use __inline__ supported by gcc regardless of the options */
#	define MIO_INLINE /*extern*/ __inline__
#	define MIO_HAVE_INLINE
#else
#	define MIO_INLINE 
#	undef MIO_HAVE_INLINE
#endif

#if defined(__GNUC__) && (__GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ > 4))
#	define MIO_UNUSED __attribute__((__unused__))
#else
#	define MIO_UNUSED
#endif

/**
 * The MIO_TYPE_IS_SIGNED() macro determines if a type is signed.
 * \code
 * printf ("%d\n", (int)MIO_TYPE_IS_SIGNED(int));
 * printf ("%d\n", (int)MIO_TYPE_IS_SIGNED(unsigned int));
 * \endcode
 */
#define MIO_TYPE_IS_SIGNED(type) (((type)0) > ((type)-1))

/**
 * The MIO_TYPE_IS_SIGNED() macro determines if a type is unsigned.
 * \code
 * printf ("%d\n", MIO_TYPE_IS_UNSIGNED(int));
 * printf ("%d\n", MIO_TYPE_IS_UNSIGNED(unsigned int));
 * \endcode
 */
#define MIO_TYPE_IS_UNSIGNED(type) (((type)0) < ((type)-1))

#define MIO_TYPE_SIGNED_MAX(type) \
	((type)~((type)1 << ((type)MIO_BITSOF(type) - 1)))
#define MIO_TYPE_UNSIGNED_MAX(type) ((type)(~(type)0))

#define MIO_TYPE_SIGNED_MIN(type) \
	((type)((type)1 << ((type)MIO_BITSOF(type) - 1)))
#define MIO_TYPE_UNSIGNED_MIN(type) ((type)0)

#define MIO_TYPE_MAX(type) \
	((MIO_TYPE_IS_SIGNED(type)? MIO_TYPE_SIGNED_MAX(type): MIO_TYPE_UNSIGNED_MAX(type)))
#define MIO_TYPE_MIN(type) \
	((MIO_TYPE_IS_SIGNED(type)? MIO_TYPE_SIGNED_MIN(type): MIO_TYPE_UNSIGNED_MIN(type)))

/* round up a positive integer x to the nearst multiple of y */
#define MIO_ALIGN(x,y) ((((x) + (y) - 1) / (y)) * (y))

/* round up a positive integer x to the nearst multiple of y where
 * y must be a multiple of a power of 2*/
#define MIO_ALIGN_POW2(x,y) ((((x) + (y) - 1)) & ~((y) - 1))

#define MIO_IS_UNALIGNED_POW2(x,y) ((x) & ((y) - 1))
#define MIO_IS_ALIGNED_POW2(x,y) (!MIO_IS_UNALIGNED_POW2(x,y))

/* =========================================================================
 * COMPILER FEATURE TEST MACROS
 * =========================================================================*/
#if !defined(__has_builtin) && defined(_INTELC32_)
	/* intel c code builder 1.0 ended up with an error without this override */
	#define __has_builtin(x) 0
#endif

/*
#if !defined(__is_identifier)
	#define __is_identifier(x) 0
#endif

#if !defined(__has_attribute)
	#define __has_attribute(x) 0
#endif
*/


#if defined(__has_builtin) 
	#if __has_builtin(__builtin_ctz)
		#define MIO_HAVE_BUILTIN_CTZ
	#endif
	#if __has_builtin(__builtin_ctzl)
		#define MIO_HAVE_BUILTIN_CTZL
	#endif
	#if __has_builtin(__builtin_ctzll)
		#define MIO_HAVE_BUILTIN_CTZLL
	#endif

	#if __has_builtin(__builtin_uadd_overflow)
		#define MIO_HAVE_BUILTIN_UADD_OVERFLOW 
	#endif
	#if __has_builtin(__builtin_uaddl_overflow)
		#define MIO_HAVE_BUILTIN_UADDL_OVERFLOW 
	#endif
	#if __has_builtin(__builtin_uaddll_overflow)
		#define MIO_HAVE_BUILTIN_UADDLL_OVERFLOW 
	#endif
	#if __has_builtin(__builtin_umul_overflow)
		#define MIO_HAVE_BUILTIN_UMUL_OVERFLOW 
	#endif
	#if __has_builtin(__builtin_umull_overflow)
		#define MIO_HAVE_BUILTIN_UMULL_OVERFLOW 
	#endif
	#if __has_builtin(__builtin_umulll_overflow)
		#define MIO_HAVE_BUILTIN_UMULLL_OVERFLOW 
	#endif

	#if __has_builtin(__builtin_sadd_overflow)
		#define MIO_HAVE_BUILTIN_SADD_OVERFLOW 
	#endif
	#if __has_builtin(__builtin_saddl_overflow)
		#define MIO_HAVE_BUILTIN_SADDL_OVERFLOW 
	#endif
	#if __has_builtin(__builtin_saddll_overflow)
		#define MIO_HAVE_BUILTIN_SADDLL_OVERFLOW 
	#endif
	#if __has_builtin(__builtin_smul_overflow)
		#define MIO_HAVE_BUILTIN_SMUL_OVERFLOW 
	#endif
	#if __has_builtin(__builtin_smull_overflow)
		#define MIO_HAVE_BUILTIN_SMULL_OVERFLOW 
	#endif
	#if __has_builtin(__builtin_smulll_overflow)
		#define MIO_HAVE_BUILTIN_SMULLL_OVERFLOW 
	#endif

	#if __has_builtin(__builtin_expect)
		#define MIO_HAVE_BUILTIN_EXPECT
	#endif


	#if __has_builtin(__sync_lock_test_and_set)
		#define MIO_HAVE_SYNC_LOCK_TEST_AND_SET
	#endif
	#if __has_builtin(__sync_lock_release)
		#define MIO_HAVE_SYNC_LOCK_RELEASE
	#endif

	#if __has_builtin(__sync_synchronize)
		#define MIO_HAVE_SYNC_SYNCHRONIZE
	#endif
	#if __has_builtin(__sync_bool_compare_and_swap)
		#define MIO_HAVE_SYNC_BOOL_COMPARE_AND_SWAP
	#endif
	#if __has_builtin(__sync_val_compare_and_swap)
		#define MIO_HAVE_SYNC_VAL_COMPARE_AND_SWAP
	#endif

	#if __has_builtin(__builtin_bswap16)
		#define MIO_HAVE_BUILTIN_BSWAP16
	#endif
	#if __has_builtin(__builtin_bswap32)
		#define MIO_HAVE_BUILTIN_BSWAP32
	#endif
	#if __has_builtin(__builtin_bswap64)
		#define MIO_HAVE_BUILTIN_BSWAP64
	#endif
	#if __has_builtin(__builtin_bswap128)
		#define MIO_HAVE_BUILTIN_BSWAP128
	#endif

#elif defined(__GNUC__) && defined(__GNUC_MINOR__)

	#if (__GNUC__ >= 4) 
		#define MIO_HAVE_SYNC_LOCK_TEST_AND_SET
		#define MIO_HAVE_SYNC_LOCK_RELEASE

		#define MIO_HAVE_SYNC_SYNCHRONIZE
		#define MIO_HAVE_SYNC_BOOL_COMPARE_AND_SWAP
		#define MIO_HAVE_SYNC_VAL_COMPARE_AND_SWAP
	#endif

	#if (__GNUC__ >= 4) || (__GNUC__ == 3 && __GNUC_MINOR__ >= 4)
		#define MIO_HAVE_BUILTIN_CTZ
		#define MIO_HAVE_BUILTIN_CTZL
		#define MIO_HAVE_BUILTIN_CTZLL
		#define MIO_HAVE_BUILTIN_EXPECT
	#endif

	#if (__GNUC__ >= 5)
		#define MIO_HAVE_BUILTIN_UADD_OVERFLOW
		#define MIO_HAVE_BUILTIN_UADDL_OVERFLOW
		#define MIO_HAVE_BUILTIN_UADDLL_OVERFLOW
		#define MIO_HAVE_BUILTIN_UMUL_OVERFLOW
		#define MIO_HAVE_BUILTIN_UMULL_OVERFLOW
		#define MIO_HAVE_BUILTIN_UMULLL_OVERFLOW

		#define MIO_HAVE_BUILTIN_SADD_OVERFLOW
		#define MIO_HAVE_BUILTIN_SADDL_OVERFLOW
		#define MIO_HAVE_BUILTIN_SADDLL_OVERFLOW
		#define MIO_HAVE_BUILTIN_SMUL_OVERFLOW
		#define MIO_HAVE_BUILTIN_SMULL_OVERFLOW
		#define MIO_HAVE_BUILTIN_SMULLL_OVERFLOW
	#endif

	#if (__GNUC__ >= 5) || (__GNUC__ == 4 && __GNUC_MINOR__ >= 8)
		/* 4.8.0 or later */
		#define MIO_HAVE_BUILTIN_BSWAP16
	#endif
	#if (__GNUC__ >= 5) || (__GNUC__ == 4 && __GNUC_MINOR__ >= 3)
		/* 4.3.0 or later */
		#define MIO_HAVE_BUILTIN_BSWAP32
		#define MIO_HAVE_BUILTIN_BSWAP64
		/*#define MIO_HAVE_BUILTIN_BSWAP128*/
	#endif
#endif

#if defined(MIO_HAVE_BUILTIN_EXPECT)
#	define MIO_LIKELY(x) (__builtin_expect(!!(x),1))
#	define MIO_UNLIKELY(x) (__builtin_expect(!!(x),0))
#else
#	define MIO_LIKELY(x) (x)
#	define MIO_UNLIKELY(x) (x)
#endif


#if defined(__GNUC__)
#	define MIO_PACKED __attribute__((__packed__))
#else
#	define MIO_PACKED 
#endif

/* =========================================================================
 * STATIC ASSERTION
 * =========================================================================*/
#define MIO_STATIC_JOIN_INNER(x, y) x ## y
#define MIO_STATIC_JOIN(x, y) MIO_STATIC_JOIN_INNER(x, y)

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
#	define MIO_STATIC_ASSERT(expr)  _Static_assert (expr, "invalid assertion")
#elif defined(__cplusplus) && (__cplusplus >= 201103L)
#	define MIO_STATIC_ASSERT(expr) static_assert (expr, "invalid assertion")
#else
#	define MIO_STATIC_ASSERT(expr) typedef char MIO_STATIC_JOIN(MIO_STATIC_ASSERT_T_, __LINE__)[(expr)? 1: -1] MIO_UNUSED
#endif

#define MIO_STATIC_ASSERT_EXPR(expr) ((void)MIO_SIZEOF(char[(expr)? 1: -1]))


/* =========================================================================
 * FILE OFFSET TYPE
 * =========================================================================*/
/**
 * The #mio_foff_t type defines an integer that can represent a file offset.
 * Depending on your system, it's defined to one of #mio_int64_t, #mio_int32_t,
 * and #mio_int16_t.
 */
#if defined(MIO_HAVE_INT64_T) && (MIO_SIZEOF_OFF64_T==8)
	typedef mio_int64_t mio_foff_t;
#	define MIO_SIZEOF_FOFF_T MIO_SIZEOF_INT64_T
#elif defined(MIO_HAVE_INT64_T) && (MIO_SIZEOF_OFF_T==8)
	typedef mio_int64_t mio_foff_t;
#	define MIO_SIZEOF_FOFF_T MIO_SIZEOF_INT64_T
#elif defined(MIO_HAVE_INT32_T) && (MIO_SIZEOF_OFF_T==4)
	typedef mio_int32_t mio_foff_t;
#	define MIO_SIZEOF_FOFF_T MIO_SIZEOF_INT32_T
#elif defined(MIO_HAVE_INT16_T) && (MIO_SIZEOF_OFF_T==2)
	typedef mio_int16_t mio_foff_t;
#	define MIO_SIZEOF_FOFF_T MIO_SIZEOF_INT16_T
#elif defined(MIO_HAVE_INT8_T) && (MIO_SIZEOF_OFF_T==1)
	typedef mio_int8_t mio_foff_t;
#	define MIO_SIZEOF_FOFF_T MIO_SIZEOF_INT16_T
#else
	typedef mio_int32_t mio_foff_t; /* this line is for doxygen */
#	error Unsupported platform
#endif

#endif
