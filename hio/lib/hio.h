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

#ifndef _HIO_H_
#define _HIO_H_

#include <hio-cmn.h>
#include <stdarg.h>

#if defined(_WIN32)
	typedef hio_uintptr_t hio_syshnd_t;
	#define HIO_SYSHND_INVALID (~(hio_uintptr_t)0)
#else
	typedef int hio_syshnd_t;
	#define HIO_SYSHND_INVALID (-1)
#endif

typedef struct hio_devaddr_t hio_devaddr_t;
struct hio_devaddr_t
{
	int   len;
	void* ptr;
};

#define HIO_ERRMSG_CAPA (2048)

/* [NOTE] ensure that it is a power of 2 */
#define HIO_LOG_CAPA_ALIGN 512

/* ========================================================================= */

typedef struct hio_t hio_t;
typedef struct hio_dev_t hio_dev_t;
typedef struct hio_dev_mth_t hio_dev_mth_t;
typedef struct hio_dev_evcb_t hio_dev_evcb_t;
typedef struct hio_svc_t hio_svc_t;

typedef struct hio_q_t hio_q_t;
typedef struct hio_wq_t hio_wq_t;
typedef struct hio_cwq_t hio_cwq_t;
typedef hio_intptr_t hio_iolen_t; /* NOTE: this is a signed type */

struct hio_iovec_t
{
	void*     iov_ptr;
	hio_oow_t iov_len;
};
typedef struct hio_iovec_t hio_iovec_t;

enum hio_errnum_t
{
	HIO_ENOERR,   /**< no error */
	HIO_EGENERIC, /**< generic error */

	HIO_ENOIMPL,  /**< not implemented */
	HIO_ESYSERR,  /**< system error */
	HIO_EINTERN,  /**< internal error */
	HIO_ESYSMEM,  /**< insufficient system memory */

	HIO_EINVAL,   /**< invalid parameter or data */
	HIO_ENOENT,   /**< data not found */
	HIO_EEXIST,   /**< existing/duplicate data */
	HIO_EBUSY,    /**< system busy */
	HIO_EACCES,   /**< access denied */
	HIO_EPERM,    /**< operation not permitted */
	HIO_ENOTDIR,  /**< not directory */
	HIO_EINTR,    /**< interrupted */
	HIO_EPIPE,    /**< pipe error */
	HIO_EAGAIN,   /**< resource temporarily unavailable */
	HIO_EBADHND,  /**< bad system handle */
	HIO_EBADRE,   /**< bad request or response */

	HIO_EMFILE,   /**< too many open files */
	HIO_ENFILE,   /**< too many open files */

	HIO_EIOERR,   /**< I/O error */
	HIO_EECERR,   /**< encoding conversion error */
	HIO_EECMORE,  /**< insufficient data for encoding conversion */
	HIO_EBUFFULL, /**< buffer full */

	HIO_ECONLOST, /**< connection lost */
	HIO_ECONRF,   /**< connection refused */
	HIO_ECONRS,   /**< connection reset */
	HIO_ENOCAPA,  /**< no capability */
	HIO_ETMOUT,   /**< timed out */
	HIO_ENORSP,   /**< no response */

	HIO_EDEVMAKE, /**< unable to make device */
	HIO_EDEVERR,  /**< device error */
	HIO_EDEVHUP   /**< device hang-up */
};
typedef enum hio_errnum_t hio_errnum_t;

struct hio_errinf_t
{
	hio_errnum_t num;
	hio_ooch_t msg[HIO_ERRMSG_CAPA];
};
typedef struct hio_errinf_t hio_errinf_t;

enum hio_feature_t
{
	HIO_FEATURE_MUX        = ((hio_bitmask_t)1 << 0),
	HIO_FEATURE_LOG        = ((hio_bitmask_t)1 << 1),
	HIO_FEATURE_LOG_WRITER = ((hio_bitmask_t)1 << 2),

	HIO_FEATURE_ALL = (HIO_FEATURE_MUX | HIO_FEATURE_LOG | HIO_FEATURE_LOG_WRITER)
};
typedef enum hio_feature_t hio_feature_t;

enum hio_option_t
{
	HIO_TRAIT,
	HIO_LOG_MASK,
	HIO_LOG_MAXCAPA,

	/* log target for the builtin writer */
	HIO_LOG_TARGET_BCSTR,
	HIO_LOG_TARGET_UCSTR,
	HIO_LOG_TARGET_BCS,
	HIO_LOG_TARGET_UCS,
#if defined(HIO_OOCH_IS_UCH)
#	define HIO_LOG_TARGET HIO_LOG_TARGET_UCSTR
#	define HIO_LOG_TARGET_OOCSTR HIO_LOG_TARGET_UCSTR
#	define HIO_LOG_TARGET_OOCS HIO_LOG_TARGET_UCS
#else
#	define HIO_LOG_TARGET HIO_LOG_TARGET_BCSTR
#	define HIO_LOG_TARGET_OOCSTR HIO_LOG_TARGET_BCSTR
#	define HIO_LOG_TARGET_OOCS HIO_LOG_TARGET_BCS
#endif

	/* user-defined log writer */
	HIO_LOG_WRITER
};
typedef enum hio_option_t hio_option_t;

enum hio_stopreq_t
{
	HIO_STOPREQ_NONE = 0,
	HIO_STOPREQ_TERMINATION,
	HIO_STOPREQ_WATCHER_ERROR
};
typedef enum hio_stopreq_t hio_stopreq_t;

/* ========================================================================= */

typedef int (*hio_log_writer_t) (
	hio_t*                 hio,
	hio_bitmask_t          mask,
	const hio_bch_t*       dptr,
	hio_oow_t              dlen
);	

/* ========================================================================= */

#define HIO_TMRIDX_INVALID ((hio_tmridx_t)-1)

typedef hio_oow_t hio_tmridx_t;

typedef struct hio_tmrjob_t hio_tmrjob_t;

typedef void (*hio_tmrjob_handler_t) (
	hio_t*             hio,
	const hio_ntime_t* now, 
	hio_tmrjob_t*      tmrjob
);

struct hio_tmrjob_t
{
	void*                 ctx;
	hio_ntime_t           when;
	hio_tmrjob_handler_t  handler;
	hio_tmridx_t*         idxptr; /* pointer to the index holder */
};

/* ========================================================================= */

struct hio_dev_mth_t
{
	/* ------------------------------------------------------------------ */
	/* mandatory. called in hio_dev_make() */
	int           (*make)         (hio_dev_t* dev, void* ctx); 

	/* ------------------------------------------------------------------ */
	/* mandatory. called in hio_dev_kill(). also called in hio_dev_make() upon
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
	int           (*kill)         (hio_dev_t* dev, int force); 

	/* optional. called if hio_dev_make() fails before the make() method is called */
	void          (*fail_before_make)     (void* ctx);

	/* ------------------------------------------------------------------ */
	hio_syshnd_t (*getsyshnd)      (hio_dev_t* dev); /* mandatory. called in hio_dev_make() after successful make() */
	int          (*issyshndbroken) (hio_dev_t* dev); /* the device whose underlying system handle can get closed before kill() must implement this */

	/* ------------------------------------------------------------------ */
	int           (*ioctl)        (hio_dev_t* dev, int cmd, void* arg);

	/* ------------------------------------------------------------------ */
	/* return -1 on failure, 0 if no data is availble, 1 otherwise.
	 * when returning 1, *len must be sent to the length of data read.
	 * if *len is set to 0, it's treated as EOF. */
	int           (*read)         (hio_dev_t* dev, void* data, hio_iolen_t* len, hio_devaddr_t* srcaddr);

	/* ------------------------------------------------------------------ */
	int           (*write)        (hio_dev_t* dev, const void* data, hio_iolen_t* len, const hio_devaddr_t* dstaddr);
	int           (*writev)       (hio_dev_t* dev, const hio_iovec_t* iov, hio_iolen_t* iovcnt, const hio_devaddr_t* dstaddr);
	int           (*sendfile)     (hio_dev_t* dev, hio_syshnd_t in_fd, hio_foff_t foff, hio_iolen_t* len);
};

