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

#ifndef _MIO_H_
#define _MIO_H_

#include <mio-cmn.h>

#if defined(_WIN32)
	typedef mio_uintptr_t qse_syshnd_t;
	#define MIO_SYSHND_INVALID (~(mio_uintptr_t)0)
#else
	typedef int mio_syshnd_t;
	#define MIO_SYSHND_INVALID (-1)
#endif

typedef struct mio_devaddr_t mio_devaddr_t;
struct mio_devaddr_t
{
	int len;
	void* ptr;
};

#define MIO_CONST_SWAP16(x) \
	((mio_uint16_t)((((mio_uint16_t)(x) & (mio_uint16_t)0x00ffU) << 8) | \
	                 (((mio_uint16_t)(x) & (mio_uint16_t)0xff00U) >> 8) ))

#define MIO_CONST_SWAP32(x) \
	((mio_uint32_t)((((mio_uint32_t)(x) & (mio_uint32_t)0x000000ffUL) << 24) | \
	                 (((mio_uint32_t)(x) & (mio_uint32_t)0x0000ff00UL) <<  8) | \
	                 (((mio_uint32_t)(x) & (mio_uint32_t)0x00ff0000UL) >>  8) | \
	                 (((mio_uint32_t)(x) & (mio_uint32_t)0xff000000UL) >> 24) ))

#if defined(MIO_ENDIAN_LITTLE)
#	define MIO_CONST_NTOH16(x) MIO_CONST_SWAP16(x)
#	define MIO_CONST_HTON16(x) MIO_CONST_SWAP16(x)
#	define MIO_CONST_NTOH32(x) MIO_CONST_SWAP32(x)
#	define MIO_CONST_HTON32(x) MIO_CONST_SWAP32(x)
#elif defined(MIO_ENDIAN_BIG)
#	define MIO_CONST_NTOH16(x) (x)
#	define MIO_CONST_HTON16(x) (x)
#	define MIO_CONST_NTOH32(x) (x)
#	define MIO_CONST_HTON32(x) (x)
#else
#	error UNKNOWN ENDIAN
#endif

/* ========================================================================= */

typedef struct mio_t mio_t;
typedef struct mio_dev_t mio_dev_t;
typedef struct mio_dev_mth_t mio_dev_mth_t;
typedef struct mio_dev_evcb_t mio_dev_evcb_t;

typedef struct mio_q_t mio_q_t;
typedef struct mio_wq_t mio_wq_t;
typedef struct mio_cwq_t mio_cwq_t;
typedef mio_intptr_t mio_iolen_t; /* NOTE: this is a signed type */

enum mio_errnum_t
{
	MIO_ENOERR,
	MIO_ENOIMPL,
	MIO_ESYSERR,
	MIO_EINTERN,

	MIO_ENOMEM,
	MIO_EINVAL,
	MIO_EEXIST,
	MIO_ENOENT,
	MIO_ENOSUP,     /* not supported */
	MIO_EMFILE,     /* too many open files */
	MIO_ENFILE,
	MIO_EAGAIN,
	MIO_ECONRF,     /* connection refused */
	MIO_ECONRS,     /* connection reset */
	MIO_ENOCAPA,    /* no capability */
	MIO_ETMOUT,     /* timed out */
	MIO_EPERM,      /* operation not permitted */

	MIO_EDEVMAKE,
	MIO_EDEVERR,
	MIO_EDEVHUP
};

typedef enum mio_errnum_t mio_errnum_t;

enum mio_stopreq_t
{
	MIO_STOPREQ_NONE = 0,
	MIO_STOPREQ_TERMINATION,
	MIO_STOPREQ_WATCHER_ERROR
};
typedef enum mio_stopreq_t mio_stopreq_t;

/* ========================================================================= */

#define MIO_TMRIDX_INVALID ((mio_tmridx_t)-1)

typedef mio_oow_t mio_tmridx_t;

typedef struct mio_tmrjob_t mio_tmrjob_t;

typedef void (*mio_tmrjob_handler_t) (
	mio_t*             mio,
	const mio_ntime_t* now, 
	mio_tmrjob_t*      tmrjob
);

struct mio_tmrjob_t
{
	void*                  ctx;
	mio_ntime_t           when;
	mio_tmrjob_handler_t  handler;
	mio_tmridx_t*         idxptr; /* pointer to the index holder */
};

/* ========================================================================= */

