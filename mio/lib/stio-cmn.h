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

#ifndef _STIO_CMN_H_
#define _STIO_CMN_H_

/* WARNING: NEVER CHANGE/DELETE THE FOLLOWING STIO_HAVE_CFG_H DEFINITION. 
 *          IT IS USED FOR DEPLOYMENT BY MAKEFILE.AM */
/*#define STIO_HAVE_CFG_H*/

#if defined(STIO_HAVE_CFG_H)
#	include "stio-cfg.h"
#elif defined(_WIN32)
#	include "stio-msw.h"
#elif defined(__OS2__)
#	include "stio-os2.h"
#elif defined(__MSDOS__)
#	include "stio-dos.h"
#elif defined(macintosh)
#	include "stio-mac.h" /* class mac os */
#else
#	error UNSUPPORTED SYSTEM
#endif

#if defined(EMSCRIPTEN)
#	if defined(STIO_SIZEOF___INT128)
#		undef STIO_SIZEOF___INT128 
#		define STIO_SIZEOF___INT128 0
#	endif
#	if defined(STIO_SIZEOF_LONG) && defined(STIO_SIZEOF_INT) && (STIO_SIZEOF_LONG > STIO_SIZEOF_INT)
		/* autoconf doesn't seem to match actual emscripten */
#		undef STIO_SIZEOF_LONG
#		define STIO_SIZEOF_LONG STIO_SIZEOF_INT
#	endif
#endif


/* =========================================================================
 * PRIMITIVE TYPE DEFINTIONS
 * ========================================================================= */

/* stio_int8_t */
#if defined(STIO_SIZEOF_CHAR) && (STIO_SIZEOF_CHAR == 1)
#	define STIO_HAVE_UINT8_T
#	define STIO_HAVE_INT8_T
	typedef unsigned char      stio_uint8_t;
	typedef signed char        stio_int8_t;
#elif defined(STIO_SIZEOF___INT8) && (STIO_SIZEOF___INT8 == 1)
#	define STIO_HAVE_UINT8_T
#	define STIO_HAVE_INT8_T
	typedef unsigned __int8    stio_uint8_t;
	typedef signed __int8      stio_int8_t;
#elif defined(STIO_SIZEOF___INT8_T) && (STIO_SIZEOF___INT8_T == 1)
#	define STIO_HAVE_UINT8_T
#	define STIO_HAVE_INT8_T
	typedef unsigned __int8_t  stio_uint8_t;
	typedef signed __int8_t    stio_int8_t;
#else
#	define STIO_HAVE_UINT8_T
#	define STIO_HAVE_INT8_T
	typedef unsigned char      stio_uint8_t;
	typedef signed char        stio_int8_t;
#endif


/* stio_int16_t */
#if defined(STIO_SIZEOF_SHORT) && (STIO_SIZEOF_SHORT == 2)
#	define STIO_HAVE_UINT16_T
#	define STIO_HAVE_INT16_T
	typedef unsigned short int  stio_uint16_t;
	typedef signed short int    stio_int16_t;
#elif defined(STIO_SIZEOF___INT16) && (STIO_SIZEOF___INT16 == 2)
#	define STIO_HAVE_UINT16_T
#	define STIO_HAVE_INT16_T
	typedef unsigned __int16    stio_uint16_t;
	typedef signed __int16      stio_int16_t;
#elif defined(STIO_SIZEOF___INT16_T) && (STIO_SIZEOF___INT16_T == 2)
#	define STIO_HAVE_UINT16_T
#	define STIO_HAVE_INT16_T
	typedef unsigned __int16_t  stio_uint16_t;
	typedef signed __int16_t    stio_int16_t;
#else
#	define STIO_HAVE_UINT16_T
#	define STIO_HAVE_INT16_T
	typedef unsigned short int  stio_uint16_t;
	typedef signed short int    stio_int16_t;
#endif


/* stio_int32_t */
#if defined(STIO_SIZEOF_INT) && (STIO_SIZEOF_INT == 4)
#	define STIO_HAVE_UINT32_T
#	define STIO_HAVE_INT32_T
	typedef unsigned int        stio_uint32_t;
	typedef signed int          stio_int32_t;
