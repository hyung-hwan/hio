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

#ifndef _HIO_CMN_H_
#define _HIO_CMN_H_

/* WARNING: NEVER CHANGE/DELETE THE FOLLOWING HIO_HAVE_CFG_H DEFINITION. 
 *          IT IS USED FOR DEPLOYMENT BY MAKEFILE.AM */
/*#define HIO_HAVE_CFG_H*/

#if defined(HIO_HAVE_CFG_H)
#	include "hio-cfg.h"
#elif defined(_WIN32)
#	include "hio-msw.h"
#elif defined(__OS2__)
#	include "hio-os2.h"
#elif defined(__MSDOS__)
#	include "hio-dos.h"
#elif defined(macintosh)
#	include "hio-mac.h" /* class mac os */
#else
#	error UNSUPPORTED SYSTEM
#endif

/* =========================================================================
 * ARCHITECTURE/COMPILER TWEAKS
 * ========================================================================= */

#if defined(EMSCRIPTEN)
#	if defined(HIO_SIZEOF___INT128)
#		undef HIO_SIZEOF___INT128 
#		define HIO_SIZEOF___INT128 0
#	endif
#	if defined(HIO_SIZEOF_LONG) && defined(HIO_SIZEOF_INT) && (HIO_SIZEOF_LONG > HIO_SIZEOF_INT)
		/* autoconf doesn't seem to match actual emscripten */
#		undef HIO_SIZEOF_LONG
#		define HIO_SIZEOF_LONG HIO_SIZEOF_INT
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

/* hio_int8_t */
#if defined(HIO_SIZEOF_CHAR) && (HIO_SIZEOF_CHAR == 1)
#	define HIO_HAVE_UINT8_T
#	define HIO_HAVE_INT8_T
#	define HIO_SIZEOF_UINT8_T (HIO_SIZEOF_CHAR)
#	define HIO_SIZEOF_INT8_T (HIO_SIZEOF_CHAR)
	typedef unsigned char      hio_uint8_t;
	typedef signed char        hio_int8_t;
#elif defined(HIO_SIZEOF___INT8) && (HIO_SIZEOF___INT8 == 1)
#	define HIO_HAVE_UINT8_T
#	define HIO_HAVE_INT8_T
#	define HIO_SIZEOF_UINT8_T (HIO_SIZEOF___INT8)
#	define HIO_SIZEOF_INT8_T (HIO_SIZEOF___INT8)
	typedef unsigned __int8    hio_uint8_t;
	typedef signed __int8      hio_int8_t;
#elif defined(HIO_SIZEOF___INT8_T) && (HIO_SIZEOF___INT8_T == 1)
#	define HIO_HAVE_UINT8_T
#	define HIO_HAVE_INT8_T
#	define HIO_SIZEOF_UINT8_T (HIO_SIZEOF___INT8_T)
#	define HIO_SIZEOF_INT8_T (HIO_SIZEOF___INT8_T)
	typedef unsigned __int8_t  hio_uint8_t;
	typedef signed __int8_t    hio_int8_t;
#else
#	define HIO_HAVE_UINT8_T
#	define HIO_HAVE_INT8_T
#	define HIO_SIZEOF_UINT8_T (1)
#	define HIO_SIZEOF_INT8_T (1)
	typedef unsigned char      hio_uint8_t;
	typedef signed char        hio_int8_t;
#endif

/* hio_int16_t */
#if defined(HIO_SIZEOF_SHORT) && (HIO_SIZEOF_SHORT == 2)
#	define HIO_HAVE_UINT16_T
#	define HIO_HAVE_INT16_T
#	define HIO_SIZEOF_UINT16_T (HIO_SIZEOF_SHORT)
#	define HIO_SIZEOF_INT16_T (HIO_SIZEOF_SHORT)
	typedef unsigned short int  hio_uint16_t;
	typedef signed short int    hio_int16_t;
#elif defined(HIO_SIZEOF___INT16) && (HIO_SIZEOF___INT16 == 2)
#	define HIO_HAVE_UINT16_T
#	define HIO_HAVE_INT16_T
#	define HIO_SIZEOF_UINT16_T (HIO_SIZEOF___INT16)
#	define HIO_SIZEOF_INT16_T (HIO_SIZEOF___INT16)
	typedef unsigned __int16    hio_uint16_t;
	typedef signed __int16      hio_int16_t;
#elif defined(HIO_SIZEOF___INT16_T) && (HIO_SIZEOF___INT16_T == 2)
#	define HIO_HAVE_UINT16_T
#	define HIO_HAVE_INT16_T
#	define HIO_SIZEOF_UINT16_T (HIO_SIZEOF___INT16_T)
#	define HIO_SIZEOF_INT16_T (HIO_SIZEOF___INT16_T)
	typedef unsigned __int16_t  hio_uint16_t;
	typedef signed __int16_t    hio_int16_t;
#else
#	define HIO_HAVE_UINT16_T
#	define HIO_HAVE_INT16_T
#	define HIO_SIZEOF_UINT16_T (2)
#	define HIO_SIZEOF_INT16_T (2)
	typedef unsigned short int  hio_uint16_t;
	typedef signed short int    hio_int16_t;
#endif


/* hio_int32_t */
#if defined(HIO_SIZEOF_INT) && (HIO_SIZEOF_INT == 4)
#	define HIO_HAVE_UINT32_T
#	define HIO_HAVE_INT32_T
#	define HIO_SIZEOF_UINT32_T (HIO_SIZEOF_INT)
#	define HIO_SIZEOF_INT32_T (HIO_SIZEOF_INT)
	typedef unsigned int        hio_uint32_t;
	typedef signed int          hio_int32_t;
#elif defined(HIO_SIZEOF_LONG) && (HIO_SIZEOF_LONG == 4)
#	define HIO_HAVE_UINT32_T
#	define HIO_HAVE_INT32_T
#	define HIO_SIZEOF_UINT32_T (HIO_SIZEOF_LONG)
#	define HIO_SIZEOF_INT32_T (HIO_SIZEOF_LONG)
	typedef unsigned long int   hio_uint32_t;
	typedef signed long int     hio_int32_t;
