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

#ifndef _MIO_H_
#define _MIO_H_

#include <mio-cmn.h>
#include <stdarg.h>

#if defined(_WIN32)
	typedef mio_uintptr_t mio_syshnd_t;
	#define MIO_SYSHND_INVALID (~(mio_uintptr_t)0)
#else
	typedef int mio_syshnd_t;
	#define MIO_SYSHND_INVALID (-1)
#endif

typedef struct mio_devaddr_t mio_devaddr_t;
struct mio_devaddr_t
{
	int   len;
	void* ptr;
};

#define MIO_ERRMSG_CAPA (2048)

/* [NOTE] ensure that it is a power of 2 */
#define MIO_LOG_CAPA_ALIGN 512

/* ========================================================================= */

typedef struct mio_t mio_t;
typedef struct mio_dev_t mio_dev_t;
typedef struct mio_dev_mth_t mio_dev_mth_t;
typedef struct mio_dev_evcb_t mio_dev_evcb_t;
typedef struct mio_svc_t mio_svc_t;

typedef struct mio_q_t mio_q_t;
typedef struct mio_wq_t mio_wq_t;
typedef struct mio_cwq_t mio_cwq_t;
typedef mio_intptr_t mio_iolen_t; /* NOTE: this is a signed type */

struct mio_iovec_t
{
	void*     iov_ptr;
	mio_oow_t iov_len;
};
typedef struct mio_iovec_t mio_iovec_t;

enum mio_errnum_t
{
	MIO_ENOERR,   /**< no error */
	MIO_EGENERIC, /**< generic error */

	MIO_ENOIMPL,  /**< not implemented */
	MIO_ESYSERR,  /**< system error */
	MIO_EINTERN,  /**< internal error */
	MIO_ESYSMEM,  /**< insufficient system memory */
	MIO_EOOMEM,   /**< insufficient object memory */

	MIO_EINVAL,   /**< invalid parameter or data */
	MIO_ENOENT,   /**< data not found */
	MIO_EEXIST,   /**< existing/duplicate data */
	MIO_EBUSY,    /**< system busy */
	MIO_EACCES,   /**< access denied */
	MIO_EPERM,    /**< operation not permitted */
	MIO_ENOTDIR,  /**< not directory */
	MIO_EINTR,    /**< interrupted */
	MIO_EPIPE,    /**< pipe error */
	MIO_EAGAIN,   /**< resource temporarily unavailable */
	MIO_EBADHND,  /**< bad system handle */

	MIO_EMFILE,   /**< too many open files */
	MIO_ENFILE,   /**< too many open files */

	MIO_EIOERR,   /**< I/O error */
	MIO_EECERR,   /**< encoding conversion error */
	MIO_EECMORE,  /**< insufficient data for encoding conversion */
	MIO_EBUFFULL, /**< buffer full */

	MIO_ECONRF,   /**< connection refused */
	MIO_ECONRS,   /**< connection reset */
	MIO_ENOCAPA,  /**< no capability */
	MIO_ETMOUT,   /**< timed out */
	MIO_ENORSP,   /**< no response */

	MIO_EDEVMAKE, /**< unable to make device */
	MIO_EDEVERR,  /**< device error */
	MIO_EDEVHUP   /**< device hang-up */
};
typedef enum mio_errnum_t mio_errnum_t;

struct mio_errinf_t
{
	mio_errnum_t num;
	mio_ooch_t msg[MIO_ERRMSG_CAPA];
};
typedef struct mio_errinf_t mio_errinf_t;

enum mio_option_t
{
	MIO_TRAIT,
	MIO_LOG_MASK,
	MIO_LOG_MAXCAPA
};
typedef enum mio_option_t mio_option_t;


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
	void*                 ctx;
	mio_ntime_t           when;
	mio_tmrjob_handler_t  handler;
	mio_tmridx_t*         idxptr; /* pointer to the index holder */
};

/* ========================================================================= */

struct mio_dev_mth_t
{
	/* ------------------------------------------------------------------ */
	/* mandatory. called in mio_dev_make() */
	int           (*make)         (mio_dev_t* dev, void* ctx); 

	/* ------------------------------------------------------------------ */
	/* mandatory. called in mio_dev_kill(). also called in mio_dev_make() upon
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
	mio_syshnd_t (*getsyshnd)    (mio_dev_t* dev); /* mandatory. called in mio_dev_make() after successful make() */


	/* ------------------------------------------------------------------ */
	/* return -1 on failure, 0 if no data is availble, 1 otherwise.
	 * when returning 1, *len must be sent to the length of data read.
	 * if *len is set to 0, it's treated as EOF. */
	int           (*read)         (mio_dev_t* dev, void* data, mio_iolen_t* len, mio_devaddr_t* srcaddr);

	/* ------------------------------------------------------------------ */
	int           (*write)        (mio_dev_t* dev, const void* data, mio_iolen_t* len, const mio_devaddr_t* dstaddr);
	int           (*writev)       (mio_dev_t* dev, const mio_iovec_t* iov, mio_iolen_t* iovcnt, const mio_devaddr_t* dstaddr);

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
	mio_q_t*    q_next;
	mio_q_t*    q_prev;
};

#define MIO_Q_INIT(q) ((q)->q_next = (q)->q_prev = (q))
#define MIO_Q_TAIL(q) ((q)->q_prev)
#define MIO_Q_HEAD(q) ((q)->q_next)
#define MIO_Q_IS_EMPTY(q) (MIO_Q_HEAD(q) == (q))
#define MIO_Q_IS_NODE(q,x) ((q) != (x))
#define MIO_Q_IS_HEAD(q,x) (MIO_Q_HEAD(q) == (x))
#define MIO_Q_IS_TAIL(q,x) (MIO_Q_TAIL(q) == (x))

#define MIO_Q_NEXT(x) ((x)->q_next)
#define MIO_Q_PREV(x) ((x)->q_prev)

#define MIO_Q_LINK(p,x,n) do { \
	mio_q_t* __pp = (p), * __nn = (n); \
	(x)->q_prev = (p); \
	(x)->q_next = (n); \
	__nn->q_prev = (x); \
	__pp->q_next = (x); \
} while (0)

