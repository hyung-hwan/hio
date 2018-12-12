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


/* =========================================================================
 * PRIMITIVE TYPE DEFINTIONS
 * ========================================================================= */

/* mio_int8_t */
#if defined(MIO_SIZEOF_CHAR) && (MIO_SIZEOF_CHAR == 1)
#	define MIO_HAVE_UINT8_T
#	define MIO_HAVE_INT8_T
	typedef unsigned char      mio_uint8_t;
	typedef signed char        mio_int8_t;
#elif defined(MIO_SIZEOF___INT8) && (MIO_SIZEOF___INT8 == 1)
#	define MIO_HAVE_UINT8_T
#	define MIO_HAVE_INT8_T
	typedef unsigned __int8    mio_uint8_t;
	typedef signed __int8      mio_int8_t;
#elif defined(MIO_SIZEOF___INT8_T) && (MIO_SIZEOF___INT8_T == 1)
#	define MIO_HAVE_UINT8_T
#	define MIO_HAVE_INT8_T
	typedef unsigned __int8_t  mio_uint8_t;
	typedef signed __int8_t    mio_int8_t;
#else
#	define MIO_HAVE_UINT8_T
#	define MIO_HAVE_INT8_T
	typedef unsigned char      mio_uint8_t;
	typedef signed char        mio_int8_t;
#endif


/* mio_int16_t */
#if defined(MIO_SIZEOF_SHORT) && (MIO_SIZEOF_SHORT == 2)
#	define MIO_HAVE_UINT16_T
#	define MIO_HAVE_INT16_T
	typedef unsigned short int  mio_uint16_t;
	typedef signed short int    mio_int16_t;
#elif defined(MIO_SIZEOF___INT16) && (MIO_SIZEOF___INT16 == 2)
#	define MIO_HAVE_UINT16_T
#	define MIO_HAVE_INT16_T
	typedef unsigned __int16    mio_uint16_t;
	typedef signed __int16      mio_int16_t;
#elif defined(MIO_SIZEOF___INT16_T) && (MIO_SIZEOF___INT16_T == 2)
#	define MIO_HAVE_UINT16_T
#	define MIO_HAVE_INT16_T
	typedef unsigned __int16_t  mio_uint16_t;
	typedef signed __int16_t    mio_int16_t;
#else
#	define MIO_HAVE_UINT16_T
#	define MIO_HAVE_INT16_T
	typedef unsigned short int  mio_uint16_t;
	typedef signed short int    mio_int16_t;
#endif


/* mio_int32_t */
#if defined(MIO_SIZEOF_INT) && (MIO_SIZEOF_INT == 4)
#	define MIO_HAVE_UINT32_T
#	define MIO_HAVE_INT32_T
	typedef unsigned int        mio_uint32_t;
	typedef signed int          mio_int32_t;
#elif defined(MIO_SIZEOF_LONG) && (MIO_SIZEOF_LONG == 4)
#	define MIO_HAVE_UINT32_T
#	define MIO_HAVE_INT32_T
	typedef unsigned long       mio_uint32_t;
	typedef signed long         mio_int32_t;
#elif defined(MIO_SIZEOF___INT32) && (MIO_SIZEOF___INT32 == 4)
#	define MIO_HAVE_UINT32_T
#	define MIO_HAVE_INT32_T
	typedef unsigned __int32    mio_uint32_t;
	typedef signed __int32      mio_int32_t;
#elif defined(MIO_SIZEOF___INT32_T) && (MIO_SIZEOF___INT32_T == 4)
#	define MIO_HAVE_UINT32_T
#	define MIO_HAVE_INT32_T
	typedef unsigned __int32_t  mio_uint32_t;
	typedef signed __int32_t    mio_int32_t;
#elif defined(__MSDOS__)
#	define MIO_HAVE_UINT32_T
#	define MIO_HAVE_INT32_T
	typedef unsigned long int   mio_uint32_t;
	typedef signed long int     mio_int32_t;
#else
#	define MIO_HAVE_UINT32_T
#	define MIO_HAVE_INT32_T
	typedef unsigned int        mio_uint32_t;
	typedef signed int          mio_int32_t;
