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

#include "stio-prv.h"


/* ========================================================================= */

#if defined(STIO_HAVE_UINT16_T)

stio_uint16_t stio_ntoh16 (stio_uint16_t x)
{
#if defined(STIO_ENDIAN_BIG)
	return x;
#elif defined(STIO_ENDIAN_LITTLE)
	stio_uint8_t* c = (stio_uint8_t*)&x;
	return (stio_uint16_t)(
		((stio_uint16_t)c[0] << 8) |
		((stio_uint16_t)c[1] << 0));
#else
#	error Unknown endian
#endif
}

stio_uint16_t stio_hton16 (stio_uint16_t x)
{
#if defined(STIO_ENDIAN_BIG)
	return x;
#elif defined(STIO_ENDIAN_LITTLE)
	stio_uint8_t* c = (stio_uint8_t*)&x;
	return (stio_uint16_t)(
		((stio_uint16_t)c[0] << 8) |
		((stio_uint16_t)c[1] << 0));
#else
#	error Unknown endian
#endif
}

#endif

/* ========================================================================= */

#if defined(STIO_HAVE_UINT32_T)

stio_uint32_t stio_ntoh32 (stio_uint32_t x)
{
#if defined(STIO_ENDIAN_BIG)
	return x;
#elif defined(STIO_ENDIAN_LITTLE)
	stio_uint8_t* c = (stio_uint8_t*)&x;
	return (stio_uint32_t)(
		((stio_uint32_t)c[0] << 24) |
		((stio_uint32_t)c[1] << 16) |
		((stio_uint32_t)c[2] << 8) | 
		((stio_uint32_t)c[3] << 0));
#else
#	error Unknown endian
#endif
}

stio_uint32_t stio_hton32 (stio_uint32_t x)
{
#if defined(STIO_ENDIAN_BIG)
	return x;
#elif defined(STIO_ENDIAN_LITTLE)
	stio_uint8_t* c = (stio_uint8_t*)&x;
	return (stio_uint32_t)(
		((stio_uint32_t)c[0] << 24) |
		((stio_uint32_t)c[1] << 16) |
		((stio_uint32_t)c[2] << 8) | 
		((stio_uint32_t)c[3] << 0));
#else
#	error Unknown endian
#endif
}
#endif

/* ========================================================================= */

#if defined(STIO_HAVE_UINT64_T)

stio_uint64_t stio_ntoh64 (stio_uint64_t x)
{
#if defined(STIO_ENDIAN_BIG)
	return x;
#elif defined(STIO_ENDIAN_LITTLE)
	stio_uint8_t* c = (stio_uint8_t*)&x;
	return (stio_uint64_t)(
		((stio_uint64_t)c[0] << 56) | 
		((stio_uint64_t)c[1] << 48) | 
		((stio_uint64_t)c[2] << 40) |
		((stio_uint64_t)c[3] << 32) |
		((stio_uint64_t)c[4] << 24) |
		((stio_uint64_t)c[5] << 16) |
		((stio_uint64_t)c[6] << 8)  |
		((stio_uint64_t)c[7] << 0));
#else
#	error Unknown endian
#endif
}

stio_uint64_t stio_hton64 (stio_uint64_t x)
{
#if defined(STIO_ENDIAN_BIG)
	return x;
#elif defined(STIO_ENDIAN_LITTLE)
	stio_uint8_t* c = (stio_uint8_t*)&x;
	return (stio_uint64_t)(
		((stio_uint64_t)c[0] << 56) | 
		((stio_uint64_t)c[1] << 48) | 
		((stio_uint64_t)c[2] << 40) |
		((stio_uint64_t)c[3] << 32) |
		((stio_uint64_t)c[4] << 24) |
		((stio_uint64_t)c[5] << 16) |
		((stio_uint64_t)c[6] << 8)  |
		((stio_uint64_t)c[7] << 0));
#else
#	error Unknown endian
#endif
}

#endif

/* ========================================================================= */

#if defined(STIO_HAVE_UINT128_T)