#elif defined(STIO_SIZEOF_LONG) && (STIO_SIZEOF_LONG == 4)
#	define STIO_HAVE_UINT32_T
#	define STIO_HAVE_INT32_T
	typedef unsigned long       stio_uint32_t;
	typedef signed long         stio_int32_t;
#elif defined(STIO_SIZEOF___INT32) && (STIO_SIZEOF___INT32 == 4)
#	define STIO_HAVE_UINT32_T
#	define STIO_HAVE_INT32_T
	typedef unsigned __int32    stio_uint32_t;
	typedef signed __int32      stio_int32_t;
#elif defined(STIO_SIZEOF___INT32_T) && (STIO_SIZEOF___INT32_T == 4)
#	define STIO_HAVE_UINT32_T
#	define STIO_HAVE_INT32_T
	typedef unsigned __int32_t  stio_uint32_t;
	typedef signed __int32_t    stio_int32_t;
#elif defined(__MSDOS__)
#	define STIO_HAVE_UINT32_T
#	define STIO_HAVE_INT32_T
	typedef unsigned long int   stio_uint32_t;
	typedef signed long int     stio_int32_t;
#else
#	define STIO_HAVE_UINT32_T
#	define STIO_HAVE_INT32_T
	typedef unsigned int        stio_uint32_t;
	typedef signed int          stio_int32_t;
#endif

/* stio_int64_t */
#if defined(STIO_SIZEOF_INT) && (STIO_SIZEOF_INT == 8)
#	define STIO_HAVE_UINT64_T
#	define STIO_HAVE_INT64_T
	typedef unsigned int        stio_uint64_t;
	typedef signed int          stio_int64_t;
#elif defined(STIO_SIZEOF_LONG) && (STIO_SIZEOF_LONG == 8)
#	define STIO_HAVE_UINT64_T
#	define STIO_HAVE_INT64_T
	typedef unsigned long       stio_uint64_t;
	typedef signed long         stio_int64_t;
#elif defined(STIO_SIZEOF_LONG_LONG) && (STIO_SIZEOF_LONG_LONG == 8)
#	define STIO_HAVE_UINT64_T
#	define STIO_HAVE_INT64_T
	typedef unsigned long long  stio_uint64_t;
	typedef signed long long    stio_int64_t;
#elif defined(STIO_SIZEOF___INT64) && (STIO_SIZEOF___INT64 == 8)
#	define STIO_HAVE_UINT64_T
#	define STIO_HAVE_INT64_T
	typedef unsigned __int64    stio_uint64_t;
	typedef signed __int64      stio_int64_t;
#elif defined(STIO_SIZEOF___INT64_T) && (STIO_SIZEOF___INT64_T == 8)
#	define STIO_HAVE_UINT64_T
#	define STIO_HAVE_INT64_T
	typedef unsigned __int64_t  stio_uint64_t;
	typedef signed __int64_t    stio_int64_t;
#elif defined(_WIN64) || defined(_WIN32)
#	define STIO_HAVE_UINT64_T
#	define STIO_HAVE_INT64_T
	typedef unsigned __int64  stio_uint64_t;
	typedef signed __int64    stio_int64_t;
#else
	/* no 64-bit integer */
#endif

/* stio_int128_t */
#if defined(STIO_SIZEOF_INT) && (STIO_SIZEOF_INT == 16)
#	define STIO_HAVE_UINT128_T
#	define STIO_HAVE_INT128_T
	typedef unsigned int        stio_uint128_t;
	typedef signed int          stio_int128_t;
#elif defined(STIO_SIZEOF_LONG) && (STIO_SIZEOF_LONG == 16)
#	define STIO_HAVE_UINT128_T
#	define STIO_HAVE_INT128_T
	typedef unsigned long       stio_uint128_t;
	typedef signed long         stio_int128_t;
#elif defined(STIO_SIZEOF_LONG_LONG) && (STIO_SIZEOF_LONG_LONG == 16)
#	define STIO_HAVE_UINT128_T
#	define STIO_HAVE_INT128_T
	typedef unsigned long long  stio_uint128_t;
	typedef signed long long    stio_int128_t;
#elif defined(STIO_SIZEOF___INT128) && (STIO_SIZEOF___INT128 == 16)
#	define STIO_HAVE_UINT128_T
#	define STIO_HAVE_INT128_T
	typedef unsigned __int128    stio_uint128_t;
	typedef signed __int128      stio_int128_t;