struct hio_dev_evcb_t
{
	/* return -1 on failure. 0 or 1 on success.
	 * when 0 is returned, it doesn't attempt to perform actual I/O.
	 * when 1 is returned, it attempts to perform actual I/O. */
	int           (*ready)        (hio_dev_t* dev, int events);

	/* return -1 on failure, 0 or 1 on success.
	 * when 0 is returned, the main loop stops the attempt to read more data.
	 * when 1 is returned, the main loop attempts to read more data without*/
	int           (*on_read)      (hio_dev_t* dev, const void* data, hio_iolen_t len, const hio_devaddr_t* srcaddr);

	/* return -1 on failure, 0 on success. 
	 * wrlen is the length of data written. it is the length of the originally
	 * posted writing request for a stream device. For a non stream device, it
	 * may be shorter than the originally posted length. */
	int           (*on_write)     (hio_dev_t* dev, hio_iolen_t wrlen, void* wrctx, const hio_devaddr_t* dstaddr);
};

struct hio_q_t
{
	hio_q_t*    q_next;
	hio_q_t*    q_prev;
};

#define HIO_Q_INIT(q) ((q)->q_next = (q)->q_prev = (q))
#define HIO_Q_TAIL(q) ((q)->q_prev)
#define HIO_Q_HEAD(q) ((q)->q_next)
#define HIO_Q_IS_EMPTY(q) (HIO_Q_HEAD(q) == (q))
#define HIO_Q_IS_NODE(q,x) ((q) != (x))
#define HIO_Q_IS_HEAD(q,x) (HIO_Q_HEAD(q) == (x))
#define HIO_Q_IS_TAIL(q,x) (HIO_Q_TAIL(q) == (x))

#define HIO_Q_NEXT(x) ((x)->q_next)
#define HIO_Q_PREV(x) ((x)->q_prev)

#define HIO_Q_LINK(p,x,n) do { \
	hio_q_t* __pp = (p), * __nn = (n); \
	(x)->q_prev = (p); \
	(x)->q_next = (n); \
	__nn->q_prev = (x); \
	__pp->q_next = (x); \
} while (0)

#define HIO_Q_UNLINK(x) do { \
	hio_q_t* __pp = (x)->q_prev, * __nn = (x)->q_next; \
	__nn->q_prev = __pp; __pp->q_next = __nn; \
} while (0)

#define HIO_Q_REPL(o,n) do { \
	hio_q_t* __oo = (o), * __nn = (n); \
	__nn->q_next = __oo->q_next; \
	__nn->q_next->q_prev = __nn; \
	__nn->q_prev = __oo->q_prev; \
	__nn->q_prev->q_next = __nn; \
} while (0)

/* insert an item at the back of the queue */
/*#define HIO_Q_ENQ(wq,x)  HIO_Q_LINK(HIO_Q_TAIL(wq), x, HIO_Q_TAIL(wq)->next)*/
#define HIO_Q_ENQ(wq,x)  HIO_Q_LINK(HIO_Q_TAIL(wq), x, wq)

/* remove an item in the front from the queue */
#define HIO_Q_DEQ(wq) HIO_Q_UNLINK(HIO_Q_HEAD(wq))

/* completed write queue */
struct hio_cwq_t
{
	hio_cwq_t*    q_next;
	hio_cwq_t*    q_prev;

	hio_iolen_t   olen; 
	void*         ctx;
	hio_dev_t*    dev;
	hio_devaddr_t dstaddr;
};

#define HIO_CWQ_INIT(cwq) ((cwq)->q_next = (cwq)->q_prev = (cwq))
#define HIO_CWQ_TAIL(cwq) ((cwq)->q_prev)
#define HIO_CWQ_HEAD(cwq) ((cwq)->q_next)
#define HIO_CWQ_IS_EMPTY(cwq) (HIO_CWQ_HEAD(cwq) == (cwq))
#define HIO_CWQ_IS_NODE(cwq,x) ((cwq) != (x))
#define HIO_CWQ_IS_HEAD(cwq,x) (HIO_CWQ_HEAD(cwq) == (x))
#define HIO_CWQ_IS_TAIL(cwq,x) (HIO_CWQ_TAIL(cwq) == (x))
#define HIO_CWQ_NEXT(x) ((x)->q_next)
#define HIO_CWQ_PREV(x) ((x)->q_prev)
#define HIO_CWQ_LINK(p,x,n) HIO_Q_LINK((hio_q_t*)p,(hio_q_t*)x,(hio_q_t*)n)
#define HIO_CWQ_UNLINK(x) HIO_Q_UNLINK((hio_q_t*)x)
#define HIO_CWQ_REPL(o,n) HIO_Q_REPL(o,n);
#define HIO_CWQ_ENQ(cwq,x) HIO_CWQ_LINK(HIO_CWQ_TAIL(cwq), (hio_q_t*)x, cwq)
#define HIO_CWQ_DEQ(cwq) HIO_CWQ_UNLINK(HIO_CWQ_HEAD(cwq))

/* write queue */
struct hio_wq_t
{
	hio_wq_t*       q_next;
	hio_wq_t*       q_prev;

	int             sendfile;
	hio_iolen_t     olen; /* original data length */
	hio_uint8_t*    ptr;  /* pointer to data */
	hio_iolen_t     len;  /* remaining data length */
	void*           ctx;
	hio_dev_t*      dev; /* back-pointer to the device */

	hio_tmridx_t    tmridx;
	hio_devaddr_t   dstaddr;
};

#define HIO_WQ_INIT(wq) ((wq)->q_next = (wq)->q_prev = (wq))
#define HIO_WQ_TAIL(wq) ((wq)->q_prev)
#define HIO_WQ_HEAD(wq) ((wq)->q_next)
#define HIO_WQ_IS_EMPTY(wq) (HIO_WQ_HEAD(wq) == (wq))
#define HIO_WQ_IS_NODE(wq,x) ((wq) != (x))
#define HIO_WQ_IS_HEAD(wq,x) (HIO_WQ_HEAD(wq) == (x))
#define HIO_WQ_IS_TAIL(wq,x) (HIO_WQ_TAIL(wq) == (x))
#define HIO_WQ_NEXT(x) ((x)->q_next)
#define HIO_WQ_PREV(x) ((x)->q_prev)
#define HIO_WQ_LINK(p,x,n) HIO_Q_LINK((hio_q_t*)p,(hio_q_t*)x,(hio_q_t*)n)
#define HIO_WQ_UNLINK(x) HIO_Q_UNLINK((hio_q_t*)x)
#define HIO_WQ_REPL(o,n) HIO_Q_REPL(o,n);
#define HIO_WQ_ENQ(wq,x) HIO_WQ_LINK(HIO_WQ_TAIL(wq), (hio_q_t*)x, wq)
#define HIO_WQ_DEQ(wq) HIO_WQ_UNLINK(HIO_WQ_HEAD(wq))

#define HIO_DEV_HEADER \
	hio_t*          hio; \
	hio_oow_t       dev_size; \
	hio_bitmask_t   dev_cap; \
	hio_dev_mth_t*  dev_mth; \
	hio_dev_evcb_t* dev_evcb; \
	hio_ntime_t     rtmout; \
	hio_tmridx_t    rtmridx; \
	hio_wq_t        wq; \
	hio_oow_t       cw_count; \
	hio_dev_t*      dev_prev; \
	hio_dev_t*      dev_next 

struct hio_dev_t
{
	HIO_DEV_HEADER;
};

#define HIO_DEVL_PREPEND_DEV(lh,dev) do { \
	(dev)->dev_prev = (lh); \
	(dev)->dev_next = (lh)->dev_next; \
	(dev)->dev_next->dev_prev = (dev); \
	(lh)->dev_next = (dev); \
} while(0)

#define HIO_DEVL_APPEND_DEV(lh,dev) do { \
	(dev)->dev_next = (lh); \
	(dev)->dev_prev = (lh)->dev_prev; \
	(dev)->dev_prev->dev_next = (dev); \
	(lh)->dev_prev = (dev); \
} while(0)

#define HIO_DEVL_UNLINK_DEV(dev) do { \
	(dev)->dev_prev->dev_next = (dev)->dev_next; \
	(dev)->dev_next->dev_prev = (dev)->dev_prev; \
} while (0)