#define MIO_Q_UNLINK(x) do { \
	mio_q_t* __pp = (x)->q_prev, * __nn = (x)->q_next; \
	__nn->q_prev = __pp; __pp->q_next = __nn; \
} while (0)

#define MIO_Q_REPL(o,n) do { \
	mio_q_t* __oo = (o), * __nn = (n); \
	__nn->q_next = __oo->q_next; \
	__nn->q_next->q_prev = __nn; \
	__nn->q_prev = __oo->q_prev; \
	__nn->q_prev->q_next = __nn; \
} while (0)

/* insert an item at the back of the queue */
/*#define MIO_Q_ENQ(wq,x)  MIO_Q_LINK(MIO_Q_TAIL(wq), x, MIO_Q_TAIL(wq)->next)*/
#define MIO_Q_ENQ(wq,x)  MIO_Q_LINK(MIO_Q_TAIL(wq), x, wq)

/* remove an item in the front from the queue */
#define MIO_Q_DEQ(wq) MIO_Q_UNLINK(MIO_Q_HEAD(wq))

/* completed write queue */
struct mio_cwq_t
{
	mio_cwq_t*    q_next;
	mio_cwq_t*    q_prev;

	mio_iolen_t   olen; 
	void*         ctx;
	mio_dev_t*    dev;
	mio_devaddr_t dstaddr;
};

#define MIO_CWQ_INIT(cwq) ((cwq)->q_next = (cwq)->q_prev = (cwq))
#define MIO_CWQ_TAIL(cwq) ((cwq)->q_prev)
#define MIO_CWQ_HEAD(cwq) ((cwq)->q_next)
#define MIO_CWQ_IS_EMPTY(cwq) (MIO_CWQ_HEAD(cwq) == (cwq))
#define MIO_CWQ_IS_NODE(cwq,x) ((cwq) != (x))
#define MIO_CWQ_IS_HEAD(cwq,x) (MIO_CWQ_HEAD(cwq) == (x))
#define MIO_CWQ_IS_TAIL(cwq,x) (MIO_CWQ_TAIL(cwq) == (x))
#define MIO_CWQ_NEXT(x) ((x)->q_next)
#define MIO_CWQ_PREV(x) ((x)->q_prev)
#define MIO_CWQ_LINK(p,x,n) MIO_Q_LINK((mio_q_t*)p,(mio_q_t*)x,(mio_q_t*)n)
#define MIO_CWQ_UNLINK(x) MIO_Q_UNLINK((mio_q_t*)x)
#define MIO_CWQ_REPL(o,n) MIO_Q_REPL(o,n);
#define MIO_CWQ_ENQ(cwq,x) MIO_CWQ_LINK(MIO_CWQ_TAIL(cwq), (mio_q_t*)x, cwq)
#define MIO_CWQ_DEQ(cwq) MIO_CWQ_UNLINK(MIO_CWQ_HEAD(cwq))

/* write queue */
struct mio_wq_t
{
	mio_wq_t*       q_next;
	mio_wq_t*       q_prev;

	mio_iolen_t     olen; /* original data length */
	mio_uint8_t*    ptr;  /* pointer to data */
	mio_iolen_t     len;  /* remaining data length */
	void*           ctx;
	mio_dev_t*      dev; /* back-pointer to the device */

	mio_tmridx_t    tmridx;
	mio_devaddr_t   dstaddr;
};

#define MIO_WQ_INIT(wq) ((wq)->q_next = (wq)->q_prev = (wq))
#define MIO_WQ_TAIL(wq) ((wq)->q_prev)
#define MIO_WQ_HEAD(wq) ((wq)->q_next)
#define MIO_WQ_IS_EMPTY(wq) (MIO_WQ_HEAD(wq) == (wq))
#define MIO_WQ_IS_NODE(wq,x) ((wq) != (x))
#define MIO_WQ_IS_HEAD(wq,x) (MIO_WQ_HEAD(wq) == (x))
#define MIO_WQ_IS_TAIL(wq,x) (MIO_WQ_TAIL(wq) == (x))
#define MIO_WQ_NEXT(x) ((x)->q_next)
#define MIO_WQ_PREV(x) ((x)->q_prev)
#define MIO_WQ_LINK(p,x,n) MIO_Q_LINK((mio_q_t*)p,(mio_q_t*)x,(mio_q_t*)n)
#define MIO_WQ_UNLINK(x) MIO_Q_UNLINK((mio_q_t*)x)
#define MIO_WQ_REPL(o,n) MIO_Q_REPL(o,n);
#define MIO_WQ_ENQ(wq,x) MIO_WQ_LINK(MIO_WQ_TAIL(wq), (mio_q_t*)x, wq)
#define MIO_WQ_DEQ(wq) MIO_WQ_UNLINK(MIO_WQ_HEAD(wq))

#define MIO_DEV_HEADER \
	mio_t*          mio; \
	mio_oow_t       dev_size; \
	int             dev_cap; \
	mio_dev_mth_t*  dev_mth; \
	mio_dev_evcb_t* dev_evcb; \
	mio_ntime_t     rtmout; \
	mio_tmridx_t    rtmridx; \
	mio_wq_t        wq; \
	mio_oow_t       cw_count; \
	mio_dev_t*      dev_prev; \
	mio_dev_t*      dev_next 

struct mio_dev_t
{
	MIO_DEV_HEADER;
};

#define MIO_DEVL_PREPEND_DEV(lh,dev) do { \
	(dev)->dev_prev = (lh); \
	(dev)->dev_next = (lh)->dev_next; \
	(dev)->dev_next->dev_prev = (dev); \
	(lh)->dev_next = (dev); \
} while(0)

#define MIO_DEVL_APPEND_DEV(lh,dev) do { \
	(dev)->dev_next = (lh); \
	(dev)->dev_prev = (lh)->dev_prev; \
	(dev)->dev_prev->dev_next = (dev); \
	(lh)->dev_prev = (dev); \
} while(0)