#elif defined(STIO_SIZEOF___INT128_T) && (STIO_SIZEOF___INT128_T == 16)
#	define STIO_HAVE_UINT128_T
#	define STIO_HAVE_INT128_T
	#if defined(__clang__)
	typedef __uint128_t  stio_uint128_t;
	typedef __int128_t   stio_int128_t;
	#else
	typedef unsigned __int128_t  stio_uint128_t;
	typedef signed __int128_t    stio_int128_t;
	#endif
#else
	/* no 128-bit integer */
#endif

#if defined(STIO_HAVE_UINT8_T) && (STIO_SIZEOF_VOID_P == 1)
#	error UNSUPPORTED POINTER SIZE
#elif defined(STIO_HAVE_UINT16_T) && (STIO_SIZEOF_VOID_P == 2)
	typedef stio_uint16_t stio_uintptr_t;
	typedef stio_int16_t stio_intptr_t;
	typedef stio_uint8_t stio_ushortptr_t;
	typedef stio_int8_t stio_shortptr_t;
#elif defined(STIO_HAVE_UINT32_T) && (STIO_SIZEOF_VOID_P == 4)
	typedef stio_uint32_t stio_uintptr_t;
	typedef stio_int32_t stio_intptr_t;
	typedef stio_uint16_t stio_ushortptr_t;
	typedef stio_int16_t stio_shortptr_t;
#elif defined(STIO_HAVE_UINT64_T) && (STIO_SIZEOF_VOID_P == 8)
	typedef stio_uint64_t stio_uintptr_t;
	typedef stio_int64_t stio_intptr_t;
	typedef stio_uint32_t stio_ushortptr_t;
	typedef stio_int32_t stio_shortptr_t;
#elif defined(STIO_HAVE_UINT128_T) && (STIO_SIZEOF_VOID_P == 16) 
	typedef stio_uint128_t stio_uintptr_t;
	typedef stio_int128_t stio_intptr_t;
	typedef stio_uint64_t stio_ushortptr_t;
	typedef stio_int64_t stio_shortptr_t;
#else
#	error UNKNOWN POINTER SIZE
#endif

#define STIO_SIZEOF_INTPTR_T STIO_SIZEOF_VOID_P
#define STIO_SIZEOF_UINTPTR_T STIO_SIZEOF_VOID_P
#define STIO_SIZEOF_SHORTPTR_T (STIO_SIZEOF_VOID_P / 2)
#define STIO_SIZEOF_USHORTPTR_T (STIO_SIZEOF_VOID_P / 2)

#if defined(STIO_HAVE_INT128_T)
#	define STIO_SIZEOF_INTMAX_T 16
#	define STIO_SIZEOF_UINTMAX_T 16
	typedef stio_int128_t stio_intmax_t;
	typedef stio_uint128_t stio_uintmax_t;
#elif defined(STIO_HAVE_INT64_T)
#	define STIO_SIZEOF_INTMAX_T 8
#	define STIO_SIZEOF_UINTMAX_T 8
	typedef stio_int64_t stio_intmax_t;
	typedef stio_uint64_t stio_uintmax_t;
#elif defined(STIO_HAVE_INT32_T)
#	define STIO_SIZEOF_INTMAX_T 4
#	define STIO_SIZEOF_UINTMAX_T 4
	typedef stio_int32_t stio_intmax_t;
	typedef stio_uint32_t stio_uintmax_t;
#elif defined(STIO_HAVE_INT16_T)
#	define STIO_SIZEOF_INTMAX_T 2
#	define STIO_SIZEOF_UINTMAX_T 2
	typedef stio_int16_t stio_intmax_t;
	typedef stio_uint16_t stio_uintmax_t;
#elif defined(STIO_HAVE_INT8_T)
#	define STIO_SIZEOF_INTMAX_T 1
#	define STIO_SIZEOF_UINTMAX_T 1
	typedef stio_int8_t stio_intmax_t;
	typedef stio_uint8_t stio_uintmax_t;
#else
#	error UNKNOWN INTMAX SIZE
#endif

/* =========================================================================
 * BASIC STIO TYPES
 * =========================================================================*/

