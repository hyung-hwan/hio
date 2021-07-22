/*
 * $Id
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

#include <mio-path.h>
#include "mio-prv.h"

/* TODO: support the \\?\ prefix and the \\.\ prefix on windows 
 *       support \\?\UNC\server\path which is equivalent to \\server\path. 
 * */
/* ------------------------------------------------------------------ */
/*  UCH IMPLEMENTATION                                                */
/* ------------------------------------------------------------------ */

#if 0
int mio_is_ucstr_path_absolute (const mio_uch_t* path)
{
	if (MIO_IS_PATH_SEP(path[0])) return 1;
#if defined(_WIN32) || defined(__OS2__) || defined(__DOS__)
	/* a drive like c:tmp is absolute in positioning the drive.
	 * but the path within the drive is kind of relative */
	if (MIO_IS_PATH_DRIVE(path)) return 1;
#endif
     return 0;
}

int mio_is_ucstr_path_drive (const mio_uch_t* path)
{
#if defined(_WIN32) || defined(__OS2__) || defined(__DOS__)
	if (MIO_IS_PATH_DRIVE(path)) return 1;
#endif
	return 0;
}

int mio_is_ucstr_path_drive_absolute (const mio_uch_t* path)
{
#if defined(_WIN32) || defined(__OS2__) || defined(__DOS__)
	if (MIO_IS_PATH_DRIVE(path) && MIO_IS_PATH_SEP(path[2])) return 1;
#endif
	return 0;
}

int mio_is_ucstr_path_drive_current (const mio_uch_t* path)
{
#if defined(_WIN32) || defined(__OS2__) || defined(__DOS__)
	if (MIO_IS_PATH_DRIVE(path) && path[2] == '\0') return 1;
#endif
	return 0;
}
#endif