#define MIO_DEVL_UNLINK_DEV(dev) do { \
	(dev)->dev_prev->dev_next = (dev)->dev_next; \
	(dev)->dev_next->dev_prev = (dev)->dev_prev; \
} while (0)

#define MIO_DEVL_INIT(lh) ((lh)->dev_next = (lh)->dev_prev = lh)
#define MIO_DEVL_FIRST_DEV(lh) ((lh)->dev_next)
#define MIO_DEVL_LAST_DEV(lh) ((lh)->dev_prev)
#define MIO_DEVL_IS_EMPTY(lh) (MIO_DEVL_FIRST_DEV(lh) == (lh))
#define MIO_DEVL_IS_NIL_DEV(lh,dev) ((dev) == (lh))

enum mio_dev_cap_t
{
	MIO_DEV_CAP_VIRTUAL         = (1 << 0),
	MIO_DEV_CAP_IN              = (1 << 1),
	MIO_DEV_CAP_OUT             = (1 << 2),
	MIO_DEV_CAP_PRI             = (1 << 3),  /* meaningful only if #MIO_DEV_CAP_IN is set */
	MIO_DEV_CAP_STREAM          = (1 << 4),
	MIO_DEV_CAP_IN_DISABLED     = (1 << 5),
	MIO_DEV_CAP_OUT_UNQUEUEABLE = (1 << 6),
	MIO_DEV_CAP_ALL_MASK        = (MIO_DEV_CAP_VIRTUAL | MIO_DEV_CAP_IN | MIO_DEV_CAP_OUT | MIO_DEV_CAP_PRI | MIO_DEV_CAP_STREAM | MIO_DEV_CAP_IN_DISABLED | MIO_DEV_CAP_OUT_UNQUEUEABLE),

	/* -------------------------------------------------------------------
	 * the followings bits are for internal use only. 
	 * never set these bits to the dev_cap field.
	 * ------------------------------------------------------------------- */
	MIO_DEV_CAP_IN_CLOSED       = (1 << 10),
	MIO_DEV_CAP_OUT_CLOSED      = (1 << 11),
	MIO_DEV_CAP_IN_WATCHED      = (1 << 12),
	MIO_DEV_CAP_OUT_WATCHED     = (1 << 13),
	MIO_DEV_CAP_PRI_WATCHED     = (1 << 14), /**< can be set only if MIO_DEV_CAP_IN_WATCHED is set */
	MIO_DEV_CAP_ACTIVE          = (1 << 15),
	MIO_DEV_CAP_HALTED          = (1 << 16),
	MIO_DEV_CAP_ZOMBIE          = (1 << 17),
	MIO_DEV_CAP_RENEW_REQUIRED  = (1 << 18)
};
typedef enum mio_dev_cap_t mio_dev_cap_t;

enum mio_dev_watch_cmd_t
{
	MIO_DEV_WATCH_START,
	MIO_DEV_WATCH_UPDATE,
	MIO_DEV_WATCH_RENEW, /* automatic renewal */
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

#define MIO_CWQFL_SIZE 16
#define MIO_CWQFL_ALIGN 16


/* =========================================================================
 * CHECK-AND-FREE MEMORY BLOCK
 * ========================================================================= */

#define MIO_CFMB_HEADER \
	mio_t* mio; \
	mio_cfmb_t* cfmb_next; \
	mio_cfmb_t* cfmb_prev; \
	mio_cfmb_checker_t cfmb_checker

typedef struct mio_cfmb_t mio_cfmb_t;

typedef int (*mio_cfmb_checker_t) (
	mio_t*       mio,
	mio_cfmb_t*  cfmb
);

struct mio_cfmb_t
{
	MIO_CFMB_HEADER;
};

#define MIO_CFMBL_PREPEND_CFMB(lh,cfmb) do { \
	(cfmb)->cfmb_prev = (lh); \
	(cfmb)->cfmb_next = (lh)->cfmb_next; \
	(cfmb)->cfmb_next->cfmb_prev = (cfmb); \
	(lh)->cfmb_next = (cfmb); \
} while(0)

#define MIO_CFMBL_APPEND_CFMB(lh,cfmb) do { \
	(cfmb)->cfmb_next = (lh); \
	(cfmb)->cfmb_prev = (lh)->cfmb_prev; \
	(cfmb)->cfmb_prev->cfmb_next = (cfmb); \
	(lh)->cfmb_prev = (cfmb); \
} while(0)

#define MIO_CFMBL_UNLINK_CFMB(cfmb) do { \
	(cfmb)->cfmb_prev->cfmb_next = (cfmb)->cfmb_next; \
	(cfmb)->cfmb_next->cfmb_prev = (cfmb)->cfmb_prev; \
} while (0)

#define MIO_CFMBL_INIT(lh) ((lh)->cfmb_next = (lh)->cfmb_prev = lh)
#define MIO_CFMBL_FIRST_CFMB(lh) ((lh)->cfmb_next)
#define MIO_CFMBL_LAST_CFMB(lh) ((lh)->cfmb_prev)
#define MIO_CFMBL_IS_EMPTY(lh) (MIO_CFMBL_FIRST_CFMB(lh) == (lh))
#define MIO_CFMBL_IS_NIL_CFMB(lh,cfmb) ((cfmb) == (lh))

#define MIO_CFMBL_PREV_CFMB(cfmb) ((cfmb)->cfmb_prev)
#define MIO_CFMBL_NEXT_CFMB(cfmb) ((cfmb)->cfmb_next)
/* =========================================================================
 * SERVICE 
 * ========================================================================= */

typedef void (*mio_svc_stop_t) (mio_svc_t* svc);

#define MIO_SVC_HEADER \
	mio_t*          mio; \
	mio_svc_stop_t  svc_stop; \
	mio_svc_t*      svc_prev; \
	mio_svc_t*      svc_next 

/* the stop callback is called if it's not NULL and the service is still 
 * alive when mio_close() is reached. it still calls MIO_SVCL_UNLINK_SVC()
 * if the stop callback is NULL. The stop callback, if specified, must
 * call MIO_SVCL_UNLINK_SVC(). */ 

