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

#ifndef _MIO_SKAD_H_
#define _MIO_SKAD_H_

#include <mio.h>


#define MIO_SIZEOF_SKAD_T 1
#if (MIO_SIZEOF_STRUCT_SOCKADDR_IN > MIO_SIZEOF_SKAD_T)
#	undef MIO_SIZEOF_SKAD_T
#	define MIO_SIZEOF_SKAD_T MIO_SIZEOF_STRUCT_SOCKADDR_IN
#endif
#if (MIO_SIZEOF_STRUCT_SOCKADDR_IN6 > MIO_SIZEOF_SKAD_T)
#	undef MIO_SIZEOF_SKAD_T
#	define MIO_SIZEOF_SKAD_T MIO_SIZEOF_STRUCT_SOCKADDR_IN6
#endif
#if (MIO_SIZEOF_STRUCT_SOCKADDR_LL > MIO_SIZEOF_SKAD_T)
#	undef MIO_SIZEOF_SKAD_T
#	define MIO_SIZEOF_SKAD_T MIO_SIZEOF_STRUCT_SOCKADDR_LL
#endif
#if (MIO_SIZEOF_STRUCT_SOCKADDR_UN > MIO_SIZEOF_SKAD_T)
#	undef MIO_SIZEOF_SKAD_T
#	define MIO_SIZEOF_SKAD_T MIO_SIZEOF_STRUCT_SOCKADDR_UN
#endif

struct mio_skad_t
{
	mio_uint8_t data[MIO_SIZEOF_SKAD_T];
};
typedef struct mio_skad_t mio_skad_t;


#define MIO_SKAD_TO_OOCSTR_ADDR (1 << 0)
#define MIO_SKAD_TO_OOCSTR_PORT (1 << 0)
#define MIO_SKAD_TO_UCSTR_ADDR MIO_SKAD_TO_OOCSTR_ADDR
#define MIO_SKAD_TO_UCSTR_PORT MIO_SKAD_TO_OOCSTR_PORT
#define MIO_SKAD_TO_BCSTR_ADDR MIO_SKAD_TO_OOCSTR_ADDR
#define MIO_SKAD_TO_BCSTR_PORT MIO_SKAD_TO_OOCSTR_PORT


#if defined(__cplusplus)
extern "C" {
#endif

MIO_EXPORT int mio_skad_family (
	const mio_skad_t* skad
);

MIO_EXPORT int mio_skad_size (
	const mio_skad_t* skad
);

MIO_EXPORT void mio_clear_skad (
	mio_skad_t* skad
);

#if defined(__cplusplus)
}
#endif

#endif
