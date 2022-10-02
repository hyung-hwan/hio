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

#ifndef _HIO_HTRD_H_
#define _HIO_HTRD_H_

#include <hio-http.h>
#include <hio-htre.h>

typedef struct hio_htrd_t hio_htrd_t;

enum hio_htrd_errnum_t
{
	HIO_HTRD_ENOERR,
	HIO_HTRD_EOTHER,
	HIO_HTRD_ENOIMPL,
	HIO_HTRD_ESYSERR,
	HIO_HTRD_EINTERN,

	HIO_HTRD_ENOMEM,
	HIO_HTRD_EBADRE,
	HIO_HTRD_EBADHDR,
	HIO_HTRD_ESUSPENDED
};

typedef enum hio_htrd_errnum_t hio_htrd_errnum_t;

/**
 * The hio_htrd_option_t type defines various options to
 * change the behavior of the hio_htrd_t reader.
 */
enum hio_htrd_option_t
{
	HIO_HTRD_SKIP_EMPTY_LINES  = ((hio_bitmask_t)1 << 0), /**< skip leading empty lines before the initial line */
	HIO_HTRD_SKIP_INITIAL_LINE = ((hio_bitmask_t)1 << 1), /**< skip processing an initial line */
	HIO_HTRD_CANONQPATH        = ((hio_bitmask_t)1 << 2), /**< canonicalize the query path */
	HIO_HTRD_REQUEST           = ((hio_bitmask_t)1 << 3), /**< parse input as a request */
	HIO_HTRD_RESPONSE          = ((hio_bitmask_t)1 << 4), /**< parse input as a response */
	HIO_HTRD_TRAILERS          = ((hio_bitmask_t)1 << 5), /**< store trailers in a separate table */
	HIO_HTRD_STRICT            = ((hio_bitmask_t)1 << 6)  /**< be more picky */
};

typedef enum hio_htrd_option_t hio_htrd_option_t;

typedef struct hio_htrd_recbs_t hio_htrd_recbs_t;

struct hio_htrd_recbs_t
{
	int  (*peek) (hio_htrd_t* htrd, hio_htre_t* re);
	int  (*poke) (hio_htrd_t* htrd, hio_htre_t* re);
	int  (*push_content) (hio_htrd_t* htrd, hio_htre_t* re, const hio_bch_t* data, hio_oow_t len);
};

struct hio_htrd_t
{
	hio_t* hio;
	hio_htrd_errnum_t errnum;
	hio_bitmask_t option;
	int flags;

	hio_htrd_recbs_t recbs;

	struct
	{
		struct
		{
			int flags;

			int crlf; /* crlf status */
			hio_oow_t plen; /* raw request length excluding crlf */
			hio_oow_t need; /* number of octets needed for contents */

			struct
			{
				hio_oow_t len;
				hio_oow_t count;
				int       phase;
			} chunk;
		} s; /* state */

		/* buffers needed for processing a request */
		struct
		{
			hio_becs_t raw; /* buffer to hold raw octets */
			hio_becs_t tra; /* buffer for handling trailers */
		} b; 
	} fed; 

	hio_htre_t re;
	int        clean;
};

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * The hio_htrd_open() function creates a htrd processor.
 */
HIO_EXPORT hio_htrd_t* hio_htrd_open (
	hio_t*      hio,   /**< memory manager */
	hio_oow_t  xtnsize /**< extension size in bytes */
);

/**
 * The hio_htrd_close() function destroys a htrd processor.
 */
HIO_EXPORT void hio_htrd_close (
	hio_htrd_t* htrd 
);

HIO_EXPORT int hio_htrd_init (
	hio_htrd_t* htrd,
	hio_t*      hio
);

HIO_EXPORT void hio_htrd_fini (
	hio_htrd_t* htrd
);

#if defined(HIO_HAVE_INLINE)
static HIO_INLINE void* hio_htrd_getxtn (hio_htrd_t* htrd) { return (void*)(htrd + 1); }
#else
#define hio_htrd_getxtn(htrd) ((void*)((hio_htrd_t*)(htrd) + 1))
#endif

HIO_EXPORT hio_htrd_errnum_t hio_htrd_geterrnum (
	hio_htrd_t* htrd
);

HIO_EXPORT void hio_htrd_clear (
	hio_htrd_t* htrd
);

HIO_EXPORT hio_bitmask_t hio_htrd_getoption (
	hio_htrd_t* htrd
);

HIO_EXPORT void hio_htrd_setoption (
	hio_htrd_t*   htrd,
	hio_bitmask_t mask
);

HIO_EXPORT const hio_htrd_recbs_t* hio_htrd_getrecbs (
	hio_htrd_t* htrd
);

HIO_EXPORT void hio_htrd_setrecbs (
	hio_htrd_t*             htrd,
	const hio_htrd_recbs_t* recbs
);

/**
 * The hio_htrd_feed() function accepts htrd request octets and invokes a 
 * callback function if it has processed a proper htrd request. 
 */
HIO_EXPORT int hio_htrd_feed (
	hio_htrd_t*        htrd, /**< htrd */
	const hio_bch_t*   req,  /**< request octets */
	hio_oow_t          len,   /**< number of octets */
	hio_oow_t*         rem
);

/**
 * The hio_htrd_halt() function indicates the end of feeeding
 * if the current response should be processed until the 
 * connection is closed.
 */ 
HIO_EXPORT int hio_htrd_halt (
	hio_htrd_t* htrd
);

HIO_EXPORT void hio_htrd_suspend (
	hio_htrd_t* htrd
);

HIO_EXPORT void hio_htrd_resume (
	hio_htrd_t* htrd
);

HIO_EXPORT void  hio_htrd_dummify (
	hio_htrd_t* htrd
);

HIO_EXPORT void hio_htrd_undummify (
	hio_htrd_t* htrd
);

#if defined(__cplusplus)
}
#endif

#endif