struct mio_dev_mth_t
{
	/* ------------------------------------------------------------------ */
	/* mandatory. called in mio_makedev() */
	int           (*make)         (mio_dev_t* dev, void* ctx); 

	/* ------------------------------------------------------------------ */
	/* mandatory. called in mio_killdev(). also called in mio_makedev() upon
	 * failure after make() success.
	 * 
	 * when 'force' is 0, the return value of -1 causes the device to be a
	 * zombie. the kill method is called periodically on a zombie device
	 * until the method returns 0.
	 *
	 * when 'force' is 1, the called should not return -1. If it does, the
	 * method is called once more only with the 'force' value of 2.
	 * 
	 * when 'force' is 2, the device is destroyed regardless of the return value.
	 */
	int           (*kill)         (mio_dev_t* dev, int force); 

	/* ------------------------------------------------------------------ */
	mio_syshnd_t (*getsyshnd)    (mio_dev_t* dev); /* mandatory. called in mio_makedev() after successful make() */


	/* ------------------------------------------------------------------ */
	/* return -1 on failure, 0 if no data is availble, 1 otherwise.
	 * when returning 1, *len must be sent to the length of data read.
	 * if *len is set to 0, it's treated as EOF. */
	int           (*read)         (mio_dev_t* dev, void* data, mio_iolen_t* len, mio_devaddr_t* srcaddr);

	/* ------------------------------------------------------------------ */
	int           (*write)        (mio_dev_t* dev, const void* data, mio_iolen_t* len, const mio_devaddr_t* dstaddr);

	/* ------------------------------------------------------------------ */
	int           (*ioctl)        (mio_dev_t* dev, int cmd, void* arg);

};

struct mio_dev_evcb_t
{
	/* return -1 on failure. 0 or 1 on success.
	 * when 0 is returned, it doesn't attempt to perform actual I/O.
	 * when 1 is returned, it attempts to perform actual I/O. */
	int           (*ready)        (mio_dev_t* dev, int events);

	/* return -1 on failure, 0 or 1 on success.
	 * when 0 is returned, the main loop stops the attempt to read more data.
	 * when 1 is returned, the main loop attempts to read more data without*/
	int           (*on_read)      (mio_dev_t* dev, const void* data, mio_iolen_t len, const mio_devaddr_t* srcaddr);

	/* return -1 on failure, 0 on success. 
	 * wrlen is the length of data written. it is the length of the originally
	 * posted writing request for a stream device. For a non stream device, it
	 * may be shorter than the originally posted length. */
	int           (*on_write)     (mio_dev_t* dev, mio_iolen_t wrlen, void* wrctx, const mio_devaddr_t* dstaddr);
};

struct mio_q_t
{
	mio_q_t*    next;
	mio_q_t*    prev;
};

#define MIO_Q_INIT(q) ((q)->next = (q)->prev = (q))
#define MIO_Q_TAIL(q) ((q)->prev)
#define MIO_Q_HEAD(q) ((q)->next)
#define MIO_Q_ISEMPTY(q) (MIO_Q_HEAD(q) == (q))
#define MIO_Q_ISNODE(q,x) ((q) != (x))
#define MIO_Q_ISHEAD(q,x) (MIO_Q_HEAD(q) == (x))
#define MIO_Q_ISTAIL(q,x) (MIO_Q_TAIL(q) == (x))

#define MIO_Q_NEXT(x) ((x)->next)
#define MIO_Q_PREV(x) ((x)->prev)

#define MIO_Q_LINK(p,x,n) do { \
	mio_q_t* pp = (p), * nn = (n); \
	(x)->prev = (p); \
	(x)->next = (n); \
	nn->prev = (x); \
	pp->next = (x); \
} while (0)

#define MIO_Q_UNLINK(x) do { \
	mio_q_t* pp = (x)->prev, * nn = (x)->next; \
	nn->prev = pp; pp->next = nn; \
} while (0)

#define MIO_Q_REPL(o,n) do { \
	mio_q_t* oo = (o), * nn = (n); \
	nn->next = oo->next; \
	nn->next->prev = nn; \
	nn->prev = oo->prev; \
	nn->prev->next = nn; \
} while (0)

/* insert an item at the back of the queue */
/*#define MIO_Q_ENQ(wq,x)  MIO_Q_LINK(MIO_Q_TAIL(wq), x, MIO_Q_TAIL(wq)->next)*/
#define MIO_Q_ENQ(wq,x)  MIO_Q_LINK(MIO_Q_TAIL(wq), x, wq)