#endif

/* mio_int64_t */
#if defined(MIO_SIZEOF_INT) && (MIO_SIZEOF_INT == 8)
#	define MIO_HAVE_UINT64_T
#	define MIO_HAVE_INT64_T
	typedef unsigned int        mio_uint64_t;
	typedef signed int          mio_int64_t;
#elif defined(MIO_SIZEOF_LONG) && (MIO_SIZEOF_LONG == 8)
#	define MIO_HAVE_UINT64_T
#	define MIO_HAVE_INT64_T
	typedef unsigned long       mio_uint64_t;
	typedef signed long         mio_int64_t;
#elif defined(MIO_SIZEOF_LONG_LONG) && (MIO_SIZEOF_LONG_LONG == 8)
#	define MIO_HAVE_UINT64_T
#	define MIO_HAVE_INT64_T
	typedef unsigned long long  mio_uint64_t;
	typedef signed long long    mio_int64_t;
#elif defined(MIO_SIZEOF___INT64) && (MIO_SIZEOF___INT64 == 8)
#	define MIO_HAVE_UINT64_T
#	define MIO_HAVE_INT64_T
	typedef unsigned __int64    mio_uint64_t;
	typedef signed __int64      mio_int64_t;
#elif defined(MIO_SIZEOF___INT64_T) && (MIO_SIZEOF___INT64_T == 8)
#	define MIO_HAVE_UINT64_T
#	define MIO_HAVE_INT64_T
	typedef unsigned __int64_t  mio_uint64_t;
	typedef signed __int64_t    mio_int64_t;
#elif defined(_WIN64) || defined(_WIN32)
#	define MIO_HAVE_UINT64_T
#	define MIO_HAVE_INT64_T
	typedef unsigned __int64  mio_uint64_t;
	typedef signed __int64    mio_int64_t;
#else
	/* no 64-bit integer */
#endif

/* mio_int128_t */
#if defined(MIO_SIZEOF_INT) && (MIO_SIZEOF_INT == 16)
#	define MIO_HAVE_UINT128_T
#	define MIO_HAVE_INT128_T
	typedef unsigned int        mio_uint128_t;
	typedef signed int          mio_int128_t;
#elif defined(MIO_SIZEOF_LONG) && (MIO_SIZEOF_LONG == 16)
#	define MIO_HAVE_UINT128_T
#	define MIO_HAVE_INT128_T
	typedef unsigned long       mio_uint128_t;
	typedef signed long         mio_int128_t;
#elif defined(MIO_SIZEOF_LONG_LONG) && (MIO_SIZEOF_LONG_LONG == 16)
#	define MIO_HAVE_UINT128_T
#	define MIO_HAVE_INT128_T
	typedef unsigned long long  mio_uint128_t;
	typedef signed long long    mio_int128_t;
#elif defined(MIO_SIZEOF___INT128) && (MIO_SIZEOF___INT128 == 16)
#	define MIO_HAVE_UINT128_T
#	define MIO_HAVE_INT128_T
	typedef unsigned __int128    mio_uint128_t;
	typedef signed __int128      mio_int128_t;
#elif defined(MIO_SIZEOF___INT128_T) && (MIO_SIZEOF___INT128_T == 16)
#	define MIO_HAVE_UINT128_T
#	define MIO_HAVE_INT128_T
	#if defined(__clang__)
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
 * BASIC MIO TYPES
 * =========================================================================*/

typedef mio_uint8_t mio_byte_t;
typedef mio_uintptr_t mio_size_t;


typedef char mio_mchar_t;
typedef int mio_mcint_t;

#define MIO_MT(x) (x)
/* =========================================================================
 * PRIMITIVE MACROS
 * ========================================================================= */
#define MIO_SIZEOF(x) (sizeof(x))
#define MIO_COUNTOF(x) (sizeof(x) / sizeof(x[0]))

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

/**
 * Round up a positive integer to the nearest multiple of 'align' 
 */
#define MIO_ALIGNTO(num,align) ((((num) + (align) - 1) / (align)) * (align))