#define HIO_DEVL_INIT(lh) ((lh)->dev_next = (lh)->dev_prev = lh)
#define HIO_DEVL_FIRST_DEV(lh) ((lh)->dev_next)
#define HIO_DEVL_LAST_DEV(lh) ((lh)->dev_prev)
#define HIO_DEVL_IS_EMPTY(lh) (HIO_DEVL_FIRST_DEV(lh) == (lh))
#define HIO_DEVL_IS_NIL_DEV(lh,dev) ((dev) == (lh))

enum hio_dev_cap_t
{
	/* don't forget to update hio_dev_cap_to_bcstr()
	 * when you add/delete these enumerators */

	HIO_DEV_CAP_VIRTUAL         = ((hio_bitmask_t)1 << 0),
	HIO_DEV_CAP_IN              = ((hio_bitmask_t)1 << 1),
	HIO_DEV_CAP_OUT             = ((hio_bitmask_t)1 << 2),
	HIO_DEV_CAP_PRI             = ((hio_bitmask_t)1 << 3), /* meaningful only if #HIO_DEV_CAP_IN is set */
	HIO_DEV_CAP_STREAM          = ((hio_bitmask_t)1 << 4), /* byte stream */


	HIO_DEV_CAP_IN_DISABLED     = ((hio_bitmask_t)1 << 5),
	HIO_DEV_CAP_OUT_UNQUEUEABLE = ((hio_bitmask_t)1 << 6),
	HIO_DEV_CAP_ALL_MASK        = (HIO_DEV_CAP_VIRTUAL | HIO_DEV_CAP_IN | HIO_DEV_CAP_OUT | HIO_DEV_CAP_PRI | HIO_DEV_CAP_STREAM | HIO_DEV_CAP_IN_DISABLED | HIO_DEV_CAP_OUT_UNQUEUEABLE),

	/* -------------------------------------------------------------------
	 * the followings bits are for internal use only. 
	 * never set these bits to the dev_cap field.
	 * ------------------------------------------------------------------- */
	HIO_DEV_CAP_IN_CLOSED       = ((hio_bitmask_t)1 << 10),
	HIO_DEV_CAP_OUT_CLOSED      = ((hio_bitmask_t)1 << 11),
	HIO_DEV_CAP_IN_WATCHED      = ((hio_bitmask_t)1 << 12),
	HIO_DEV_CAP_OUT_WATCHED     = ((hio_bitmask_t)1 << 13),
	HIO_DEV_CAP_PRI_WATCHED     = ((hio_bitmask_t)1 << 14), /**< can be set only if HIO_DEV_CAP_IN_WATCHED is set */
	HIO_DEV_CAP_ACTIVE          = ((hio_bitmask_t)1 << 15),
	HIO_DEV_CAP_HALTED          = ((hio_bitmask_t)1 << 16),
	HIO_DEV_CAP_ZOMBIE          = ((hio_bitmask_t)1 << 17),
	HIO_DEV_CAP_RENEW_REQUIRED  = ((hio_bitmask_t)1 << 18),
	HIO_DEV_CAP_WATCH_STARTED   = ((hio_bitmask_t)1 << 19),
	HIO_DEV_CAP_WATCH_SUSPENDED = ((hio_bitmask_t)1 << 20),
	HIO_DEV_CAP_WATCH_REREG_REQUIRED = ((hio_bitmask_t)1 << 21), 
};
typedef enum hio_dev_cap_t hio_dev_cap_t;

enum hio_dev_watch_cmd_t
{
	HIO_DEV_WATCH_START,
	HIO_DEV_WATCH_UPDATE,
	HIO_DEV_WATCH_RENEW, /* automatic renewal */
	HIO_DEV_WATCH_STOP
};
typedef enum hio_dev_watch_cmd_t hio_dev_watch_cmd_t;

enum hio_dev_event_t
{
	HIO_DEV_EVENT_IN  = (1 << 0),
	HIO_DEV_EVENT_OUT = (1 << 1),

	HIO_DEV_EVENT_PRI = (1 << 2),
	HIO_DEV_EVENT_HUP = (1 << 3),
	HIO_DEV_EVENT_ERR = (1 << 4)
};
typedef enum hio_dev_event_t hio_dev_event_t;

#define HIO_CWQFL_SIZE 16
#define HIO_CWQFL_ALIGN 16


/* =========================================================================
 * CHECK-AND-FREE MEMORY BLOCK
 * ========================================================================= */

#define HIO_CFMB_HEADER \
	hio_t* hio; \
	hio_cfmb_t* cfmb_next; \
	hio_cfmb_t* cfmb_prev; \
	hio_cfmb_checker_t cfmb_checker

typedef struct hio_cfmb_t hio_cfmb_t;

typedef int (*hio_cfmb_checker_t) (
	hio_t*       hio,
	hio_cfmb_t*  cfmb
);

struct hio_cfmb_t
{
	HIO_CFMB_HEADER;
};

#define HIO_CFMBL_PREPEND_CFMB(lh,cfmb) do { \
	(cfmb)->cfmb_prev = (lh); \
	(cfmb)->cfmb_next = (lh)->cfmb_next; \
	(cfmb)->cfmb_next->cfmb_prev = (cfmb); \
	(lh)->cfmb_next = (cfmb); \
} while(0)

#define HIO_CFMBL_APPEND_CFMB(lh,cfmb) do { \
	(cfmb)->cfmb_next = (lh); \
	(cfmb)->cfmb_prev = (lh)->cfmb_prev; \
	(cfmb)->cfmb_prev->cfmb_next = (cfmb); \
	(lh)->cfmb_prev = (cfmb); \
} while(0)

#define HIO_CFMBL_UNLINK_CFMB(cfmb) do { \
	(cfmb)->cfmb_prev->cfmb_next = (cfmb)->cfmb_next; \
	(cfmb)->cfmb_next->cfmb_prev = (cfmb)->cfmb_prev; \
} while (0)

#define HIO_CFMBL_INIT(lh) ((lh)->cfmb_next = (lh)->cfmb_prev = lh)
#define HIO_CFMBL_FIRST_CFMB(lh) ((lh)->cfmb_next)
#define HIO_CFMBL_LAST_CFMB(lh) ((lh)->cfmb_prev)
#define HIO_CFMBL_IS_EMPTY(lh) (HIO_CFMBL_FIRST_CFMB(lh) == (lh))
#define HIO_CFMBL_IS_NIL_CFMB(lh,cfmb) ((cfmb) == (lh))

#define HIO_CFMBL_PREV_CFMB(cfmb) ((cfmb)->cfmb_prev)
#define HIO_CFMBL_NEXT_CFMB(cfmb) ((cfmb)->cfmb_next)
/* =========================================================================
 * SERVICE 
 * ========================================================================= */

typedef void (*hio_svc_stop_t) (hio_svc_t* svc);

#define HIO_SVC_HEADER \
	hio_t*          hio; \
	hio_svc_stop_t  svc_stop; \
	hio_svc_t*      svc_prev; \
	hio_svc_t*      svc_next 

/* the stop callback is called if it's not NULL and the service is still 
 * alive when hio_close() is reached. it still calls HIO_SVCL_UNLINK_SVC()
 * if the stop callback is NULL. The stop callback, if specified, must
 * call HIO_SVCL_UNLINK_SVC(). */ 

struct hio_svc_t
{
	HIO_SVC_HEADER;
};

#define HIO_SVCL_PREPEND_SVC(lh,svc) do { \
	(svc)->svc_prev = (lh); \
	(svc)->svc_next = (lh)->svc_next; \
	(svc)->svc_next->svc_prev = (svc); \
	(lh)->svc_next = (svc); \
} while(0)

#define HIO_SVCL_APPEND_SVC(lh,svc) do { \
	(svc)->svc_next = (lh); \
	(svc)->svc_prev = (lh)->svc_prev; \
	(svc)->svc_prev->svc_next = (svc); \
	(lh)->svc_prev = (svc); \
} while(0)

#define HIO_SVCL_UNLINK_SVC(svc) do { \
	(svc)->svc_prev->svc_next = (svc)->svc_next; \
	(svc)->svc_next->svc_prev = (svc)->svc_prev; \
} while (0)