mio_oow_t mio_canon_ucstr_path (const mio_uch_t* path, mio_uch_t* canon, int flags)
{
	const mio_uch_t* ptr;
	mio_uch_t* dst;
	mio_uch_t* non_root_start;
	int has_root = 0;
#if defined(_WIN32) || defined(__OS2__) || defined(__DOS__)
	int is_drive = 0;
#endif
	mio_oow_t canon_len;

	if (path[0] == '\0') 
	{
		/* if the source is empty, no translation is needed */
		canon[0] = '\0';
		return 0;
	}

	ptr = path;
	dst = canon;

#if defined(_WIN32) || defined(__OS2__) || defined(__DOS__)
	if (MIO_IS_PATH_DRIVE(ptr))
	{
		/* handle drive letter */
		*dst++ = *ptr++; /* drive letter */
		*dst++ = *ptr++; /* colon */

		is_drive = 1;
		if (MIO_IS_PATH_SEP(*ptr)) 
		{
			*dst++ = *ptr++; /* root directory */
			has_root = 1;
		}
	}
	else if (MIO_IS_PATH_SEP(*ptr)) 
	{
		*dst++ = *ptr++; /* root directory */
		has_root = 1;

	#if defined(_WIN32)
		/* handle UNC path for Windows */
		if (MIO_IS_PATH_SEP(*ptr)) 
		{
			*dst++ = *ptr++;

			if (MIO_IS_PATH_SEP_OR_NIL(*ptr))
			{
				/* if there is another separator after \\,
				 * it's not an UNC path. */
				dst--;
			}
			else
			{
				/* if it starts with \\, process host name */
				do { *dst++ = *ptr++; } while (!MIO_IS_PATH_SEP_OR_NIL(*ptr));
				if (MIO_IS_PATH_SEP(*ptr)) *dst++ = *ptr++;
			}
		}
	#endif
	}
#else
	if (MIO_IS_PATH_SEP(*ptr)) 
	{
		*dst++ = *ptr++; /* root directory */
		has_root = 1;
	}
#endif

	/* non_root_start points to the beginning of the canonicalized 
	 * path excluding the root directory part. */
	non_root_start = dst;

	do
	{
		const mio_uch_t* seg;
		mio_oow_t seglen;

		/* skip duplicate separators */
		while (MIO_IS_PATH_SEP(*ptr)) ptr++;

		/* end of path reached */
		if (*ptr == '\0') break;

		/* find the next segment */
		seg = ptr;
		while (!MIO_IS_PATH_SEP_OR_NIL(*ptr)) ptr++;
		seglen = ptr - seg;

		/* handle the segment */
		if (seglen == 1 && seg[0] == '.')
		{
			/* eat up . */
		}
		else if (!(flags & MIO_CANON_UCSTR_PATH_KEEP_DOUBLE_DOTS) &&
		         seglen == 2 && seg[0] == '.' && seg[1] == '.')
		{
			/* eat up the previous segment */
			mio_uch_t* tmp;

			tmp = dst;
			if (tmp > non_root_start) 
			{
				/* there is a previous segment. */

				tmp--; /* skip the separator just before .. */

				/* find the beginning of the previous segment */
				while (tmp > non_root_start)
				{
					tmp--;
					if (MIO_IS_PATH_SEP(*tmp)) 
					{
						tmp++; /* position it next to the separator */
						break; 
					}
				}
			}

			if (has_root)
			{
				/*
				 * Eat up the previous segment if it exists.
				 *
				 * If it doesn't exist, tmp == dst so dst = tmp
				 * keeps dst unchanged. If it exists, 
				 * tmp != dst. so dst = tmp changes dst.
				 *
				 * path  /abc/def/..
				 *                ^ ^
				 *              seg ptr
				 *
				 * canon /abc/def/
				 *            ^   ^   
				 *           tmp dst
				 */
				dst = tmp;
			}
			else
			{
				mio_oow_t prevlen;

				prevlen = dst - tmp;

				if (/*tmp == non_root_start &&*/ prevlen == 0)
				{
					/* there is no previous segment */
					goto normal;
				}

				if (prevlen == 3 && tmp[0] == '.' && tmp[1] == '.') 
				{
					/* nothing to eat away because the previous segment is ../
					 *
					 * path  ../../
					 *          ^ ^
					 *        seg ptr
					 *
					 * canon ../
					 *       ^  ^
					 *      tmp dst
					 */
					goto normal;
				}

				dst = tmp;
			}
		}
		else
		{
		normal:
			while (seg < ptr) *dst++ = *seg++;
			if (MIO_IS_PATH_SEP(*ptr)) 
			{
				/* this segment ended with a separator */
				*dst++ = *seg++; /* copy the separator */
				ptr++; /* move forward the pointer */
			}
		}
	}
	while (1);

	if (dst > non_root_start && MIO_IS_PATH_SEP(dst[-1]) && 
	    ((flags & MIO_CANON_BCSTR_PATH_DROP_TRAILING_SEP) || !MIO_IS_PATH_SEP(ptr[-1]))) 
	{
		/* if the canoncal path composed so far ends with a separator
		 * and the original path didn't end with the separator, delete
		 * the ending separator. 
		 * also delete it if MIO_CANON_BCSTR_PATH_DROP_TRAILING_SEP is set.
		 *
		 *   dst > non_root_start:
		 *     there is at least 1 character after the root directory 
		 *     part.
		 *   MIO_IS_PATH_SEP(dst[-1]):
		 *     the canonical path ends with a separator.
		 *   MIO_IS_PATH_SEP(ptr[-1]):
		 *     the origial path ends with a separator.
		 */
		dst[-1] = '\0';
		canon_len = dst - canon - 1;
	}
	else 
	{
		/* just null-terminate the canonical path normally */
		dst[0] = '\0';	
		canon_len = dst - canon;
	}

	if (canon_len <= 0) 
	{
		if (!(flags & MIO_CANON_UCSTR_PATH_EMPTY_SINGLE_DOT))
		{
			/* when resolving to a single dot, a trailing separator is not
			 * retained though the orignal path name contains it. */
			canon[0] = '.';
			canon[1] = '\0';
			canon_len = 1;
		}
	}
	else 
	{
		/* drop a traling separator if the last segment is 
		 * double slashes */

		int adj_base_len = 3;

#if defined(_WIN32) || defined(__OS2__) || defined(__DOS__)
		if (is_drive && !has_root) 
		{
			/* A path like A:..\\\ need some adjustment for
			 * finalization below. */
			adj_base_len += 2;
		}
#endif

		if (canon_len == adj_base_len)
		{
			/* i don't have to retain a trailing separator
			 * if the last segment is double slashes because 
			 * the double slahses indicate a directory obviously */
			if (canon[canon_len-3] == '.' &&
			    canon[canon_len-2] == '.' &&
			    MIO_IS_PATH_SEP(canon[canon_len-1]))
			{
				canon[--canon_len] = '\0';
			}
		}
		else if (canon_len > adj_base_len)
		{
			if (MIO_IS_PATH_SEP(canon[canon_len-4]) &&
			    canon[canon_len-3] == '.' &&
			    canon[canon_len-2] == '.' &&
			    MIO_IS_PATH_SEP(canon[canon_len-1]))
			{
				canon[--canon_len] = '\0';
			}
		}
	}

	return canon_len;
}


