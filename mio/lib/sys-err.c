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

#include <errno.h>
#include <string.h>

static mio_errnum_t errno_to_errnum (int errcode)
{
	switch (errcode)
	{
		case ENOMEM: return MIO_ESYSMEM;
		case EINVAL: return MIO_EINVAL;

	#if defined(EBUSY)
		case EBUSY: return MIO_EBUSY;
	#endif
		case EACCES: return MIO_EACCES;
	#if defined(EPERM)
		case EPERM: return MIO_EPERM;
	#endif
	#if defined(ENOTDIR)
		case ENOTDIR: return MIO_ENOTDIR;
	#endif
		case ENOENT: return MIO_ENOENT;
	#if defined(EEXIST)
		case EEXIST: return MIO_EEXIST;
	#endif
	#if defined(EINTR)
		case EINTR:  return MIO_EINTR;
	#endif

	#if defined(EPIPE)
		case EPIPE:  return MIO_EPIPE;
	#endif

	#if defined(EAGAIN) && defined(EWOULDBLOCK) && (EAGAIN != EWOULDBLOCK)
		case EAGAIN: 
		case EWOULDBLOCK: return MIO_EAGAIN;
	#elif defined(EAGAIN)
		case EAGAIN: return MIO_EAGAIN;
	#elif defined(EWOULDBLOCK)
		case EWOULDBLOCK: return MIO_EAGAIN;
	#endif

	#if defined(EBADF)
		case EBADF: return MIO_EBADHND;
	#endif

	#if defined(EIO)
		case EIO: return MIO_EIOERR;
	#endif

	#if defined(EMFILE)
		case EMFILE:
			return MIO_EMFILE;
	#endif

	#if defined(ENFILE)
		case ENFILE:
			return MIO_ENFILE;
	#endif

	#if defined(ECONNREFUSED)
		case ECONNREFUSED:
			return MIO_ECONRF;
	#endif

	#if defined(ECONNRESETD)
		case ECONNRESET:
			return MIO_ECONRS;
	#endif

		default: return MIO_ESYSERR;
	}
}

#if defined(_WIN32)
static mio_errnum_t winerr_to_errnum (DWORD errcode)
{
	switch (errcode)
	{
		case ERROR_NOT_ENOUGH_MEMORY:
		case ERROR_OUTOFMEMORY:
			return MIO_ESYSMEM;

		case ERROR_INVALID_PARAMETER:
		case ERROR_INVALID_NAME:
			return MIO_EINVAL;

		case ERROR_INVALID_HANDLE:
			return MIO_EBADHND;

		case ERROR_ACCESS_DENIED:
		case ERROR_SHARING_VIOLATION:
			return MIO_EACCES;

	#if defined(ERROR_IO_PRIVILEGE_FAILED)
		case ERROR_IO_PRIVILEGE_FAILED:
	#endif
		case ERROR_PRIVILEGE_NOT_HELD:
			return MIO_EPERM;

		case ERROR_FILE_NOT_FOUND:
		case ERROR_PATH_NOT_FOUND:
			return MIO_ENOENT;

		case ERROR_ALREADY_EXISTS:
		case ERROR_FILE_EXISTS:
			return MIO_EEXIST;

		case ERROR_BROKEN_PIPE:
			return MIO_EPIPE;

		default:
			return MIO_ESYSERR;
	}
}
#endif

#if defined(__OS2__)
static mio_errnum_t os2err_to_errnum (APIRET errcode)
{
	/* APIRET e */
	switch (errcode)
	{
		case ERROR_NOT_ENOUGH_MEMORY:
			return MIO_ESYSMEM;

		case ERROR_INVALID_PARAMETER: 
		case ERROR_INVALID_NAME:
			return MIO_EINVAL;

		case ERROR_INVALID_HANDLE: 
			return MIO_EBADHND;

		case ERROR_ACCESS_DENIED: 
		case ERROR_SHARING_VIOLATION:
			return MIO_EACCES;

		case ERROR_FILE_NOT_FOUND:
		case ERROR_PATH_NOT_FOUND:
			return MIO_ENOENT;

		case ERROR_ALREADY_EXISTS:
			return MIO_EEXIST;

		/*TODO: add more mappings */
		default:
			return MIO_ESYSERR;
	}
}
#endif

#if defined(macintosh)
static mio_errnum_t macerr_to_errnum (int errcode)
{
	switch (e)
	{
		case notEnoughMemoryErr:
			return MIO_ESYSMEM;
		case paramErr:
			return MIO_EINVAL;

		case qErr: /* queue element not found during deletion */
		case fnfErr: /* file not found */
		case dirNFErr: /* direcotry not found */
		case resNotFound: /* resource not found */
		case resFNotFound: /* resource file not found */
		case nbpNotFound: /* name not found on remove */
			return MIO_ENOENT;

		/*TODO: add more mappings */
		default: 
			return MIO_ESYSERR;
	}
}
#endif

mio_errnum_t mio_sys_syserrstrb (mio_t* mio, int syserr_type, int syserr_code, mio_bch_t* buf, mio_oow_t len)
{
	switch (syserr_type)
	{
		case 1: 
		#if defined(_WIN32)
			if (buf)
			{
				DWORD rc;
				rc = FormatMessageA (
					FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
					NULL, syserr_code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), 
					buf, len, MIO_NULL
				);
				while (rc > 0 && buf[rc - 1] == '\r' || buf[rc - 1] == '\n') buf[--rc] = '\0';
			}
			return winerr_to_errnum(syserr_code);
		#elif defined(__OS2__)
			/* TODO: convert code to string */
			if (buf) mio_copy_bcstr (buf, len, "system error");
			return os2err_to_errnum(syserr_code);
		#elif defined(macintosh)
			/* TODO: convert code to string */
			if (buf) mio_copy_bcstr (buf, len, "system error");
			return os2err_to_errnum(syserr_code);
		#else
			/* in other systems, errno is still the native system error code.
			 * fall thru */
		#endif

		case 0:
		#if defined(HAVE_STRERROR_R)
			if (buf) strerror_r (syserr_code, buf, len);
		#else
			/* this is not thread safe */
			if (buf) mio_copy_bcstr (buf, len, strerror(syserr_code));
		#endif
			return errno_to_errnum(syserr_code);
	}


	if (buf) mio_copy_bcstr (buf, len, "system error");
	return MIO_ESYSERR;
}