#define HIO_SVCL_INIT(lh) ((lh)->svc_next = (lh)->svc_prev = lh)
#define HIO_SVCL_FIRST_SVC(lh) ((lh)->svc_next)
#define HIO_SVCL_LAST_SVC(lh) ((lh)->svc_prev)
#define HIO_SVCL_IS_EMPTY(lh) (HIO_SVCL_FIRST_SVC(lh) == (lh))
#define HIO_SVCL_IS_NIL_SVC(lh,svc) ((svc) == (lh))

#define HIO_SVCL_PREV_SVC(svc) ((svc)->svc_prev)
#define HIO_SVCL_NEXT_SVC(svc) ((svc)->svc_next)
/* =========================================================================
 * MIO LOGGING
 * ========================================================================= */

enum hio_log_mask_t
{
	HIO_LOG_DEBUG      = ((hio_bitmask_t)1 << 0),
	HIO_LOG_INFO       = ((hio_bitmask_t)1 << 1),
	HIO_LOG_WARN       = ((hio_bitmask_t)1 << 2),
	HIO_LOG_ERROR      = ((hio_bitmask_t)1 << 3),
	HIO_LOG_FATAL      = ((hio_bitmask_t)1 << 4),

	HIO_LOG_UNTYPED    = ((hio_bitmask_t)1 << 6), /* only to be used by HIO_DEBUGx() and HIO_INFOx() */
	HIO_LOG_CORE       = ((hio_bitmask_t)1 << 7),
	HIO_LOG_DEV        = ((hio_bitmask_t)1 << 8),
	HIO_LOG_TIMER      = ((hio_bitmask_t)1 << 9),

	HIO_LOG_ALL_LEVELS = (HIO_LOG_DEBUG  | HIO_LOG_INFO | HIO_LOG_WARN | HIO_LOG_ERROR | HIO_LOG_FATAL),
	HIO_LOG_ALL_TYPES  = (HIO_LOG_UNTYPED | HIO_LOG_CORE | HIO_LOG_DEV | HIO_LOG_TIMER),

	HIO_LOG_GUARDED    = ((hio_bitmask_t)1 << 13), /* make logging thread-safe */
	HIO_LOG_STDOUT     = ((hio_bitmask_t)1 << 14), /* write log messages to stdout without timestamp. HIO_LOG_STDOUT wins over HIO_LOG_STDERR. */
	HIO_LOG_STDERR     = ((hio_bitmask_t)1 << 15)  /* write log messages to stderr without timestamp. */
};
typedef enum hio_log_mask_t hio_log_mask_t;

/* all bits must be set to get enabled */
#define HIO_LOG_ENABLED(hio,mask) (((hio)->option.log_mask & (mask)) == (mask))

#define HIO_LOG0(hio,mask,fmt) do { if (HIO_LOG_ENABLED(hio,mask)) hio_logbfmt(hio, mask, fmt); } while(0)
#define HIO_LOG1(hio,mask,fmt,a1) do { if (HIO_LOG_ENABLED(hio,mask)) hio_logbfmt(hio, mask, fmt, a1); } while(0)
#define HIO_LOG2(hio,mask,fmt,a1,a2) do { if (HIO_LOG_ENABLED(hio,mask)) hio_logbfmt(hio, mask, fmt, a1, a2); } while(0)
#define HIO_LOG3(hio,mask,fmt,a1,a2,a3) do { if (HIO_LOG_ENABLED(hio,mask)) hio_logbfmt(hio, mask, fmt, a1, a2, a3); } while(0)
#define HIO_LOG4(hio,mask,fmt,a1,a2,a3,a4) do { if (HIO_LOG_ENABLED(hio,mask)) hio_logbfmt(hio, mask, fmt, a1, a2, a3, a4); } while(0)
#define HIO_LOG5(hio,mask,fmt,a1,a2,a3,a4,a5) do { if (HIO_LOG_ENABLED(hio,mask)) hio_logbfmt(hio, mask, fmt, a1, a2, a3, a4, a5); } while(0)
#define HIO_LOG6(hio,mask,fmt,a1,a2,a3,a4,a5,a6) do { if (HIO_LOG_ENABLED(hio,mask)) hio_logbfmt(hio, mask, fmt, a1, a2, a3, a4, a5, a6); } while(0)
#define HIO_LOG7(hio,mask,fmt,a1,a2,a3,a4,a5,a6,a7) do { if (HIO_LOG_ENABLED(hio,mask)) hio_logbfmt(hio, mask, fmt, a1, a2, a3, a4, a5, a6, a7); } while(0)
#define HIO_LOG8(hio,mask,fmt,a1,a2,a3,a4,a5,a6,a7,a8) do { if (HIO_LOG_ENABLED(hio,mask)) hio_logbfmt(hio, mask, fmt, a1, a2, a3, a4, a5, a6, a7, a8); } while(0)
#define HIO_LOG9(hio,mask,fmt,a1,a2,a3,a4,a5,a6,a7,a8,a9) do { if (HIO_LOG_ENABLED(hio,mask)) hio_logbfmt(hio, mask, fmt, a1, a2, a3, a4, a5, a6, a7, a8, a9); } while(0)

#if defined(HIO_BUILD_RELEASE)
	/* [NOTE]
	 *  get rid of debugging message totally regardless of
	 *  the log mask in the release build.
	 */
#	define HIO_DEBUG0(hio,fmt)
#	define HIO_DEBUG1(hio,fmt,a1)
#	define HIO_DEBUG2(hio,fmt,a1,a2)
#	define HIO_DEBUG3(hio,fmt,a1,a2,a3)
#	define HIO_DEBUG4(hio,fmt,a1,a2,a3,a4)
#	define HIO_DEBUG5(hio,fmt,a1,a2,a3,a4,a5)
#	define HIO_DEBUG6(hio,fmt,a1,a2,a3,a4,a5,a6)
#	define HIO_DEBUG7(hio,fmt,a1,a2,a3,a4,a5,a6,a7)
#	define HIO_DEBUG8(hio,fmt,a1,a2,a3,a4,a5,a6,a7,a8)
#	define HIO_DEBUG9(hio,fmt,a1,a2,a3,a4,a5,a6,a7,a8,a9)
#else
#	define HIO_DEBUG0(hio,fmt) HIO_LOG0(hio, HIO_LOG_DEBUG | HIO_LOG_UNTYPED, fmt)
#	define HIO_DEBUG1(hio,fmt,a1) HIO_LOG1(hio, HIO_LOG_DEBUG | HIO_LOG_UNTYPED, fmt, a1)
#	define HIO_DEBUG2(hio,fmt,a1,a2) HIO_LOG2(hio, HIO_LOG_DEBUG | HIO_LOG_UNTYPED, fmt, a1, a2)
#	define HIO_DEBUG3(hio,fmt,a1,a2,a3) HIO_LOG3(hio, HIO_LOG_DEBUG | HIO_LOG_UNTYPED, fmt, a1, a2, a3)
#	define HIO_DEBUG4(hio,fmt,a1,a2,a3,a4) HIO_LOG4(hio, HIO_LOG_DEBUG | HIO_LOG_UNTYPED, fmt, a1, a2, a3, a4)
#	define HIO_DEBUG5(hio,fmt,a1,a2,a3,a4,a5) HIO_LOG5(hio, HIO_LOG_DEBUG | HIO_LOG_UNTYPED, fmt, a1, a2, a3, a4, a5)
#	define HIO_DEBUG6(hio,fmt,a1,a2,a3,a4,a5,a6) HIO_LOG6(hio, HIO_LOG_DEBUG | HIO_LOG_UNTYPED, fmt, a1, a2, a3, a4, a5, a6)
#	define HIO_DEBUG7(hio,fmt,a1,a2,a3,a4,a5,a6,a7) HIO_LOG7(hio, HIO_LOG_DEBUG | HIO_LOG_UNTYPED, fmt, a1, a2, a3, a4, a5, a6, a7)
#	define HIO_DEBUG8(hio,fmt,a1,a2,a3,a4,a5,a6,a7,a8) HIO_LOG8(hio, HIO_LOG_DEBUG | HIO_LOG_UNTYPED, fmt, a1, a2, a3, a4, a5, a6, a7, a8)
#	define HIO_DEBUG9(hio,fmt,a1,a2,a3,a4,a5,a6,a7,a8,a9) HIO_LOG9(hio, HIO_LOG_DEBUG | HIO_LOG_UNTYPED, fmt, a1, a2, a3, a4, a5, a6, a7, a8, a9)
#endif

