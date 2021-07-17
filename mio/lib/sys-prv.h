/*
 * $Id$
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

#ifndef _MIO_SYS_PRV_H_
#define _MIO_SYS_PRV_H_

/* this is a private header file used by sys-XXX files */

#include "mio-prv.h"

#if defined(HAVE_SYS_EPOLL_H)
#	include <sys/epoll.h>
#	define USE_EPOLL
#elif defined(HAVE_SYS_POLL_H)
#	include <sys/poll.h>
#	define USE_POLL
#else
#	error NO SUPPORTED MULTIPLEXER
#endif

#include <pthread.h>

/* -------------------------------------------------------------------------- */

#if defined(USE_POLL)

struct mio_sys_mux_t
{
	struct
	{
		mio_oow_t* ptr;
		mio_oow_t  size;
		mio_oow_t  capa;
	} map; /* handle to index */

	struct
	{
		struct pollfd* pfd;
		mio_dev_t** dptr;
		mio_oow_t size;
		mio_oow_t capa;
	} pd; /* poll data */

	int ctrlp[2];
};

#elif defined(USE_EPOLL)

struct mio_sys_mux_t
{
	int hnd;
	struct epoll_event revs[1024]; /* TODO: is it a good size? */

	int ctrlp[2];
};

#endif

typedef struct mio_sys_mux_t mio_sys_mux_t;

/* -------------------------------------------------------------------------- */

struct mio_sys_log_t
{
	int fd;
	int fd_flag; /* bitwise OR'ed of logfd_flag_t bits */

	struct
	{
		mio_bch_t buf[4096];
		mio_oow_t len;
	} out;

	pthread_mutex_t mtx;
};
typedef struct mio_sys_log_t mio_sys_log_t;

/* -------------------------------------------------------------------------- */

struct mio_sys_time_t
{
#if defined(_WIN32)
	HANDLE waitable_timer;
	DWORD tc_last;
	DWORD tc_overflow;
#elif defined(__OS2__)
	ULONG tc_last;
	mio_ntime_t tc_last_ret;
#elif defined(__DOS__)
	clock_t tc_last;
	mio_ntime_t tc_last_ret;
#else
	/* nothing */
#endif
};

typedef struct mio_sys_time_t mio_sys_time_t;

/* -------------------------------------------------------------------------- */
struct mio_sys_t
{
	mio_sys_log_t log;
	mio_sys_mux_t mux;
	mio_sys_time_t time;
};

/* -------------------------------------------------------------------------- */

#if defined(__cplusplus)
extern "C" {
#endif

int mio_sys_initlog (
	mio_t* mio
);

void mio_sys_finilog (
	mio_t* mio
);

void mio_sys_locklog (
	mio_t* mio
);

void mio_sys_locklog (
	mio_t* mio
);


int mio_sys_initmux (
	mio_t* mio
);

void mio_sys_finimux (
	mio_t* mio
);

int mio_sys_inittime (
	mio_t* mio
);

void mio_sys_finitime (
	mio_t* mio
);

#if defined(__cplusplus)
}
#endif

#endif
