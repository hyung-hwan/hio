/*
 * $Id$
 *
    Copyright (c) 2015-2016 Chung, Hyung-Hwan. All rights reserved.

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

#include "mio-prv.h"


/* ========================================================================= */

#if defined(MIO_HAVE_UINT16_T)

mio_uint16_t mio_ntoh16 (mio_uint16_t x)
{
#if defined(MIO_ENDIAN_BIG)
	return x;
#elif defined(MIO_ENDIAN_LITTLE)
	mio_uint8_t* c = (mio_uint8_t*)&x;
	return (mio_uint16_t)(
		((mio_uint16_t)c[0] << 8) |
		((mio_uint16_t)c[1] << 0));
#else
#	error Unknown endian
#endif
}

mio_uint16_t mio_hton16 (mio_uint16_t x)
{
#if defined(MIO_ENDIAN_BIG)
	return x;
#elif defined(MIO_ENDIAN_LITTLE)
	mio_uint8_t* c = (mio_uint8_t*)&x;
	return (mio_uint16_t)(
		((mio_uint16_t)c[0] << 8) |
		((mio_uint16_t)c[1] << 0));
#else
#	error Unknown endian
#endif
}

#endif

/* ========================================================================= */

#if defined(MIO_HAVE_UINT32_T)

mio_uint32_t mio_ntoh32 (mio_uint32_t x)
{
#if defined(MIO_ENDIAN_BIG)
	return x;
#elif defined(MIO_ENDIAN_LITTLE)
	mio_uint8_t* c = (mio_uint8_t*)&x;
	return (mio_uint32_t)(
		((mio_uint32_t)c[0] << 24) |
		((mio_uint32_t)c[1] << 16) |
		((mio_uint32_t)c[2] << 8) | 
		((mio_uint32_t)c[3] << 0));
#else
#	error Unknown endian
#endif
}

mio_uint32_t mio_hton32 (mio_uint32_t x)
{
#if defined(MIO_ENDIAN_BIG)
	return x;
#elif defined(MIO_ENDIAN_LITTLE)
	mio_uint8_t* c = (mio_uint8_t*)&x;
	return (mio_uint32_t)(
		((mio_uint32_t)c[0] << 24) |
		((mio_uint32_t)c[1] << 16) |
		((mio_uint32_t)c[2] << 8) | 
		((mio_uint32_t)c[3] << 0));
#else
#	error Unknown endian
#endif
}
#endif

/* ========================================================================= */

#if defined(MIO_HAVE_UINT64_T)

mio_uint64_t mio_ntoh64 (mio_uint64_t x)
{
#if defined(MIO_ENDIAN_BIG)
	return x;
#elif defined(MIO_ENDIAN_LITTLE)
	mio_uint8_t* c = (mio_uint8_t*)&x;
	return (mio_uint64_t)(
		((mio_uint64_t)c[0] << 56) | 
		((mio_uint64_t)c[1] << 48) | 
		((mio_uint64_t)c[2] << 40) |
		((mio_uint64_t)c[3] << 32) |
		((mio_uint64_t)c[4] << 24) |
		((mio_uint64_t)c[5] << 16) |
		((mio_uint64_t)c[6] << 8)  |
		((mio_uint64_t)c[7] << 0));
#else
#	error Unknown endian
#endif
}

mio_uint64_t mio_hton64 (mio_uint64_t x)
{
#if defined(MIO_ENDIAN_BIG)
	return x;
#elif defined(MIO_ENDIAN_LITTLE)
	mio_uint8_t* c = (mio_uint8_t*)&x;
	return (mio_uint64_t)(
		((mio_uint64_t)c[0] << 56) | 
		((mio_uint64_t)c[1] << 48) | 
		((mio_uint64_t)c[2] << 40) |
		((mio_uint64_t)c[3] << 32) |
		((mio_uint64_t)c[4] << 24) |
		((mio_uint64_t)c[5] << 16) |
		((mio_uint64_t)c[6] << 8)  |
		((mio_uint64_t)c[7] << 0));
#else
#	error Unknown endian
#endif
}

#endif

/* ========================================================================= */

#if defined(MIO_HAVE_UINT128_T)

mio_uint128_t mio_ntoh128 (mio_uint128_t x)
{
#if defined(MIO_ENDIAN_BIG)
	return x;
#elif defined(MIO_ENDIAN_LITTLE)
	mio_uint8_t* c = (mio_uint8_t*)&x;
	return (mio_uint128_t)(
		((mio_uint128_t)c[0]  << 120) | 
		((mio_uint128_t)c[1]  << 112) | 
		((mio_uint128_t)c[2]  << 104) |
		((mio_uint128_t)c[3]  << 96) |
		((mio_uint128_t)c[4]  << 88) |
		((mio_uint128_t)c[5]  << 80) |
		((mio_uint128_t)c[6]  << 72) |
		((mio_uint128_t)c[7]  << 64) |
		((mio_uint128_t)c[8]  << 56) | 
		((mio_uint128_t)c[9]  << 48) | 
		((mio_uint128_t)c[10] << 40) |
		((mio_uint128_t)c[11] << 32) |
		((mio_uint128_t)c[12] << 24) |
		((mio_uint128_t)c[13] << 16) |
		((mio_uint128_t)c[14] << 8)  |
		((mio_uint128_t)c[15] << 0));
#else
#	error Unknown endian
#endif
}