/* ------------------------------------------------------------------ */
/*  BCH IMPLEMENTATION                                                */
/* ------------------------------------------------------------------ */

#if 0
int mio_is_bcstr_path_absolute (const mio_bch_t* path)
{
	if (MIO_IS_PATH_SEP(path[0])) return 1;
#if defined(_WIN32) || defined(__OS2__) || defined(__DOS__)
	/* a drive like c:tmp is absolute in positioning the drive.
	 * but the path within the drive is kind of relative */
	if (MIO_IS_PATH_DRIVE(path)) return 1;
#endif
	return 0;
}

int mio_is_bcstr_path_drive (const mio_bch_t* path)
{
#if defined(_WIN32) || defined(__OS2__) || defined(__DOS__)
	if (MIO_IS_PATH_DRIVE(path)) return 1;
#endif
	return 0;
}

int mio_is_bcstr_path_drive_absolute (const mio_bch_t* path)
{
#if defined(_WIN32) || defined(__OS2__) || defined(__DOS__)
	if (MIO_IS_PATH_DRIVE(path) && MIO_IS_PATH_SEP(path[2])) return 1;
#endif
	return 0;
}

int mio_is_bcstr_path_drive_current (const mio_bch_t* path)
{
#if defined(_WIN32) || defined(__OS2__) || defined(__DOS__)
	if (MIO_IS_PATH_DRIVE(path) && path[2] == '\0') return 1;
#endif
	return 0;
}

#endif