typedef stio_uint8_t stio_byte_t;
typedef stio_uintptr_t stio_size_t;


typedef char stio_mchar_t;
typedef int stio_mcint_t;

#define STIO_MT(x) (x)
/* =========================================================================
 * PRIMITIVE MACROS
 * ========================================================================= */
#define STIO_SIZEOF(x) (sizeof(x))
#define STIO_COUNTOF(x) (sizeof(x) / sizeof(x[0]))

/**
 * The STIO_OFFSETOF() macro returns the offset of a field from the beginning
 * of a structure.
 */
#define STIO_OFFSETOF(type,member) ((stio_uintptr_t)&((type*)0)->member)

/**
 * The STIO_ALIGNOF() macro returns the alignment size of a structure.
 * Note that this macro may not work reliably depending on the type given.
 */
#define STIO_ALIGNOF(type) STIO_OFFSETOF(struct { stio_uint8_t d1; type d2; }, d2)
        /*(sizeof(struct { stio_uint8_t d1; type d2; }) - sizeof(type))*/

/**
 * Round up a positive integer to the nearest multiple of 'align' 
 */
#define STIO_ALIGNTO(num,align) ((((num) + (align) - 1) / (align)) * (align))

#if 0
/**
 * Round up a number, both positive and negative, to the nearest multiple of 'align' 
 */
#define STIO_ALIGNTO(num,align) ((((num) + (num >= 0? 1: -1) * (align) - 1) / (align)) * (align))
#endif

/**
 * Round up a positive integer to to the nearest multiple of 'align' which
 * should be a multiple of a power of 2
 */
#define STIO_ALIGNTO_POW2(num,align) ((((num) + (align) - 1)) & ~((align) - 1))

#if defined(__cplusplus)
#	if (__cplusplus >= 201103L) /* C++11 */
#		define STIO_NULL nullptr
#	else
#		define STIO_NULL (0)
#	endif
#else
#	define STIO_NULL ((void*)0)
#endif

/* make a bit mask that can mask off low n bits */
#define STIO_LBMASK(type,n) (~(~((type)0) << (n))) 
#define STIO_LBMASK_SAFE(type,n) (((n) < STIO_SIZEOF(type) * 8)? STIO_LBMASK(type,n): ~(type)0)

/* make a bit mask that can mask off hig n bits */
#define STIO_HBMASK(type,n) (~(~((type)0) >> (n)))
#define STIO_HBMASK_SAFE(type,n) (((n) < STIO_SIZEOF(type) * 8)? STIO_HBMASK(type,n): ~(type)0)

/* get 'length' bits starting from the bit at the 'offset' */
#define STIO_GETBITS(type,value,offset,length) \
	((((type)(value)) >> (offset)) & STIO_LBMASK(type,length))

#define STIO_SETBITS(type,value,offset,length,bits) \
	(value = (((type)(value)) | (((bits) & STIO_LBMASK(type,length)) << (offset))))


/** 
 * The STIO_BITS_MAX() macros calculates the maximum value that the 'nbits'
 * bits of an unsigned integer of the given 'type' can hold.
 * \code
 * printf ("%u", STIO_BITS_MAX(unsigned int, 5));
 * \endcode
 */
/*#define STIO_BITS_MAX(type,nbits) ((((type)1) << (nbits)) - 1)*/
#define STIO_BITS_MAX(type,nbits) ((~(type)0) >> (STIO_SIZEOF(type) * 8 - (nbits)))

/* =========================================================================
 * MMGR
 * ========================================================================= */
typedef struct stio_mmgr_t stio_mmgr_t;

/** 
 * allocate a memory chunk of the size \a n.
 * \return pointer to a memory chunk on success, #STIO_NULL on failure.
 */
typedef void* (*stio_mmgr_alloc_t)   (stio_mmgr_t* mmgr, stio_size_t n);
/** 
 * resize a memory chunk pointed to by \a ptr to the size \a n.
 * \return pointer to a memory chunk on success, #STIO_NULL on failure.
 */
typedef void* (*stio_mmgr_realloc_t) (stio_mmgr_t* mmgr, void* ptr, stio_size_t n);
/**
 * free a memory chunk pointed to by \a ptr.
 */
typedef void  (*stio_mmgr_free_t)    (stio_mmgr_t* mmgr, void* ptr);