#define HIO_INFO0(hio,fmt) HIO_LOG0(hio, HIO_LOG_INFO | HIO_LOG_UNTYPED, fmt)
#define HIO_INFO1(hio,fmt,a1) HIO_LOG1(hio, HIO_LOG_INFO | HIO_LOG_UNTYPED, fmt, a1)
#define HIO_INFO2(hio,fmt,a1,a2) HIO_LOG2(hio, HIO_LOG_INFO | HIO_LOG_UNTYPED, fmt, a1, a2)
#define HIO_INFO3(hio,fmt,a1,a2,a3) HIO_LOG3(hio, HIO_LOG_INFO | HIO_LOG_UNTYPED, fmt, a1, a2, a3)
#define HIO_INFO4(hio,fmt,a1,a2,a3,a4) HIO_LOG4(hio, HIO_LOG_INFO | HIO_LOG_UNTYPED, fmt, a1, a2, a3, a4)
#define HIO_INFO5(hio,fmt,a1,a2,a3,a4,a5) HIO_LOG5(hio, HIO_LOG_INFO | HIO_LOG_UNTYPED, fmt, a1, a2, a3, a4, a5)
#define HIO_INFO6(hio,fmt,a1,a2,a3,a4,a5,a6) HIO_LOG6(hio, HIO_LOG_INFO | HIO_LOG_UNTYPED, fmt, a1, a2, a3, a4, a5, a6)
#define HIO_INFO7(hio,fmt,a1,a2,a3,a4,a5,a6,a7) HIO_LOG7(hio, HIO_LOG_INFO | HIO_LOG_UNTYPED, fmt, a1, a2, a3, a4, a5, a6, a7)
#define HIO_INFO8(hio,fmt,a1,a2,a3,a4,a5,a6,a7,a8) HIO_LOG7(hio, HIO_LOG_INFO | HIO_LOG_UNTYPED, fmt, a1, a2, a3, a4, a5, a6, a7, a8)
#define HIO_INFO9(hio,fmt,a1,a2,a3,a4,a5,a6,a7,a8,a9) HIO_LOG7(hio, HIO_LOG_INFO | HIO_LOG_UNTYPED, fmt, a1, a2, a3, a4, a5, a6, a7, a8, a9)

#define HIO_WARN0(hio,fmt) HIO_LOG0(hio, HIO_LOG_WARN | HIO_LOG_UNTYPED, fmt)
#define HIO_WARN1(hio,fmt,a1) HIO_LOG1(hio, HIO_LOG_WARN | HIO_LOG_UNTYPED, fmt, a1)
#define HIO_WARN2(hio,fmt,a1,a2) HIO_LOG2(hio, HIO_LOG_WARN | HIO_LOG_UNTYPED, fmt, a1, a2)
#define HIO_WARN3(hio,fmt,a1,a2,a3) HIO_LOG3(hio, HIO_LOG_WARN | HIO_LOG_UNTYPED, fmt, a1, a2, a3)
#define HIO_WARN4(hio,fmt,a1,a2,a3,a4) HIO_LOG4(hio, HIO_LOG_WARN | HIO_LOG_UNTYPED, fmt, a1, a2, a3, a4)
#define HIO_WARN5(hio,fmt,a1,a2,a3,a4,a5) HIO_LOG5(hio, HIO_LOG_WARN | HIO_LOG_UNTYPED, fmt, a1, a2, a3, a4, a5)
#define HIO_WARN6(hio,fmt,a1,a2,a3,a4,a5,a6) HIO_LOG6(hio, HIO_LOG_WARN | HIO_LOG_UNTYPED, fmt, a1, a2, a3, a4, a5, a6)
#define HIO_WARN7(hio,fmt,a1,a2,a3,a4,a5,a6,a7) HIO_LOG7(hio, HIO_LOG_WARN | HIO_LOG_UNTYPED, fmt, a1, a2, a3, a4, a5, a6, a7)
#define HIO_WARN8(hio,fmt,a1,a2,a3,a4,a5,a6,a7,a8) HIO_LOG8(hio, HIO_LOG_WARN | HIO_LOG_UNTYPED, fmt, a1, a2, a3, a4, a5, a6, a7, a8)
#define HIO_WARN9(hio,fmt,a1,a2,a3,a4,a5,a6,a7,a8,a9) HIO_LOG9(hio, HIO_LOG_WARN | HIO_LOG_UNTYPED, fmt, a1, a2, a3, a4, a5, a6, a7, a8, a9)

#define HIO_ERROR0(hio,fmt) HIO_LOG0(hio, HIO_LOG_ERROR | HIO_LOG_UNTYPED, fmt)
#define HIO_ERROR1(hio,fmt,a1) HIO_LOG1(hio, HIO_LOG_ERROR | HIO_LOG_UNTYPED, fmt, a1)
#define HIO_ERROR2(hio,fmt,a1,a2) HIO_LOG2(hio, HIO_LOG_ERROR | HIO_LOG_UNTYPED, fmt, a1, a2)
#define HIO_ERROR3(hio,fmt,a1,a2,a3) HIO_LOG3(hio, HIO_LOG_ERROR | HIO_LOG_UNTYPED, fmt, a1, a2, a3)
#define HIO_ERROR4(hio,fmt,a1,a2,a3,a4) HIO_LOG4(hio, HIO_LOG_ERROR | HIO_LOG_UNTYPED, fmt, a1, a2, a3, a4)
#define HIO_ERROR5(hio,fmt,a1,a2,a3,a4,a5) HIO_LOG5(hio, HIO_LOG_ERROR | HIO_LOG_UNTYPED, fmt, a1, a2, a3, a4, a5)
#define HIO_ERROR6(hio,fmt,a1,a2,a3,a4,a5,a6) HIO_LOG6(hio, HIO_LOG_ERROR | HIO_LOG_UNTYPED, fmt, a1, a2, a3, a4, a5, a6)
#define HIO_ERROR7(hio,fmt,a1,a2,a3,a4,a5,a6,a7) HIO_LOG7(hio, HIO_LOG_ERROR | HIO_LOG_UNTYPED, fmt, a1, a2, a3, a4, a5, a6, a7)
#define HIO_ERROR8(hio,fmt,a1,a2,a3,a4,a5,a6,a7,a8) HIO_LOG8(hio, HIO_LOG_ERROR | HIO_LOG_UNTYPED, fmt, a1, a2, a3, a4, a5, a6, a7, a8)
#define HIO_ERROR9(hio,fmt,a1,a2,a3,a4,a5,a6,a7,a8,a9) HIO_LOG9(hio, HIO_LOG_ERROR | HIO_LOG_UNTYPED, fmt, a1, a2, a3, a4, a5, a6, a7, a8, a9)

/* ========================================================================= */

enum hio_sys_mux_cmd_t
{
	HIO_SYS_MUX_CMD_INSERT = 0,
	HIO_SYS_MUX_CMD_UPDATE = 1,
	HIO_SYS_MUX_CMD_DELETE = 2
};
typedef enum hio_sys_mux_cmd_t hio_sys_mux_cmd_t;

typedef void (*hio_sys_mux_evtcb_t) (
	hio_t*                  hio,
	hio_dev_t*              dev,
	int                     events,
	int                     rdhup
);

typedef struct hio_sys_t hio_sys_t;

struct hio_t
{
	hio_oow_t    _instsize;
	hio_mmgr_t*  _mmgr;
	hio_cmgr_t*  _cmgr;
	hio_errnum_t errnum;
	struct
	{
		union
		{
			hio_ooch_t ooch[HIO_ERRMSG_CAPA];
			hio_bch_t bch[HIO_ERRMSG_CAPA];
			hio_uch_t uch[HIO_ERRMSG_CAPA];
		} tmpbuf;
		hio_ooch_t buf[HIO_ERRMSG_CAPA];
		hio_oow_t len;
	} errmsg;

