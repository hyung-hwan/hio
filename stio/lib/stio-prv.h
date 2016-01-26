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

#ifndef _STIO_PRV_H_
#define _STIO_PRV_H_

#include "stio.h"
#include <sys/epoll.h>

struct stio_t
{
	stio_mmgr_t* mmgr;
	stio_errnum_t errnum;
	int stopreq;  /* stop request to abort stio_loop() */

	struct
	{
		stio_dev_t* head;
		stio_dev_t* tail;
	} dev;

	stio_uint8_t bigbuf[65535]; /* make this dynamic depending on devices added. device may indicate a buffer size required??? */


#if defined(_WIN32)
	HANDLE iocp;
#else
	int mux;
	struct epoll_event revs[100];
#endif
};


#ifdef __cplusplus
extern "C" {
#endif

stio_sckhnd_t stio_openasyncsck (int domain, int type);
void stio_closeasyncsck (stio_sckhnd_t sck);
int stio_makesckasync (stio_sckhnd_t sck);

#ifdef __cplusplus
}
#endif


#endif