/* remove an item in the front from the queue */
#define MIO_Q_DEQ(wq) MIO_Q_UNLINK(MIO_Q_HEAD(wq))

/* completed write queue */
struct mio_cwq_t
{
	mio_cwq_t*    next;
	mio_cwq_t*    prev;

	mio_iolen_t   olen; 
	void*         ctx;
	mio_dev_t*    dev;
	mio_devaddr_t dstaddr;
};

#define MIO_CWQ_INIT(cwq) ((cwq)->next = (cwq)->prev = (cwq))
#define MIO_CWQ_TAIL(cwq) ((cwq)->prev)
#define MIO_CWQ_HEAD(cwq) ((cwq)->next)
#define MIO_CWQ_ISEMPTY(cwq) (MIO_CWQ_HEAD(cwq) == (cwq))
#define MIO_CWQ_ISNODE(cwq,x) ((cwq) != (x))
#define MIO_CWQ_ISHEAD(cwq,x) (MIO_CWQ_HEAD(cwq) == (x))
#define MIO_CWQ_ISTAIL(cwq,x) (MIO_CWQ_TAIL(cwq) == (x))
#define MIO_CWQ_NEXT(x) ((x)->next)
#define MIO_CWQ_PREV(x) ((x)->prev)
#define MIO_CWQ_LINK(p,x,n) MIO_Q_LINK((mio_q_t*)p,(mio_q_t*)x,(mio_q_t*)n)
#define MIO_CWQ_UNLINK(x) MIO_Q_UNLINK((mio_q_t*)x)
#define MIO_CWQ_REPL(o,n) MIO_CWQ_REPL(o,n);
#define MIO_CWQ_ENQ(cwq,x) MIO_CWQ_LINK(MIO_CWQ_TAIL(cwq), (mio_q_t*)x, cwq)
#define MIO_CWQ_DEQ(cwq) MIO_CWQ_UNLINK(MIO_CWQ_HEAD(cwq))

/* write queue */
struct mio_wq_t
{
	mio_wq_t*       next;
	mio_wq_t*       prev;

	mio_iolen_t     olen; /* original data length */
	mio_uint8_t*    ptr;  /* pointer to data */
	mio_iolen_t     len;  /* remaining data length */
	void*           ctx;
	mio_dev_t*      dev; /* back-pointer to the device */

	mio_tmridx_t    tmridx;
	mio_devaddr_t   dstaddr;
};

#define MIO_WQ_INIT(wq) ((wq)->next = (wq)->prev = (wq))
#define MIO_WQ_TAIL(wq) ((wq)->prev)
#define MIO_WQ_HEAD(wq) ((wq)->next)
#define MIO_WQ_ISEMPTY(wq) (MIO_WQ_HEAD(wq) == (wq))
#define MIO_WQ_ISNODE(wq,x) ((wq) != (x))
#define MIO_WQ_ISHEAD(wq,x) (MIO_WQ_HEAD(wq) == (x))
#define MIO_WQ_ISTAIL(wq,x) (MIO_WQ_TAIL(wq) == (x))
#define MIO_WQ_NEXT(x) ((x)->next)
#define MIO_WQ_PREV(x) ((x)->prev)
#define MIO_WQ_LINK(p,x,n) MIO_Q_LINK((mio_q_t*)p,(mio_q_t*)x,(mio_q_t*)n)
#define MIO_WQ_UNLINK(x) MIO_Q_UNLINK((mio_q_t*)x)
#define MIO_WQ_REPL(o,n) MIO_WQ_REPL(o,n);
#define MIO_WQ_ENQ(wq,x) MIO_WQ_LINK(MIO_WQ_TAIL(wq), (mio_q_t*)x, wq)
#define MIO_WQ_DEQ(wq) MIO_WQ_UNLINK(MIO_WQ_HEAD(wq))

#define MIO_DEV_HEADERS \
	mio_t*          mio; \
	mio_oow_t       dev_size; \
	int             dev_capa; \
	mio_dev_mth_t*  dev_mth; \
	mio_dev_evcb_t* dev_evcb; \
	mio_wq_t        wq; \
	mio_oow_t       cw_count; \
	mio_dev_t*      dev_prev; \
	mio_dev_t*      dev_next 