#elif defined(HIO_SIZEOF___INT32) && (HIO_SIZEOF___INT32 == 4)
#	define HIO_HAVE_UINT32_T
#	define HIO_HAVE_INT32_T
#	define HIO_SIZEOF_UINT32_T (HIO_SIZEOF___INT32)
#	define HIO_SIZEOF_INT32_T (HIO_SIZEOF___INT32)
	typedef unsigned __int32    hio_uint32_t;
	typedef signed __int32      hio_int32_t;
#elif defined(HIO_SIZEOF___INT32_T) && (HIO_SIZEOF___INT32_T == 4)
#	define HIO_HAVE_UINT32_T
#	define HIO_HAVE_INT32_T
#	define HIO_SIZEOF_UINT32_T (HIO_SIZEOF___INT32_T)
#	define HIO_SIZEOF_INT32_T (HIO_SIZEOF___INT32_T)
	typedef unsigned __int32_t  hio_uint32_t;
	typedef signed __int32_t    hio_int32_t;
#elif defined(__DOS__)
#	define HIO_HAVE_UINT32_T
#	define HIO_HAVE_INT32_T
#	define HIO_SIZEOF_UINT32_T (4)
#	define HIO_SIZEOF_INT32_T (4)
	typedef unsigned long int   hio_uint32_t;
	typedef signed long int     hio_int32_t;
#else
#	define HIO_HAVE_UINT32_T
#	define HIO_HAVE_INT32_T
#	define HIO_SIZEOF_UINT32_T (4)
#	define HIO_SIZEOF_INT32_T (4)
	typedef unsigned int        hio_uint32_t;
	typedef signed int          hio_int32_t;
#endif

/* hio_int64_t */
#if defined(HIO_SIZEOF_INT) && (HIO_SIZEOF_INT == 8)
#	define HIO_HAVE_UINT64_T
#	define HIO_HAVE_INT64_T
#	define HIO_SIZEOF_UINT64_T (HIO_SIZEOF_INT)
#	define HIO_SIZEOF_INT64_T (HIO_SIZEOF_INT)
	typedef unsigned int        hio_uint64_t;
	typedef signed int          hio_int64_t;
#elif defined(HIO_SIZEOF_LONG) && (HIO_SIZEOF_LONG == 8)
#	define HIO_HAVE_UINT64_T
#	define HIO_HAVE_INT64_T
#	define HIO_SIZEOF_UINT64_T (HIO_SIZEOF_LONG)
#	define HIO_SIZEOF_INT64_T (HIO_SIZEOF_LONG)
	typedef unsigned long int  hio_uint64_t;
	typedef signed long int    hio_int64_t;
#elif defined(HIO_SIZEOF_LONG_LONG) && (HIO_SIZEOF_LONG_LONG == 8)
#	define HIO_HAVE_UINT64_T
#	define HIO_HAVE_INT64_T
#	define HIO_SIZEOF_UINT64_T (HIO_SIZEOF_LONG_LONG)
#	define HIO_SIZEOF_INT64_T (HIO_SIZEOF_LONG_LONG)
	typedef unsigned long long int  hio_uint64_t;
	typedef signed long long int    hio_int64_t;
#elif defined(HIO_SIZEOF___INT64) && (HIO_SIZEOF___INT64 == 8)
#	define HIO_HAVE_UINT64_T
#	define HIO_HAVE_INT64_T
#	define HIO_SIZEOF_UINT64_T (HIO_SIZEOF_LONG___INT64)
#	define HIO_SIZEOF_INT64_T (HIO_SIZEOF_LONG___INT64)
	typedef unsigned __int64    hio_uint64_t;
	typedef signed __int64      hio_int64_t;
#elif defined(HIO_SIZEOF___INT64_T) && (HIO_SIZEOF___INT64_T == 8)
#	define HIO_HAVE_UINT64_T
#	define HIO_HAVE_INT64_T
#	define HIO_SIZEOF_UINT64_T (HIO_SIZEOF_LONG___INT64_T)
#	define HIO_SIZEOF_INT64_T (HIO_SIZEOF_LONG___INT64_T)
	typedef unsigned __int64_t  hio_uint64_t;
	typedef signed __int64_t    hio_int64_t;
#else
	/* no 64-bit integer */
#endif

/* hio_int128_t */
#if defined(HIO_SIZEOF_INT) && (HIO_SIZEOF_INT == 16)
#	define HIO_HAVE_UINT128_T
#	define HIO_HAVE_INT128_T
#	define HIO_SIZEOF_UINT128_T (HIO_SIZEOF_INT)
#	define HIO_SIZEOF_INT128_T (HIO_SIZEOF_INT)
	typedef unsigned int        hio_uint128_t;
	typedef signed int          hio_int128_t;
#elif defined(HIO_SIZEOF_LONG) && (HIO_SIZEOF_LONG == 16)
#	define HIO_HAVE_UINT128_T
#	define HIO_HAVE_INT128_T
#	define HIO_SIZEOF_UINT128_T (HIO_SIZEOF_LONG)
#	define HIO_SIZEOF_INT128_T (HIO_SIZEOF_LONG)
	typedef unsigned long int   hio_uint128_t;
	typedef signed long int     hio_int128_t;
#elif defined(HIO_SIZEOF_LONG_LONG) && (HIO_SIZEOF_LONG_LONG == 16)
#	define HIO_HAVE_UINT128_T
#	define HIO_HAVE_INT128_T
#	define HIO_SIZEOF_UINT128_T (HIO_SIZEOF_LONG_LONG)
#	define HIO_SIZEOF_INT128_T (HIO_SIZEOF_LONG_LONG)
	typedef unsigned long long int hio_uint128_t;
	typedef signed long long int   hio_int128_t;
#elif defined(HIO_SIZEOF___INT128) && (HIO_SIZEOF___INT128 == 16)
#	define HIO_HAVE_UINT128_T
#	define HIO_HAVE_INT128_T
#	define HIO_SIZEOF_UINT128_T (HIO_SIZEOF___INT128)
#	define HIO_SIZEOF_INT128_T (HIO_SIZEOF___INT128)
	typedef unsigned __int128    hio_uint128_t;
	typedef signed __int128      hio_int128_t;
