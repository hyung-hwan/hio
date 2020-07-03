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


static const char sip_init_state_bin[] = "uespemos""modnarod""arenegyl""setybdet";
#define sip_init_state (*(sip_uint64_t(*)[4])sip_init_state_bin)

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

void mio_sip_hash_24 (const mio_uint8_t key[16], mio_uint8_t *dptr, mio_oow_t dlen, mio_uint8_t out[8])
{
	sip_uint64_t k0, k1;
	sip_uint64_t v0, v1, v2, v3;
	sip_uint64_t m, last;

	mio_oow_t rem;
	mio_uint8_t* end;

	rem = dlen & 7; /* dlen % 8 */
	end = dptr + dlen - rem;

	k0 = U8TO64_LE(key);
	k1 = U8TO64_LE(key + 8);

	v0 = k0; XOR64_TO(v0, sip_init_state[0]);
	v1 = k1; XOR64_TO(v1, sip_init_state[1]);
	v2 = k0; XOR64_TO(v2, sip_init_state[2]);
	v3 = k1; XOR64_TO(v3, sip_init_state[3]);

	for (; dptr != end; dptr += 8) 
	{
		m = U8TO64_LE(dptr);
		SIP_2_ROUND (m, v0, v1, v2, v3);
	}

#if (MIO_SIZEOF_UINT64_T > 0)
	last = dlen << 56;

#define OR_BYTE(n) (last |= ((mio_uint64_t)end[n]) << ((n) * 8))

#else
	last.hi = dlen << 24;
	last.lo = 0;
#define OR_BYTE(n) do { \
	if (n >= 4) last.hi |= ((mio_uint32_t)end[n]) << ((n) >= 4 ? (n) * 8 - 32 : 0); \
	else last.lo |= ((mio_uint32_t)end[n]) << ((n) >= 4 ? 0 : (n) * 8); \
} while (0)
#endif

	switch (rem)
	{
		case 7: OR_BYTE (6);
		case 6: OR_BYTE (5);
		case 5: OR_BYTE (4);
		case 4: OR_BYTE (3);
		case 3: OR_BYTE (2);
		case 2: OR_BYTE (1);
		case 1: OR_BYTE (0);
		case 0: break;
	}

	SIP_2_ROUND (last, v0, v1, v2, v3);

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