stio_uint128_t stio_ntoh128 (stio_uint128_t x)
{
#if defined(STIO_ENDIAN_BIG)
	return x;
#elif defined(STIO_ENDIAN_LITTLE)
	stio_uint8_t* c = (stio_uint8_t*)&x;
	return (stio_uint128_t)(
		((stio_uint128_t)c[0]  << 120) | 
		((stio_uint128_t)c[1]  << 112) | 
		((stio_uint128_t)c[2]  << 104) |
		((stio_uint128_t)c[3]  << 96) |
		((stio_uint128_t)c[4]  << 88) |
		((stio_uint128_t)c[5]  << 80) |
		((stio_uint128_t)c[6]  << 72) |
		((stio_uint128_t)c[7]  << 64) |
		((stio_uint128_t)c[8]  << 56) | 
		((stio_uint128_t)c[9]  << 48) | 
		((stio_uint128_t)c[10] << 40) |
		((stio_uint128_t)c[11] << 32) |
		((stio_uint128_t)c[12] << 24) |
		((stio_uint128_t)c[13] << 16) |
		((stio_uint128_t)c[14] << 8)  |
		((stio_uint128_t)c[15] << 0));
#else
#	error Unknown endian
#endif
}

stio_uint128_t stio_hton128 (stio_uint128_t x)
{
#if defined(STIO_ENDIAN_BIG)
	return x;
#elif defined(STIO_ENDIAN_LITTLE)
	stio_uint8_t* c = (stio_uint8_t*)&x;
	return (stio_uint128_t)(
		((stio_uint128_t)c[0]  << 120) | 
		((stio_uint128_t)c[1]  << 112) | 
		((stio_uint128_t)c[2]  << 104) |
		((stio_uint128_t)c[3]  << 96) |
		((stio_uint128_t)c[4]  << 88) |
		((stio_uint128_t)c[5]  << 80) |
		((stio_uint128_t)c[6]  << 72) |
		((stio_uint128_t)c[7]  << 64) |
		((stio_uint128_t)c[8]  << 56) | 
		((stio_uint128_t)c[9]  << 48) | 
		((stio_uint128_t)c[10] << 40) |
		((stio_uint128_t)c[11] << 32) |
		((stio_uint128_t)c[12] << 24) |
		((stio_uint128_t)c[13] << 16) |
		((stio_uint128_t)c[14] << 8)  |
		((stio_uint128_t)c[15] << 0));
#else
#	error Unknown endian
#endif
}

#endif

/* ========================================================================= */

#define IS_MSPACE(x) ((x) == STIO_MT(' ') || (x) == STIO_MT('\t') || (x) == STIO_MT('\n') || (x) == STIO_MT('\r'))

stio_mchar_t* stio_mbsdup (stio_t* stio, const stio_mchar_t* src)
{
	stio_mchar_t* dst;
	stio_size_t len;

	dst = (stio_mchar_t*)src;
	while (*dst != STIO_MT('\0')) dst++;
	len = dst - src;

	dst = STIO_MMGR_ALLOC (stio->mmgr, (len + 1) * STIO_SIZEOF(*src));
	if (!dst)
	{
		stio->errnum = STIO_ENOMEM;
		return STIO_NULL;
	}

	STIO_MEMCPY (dst, src, (len + 1) * STIO_SIZEOF(*src));
	return dst;
}

stio_size_t stio_mbscpy (stio_mchar_t* buf, const stio_mchar_t* str)
{
	stio_mchar_t* org = buf;
	while ((*buf++ = *str++) != STIO_MT('\0'));
	return buf - org - 1;
}