#elif defined(HIO_SIZEOF___INT128_T) && (HIO_SIZEOF___INT128_T == 16)
#	define HIO_HAVE_UINT128_T
#	define HIO_HAVE_INT128_T
#	define HIO_SIZEOF_UINT128_T (HIO_SIZEOF___INT128_T)
#	define HIO_SIZEOF_INT128_T (HIO_SIZEOF___INT128_T)
	#if defined(HIO_SIZEOF___UINT128_T) && (HIO_SIZEOF___UINT128_T == HIO_SIZEOF___INT128_T)
	typedef __uint128_t  hio_uint128_t;
	typedef __int128_t   hio_int128_t;
	#elif defined(__clang__)
	typedef __uint128_t  hio_uint128_t;
	typedef __int128_t   hio_int128_t;
	#else
	typedef unsigned __int128_t  hio_uint128_t;
	typedef signed __int128_t    hio_int128_t;
	#endif
#else
	/* no 128-bit integer */
#endif


#if defined(HIO_HAVE_UINT8_T) && (HIO_SIZEOF_VOID_P == 1)
#	error UNSUPPORTED POINTER SIZE
#elif defined(HIO_HAVE_UINT16_T) && (HIO_SIZEOF_VOID_P == 2)
	typedef hio_uint16_t hio_uintptr_t;
	typedef hio_int16_t hio_intptr_t;
	typedef hio_uint8_t hio_ushortptr_t;
	typedef hio_int8_t hio_shortptr_t;
#elif defined(HIO_HAVE_UINT32_T) && (HIO_SIZEOF_VOID_P == 4)
	typedef hio_uint32_t hio_uintptr_t;
	typedef hio_int32_t hio_intptr_t;
	typedef hio_uint16_t hio_ushortptr_t;
	typedef hio_int16_t hio_shortptr_t;
#elif defined(HIO_HAVE_UINT64_T) && (HIO_SIZEOF_VOID_P == 8)
	typedef hio_uint64_t hio_uintptr_t;
	typedef hio_int64_t hio_intptr_t;
	typedef hio_uint32_t hio_ushortptr_t;
	typedef hio_int32_t hio_shortptr_t;
#elif defined(HIO_HAVE_UINT128_T) && (HIO_SIZEOF_VOID_P == 16) 
	typedef hio_uint128_t hio_uintptr_t;
	typedef hio_int128_t hio_intptr_t;
	typedef hio_uint64_t hio_ushortptr_t;
	typedef hio_int64_t hio_shortptr_t;
#else
#	error UNKNOWN POINTER SIZE
#endif

#define HIO_SIZEOF_INTPTR_T HIO_SIZEOF_VOID_P
#define HIO_SIZEOF_UINTPTR_T HIO_SIZEOF_VOID_P
#define HIO_SIZEOF_SHORTPTR_T (HIO_SIZEOF_VOID_P / 2)
#define HIO_SIZEOF_USHORTPTR_T (HIO_SIZEOF_VOID_P / 2)

#if defined(HIO_HAVE_INT128_T)
#	define HIO_SIZEOF_INTMAX_T 16
#	define HIO_SIZEOF_UINTMAX_T 16
	typedef hio_int128_t hio_intmax_t;
	typedef hio_uint128_t hio_uintmax_t;
#elif defined(HIO_HAVE_INT64_T)
#	define HIO_SIZEOF_INTMAX_T 8
#	define HIO_SIZEOF_UINTMAX_T 8
	typedef hio_int64_t hio_intmax_t;
	typedef hio_uint64_t hio_uintmax_t;
#elif defined(HIO_HAVE_INT32_T)
#	define HIO_SIZEOF_INTMAX_T 4
#	define HIO_SIZEOF_UINTMAX_T 4
	typedef hio_int32_t hio_intmax_t;
	typedef hio_uint32_t hio_uintmax_t;
#elif defined(HIO_HAVE_INT16_T)
#	define HIO_SIZEOF_INTMAX_T 2
#	define HIO_SIZEOF_UINTMAX_T 2
	typedef hio_int16_t hio_intmax_t;
	typedef hio_uint16_t hio_uintmax_t;
#elif defined(HIO_HAVE_INT8_T)
#	define HIO_SIZEOF_INTMAX_T 1
#	define HIO_SIZEOF_UINTMAX_T 1
	typedef hio_int8_t hio_intmax_t;
	typedef hio_uint8_t hio_uintmax_t;
#else
#	error UNKNOWN INTMAX SIZE
#endif

/* =========================================================================
 * FLOATING-POINT TYPE
 * ========================================================================= */
/** \typedef hio_fltbas_t
 * The hio_fltbas_t type defines the largest floating-pointer number type
 * naturally supported.
 */
#if defined(__FreeBSD__) || defined(__MINGW32__)
	/* TODO: check if the support for long double is complete.
	 *       if so, use long double for hio_flt_t */
	typedef double hio_fltbas_t;
#	define HIO_SIZEOF_FLTBAS_T HIO_SIZEOF_DOUBLE
#elif HIO_SIZEOF_LONG_DOUBLE > HIO_SIZEOF_DOUBLE
	typedef long double hio_fltbas_t;
#	define HIO_SIZEOF_FLTBAS_T HIO_SIZEOF_LONG_DOUBLE
#else
	typedef double hio_fltbas_t;
#	define HIO_SIZEOF_FLTBAS_T HIO_SIZEOF_DOUBLE
#endif

/** \typedef hio_fltmax_t
 * The hio_fltmax_t type defines the largest floating-pointer number type
 * ever supported.
 */
#if HIO_SIZEOF___FLOAT128 >= HIO_SIZEOF_FLTBAS_T
	/* the size of long double may be equal to the size of __float128
	 * for alignment on some platforms */
	typedef __float128 hio_fltmax_t;
#	define HIO_SIZEOF_FLTMAX_T HIO_SIZEOF___FLOAT128
#	define HIO_FLTMAX_REQUIRE_QUADMATH 1
#else
	typedef hio_fltbas_t hio_fltmax_t;
#	define HIO_SIZEOF_FLTMAX_T HIO_SIZEOF_FLTBAS_T
#	undef HIO_FLTMAX_REQUIRE_QUADMATH
#endif

