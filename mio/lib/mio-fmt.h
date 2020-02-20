/*
 * $Id$
 *
    Copyright (c) 2006-2019 Chung, Hyung-Hwan. All rights reserved.

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

#ifndef _MIO_FMT_H_
#define _MIO_FMT_H_

#include <mio-cmn.h>
#include <stdarg.h>

/** \file
 * This file defines various formatting functions.
 */

/* =========================================================================
 * FORMATTED OUTPUT
 * ========================================================================= */
typedef struct mio_fmtout_t mio_fmtout_t;

typedef int (*mio_fmtout_putbcs_t) (
	mio_fmtout_t*     fmtout,
	const mio_bch_t*  ptr,
	mio_oow_t         len
);

typedef int (*mio_fmtout_putucs_t) (
	mio_fmtout_t*     fmtout,
	const mio_uch_t*  ptr,
	mio_oow_t         len
);

enum mio_fmtout_fmt_type_t 
{
	MIO_FMTOUT_FMT_TYPE_BCH = 0,
	MIO_FMTOUT_FMT_TYPE_UCH
};
typedef enum mio_fmtout_fmt_type_t mio_fmtout_fmt_type_t;


struct mio_fmtout_t
{
	mio_oow_t             count; /* out */

	mio_fmtout_putbcs_t   putbcs; /* in */
	mio_fmtout_putucs_t   putucs; /* in */
	mio_bitmask_t         mask;   /* in */
	void*                 ctx;    /* in */

	mio_fmtout_fmt_type_t fmt_type;
	const void*           fmt_str;
};

/* =========================================================================
 * INTMAX FORMATTING 
 * ========================================================================= */
/** 
 * The mio_fmt_intmax_flag_t type defines enumerators to change the
 * behavior of mio_fmt_intmax() and mio_fmt_uintmax().
 */
enum mio_fmt_intmax_flag_t
{
	/* Use lower 6 bits to represent base between 2 and 36 inclusive.
	 * Upper bits are used for these flag options */

	/** Don't truncate if the buffer is not large enough */
	MIO_FMT_INTMAX_NOTRUNC = (0x40 << 0),
#define MIO_FMT_INTMAX_NOTRUNC             MIO_FMT_INTMAX_NOTRUNC
#define MIO_FMT_UINTMAX_NOTRUNC            MIO_FMT_INTMAX_NOTRUNC
#define MIO_FMT_INTMAX_TO_BCSTR_NOTRUNC    MIO_FMT_INTMAX_NOTRUNC
#define MIO_FMT_UINTMAX_TO_BCSTR_NOTRUNC   MIO_FMT_INTMAX_NOTRUNC
#define MIO_FMT_INTMAX_TO_UCSTR_NOTRUNC    MIO_FMT_INTMAX_NOTRUNC
#define MIO_FMT_UINTMAX_TO_UCSTR_NOTRUNC   MIO_FMT_INTMAX_NOTRUNC
#define MIO_FMT_INTMAX_TO_OOCSTR_NOTRUNC   MIO_FMT_INTMAX_NOTRUNC
#define MIO_FMT_UINTMAX_TO_OOCSTR_NOTRUNC  MIO_FMT_INTMAX_NOTRUNC

	/** Don't append a terminating null */
	MIO_FMT_INTMAX_NONULL = (0x40 << 1),
#define MIO_FMT_INTMAX_NONULL             MIO_FMT_INTMAX_NONULL
#define MIO_FMT_UINTMAX_NONULL            MIO_FMT_INTMAX_NONULL
#define MIO_FMT_INTMAX_TO_BCSTR_NONULL    MIO_FMT_INTMAX_NONULL
#define MIO_FMT_UINTMAX_TO_BCSTR_NONULL   MIO_FMT_INTMAX_NONULL
#define MIO_FMT_INTMAX_TO_UCSTR_NONULL    MIO_FMT_INTMAX_NONULL
#define MIO_FMT_UINTMAX_TO_UCSTR_NONULL   MIO_FMT_INTMAX_NONULL
#define MIO_FMT_INTMAX_TO_OOCSTR_NONULL   MIO_FMT_INTMAX_NONULL
#define MIO_FMT_UINTMAX_TO_OOCSTR_NONULL  MIO_FMT_INTMAX_NONULL

