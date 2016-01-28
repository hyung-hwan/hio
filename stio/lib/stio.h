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

#ifndef _STIO_H_
#define _STIO_H_

/* TODO: remove these headers */
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>


typedef signed char stio_int8_t;
typedef unsigned char stio_uint8_t;
typedef unsigned long stio_size_t;
#define STIO_MEMSET(dst,byte,count) memset(dst,byte,count)
#define STIO_MEMCPY(dst,src,count) memcpy(dst,src,count)
#define STIO_MEMMOVE(dst,src,count) memmove(dst,src,count)
#define STIO_ASSERT assert


#define STIO_SIZEOF(x) sizeof(x)
#define STIO_COUNTOF(x) (sizeof(x) / sizeof(x[0]))
#define STIO_NULL ((void*)0)

typedef struct stio_mmgr_t stio_mmgr_t;

typedef void* (*stio_mmgr_alloc_t) (stio_mmgr_t* mmgr, stio_size_t size);
typedef void (*stio_mmgr_free_t) (stio_mmgr_t* mmgr, void* ptr);

struct stio_mmgr_t
{
	stio_mmgr_alloc_t alloc;	
	stio_mmgr_free_t free;
	void* ctx;
};

#define STIO_MMGR_ALLOC(mmgr,size) (mmgr)->alloc(mmgr,size)
#define STIO_MMGR_FREE(mmgr,ptr) (mmgr)->free(mmgr,ptr)

struct stio_sckadr_t
{
	int family;
	stio_uint8_t data[64]; /* TODO: use the actual sockaddr size */
};

typedef struct stio_sckadr_t stio_sckadr_t;

#if defined(_WIN32)
#	define STIO_IOCP_KEY 1

	typedef int stio_scklen_t;
	typedef SOCKET stio_sckhnd_t;
#	define STIO_SCKHND_INVALID (INVALID_SOCKET)

	typedef HANDLE stio_syshnd_t;
#else
	typedef socklen_t stio_scklen_t;
	typedef int stio_sckhnd_t;
#	define STIO_SCKHND_INVALID (-1)

	typedef int stio_syshnd_t;
#endif

typedef int stio_sckfam_t;


/* ------------------------------------------------------------------------- */
typedef struct stio_t stio_t;
typedef struct stio_dev_t stio_dev_t;
typedef struct stio_dev_mth_t stio_dev_mth_t;
typedef struct stio_dev_evcb_t stio_dev_evcb_t;

typedef struct stio_wq_t stio_wq_t;
typedef unsigned int stio_len_t; /* TODO: remove it? */


enum stio_errnum_t
{
	STIO_ENOERR,
	STIO_ENOMEM,
	STIO_EINVAL,
	STIO_ENOSUP, /* not supported */

	STIO_EDEVMAKE
};

typedef enum stio_errnum_t stio_errnum_t;


struct stio_dev_mth_t
{
	/* ------------------------------------------------------------------ */
	int           (*make)         (stio_dev_t* dev, void* ctx); /* mandatory. called in stix_makedev() */

	/* ------------------------------------------------------------------ */
	void          (*kill)         (stio_dev_t* dev); /* mandatory. called in stix_killdev(). called in stix_makedev() upon failure after make() success */

	/* ------------------------------------------------------------------ */
#if 0
/* TODO: countsyshnds() if the device has multiple handles.
 * getsyshnd() to accept the handle id between 0 and countsysnhnds() - 1
 */
	int           (*countsyshnds) (stio_dev_t* dev); /* optional */
#endif
	stio_syshnd_t (*getsyshnd)    (stio_dev_t* dev); /* mandatory. called in stix_makedev() after successful make() */

	/* ------------------------------------------------------------------ */
	int           (*ioctl)        (stio_dev_t* dev, int cmd, void* arg);

	/* ------------------------------------------------------------------ */
	int           (*recv)         (stio_dev_t* dev, void* data, stio_len_t* len);

	/* ------------------------------------------------------------------ */
	int           (*send)         (stio_dev_t* dev, const void* data, stio_len_t* len);
};

struct stio_dev_evcb_t
{
	/* return -1 on failure. 0 or 1 on success.
	 * when 0 is returned, it doesn't attempt to perform actual I/O.
	 * when 1 is returned, it attempts to perform actual I/O.
	 * it must not kill the device */
	int           (*ready)        (stio_dev_t* dev, int events);

	/* return -1 on failure, 0 on success
	 * it must not kill the device */
	int           (*on_recv)      (stio_dev_t* dev, const void* data, stio_len_t len);