#if defined(HIO_USE_FLTMAX)
typedef hio_fltmax_t hio_flt_t;
#define HIO_SIZEOF_FLT_T HIO_SIZEOF_FLTMAX_T
#else
typedef hio_fltbas_t hio_flt_t;
#define HIO_SIZEOF_FLT_T HIO_SIZEOF_FLTBAS_T
#endif

/* =========================================================================
 * BASIC HARD-CODED DEFINES
 * ========================================================================= */
#define HIO_BITS_PER_BYTE (8)
/* the maximum number of bch charaters to represent a single uch character */
#define HIO_BCSIZE_MAX 6

/* =========================================================================
 * BASIC MIO TYPES
 * =========================================================================*/
typedef char                    hio_bch_t;
typedef int                     hio_bci_t;
typedef unsigned int            hio_bcu_t;
typedef unsigned char           hio_bchu_t; /* unsigned version of hio_bch_t for inner working */
#define HIO_SIZEOF_BCH_T HIO_SIZEOF_CHAR
#define HIO_SIZEOF_BCI_T HIO_SIZEOF_INT

#if defined(HIO_WIDE_CHAR_SIZE) && (HIO_WIDE_CHAR_SIZE >= 4)
#	if defined(__GNUC__) && defined(__CHAR32_TYPE__)
	typedef __CHAR32_TYPE__    hio_uch_t;
#	else
	typedef hio_uint32_t       hio_uch_t;
#	endif
	typedef hio_uint32_t       hio_uchu_t; /* same as hio_uch_t as it is already unsigned */
#	define HIO_SIZEOF_UCH_T 4

#elif defined(__GNUC__) && defined(__CHAR16_TYPE__)
	typedef __CHAR16_TYPE__    hio_uch_t; 
	typedef hio_uint16_t       hio_uchu_t; /* same as hio_uch_t as it is already unsigned */
#	define HIO_SIZEOF_UCH_T 2
#else
	typedef hio_uint16_t       hio_uch_t;
	typedef hio_uint16_t       hio_uchu_t; /* same as hio_uch_t as it is already unsigned */
#	define HIO_SIZEOF_UCH_T 2
#endif

typedef hio_int32_t             hio_uci_t;
typedef hio_uint32_t            hio_ucu_t;
#define HIO_SIZEOF_UCI_T 4

typedef hio_uint8_t             hio_oob_t;

/* NOTE: sizeof(hio_oop_t) must be equal to sizeof(hio_oow_t) */
typedef hio_uintptr_t           hio_oow_t;
typedef hio_intptr_t            hio_ooi_t;
#define HIO_SIZEOF_OOW_T HIO_SIZEOF_UINTPTR_T
#define HIO_SIZEOF_OOI_T HIO_SIZEOF_INTPTR_T
#define HIO_OOW_BITS  (HIO_SIZEOF_OOW_T * HIO_BITS_PER_BYTE)
#define HIO_OOI_BITS  (HIO_SIZEOF_OOI_T * HIO_BITS_PER_BYTE)

typedef hio_ushortptr_t         hio_oohw_t; /* half word - half word */
typedef hio_shortptr_t          hio_oohi_t; /* signed half word */
#define HIO_SIZEOF_OOHW_T HIO_SIZEOF_USHORTPTR_T
#define HIO_SIZEOF_OOHI_T HIO_SIZEOF_SHORTPTR_T
#define HIO_OOHW_BITS  (HIO_SIZEOF_OOHW_T * HIO_BITS_PER_BYTE)
#define HIO_OOHI_BITS  (HIO_SIZEOF_OOHI_T * HIO_BITS_PER_BYTE)

struct hio_ucs_t
{
	hio_uch_t* ptr;
	hio_oow_t  len;
};
typedef struct hio_ucs_t hio_ucs_t;

struct hio_bcs_t
{
	hio_bch_t* ptr;
	hio_oow_t  len;
};
typedef struct hio_bcs_t hio_bcs_t;

#if defined(HIO_ENABLE_WIDE_CHAR)
	typedef hio_uch_t               hio_ooch_t;
	typedef hio_uchu_t              hio_oochu_t;
	typedef hio_uci_t               hio_ooci_t;
	typedef hio_ucu_t               hio_oocu_t;
	typedef hio_ucs_t               hio_oocs_t;
#	define HIO_OOCH_IS_UCH
#	define HIO_SIZEOF_OOCH_T HIO_SIZEOF_UCH_T
#else
	typedef hio_bch_t               hio_ooch_t;
	typedef hio_bchu_t              hio_oochu_t;
	typedef hio_bci_t               hio_ooci_t;
	typedef hio_bcu_t               hio_oocu_t;
	typedef hio_bcs_t               hio_oocs_t;
#	define HIO_OOCH_IS_BCH
#	define HIO_SIZEOF_OOCH_T HIO_SIZEOF_BCH_T
#endif

/* the maximum number of bch charaters to represent a single uch character */
#define HIO_BCSIZE_MAX 6

typedef unsigned int hio_bitmask_t;

typedef struct hio_ptl_t hio_ptl_t;
struct hio_ptl_t
{
	void*     ptr;
	hio_oow_t len;
};

/* =========================================================================
 * TIME-RELATED TYPES
 * =========================================================================*/
#define HIO_MSECS_PER_SEC  (1000)
#define HIO_MSECS_PER_MIN  (HIO_MSECS_PER_SEC * HIO_SECS_PER_MIN)
#define HIO_MSECS_PER_HOUR (HIO_MSECS_PER_SEC * HIO_SECS_PER_HOUR)
#define HIO_MSECS_PER_DAY  (HIO_MSECS_PER_SEC * HIO_SECS_PER_DAY)

#define HIO_USECS_PER_MSEC (1000)
#define HIO_NSECS_PER_USEC (1000)
#define HIO_NSECS_PER_MSEC (HIO_NSECS_PER_USEC * HIO_USECS_PER_MSEC)
#define HIO_USECS_PER_SEC  (HIO_USECS_PER_MSEC * HIO_MSECS_PER_SEC)
#define HIO_NSECS_PER_SEC  (HIO_NSECS_PER_USEC * HIO_USECS_PER_MSEC * HIO_MSECS_PER_SEC)

