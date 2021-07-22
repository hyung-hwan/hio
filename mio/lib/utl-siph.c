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
    IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WAfRRANTIES
    OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
    IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
    INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
    NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
    THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <mio-utl.h>

/* 
 * This code is based on https://github.com/emboss/siphash-c/blob/master/src/siphash.c
 *
 * See https://131002.net/siphash/siphash24.c for a reference implementation by the inventor.
 */

#define U8TO32_LE(p) (((mio_uint32_t)((p)[0])) | ((mio_uint32_t)((p)[1]) <<  8) | ((mio_uint32_t)((p)[2]) <<  16) | ((mio_uint32_t)((p)[3]) << 24))

#define U32TO8_LE(p, v) do { \
	(p)[0] = (mio_uint8_t)((v)); \
	(p)[1] = (mio_uint8_t)((v) >>  8); \
	(p)[2] = (mio_uint8_t)((v) >> 16); \
	(p)[3] = (mio_uint8_t)((v) >> 24); \
} while (0)


#if (MIO_SIZEOF_UINT64_T > 0)
typedef mio_uint64_t sip_uint64_t;

#define U8TO64_LE(p) ((mio_uint64_t)U8TO32_LE(p) | ((mio_uint64_t)U8TO32_LE((p) + 4)) << 32 )

#define U64TO8_LE(p, v) do { \
	U32TO8_LE((p), (mio_uint32_t)((v))); \
	U32TO8_LE((p) + 4, (mio_uint32_t)((v) >> 32)); \
} while (0)

#define ROTATE_LEFT_64(v, s) ((v) << (s)) | ((v) >> (64 - (s)))
#define ROTATE_LEFT_64_TO(v, s) ((v) = ROTATE_LEFT_64((v), (s)))

#define ADD64_TO(v, s) ((v) += (s))
#define XOR64_TO(v, s) ((v) ^= (s))
#define XOR64_INT(v, x) ((v) ^= (x))

#else /* (MIO_SIZEOF_UINT64_T > 0) */

struct sip_uint64_t
{
	mio_uint32_t _u32[2];
}; 
typedef struct sip_uint64_t sip_uint64_t;

#if defined(MIO_ENDIAN_LITTLE)
#	define lo _u32[0]
#	define hi _u32[1]
#elif defined(MIO_ENDIAN_BIG)
#	define hi _u32[0]
#	define lo _u32[1]
#else
#	error UNKNOWN ENDIAN
#endif


#define U8TO64_LE(p) u8to64_le(p)
static MIO_INLINE sip_uint64_t u8to64_le (const mio_uint8_t* p)
{
	sip_uint64_t ret;
	ret.lo = U8TO32_LE(p);
	ret.hi = U8TO32_LE(p + 4);
	return ret;
}

#define U64TO8_LE(p, v) u64to8_le(p, v)
static MIO_INLINE void u64to8_le (mio_uint8_t* p, sip_uint64_t v)
{
	U32TO8_LE (p, v.lo);
	U32TO8_LE (p + 4, v.hi);
}

#define ROTATE_LEFT_64_TO(v, s) \
	((s) > 32? rotl64_swap(rotl64_to(&(v), (s) - 32)) : \
	 (s) == 32? rotl64_swap(&(v)): rotl64_to(&(v), (s)))
static MIO_INLINE sip_uint64_t* rotl64_to (sip_uint64_t* v, unsigned int s)
{
	mio_uint32_t uhi = (v->hi << s) | (v->lo >> (32 - s));
	mio_uint32_t ulo = (v->lo << s) | (v->hi >> (32 - s));
	v->hi = uhi;
	v->lo = ulo;
	return v;
}

static MIO_INLINE sip_uint64_t* rotl64_swap (sip_uint64_t *v)
{
	mio_uint32_t t = v->lo;
	v->lo = v->hi;
	v->hi = t;
	return v;
}

#define ADD64_TO(v, s) add64_to(&(v), (s))
static MIO_INLINE sip_uint64_t* add64_to (sip_uint64_t* v, sip_uint64_t s)
{
	v->lo += s.lo;
	v->hi += s.hi;
	if (v->lo < s.lo) v->hi++;
	return v;
}

#define XOR64_TO(v, s) xor64_to(&(v), (s))
static MIO_INLINE sip_uint64_t* xor64_to (sip_uint64_t* v, sip_uint64_t s)
{
	v->lo ^= s.lo;
	v->hi ^= s.hi;
	return v;
}

#define XOR64_INT(v, x) ((v).lo ^= (x))

