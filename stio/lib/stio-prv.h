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

/*TODO: redefine and remove these */
#include <assert.h>
#include <string.h>
#include <stdio.h>
/*TODO: redefine these */
#define STIO_MEMSET(dst,byte,count) memset(dst,byte,count)
#define STIO_MEMCPY(dst,src,count) memcpy(dst,src,count)
#define STIO_MEMMOVE(dst,src,count) memmove(dst,src,count)
#define STIO_MEMCMP(dst,src,count) memcmp(dst,src,count)
#define STIO_ASSERT assert


struct stio_tmrjob_t
{
	void*                  ctx;
	stio_ntime_t           when;
	stio_tmrjob_handler_t  handler;
	stio_tmridx_t*         idxptr; /* pointer to the index holder */
};

#define STIO_TMRIDX_INVALID ((stio_tmridx_t)-1)

typedef struct stio_mux_t stio_mux_t;

struct stio_t
{
	stio_mmgr_t* mmgr;
	stio_errnum_t errnum;
	stio_stopreq_t stopreq;  /* stop request to abort stio_loop() */

	struct
	{
		stio_dev_t* head;
		stio_dev_t* tail;
	} actdev; /* active devices */

	struct
	{
		stio_dev_t* head;
		stio_dev_t* tail;
	} hltdev; /* halted devices */

	struct
	{
		stio_dev_t* head;
		stio_dev_t* tail;
	} zmbdev; /* zombie devices */

	stio_uint8_t bigbuf[65535]; /* TODO: make this dynamic depending on devices added. device may indicate a buffer size required??? */

	unsigned int renew_watch: 1;
	unsigned int in_exec: 1;

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
	stio_mux_t* mux;
#endif
};


#define STIO_EPOCH_YEAR  (1970)
#define STIO_EPOCH_MON   (1)
#define STIO_EPOCH_DAY   (1)
#define STIO_EPOCH_WDAY  (4)

/* windows specific epoch time */
#define STIO_EPOCH_YEAR_WIN   (1601)
#define STIO_EPOCH_MON_WIN    (1)
#define STIO_EPOCH_DAY_WIN    (1)

#define STIO_DAYS_PER_WEEK  (7)
#define STIO_MONS_PER_YEAR  (12)
#define STIO_HOURS_PER_DAY  (24)
#define STIO_MINS_PER_HOUR  (60)
#define STIO_MINS_PER_DAY   (STIO_MINS_PER_HOUR*STIO_HOURS_PER_DAY)
#define STIO_SECS_PER_MIN   (60)
#define STIO_SECS_PER_HOUR  (STIO_SECS_PER_MIN*STIO_MINS_PER_HOUR)
#define STIO_SECS_PER_DAY   (STIO_SECS_PER_MIN*STIO_MINS_PER_DAY)
#define STIO_MSECS_PER_SEC  (1000)
#define STIO_MSECS_PER_MIN  (STIO_MSECS_PER_SEC*STIO_SECS_PER_MIN)
#define STIO_MSECS_PER_HOUR (STIO_MSECS_PER_SEC*STIO_SECS_PER_HOUR)
#define STIO_MSECS_PER_DAY  (STIO_MSECS_PER_SEC*STIO_SECS_PER_DAY)

#define STIO_USECS_PER_MSEC (1000)
#define STIO_NSECS_PER_USEC (1000)
#define STIO_NSECS_PER_MSEC (STIO_NSECS_PER_USEC*STIO_USECS_PER_MSEC)
#define STIO_USECS_PER_SEC  (STIO_USECS_PER_MSEC*STIO_MSECS_PER_SEC)
#define STIO_NSECS_PER_SEC  (STIO_NSECS_PER_USEC*STIO_USECS_PER_MSEC*STIO_MSECS_PER_SEC)

#define STIO_SECNSEC_TO_MSEC(sec,nsec) \
	(((stio_intptr_t)(sec) * STIO_MSECS_PER_SEC) + ((stio_intptr_t)(nsec) / STIO_NSECS_PER_MSEC))

#define STIO_SECNSEC_TO_USEC(sec,nsec) \
	(((stio_intptr_t)(sec) * STIO_USECS_PER_SEC) + ((stio_intptr_t)(nsec) / STIO_NSECS_PER_USEC))

#define STIO_SEC_TO_MSEC(sec) ((sec) * STIO_MSECS_PER_SEC)
#define STIO_MSEC_TO_SEC(sec) ((sec) / STIO_MSECS_PER_SEC)

#define STIO_USEC_TO_NSEC(usec) ((usec) * STIO_NSECS_PER_USEC)
#define STIO_NSEC_TO_USEC(nsec) ((nsec) / STIO_NSECS_PER_USEC)

#define STIO_MSEC_TO_NSEC(msec) ((msec) * STIO_NSECS_PER_MSEC)
#define STIO_NSEC_TO_MSEC(nsec) ((nsec) / STIO_NSECS_PER_MSEC)

#define STIO_SEC_TO_NSEC(sec) ((sec) * STIO_NSECS_PER_SEC)
#define STIO_NSEC_TO_SEC(nsec) ((nsec) / STIO_NSECS_PER_SEC)

#define STIO_SEC_TO_USEC(sec) ((sec) * STIO_USECS_PER_SEC)
#define STIO_USEC_TO_SEC(usec) ((usec) / STIO_USECS_PER_SEC)


#ifdef __cplusplus
extern "C" {
#endif

int stio_makesyshndasync (
	stio_t*       stio,
	stio_syshnd_t hnd
);

stio_errnum_t stio_syserrtoerrnum (
	int no
);


stio_mchar_t* stio_mbsdup (
	stio_t*             stio,
	const stio_mchar_t* src
);

stio_size_t stio_mbscpy (
	stio_mchar_t*       buf,
	const stio_mchar_t* str
);

int stio_mbsspltrn (
	stio_mchar_t*       s,
	const stio_mchar_t* delim,
	stio_mchar_t        lquote,
	stio_mchar_t        rquote, 
	stio_mchar_t        escape,
	const stio_mchar_t* trset
);

int stio_mbsspl (
	stio_mchar_t*       s,
	const stio_mchar_t* delim,
	stio_mchar_t        lquote,
	stio_mchar_t        rquote,
	stio_mchar_t        escape
);

void stio_cleartmrjobs (
	stio_t* stio
);

void stio_firetmrjobs (
	stio_t*             stio,
	const stio_ntime_t* tmbase,
	stio_size_t*        firecnt
);


int stio_gettmrtmout (
	stio_t*             stio,
	const stio_ntime_t* tmbase,
	stio_ntime_t*       tmout
);

#ifdef __cplusplus
}
#endif


#endif