mio_uint128_t mio_hton128 (mio_uint128_t x)
{
#if defined(MIO_ENDIAN_BIG)
	return x;
#elif defined(MIO_ENDIAN_LITTLE)
	mio_uint8_t* c = (mio_uint8_t*)&x;
	return (mio_uint128_t)(
		((mio_uint128_t)c[0]  << 120) | 
		((mio_uint128_t)c[1]  << 112) | 
		((mio_uint128_t)c[2]  << 104) |
		((mio_uint128_t)c[3]  << 96) |
		((mio_uint128_t)c[4]  << 88) |
		((mio_uint128_t)c[5]  << 80) |
		((mio_uint128_t)c[6]  << 72) |
		((mio_uint128_t)c[7]  << 64) |
		((mio_uint128_t)c[8]  << 56) | 
		((mio_uint128_t)c[9]  << 48) | 
		((mio_uint128_t)c[10] << 40) |
		((mio_uint128_t)c[11] << 32) |
		((mio_uint128_t)c[12] << 24) |
		((mio_uint128_t)c[13] << 16) |
		((mio_uint128_t)c[14] << 8)  |
		((mio_uint128_t)c[15] << 0));
#else
#	error Unknown endian
#endif
}

#endif

/* ========================================================================= */

#define IS_MSPACE(x) ((x) == MIO_MT(' ') || (x) == MIO_MT('\t') || (x) == MIO_MT('\n') || (x) == MIO_MT('\r'))

mio_mchar_t* mio_mbsdup (mio_t* mio, const mio_mchar_t* src)
{
	mio_mchar_t* dst;
	mio_size_t len;

	dst = (mio_mchar_t*)src;
	while (*dst != MIO_MT('\0')) dst++;
	len = dst - src;

	dst = MIO_MMGR_ALLOC (mio->mmgr, (len + 1) * MIO_SIZEOF(*src));
	if (!dst)
	{
		mio->errnum = MIO_ENOMEM;
		return MIO_NULL;
	}

	MIO_MEMCPY (dst, src, (len + 1) * MIO_SIZEOF(*src));
	return dst;
}

mio_size_t mio_mbscpy (mio_mchar_t* buf, const mio_mchar_t* str)
{
	mio_mchar_t* org = buf;
	while ((*buf++ = *str++) != MIO_MT('\0'));
	return buf - org - 1;
}