	/* return -1 on failure, 0 on success. 
	 * it must not kill the device */
	int           (*on_sent)      (stio_dev_t* dev, void* sendctx);
};

struct stio_wq_t
{
	stio_wq_t*    next;
	stio_wq_t*    prev;

	stio_uint8_t* ptr;
	stio_len_t    len;
	void*         ctx;
};

#define STIO_WQ_INIT(wq) ((wq)->next = (wq)->prev = (wq))
#define STIO_WQ_TAIL(wq) ((wq)->prev)
#define STIO_WQ_HEAD(wq) ((wq)->next)
#define STIO_WQ_ISEMPTY(wq) (STIO_WQ_HEAD(wq) == (wq))
#define STIO_WQ_ISNODE(wq,x) ((wq) != (x))
#define STIO_WQ_ISHEAD(wq,x) (STIO_WQ_HEAD(wq) == (x))
#define STIO_WQ_ISTAIL(wq,x) (STIO_WQ_TAIL(wq) == (x))

#define STIO_WQ_NEXT(x) ((x)->next)
#define STIO_WQ_PREV(x) ((x)->prev)

#define STIO_WQ_LINK(p,x,n) do { \
	stio_wq_t* pp = (p), * nn = (n); \
	(x)->prev = (p); \
	(x)->next = (n); \
	nn->prev = (x); \
	pp->next = (x); \
} while (0)

#define STIO_WQ_UNLINK(x) do { \
	stio_wq_t* pp = (x)->prev, * nn = (x)->next; \
	nn->prev = pp; pp->next = nn; \
} while (0)

#define STIO_WQ_REPL(o,n) do { \
	stio_wq_t* oo = (o), * nn = (n); \
	nn->next = oo->next; \
	nn->next->prev = nn; \
	nn->prev = oo->prev; \
	nn->prev->next = nn; \
} while (0)

/* insert an item at the back of the queue */
/*#define STIO_WQ_ENQ(wq,x)  STIO_WQ_LINK(STIO_WQ_TAIL(wq), x, STIO_WQ_TAIL(wq)->next)*/
#define STIO_WQ_ENQ(wq,x)  STIO_WQ_LINK(STIO_WQ_TAIL(wq), x, wq)

/* remove an item in the front from the queue */
#define STIO_WQ_DEQ(wq) STIO_WQ_UNLINK(STIO_WQ_HEAD(wq))

#define STIO_DEV_HEADERS \
	stio_t* stio; \
	stio_dev_mth_t* mth; \
	stio_dev_evcb_t* evcb; \
	stio_wq_t wq; \
	stio_dev_t* prev; \
	stio_dev_t* next 

struct stio_dev_t
{
	STIO_DEV_HEADERS;
};

enum stio_dev_event_cmd_t
{
	STIO_DEV_EVENT_ADD,
	STIO_DEV_EVENT_MOD,
	STIO_DEV_EVENT_DEL
};
typedef enum stio_dev_event_cmd_t stio_dev_event_cmd_t;

enum stio_dev_event_flag_t
{
	STIO_DEV_EVENT_IN  = (1 << 0),
	STIO_DEV_EVENT_OUT = (1 << 1),
	STIO_DEV_EVENT_PRI = (1 << 2),
	STIO_DEV_EVENT_HUP = (1 << 3),
	STIO_DEV_EVENT_ERR = (1 << 4)
};
typedef enum stio_dev_event_flag_t stio_dev_event_flag_t;


#ifdef __cplusplus
extern "C" {
#endif

stio_t* stio_open (
	stio_mmgr_t* mmgr,
	stio_size_t xtnsize,
	stio_errnum_t* errnum
);

void stio_close (
	stio_t* stio
);

int stio_exec (
	stio_t* stio
);

int stio_loop (
	stio_t* stio
);

void stio_stop (
	stio_t* stio
);

stio_dev_t* stio_makedev (
	stio_t*          stio,
	stio_size_t      dev_size,
	stio_dev_mth_t*  dev_mth,
	stio_dev_evcb_t* dev_evcb,
	void*            make_ctx
);

void stio_killdev (
	stio_t*     stio,
	stio_dev_t* dev
);

int stio_dev_ioctl (
	stio_dev_t* dev,
	int         cmd,
	void*       arg
);

int stio_dev_event (
	stio_dev_t*          dev,
	stio_dev_event_cmd_t cmd,
	int                  flags
);

int stio_dev_send (
	stio_dev_t*   dev,
	const void*   data,
	stio_len_t    len,
	void*         sendctx
);

#ifdef __cplusplus
}
#endif


#endif