#define HIO_SECNSEC_TO_MSEC(sec,nsec) \
        (((hio_intptr_t)(sec) * HIO_MSECS_PER_SEC) + ((hio_intptr_t)(nsec) / HIO_NSECS_PER_MSEC))

#define HIO_SECNSEC_TO_USEC(sec,nsec) \
        (((hio_intptr_t)(sec) * HIO_USECS_PER_SEC) + ((hio_intptr_t)(nsec) / HIO_NSECS_PER_USEC))

#define HIO_SECNSEC_TO_NSEC(sec,nsec) \
        (((hio_intptr_t)(sec) * HIO_NSECS_PER_SEC) + (hio_intptr_t)(nsec))

#define HIO_SEC_TO_MSEC(sec) ((sec) * HIO_MSECS_PER_SEC)
#define HIO_MSEC_TO_SEC(sec) ((sec) / HIO_MSECS_PER_SEC)

#define HIO_USEC_TO_NSEC(usec) ((usec) * HIO_NSECS_PER_USEC)
#define HIO_NSEC_TO_USEC(nsec) ((nsec) / HIO_NSECS_PER_USEC)

#define HIO_MSEC_TO_NSEC(msec) ((msec) * HIO_NSECS_PER_MSEC)
#define HIO_NSEC_TO_MSEC(nsec) ((nsec) / HIO_NSECS_PER_MSEC)

#define HIO_MSEC_TO_USEC(msec) ((msec) * HIO_USECS_PER_MSEC)
#define HIO_USEC_TO_MSEC(usec) ((usec) / HIO_USECS_PER_MSEC)

#define HIO_SEC_TO_NSEC(sec) ((sec) * HIO_NSECS_PER_SEC)
#define HIO_NSEC_TO_SEC(nsec) ((nsec) / HIO_NSECS_PER_SEC)

#define HIO_SEC_TO_USEC(sec) ((sec) * HIO_USECS_PER_SEC)
#define HIO_USEC_TO_SEC(usec) ((usec) / HIO_USECS_PER_SEC)

#if defined(HIO_SIZEOF_INT64_T) && (HIO_SIZEOF_INT64_T > 0)
typedef hio_int64_t hio_ntime_sec_t;
#else
typedef hio_int32_t hio_ntime_sec_t;
#endif
typedef hio_int32_t hio_ntime_nsec_t;

typedef struct hio_ntime_t hio_ntime_t;
struct hio_ntime_t
{
	hio_ntime_sec_t  sec;
	hio_ntime_nsec_t   nsec; /* nanoseconds */
};

#define HIO_INIT_NTIME(c,s,ns) (((c)->sec = (s)), ((c)->nsec = (ns)))
#define HIO_CLEAR_NTIME(c) HIO_INIT_NTIME(c, 0, 0)

#define HIO_ADD_NTIME(c,a,b) \
	do { \
		(c)->sec = (a)->sec + (b)->sec; \
		(c)->nsec = (a)->nsec + (b)->nsec; \
		while ((c)->nsec >= HIO_NSECS_PER_SEC) { (c)->sec++; (c)->nsec -= HIO_NSECS_PER_SEC; } \
	} while(0)

#define HIO_ADD_NTIME_SNS(c,a,s,ns) \
	do { \
		(c)->sec = (a)->sec + (s); \
		(c)->nsec = (a)->nsec + (ns); \
		while ((c)->nsec >= HIO_NSECS_PER_SEC) { (c)->sec++; (c)->nsec -= HIO_NSECS_PER_SEC; } \
	} while(0)

#define HIO_SUB_NTIME(c,a,b) \
	do { \
		(c)->sec = (a)->sec - (b)->sec; \
		(c)->nsec = (a)->nsec - (b)->nsec; \
		while ((c)->nsec < 0) { (c)->sec--; (c)->nsec += HIO_NSECS_PER_SEC; } \
	} while(0)

#define HIO_SUB_NTIME_SNS(c,a,s,ns) \
	do { \
		(c)->sec = (a)->sec - s; \
		(c)->nsec = (a)->nsec - ns; \
		while ((c)->nsec < 0) { (c)->sec--; (c)->nsec += HIO_NSECS_PER_SEC; } \
	} while(0)


#define HIO_CMP_NTIME(a,b) (((a)->sec == (b)->sec)? ((a)->nsec - (b)->nsec): ((a)->sec - (b)->sec))

/* if time has been normalized properly, nsec must be equal to or
 * greater than 0. */
#define HIO_IS_NEG_NTIME(x) ((x)->sec < 0)
#define HIO_IS_POS_NTIME(x) ((x)->sec > 0 || ((x)->sec == 0 && (x)->nsec > 0))
#define HIO_IS_ZERO_NTIME(x) ((x)->sec == 0 && (x)->nsec == 0)


/* =========================================================================
 * PRIMITIVE MACROS
 * ========================================================================= */
#define HIO_UCI_EOF ((hio_uci_t)-1)
#define HIO_BCI_EOF ((hio_bci_t)-1)
#define HIO_OOCI_EOF ((hio_ooci_t)-1)

#define HIO_SIZEOF(x) (sizeof(x))
#define HIO_COUNTOF(x) (sizeof(x) / sizeof(x[0]))
#define HIO_BITSOF(x) (sizeof(x) * HIO_BITS_PER_BYTE)


#define HIO_EOL ('\n')

/**
 * The HIO_OFFSETOF() macro returns the offset of a field from the beginning
 * of a structure.
 */
#define HIO_OFFSETOF(type,member) ((hio_uintptr_t)&((type*)0)->member)

/**
 * The HIO_ALIGNOF() macro returns the alignment size of a structure.
 * Note that this macro may not work reliably depending on the type given.
 */
#define HIO_ALIGNOF(type) HIO_OFFSETOF(struct { hio_uint8_t d1; type d2; }, d2)
        /*(sizeof(struct { hio_uint8_t d1; type d2; }) - sizeof(type))*/

#if defined(__cplusplus)
#	if (__cplusplus >= 201103L) /* C++11 */
#		define HIO_NULL nullptr
#	else
#		define HIO_NULL (0)
#	endif
#else
#	define HIO_NULL ((void*)0)
#endif

