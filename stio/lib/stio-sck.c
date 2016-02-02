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


#include "stio-sck.h"
#include "stio-prv.h"

#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* ------------------------------------------------------------------------ */
void stio_closeasyncsck (stio_t* stio, stio_sckhnd_t sck)
{
#if defined(_WIN32)
	closesocket (sck);
#else
	close (sck);
#endif
}

int stio_makesckasync (stio_t* stio, stio_sckhnd_t sck)
{
	return stio_makesyshndasync (stio, (stio_syshnd_t)sck);
}

stio_sckhnd_t stio_openasyncsck (stio_t* stio, int domain, int type)
{
	stio_sckhnd_t sck;

#if defined(_WIN32)
	sck = WSASocket (domain, type, 0, NULL, 0, WSA_FLAG_OVERLAPPED /*| WSA_FLAG_NO_HANDLE_INHERIT*/);
	if (sck == STIO_SCKHND_INVALID) 
	{
		/* stio_seterrnum (dev->stio, STIO_ESYSERR); or translate errno to stio errnum */
		return STIO_SCKHND_INVALID;
	}
#else
	sck = socket (domain, type, 0); /* NO CLOEXEC or somethign */
	if (sck == STIO_SCKHND_INVALID) 
	{
		/* stio_seterrnum (dev->stio, STIO_ESYSERR); or translate errno to stio errnum */
		return STIO_SCKHND_INVALID;
	}

	if (stio_makesckasync (stio, sck) <= -1)
	{
		close (sck);
		return STIO_SCKHND_INVALID;
	}
#endif

	return sck;
}

int stio_getsckadrinfo (stio_t* stio, const stio_sckadr_t* addr, stio_scklen_t* len, stio_sckfam_t* family)
{
	struct sockaddr* saddr = (struct sockaddr*)addr;

	if (saddr->sa_family == AF_INET) 
	{
		if (len) *len = STIO_SIZEOF(struct sockaddr_in);
		if (family) *family = AF_INET;
		return 0;
	}
	else if (saddr->sa_family == AF_INET6)
	{
		if (len) *len =  STIO_SIZEOF(struct sockaddr_in6);
		if (family) *family = AF_INET6;
		return 0;
	}

	stio->errnum = STIO_EINVAL;
	return -1;
}