/**
 * The stio_mmgr_t type defines the memory management interface.
 * As the type is merely a structure, it is just used as a single container
 * for memory management functions with a pointer to user-defined data. 
 * The user-defined data pointer \a ctx is passed to each memory management 
 * function whenever it is called. You can allocate, reallocate, and free 
 * a memory chunk.
 *
 * For example, a stio_xxx_open() function accepts a pointer of the stio_mmgr_t
 * type and the xxx object uses it to manage dynamic data within the object. 
 */
struct stio_mmgr_t
{
	stio_mmgr_alloc_t   alloc;   /**< allocation function */
	stio_mmgr_realloc_t realloc; /**< resizing function */
	stio_mmgr_free_t    free;    /**< disposal function */
	void*               ctx;     /**< user-defined data pointer */
};

/**
 * The STIO_MMGR_ALLOC() macro allocates a memory block of the \a size bytes
 * using the \a mmgr memory manager.
 */
#define STIO_MMGR_ALLOC(mmgr,size) ((mmgr)->alloc(mmgr,size))

/**
 * The STIO_MMGR_REALLOC() macro resizes a memory block pointed to by \a ptr 
 * to the \a size bytes using the \a mmgr memory manager.
 */
#define STIO_MMGR_REALLOC(mmgr,ptr,size) ((mmgr)->realloc(mmgr,ptr,size))

/** 
 * The STIO_MMGR_FREE() macro deallocates the memory block pointed to by \a ptr.
 */
#define STIO_MMGR_FREE(mmgr,ptr) ((mmgr)->free(mmgr,ptr))


/* =========================================================================
 * MACROS THAT CHANGES THE BEHAVIORS OF THE C COMPILER/LINKER
 * =========================================================================*/

#if defined(_WIN32) || (defined(__WATCOMC__) && !defined(__WINDOWS_386__))
#	define STIO_IMPORT __declspec(dllimport)
#	define STIO_EXPORT __declspec(dllexport)
#	define STIO_PRIVATE 
#elif defined(__GNUC__) && (__GNUC__>=4)
#	define STIO_IMPORT __attribute__((visibility("default")))
#	define STIO_EXPORT __attribute__((visibility("default")))
#	define STIO_PRIVATE __attribute__((visibility("hidden")))
/*#	define STIO_PRIVATE __attribute__((visibility("internal")))*/
#else
#	define STIO_IMPORT
#	define STIO_EXPORT
#	define STIO_PRIVATE
#endif

#if defined(__STDC_VERSION__) && (__STDC_VERSION__>=199901L)
#	define STIO_INLINE inline
#	define STIO_HAVE_INLINE
#elif defined(__GNUC__) && defined(__GNUC_GNU_INLINE__)
	/* gcc disables inline when -std=c89 or -ansi is used. 
	 * so use __inline__ supported by gcc regardless of the options */
#	define STIO_INLINE /*extern*/ __inline__
#	define STIO_HAVE_INLINE
#else
#	define STIO_INLINE 
#	undef STIO_HAVE_INLINE
#endif


/**
 * The STIO_TYPE_IS_SIGNED() macro determines if a type is signed.
 * \code
 * printf ("%d\n", (int)STIO_TYPE_IS_SIGNED(int));
 * printf ("%d\n", (int)STIO_TYPE_IS_SIGNED(unsigned int));
 * \endcode
 */
#define STIO_TYPE_IS_SIGNED(type) (((type)0) > ((type)-1))

/**
 * The STIO_TYPE_IS_SIGNED() macro determines if a type is unsigned.
 * \code
 * printf ("%d\n", STIO_TYPE_IS_UNSIGNED(int));
 * printf ("%d\n", STIO_TYPE_IS_UNSIGNED(unsigned int));
 * \endcode
 */
#define STIO_TYPE_IS_UNSIGNED(type) (((type)0) < ((type)-1))

#define STIO_TYPE_SIGNED_MAX(type) \
	((type)~((type)1 << ((type)STIO_SIZEOF(type) * 8 - 1)))
#define STIO_TYPE_UNSIGNED_MAX(type) ((type)(~(type)0))

#define STIO_TYPE_SIGNED_MIN(type) \
	((type)((type)1 << ((type)STIO_SIZEOF(type) * 8 - 1)))