/* make a bit mask that can mask off low n bits */
#define HIO_LBMASK(type,n) (~(~((type)0) << (n))) 
#define HIO_LBMASK_SAFE(type,n) (((n) < HIO_BITSOF(type))? HIO_LBMASK(type,n): ~(type)0)

/* make a bit mask that can mask off hig n bits */
#define HIO_HBMASK(type,n) (~(~((type)0) >> (n)))
#define HIO_HBMASK_SAFE(type,n) (((n) < HIO_BITSOF(type))? HIO_HBMASK(type,n): ~(type)0)

/* get 'length' bits starting from the bit at the 'offset' */
#define HIO_GETBITS(type,value,offset,length) \
	((((type)(value)) >> (offset)) & HIO_LBMASK(type,length))

#define HIO_CLEARBITS(type,value,offset,length) \
	(((type)(value)) & ~(HIO_LBMASK(type,length) << (offset)))

#define HIO_SETBITS(type,value,offset,length,bits) \
	(value = (HIO_CLEARBITS(type,value,offset,length) | (((bits) & HIO_LBMASK(type,length)) << (offset))))

#define HIO_FLIPBITS(type,value,offset,length) \
	(((type)(value)) ^ (HIO_LBMASK(type,length) << (offset)))

#define HIO_ORBITS(type,value,offset,length,bits) \
	(value = (((type)(value)) | (((bits) & HIO_LBMASK(type,length)) << (offset))))


/** 
 * The HIO_BITS_MAX() macros calculates the maximum value that the 'nbits'
 * bits of an unsigned integer of the given 'type' can hold.
 * \code
 * printf ("%u", HIO_BITS_MAX(unsigned int, 5));
 * \endcode
 */
/*#define HIO_BITS_MAX(type,nbits) ((((type)1) << (nbits)) - 1)*/
#define HIO_BITS_MAX(type,nbits) ((~(type)0) >> (HIO_BITSOF(type) - (nbits)))

/* =========================================================================
 * MMGR
 * ========================================================================= */
typedef struct hio_mmgr_t hio_mmgr_t;

/** 
 * allocate a memory chunk of the size \a n.
 * \return pointer to a memory chunk on success, #HIO_NULL on failure.
 */
typedef void* (*hio_mmgr_alloc_t)   (hio_mmgr_t* mmgr, hio_oow_t n);
/** 
 * resize a memory chunk pointed to by \a ptr to the size \a n.
 * \return pointer to a memory chunk on success, #HIO_NULL on failure.
 */
typedef void* (*hio_mmgr_realloc_t) (hio_mmgr_t* mmgr, void* ptr, hio_oow_t n);
/**
 * free a memory chunk pointed to by \a ptr.
 */
typedef void  (*hio_mmgr_free_t)    (hio_mmgr_t* mmgr, void* ptr);

/**
 * The hio_mmgr_t type defines the memory management interface.
 * As the type is merely a structure, it is just used as a single container
 * for memory management functions with a pointer to user-defined data. 
 * The user-defined data pointer \a ctx is passed to each memory management 
 * function whenever it is called. You can allocate, reallocate, and free 
 * a memory chunk.
 *
 * For example, a hio_xxx_open() function accepts a pointer of the hio_mmgr_t
 * type and the xxx object uses it to manage dynamic data within the object. 
 */
struct hio_mmgr_t
{
	hio_mmgr_alloc_t   alloc;   /**< allocation function */
	hio_mmgr_realloc_t realloc; /**< resizing function */
	hio_mmgr_free_t    free;    /**< disposal function */
	void*               ctx;     /**< user-defined data pointer */
};

/**
 * The HIO_MMGR_ALLOC() macro allocates a memory block of the \a size bytes
 * using the \a mmgr memory manager.
 */
#define HIO_MMGR_ALLOC(mmgr,size) ((mmgr)->alloc(mmgr,size))

/**
 * The HIO_MMGR_REALLOC() macro resizes a memory block pointed to by \a ptr 
 * to the \a size bytes using the \a mmgr memory manager.
 */
#define HIO_MMGR_REALLOC(mmgr,ptr,size) ((mmgr)->realloc(mmgr,ptr,size))

/** 
 * The HIO_MMGR_FREE() macro deallocates the memory block pointed to by \a ptr.
 */
#define HIO_MMGR_FREE(mmgr,ptr) ((mmgr)->free(mmgr,ptr))

/* =========================================================================
 * CMGR
 * =========================================================================*/

typedef struct hio_cmgr_t hio_cmgr_t;

typedef hio_oow_t (*hio_cmgr_bctouc_t) (
	const hio_bch_t*   mb, 
	hio_oow_t         size,
	hio_uch_t*         wc
);

typedef hio_oow_t (*hio_cmgr_uctobc_t) (
	hio_uch_t    wc,
	hio_bch_t*   mb,
	hio_oow_t   size
);

/**
 * The hio_cmgr_t type defines the character-level interface to 
 * multibyte/wide-character conversion. This interface doesn't 
 * provide any facility to store conversion state in a context
 * independent manner. This leads to the limitation that it can
 * handle a stateless multibyte encoding only.
 */
struct hio_cmgr_t
{
	hio_cmgr_bctouc_t bctouc;
	hio_cmgr_uctobc_t uctobc;
};

/* =========================================================================
 * MACROS THAT CHANGES THE BEHAVIORS OF THE C COMPILER/LINKER
 * =========================================================================*/

#if defined(_WIN32) || (defined(__WATCOMC__) && !defined(__WINDOWS_386__))
#	define HIO_IMPORT __declspec(dllimport)
#	define HIO_EXPORT __declspec(dllexport)
#	define HIO_PRIVATE 
#elif defined(__GNUC__) && (__GNUC__>=4)
#	define HIO_IMPORT __attribute__((visibility("default")))
#	define HIO_EXPORT __attribute__((visibility("default")))
#	define HIO_PRIVATE __attribute__((visibility("hidden")))
/*#	define HIO_PRIVATE __attribute__((visibility("internal")))*/
#else
#	define HIO_IMPORT
#	define HIO_EXPORT
#	define HIO_PRIVATE
#endif

