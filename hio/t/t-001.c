/* test endian conversion macros */

#include <mio-utl.h>
#include <stdio.h>
#include "t.h"

int main ()
{
	{
		union {
			mio_uint16_t u16;
			mio_uint8_t arr[2];
		} x;

		x.arr[0] = 0x11;
		x.arr[1] = 0x22;

		printf("x.u16 = 0x%04x\n", x.u16);
		printf("htole16(x.u16) = 0x%04x\n", mio_htole16(x.u16));
		printf("htobe16(x.u16) = 0x%04x\n", mio_htobe16(x.u16));

		T_ASSERT1 (x.u16 != mio_htole16(x.u16) || x.u16 != mio_htobe16(x.u16), "u16 endian conversion #0");
		T_ASSERT1 (x.u16 == mio_le16toh(mio_htole16(x.u16)), "u16 endian conversion #1");
		T_ASSERT1 (x.u16 == mio_be16toh(mio_htobe16(x.u16)), "u16 endian conversion #2");
		T_ASSERT1 (x.u16 == mio_ntoh16(mio_hton16(x.u16)), "u16 endian conversion #3");

		#define X_CONST (0x1122)
		T_ASSERT1 (X_CONST != MIO_CONST_HTOLE16(X_CONST) || X_CONST != MIO_CONST_HTOBE16(X_CONST), "u16 constant endian conversion #0");
		T_ASSERT1 (X_CONST == MIO_CONST_LE16TOH(MIO_CONST_HTOLE16(X_CONST)), "u16 constant endian conversion #1");
		T_ASSERT1 (X_CONST == MIO_CONST_BE16TOH(MIO_CONST_HTOBE16(X_CONST)), "u16 constant endian conversion #2");
		T_ASSERT1 (X_CONST == MIO_CONST_NTOH16(MIO_CONST_HTON16(X_CONST)), "u16 constant endian conversion #3");
		#undef X_CONST
	}


	{
		union {
			mio_uint32_t u32;
			mio_uint8_t arr[4];
		} x;

		x.arr[0] = 0x11;
		x.arr[1] = 0x22;
		x.arr[2] = 0x33;
		x.arr[3] = 0x44;

		printf("x.u32 = 0x%08x\n", (unsigned int)x.u32);
		printf("htole32(x.u32) = 0x%08x\n", (unsigned int)mio_htole32(x.u32));
		printf("htobe32(x.u32) = 0x%08x\n", (unsigned int)mio_htobe32(x.u32));

		T_ASSERT1 (x.u32 != mio_htole32(x.u32) || x.u32 != mio_htobe32(x.u32), "u32 endian conversion #0");
		T_ASSERT1 (x.u32 == mio_le32toh(mio_htole32(x.u32)), "u32 endian conversion #1");
		T_ASSERT1 (x.u32 == mio_be32toh(mio_htobe32(x.u32)), "u32 endian conversion #2");
		T_ASSERT1 (x.u32 == mio_ntoh32(mio_hton32(x.u32)), "u32 endian conversion #3");

		#define X_CONST (0x11223344)
		T_ASSERT1 (X_CONST != MIO_CONST_HTOLE32(X_CONST) || X_CONST != MIO_CONST_HTOBE32(X_CONST), "u32 constant endian conversion #0");
		T_ASSERT1 (X_CONST == MIO_CONST_LE32TOH(MIO_CONST_HTOLE32(X_CONST)), "u32 constant endian conversion #1");
		T_ASSERT1 (X_CONST == MIO_CONST_BE32TOH(MIO_CONST_HTOBE32(X_CONST)), "u32 constant endian conversion #2");
		T_ASSERT1 (X_CONST == MIO_CONST_NTOH32(MIO_CONST_HTON32(X_CONST)), "u32 constant endian conversion #3");
		#undef X_CONST
	}

#if defined(MIO_HAVE_UINT64_T)
	{
		union {
			mio_uint64_t u64;
			mio_uint8_t arr[8];
		} x;

		x.arr[0] = 0x11;
		x.arr[1] = 0x22;
		x.arr[2] = 0x33;
		x.arr[3] = 0x44;
		x.arr[4] = 0x55;
		x.arr[5] = 0x66;
		x.arr[6] = 0x77;
		x.arr[7] = 0x88;

		printf("x.u64 = 0x%016llx\n", (unsigned long long)x.u64);
		printf("htole64(x.u64) = 0x%016llx\n", (unsigned long long)mio_htole64(x.u64));
		printf("htobe64(x.u64) = 0x%016llx\n", (unsigned long long)mio_htobe64(x.u64));

		T_ASSERT1 (x.u64 != mio_htole64(x.u64) || x.u64 != mio_htobe64(x.u64), "u64 endian conversion #0");
		T_ASSERT1 (x.u64 == mio_le64toh(mio_htole64(x.u64)), "u64 endian conversion #1");
		T_ASSERT1 (x.u64 == mio_be64toh(mio_htobe64(x.u64)), "u64 endian conversion #2");
		T_ASSERT1 (x.u64 == mio_ntoh64(mio_hton64(x.u64)), "u64 endian conversion #3");

		#define X_CONST (((mio_uint64_t)0x11223344 << 32) | (mio_uint64_t)0x55667788)
		T_ASSERT1 (X_CONST != MIO_CONST_HTOLE64(X_CONST) || X_CONST != MIO_CONST_HTOBE64(X_CONST), "u64 constant endian conversion #0");
		T_ASSERT1 (X_CONST == MIO_CONST_LE64TOH(MIO_CONST_HTOLE64(X_CONST)), "u64 constant endian conversion #1");
		T_ASSERT1 (X_CONST == MIO_CONST_BE64TOH(MIO_CONST_HTOBE64(X_CONST)), "u64 constant endian conversion #2");
		T_ASSERT1 (X_CONST == MIO_CONST_NTOH64(MIO_CONST_HTON64(X_CONST)), "u64 constant endian conversion #3");
		#undef X_CONST
	}
#endif

#if defined(MIO_HAVE_UINT128_T)
	{
		union {
			mio_uint128_t u128;
			mio_uint8_t arr[16];
		} x;
		mio_uint128_t tmp;

		x.arr[0] = 0x11;
		x.arr[1] = 0x22;
		x.arr[2] = 0x33;
		x.arr[3] = 0x44;
		x.arr[4] = 0x55;
		x.arr[5] = 0x66;
		x.arr[6] = 0x77;
		x.arr[7] = 0x88;
		x.arr[8] = 0x99;
		x.arr[9] = 0xaa;
		x.arr[10] = 0xbb;
		x.arr[11] = 0xcc;
		x.arr[12] = 0xdd;
		x.arr[13] = 0xee;
		x.arr[14] = 0xff;
		x.arr[15] = 0xfa;

		printf("x.u128 = 0x%016llx%016llx\n", (unsigned long long)(mio_uint64_t)(x.u128 >> 64), (unsigned long long)(mio_uint64_t)(x.u128 >> 0));

		tmp = mio_htole128(x.u128);
		printf("htole128(tmp) = 0x%016llx%016llx\n", (unsigned long long)(mio_uint64_t)(tmp >> 64), (unsigned long long)(mio_uint64_t)(tmp >> 0));

		tmp = mio_htobe128(x.u128);
		printf("htobe128(tmp) = 0x%016llx%016llx\n", (unsigned long long)(mio_uint64_t)(tmp >> 64), (unsigned long long)(mio_uint64_t)(tmp >> 0));

		T_ASSERT1 (x.u128 != mio_htole128(x.u128) || x.u128 != mio_htobe128(x.u128), "u128 endian conversion #0");
		T_ASSERT1 (x.u128 == mio_le128toh(mio_htole128(x.u128)), "u128 endian conversion #1");
		T_ASSERT1 (x.u128 == mio_be128toh(mio_htobe128(x.u128)), "u128 endian conversion #2");
		T_ASSERT1 (x.u128 == mio_ntoh128(mio_hton128(x.u128)), "u128 endian conversion #3");

		#define X_CONST (((mio_uint128_t)0x11223344 << 96) | ((mio_uint128_t)0x55667788 << 64) | ((mio_uint128_t)0x99aabbcc << 32)  | ((mio_uint128_t)0xddeefffa))
		T_ASSERT1 (X_CONST != MIO_CONST_HTOLE128(X_CONST) || X_CONST != MIO_CONST_HTOBE128(X_CONST), "u128 constant endian conversion #0");
		T_ASSERT1 (X_CONST == MIO_CONST_LE128TOH(MIO_CONST_HTOLE128(X_CONST)), "u128 constant endian conversion #1");
		T_ASSERT1 (X_CONST == MIO_CONST_BE128TOH(MIO_CONST_HTOBE128(X_CONST)), "u128 constant endian conversion #2");
		T_ASSERT1 (X_CONST == MIO_CONST_NTOH128(MIO_CONST_HTON128(X_CONST)), "u128 constant endian conversion #3");
		#undef X_CONST
	}
#endif

	return 0;

oops:
	return -1;
}