struct mio_svc_t
{
	MIO_SVC_HEADER;
};

#define MIO_SVCL_PREPEND_SVC(lh,svc) do { \
	(svc)->svc_prev = (lh); \
	(svc)->svc_next = (lh)->svc_next; \
	(svc)->svc_next->svc_prev = (svc); \
	(lh)->svc_next = (svc); \
} while(0)

#define MIO_SVCL_APPEND_SVC(lh,svc) do { \
	(svc)->svc_next = (lh); \
	(svc)->svc_prev = (lh)->svc_prev; \
	(svc)->svc_prev->svc_next = (svc); \
	(lh)->svc_prev = (svc); \
} while(0)

#define MIO_SVCL_UNLINK_SVC(svc) do { \
	(svc)->svc_prev->svc_next = (svc)->svc_next; \
	(svc)->svc_next->svc_prev = (svc)->svc_prev; \
} while (0)


#define MIO_SVCL_INIT(lh) ((lh)->svc_next = (lh)->svc_prev = lh)
#define MIO_SVCL_FIRST_SVC(lh) ((lh)->svc_next)
#define MIO_SVCL_LAST_SVC(lh) ((lh)->svc_prev)
#define MIO_SVCL_IS_EMPTY(lh) (MIO_SVCL_FIRST_SVC(lh) == (lh))
#define MIO_SVCL_IS_NIL_SVC(lh,svc) ((svc) == (lh))

#define MIO_SVCL_PREV_SVC(svc) ((svc)->svc_prev)
#define MIO_SVCL_NEXT_SVC(svc) ((svc)->svc_next)
/* =========================================================================
 * MIO LOGGING
 * ========================================================================= */

enum mio_log_mask_t
{
	MIO_LOG_DEBUG      = (1u << 0),
	MIO_LOG_INFO       = (1u << 1),
	MIO_LOG_WARN       = (1u << 2),
	MIO_LOG_ERROR      = (1u << 3),
	MIO_LOG_FATAL      = (1u << 4),

	MIO_LOG_UNTYPED    = (1u << 6), /* only to be used by MIO_DEBUGx() and MIO_INFOx() */
	MIO_LOG_CORE       = (1u << 7),
	MIO_LOG_DEV        = (1u << 8),
	MIO_LOG_TIMER      = (1u << 9),

	MIO_LOG_ALL_LEVELS = (MIO_LOG_DEBUG  | MIO_LOG_INFO | MIO_LOG_WARN | MIO_LOG_ERROR | MIO_LOG_FATAL),
	MIO_LOG_ALL_TYPES  = (MIO_LOG_UNTYPED | MIO_LOG_CORE | MIO_LOG_DEV | MIO_LOG_TIMER),

	MIO_LOG_STDOUT     = (1u << 14), /* write log messages to stdout without timestamp. MIO_LOG_STDOUT wins over MIO_LOG_STDERR. */
	MIO_LOG_STDERR     = (1u << 15)  /* write log messages to stderr without timestamp. */
};
typedef enum mio_log_mask_t mio_log_mask_t;

/* all bits must be set to get enabled */
#define MIO_LOG_ENABLED(mio,mask) (((mio)->option.log_mask & (mask)) == (mask))

#define MIO_LOG0(mio,mask,fmt) do { if (MIO_LOG_ENABLED(mio,mask)) mio_logbfmt(mio, mask, fmt); } while(0)
#define MIO_LOG1(mio,mask,fmt,a1) do { if (MIO_LOG_ENABLED(mio,mask)) mio_logbfmt(mio, mask, fmt, a1); } while(0)
#define MIO_LOG2(mio,mask,fmt,a1,a2) do { if (MIO_LOG_ENABLED(mio,mask)) mio_logbfmt(mio, mask, fmt, a1, a2); } while(0)
#define MIO_LOG3(mio,mask,fmt,a1,a2,a3) do { if (MIO_LOG_ENABLED(mio,mask)) mio_logbfmt(mio, mask, fmt, a1, a2, a3); } while(0)
#define MIO_LOG4(mio,mask,fmt,a1,a2,a3,a4) do { if (MIO_LOG_ENABLED(mio,mask)) mio_logbfmt(mio, mask, fmt, a1, a2, a3, a4); } while(0)
#define MIO_LOG5(mio,mask,fmt,a1,a2,a3,a4,a5) do { if (MIO_LOG_ENABLED(mio,mask)) mio_logbfmt(mio, mask, fmt, a1, a2, a3, a4, a5); } while(0)
#define MIO_LOG6(mio,mask,fmt,a1,a2,a3,a4,a5,a6) do { if (MIO_LOG_ENABLED(mio,mask)) mio_logbfmt(mio, mask, fmt, a1, a2, a3, a4, a5, a6); } while(0)

#if defined(MIO_BUILD_RELEASE)
	/* [NOTE]
	 *  get rid of debugging message totally regardless of
	 *  the log mask in the release build.
	 */
#	define MIO_DEBUG0(mio,fmt)
#	define MIO_DEBUG1(mio,fmt,a1)
#	define MIO_DEBUG2(mio,fmt,a1,a2)
#	define MIO_DEBUG3(mio,fmt,a1,a2,a3)
#	define MIO_DEBUG4(mio,fmt,a1,a2,a3,a4)
#	define MIO_DEBUG5(mio,fmt,a1,a2,a3,a4,a5)
#	define MIO_DEBUG6(mio,fmt,a1,a2,a3,a4,a5,a6)
#else
#	define MIO_DEBUG0(mio,fmt) MIO_LOG0(mio, MIO_LOG_DEBUG | MIO_LOG_UNTYPED, fmt)
#	define MIO_DEBUG1(mio,fmt,a1) MIO_LOG1(mio, MIO_LOG_DEBUG | MIO_LOG_UNTYPED, fmt, a1)
#	define MIO_DEBUG2(mio,fmt,a1,a2) MIO_LOG2(mio, MIO_LOG_DEBUG | MIO_LOG_UNTYPED, fmt, a1, a2)
#	define MIO_DEBUG3(mio,fmt,a1,a2,a3) MIO_LOG3(mio, MIO_LOG_DEBUG | MIO_LOG_UNTYPED, fmt, a1, a2, a3)
#	define MIO_DEBUG4(mio,fmt,a1,a2,a3,a4) MIO_LOG4(mio, MIO_LOG_DEBUG | MIO_LOG_UNTYPED, fmt, a1, a2, a3, a4)
#	define MIO_DEBUG5(mio,fmt,a1,a2,a3,a4,a5) MIO_LOG5(mio, MIO_LOG_DEBUG | MIO_LOG_UNTYPED, fmt, a1, a2, a3, a4, a5)
#	define MIO_DEBUG6(mio,fmt,a1,a2,a3,a4,a5,a6) MIO_LOG6(mio, MIO_LOG_DEBUG | MIO_LOG_UNTYPED, fmt, a1, a2, a3, a4, a5, a6)
#endif