	/** Produce no digit for a value of zero  */
	MIO_FMT_INTMAX_NOZERO = (0x40 << 2),
#define MIO_FMT_INTMAX_NOZERO             MIO_FMT_INTMAX_NOZERO
#define MIO_FMT_UINTMAX_NOZERO            MIO_FMT_INTMAX_NOZERO
#define MIO_FMT_INTMAX_TO_BCSTR_NOZERO    MIO_FMT_INTMAX_NOZERO
#define MIO_FMT_UINTMAX_TO_BCSTR_NOZERO   MIO_FMT_INTMAX_NOZERO
#define MIO_FMT_INTMAX_TO_UCSTR_NOZERO    MIO_FMT_INTMAX_NOZERO
#define MIO_FMT_UINTMAX_TO_UCSTR_NOZERO   MIO_FMT_INTMAX_NOZERO
#define MIO_FMT_INTMAX_TO_OOCSTR_NOZERO   MIO_FMT_INTMAX_NOZERO
#define MIO_FMT_UINTMAX_TO_OOCSTR_NOZERO  MIO_FMT_INTMAX_NOZERO

	/** Produce a leading zero for a non-zero value */
	MIO_FMT_INTMAX_ZEROLEAD = (0x40 << 3),
#define MIO_FMT_INTMAX_ZEROLEAD             MIO_FMT_INTMAX_ZEROLEAD
#define MIO_FMT_UINTMAX_ZEROLEAD            MIO_FMT_INTMAX_ZEROLEAD
#define MIO_FMT_INTMAX_TO_BCSTR_ZEROLEAD    MIO_FMT_INTMAX_ZEROLEAD
#define MIO_FMT_UINTMAX_TO_BCSTR_ZEROLEAD   MIO_FMT_INTMAX_ZEROLEAD
#define MIO_FMT_INTMAX_TO_UCSTR_ZEROLEAD    MIO_FMT_INTMAX_ZEROLEAD
#define MIO_FMT_UINTMAX_TO_UCSTR_ZEROLEAD   MIO_FMT_INTMAX_ZEROLEAD
#define MIO_FMT_INTMAX_TO_OOCSTR_ZEROLEAD   MIO_FMT_INTMAX_ZEROLEAD
#define MIO_FMT_UINTMAX_TO_OOCSTR_ZEROLEAD  MIO_FMT_INTMAX_ZEROLEAD

	/** Use uppercase letters for alphabetic digits */
	MIO_FMT_INTMAX_UPPERCASE = (0x40 << 4),
#define MIO_FMT_INTMAX_UPPERCASE             MIO_FMT_INTMAX_UPPERCASE
#define MIO_FMT_UINTMAX_UPPERCASE            MIO_FMT_INTMAX_UPPERCASE
#define MIO_FMT_INTMAX_TO_BCSTR_UPPERCASE    MIO_FMT_INTMAX_UPPERCASE
#define MIO_FMT_UINTMAX_TO_BCSTR_UPPERCASE   MIO_FMT_INTMAX_UPPERCASE
#define MIO_FMT_INTMAX_TO_UCSTR_UPPERCASE    MIO_FMT_INTMAX_UPPERCASE
#define MIO_FMT_UINTMAX_TO_UCSTR_UPPERCASE   MIO_FMT_INTMAX_UPPERCASE
#define MIO_FMT_INTMAX_TO_OOCSTR_UPPERCASE   MIO_FMT_INTMAX_UPPERCASE
#define MIO_FMT_UINTMAX_TO_OOCSTR_UPPERCASE  MIO_FMT_INTMAX_UPPERCASE

	/** Insert a plus sign for a positive integer including 0 */
	MIO_FMT_INTMAX_PLUSSIGN = (0x40 << 5),
#define MIO_FMT_INTMAX_PLUSSIGN             MIO_FMT_INTMAX_PLUSSIGN
#define MIO_FMT_UINTMAX_PLUSSIGN            MIO_FMT_INTMAX_PLUSSIGN
#define MIO_FMT_INTMAX_TO_BCSTR_PLUSSIGN    MIO_FMT_INTMAX_PLUSSIGN
#define MIO_FMT_UINTMAX_TO_BCSTR_PLUSSIGN   MIO_FMT_INTMAX_PLUSSIGN
#define MIO_FMT_INTMAX_TO_UCSTR_PLUSSIGN    MIO_FMT_INTMAX_PLUSSIGN
#define MIO_FMT_UINTMAX_TO_UCSTR_PLUSSIGN   MIO_FMT_INTMAX_PLUSSIGN
#define MIO_FMT_INTMAX_TO_OOCSTR_PLUSSIGN   MIO_FMT_INTMAX_PLUSSIGN
#define MIO_FMT_UINTMAX_TO_OOCSTR_PLUSSIGN  MIO_FMT_INTMAX_PLUSSIGN