#if defined(__cplusplus) || (defined(__STDC_VERSION__) && (__STDC_VERSION__>=199901L))
	/* C++/C99 */
#	define HIO_INLINE inline
#	define HIO_HAVE_INLINE
#elif defined(__GNUC__) && defined(__GNUC_GNU_INLINE__)
	/* gcc disables inline when -std=c89 or -ansi is used. 
	 * so use __inline__ supported by gcc regardless of the options */
#	define HIO_INLINE /*extern*/ __inline__
#	define HIO_HAVE_INLINE
#else
#	define HIO_INLINE 
#	undef HIO_HAVE_INLINE
#endif

#if defined(__GNUC__) && (__GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ > 4))
#	define HIO_UNUSED __attribute__((__unused__))
#else
#	define HIO_UNUSED
#endif

/**
 * The HIO_TYPE_IS_SIGNED() macro determines if a type is signed.
 * \code
 * printf ("%d\n", (int)HIO_TYPE_IS_SIGNED(int));
 * printf ("%d\n", (int)HIO_TYPE_IS_SIGNED(unsigned int));
 * \endcode
 */
#define HIO_TYPE_IS_SIGNED(type) (((type)0) > ((type)-1))

/**
 * The HIO_TYPE_IS_SIGNED() macro determines if a type is unsigned.
 * \code
 * printf ("%d\n", HIO_TYPE_IS_UNSIGNED(int));
 * printf ("%d\n", HIO_TYPE_IS_UNSIGNED(unsigned int));
 * \endcode
 */
#define HIO_TYPE_IS_UNSIGNED(type) (((type)0) < ((type)-1))

#define HIO_TYPE_SIGNED_MAX(type) \
	((type)~((type)1 << ((type)HIO_BITSOF(type) - 1)))
#define HIO_TYPE_UNSIGNED_MAX(type) ((type)(~(type)0))

#define HIO_TYPE_SIGNED_MIN(type) \
	((type)((type)1 << ((type)HIO_BITSOF(type) - 1)))
#define HIO_TYPE_UNSIGNED_MIN(type) ((type)0)

#define HIO_TYPE_MAX(type) \
	((HIO_TYPE_IS_SIGNED(type)? HIO_TYPE_SIGNED_MAX(type): HIO_TYPE_UNSIGNED_MAX(type)))
#define HIO_TYPE_MIN(type) \
	((HIO_TYPE_IS_SIGNED(type)? HIO_TYPE_SIGNED_MIN(type): HIO_TYPE_UNSIGNED_MIN(type)))

/* round up a positive integer x to the nearst multiple of y */
#define HIO_ALIGN(x,y) ((((x) + (y) - 1) / (y)) * (y))

/* round up a positive integer x to the nearst multiple of y where
 * y must be a multiple of a power of 2*/
#define HIO_ALIGN_POW2(x,y) ((((x) + (y) - 1)) & ~((y) - 1))

#define HIO_IS_UNALIGNED_POW2(x,y) ((x) & ((y) - 1))
#define HIO_IS_ALIGNED_POW2(x,y) (!HIO_IS_UNALIGNED_POW2(x,y))

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
		#define HIO_HAVE_BUILTIN_CTZ
	#endif
	#if __has_builtin(__builtin_ctzl)
		#define HIO_HAVE_BUILTIN_CTZL
	#endif
	#if __has_builtin(__builtin_ctzll)
		#define HIO_HAVE_BUILTIN_CTZLL
	#endif

	#if __has_builtin(__builtin_uadd_overflow)
		#define HIO_HAVE_BUILTIN_UADD_OVERFLOW 
	#endif
	#if __has_builtin(__builtin_uaddl_overflow)
		#define HIO_HAVE_BUILTIN_UADDL_OVERFLOW 
	#endif
	#if __has_builtin(__builtin_uaddll_overflow)
		#define HIO_HAVE_BUILTIN_UADDLL_OVERFLOW 
	#endif
	#if __has_builtin(__builtin_umul_overflow)
		#define HIO_HAVE_BUILTIN_UMUL_OVERFLOW 
	#endif
	#if __has_builtin(__builtin_umull_overflow)
		#define HIO_HAVE_BUILTIN_UMULL_OVERFLOW 
	#endif
	#if __has_builtin(__builtin_umulll_overflow)
		#define HIO_HAVE_BUILTIN_UMULLL_OVERFLOW 
	#endif

	#if __has_builtin(__builtin_sadd_overflow)
		#define HIO_HAVE_BUILTIN_SADD_OVERFLOW 
	#endif
	#if __has_builtin(__builtin_saddl_overflow)
		#define HIO_HAVE_BUILTIN_SADDL_OVERFLOW 
	#endif
	#if __has_builtin(__builtin_saddll_overflow)
		#define HIO_HAVE_BUILTIN_SADDLL_OVERFLOW 
	#endif
	#if __has_builtin(__builtin_smul_overflow)
		#define HIO_HAVE_BUILTIN_SMUL_OVERFLOW 
	#endif
	#if __has_builtin(__builtin_smull_overflow)
		#define HIO_HAVE_BUILTIN_SMULL_OVERFLOW 
	#endif
	#if __has_builtin(__builtin_smulll_overflow)
		#define HIO_HAVE_BUILTIN_SMULLL_OVERFLOW 
	#endif

	#if __has_builtin(__builtin_expect)
		#define HIO_HAVE_BUILTIN_EXPECT
	#endif


	#if __has_builtin(__sync_lock_test_and_set)
		#define HIO_HAVE_SYNC_LOCK_TEST_AND_SET
	#endif
	#if __has_builtin(__sync_lock_release)
		#define HIO_HAVE_SYNC_LOCK_RELEASE
	#endif

	#if __has_builtin(__sync_synchronize)
		#define HIO_HAVE_SYNC_SYNCHRONIZE
	#endif
	#if __has_builtin(__sync_bool_compare_and_swap)
		#define HIO_HAVE_SYNC_BOOL_COMPARE_AND_SWAP
	#endif
	#if __has_builtin(__sync_val_compare_and_swap)
		#define HIO_HAVE_SYNC_VAL_COMPARE_AND_SWAP
	#endif

	#if __has_builtin(__builtin_bswap16)
		#define HIO_HAVE_BUILTIN_BSWAP16
	#endif
	#if __has_builtin(__builtin_bswap32)
		#define HIO_HAVE_BUILTIN_BSWAP32
	#endif
	#if __has_builtin(__builtin_bswap64)
		#define HIO_HAVE_BUILTIN_BSWAP64
	#endif
	#if __has_builtin(__builtin_bswap128)
		#define HIO_HAVE_BUILTIN_BSWAP128
	#endif