mio_oow_t mio_canon_bcstr_path (const mio_bch_t* path, mio_bch_t* canon, int flags)
{
	const mio_bch_t* ptr;
	mio_bch_t* dst;
	mio_bch_t* non_root_start;
	int has_root = 0;
#if defined(_WIN32) || defined(__OS2__) || defined(__DOS__)
	int is_drive = 0;
#endif
	mio_oow_t canon_len;

	if (path[0] == '\0') 
	{
		/* if the source is empty, no translation is needed */
		canon[0] = '\0';
		return 0;
	}

	ptr = path;
	dst = canon;

#if defined(_WIN32) || defined(__OS2__) || defined(__DOS__)
	if (MIO_IS_PATH_DRIVE(ptr))
	{
		/* handle drive letter */
		*dst++ = *ptr++; /* drive letter */
		*dst++ = *ptr++; /* colon */

		is_drive = 1;
		if (MIO_IS_PATH_SEP(*ptr)) 
		{
			*dst++ = *ptr++; /* root directory */
			has_root = 1;
		}
	}
	else if (MIO_IS_PATH_SEP(*ptr)) 
	{
		*dst++ = *ptr++; /* root directory */
		has_root = 1;

	#if defined(_WIN32)
		/* handle UNC path for Windows */
		if (MIO_IS_PATH_SEP(*ptr)) 
		{
			*dst++ = *ptr++;

			if (MIO_IS_PATH_SEP_OR_NIL(*ptr))
			{
				/* if there is another separator after \\,
				 * it's not an UNC path. */
				dst--;
			}
			else
			{
				/* if it starts with \\, process host name */
				do { *dst++ = *ptr++; } while (!MIO_IS_PATH_SEP_OR_NIL(*ptr));
				if (MIO_IS_PATH_SEP(*ptr)) *dst++ = *ptr++;
			}
		}
	#endif
	}
#else
	if (MIO_IS_PATH_SEP(*ptr)) 
	{
		*dst++ = *ptr++; /* root directory */
		has_root = 1;
	}
#endif

	/* non_root_start points to the beginning of the canonicalized 
	 * path excluding the root directory part. */
	non_root_start = dst;

	do
	{
		const mio_bch_t* seg;
		mio_oow_t seglen;

		/* skip duplicate separators */
		while (MIO_IS_PATH_SEP(*ptr)) ptr++;

		/* end of path reached */
		if (*ptr == '\0') break;

		/* find the next segment */
		seg = ptr;
		while (!MIO_IS_PATH_SEP_OR_NIL(*ptr)) ptr++;
		seglen = ptr - seg;

		/* handle the segment */
		if (seglen == 1 && seg[0] == '.')
		{
			/* eat up . */
		}
		else if (!(flags & MIO_CANON_BCSTR_PATH_KEEP_DOUBLE_DOTS) &&
		         seglen == 2 && seg[0] == '.' && seg[1] == '.')
		{
			/* eat up the previous segment */
			mio_bch_t* tmp;

			tmp = dst;
			if (tmp > non_root_start) 
			{
				/* there is a previous segment. */

				tmp--; /* skip the separator just before .. */

				/* find the beginning of the previous segment */
				while (tmp > non_root_start)
				{
					tmp--;
					if (MIO_IS_PATH_SEP(*tmp)) 
					{
						tmp++; /* position it next to the separator */
						break; 
					}
				}
			}

			if (has_root)
			{
				/*
				 * Eat up the previous segment if it exists.
				 *
				 * If it doesn't exist, tmp == dst so dst = tmp
				 * keeps dst unchanged. If it exists, 
				 * tmp != dst. so dst = tmp changes dst.
				 *
				 * path  /abc/def/..
				 *                ^ ^
				 *              seg ptr
				 *
				 * canon /abc/def/
				 *            ^   ^   
				 *           tmp dst
				 */
				dst = tmp;
			}
			else
			{
				mio_oow_t prevlen;

				prevlen = dst - tmp;

				if (/*tmp == non_root_start &&*/ prevlen == 0)
				{
					/* there is no previous segment */
					goto normal;
				}

				if (prevlen == 3 && tmp[0] == '.' && tmp[1] == '.') 
				{
					/* nothing to eat away because the previous segment is ../
					 *
					 * path  ../../
					 *          ^ ^
					 *        seg ptr
					 *
					 * canon ../
					 *       ^  ^
					 *      tmp dst
					 */
					goto normal;
				}

				dst = tmp;
			}
		}
		else
		{
		normal:
			while (seg < ptr) *dst++ = *seg++;
			if (MIO_IS_PATH_SEP(*ptr)) 
			{
				/* this segment ended with a separator */
				*dst++ = *seg++; /* copy the separator */
				ptr++; /* move forward the pointer */
			}
		}
	}
	while (1);

	if (dst > non_root_start && MIO_IS_PATH_SEP(dst[-1]) && 
	    ((flags & MIO_CANON_BCSTR_PATH_DROP_TRAILING_SEP) || !MIO_IS_PATH_SEP(ptr[-1]))) 
	{
		/* if the canoncal path composed so far ends with a separator
		 * and the original path didn't end with the separator, delete
		 * the ending separator. 
		 * also delete it if MIO_CANON_BCSTR_PATH_DROP_TRAILING_SEP is set.
		 *
		 *   dst > non_root_start:
		 *     there is at least 1 character after the root directory 
		 *     part.
		 *   MIO_IS_PATH_SEP(dst[-1]):
		 *     the canonical path ends with a separator.
		 *   MIO_IS_PATH_SEP(ptr[-1]):
		 *     the origial path ends with a separator.
		 */
		dst[-1] = '\0';
		canon_len = dst - canon - 1;
	}
	else 
	{
		/* just null-terminate the canonical path normally */
		dst[0] = '\0';
		canon_len = dst - canon;
	}

	if (canon_len <= 0) 
	{
		if (!(flags & MIO_CANON_BCSTR_PATH_EMPTY_SINGLE_DOT))
		{
			/* when resolving to a single dot, a trailing separator is not
			 * retained though the orignal path name contains it. */
			canon[0] = '.';
			canon[1] = '\0';
			canon_len = 1;
		}
	}
	else 
	{
		/* drop a traling separator if the last segment is 
		 * double slashes */

		int adj_base_len = 3;

#if defined(_WIN32) || defined(__OS2__) || defined(__DOS__)
		if (is_drive && !has_root) 
		{
			/* A path like A:..\\\ need some adjustment for
			 * finalization below. */
			adj_base_len += 2;
		}
#endif

		if (canon_len == adj_base_len)
		{
			/* i don't have to retain a trailing separator
			 * if the last segment is double slashes because 
			 * the double slahses indicate a directory obviously */
			if (canon[canon_len-3] == '.' &&
			    canon[canon_len-2] == '.' &&
			    MIO_IS_PATH_SEP(canon[canon_len-1]))
			{
				canon[--canon_len] = '\0';
			}
		}
		else if (canon_len > adj_base_len)
		{
			if (MIO_IS_PATH_SEP(canon[canon_len-4]) &&
			    canon[canon_len-3] == '.' &&
			    canon[canon_len-2] == '.' &&
			    MIO_IS_PATH_SEP(canon[canon_len-1]))
			{
				canon[--canon_len] = '\0';
			}
		}
	}

	return canon_len;
}


