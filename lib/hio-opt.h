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

#ifndef _HIO_OPT_H_
#define _HIO_OPT_H_

#include "hio-cmn.h"

/** \file
 * This file defines functions and data structures to process
 * command-line arguments.
 */

typedef struct hio_uopt_t hio_uopt_t;
typedef struct hio_uopt_lng_t hio_uopt_lng_t;

struct hio_uopt_lng_t
{
	const hio_uch_t* str;
	hio_uci_t        val;
};

struct hio_uopt_t
{
	/* input */
	const hio_uch_t* str; /* option string  */
	hio_uopt_lng_t*  lng; /* long options */

	/* output */
	hio_uci_t        opt; /* character checked for validity */
	hio_uch_t*       arg; /* argument associated with an option */

	/* output */
	const hio_uch_t* lngopt;

	/* input + output */
	int              ind; /* index into parent argv vector */

	/* input + output - internal*/
	hio_uch_t*       cur;
};

typedef struct hio_bopt_t hio_bopt_t;
typedef struct hio_bopt_lng_t hio_bopt_lng_t;

struct hio_bopt_lng_t
{
	const hio_bch_t* str;
	hio_bci_t        val;
};

struct hio_bopt_t
{
	/* input */
	const hio_bch_t* str; /**< option string  */
	hio_bopt_lng_t*  lng; /**< long options */

	/* output */
	hio_bci_t        opt; /**< character checked for validity */
	hio_bch_t*       arg; /**< argument associated with an option */

	/* output */
	const hio_bch_t* lngopt;

	/* input + output */
	int              ind; /**< index into parent argv vector */

	/* input + output - internal*/
	hio_bch_t*       cur;
};

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * The hio_getuopt() function processes the \a argc command-line arguments
 * pointed to by \a argv as configured in \a opt. It can process two
 * different option styles: a single character starting with '-', and a
 * long name starting with '--'.
 *
 * A character in \a opt.str is treated as a single character option. Should
 * it require a parameter, specify ':' after it.
 *
 * Two special returning option characters indicate special error conditions.
 * - \b ? indicates a bad option stored in the \a opt->opt field.
 * - \b : indicates a bad parameter for an option stored in the \a opt->opt field.
 *
 * @return an option character on success, #HIO_UCI_EOF on no more options.
 */
HIO_EXPORT hio_uci_t hio_getuopt (
	int                argc, /* argument count */
	hio_uch_t* const*  argv, /* argument array */
	hio_uopt_t*        opt   /* option configuration */
);

/**
 * The hio_getbopt() function is analogous to hio_getuopt() except that
 * it accepts character strings of the #hio_bch_t type.
 */
HIO_EXPORT hio_bci_t hio_getbopt (
	int                argc, /* argument count */
	hio_bch_t* const*  argv, /* argument array */
	hio_bopt_t*        opt   /* option configuration */
);


#if defined(HIO_OOCH_IS_UCH)
#	define hio_opt_t hio_uopt_t
#	define hio_opt_lng_t hio_uopt_lng_t
#	define hio_getopt(argc,argv,opt) hio_getuopt(argc,argv,opt)
#else
#	define hio_opt_t hio_bopt_t
#	define hio_opt_lng_t hio_bopt_lng_t
#	define hio_getopt(argc,argv,opt) hio_getbopt(argc,argv,opt)
#endif

#if defined(__cplusplus)
}
#endif

#endif
