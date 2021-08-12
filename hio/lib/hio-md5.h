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

#ifndef _HIO_MD5_H_
#define _HIO_MD5_H_

#include <hio.h>

#define HIO_MD5_DIGEST_LEN (16)
#define HIO_MD5_BLOCK_LEN  (64)

struct hio_md5_t
{
	hio_uint32_t  count[2];
	hio_uint32_t  state[4];
	hio_uint8_t   buffer[HIO_MD5_BLOCK_LEN];
};
typedef struct hio_md5_t hio_md5_t;

#ifdef __cplusplus
extern "C" {
#endif

HIO_EXPORT void hio_md5_initialize (
	hio_md5_t* md5
);

HIO_EXPORT void hio_md5_update (
	hio_md5_t*   md5,
	const void*  data,
	hio_uint32_t len
);

HIO_EXPORT void hio_md5_updatex (
	hio_md5_t*   md5,
	const void*  data,
	hio_oow_t    len
);

HIO_EXPORT hio_oow_t hio_md5_digest (
	hio_md5_t* md5,
	void*      digest,
	hio_oow_t  size
);

#ifdef __cplusplus
}
#endif

#endif