struct mio_dev_t
{
	MIO_DEV_HEADERS;
};

enum mio_dev_capa_t
{
	MIO_DEV_CAPA_VIRTUAL      = (1 << 0),
	MIO_DEV_CAPA_IN           = (1 << 1),
	MIO_DEV_CAPA_OUT          = (1 << 2),
	/* #MIO_DEV_CAPA_PRI is meaningful only if #MIO_DEV_CAPA_IN is set */
	MIO_DEV_CAPA_PRI          = (1 << 3), 
	MIO_DEV_CAPA_STREAM       = (1 << 4),
	MIO_DEV_CAPA_OUT_QUEUED   = (1 << 5),

	/* internal use only. never set this bit to the dev_capa field */
	MIO_DEV_CAPA_IN_DISABLED  = (1 << 9),
	MIO_DEV_CAPA_IN_CLOSED    = (1 << 10),
	MIO_DEV_CAPA_OUT_CLOSED   = (1 << 11),
	MIO_DEV_CAPA_IN_WATCHED   = (1 << 12),
	MIO_DEV_CAPA_OUT_WATCHED  = (1 << 13),
	MIO_DEV_CAPA_PRI_WATCHED  = (1 << 14), /**< can be set only if MIO_DEV_CAPA_IN_WATCHED is set */

	MIO_DEV_CAPA_ACTIVE       = (1 << 15),
	MIO_DEV_CAPA_HALTED       = (1 << 16),
	MIO_DEV_CAPA_ZOMBIE       = (1 << 17)
};
typedef enum mio_dev_capa_t mio_dev_capa_t;

enum mio_dev_watch_cmd_t
{
	MIO_DEV_WATCH_START,
	MIO_DEV_WATCH_UPDATE,
	MIO_DEV_WATCH_RENEW, /* automatic update */
	MIO_DEV_WATCH_STOP
};
typedef enum mio_dev_watch_cmd_t mio_dev_watch_cmd_t;

enum mio_dev_event_t
{
	MIO_DEV_EVENT_IN  = (1 << 0),
	MIO_DEV_EVENT_OUT = (1 << 1),

	MIO_DEV_EVENT_PRI = (1 << 2),
	MIO_DEV_EVENT_HUP = (1 << 3),
	MIO_DEV_EVENT_ERR = (1 << 4)
};
typedef enum mio_dev_event_t mio_dev_event_t;


/* ========================================================================= */