	hio_bitmask_t _features;

	unsigned short int _shuterr;
	unsigned short int _fini_in_progress;

	struct
	{
		hio_bitmask_t trait;
		hio_bitmask_t log_mask;
		hio_oow_t log_maxcapa;
		hio_uch_t* log_target_u;
		hio_bch_t* log_target_b;
		hio_log_writer_t log_writer;
	} option;

	struct
	{
		hio_ooch_t* ptr;
		hio_oow_t len;
		hio_oow_t capa;
		hio_bitmask_t last_mask;
		hio_bitmask_t default_type_mask;
	} log;

	struct
	{
		struct
		{
			hio_ooch_t* ptr;
			hio_oow_t capa;
			hio_oow_t len;
		} xbuf; /* buffer to support sprintf */
	} sprintf;

	hio_stopreq_t stopreq;  /* stop request to abort hio_loop() */

	hio_cfmb_t cfmb; /* list head of cfmbs */
	hio_dev_t actdev; /* list head of active devices */
	hio_dev_t hltdev; /* list head of halted devices */
	hio_dev_t zmbdev; /* list head of zombie devices */

	hio_uint8_t bigbuf[65535]; /* TODO: make this dynamic depending on devices added. device may indicate a buffer size required??? */

	hio_ntime_t init_time;
	struct
	{
		hio_oow_t     capa;
		hio_oow_t     size;
		hio_tmrjob_t* jobs;
	} tmr;

	hio_cwq_t cwq;
	hio_cwq_t* cwqfl[HIO_CWQFL_SIZE]; /* list of free cwq objects */

	hio_svc_t actsvc; /* list head of active services */

	/* platform specific fields below */
	hio_sys_t* sysdep;
};

/* ========================================================================= */

