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

#ifndef _HIO_SYS_PRV_H_
#define _HIO_SYS_PRV_H_

/* this is a private header file used by sys-XXX files */

#include "hio-prv.h"

#if defined(HAVE_SYS_EVENT_H) && defined(HAVE_KQUEUE) && defined(HAVE_KEVENT)
#	include <sys/types.h>
#	include <sys/event.h>
#	define USE_KQUEUE
#elif defined(HAVE_SYS_EPOLL_H)
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

struct hio_sys_mux_t
{
	struct
	{
		hio_oow_t* ptr;
		hio_oow_t  size;
		hio_oow_t  capa;
	} map; /* handle to index */

	struct
	{
		struct pollfd* pfd;
		hio_dev_t** dptr;
		hio_oow_t size;
		hio_oow_t capa;
	} pd; /* poll data */

	int ctrlp[2];
};

#elif defined(USE_SELECT)
struct hio_sys_mux_t
{
	fd_set rfds;
	fd_set wfds;
};


#elif defined(USE_KQUEUE)

struct hio_sys_mux_t
{
	int kq;

	struct kevent revs[1024]; /* TODO: is it a good size? */

	int ctrlp[2];
};

#elif defined(USE_EPOLL)

struct hio_sys_mux_t
{
	int hnd;
	struct epoll_event revs[1024]; /* TODO: is it a good size? */

	int ctrlp[2];
};

#endif

typedef struct hio_sys_mux_t hio_sys_mux_t;

/* -------------------------------------------------------------------------- */

struct hio_sys_log_t
{
	int fd;
	int fd_flag; /* bitwise OR'ed of logfd_flag_t bits */

	struct
	{
		hio_bch_t buf[4096];
		hio_oow_t len;
	} out;

	pthread_mutex_t mtx;
};
typedef struct hio_sys_log_t hio_sys_log_t;

/* -------------------------------------------------------------------------- */

struct hio_sys_time_t
{
#if defined(_WIN32)
	HANDLE waitable_timer;
	DWORD tc_last;
	DWORD tc_overflow;
#elif defined(__OS2__)
	ULONG tc_last;
	hio_ntime_t tc_last_ret;
#elif defined(__DOS__)
	clock_t tc_last;
	hio_ntime_t tc_last_ret;
#else
	/* nothing */
#endif
};

typedef struct hio_sys_time_t hio_sys_time_t;

/* -------------------------------------------------------------------------- */
struct hio_sys_t
{
	hio_sys_log_t log;
	hio_sys_mux_t mux;
	hio_sys_time_t time;
};

/* -------------------------------------------------------------------------- */

#if defined(__cplusplus)
extern "C" {
#endif

int hio_sys_initlog (
	hio_t* hio
);

void hio_sys_finilog (
	hio_t* hio
);

int hio_sys_initmux (
	hio_t* hio
);

void hio_sys_finimux (
	hio_t* hio
);

int hio_sys_inittime (
	hio_t* hio
);

void hio_sys_finitime (
	hio_t* hio
);

#if defined(__cplusplus)
}
#endif

#endif