	/** Insert a space for a positive integer including 0 */
	MIO_FMT_INTMAX_EMPTYSIGN = (0x40 << 6),
#define MIO_FMT_INTMAX_EMPTYSIGN             MIO_FMT_INTMAX_EMPTYSIGN
#define MIO_FMT_UINTMAX_EMPTYSIGN            MIO_FMT_INTMAX_EMPTYSIGN
#define MIO_FMT_INTMAX_TO_BCSTR_EMPTYSIGN    MIO_FMT_INTMAX_EMPTYSIGN
#define MIO_FMT_UINTMAX_TO_BCSTR_EMPTYSIGN   MIO_FMT_INTMAX_EMPTYSIGN
#define MIO_FMT_INTMAX_TO_UCSTR_EMPTYSIGN    MIO_FMT_INTMAX_EMPTYSIGN
#define MIO_FMT_UINTMAX_TO_UCSTR_EMPTYSIGN   MIO_FMT_INTMAX_EMPTYSIGN

	/** Fill the right part of the string */
	MIO_FMT_INTMAX_FILLRIGHT = (0x40 << 7),
#define MIO_FMT_INTMAX_FILLRIGHT             MIO_FMT_INTMAX_FILLRIGHT
#define MIO_FMT_UINTMAX_FILLRIGHT            MIO_FMT_INTMAX_FILLRIGHT
#define MIO_FMT_INTMAX_TO_BCSTR_FILLRIGHT    MIO_FMT_INTMAX_FILLRIGHT
#define MIO_FMT_UINTMAX_TO_BCSTR_FILLRIGHT   MIO_FMT_INTMAX_FILLRIGHT
#define MIO_FMT_INTMAX_TO_UCSTR_FILLRIGHT    MIO_FMT_INTMAX_FILLRIGHT
#define MIO_FMT_UINTMAX_TO_UCSTR_FILLRIGHT   MIO_FMT_INTMAX_FILLRIGHT
#define MIO_FMT_INTMAX_TO_OOCSTR_FILLRIGHT   MIO_FMT_INTMAX_FILLRIGHT
#define MIO_FMT_UINTMAX_TO_OOCSTR_FILLRIGHT  MIO_FMT_INTMAX_FILLRIGHT

	/** Fill between the sign chacter and the digit part */
	MIO_FMT_INTMAX_FILLCENTER = (0x40 << 8)
#define MIO_FMT_INTMAX_FILLCENTER             MIO_FMT_INTMAX_FILLCENTER
#define MIO_FMT_UINTMAX_FILLCENTER            MIO_FMT_INTMAX_FILLCENTER
#define MIO_FMT_INTMAX_TO_BCSTR_FILLCENTER    MIO_FMT_INTMAX_FILLCENTER
#define MIO_FMT_UINTMAX_TO_BCSTR_FILLCENTER   MIO_FMT_INTMAX_FILLCENTER
#define MIO_FMT_INTMAX_TO_UCSTR_FILLCENTER    MIO_FMT_INTMAX_FILLCENTER
#define MIO_FMT_UINTMAX_TO_UCSTR_FILLCENTER   MIO_FMT_INTMAX_FILLCENTER
#define MIO_FMT_INTMAX_TO_OOCSTR_FILLCENTER   MIO_FMT_INTMAX_FILLCENTER
#define MIO_FMT_UINTMAX_TO_OOCSTR_FILLCENTER  MIO_FMT_INTMAX_FILLCENTER
};