#if defined(__cplusplus)
extern "C" {
#endif

HIO_EXPORT hio_t* hio_open (
	hio_mmgr_t*   mmgr,
	hio_oow_t     xtnsize,
	hio_cmgr_t*   cmgr,
	hio_bitmask_t features,
	hio_oow_t     tmrcapa,  /**< initial timer capacity */
	hio_errinf_t* errinf
);

HIO_EXPORT void hio_close (
	hio_t*        hio
);

HIO_EXPORT int hio_init (
	hio_t*       hio,
	hio_mmgr_t*  mmgr,
	hio_cmgr_t*  cmgr,
	hio_bitmask_t features,
	hio_oow_t    tmrcapa
);

HIO_EXPORT void hio_fini (
	hio_t*       hio
);

HIO_EXPORT int hio_getoption (
	hio_t*        hio,
	hio_option_t  id,
	void*         value
);

HIO_EXPORT int hio_setoption (
	hio_t*       hio,
	hio_option_t id,
	const void*  value
);

#if defined(HIO_HAVE_INLINE)
static HIO_INLINE void* hio_getxtn (hio_t* hio) { return (void*)((hio_uint8_t*)hio + hio->_instsize); }
static HIO_INLINE hio_mmgr_t* hio_getmmgr (hio_t* hio) { return hio->_mmgr; }
static HIO_INLINE hio_cmgr_t* hio_getcmgr (hio_t* hio) { return hio->_cmgr; }
static HIO_INLINE void hio_setcmgr (hio_t* hio, hio_cmgr_t* cmgr) { hio->_cmgr = cmgr; }
static HIO_INLINE hio_errnum_t hio_geterrnum (hio_t* hio) { return hio->errnum; }
#else
#	define hio_getxtn(hio) ((void*)((hio_uint8_t*)hio + ((hio_t*)hio)->_instsize))
#	define hio_getmmgr(hio) (((hio_t*)(hio))->_mmgr)
#	define hio_getcmgr(hio) (((hio_t*)(hio))->_cmgr)
#	define hio_setcmgr(hio,cmgr) (((hio_t*)(hio))->_cmgr = (cmgr))
#	define hio_geterrnum(hio) (((hio_t*)(hio))->errnum)
#endif

HIO_EXPORT void hio_seterrnum (
	hio_t*       hio, 
	hio_errnum_t errnum
);

HIO_EXPORT void hio_seterrwithsyserr (
	hio_t* hio,
	int    syserr_type,
	int    syserr_code
);

HIO_EXPORT void hio_seterrbfmt (
	hio_t*           hio,
	hio_errnum_t     errnum,
	const hio_bch_t* fmt,
	...
);

HIO_EXPORT void hio_seterrufmt (
	hio_t*           hio,
	hio_errnum_t     errnum,
	const hio_uch_t* fmt,
	...
);

HIO_EXPORT void hio_seterrbfmtv (
	hio_t*           hio,
	hio_errnum_t     errnum,
	const hio_bch_t* fmt,
	va_list          ap
);

HIO_EXPORT void hio_seterrufmtv (
	hio_t*           hio,
	hio_errnum_t     errnum,
	const hio_uch_t* fmt,
	va_list          ap
);

HIO_EXPORT void hio_seterrbfmtwithsyserr (
	hio_t*           hio,
	int              syserr_type,
	int              syserr_code,
	const hio_bch_t* fmt,
	...
);

HIO_EXPORT void hio_seterrufmtwithsyserr (
	hio_t*           hio,
	int              syserr_type,
	int              syserr_code,
	const hio_uch_t* fmt,
	...
);

HIO_EXPORT const hio_ooch_t* hio_geterrstr (
	hio_t* hio
);

HIO_EXPORT const hio_ooch_t* hio_geterrmsg (
	hio_t* hio
);

HIO_EXPORT void hio_geterrinf (
	hio_t*        hio,
	hio_errinf_t* info
);

HIO_EXPORT const hio_ooch_t* hio_backuperrmsg (
	hio_t* hio
);


HIO_EXPORT int hio_exec (
	hio_t* hio
);

HIO_EXPORT int hio_loop (
	hio_t* hio
);

HIO_EXPORT void hio_stop (
	hio_t*        hio,
	hio_stopreq_t stopreq
);



HIO_EXPORT hio_dev_t* hio_dev_make (
	hio_t*           hio,
	hio_oow_t        dev_size,
	hio_dev_mth_t*   dev_mth,
	hio_dev_evcb_t*  dev_evcb,
	void*            make_ctx
);

HIO_EXPORT void hio_dev_kill (
	hio_dev_t* dev
);

HIO_EXPORT void hio_dev_halt (
	hio_dev_t* dev
);

#if defined(HIO_HAVE_INLINE)
static HIO_INLINE hio_t* hio_dev_gethio (hio_dev_t* dev) { return dev->hio; }
#else
#	define hio_dev_gethio(dev) (((hio_dev_t*)(dev))->hio)
#endif

HIO_EXPORT int hio_dev_ioctl (
	hio_dev_t*  dev,
	int         cmd,
	void*       arg
);

HIO_EXPORT int hio_dev_watch (
	hio_dev_t*          dev,
	hio_dev_watch_cmd_t cmd,
	/** 0 or bitwise-ORed of #HIO_DEV_EVENT_IN and #HIO_DEV_EVENT_OUT */
	int                 events
);

HIO_EXPORT int hio_dev_read (
	hio_dev_t*         dev,
	int                enabled
);

/*
 * The hio_dev_timedread() function enables or disables the input watching.
 * If tmout is not HIO_NULL, it schedules to fire the on_read() callback
 * with the length of -1 and the hio error number set to HIO_ETMOUT.
 * If there is input before the time elapses, the scheduled timer job
 * is automaticaly cancelled. A call to hio_dev_read() or hio_dev_timedread()
 * with no timeout also cancels the unfired scheduled job.
 */
HIO_EXPORT int hio_dev_timedread (
	hio_dev_t*         dev,
	int                enabled,
	const hio_ntime_t* tmout
);

/**
 * The hio_dev_write() function posts a writing request. 
 * It attempts to write data immediately if there is no pending requests.
 * If writing fails, it returns -1. If writing succeeds, it calls the
 * on_write callback. If the callback fails, it returns -1. If the callback 
 * succeeds, it returns 1. If no immediate writing is possible, the request
 * is enqueued to a pending request list. If enqueing gets successful,
 * it returns 0. otherwise it returns -1.
 */ 
HIO_EXPORT int hio_dev_write (
	hio_dev_t*            dev,
	const void*           data,
	hio_iolen_t           len,
	void*                 wrctx,
	const hio_devaddr_t*  dstaddr
);

HIO_EXPORT int hio_dev_writev (
	hio_dev_t*            dev,
	hio_iovec_t*          iov,
	hio_iolen_t           iovcnt,
	void*                 wrctx,
	const hio_devaddr_t*  dstaddr
);

HIO_EXPORT int hio_dev_sendfile (
	hio_dev_t*            dev,
	hio_syshnd_t          in_fd,
	hio_foff_t            foff,
	hio_iolen_t           len,
	void*                 wrctx
);

HIO_EXPORT int hio_dev_timedwrite (
	hio_dev_t*            dev,
	const void*           data,
	hio_iolen_t           len,
	const hio_ntime_t*    tmout,
	void*                 wrctx,
	const hio_devaddr_t*  dstaddr
);


HIO_EXPORT int hio_dev_timedwritev (
	hio_dev_t*            dev,
	hio_iovec_t*          iov,
	hio_iolen_t           iovcnt,
	const hio_ntime_t*    tmout,
	void*                 wrctx,
	const hio_devaddr_t*  dstaddr
);

HIO_EXPORT int hio_dev_timedsendfile (
	hio_dev_t*            dev,
	hio_syshnd_t          in_fd,
	hio_foff_t            foff,
	hio_iolen_t           len,
	const hio_ntime_t*    tmout,
	void*                 wrctx
);
/* =========================================================================
 * SERVICE 
 * ========================================================================= */

#if defined(HIO_HAVE_INLINE)
static HIO_INLINE hio_t* hio_svc_gethio (hio_svc_t* svc) { return svc->hio; }
#else
#	define hio_svc_gethio(svc) (((hio_svc_t*)(svc))->hio)
#endif


/* =========================================================================
 * TIMER MANAGEMENT
 * ========================================================================= */

/**
 * The hio_instmrjob() function schedules a new event.
 *
 * \return #HIO_TMRIDX_INVALID on failure, valid index on success.
 */

HIO_EXPORT hio_tmridx_t hio_instmrjob (
	hio_t*              hio,
	const hio_tmrjob_t* job
);

HIO_EXPORT hio_tmridx_t hio_updtmrjob (
	hio_t*              hio,
	hio_tmridx_t        index,
	const hio_tmrjob_t* job
);

HIO_EXPORT void hio_deltmrjob (
	hio_t*          hio,
	hio_tmridx_t    index
);

/**
 * The hio_gettmrjob() function returns the
 * pointer to the registered event at the given index.
 */
HIO_EXPORT hio_tmrjob_t* hio_gettmrjob (
	hio_t*            hio,
	hio_tmridx_t      index
);

HIO_EXPORT int hio_gettmrjobdeadline (
	hio_t*            hio,
	hio_tmridx_t      index,
	hio_ntime_t*      deadline
);


HIO_EXPORT int hio_schedtmrjobat (
	hio_t*               hio,
	const hio_ntime_t*   fire_at,
	hio_tmrjob_handler_t handler,
	hio_tmridx_t*        tmridx,
	void*                ctx
);


HIO_EXPORT int hio_schedtmrjobafter (
	hio_t*               hio,
	const hio_ntime_t*   fire_after,
	hio_tmrjob_handler_t handler,
	hio_tmridx_t*        tmridx,
	void*                ctx
);

/* =========================================================================
 * TIME
 * ========================================================================= */

/**
 * The hio_gettime() function returns the elapsed time since hio initialization.
 */
HIO_EXPORT void hio_gettime (
	hio_t*            hio,
	hio_ntime_t*      now
);

/* =========================================================================
 * SYSTEM MEMORY MANAGEMENT FUCNTIONS VIA MMGR
 * ========================================================================= */
HIO_EXPORT void* hio_allocmem (
	hio_t*     hio,
	hio_oow_t  size
);

HIO_EXPORT void* hio_callocmem (
	hio_t*     hio,
	hio_oow_t  size
);

HIO_EXPORT void* hio_reallocmem (
	hio_t*      hio,
	void*       ptr,
	hio_oow_t   size
);

HIO_EXPORT void hio_freemem (
	hio_t*  hio,
	void*   ptr
);

HIO_EXPORT void hio_addcfmb (
	hio_t*             hio,
	hio_cfmb_t*        cfmb,
	hio_cfmb_checker_t checker
);

/* =========================================================================
 * STRING ENCODING CONVERSION
 * ========================================================================= */

#if defined(HIO_OOCH_IS_UCH)
#	define hio_convootobchars(hio,oocs,oocslen,bcs,bcslen) hio_convutobchars(hio,oocs,oocslen,bcs,bcslen)
#	define hio_convbtooochars(hio,bcs,bcslen,oocs,oocslen) hio_convbtouchars(hio,bcs,bcslen,oocs,oocslen)
#	define hio_convootobcstr(hio,oocs,oocslen,bcs,bcslen) hio_convutobcstr(hio,oocs,oocslen,bcs,bcslen)
#	define hio_convbtooocstr(hio,bcs,bcslen,oocs,oocslen) hio_convbtoucstr(hio,bcs,bcslen,oocs,oocslen)
#else
#	define hio_convootouchars(hio,oocs,oocslen,bcs,bcslen) hio_convbtouchars(hio,oocs,oocslen,bcs,bcslen)
#	define hio_convutooochars(hio,bcs,bcslen,oocs,oocslen) hio_convutobchars(hio,bcs,bcslen,oocs,oocslen)
#	define hio_convootoucstr(hio,oocs,oocslen,bcs,bcslen) hio_convbtoucstr(hio,oocs,oocslen,bcs,bcslen)
#	define hio_convutooocstr(hio,bcs,bcslen,oocs,oocslen) hio_convutobcstr(hio,bcs,bcslen,oocs,oocslen)
#endif

HIO_EXPORT int hio_convbtouchars (
	hio_t*           hio,
	const hio_bch_t* bcs,
	hio_oow_t*       bcslen,
	hio_uch_t*       ucs,
	hio_oow_t*       ucslen,
	int              all
);

HIO_EXPORT int hio_convutobchars (
	hio_t*           hio,
	const hio_uch_t* ucs,
	hio_oow_t*       ucslen,
	hio_bch_t*       bcs,
	hio_oow_t*       bcslen
);

/**
 * The hio_convbtoucstr() function converts a null-terminated byte string 
 * to a wide string.
 */
HIO_EXPORT int hio_convbtoucstr (
	hio_t*           hio,
	const hio_bch_t* bcs,
	hio_oow_t*       bcslen,
	hio_uch_t*       ucs,
	hio_oow_t*       ucslen,
	int              all
);


/**
 * The hio_convutobcstr() function converts a null-terminated wide string
 * to a byte string.
 */
HIO_EXPORT int hio_convutobcstr (
	hio_t*           hio,
	const hio_uch_t* ucs,
	hio_oow_t*       ucslen,
	hio_bch_t*       bcs,
	hio_oow_t*       bcslen
);


#if defined(HIO_OOCH_IS_UCH)
#	define hio_dupootobcharswithheadroom(hio,hrb,oocs,oocslen,bcslen) hio_duputobcharswithheadroom(hio,hrb,oocs,oocslen,bcslen)
#	define hio_dupbtooocharswithheadroom(hio,hrb,bcs,bcslen,oocslen,all) hio_dupbtoucharswithheadroom(hio,hrb,bcs,bcslen,oocslen,all)
#	define hio_dupootobchars(hio,oocs,oocslen,bcslen) hio_duputobchars(hio,oocs,oocslen,bcslen)
#	define hio_dupbtooochars(hio,bcs,bcslen,oocslen,all) hio_dupbtouchars(hio,bcs,bcslen,oocslen,all)

#	define hio_dupootobcstrwithheadroom(hio,hrb,oocs,bcslen) hio_duputobcstrwithheadroom(hio,hrb,oocs,bcslen)
#	define hio_dupbtooocstrwithheadroom(hio,hrb,bcs,oocslen,all) hio_dupbtoucstrwithheadroom(hio,hrb,bcs,oocslen,all)
#	define hio_dupootobcstr(hio,oocs,bcslen) hio_duputobcstr(hio,oocs,bcslen)
#	define hio_dupbtooocstr(hio,bcs,oocslen,all) hio_dupbtoucstr(hio,bcs,oocslen,all)
#else
#	define hio_dupootoucharswithheadroom(hio,hrb,oocs,oocslen,ucslen,all) hio_dupbtoucharswithheadroom(hio,hrb,oocs,oocslen,ucslen,all)
#	define hio_duputooocharswithheadroom(hio,hrb,ucs,ucslen,oocslen) hio_duputobcharswithheadroom(hio,hrb,ucs,ucslen,oocslen)
#	define hio_dupootouchars(hio,oocs,oocslen,ucslen,all) hio_dupbtouchars(hio,oocs,oocslen,ucslen,all)
#	define hio_duputooochars(hio,ucs,ucslen,oocslen) hio_duputobchars(hio,ucs,ucslen,oocslen)

#	define hio_dupootoucstrwithheadroom(hio,hrb,oocs,ucslen,all) hio_dupbtoucstrwithheadroom(hio,hrb,oocs,ucslen,all)
#	define hio_duputooocstrwithheadroom(hio,hrb,ucs,oocslen) hio_duputobcstrwithheadroom(hio,hrb,ucs,oocslen)
#	define hio_dupootoucstr(hio,oocs,ucslen,all) hio_dupbtoucstr(hio,oocs,ucslen,all)
#	define hio_duputooocstr(hio,ucs,oocslen) hio_duputobcstr(hio,ucs,oocslen)
#endif


HIO_EXPORT hio_uch_t* hio_dupbtoucharswithheadroom (
	hio_t*           hio,
	hio_oow_t        headroom_bytes,
	const hio_bch_t* bcs,
	hio_oow_t        bcslen,
	hio_oow_t*       ucslen,
	int              all
);

HIO_EXPORT hio_bch_t* hio_duputobcharswithheadroom (
	hio_t*           hio,
	hio_oow_t        headroom_bytes,
	const hio_uch_t* ucs,
	hio_oow_t        ucslen,
	hio_oow_t*       bcslen
);

HIO_EXPORT hio_uch_t* hio_dupbtouchars (
	hio_t*           hio,
	const hio_bch_t* bcs,
	hio_oow_t        bcslen,
	hio_oow_t*       ucslen,
	int              all
);

HIO_EXPORT hio_bch_t* hio_duputobchars (
	hio_t*           hio,
	const hio_uch_t* ucs,
	hio_oow_t        ucslen,
	hio_oow_t*       bcslen
);


HIO_EXPORT hio_uch_t* hio_dupbtoucstrwithheadroom (
	hio_t*           hio,
	hio_oow_t        headroom_bytes,
	const hio_bch_t* bcs,
	hio_oow_t*       ucslen,
	int              all
);

HIO_EXPORT hio_bch_t* hio_duputobcstrwithheadroom (
	hio_t*           hio,
	hio_oow_t        headroom_bytes,
	const hio_uch_t* ucs,
	hio_oow_t* bcslen
);

HIO_EXPORT hio_uch_t* hio_dupbtoucstr (
	hio_t*           hio,
	const hio_bch_t* bcs,
	hio_oow_t*       ucslen, /* optional: length of returned string */
	int              all
);

HIO_EXPORT hio_bch_t* hio_duputobcstr (
	hio_t*           hio,
	const hio_uch_t* ucs,
	hio_oow_t*       bcslen /* optional: length of returned string */
);



HIO_EXPORT hio_uch_t* hio_dupuchars (
	hio_t*           hio,
	const hio_uch_t* ucs,
	hio_oow_t        ucslen
);

HIO_EXPORT hio_bch_t* hio_dupbchars (
	hio_t*           hio,
	const hio_bch_t* bcs,
	hio_oow_t        bcslen
);


HIO_EXPORT hio_uch_t* hio_dupucstr (
	hio_t*           hio,
	const hio_uch_t* ucs,
	hio_oow_t*       ucslen /* [OUT] length*/
);

HIO_EXPORT hio_bch_t* hio_dupbcstr (
	hio_t*           hio,
	const hio_bch_t* bcs,
	hio_oow_t*       bcslen /* [OUT] length */
);

HIO_EXPORT hio_uch_t* hio_dupucstrs (
	hio_t*           hio,
	const hio_uch_t* ucs[],
	hio_oow_t*       ucslen
);

HIO_EXPORT hio_bch_t* hio_dupbcstrs (
	hio_t*           hio,
	const hio_bch_t* bcs[],
	hio_oow_t*       bcslen
);

#if defined(HIO_OOCH_IS_UCH)
#	define hio_dupoochars(hio,oocs,oocslen) hio_dupuchars(hio,oocs,oocslen)
#	define hio_dupoocstr(hio,oocs,oocslen) hio_dupucstr(hio,oocs,oocslen)
#else
#	define hio_dupoochars(hio,oocs,oocslen) hio_dupbchars(hio,oocs,oocslen)
#	define hio_dupoocstr(hio,oocs,oocslen) hio_dupbcstr(hio,oocs,oocslen)
#endif

/* =========================================================================
 * STRING FORMATTING
 * ========================================================================= */

HIO_EXPORT hio_oow_t hio_vfmttoucstr (
	hio_t*           hio,
	hio_uch_t*       buf,
	hio_oow_t        bufsz,
	const hio_uch_t* fmt,
	va_list          ap
);

HIO_EXPORT hio_oow_t hio_fmttoucstr (
	hio_t*           hio,
	hio_uch_t*       buf,
	hio_oow_t        bufsz,
	const hio_uch_t* fmt,
	...
);

HIO_EXPORT hio_oow_t hio_vfmttobcstr (
	hio_t*           hio,
	hio_bch_t*       buf,
	hio_oow_t        bufsz,
	const hio_bch_t* fmt,
	va_list          ap
);

HIO_EXPORT hio_oow_t hio_fmttobcstr (
	hio_t*           hio,
	hio_bch_t*       buf,
	hio_oow_t        bufsz,
	const hio_bch_t* fmt,
	...
);

#if defined(HIO_OOCH_IS_UCH)
#	define hio_vfmttooocstr hio_vfmttoucstr
#	define hio_fmttooocstr hio_fmttoucstr
#else
#	define hio_vfmttooocstr hio_vfmttobcstr
#	define hio_fmttooocstr hio_fmttobcstr
#endif

/* =========================================================================
 * MIO VM LOGGING
 * ========================================================================= */

HIO_EXPORT hio_ooi_t hio_logbfmt (
	hio_t*           hio,
	hio_bitmask_t    mask,
	const hio_bch_t* fmt,
	...
);

HIO_EXPORT hio_ooi_t hio_logufmt (
	hio_t*            hio,
	hio_bitmask_t     mask,
	const hio_uch_t*  fmt,
	...
);

HIO_EXPORT hio_ooi_t hio_logbfmtv (
	hio_t*           hio,
	hio_bitmask_t    mask,
	const hio_bch_t* fmt,
	va_list           ap
);

HIO_EXPORT hio_ooi_t hio_logufmtv (
	hio_t*            hio,
	hio_bitmask_t     mask,
	const hio_uch_t*  fmt,
	va_list           ap
);
 
#if defined(HIO_OOCH_IS_UCH)
#	define hio_logoofmt hio_logufmt
#	define hio_logoofmtv hio_logufmtv
#else
#	define hio_logoofmt hio_logbfmt
#	define hio_logoofmtv hio_logbfmtv
#endif

/* =========================================================================
 * MISCELLANEOUS HELPER FUNCTIONS
 * ========================================================================= */

HIO_EXPORT const hio_ooch_t* hio_errnum_to_errstr (
	hio_errnum_t errnum
);

HIO_EXPORT hio_oow_t hio_dev_cap_to_bcstr (
	hio_bitmask_t cap,
	hio_bch_t*    buf,
	hio_oow_t     bsz
);

#if defined(__cplusplus)
}
#endif


#endif