int stio_mbsspltrn (
	stio_mchar_t* s, const stio_mchar_t* delim,
	stio_mchar_t lquote, stio_mchar_t rquote, 
	stio_mchar_t escape, const stio_mchar_t* trset)
{
	stio_mchar_t* p = s, *d;
	stio_mchar_t* sp = STIO_NULL, * ep = STIO_NULL;
	int delim_mode;
	int cnt = 0;

	if (delim == STIO_NULL) delim_mode = 0;
	else 
	{
		delim_mode = 1;
		for (d = (stio_mchar_t*)delim; *d != STIO_MT('\0'); d++)
			if (!IS_MSPACE(*d)) delim_mode = 2;
	}

	if (delim_mode == 0) 
	{
		/* skip preceding space characters */
		while (IS_MSPACE(*p)) p++;

		/* when 0 is given as "delim", it has an effect of cutting
		   preceding and trailing space characters off "s". */
		if (lquote != STIO_MT('\0') && *p == lquote) 
		{
			stio_mbscpy (p, p + 1);

			for (;;) 
			{
				if (*p == STIO_MT('\0')) return -1;

				if (escape != STIO_MT('\0') && *p == escape) 
				{
					if (trset != STIO_NULL && p[1] != STIO_MT('\0'))
					{
						const stio_mchar_t* ep = trset;
						while (*ep != STIO_MT('\0'))
						{
							if (p[1] == *ep++) 
							{
								p[1] = *ep;
								break;
							}
						}
					}

					stio_mbscpy (p, p + 1);
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
			if (*p != STIO_MT('\0')) return -1;

			if (sp == 0 && ep == 0) s[0] = STIO_MT('\0');
			else 
			{
				ep[1] = STIO_MT('\0');
				if (s != (stio_mchar_t*)sp) stio_mbscpy (s, sp);
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

			if (sp == 0 && ep == 0) s[0] = STIO_MT('\0');
			else 
			{
				ep[1] = STIO_MT('\0');
				if (s != (stio_mchar_t*)sp) stio_mbscpy (s, sp);
				cnt++;
			}
		}
	}
	else if (delim_mode == 1) 
	{
		stio_mchar_t* o;

		while (*p) 
		{
			o = p;
			while (IS_MSPACE(*p)) p++;
			if (o != p) { stio_mbscpy (o, p); p = o; }

			if (lquote != STIO_MT('\0') && *p == lquote) 
			{
				stio_mbscpy (p, p + 1);

				for (;;) 
				{
					if (*p == STIO_MT('\0')) return -1;

					if (escape != STIO_MT('\0') && *p == escape) 
					{
						if (trset != STIO_NULL && p[1] != STIO_MT('\0'))
						{
							const stio_mchar_t* ep = trset;
							while (*ep != STIO_MT('\0'))
							{
								if (p[1] == *ep++) 
								{
									p[1] = *ep;
									break;
								}
							}
						}
						stio_mbscpy (p, p + 1);
					}
					else 
					{
						if (*p == rquote) 
						{
							*p++ = STIO_MT('\0');
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
					if (*p == STIO_MT('\0')) 
					{
						if (o != p) cnt++;
						break;
					}
					if (IS_MSPACE (*p)) 
					{
						*p++ = STIO_MT('\0');
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
		stio_mchar_t* o;
		int ok;

		while (*p != STIO_MT('\0')) 
		{
			o = p;
			while (IS_MSPACE(*p)) p++;
			if (o != p) { stio_mbscpy (o, p); p = o; }

			if (lquote != STIO_MT('\0') && *p == lquote) 
			{
				stio_mbscpy (p, p + 1);

				for (;;) 
				{
					if (*p == STIO_MT('\0')) return -1;

					if (escape != STIO_MT('\0') && *p == escape) 
					{
						if (trset != STIO_NULL && p[1] != STIO_MT('\0'))
						{
							const stio_mchar_t* ep = trset;
							while (*ep != STIO_MT('\0'))
							{
								if (p[1] == *ep++) 
								{
									p[1] = *ep;
									break;
								}
							}
						}

						stio_mbscpy (p, p + 1);
					}
					else 
					{
						if (*p == rquote) 
						{
							*p++ = STIO_MT('\0');
							cnt++;
							break;
						}
					}
					p++;
				}

				ok = 0;
				while (IS_MSPACE(*p)) p++;
				if (*p == STIO_MT('\0')) ok = 1;
				for (d = (stio_mchar_t*)delim; *d != STIO_MT('\0'); d++) 
				{
					if (*p == *d) 
					{
						ok = 1;
						stio_mbscpy (p, p + 1);
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
					if (*p == STIO_MT('\0')) 
					{
						if (ep) 
						{
							ep[1] = STIO_MT('\0');
							p = &ep[1];
						}
						cnt++;
						break;
					}
					for (d = (stio_mchar_t*)delim; *d != STIO_MT('\0'); d++) 
					{
						if (*p == *d)  
						{
							if (sp == STIO_NULL) 
							{
								stio_mbscpy (o, p); p = o;
								*p++ = STIO_MT('\0');
							}
							else 
							{
								stio_mbscpy (&ep[1], p);
								stio_mbscpy (o, sp);
								o[ep - sp + 1] = STIO_MT('\0');
								p = &o[ep - sp + 2];
							}
							cnt++;
							/* last empty field after delim */
							if (*p == STIO_MT('\0')) cnt++;
							goto exit_point;
						}
					}

					if (!IS_MSPACE (*p)) 
					{
						if (sp == STIO_NULL) sp = p;
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

int stio_mbsspl (
	stio_mchar_t* s, const stio_mchar_t* delim,
	stio_mchar_t lquote, stio_mchar_t rquote, stio_mchar_t escape)
{
	return stio_mbsspltrn (s, delim, lquote, rquote, escape, STIO_NULL);
}