#if defined(__cplusplus)
extern "C" {
#endif

/* =========================================================================
 * FORMATTED OUTPUT
 * ========================================================================= */

MIO_EXPORT int mio_bfmt_outv (
	mio_fmtout_t*    fmtout,
	const mio_bch_t* fmt,
	va_list          ap
);

MIO_EXPORT int mio_ufmt_outv (
	mio_fmtout_t*    fmtout,
	const mio_uch_t* fmt,
	va_list          ap
);


MIO_EXPORT int mio_bfmt_out (
	mio_fmtout_t*    fmtout,
	const mio_bch_t* fmt,
	...
);

MIO_EXPORT int mio_ufmt_out (
	mio_fmtout_t*    fmtout,
	const mio_uch_t* fmt,
	...
);



/* =========================================================================
 * INTMAX FORMATTING
 * ========================================================================= */

/**
 * The mio_fmt_intmax_to_bcstr() function formats an integer \a value to a 
 * multibyte string according to the given base and writes it to a buffer 
 * pointed to by \a buf. It writes to the buffer at most \a size characters 
 * including the terminating null. The base must be between 2 and 36 inclusive 
 * and can be ORed with zero or more #mio_fmt_intmax_to_bcstr_flag_t enumerators. 
 * This ORed value is passed to the function via the \a base_and_flags 
 * parameter. If the formatted string is shorter than \a bufsize, the redundant
 * slots are filled with the fill character \a fillchar if it is not a null 
 * character. The filling behavior is determined by the flags shown below:
 *
 * - If #MIO_FMT_INTMAX_TO_BCSTR_FILLRIGHT is set in \a base_and_flags, slots 
 *   after the formatting string are filled.
 * - If #MIO_FMT_INTMAX_TO_BCSTR_FILLCENTER is set in \a base_and_flags, slots 
 *   before the formatting string are filled. However, if it contains the
 *   sign character, the slots between the sign character and the digit part
 *   are filled.  
 * - If neither #MIO_FMT_INTMAX_TO_BCSTR_FILLRIGHT nor #MIO_FMT_INTMAX_TO_BCSTR_FILLCENTER
 *   , slots before the formatting string are filled.
 *
 * The \a precision parameter specified the minimum number of digits to
 * produce from the \a value. If \a value produces fewer digits than
 * \a precision, the actual digits are padded with '0' to meet the precision
 * requirement. You can pass a negative number if you don't wish to specify
 * precision.
 *
 * The terminating null is not added if #MIO_FMT_INTMAX_TO_BCSTR_NONULL is set;
 * The #MIO_FMT_INTMAX_TO_BCSTR_UPPERCASE flag indicates that the function should
 * use the uppercase letter for a alphabetic digit; 
 * You can set #MIO_FMT_INTMAX_TO_BCSTR_NOTRUNC if you require lossless formatting.
 * The #MIO_FMT_INTMAX_TO_BCSTR_PLUSSIGN flag and #MIO_FMT_INTMAX_TO_BCSTR_EMPTYSIGN 
 * ensures that the plus sign and a space is added for a positive integer 
 * including 0 respectively.
 * The #MIO_FMT_INTMAX_TO_BCSTR_ZEROLEAD flag ensures that the numeric string
 * begins with '0' before applying the prefix.
 * You can set the #MIO_FMT_INTMAX_TO_BCSTR_NOZERO flag if you want the value of
 * 0 to produce nothing. If both #MIO_FMT_INTMAX_TO_BCSTR_NOZERO and 
 * #MIO_FMT_INTMAX_TO_BCSTR_ZEROLEAD are specified, '0' is still produced.
 * 
 * If \a prefix is not #MIO_NULL, it is inserted before the digits.
 * 
 * \return
 *  - -1 if the base is not between 2 and 36 inclusive. 
 *  - negated number of characters required for lossless formatting 
 *   - if \a bufsize is 0.
 *   - if #MIO_FMT_INTMAX_TO_BCSTR_NOTRUNC is set and \a bufsize is less than
 *     the minimum required for lossless formatting.
 *  - number of characters written to the buffer excluding a terminating 
 *    null in all other cases.
 */
MIO_EXPORT int mio_fmt_intmax_to_bcstr (
	mio_bch_t*         buf,             /**< buffer pointer */
	int                bufsize,         /**< buffer size */
	mio_intmax_t       value,           /**< integer to format */
	int                base_and_flags,  /**< base ORed with flags */
	int                precision,       /**< precision */
	mio_bch_t          fillchar,        /**< fill character */
	const mio_bch_t*   prefix           /**< prefix */
);

/**
 * The mio_fmt_intmax_to_ucstr() function formats an integer \a value to a 
 * wide-character string according to the given base and writes it to a buffer 
 * pointed to by \a buf. It writes to the buffer at most \a size characters 
 * including the terminating null. The base must be between 2 and 36 inclusive 
 * and can be ORed with zero or more #mio_fmt_intmax_to_ucstr_flag_t enumerators. 
 * This ORed value is passed to the function via the \a base_and_flags 
 * parameter. If the formatted string is shorter than \a bufsize, the redundant
 * slots are filled with the fill character \a fillchar if it is not a null 
 * character. The filling behavior is determined by the flags shown below:
 *
 * - If #MIO_FMT_INTMAX_TO_UCSTR_FILLRIGHT is set in \a base_and_flags, slots 
 *   after the formatting string are filled.
 * - If #MIO_FMT_INTMAX_TO_UCSTR_FILLCENTER is set in \a base_and_flags, slots 
 *   before the formatting string are filled. However, if it contains the
 *   sign character, the slots between the sign character and the digit part
 *   are filled.  
 * - If neither #MIO_FMT_INTMAX_TO_UCSTR_FILLRIGHT nor #MIO_FMT_INTMAX_TO_UCSTR_FILLCENTER
 *   , slots before the formatting string are filled.
 * 
 * The \a precision parameter specified the minimum number of digits to
 * produce from the \ value. If \a value produces fewer digits than
 * \a precision, the actual digits are padded with '0' to meet the precision
 * requirement. You can pass a negative number if don't wish to specify
 * precision.
 *
 * The terminating null is not added if #MIO_FMT_INTMAX_TO_UCSTR_NONULL is set;
 * The #MIO_FMT_INTMAX_TO_UCSTR_UPPERCASE flag indicates that the function should
 * use the uppercase letter for a alphabetic digit; 
 * You can set #MIO_FMT_INTMAX_TO_UCSTR_NOTRUNC if you require lossless formatting.
 * The #MIO_FMT_INTMAX_TO_UCSTR_PLUSSIGN flag and #MIO_FMT_INTMAX_TO_UCSTR_EMPTYSIGN 
 * ensures that the plus sign and a space is added for a positive integer 
 * including 0 respectively.
 * The #MIO_FMT_INTMAX_TO_UCSTR_ZEROLEAD flag ensures that the numeric string
 * begins with 0 before applying the prefix.
 * You can set the #MIO_FMT_INTMAX_TO_UCSTR_NOZERO flag if you want the value of
 * 0 to produce nothing. If both #MIO_FMT_INTMAX_TO_UCSTR_NOZERO and 
 * #MIO_FMT_INTMAX_TO_UCSTR_ZEROLEAD are specified, '0' is still produced.
 *
 * If \a prefix is not #MIO_NULL, it is inserted before the digits.
 * 
 * \return
 *  - -1 if the base is not between 2 and 36 inclusive. 
 *  - negated number of characters required for lossless formatting 
 *   - if \a bufsize is 0.
 *   - if #MIO_FMT_INTMAX_TO_UCSTR_NOTRUNC is set and \a bufsize is less than
 *     the minimum required for lossless formatting.
 *  - number of characters written to the buffer excluding a terminating 
 *    null in all other cases.
 */
MIO_EXPORT int mio_fmt_intmax_to_ucstr (
	mio_uch_t*        buf,             /**< buffer pointer */
	int               bufsize,         /**< buffer size */
	mio_intmax_t      value,           /**< integer to format */
	int               base_and_flags,  /**< base ORed with flags */
	int               precision,       /**< precision */
	mio_uch_t         fillchar,        /**< fill character */
	const mio_uch_t*  prefix           /**< prefix */
);


/**
 * The mio_fmt_uintmax_to_bcstr() function formats an unsigned integer \a value 
 * to a multibyte string buffer. It behaves the same as mio_fmt_intmax_to_bcstr() 
 * except that it handles an unsigned integer.
 */
MIO_EXPORT int mio_fmt_uintmax_to_bcstr (
	mio_bch_t*        buf,             /**< buffer pointer */
	int               bufsize,         /**< buffer size */
	mio_uintmax_t     value,           /**< integer to format */
	int               base_and_flags,  /**< base ORed with flags */
	int               precision,       /**< precision */
	mio_bch_t         fillchar,        /**< fill character */
	const mio_bch_t*  prefix           /**< prefix */
);

/**
 * The mio_fmt_uintmax_to_ucstr() function formats an unsigned integer \a value 
 * to a unicode string buffer. It behaves the same as mio_fmt_intmax_to_ucstr() 
 * except that it handles an unsigned integer.
 */
MIO_EXPORT int mio_fmt_uintmax_to_ucstr (
	mio_uch_t*        buf,             /**< buffer pointer */
	int               bufsize,         /**< buffer size */
	mio_uintmax_t     value,           /**< integer to format */
	int               base_and_flags,  /**< base ORed with flags */
	int               precision,       /**< precision */
	mio_uch_t         fillchar,        /**< fill character */
	const mio_uch_t*  prefix           /**< prefix */
);

#if defined(MIO_OOCH_IS_BCH)
#	define mio_fmt_intmax_to_oocstr mio_fmt_intmax_to_bcstr
#	define mio_fmt_uintmax_to_oocstr mio_fmt_uintmax_to_bcstr
#else
#	define mio_fmt_intmax_to_oocstr mio_fmt_intmax_to_ucstr
#	define mio_fmt_uintmax_to_oocstr mio_fmt_uintmax_to_ucstr
#endif

/* TODO: mio_fmt_fltmax_to_bcstr()... mio_fmt_fltmax_to_ucstr() */

#if defined(__cplusplus)
}
#endif

#endif