#define MIO_INFO0(mio,fmt) MIO_LOG0(mio, MIO_LOG_INFO | MIO_LOG_UNTYPED, fmt)
#define MIO_INFO1(mio,fmt,a1) MIO_LOG1(mio, MIO_LOG_INFO | MIO_LOG_UNTYPED, fmt, a1)
#define MIO_INFO2(mio,fmt,a1,a2) MIO_LOG2(mio, MIO_LOG_INFO | MIO_LOG_UNTYPED, fmt, a1, a2)
#define MIO_INFO3(mio,fmt,a1,a2,a3) MIO_LOG3(mio, MIO_LOG_INFO | MIO_LOG_UNTYPED, fmt, a1, a2, a3)
#define MIO_INFO4(mio,fmt,a1,a2,a3,a4) MIO_LOG4(mio, MIO_LOG_INFO | MIO_LOG_UNTYPED, fmt, a1, a2, a3, a4)
#define MIO_INFO5(mio,fmt,a1,a2,a3,a4,a5) MIO_LOG5(mio, MIO_LOG_INFO | MIO_LOG_UNTYPED, fmt, a1, a2, a3, a4, a5)
#define MIO_INFO6(mio,fmt,a1,a2,a3,a4,a5,a6) MIO_LOG6(mio, MIO_LOG_INFO | MIO_LOG_UNTYPED, fmt, a1, a2, a3, a4, a5, a6)

/* ========================================================================= */

enum mio_sys_mux_cmd_t
{
	MIO_SYS_MUX_CMD_INSERT = 0,
	MIO_SYS_MUX_CMD_UPDATE = 1,
	MIO_SYS_MUX_CMD_DELETE = 2
};
typedef enum mio_sys_mux_cmd_t mio_sys_mux_cmd_t;

typedef void (*mio_sys_mux_evtcb_t) (
	mio_t*                  mio,
	mio_dev_t*              dev,
	int                     events,
	int                     rdhup
);

typedef struct mio_sys_t mio_sys_t;

struct mio_t
{
	mio_oow_t    _instsize;
	mio_mmgr_t*  _mmgr;
	mio_cmgr_t*  _cmgr;
	mio_errnum_t errnum;
	struct
	{
		union
		{
			mio_ooch_t ooch[MIO_ERRMSG_CAPA];
			mio_bch_t bch[MIO_ERRMSG_CAPA];
			mio_uch_t uch[MIO_ERRMSG_CAPA];
		} tmpbuf;
		mio_ooch_t buf[MIO_ERRMSG_CAPA];
		mio_oow_t len;
	} errmsg;

	unsigned short int _shuterr;
	unsigned short int _fini_in_progress;

	struct
	{
		mio_bitmask_t trait;
		mio_bitmask_t log_mask;
		mio_oow_t log_maxcapa;
	} option;

	struct
	{
		mio_ooch_t* ptr;
		mio_oow_t len;
		mio_oow_t capa;
		mio_bitmask_t last_mask;
		mio_bitmask_t default_type_mask;
	} log;

	struct
	{
		struct
		{
			mio_ooch_t* ptr;
			mio_oow_t capa;
			mio_oow_t len;
		} xbuf; /* buffer to support sprintf */
	} sprintf;

	mio_stopreq_t stopreq;  /* stop request to abort mio_loop() */

	mio_cfmb_t cfmb; /* list head of cfmbs */
	mio_dev_t actdev; /* list head of active devices */
	mio_dev_t hltdev; /* list head of halted devices */
	mio_dev_t zmbdev; /* list head of zombie devices */

	mio_uint8_t bigbuf[65535]; /* TODO: make this dynamic depending on devices added. device may indicate a buffer size required??? */

	mio_ntime_t init_time;
	struct
	{
		mio_oow_t     capa;
		mio_oow_t     size;
		mio_tmrjob_t* jobs;
	} tmr;

	mio_cwq_t cwq;
	mio_cwq_t* cwqfl[MIO_CWQFL_SIZE]; /* list of free cwq objects */

	mio_svc_t actsvc; /* list head of active services */

	/* platform specific fields below */
	mio_sys_t* sysdep;
};

/* ========================================================================= */

