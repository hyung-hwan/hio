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

#include "hio-prv.h"

#include <errno.h>
#include <string.h>

static hio_errnum_t errno_to_errnum (int errcode)
{
	switch (errcode)
	{
		case ENOMEM: return HIO_ESYSMEM;
		case EINVAL: return HIO_EINVAL;

	#if defined(EBUSY)
		case EBUSY: return HIO_EBUSY;
	#endif
		case EACCES: return HIO_EACCES;
	#if defined(EPERM)
		case EPERM: return HIO_EPERM;
	#endif
	#if defined(EISDIR)
		case EISDIR: return HIO_EISDIR;
	#endif
	#if defined(ENOTDIR)
		case ENOTDIR: return HIO_ENOTDIR;
	#endif
		case ENOENT: return HIO_ENOENT;
	#if defined(EEXIST)
		case EEXIST: return HIO_EEXIST;
	#endif
	#if defined(EINTR)
		case EINTR:  return HIO_EINTR;
	#endif

	#if defined(EPIPE)
		case EPIPE:  return HIO_EPIPE;
	#endif

	#if defined(EAGAIN) && defined(EWOULDBLOCK) && (EAGAIN != EWOULDBLOCK)
		case EAGAIN: 
		case EWOULDBLOCK: return HIO_EAGAIN;
	#elif defined(EAGAIN)
		case EAGAIN: return HIO_EAGAIN;
	#elif defined(EWOULDBLOCK)
		case EWOULDBLOCK: return HIO_EAGAIN;
	#endif

	#if defined(EBADF)
		case EBADF: return HIO_EBADHND;
	#endif

	#if defined(EIO)
		case EIO: return HIO_EIOERR;
	#endif

	#if defined(EMFILE)
		case EMFILE:
			return HIO_EMFILE;
	#endif

	#if defined(ENFILE)
		case ENFILE:
			return HIO_ENFILE;
	#endif

	#if defined(ECONNREFUSED)
		case ECONNREFUSED:
			return HIO_ECONRF;
	#endif

	#if defined(ECONNRESETD)
		case ECONNRESET:
			return HIO_ECONRS;
	#endif

		default: return HIO_ESYSERR;
	}
}

#if defined(_WIN32)
static hio_errnum_t winerr_to_errnum (DWORD errcode)
{
	switch (errcode)
	{
		case ERROR_NOT_ENOUGH_MEMORY:
		case ERROR_OUTOFMEMORY:
			return HIO_ESYSMEM;

		case ERROR_INVALID_PARAMETER:
		case ERROR_INVALID_NAME:
			return HIO_EINVAL;

		case ERROR_INVALID_HANDLE:
			return HIO_EBADHND;

		case ERROR_ACCESS_DENIED:
		case ERROR_SHARING_VIOLATION:
			return HIO_EACCES;

	#if defined(ERROR_IO_PRIVILEGE_FAILED)
		case ERROR_IO_PRIVILEGE_FAILED:
	#endif
		case ERROR_PRIVILEGE_NOT_HELD:
			return HIO_EPERM;

		case ERROR_FILE_NOT_FOUND:
		case ERROR_PATH_NOT_FOUND:
			return HIO_ENOENT;

		case ERROR_ALREADY_EXISTS:
		case ERROR_FILE_EXISTS:
			return HIO_EEXIST;

		case ERROR_BROKEN_PIPE:
			return HIO_EPIPE;

		default:
			return HIO_ESYSERR;
	}
}
#endif

#if defined(__OS2__)
static hio_errnum_t os2err_to_errnum (APIRET errcode)
{
	/* APIRET e */
	switch (errcode)
	{
		case ERROR_NOT_ENOUGH_MEMORY:
			return HIO_ESYSMEM;

		case ERROR_INVALID_PARAMETER: 
		case ERROR_INVALID_NAME:
			return HIO_EINVAL;

		case ERROR_INVALID_HANDLE: 
			return HIO_EBADHND;

		case ERROR_ACCESS_DENIED: 
		case ERROR_SHARING_VIOLATION:
			return HIO_EACCES;

		case ERROR_FILE_NOT_FOUND:
		case ERROR_PATH_NOT_FOUND:
			return HIO_ENOENT;

		case ERROR_ALREADY_EXISTS:
			return HIO_EEXIST;

		/*TODO: add more mappings */
		default:
			return HIO_ESYSERR;
	}
}
#endif

#if defined(macintosh)
static hio_errnum_t macerr_to_errnum (int errcode)
{
	switch (e)
	{
		case notEnoughMemoryErr:
			return HIO_ESYSMEM;
		case paramErr:
			return HIO_EINVAL;

		case qErr: /* queue element not found during deletion */
		case fnfErr: /* file not found */
		case dirNFErr: /* direcotry not found */
		case resNotFound: /* resource not found */
		case resFNotFound: /* resource file not found */
		case nbpNotFound: /* name not found on remove */
			return HIO_ENOENT;

		/*TODO: add more mappings */
		default: 
			return HIO_ESYSERR;
	}
}
#endif

hio_errnum_t hio_sys_syserrstrb (hio_t* hio, int syserr_type, int syserr_code, hio_bch_t* buf, hio_oow_t len)
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
					buf, len, HIO_NULL
				);
				while (rc > 0 && buf[rc - 1] == '\r' || buf[rc - 1] == '\n') buf[--rc] = '\0';
			}
			return winerr_to_errnum(syserr_code);
		#elif defined(__OS2__)
			/* TODO: convert code to string */
			if (buf) hio_copy_bcstr (buf, len, "system error");
			return os2err_to_errnum(syserr_code);
		#elif defined(macintosh)
			/* TODO: convert code to string */
			if (buf) hio_copy_bcstr (buf, len, "system error");
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
			if (buf) hio_copy_bcstr (buf, len, strerror(syserr_code));
		#endif
			return errno_to_errnum(syserr_code);
	}


	if (buf) hio_copy_bcstr (buf, len, "system error");
	return HIO_ESYSERR;
}

