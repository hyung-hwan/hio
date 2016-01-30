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
#include "stio-tim.h"

#include <sys/epoll.h>

/*TODO: redefine and remove these */
#include <assert.h>
#include <string.h>
#include <stdio.h>
/*TODO: redefine these */
#define STIO_MEMSET(dst,byte,count) memset(dst,byte,count)
#define STIO_MEMCPY(dst,src,count) memcpy(dst,src,count)
#define STIO_MEMMOVE(dst,src,count) memmove(dst,src,count)
#define STIO_ASSERT assert

typedef struct stio_tmrjob_t stio_tmrjob_t;
typedef stio_size_t stio_tmridx_t;

typedef void (*stio_tmr_handler_t) (
	stio_t*             stio,
	const stio_ntime_t* now, 
	stio_tmrjob_t*      evt
);

typedef void (*stio_updtmrjobr_t) (
	stio_t*         stio,
	stio_tmridx_t   old_index,
	stio_tmridx_t   new_index,
	stio_tmrjob_t*  evt
);


struct stio_tmrjob_t
{
	void*               ctx;
	stio_ntime_t        when;
	stio_tmr_handler_t  handler;
	stio_updtmrjobr_t   updater;
};

#define STIO_TMRIDX_INVALID ((stio_tmridx_t)-1)

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

	stio_uint8_t bigbuf[65535]; /* TODO: make this dynamic depending on devices added. device may indicate a buffer size required??? */

	struct
	{
		stio_size_t     capa;
		stio_size_t     size;
		stio_tmrjob_t*  jobs;
	} tmr;

	/* platform specific fields below */
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

int stio_getsckadrinfo (stio_t* stio, const stio_sckadr_t* addr, stio_scklen_t* len, stio_sckfam_t* family);


/**
 * The stio_instmrjob() function schedules a new event.
 *
 * \return #STIO_TMRIDX_INVALID on failure, valid index on success.
 */

stio_tmridx_t stio_instmrjob (
	stio_t*              stio,
	const stio_tmrjob_t* job
);

stio_tmridx_t stio_updtmrjob (
	stio_t*              stio,
	stio_tmridx_t        index,
	const stio_tmrjob_t* job
);

void stio_deltmrjob (
	stio_t*          stio,
	stio_tmridx_t    index
);

void stio_cleartmrjobs (
	stio_t* stio
);

stio_size_t stio_firetmrjobs (
	stio_t*             stio,
	const stio_ntime_t* tm
);

int stio_gettmrtmout (
	stio_t*             stio,
	const stio_ntime_t* tm,
	stio_ntime_t*       tmout
);

/**
 * The stio_gettmrjob() function returns the
 * pointer to the registered event at the given index.
 */
stio_tmrjob_t* stio_gettmrjob (
	stio_t*            stio,
	stio_tmridx_t   index
);
#ifdef __cplusplus
}
#endif


#endif