#define STIO_TYPE_UNSIGNED_MIN(type) ((type)0)

#define STIO_TYPE_MAX(type) \
	((STIO_TYPE_IS_SIGNED(type)? STIO_TYPE_SIGNED_MAX(type): STIO_TYPE_UNSIGNED_MAX(type)))
#define STIO_TYPE_MIN(type) \
	((STIO_TYPE_IS_SIGNED(type)? STIO_TYPE_SIGNED_MIN(type): STIO_TYPE_UNSIGNED_MIN(type)))


/* =========================================================================
 * COMPILER FEATURE TEST MACROS
 * =========================================================================*/
#if defined(__has_builtin)
	#if __has_builtin(__builtin_ctz)
		#define STIO_HAVE_BUILTIN_CTZ
	#endif

	#if __has_builtin(__builtin_uadd_overflow)
		#define STIO_HAVE_BUILTIN_UADD_OVERFLOW 
	#endif
	#if __has_builtin(__builtin_uaddl_overflow)
		#define STIO_HAVE_BUILTIN_UADDL_OVERFLOW 
	#endif
	#if __has_builtin(__builtin_uaddll_overflow)
		#define STIO_HAVE_BUILTIN_UADDLL_OVERFLOW 
	#endif
	#if __has_builtin(__builtin_umul_overflow)
		#define STIO_HAVE_BUILTIN_UMUL_OVERFLOW 
	#endif
	#if __has_builtin(__builtin_umull_overflow)
		#define STIO_HAVE_BUILTIN_UMULL_OVERFLOW 
	#endif
	#if __has_builtin(__builtin_umulll_overflow)
		#define STIO_HAVE_BUILTIN_UMULLL_OVERFLOW 
	#endif

	#if __has_builtin(__builtin_sadd_overflow)
		#define STIO_HAVE_BUILTIN_SADD_OVERFLOW 
	#endif
	#if __has_builtin(__builtin_saddl_overflow)
		#define STIO_HAVE_BUILTIN_SADDL_OVERFLOW 
	#endif
	#if __has_builtin(__builtin_saddll_overflow)
		#define STIO_HAVE_BUILTIN_SADDLL_OVERFLOW 
	#endif
	#if __has_builtin(__builtin_smul_overflow)
		#define STIO_HAVE_BUILTIN_SMUL_OVERFLOW 
	#endif
	#if __has_builtin(__builtin_smull_overflow)
		#define STIO_HAVE_BUILTIN_SMULL_OVERFLOW 
	#endif
	#if __has_builtin(__builtin_smulll_overflow)
		#define STIO_HAVE_BUILTIN_SMULLL_OVERFLOW 
	#endif
#elif defined(__GNUC__) && defined(__GNUC_MINOR__)

	#if (__GNUC__ >= 4) || (__GNUC__ == 3 && __GNUC_MINOR__ >= 4)
		#define STIO_HAVE_BUILTIN_CTZ
	#endif

	#if (__GNUC__ >= 5)
		#define STIO_HAVE_BUILTIN_UADD_OVERFLOW
		#define STIO_HAVE_BUILTIN_UADDL_OVERFLOW
		#define STIO_HAVE_BUILTIN_UADDLL_OVERFLOW
		#define STIO_HAVE_BUILTIN_UMUL_OVERFLOW
		#define STIO_HAVE_BUILTIN_UMULL_OVERFLOW
		#define STIO_HAVE_BUILTIN_UMULLL_OVERFLOW

		#define STIO_HAVE_BUILTIN_SADD_OVERFLOW
		#define STIO_HAVE_BUILTIN_SADDL_OVERFLOW
		#define STIO_HAVE_BUILTIN_SADDLL_OVERFLOW
		#define STIO_HAVE_BUILTIN_SMUL_OVERFLOW
		#define STIO_HAVE_BUILTIN_SMULL_OVERFLOW
		#define STIO_HAVE_BUILTIN_SMULLL_OVERFLOW
	#endif

#endif

/*
#if !defined(__has_builtin)
	#define __has_builtin(x) 0
#endif


#if !defined(__is_identifier)
	#define __is_identifier(x) 0
#endif

#if !defined(__has_attribute)
	#define __has_attribute(x) 0
#endif
*/



#endif
