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

#ifndef _MIO_PATH_H_
#define _MIO_PATH_H_

#include <mio.h>
#include <mio-utl.h>

enum mio_canon_path_flag_t
{
	/** if the final output is . logically, return an empty path */
	MIO_CANON_PATH_EMPTY_SINGLE_DOT    = (1 << 0),

	/** keep the .. segment in the path name */
	MIO_CANON_PATH_KEEP_DOUBLE_DOTS    = (1 << 1),

	/** drop a trailing separator even if the source contains one */
	MIO_CANON_PATH_DROP_TRAILING_SEP   = (1 << 2)
};

typedef enum mio_canon_path_flag_t mio_canon_path_flag_t;

#define MIO_CANON_OOCSTR_PATH_EMPTY_SINGLE_DOT MIO_CANON_PATH_EMPTY_SINGLE_DOT
#define MIO_CANON_OOCSTR_PATH_KEEP_DOUBLE_DOTS MIO_CANON_PATH_KEEP_DOUBLE_DOTS
#define MIO_CANON_OOCSTR_PATH_DROP_TRAILING_SEP MIO_CANON_PATH_DROP_TRAILING_SEP

#define MIO_CANON_UCSTR_PATH_EMPTY_SINGLE_DOT MIO_CANON_PATH_EMPTY_SINGLE_DOT
#define MIO_CANON_UCSTR_PATH_KEEP_DOUBLE_DOTS MIO_CANON_PATH_KEEP_DOUBLE_DOTS
#define MIO_CANON_UCSTR_PATH_DROP_TRAILING_SEP MIO_CANON_PATH_DROP_TRAILING_SEP

#define MIO_CANON_BCSTR_PATH_EMPTY_SINGLE_DOT MIO_CANON_PATH_EMPTY_SINGLE_DOT
#define MIO_CANON_BCSTR_PATH_KEEP_DOUBLE_DOTS MIO_CANON_PATH_KEEP_DOUBLE_DOTS
#define MIO_CANON_BCSTR_PATH_DROP_TRAILING_SEP MIO_CANON_PATH_DROP_TRAILING_SEP


#if defined(_WIN32) || defined(__OS2__) || defined(__DOS__)

#	define MIO_IS_PATH_SEP(c) ((c) == '/' || (c) == '\\')

#else
#	define MIO_IS_PATH_SEP(c) ((c) == '/')
#endif

#define MIO_IS_PATH_DRIVE(s) \
	(((s[0] >= 'A' && s[0] <= 'Z') || \
	  (s[0] >= 'a' && s[0] <= 'z')) && \
	 s[1] == ':')

#define MIO_IS_PATH_SEP_OR_NIL(c) (MIO_IS_PATH_SEP(c) || (c) == '\0')


#if defined(__cplusplus)
extern "C" {
#endif

MIO_EXPORT mio_oow_t mio_canon_ucstr_path (
	const mio_uch_t* path,
	mio_uch_t*       canon,
	int              flags
);

MIO_EXPORT mio_oow_t mio_canon_bcstr_path (
	const mio_bch_t* path,
	mio_bch_t*       canon,
	int              flags
);

#if defined(__cplusplus)
}
#endif

#endif