int mio_mbsspltrn (
	mio_mchar_t* s, const mio_mchar_t* delim,
	mio_mchar_t lquote, mio_mchar_t rquote, 
	mio_mchar_t escape, const mio_mchar_t* trset)
{
	mio_mchar_t* p = s, *d;
	mio_mchar_t* sp = MIO_NULL, * ep = MIO_NULL;
	int delim_mode;
	int cnt = 0;

	if (delim == MIO_NULL) delim_mode = 0;
	else 
	{
		delim_mode = 1;
		for (d = (mio_mchar_t*)delim; *d != MIO_MT('\0'); d++)
			if (!IS_MSPACE(*d)) delim_mode = 2;
	}

	if (delim_mode == 0) 
	{
		/* skip preceding space characters */
		while (IS_MSPACE(*p)) p++;

		/* when 0 is given as "delim", it has an effect of cutting
		   preceding and trailing space characters off "s". */
		if (lquote != MIO_MT('\0') && *p == lquote) 
		{
			mio_mbscpy (p, p + 1);

			for (;;) 
			{
				if (*p == MIO_MT('\0')) return -1;

				if (escape != MIO_MT('\0') && *p == escape) 
				{
					if (trset != MIO_NULL && p[1] != MIO_MT('\0'))
					{
						const mio_mchar_t* ep = trset;
						while (*ep != MIO_MT('\0'))
						{
							if (p[1] == *ep++) 
							{
								p[1] = *ep;
								break;
							}
						}
					}

					mio_mbscpy (p, p + 1);
				}
				else 
				{
					if (*p == rquote) 
					{
						p++;
						break;
					}
				}

				if (sp == 0) sp = p;
				ep = p;
				p++;
			}
			while (IS_MSPACE(*p)) p++;
			if (*p != MIO_MT('\0')) return -1;

			if (sp == 0 && ep == 0) s[0] = MIO_MT('\0');
			else 
			{
				ep[1] = MIO_MT('\0');
				if (s != (mio_mchar_t*)sp) mio_mbscpy (s, sp);
				cnt++;
			}
		}
		else 
		{
			while (*p) 
			{
				if (!IS_MSPACE(*p)) 
				{
					if (sp == 0) sp = p;
					ep = p;
				}
				p++;
			}

			if (sp == 0 && ep == 0) s[0] = MIO_MT('\0');
			else 
			{
				ep[1] = MIO_MT('\0');
				if (s != (mio_mchar_t*)sp) mio_mbscpy (s, sp);
				cnt++;
			}
		}
	}
	else if (delim_mode == 1) 
	{
		mio_mchar_t* o;

		while (*p) 
		{
			o = p;
			while (IS_MSPACE(*p)) p++;
			if (o != p) { mio_mbscpy (o, p); p = o; }

			if (lquote != MIO_MT('\0') && *p == lquote) 
			{
				mio_mbscpy (p, p + 1);

				for (;;) 
				{
					if (*p == MIO_MT('\0')) return -1;

					if (escape != MIO_MT('\0') && *p == escape) 
					{
						if (trset != MIO_NULL && p[1] != MIO_MT('\0'))
						{
							const mio_mchar_t* ep = trset;
							while (*ep != MIO_MT('\0'))
							{
								if (p[1] == *ep++) 
								{
									p[1] = *ep;
									break;
								}
							}
						}
						mio_mbscpy (p, p + 1);
					}
					else 
					{
						if (*p == rquote) 
						{
							*p++ = MIO_MT('\0');
							cnt++;
							break;
						}
					}
					p++;
				}
			}
			else 
			{
				o = p;
				for (;;) 
				{
					if (*p == MIO_MT('\0')) 
					{
						if (o != p) cnt++;
						break;
					}
					if (IS_MSPACE (*p)) 
					{
						*p++ = MIO_MT('\0');
						cnt++;
						break;
					}
					p++;
				}
			}
		}
	}
	else /* if (delim_mode == 2) */
	{
		mio_mchar_t* o;
		int ok;

		while (*p != MIO_MT('\0')) 
		{
			o = p;
			while (IS_MSPACE(*p)) p++;
			if (o != p) { mio_mbscpy (o, p); p = o; }

			if (lquote != MIO_MT('\0') && *p == lquote) 
			{
				mio_mbscpy (p, p + 1);

				for (;;) 
				{
					if (*p == MIO_MT('\0')) return -1;

					if (escape != MIO_MT('\0') && *p == escape) 
					{
						if (trset != MIO_NULL && p[1] != MIO_MT('\0'))
						{
							const mio_mchar_t* ep = trset;
							while (*ep != MIO_MT('\0'))
							{
								if (p[1] == *ep++) 
								{
									p[1] = *ep;
									break;
								}
							}
						}

						mio_mbscpy (p, p + 1);
					}
					else 
					{
						if (*p == rquote) 
						{
							*p++ = MIO_MT('\0');
							cnt++;
							break;
						}
					}
					p++;
				}

				ok = 0;
				while (IS_MSPACE(*p)) p++;
				if (*p == MIO_MT('\0')) ok = 1;
				for (d = (mio_mchar_t*)delim; *d != MIO_MT('\0'); d++) 
				{
					if (*p == *d) 
					{
						ok = 1;
						mio_mbscpy (p, p + 1);
						break;
					}
				}
				if (ok == 0) return -1;
			}
			else 
			{
				o = p; sp = ep = 0;

				for (;;) 
				{
					if (*p == MIO_MT('\0')) 
					{
						if (ep) 
						{
							ep[1] = MIO_MT('\0');
							p = &ep[1];
						}
						cnt++;
						break;
					}
					for (d = (mio_mchar_t*)delim; *d != MIO_MT('\0'); d++) 
					{
						if (*p == *d)  
						{
							if (sp == MIO_NULL) 
							{
								mio_mbscpy (o, p); p = o;
								*p++ = MIO_MT('\0');
							}
							else 
							{
								mio_mbscpy (&ep[1], p);
								mio_mbscpy (o, sp);
								o[ep - sp + 1] = MIO_MT('\0');
								p = &o[ep - sp + 2];
							}
							cnt++;
							/* last empty field after delim */
							if (*p == MIO_MT('\0')) cnt++;
							goto exit_point;
						}
					}

					if (!IS_MSPACE (*p)) 
					{
						if (sp == MIO_NULL) sp = p;
						ep = p;
					}
					p++;
				}
exit_point:
				;
			}
		}
	}

	return cnt;
}

int mio_mbsspl (
	mio_mchar_t* s, const mio_mchar_t* delim,
	mio_mchar_t lquote, mio_mchar_t rquote, mio_mchar_t escape)
{
	return mio_mbsspltrn (s, delim, lquote, rquote, escape, MIO_NULL);
}