#ifdef __cplusplus
extern "C" {
#endif

MIO_EXPORT mio_t* mio_open (
	mio_mmgr_t*   mmgr,
	mio_oow_t    xtnsize,
	mio_oow_t    tmrcapa,  /**< initial timer capacity */
	mio_errnum_t* errnum
);

MIO_EXPORT void mio_close (
	mio_t* mio
);

MIO_EXPORT int mio_init (
	mio_t*      mio,
	mio_mmgr_t* mmgr,
	mio_oow_t  tmrcapa
);

MIO_EXPORT void mio_fini (
	mio_t*      mio
);

MIO_EXPORT int mio_exec (
	mio_t* mio
);

MIO_EXPORT int mio_loop (
	mio_t* mio
);

MIO_EXPORT void mio_stop (
	mio_t*        mio,
	mio_stopreq_t stopreq
);

MIO_EXPORT mio_dev_t* mio_makedev (
	mio_t*          mio,
	mio_oow_t      dev_size,
	mio_dev_mth_t*  dev_mth,
	mio_dev_evcb_t* dev_evcb,
	void*            make_ctx
);

MIO_EXPORT void mio_killdev (
	mio_t*     mio,
	mio_dev_t* dev
);

MIO_EXPORT int mio_dev_ioctl (
	mio_dev_t* dev,
	int         cmd,
	void*       arg
);

MIO_EXPORT int mio_dev_watch (
	mio_dev_t*          dev,
	mio_dev_watch_cmd_t cmd,
	/** 0 or bitwise-ORed of #MIO_DEV_EVENT_IN and #MIO_DEV_EVENT_OUT */
	int                  events
);

MIO_EXPORT int mio_dev_read (
	mio_dev_t*         dev,
	int                 enabled
);

/**
 * The mio_dev_write() function posts a writing request. 
 * It attempts to write data immediately if there is no pending requests.
 * If writing fails, it returns -1. If writing succeeds, it calls the
 * on_write callback. If the callback fails, it returns -1. If the callback 
 * succeeds, it returns 1. If no immediate writing is possible, the request
 * is enqueued to a pending request list. If enqueing gets successful,
 * it returns 0. otherwise it returns -1.
 */ 
MIO_EXPORT int mio_dev_write (
	mio_dev_t*            dev,
	const void*            data,
	mio_iolen_t           len,
	void*                  wrctx,
	const mio_devaddr_t*  dstaddr
);


MIO_EXPORT int mio_dev_timedwrite (
	mio_dev_t*           dev,
	const void*           data,
	mio_iolen_t          len,
	const mio_ntime_t*   tmout,
	void*                 wrctx,
	const mio_devaddr_t* dstaddr
);

MIO_EXPORT void mio_dev_halt (
	mio_dev_t* dev
);

/* ========================================================================= */

#define mio_inittime(x,s,ns) (((x)->sec = (s)), ((x)->nsec = (ns)))
#define mio_cleartime(x) mio_inittime(x,0,0)
#define mio_cmptime(x,y) \
	(((x)->sec == (y)->sec)? ((x)->nsec - (y)->nsec): \
	                         ((x)->sec -  (y)->sec))

/* if time has been normalized properly, nsec must be equal to or
 * greater than 0. */
#define mio_isnegtime(x) ((x)->sec < 0)
#define mio_ispostime(x) ((x)->sec > 0 || ((x)->sec == 0 && (x)->nsec > 0))
#define mio_iszerotime(x) ((x)->sec == 0 && (x)->nsec == 0)


/**
 * The mio_gettime() function gets the current time.
 */
MIO_EXPORT void mio_gettime (
	mio_ntime_t* nt
);

/**
 * The mio_addtime() function adds x and y and stores the result in z 
 */
MIO_EXPORT void mio_addtime (
	const mio_ntime_t* x,
	const mio_ntime_t* y,
	mio_ntime_t*       z
);

/**
 * The mio_subtime() function subtract y from x and stores the result in z.
 */
MIO_EXPORT void mio_subtime (
	const mio_ntime_t* x,
	const mio_ntime_t* y,
	mio_ntime_t*       z
);

/**
 * The mio_instmrjob() function schedules a new event.
 *
 * \return #MIO_TMRIDX_INVALID on failure, valid index on success.
 */

MIO_EXPORT mio_tmridx_t mio_instmrjob (
	mio_t*              mio,
	const mio_tmrjob_t* job
);

MIO_EXPORT mio_tmridx_t mio_updtmrjob (
	mio_t*              mio,
	mio_tmridx_t        index,
	const mio_tmrjob_t* job
);

MIO_EXPORT void mio_deltmrjob (
	mio_t*          mio,
	mio_tmridx_t    index
);

/**
 * The mio_gettmrjob() function returns the
 * pointer to the registered event at the given index.
 */
MIO_EXPORT mio_tmrjob_t* mio_gettmrjob (
	mio_t*            mio,
	mio_tmridx_t      index
);

MIO_EXPORT int mio_gettmrjobdeadline (
	mio_t*            mio,
	mio_tmridx_t      index,
	mio_ntime_t*      deadline
);

/* ========================================================================= */


#if defined(MIO_HAVE_UINT16_T)
MIO_EXPORT mio_uint16_t mio_ntoh16 (
	mio_uint16_t x
);

MIO_EXPORT mio_uint16_t mio_hton16 (
	mio_uint16_t x
);
#endif

#if defined(MIO_HAVE_UINT32_T)
MIO_EXPORT mio_uint32_t mio_ntoh32 (
	mio_uint32_t x
);

MIO_EXPORT mio_uint32_t mio_hton32 (
	mio_uint32_t x
);
#endif

#if defined(MIO_HAVE_UINT64_T)
MIO_EXPORT mio_uint64_t mio_ntoh64 (
	mio_uint64_t x
);

MIO_EXPORT mio_uint64_t mio_hton64 (
	mio_uint64_t x
);
#endif

#if defined(MIO_HAVE_UINT128_T)
MIO_EXPORT mio_uint128_t mio_ntoh128 (
	mio_uint128_t x
);

MIO_EXPORT mio_uint128_t mio_hton128 (
	mio_uint128_t x
);
#endif

/* ========================================================================= */


#ifdef __cplusplus
}
#endif


#endif
