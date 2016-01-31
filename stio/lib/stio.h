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

#include <stio-cmn.h>

/**
 * The stio_ntime_t type defines a numeric time type expressed in the 
 *  number of milliseconds since the Epoch (00:00:00 UTC, Jan 1, 1970).
 */
typedef struct stio_ntime_t stio_ntime_t;
struct stio_ntime_t
{
	stio_intptr_t  sec;
	stio_int32_t   nsec; /* nanoseconds */
};


struct stio_sckadr_t
{
	int family;
	stio_uint8_t data[128]; /* TODO: use the actual sockaddr size */
};

typedef struct stio_sckadr_t stio_sckadr_t;

#if (STIO_SIZEOF_SOCKLEN_T == STIO_SIZEOF_INT)
	#if defined(STIO_SOCKLEN_T_IS_SIGNED)
		typedef int stio_scklen_t;
	#else
		typedef unsigned int stio_scklen_t;
	#endif
#elif (STIO_SIZEOF_SOCKLEN_T == STIO_SIZEOF_LONG)
	#if defined(STIO_SOCKLEN_T_IS_SIGNED)
		typedef long stio_scklen_t;
	#else
		typedef unsigned long stio_scklen_t;
	#endif
#else
	typedef int stio_scklen_t;
#endif

#if defined(_WIN32)
#	define STIO_IOCP_KEY 1
	/*
	typedef HANDLE stio_syshnd_t;
	typedef SOCKET stio_sckhnd_t;
#	define STIO_SCKHND_INVALID (INVALID_SOCKET)
	*/

	typedef stio_uintptr_t qse_syshnd_t;
	typedef stio_uintptr_t qse_sckhnd_t;
#	define STIO_SCKHND_INVALID (~(qse_sck_hnd_t)0)

#else
	typedef int stio_syshnd_t;
	typedef int stio_sckhnd_t;
#	define STIO_SCKHND_INVALID (-1)
	
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
	STIO_ENOENT,
	STIO_ENOSUP, /* not supported */
	STIO_EMFILE,
	STIO_ENFILE,

	STIO_EDEVMAKE,
	STIO_ESYSERR
};

typedef enum stio_errnum_t stio_errnum_t;


struct stio_dev_mth_t
{
	/* ------------------------------------------------------------------ */
	int           (*make)         (stio_dev_t* dev, void* ctx); /* mandatory. called in stio_makedev() */

	/* ------------------------------------------------------------------ */
	void          (*kill)         (stio_dev_t* dev); /* mandatory. called in stio_killdev(). called in stio_makedev() upon failure after make() success */

	/* ------------------------------------------------------------------ */
#if 0
/* TODO: countsyshnds() if the device has multiple handles.
 * getsyshnd() to accept the handle id between 0 and countsysnhnds() - 1
 */
	int           (*countsyshnds) (stio_dev_t* dev); /* optional */
#endif
	stio_syshnd_t (*getsyshnd)    (stio_dev_t* dev); /* mandatory. called in stio_makedev() after successful make() */

	/* ------------------------------------------------------------------ */
	int           (*ioctl)        (stio_dev_t* dev, int cmd, void* arg);

	/* ------------------------------------------------------------------ */
	/* return -1 on failure, 0 if no data is availble, 1 otherwise.
	 * when returning 1, *len must be sent to the length of data read.
	 * if *len is set to 0, it's treated as EOF.
	 * it must not kill the device */
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
	STIO_DEV_EVENT_UPD,
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


typedef struct stio_tmrjob_t stio_tmrjob_t;
typedef stio_size_t stio_tmridx_t;

typedef void (*stio_tmrjob_handler_t) (
	stio_t*             stio,
	const stio_ntime_t* now, 
	stio_tmrjob_t*      tmrjob
);

typedef void (*stio_tmrjob_updater_t) (
	stio_t*         stio,
	stio_tmridx_t   old_index,
	stio_tmridx_t   new_index,
	stio_tmrjob_t*  tmrjob
);

#ifdef __cplusplus
extern "C" {
#endif

STIO_EXPORT stio_t* stio_open (
	stio_mmgr_t*   mmgr,
	stio_size_t    xtnsize,
	stio_size_t    tmrcapa,  /**< initial timer capacity */
	stio_errnum_t* errnum
);

STIO_EXPORT void stio_close (
	stio_t* stio
);

STIO_EXPORT int stio_init (
	stio_t*      stio,
	stio_mmgr_t* mmgr,
	stio_size_t  tmrcapa
);

STIO_EXPORT void stio_fini (
	stio_t*      stio
);

STIO_EXPORT int stio_exec (
	stio_t* stio
);

STIO_EXPORT int stio_loop (
	stio_t* stio
);

STIO_EXPORT void stio_stop (
	stio_t* stio
);

STIO_EXPORT stio_dev_t* stio_makedev (
	stio_t*          stio,
	stio_size_t      dev_size,
	stio_dev_mth_t*  dev_mth,
	stio_dev_evcb_t* dev_evcb,
	void*            make_ctx
);

STIO_EXPORT void stio_killdev (
	stio_t*     stio,
	stio_dev_t* dev
);

STIO_EXPORT int stio_dev_ioctl (
	stio_dev_t* dev,
	int         cmd,
	void*       arg
);

STIO_EXPORT int stio_dev_event (
	stio_dev_t*          dev,
	stio_dev_event_cmd_t cmd,
	int                  flags
);

STIO_EXPORT int stio_dev_send (
	stio_dev_t*   dev,
	const void*   data,
	stio_len_t    len,
	void*         sendctx
);

#ifdef __cplusplus
}
#endif


#endif
