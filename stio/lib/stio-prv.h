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

/*TODO: redefine and remove these */
#include <assert.h>
#include <string.h>
#include <stdio.h>
/*TODO: redefine these */
#define STIO_MEMSET(dst,byte,count) memset(dst,byte,count)
#define STIO_MEMCPY(dst,src,count) memcpy(dst,src,count)
#define STIO_MEMMOVE(dst,src,count) memmove(dst,src,count)
#define STIO_ASSERT assert


#define STIO_USE_TMRJOB_IDXPTR

struct stio_tmrjob_t
{
	void*                  ctx;
	stio_ntime_t           when;
	stio_tmrjob_handler_t  handler;
#if defined(STIO_USE_TMRJOB_IDXPTR)
	stio_tmridx_t*         idxptr; /* pointer to the index holder */
#else
	stio_tmrjob_updater_t  updater;
#endif
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
	} dev; /* normal devices */

	stio_dev_t* rdev; /* ruined device list - singly linked list */

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

#define stio_inittime(x,s,ns) (((x)->sec = (s)), ((x)->nsec = (ns)))
#define stio_cleartime(x) stio_inittime(x,0,0)
/*#define stio_cleartime(x) ((x)->sec = (x)->nsec = 0)*/
#define stio_cmptime(x,y) \
	(((x)->sec == (y)->sec)? ((x)->nsec - (y)->nsec): \
	                         ((x)->sec -  (y)->sec))

#define stio_iszerotime(x) ((x)->sec == 0 && (x)->nsec == 0)


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


/**
 * The stio_gettime() function gets the current time.
 */
STIO_EXPORT void stio_gettime (
	stio_ntime_t* nt
);

/**
 * The stio_addtime() function adds x and y and stores the result in z 
 */
STIO_EXPORT void stio_addtime (
	const stio_ntime_t* x,
	const stio_ntime_t* y,
	stio_ntime_t*       z
);

/**
 * The stio_subtime() function subtract y from x and stores the result in z.
 */
STIO_EXPORT void stio_subtime (
	const stio_ntime_t* x,
	const stio_ntime_t* y,
	stio_ntime_t*       z
);

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

void stio_firetmrjobs (
	stio_t*             stio,
	const stio_ntime_t* tm,
	stio_size_t*        firecnt
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
