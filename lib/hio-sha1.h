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


#ifndef _HIO_SHA1_H_
#define _HIO_SHA1_H_

#include <hio.h>

#define HIO_SHA1_DIGEST_LEN (20)
#define HIO_SHA1_BLOCK_LEN  (64)

struct hio_sha1_ctx_t
{
	hio_uint8_t  data[HIO_SHA1_BLOCK_LEN];
	hio_uint8_t  datalen;

	/* the padding encodes the message length in bits as a 64-bit quantity.
	 * it is held as two 32-bit halves so that nothing here depends on a
	 * 64-bit integer type being available. */
	hio_uint32_t bitlen_lo;
	hio_uint32_t bitlen_hi;

	hio_uint32_t state[5];
};
typedef struct hio_sha1_ctx_t hio_sha1_ctx_t;

#ifdef __cplusplus
extern "C" {
#endif

HIO_EXPORT void hio_sha1_init (
	hio_sha1_ctx_t* ctx
);

HIO_EXPORT void hio_sha1_update (
	hio_sha1_ctx_t*  ctx,
	const void*      data,
	hio_oow_t        len
);

/**
 * The hio_sha1_final() function writes the digest of everything fed to
 * hio_sha1_update() so far. The context is spent once this returns; feed a
 * fresh one through hio_sha1_init() to hash anything else.
 */
HIO_EXPORT void hio_sha1_final (
	hio_sha1_ctx_t* ctx,
	hio_uint8_t     hash[HIO_SHA1_DIGEST_LEN]
);

/**
 * The hio_sha1_digest() function hashes one buffer that is already whole,
 * which is the init/update/final sequence with nothing in between.
 */
HIO_EXPORT void hio_sha1_digest (
	hio_uint8_t     hash[HIO_SHA1_DIGEST_LEN],
	const void*     data,
	hio_oow_t       dlen
);

#ifdef __cplusplus
}
#endif

#endif