#if 0
/**
 * Round up a number, both positive and negative, to the nearest multiple of 'align' 
 */
#define MIO_ALIGNTO(num,align) ((((num) + (num >= 0? 1: -1) * (align) - 1) / (align)) * (align))
#endif

/**
 * Round up a positive integer to to the nearest multiple of 'align' which
 * should be a multiple of a power of 2
 */
#define MIO_ALIGNTO_POW2(num,align) ((((num) + (align) - 1)) & ~((align) - 1))

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
#define MIO_LBMASK_SAFE(type,n) (((n) < MIO_SIZEOF(type) * 8)? MIO_LBMASK(type,n): ~(type)0)

/* make a bit mask that can mask off hig n bits */
#define MIO_HBMASK(type,n) (~(~((type)0) >> (n)))
#define MIO_HBMASK_SAFE(type,n) (((n) < MIO_SIZEOF(type) * 8)? MIO_HBMASK(type,n): ~(type)0)

/* get 'length' bits starting from the bit at the 'offset' */
#define MIO_GETBITS(type,value,offset,length) \
	((((type)(value)) >> (offset)) & MIO_LBMASK(type,length))

#define MIO_SETBITS(type,value,offset,length,bits) \
	(value = (((type)(value)) | (((bits) & MIO_LBMASK(type,length)) << (offset))))


/** 
 * The MIO_BITS_MAX() macros calculates the maximum value that the 'nbits'
 * bits of an unsigned integer of the given 'type' can hold.
 * \code
 * printf ("%u", MIO_BITS_MAX(unsigned int, 5));
 * \endcode
 */
/*#define MIO_BITS_MAX(type,nbits) ((((type)1) << (nbits)) - 1)*/
#define MIO_BITS_MAX(type,nbits) ((~(type)0) >> (MIO_SIZEOF(type) * 8 - (nbits)))

/* =========================================================================
 * MMGR
 * ========================================================================= */
typedef struct mio_mmgr_t mio_mmgr_t;

/** 
 * allocate a memory chunk of the size \a n.
 * \return pointer to a memory chunk on success, #MIO_NULL on failure.
 */
typedef void* (*mio_mmgr_alloc_t)   (mio_mmgr_t* mmgr, mio_size_t n);
/** 
 * resize a memory chunk pointed to by \a ptr to the size \a n.
 * \return pointer to a memory chunk on success, #MIO_NULL on failure.
 */
typedef void* (*mio_mmgr_realloc_t) (mio_mmgr_t* mmgr, void* ptr, mio_size_t n);
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

#if defined(__STDC_VERSION__) && (__STDC_VERSION__>=199901L)
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
	((type)~((type)1 << ((type)MIO_SIZEOF(type) * 8 - 1)))
#define MIO_TYPE_UNSIGNED_MAX(type) ((type)(~(type)0))

#define MIO_TYPE_SIGNED_MIN(type) \
	((type)((type)1 << ((type)MIO_SIZEOF(type) * 8 - 1)))
#define MIO_TYPE_UNSIGNED_MIN(type) ((type)0)

#define MIO_TYPE_MAX(type) \
	((MIO_TYPE_IS_SIGNED(type)? MIO_TYPE_SIGNED_MAX(type): MIO_TYPE_UNSIGNED_MAX(type)))
#define MIO_TYPE_MIN(type) \
	((MIO_TYPE_IS_SIGNED(type)? MIO_TYPE_SIGNED_MIN(type): MIO_TYPE_UNSIGNED_MIN(type)))


/* =========================================================================
 * COMPILER FEATURE TEST MACROS
 * =========================================================================*/
#if defined(__has_builtin)
	#if __has_builtin(__builtin_ctz)
		#define MIO_HAVE_BUILTIN_CTZ
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
#elif defined(__GNUC__) && defined(__GNUC_MINOR__)

	#if (__GNUC__ >= 4) || (__GNUC__ == 3 && __GNUC_MINOR__ >= 4)
		#define MIO_HAVE_BUILTIN_CTZ
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