#ifdef __cplusplus
extern "C" {
#endif

MIO_EXPORT mio_t* mio_open (
	mio_mmgr_t*   mmgr,
	mio_oow_t     xtnsize,
	mio_cmgr_t*   cmgr,
	mio_oow_t     tmrcapa,  /**< initial timer capacity */
	mio_errinf_t* errinf
);

MIO_EXPORT void mio_close (
	mio_t*        mio
);

MIO_EXPORT int mio_init (
	mio_t*       mio,
	mio_mmgr_t*  mmgr,
	mio_cmgr_t*  cmgr,
	mio_oow_t    tmrcapa
);

MIO_EXPORT void mio_fini (
	mio_t*       mio
);

MIO_EXPORT int mio_getoption (
	mio_t*        mio,
	mio_option_t  id,
	void*         value
);

MIO_EXPORT int mio_setoption (
	mio_t*       mio,
	mio_option_t id,
	const void*  value
);

#if defined(MIO_HAVE_INLINE)
static MIO_INLINE void* mio_getxtn (mio_t* mio) { return (void*)((mio_uint8_t*)mio + mio->_instsize); }
static MIO_INLINE mio_mmgr_t* mio_getmmgr (mio_t* mio) { return mio->_mmgr; }
static MIO_INLINE mio_cmgr_t* mio_getcmgr (mio_t* mio) { return mio->_cmgr; }
static MIO_INLINE void mio_setcmgr (mio_t* mio, mio_cmgr_t* cmgr) { mio->_cmgr = cmgr; }
static MIO_INLINE mio_errnum_t mio_geterrnum (mio_t* mio) { return mio->errnum; }
#else
#	define mio_getxtn(mio) ((void*)((mio_uint8_t*)mio + ((mio_t*)mio)->_instsize))
#	define mio_getmmgr(mio) (((mio_t*)(mio))->_mmgr)
#	define mio_getcmgr(mio) (((mio_t*)(mio))->_cmgr)
#	define mio_setcmgr(mio,cmgr) (((mio_t*)(mio))->_cmgr = (cmgr))
#	define mio_geterrnum(mio) (((mio_t*)(mio))->errnum)
#endif

MIO_EXPORT void mio_seterrnum (
	mio_t*       mio, 
	mio_errnum_t errnum
);

MIO_EXPORT void mio_seterrwithsyserr (
	mio_t* mio,
	int    syserr_type,
	int    syserr_code
);

MIO_EXPORT void mio_seterrbfmt (
	mio_t*           mio,
	mio_errnum_t     errnum,
	const mio_bch_t* fmt,
	...
);

MIO_EXPORT void mio_seterrufmt (
	mio_t*           mio,
	mio_errnum_t     errnum,
	const mio_uch_t* fmt,
	...
);

MIO_EXPORT void mio_seterrbfmtv (
	mio_t*           mio,
	mio_errnum_t     errnum,
	const mio_bch_t* fmt,
	va_list          ap
);

MIO_EXPORT void mio_seterrufmtv (
	mio_t*           mio,
	mio_errnum_t     errnum,
	const mio_uch_t* fmt,
	va_list          ap
);

MIO_EXPORT void mio_seterrbfmtwithsyserr (
	mio_t*           mio,
	int              syserr_type,
	int              syserr_code,
	const mio_bch_t* fmt,
	...
);

MIO_EXPORT void mio_seterrufmtwithsyserr (
	mio_t*           mio,
	int              syserr_type,
	int              syserr_code,
	const mio_uch_t* fmt,
	...
);

MIO_EXPORT const mio_ooch_t* mio_geterrstr (
	mio_t* mio
);

MIO_EXPORT const mio_ooch_t* mio_geterrmsg (
	mio_t* mio
);

MIO_EXPORT void mio_geterrinf (
	mio_t*        mio,
	mio_errinf_t* info
);

MIO_EXPORT const mio_ooch_t* mio_backuperrmsg (
	mio_t* mio
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



MIO_EXPORT mio_dev_t* mio_dev_make (
	mio_t*           mio,
	mio_oow_t        dev_size,
	mio_dev_mth_t*   dev_mth,
	mio_dev_evcb_t*  dev_evcb,
	void*            make_ctx
);

MIO_EXPORT void mio_dev_kill (
	mio_dev_t* dev
);

MIO_EXPORT void mio_dev_halt (
	mio_dev_t* dev
);

#if defined(MIO_HAVE_INLINE)
static MIO_INLINE mio_t* mio_dev_getmio (mio_dev_t* dev) { return dev->mio; }
#else
#	define mio_dev_getmio(dev) (((mio_dev_t*)(dev))->mio)
#endif

MIO_EXPORT int mio_dev_ioctl (
	mio_dev_t*  dev,
	int         cmd,
	void*       arg
);

MIO_EXPORT int mio_dev_watch (
	mio_dev_t*          dev,
	mio_dev_watch_cmd_t cmd,
	/** 0 or bitwise-ORed of #MIO_DEV_EVENT_IN and #MIO_DEV_EVENT_OUT */
	int                 events
);

MIO_EXPORT int mio_dev_read (
	mio_dev_t*         dev,
	int                enabled
);

/*
 * The mio_dev_timedread() function enables or disables the input watching.
 * If tmout is not MIO_NULL, it schedules to fire the on_read() callback
 * with the length of -1 and the mio error number set to MIO_ETMOUT.
 * If there is input before the time elapses, the scheduled timer job
 * is automaticaly cancelled. A call to mio_dev_read() or mio_dev_timedread()
 * with no timeout also cancels the unfired scheduled job.
 */
MIO_EXPORT int mio_dev_timedread (
	mio_dev_t*         dev,
	int                enabled,
	const mio_ntime_t* tmout
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
	const void*           data,
	mio_iolen_t           len,
	void*                 wrctx,
	const mio_devaddr_t*  dstaddr
);

MIO_EXPORT int mio_dev_writev (
	mio_dev_t*            dev,
	mio_iovec_t*          iov,
	mio_iolen_t           iovcnt,
	void*                 wrctx,
	const mio_devaddr_t*  dstaddr
);

MIO_EXPORT int mio_dev_timedwrite (
	mio_dev_t*           dev,
	const void*          data,
	mio_iolen_t          len,
	const mio_ntime_t*   tmout,
	void*                wrctx,
	const mio_devaddr_t* dstaddr
);


MIO_EXPORT int mio_dev_timedwritev (
	mio_dev_t*            dev,
	mio_iovec_t*          iov,
	mio_iolen_t           iovcnt,
	const mio_ntime_t*    tmout,
	void*                 wrctx,
	const mio_devaddr_t*  dstaddr
);

/* =========================================================================
 * SERVICE 
 * ========================================================================= */

#if defined(MIO_HAVE_INLINE)
static MIO_INLINE mio_t* mio_svc_getmio (mio_svc_t* svc) { return svc->mio; }
#else
#	define mio_svc_getmio(svc) (((mio_svc_t*)(svc))->mio)
#endif


/* =========================================================================
 * TIMER MANAGEMENT
 * ========================================================================= */

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

/* =========================================================================
 * TIME
 * ========================================================================= */

/**
 * the mio_gettime() function returns the elapsed time since mio initialization.
 */
MIO_EXPORT void mio_gettime (
	mio_t*            mio,
	mio_ntime_t*      now
);

/* =========================================================================
 * SYSTEM MEMORY MANAGEMENT FUCNTIONS VIA MMGR
 * ========================================================================= */
MIO_EXPORT void* mio_allocmem (
	mio_t*     mio,
	mio_oow_t  size
);

MIO_EXPORT void* mio_callocmem (
	mio_t*     mio,
	mio_oow_t  size
);

MIO_EXPORT void* mio_reallocmem (
	mio_t*      mio,
	void*       ptr,
	mio_oow_t   size
);

MIO_EXPORT void mio_freemem (
	mio_t*  mio,
	void*   ptr
);

MIO_EXPORT void mio_addcfmb (
	mio_t*             mio,
	mio_cfmb_t*        cfmb,
	mio_cfmb_checker_t checker
);

/* =========================================================================
 * STRING ENCODING CONVERSION
 * ========================================================================= */

#if defined(MIO_OOCH_IS_UCH)
#	define mio_convootobchars(mio,oocs,oocslen,bcs,bcslen) mio_convutobchars(mio,oocs,oocslen,bcs,bcslen)
#	define mio_convbtooochars(mio,bcs,bcslen,oocs,oocslen) mio_convbtouchars(mio,bcs,bcslen,oocs,oocslen)
#	define mio_convootobcstr(mio,oocs,oocslen,bcs,bcslen) mio_convutobcstr(mio,oocs,oocslen,bcs,bcslen)
#	define mio_convbtooocstr(mio,bcs,bcslen,oocs,oocslen) mio_convbtoucstr(mio,bcs,bcslen,oocs,oocslen)
#else
#	define mio_convootouchars(mio,oocs,oocslen,bcs,bcslen) mio_convbtouchars(mio,oocs,oocslen,bcs,bcslen)
#	define mio_convutooochars(mio,bcs,bcslen,oocs,oocslen) mio_convutobchars(mio,bcs,bcslen,oocs,oocslen)
#	define mio_convootoucstr(mio,oocs,oocslen,bcs,bcslen) mio_convbtoucstr(mio,oocs,oocslen,bcs,bcslen)
#	define mio_convutooocstr(mio,bcs,bcslen,oocs,oocslen) mio_convutobcstr(mio,bcs,bcslen,oocs,oocslen)
#endif

MIO_EXPORT int mio_convbtouchars (
	mio_t*           mio,
	const mio_bch_t* bcs,
	mio_oow_t*       bcslen,
	mio_uch_t*       ucs,
	mio_oow_t*       ucslen,
	int              all
);

MIO_EXPORT int mio_convutobchars (
	mio_t*           mio,
	const mio_uch_t* ucs,
	mio_oow_t*       ucslen,
	mio_bch_t*       bcs,
	mio_oow_t*       bcslen
);

/**
 * The mio_convbtoucstr() function converts a null-terminated byte string 
 * to a wide string.
 */
MIO_EXPORT int mio_convbtoucstr (
	mio_t*           mio,
	const mio_bch_t* bcs,
	mio_oow_t*       bcslen,
	mio_uch_t*       ucs,
	mio_oow_t*       ucslen,
	int              all
);


/**
 * The mio_convutobcstr() function converts a null-terminated wide string
 * to a byte string.
 */
MIO_EXPORT int mio_convutobcstr (
	mio_t*           mio,
	const mio_uch_t* ucs,
	mio_oow_t*       ucslen,
	mio_bch_t*       bcs,
	mio_oow_t*       bcslen
);


#if defined(MIO_OOCH_IS_UCH)
#	define mio_dupootobcharswithheadroom(mio,hrb,oocs,oocslen,bcslen) mio_duputobcharswithheadroom(mio,hrb,oocs,oocslen,bcslen)
#	define mio_dupbtooocharswithheadroom(mio,hrb,bcs,bcslen,oocslen,all) mio_dupbtoucharswithheadroom(mio,hrb,bcs,bcslen,oocslen,all)
#	define mio_dupootobchars(mio,oocs,oocslen,bcslen) mio_duputobchars(mio,oocs,oocslen,bcslen)
#	define mio_dupbtooochars(mio,bcs,bcslen,oocslen,all) mio_dupbtouchars(mio,bcs,bcslen,oocslen,all)

#	define mio_dupootobcstrwithheadroom(mio,hrb,oocs,bcslen) mio_duputobcstrwithheadroom(mio,hrb,oocs,bcslen)
#	define mio_dupbtooocstrwithheadroom(mio,hrb,bcs,oocslen,all) mio_dupbtoucstrwithheadroom(mio,hrb,bcs,oocslen,all)
#	define mio_dupootobcstr(mio,oocs,bcslen) mio_duputobcstr(mio,oocs,bcslen)
#	define mio_dupbtooocstr(mio,bcs,oocslen,all) mio_dupbtoucstr(mio,bcs,oocslen,all)
#else
#	define mio_dupootoucharswithheadroom(mio,hrb,oocs,oocslen,ucslen,all) mio_dupbtoucharswithheadroom(mio,hrb,oocs,oocslen,ucslen,all)
#	define mio_duputooocharswithheadroom(mio,hrb,ucs,ucslen,oocslen) mio_duputobcharswithheadroom(mio,hrb,ucs,ucslen,oocslen)
#	define mio_dupootouchars(mio,oocs,oocslen,ucslen,all) mio_dupbtouchars(mio,oocs,oocslen,ucslen,all)
#	define mio_duputooochars(mio,ucs,ucslen,oocslen) mio_duputobchars(mio,ucs,ucslen,oocslen)

#	define mio_dupootoucstrwithheadroom(mio,hrb,oocs,ucslen,all) mio_dupbtoucstrwithheadroom(mio,hrb,oocs,ucslen,all)
#	define mio_duputooocstrwithheadroom(mio,hrb,ucs,oocslen) mio_duputobcstrwithheadroom(mio,hrb,ucs,oocslen)
#	define mio_dupootoucstr(mio,oocs,ucslen,all) mio_dupbtoucstr(mio,oocs,ucslen,all)
#	define mio_duputooocstr(mio,ucs,oocslen) mio_duputobcstr(mio,ucs,oocslen)
#endif


MIO_EXPORT mio_uch_t* mio_dupbtoucharswithheadroom (
	mio_t*           mio,
	mio_oow_t        headroom_bytes,
	const mio_bch_t* bcs,
	mio_oow_t        bcslen,
	mio_oow_t*       ucslen,
	int              all
);

MIO_EXPORT mio_bch_t* mio_duputobcharswithheadroom (
	mio_t*           mio,
	mio_oow_t        headroom_bytes,
	const mio_uch_t* ucs,
	mio_oow_t        ucslen,
	mio_oow_t*       bcslen
);

MIO_EXPORT mio_uch_t* mio_dupbtouchars (
	mio_t*           mio,
	const mio_bch_t* bcs,
	mio_oow_t        bcslen,
	mio_oow_t*       ucslen,
	int              all
);

MIO_EXPORT mio_bch_t* mio_duputobchars (
	mio_t*           mio,
	const mio_uch_t* ucs,
	mio_oow_t        ucslen,
	mio_oow_t*       bcslen
);


MIO_EXPORT mio_uch_t* mio_dupbtoucstrwithheadroom (
	mio_t*           mio,
	mio_oow_t        headroom_bytes,
	const mio_bch_t* bcs,
	mio_oow_t*       ucslen,
	int              all
);

MIO_EXPORT mio_bch_t* mio_duputobcstrwithheadroom (
	mio_t*           mio,
	mio_oow_t        headroom_bytes,
	const mio_uch_t* ucs,
	mio_oow_t* bcslen
);

MIO_EXPORT mio_uch_t* mio_dupbtoucstr (
	mio_t*           mio,
	const mio_bch_t* bcs,
	mio_oow_t*       ucslen, /* optional: length of returned string */
	int              all
);

MIO_EXPORT mio_bch_t* mio_duputobcstr (
	mio_t*           mio,
	const mio_uch_t* ucs,
	mio_oow_t*       bcslen /* optional: length of returned string */
);



MIO_EXPORT mio_uch_t* mio_dupuchars (
	mio_t*           mio,
	const mio_uch_t* ucs,
	mio_oow_t        ucslen
);

MIO_EXPORT mio_bch_t* mio_dupbchars (
	mio_t*           mio,
	const mio_bch_t* bcs,
	mio_oow_t        bcslen
);


MIO_EXPORT mio_uch_t* mio_dupucstr (
	mio_t*           mio,
	const mio_uch_t* ucs,
	mio_oow_t*       ucslen /* [OUT] length*/
);

MIO_EXPORT mio_bch_t* mio_dupbcstr (
	mio_t*           mio,
	const mio_bch_t* bcs,
	mio_oow_t*       bcslen /* [OUT] length */
);

MIO_EXPORT mio_uch_t* mio_dupucstrs (
	mio_t*           mio,
	const mio_uch_t* ucs[],
	mio_oow_t*       ucslen
);

MIO_EXPORT mio_bch_t* mio_dupbcstrs (
	mio_t*           mio,
	const mio_bch_t* bcs[],
	mio_oow_t*       bcslen
);

#if defined(MIO_OOCH_IS_UCH)
#	define mio_dupoochars(mio,oocs,oocslen) mio_dupuchars(mio,oocs,oocslen)
#	define mio_dupoocstr(mio,oocs,oocslen) mio_dupucstr(mio,oocs,oocslen)
#else
#	define mio_dupoochars(mio,oocs,oocslen) mio_dupbchars(mio,oocs,oocslen)
#	define mio_dupoocstr(mio,oocs,oocslen) mio_dupbcstr(mio,oocs,oocslen)
#endif

/* =========================================================================
 * STRING FORMATTING
 * ========================================================================= */

MIO_EXPORT mio_oow_t mio_vfmttoucstr (
	mio_t*           mio,
	mio_uch_t*       buf,
	mio_oow_t        bufsz,
	const mio_uch_t* fmt,
	va_list          ap
);

MIO_EXPORT mio_oow_t mio_fmttoucstr (
	mio_t*           mio,
	mio_uch_t*       buf,
	mio_oow_t        bufsz,
	const mio_uch_t* fmt,
	...
);

MIO_EXPORT mio_oow_t mio_vfmttobcstr (
	mio_t*           mio,
	mio_bch_t*       buf,
	mio_oow_t        bufsz,
	const mio_bch_t* fmt,
	va_list          ap
);

MIO_EXPORT mio_oow_t mio_fmttobcstr (
	mio_t*           mio,
	mio_bch_t*       buf,
	mio_oow_t        bufsz,
	const mio_bch_t* fmt,
	...
);

#if defined(MIO_OOCH_IS_UCH)
#	define mio_vfmttooocstr mio_vfmttoucstr
#	define mio_fmttooocstr mio_fmttoucstr
#else
#	define mio_vfmttooocstr mio_vfmttobcstr
#	define mio_fmttooocstr mio_fmttobcstr
#endif

/* =========================================================================
 * MIO VM LOGGING
 * ========================================================================= */

MIO_EXPORT mio_ooi_t mio_logbfmt (
	mio_t*           mio,
	mio_bitmask_t    mask,
	const mio_bch_t* fmt,
	...
);

MIO_EXPORT mio_ooi_t mio_logufmt (
	mio_t*            mio,
	mio_bitmask_t     mask,
	const mio_uch_t*  fmt,
	...
);
 
#if defined(MIO_OOCH_IS_UCH)
#	define mio_logoofmt mio_logufmt
#else
#	define mio_logoofmt mio_logbfmt
#endif

/* =========================================================================
 * MISCELLANEOUS HELPER FUNCTIONS
 * ========================================================================= */

MIO_EXPORT const mio_ooch_t* mio_errnum_to_errstr (
	mio_errnum_t errnum
);

#ifdef __cplusplus
}
#endif


#endif
