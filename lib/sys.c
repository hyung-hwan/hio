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

#include "sys-prv.h"

int hio_sys_init (hio_t* hio)
{
	int log_inited = 0;
	int mux_inited = 0;
	int time_inited = 0;

	hio->sysdep = (hio_sys_t*)hio_callocmem(hio, HIO_SIZEOF(*hio->sysdep));
	if (!hio->sysdep) return -1;

	if (hio->_features & HIO_FEATURE_LOG)
	{
		if (hio_sys_initlog(hio) <= -1) goto oops;
		log_inited = 1;
	}

	if (hio->_features & HIO_FEATURE_MUX)
	{
		if (hio_sys_initmux(hio) <= -1) goto oops;
		mux_inited = 1;
	}

	if (hio_sys_inittime(hio) <= -1) goto oops;
	time_inited = 1;

	return 0;

oops:
	if (time_inited) hio_sys_finitime (hio);
	if (mux_inited) hio_sys_finimux (hio);
	if (log_inited) hio_sys_finilog (hio);
	if (hio->sysdep) 
	{
		hio_freemem (hio, hio->sysdep);
		hio->sysdep = HIO_NULL;
	}
	return -1;
}

void hio_sys_fini (hio_t* hio)
{
	hio_sys_finitime (hio);
	if (hio->_features & HIO_FEATURE_MUX) hio_sys_finimux (hio);
	if (hio->_features & HIO_FEATURE_LOG) hio_sys_finilog (hio);

	hio_freemem (hio, hio->sysdep);
	hio->sysdep = HIO_NULL;
}


/* TODO: migrate these functions */
#include <fcntl.h>
#include <errno.h>

int hio_makesyshndasync (hio_t* hio, hio_syshnd_t hnd)
{
#if defined(F_GETFL) && defined(F_SETFL) && defined(O_NONBLOCK)
	int flags;

	if ((flags = fcntl(hnd, F_GETFL, 0)) <= -1 ||
	    fcntl(hnd, F_SETFL, flags | O_NONBLOCK) <= -1)
	{
		hio_seterrwithsyserr (hio, 0, errno);
		return -1;
	}

	return 0;
#else
	hio_seterrnum (hio, HIO_ENOIMPL);
	return -1;
#endif
}

int hio_makesyshndcloexec (hio_t* hio, hio_syshnd_t hnd)
{
#if defined(F_GETFL) && defined(F_SETFL) && defined(FD_CLOEXEC)
	int flags;

	if ((flags = fcntl(hnd, F_GETFD, 0)) <= -1 ||
	    fcntl(hnd, F_SETFD, flags | FD_CLOEXEC) <= -1)
	{
		hio_seterrwithsyserr (hio, 0, errno);
		return -1;
	}

	return 0;
#else
	hio_seterrnum (hio, HIO_ENOIMPL);
	return -1;
#endif
}