#endif /* (MIO_SIZEOF_UINT64_T > 0) */


static const mio_uint8_t sip_init_v_bin[] = 
{
	0x75, 0x65, 0x73, 0x70, 0x65, 0x6d, 0x6f, 0x73, 
	0x6d, 0x6f, 0x64, 0x6e, 0x61, 0x72, 0x6f, 0x64,
	0x61, 0x72, 0x65, 0x6e, 0x65, 0x67, 0x79, 0x6c,
	0x73, 0x65, 0x74, 0x79, 0x62, 0x64, 0x65, 0x74
};
#define sip_init_v (*(sip_uint64_t(*)[4])sip_init_v_bin)

#define SIP_COMPRESS(v0, v1, v2, v3) do {\
	ADD64_TO((v0), (v1)); \
	ADD64_TO((v2), (v3)); \
	ROTATE_LEFT_64_TO((v1), 13); \
	ROTATE_LEFT_64_TO((v3), 16); \
	XOR64_TO((v1), (v0)); \
	XOR64_TO((v3), (v2)); \
	ROTATE_LEFT_64_TO((v0), 32); \
	ADD64_TO((v2), (v1)); \
	ADD64_TO((v0), (v3)); \
	ROTATE_LEFT_64_TO((v1), 17); \
	ROTATE_LEFT_64_TO((v3), 21); \
	XOR64_TO((v1), (v2)); \
	XOR64_TO((v3), (v0)); \
	ROTATE_LEFT_64_TO((v2), 32); \
} while(0)

#define SIP_2_ROUND(m, v0, v1, v2, v3) do { \
	XOR64_TO((v3), (m)); \
	SIP_COMPRESS(v0, v1, v2, v3); \
	SIP_COMPRESS(v0, v1, v2, v3); \
	XOR64_TO((v0), (m)); \
} while (0)

void mio_sip_hash_24 (const mio_uint8_t key[16], const void* dptr, mio_oow_t dlen, mio_uint8_t out[8])
{
	sip_uint64_t k0, k1;
	sip_uint64_t v0, v1, v2, v3;
	sip_uint64_t m, b;
	mio_oow_t rem;
	const mio_uint8_t* ptr, * end;

	rem = dlen & 7; /* dlen % 8 */
	ptr = (const mio_uint8_t*)dptr;
	end = ptr + dlen - rem;

	k0 = U8TO64_LE(key);
	k1 = U8TO64_LE(key + 8);

	v0 = k0; XOR64_TO(v0, sip_init_v[0]);
	v1 = k1; XOR64_TO(v1, sip_init_v[1]);
	v2 = k0; XOR64_TO(v2, sip_init_v[2]);
	v3 = k1; XOR64_TO(v3, sip_init_v[3]);

	for (; ptr != end; ptr += 8) 
	{
		m = U8TO64_LE(ptr);
		SIP_2_ROUND (m, v0, v1, v2, v3);
	}

#if (MIO_SIZEOF_UINT64_T > 0)
	b = (mio_uint64_t)dlen << 56;

#define OR_BYTE_HI(n) (b |= ((mio_uint64_t)end[n]) << ((n) * 8))
#define OR_BYTE_LO(n) (b |= ((mio_uint64_t)end[n]) << ((n) * 8))

#else
	b.hi = (mio_uint32_t)dlen << 24;
	b.lo = 0;

#define OR_BYTE_HI(n) (b.hi |= ((mio_uint32_t)end[n]) << ((n) * 8 - 32)) /* n: 4 to 7 */
#define OR_BYTE_LO(n) (b.lo |= ((mio_uint32_t)end[n]) << ((n) * 8)) /* n: 0 to 3 */

#endif

	switch (rem)
	{
		case 7: OR_BYTE_HI (6);
		case 6: OR_BYTE_HI (5);
		case 5: OR_BYTE_HI (4);
		case 4: OR_BYTE_LO (3);
		case 3: OR_BYTE_LO (2);
		case 2: OR_BYTE_LO (1);
		case 1: OR_BYTE_LO (0); break;
		case 0: break;
	}

	SIP_2_ROUND (b, v0, v1, v2, v3);

	XOR64_INT (v2, 0xff);

	SIP_COMPRESS (v0, v1, v2, v3);
	SIP_COMPRESS (v0, v1, v2, v3);
	SIP_COMPRESS (v0, v1, v2, v3);
	SIP_COMPRESS (v0, v1, v2, v3);

	XOR64_TO (v0, v1);
	XOR64_TO (v0, v2);
	XOR64_TO (v0, v3);

	U64TO8_LE (out, v0);
}