#elif defined(__GNUC__) && defined(__GNUC_MINOR__)

	#if (__GNUC__ >= 4) 
		#define HIO_HAVE_SYNC_LOCK_TEST_AND_SET
		#define HIO_HAVE_SYNC_LOCK_RELEASE

		#define HIO_HAVE_SYNC_SYNCHRONIZE
		#define HIO_HAVE_SYNC_BOOL_COMPARE_AND_SWAP
		#define HIO_HAVE_SYNC_VAL_COMPARE_AND_SWAP
	#endif

	#if (__GNUC__ >= 4) || (__GNUC__ == 3 && __GNUC_MINOR__ >= 4)
		#define HIO_HAVE_BUILTIN_CTZ
		#define HIO_HAVE_BUILTIN_CTZL
		#define HIO_HAVE_BUILTIN_CTZLL
		#define HIO_HAVE_BUILTIN_EXPECT
	#endif

	#if (__GNUC__ >= 5)
		#define HIO_HAVE_BUILTIN_UADD_OVERFLOW
		#define HIO_HAVE_BUILTIN_UADDL_OVERFLOW
		#define HIO_HAVE_BUILTIN_UADDLL_OVERFLOW
		#define HIO_HAVE_BUILTIN_UMUL_OVERFLOW
		#define HIO_HAVE_BUILTIN_UMULL_OVERFLOW
		#define HIO_HAVE_BUILTIN_UMULLL_OVERFLOW

		#define HIO_HAVE_BUILTIN_SADD_OVERFLOW
		#define HIO_HAVE_BUILTIN_SADDL_OVERFLOW
		#define HIO_HAVE_BUILTIN_SADDLL_OVERFLOW
		#define HIO_HAVE_BUILTIN_SMUL_OVERFLOW
		#define HIO_HAVE_BUILTIN_SMULL_OVERFLOW
		#define HIO_HAVE_BUILTIN_SMULLL_OVERFLOW
	#endif

	#if (__GNUC__ >= 5) || (__GNUC__ == 4 && __GNUC_MINOR__ >= 8)
		/* 4.8.0 or later */
		#define HIO_HAVE_BUILTIN_BSWAP16
	#endif
	#if (__GNUC__ >= 5) || (__GNUC__ == 4 && __GNUC_MINOR__ >= 3)
		/* 4.3.0 or later */
		#define HIO_HAVE_BUILTIN_BSWAP32
		#define HIO_HAVE_BUILTIN_BSWAP64
		/*#define HIO_HAVE_BUILTIN_BSWAP128*/
	#endif
#endif

#if defined(HIO_HAVE_BUILTIN_EXPECT)
#	define HIO_LIKELY(x) (__builtin_expect(!!(x),1))
#	define HIO_UNLIKELY(x) (__builtin_expect(!!(x),0))
#else
#	define HIO_LIKELY(x) (x)
#	define HIO_UNLIKELY(x) (x)
#endif


#if defined(__GNUC__)
#	define HIO_PACKED __attribute__((__packed__))
#else
#	define HIO_PACKED 
#endif

/* =========================================================================
 * STATIC ASSERTION
 * =========================================================================*/
#define HIO_STATIC_JOIN_INNER(x, y) x ## y
#define HIO_STATIC_JOIN(x, y) HIO_STATIC_JOIN_INNER(x, y)

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
#	define HIO_STATIC_ASSERT(expr)  _Static_assert (expr, "invalid assertion")
#elif defined(__cplusplus) && (__cplusplus >= 201103L)
#	define HIO_STATIC_ASSERT(expr) static_assert (expr, "invalid assertion")
#else
#	define HIO_STATIC_ASSERT(expr) typedef char HIO_STATIC_JOIN(HIO_STATIC_ASSERT_T_, __LINE__)[(expr)? 1: -1] HIO_UNUSED
#endif

#define HIO_STATIC_ASSERT_EXPR(expr) ((void)HIO_SIZEOF(char[(expr)? 1: -1]))


/* =========================================================================
 * FILE OFFSET TYPE
 * =========================================================================*/
/**
 * The #hio_foff_t type defines an integer that can represent a file offset.
 * Depending on your system, it's defined to one of #hio_int64_t, #hio_int32_t,
 * and #hio_int16_t.
 */
#if defined(HIO_HAVE_INT64_T) && (HIO_SIZEOF_OFF64_T==8)
	typedef hio_int64_t hio_foff_t;
#	define HIO_SIZEOF_FOFF_T HIO_SIZEOF_INT64_T
#elif defined(HIO_HAVE_INT64_T) && (HIO_SIZEOF_OFF_T==8)
	typedef hio_int64_t hio_foff_t;
#	define HIO_SIZEOF_FOFF_T HIO_SIZEOF_INT64_T
#elif defined(HIO_HAVE_INT32_T) && (HIO_SIZEOF_OFF_T==4)
	typedef hio_int32_t hio_foff_t;
#	define HIO_SIZEOF_FOFF_T HIO_SIZEOF_INT32_T
#elif defined(HIO_HAVE_INT16_T) && (HIO_SIZEOF_OFF_T==2)
	typedef hio_int16_t hio_foff_t;
#	define HIO_SIZEOF_FOFF_T HIO_SIZEOF_INT16_T
#elif defined(HIO_HAVE_INT8_T) && (HIO_SIZEOF_OFF_T==1)
	typedef hio_int8_t hio_foff_t;
#	define HIO_SIZEOF_FOFF_T HIO_SIZEOF_INT16_T
#else
	typedef hio_int32_t hio_foff_t; /* this line is for doxygen */
#	error Unsupported platform
#endif

#endif
