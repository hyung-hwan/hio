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

#ifndef _MIO_HTRD_H_
#define _MIO_HTRD_H_

#include <mio-http.h>
#include <mio-htre.h>

typedef struct mio_htrd_t mio_htrd_t;

enum mio_htrd_errnum_t
{
	MIO_HTRD_ENOERR,
	MIO_HTRD_EOTHER,
	MIO_HTRD_ENOIMPL,
	MIO_HTRD_ESYSERR,
	MIO_HTRD_EINTERN,

	MIO_HTRD_ENOMEM,
	MIO_HTRD_EBADRE,
	MIO_HTRD_EBADHDR,
	MIO_HTRD_ESUSPENDED
};

typedef enum mio_htrd_errnum_t mio_htrd_errnum_t;

/**
 * The mio_htrd_option_t type defines various options to
 * change the behavior of the mio_htrd_t reader.
 */
enum mio_htrd_option_t
{
	MIO_HTRD_SKIPEMPTYLINES  = (1 << 0), /**< skip leading empty lines before the initial line */
	MIO_HTRD_SKIPINITIALLINE = (1 << 1), /**< skip processing an initial line */
	MIO_HTRD_CANONQPATH      = (1 << 2), /**< canonicalize the query path */
	MIO_HTRD_REQUEST         = (1 << 3), /**< parse input as a request */
	MIO_HTRD_RESPONSE        = (1 << 4), /**< parse input as a response */
	MIO_HTRD_TRAILERS        = (1 << 5), /**< store trailers in a separate table */
	MIO_HTRD_STRICT          = (1 << 6)  /**< be more picky */
};

typedef enum mio_htrd_option_t mio_htrd_option_t;

typedef struct mio_htrd_recbs_t mio_htrd_recbs_t;

struct mio_htrd_recbs_t
{
	int  (*peek) (mio_htrd_t* htrd, mio_htre_t* re);
	int  (*poke) (mio_htrd_t* htrd, mio_htre_t* re);
	int  (*push_content) (mio_htrd_t* htrd, mio_htre_t* re, const mio_bch_t* data, mio_oow_t len);
};

struct mio_htrd_t
{
	mio_t* mio;
	mio_htrd_errnum_t errnum;
	int option;
	int flags;

	mio_htrd_recbs_t recbs;

	struct
	{
		struct
		{
			int flags;

			int crlf; /* crlf status */
			mio_oow_t plen; /* raw request length excluding crlf */
			mio_oow_t need; /* number of octets needed for contents */

			struct
			{
				mio_oow_t len;
				mio_oow_t count;
				int       phase;
			} chunk;
		} s; /* state */

		/* buffers needed for processing a request */
		struct
		{
			mio_becs_t raw; /* buffer to hold raw octets */
			mio_becs_t tra; /* buffer for handling trailers */
		} b; 
	} fed; 

	mio_htre_t re;
	int        clean;
};

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * The mio_htrd_open() function creates a htrd processor.
 */
MIO_EXPORT mio_htrd_t* mio_htrd_open (
	mio_t*      mio,   /**< memory manager */
	mio_oow_t  xtnsize /**< extension size in bytes */
);

/**
 * The mio_htrd_close() function destroys a htrd processor.
 */
MIO_EXPORT void mio_htrd_close (
	mio_htrd_t* htrd 
);

MIO_EXPORT int mio_htrd_init (
	mio_htrd_t* htrd,
	mio_t*      mio
);

MIO_EXPORT void mio_htrd_fini (
	mio_htrd_t* htrd
);

#if defined(MIO_HAVE_INLINE)
static MIO_INLINE void* mio_htrd_getxtn (mio_htrd_t* htrd) { return (void*)(htrd + 1); }
#else
#define mio_htrd_getxtn(htrd) ((void*)((mio_htrd_t*)(htrd) + 1))
#endif

MIO_EXPORT mio_htrd_errnum_t mio_htrd_geterrnum (
	mio_htrd_t* htrd
);

MIO_EXPORT void mio_htrd_clear (
	mio_htrd_t* htrd
);

MIO_EXPORT int mio_htrd_getopt (
	mio_htrd_t* htrd
);

MIO_EXPORT void mio_htrd_setopt (
	mio_htrd_t* htrd,
	int         opts
);

MIO_EXPORT const mio_htrd_recbs_t* mio_htrd_getrecbs (
	mio_htrd_t* htrd
);

MIO_EXPORT void mio_htrd_setrecbs (
	mio_htrd_t*             htrd,
	const mio_htrd_recbs_t* recbs
);

/**
 * The mio_htrd_feed() function accepts htrd request octets and invokes a 
 * callback function if it has processed a proper htrd request. 
 */
MIO_EXPORT int mio_htrd_feed (
	mio_htrd_t*        htrd, /**< htrd */
	const mio_bch_t*   req,  /**< request octets */
	mio_oow_t          len,   /**< number of octets */
	mio_oow_t*         rem
);

/**
 * The mio_htrd_halt() function indicates the end of feeeding
 * if the current response should be processed until the 
 * connection is closed.
 */ 
MIO_EXPORT int mio_htrd_halt (
	mio_htrd_t* htrd
);

MIO_EXPORT void mio_htrd_suspend (
	mio_htrd_t* htrd
);

MIO_EXPORT void mio_htrd_resume (
	mio_htrd_t* htrd
);

MIO_EXPORT void  mio_htrd_dummify (
	mio_htrd_t* htrd
);

MIO_EXPORT void mio_htrd_undummify (
	mio_htrd_t* htrd
);

#if defined(__cplusplus)
}
#endif

#endif
