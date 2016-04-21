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

#if defined(_WIN32)
	typedef stio_uintptr_t qse_syshnd_t;
	#define STIO_SYSHND_INVALID (~(stio_uintptr_t)0)
#else
	typedef int stio_syshnd_t;
	#define STIO_SYSHND_INVALID (-1)
#endif

typedef struct stio_devaddr_t stio_devaddr_t;
struct stio_devaddr_t
{
	int len;
	void* ptr;
};

#define STIO_CONST_SWAP16(x) \
	((stio_uint16_t)((((stio_uint16_t)(x) & (stio_uint16_t)0x00ffU) << 8) | \
	                 (((stio_uint16_t)(x) & (stio_uint16_t)0xff00U) >> 8) ))

#define STIO_CONST_SWAP32(x) \
	((stio_uint32_t)((((stio_uint32_t)(x) & (stio_uint32_t)0x000000ffUL) << 24) | \
	                 (((stio_uint32_t)(x) & (stio_uint32_t)0x0000ff00UL) <<  8) | \
	                 (((stio_uint32_t)(x) & (stio_uint32_t)0x00ff0000UL) >>  8) | \
	                 (((stio_uint32_t)(x) & (stio_uint32_t)0xff000000UL) >> 24) ))

#if defined(STIO_ENDIAN_LITTLE)
#	define STIO_CONST_NTOH16(x) STIO_CONST_SWAP16(x)
#	define STIO_CONST_HTON16(x) STIO_CONST_SWAP16(x)
#	define STIO_CONST_NTOH32(x) STIO_CONST_SWAP32(x)
#	define STIO_CONST_HTON32(x) STIO_CONST_SWAP32(x)
#elif defined(STIO_ENDIAN_BIG)
#	define STIO_CONST_NTOH16(x)
#	define STIO_CONST_HTON16(x)
#	define STIO_CONST_NTOH32(x)
#	define STIO_CONST_HTON32(x)
#else
#	error UNKNOWN ENDIAN
#endif

/* ========================================================================= */

typedef struct stio_t stio_t;
typedef struct stio_dev_t stio_dev_t;
typedef struct stio_dev_mth_t stio_dev_mth_t;
typedef struct stio_dev_evcb_t stio_dev_evcb_t;

typedef struct stio_wq_t stio_wq_t;
typedef stio_intptr_t stio_iolen_t; /* NOTE: this is a signed type */

enum stio_errnum_t
{
	STIO_ENOERR,
	STIO_ENOIMPL,
	STIO_ESYSERR,
	STIO_EINTERN,

	STIO_ENOMEM,
	STIO_EINVAL,
	STIO_ENOENT,
	STIO_ENOSUP,     /* not supported */
	STIO_EMFILE,     /* too many open files */
	STIO_ENFILE,
	STIO_EAGAIN,
	STIO_ECONRF,     /* connection refused */
	STIO_ECONRS,     /* connection reset */
	STIO_ENOCAPA,    /* no capability */
	STIO_ETMOUT,     /* timed out */

	STIO_EDEVMAKE,
	STIO_EDEVERR,
	STIO_EDEVHUP
};

typedef enum stio_errnum_t stio_errnum_t;

enum stio_stopreq_t
{
	STIO_STOPREQ_NONE = 0,
	STIO_STOPREQ_TERMINATION,
	STIO_STOPREQ_WATCHER_UPDATE_ERROR,
	STIO_STOPREQ_WATCHER_RENEW_ERROR
};
typedef enum stio_stopreq_t stio_stopreq_t;

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

struct stio_dev_mth_t
{
	/* ------------------------------------------------------------------ */
	/* mandatory. called in stio_makedev() */
	int           (*make)         (stio_dev_t* dev, void* ctx); 

	/* ------------------------------------------------------------------ */
	/* mandatory. called in stio_killdev(). also called in stio_makedev() upon
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
	int           (*kill)         (stio_dev_t* dev, int force); 

	/* ------------------------------------------------------------------ */
	stio_syshnd_t (*getsyshnd)    (stio_dev_t* dev); /* mandatory. called in stio_makedev() after successful make() */


	/* ------------------------------------------------------------------ */
	/* return -1 on failure, 0 if no data is availble, 1 otherwise.
	 * when returning 1, *len must be sent to the length of data read.
	 * if *len is set to 0, it's treated as EOF. */
	int           (*read)         (stio_dev_t* dev, void* data, stio_iolen_t* len, stio_devaddr_t* srcaddr);

	/* ------------------------------------------------------------------ */
	int           (*write)        (stio_dev_t* dev, const void* data, stio_iolen_t* len, const stio_devaddr_t* dstaddr);

	/* ------------------------------------------------------------------ */
	int           (*ioctl)        (stio_dev_t* dev, int cmd, void* arg);

};

struct stio_dev_evcb_t
{
	/* return -1 on failure. 0 or 1 on success.
	 * when 0 is returned, it doesn't attempt to perform actual I/O.
	 * when 1 is returned, it attempts to perform actual I/O. */
	int           (*ready)        (stio_dev_t* dev, int events);

	/* return -1 on failure, 0 or 1 on success.
	 * when 0 is returned, the main loop stops the attempt to read more data.
	 * when 1 is returned, the main loop attempts to read more data without*/
	int           (*on_read)      (stio_dev_t* dev, const void* data, stio_iolen_t len, const stio_devaddr_t* srcaddr);

	/* return -1 on failure, 0 on success. 
	 * wrlen is the length of data written. it is the length of the originally
	 * posted writing request for a stream device. For a non stream device, it
	 * may be shorter than the originally posted length. */
	int           (*on_write)     (stio_dev_t* dev, stio_iolen_t wrlen, void* wrctx, const stio_devaddr_t* dstaddr);
};

struct stio_wq_t
{
	stio_wq_t*       next;
	stio_wq_t*       prev;

	stio_iolen_t     olen; /* original data length */
	stio_uint8_t*    ptr;  /* pointer to data */
	stio_iolen_t     len;  /* remaining data length */
	void*            ctx;
	stio_dev_t*      dev; /* back-pointer to the device */

	stio_tmridx_t    tmridx;
	stio_devaddr_t   dstaddr;
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
	stio_t*          stio; \
	stio_size_t      dev_size; \
	int              dev_capa; \
	stio_dev_mth_t*  dev_mth; \
	stio_dev_evcb_t* dev_evcb; \
	stio_wq_t        wq; \
	stio_dev_t*      dev_prev; \
	stio_dev_t*      dev_next 

struct stio_dev_t
{
	STIO_DEV_HEADERS;
};

enum stio_dev_capa_t
{
	STIO_DEV_CAPA_VIRTUAL      = (1 << 0),
	STIO_DEV_CAPA_IN           = (1 << 1),
	STIO_DEV_CAPA_OUT          = (1 << 2),
	/* #STIO_DEV_CAPA_PRI is meaningful only if #STIO_DEV_CAPA_IN is set */
	STIO_DEV_CAPA_PRI          = (1 << 3), 
	STIO_DEV_CAPA_STREAM       = (1 << 4),
	STIO_DEV_CAPA_OUT_QUEUED   = (1 << 5),

	/* internal use only. never set this bit to the dev_capa field */
	STIO_DEV_CAPA_IN_DISABLED  = (1 << 9),
	STIO_DEV_CAPA_IN_CLOSED    = (1 << 10),
	STIO_DEV_CAPA_OUT_CLOSED   = (1 << 11),
	STIO_DEV_CAPA_IN_WATCHED   = (1 << 12),
	STIO_DEV_CAPA_OUT_WATCHED  = (1 << 13),
	STIO_DEV_CAPA_PRI_WATCHED  = (1 << 14), /**< can be set only if STIO_DEV_CAPA_IN_WATCHED is set */

	STIO_DEV_CAPA_ACTIVE       = (1 << 15),
	STIO_DEV_CAPA_HALTED       = (1 << 16),
	STIO_DEV_CAPA_ZOMBIE       = (1 << 17)
};
typedef enum stio_dev_capa_t stio_dev_capa_t;

enum stio_dev_watch_cmd_t
{
	STIO_DEV_WATCH_START,
	STIO_DEV_WATCH_UPDATE,
	STIO_DEV_WATCH_RENEW, /* automatic update */
	STIO_DEV_WATCH_STOP
};
typedef enum stio_dev_watch_cmd_t stio_dev_watch_cmd_t;

enum stio_dev_event_t
{
	STIO_DEV_EVENT_IN  = (1 << 0),
	STIO_DEV_EVENT_OUT = (1 << 1),

	STIO_DEV_EVENT_PRI = (1 << 2),
	STIO_DEV_EVENT_HUP = (1 << 3),
	STIO_DEV_EVENT_ERR = (1 << 4)
};
typedef enum stio_dev_event_t stio_dev_event_t;



/* ========================================================================= */
/* TOOD: move these to a separte file */

#define STIO_ETHHDR_PROTO_IP4 0x0800
#define STIO_ETHHDR_PROTO_ARP 0x0806

#define STIO_ARPHDR_OPCODE_REQUEST 1
#define STIO_ARPHDR_OPCODE_REPLY   2

#define STIO_ARPHDR_HTYPE_ETH 0x0001
#define STIO_ARPHDR_PTYPE_IP4 0x0800

#define STIO_ETHADDR_LEN 6
#define STIO_IP4ADDR_LEN 4
#define STIO_IP6ADDR_LEN 16 


#if defined(__GNUC__)
#	define STIO_PACKED __attribute__((__packed__))

#else
#	define STIO_PACKED 
#	STIO_PACK_PUSH pack(push)
#	STIO_PACK_PUSH pack(push)
#	STIO_PACK(x) pack(x)
#endif


#if defined(__GNUC__)
	/* nothing */
#else
	#pragma pack(push)
	#pragma pack(1)
#endif
struct STIO_PACKED stio_ethaddr_t
{
	stio_uint8_t v[STIO_ETHADDR_LEN]; 
};
typedef struct stio_ethaddr_t stio_ethaddr_t;

struct STIO_PACKED stio_ip4addr_t
{
	stio_uint8_t v[STIO_IP4ADDR_LEN];
};
typedef struct stio_ip4addr_t stio_ip4addr_t;

struct STIO_PACKED stio_ip6addr_t
{
	stio_uint8_t v[STIO_IP6ADDR_LEN]; 
};
typedef struct stio_ip6addr_t stio_ip6addr_t;


struct STIO_PACKED stio_ethhdr_t
{
	stio_uint8_t  dest[STIO_ETHADDR_LEN];
	stio_uint8_t  source[STIO_ETHADDR_LEN];
	stio_uint16_t proto;
};
typedef struct stio_ethhdr_t stio_ethhdr_t;

struct STIO_PACKED stio_arphdr_t
{
	stio_uint16_t htype;   /* hardware type (ethernet: 0x0001) */
	stio_uint16_t ptype;   /* protocol type (ipv4: 0x0800) */
	stio_uint8_t  hlen;    /* hardware address length (ethernet: 6) */
	stio_uint8_t  plen;    /* protocol address length (ipv4 :4) */
	stio_uint16_t opcode;  /* operation code */
};
typedef struct stio_arphdr_t stio_arphdr_t;

/* arp payload for ipv4 over ethernet */
struct STIO_PACKED stio_etharp_t
{
	stio_uint8_t sha[STIO_ETHADDR_LEN];   /* source hardware address */
	stio_uint8_t spa[STIO_IP4ADDR_LEN];   /* source protocol address */
	stio_uint8_t tha[STIO_ETHADDR_LEN];   /* target hardware address */
	stio_uint8_t tpa[STIO_IP4ADDR_LEN];   /* target protocol address */
};
typedef struct stio_etharp_t stio_etharp_t;

struct STIO_PACKED stio_etharp_pkt_t
{
	stio_ethhdr_t ethhdr;
	stio_arphdr_t arphdr;
	stio_etharp_t arppld;
};
typedef struct stio_etharp_pkt_t stio_etharp_pkt_t;

#if defined(__GNUC__)
	/* nothing */
#else
	#pragma pack(pop)
#endif

/* ========================================================================= */


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
	stio_t*        stio,
	stio_stopreq_t stopreq
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

STIO_EXPORT int stio_dev_watch (
	stio_dev_t*          dev,
	stio_dev_watch_cmd_t cmd,
	/** 0 or bitwise-ORed of #STIO_DEV_EVENT_IN and #STIO_DEV_EVENT_OUT */
	int                  events
);

STIO_EXPORT int stio_dev_read (
	stio_dev_t*   dev,
	int           enabled
);

/**
 * The stio_dev_write() function posts a writing request. 
 * It attempts to write data immediately if there is no pending requests.
 * If writing fails, it returns -1. If writing succeeds, it calls the
 * on_write callback. If the callback fails, it returns -1. If the callback 
 * succeeds, it returns 1. If no immediate writing is possible, the request
 * is enqueued to a pending request list. If enqueing gets successful,
 * it returns 0. otherwise it returns -1.
 */ 
STIO_EXPORT int stio_dev_write (
	stio_dev_t*            dev,
	const void*            data,
	stio_iolen_t           len,
	void*                  wrctx,
	const stio_devaddr_t*  dstaddr
);


STIO_EXPORT int stio_dev_timedwrite (
	stio_dev_t*           dev,
	const void*           data,
	stio_iolen_t          len,
	const stio_ntime_t*   tmout,
	void*                 wrctx,
	const stio_devaddr_t* dstaddr
);

STIO_EXPORT void stio_dev_halt (
	stio_dev_t* dev
);


/* ========================================================================= */

#define stio_inittime(x,s,ns) (((x)->sec = (s)), ((x)->nsec = (ns)))
#define stio_cleartime(x) stio_inittime(x,0,0)
#define stio_cmptime(x,y) \
	(((x)->sec == (y)->sec)? ((x)->nsec - (y)->nsec): \
	                         ((x)->sec -  (y)->sec))

/* if time has been normalized properly, nsec must be equal to or
 * greater than 0. */
#define stio_isnegtime(x) ((x)->sec < 0)
#define stio_ispostime(x) ((x)->sec > 0 || ((x)->sec == 0 && (x)->nsec > 0))
#define stio_iszerotime(x) ((x)->sec == 0 && (x)->nsec == 0)


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

STIO_EXPORT stio_tmridx_t stio_instmrjob (
	stio_t*              stio,
	const stio_tmrjob_t* job
);

STIO_EXPORT stio_tmridx_t stio_updtmrjob (
	stio_t*              stio,
	stio_tmridx_t        index,
	const stio_tmrjob_t* job
);

STIO_EXPORT void stio_deltmrjob (
	stio_t*          stio,
	stio_tmridx_t    index
);

/**
 * The stio_gettmrjob() function returns the
 * pointer to the registered event at the given index.
 */
STIO_EXPORT stio_tmrjob_t* stio_gettmrjob (
	stio_t*            stio,
	stio_tmridx_t      index
);


/* ========================================================================= */


#if defined(STIO_HAVE_UINT16_T)
STIO_EXPORT stio_uint16_t stio_ntoh16 (
	stio_uint16_t x
);

STIO_EXPORT stio_uint16_t stio_hton16 (
	stio_uint16_t x
);
#endif

#if defined(STIO_HAVE_UINT32_T)
STIO_EXPORT stio_uint32_t stio_ntoh32 (
	stio_uint32_t x
);

STIO_EXPORT stio_uint32_t stio_hton32 (
	stio_uint32_t x
);
#endif

#if defined(STIO_HAVE_UINT64_T)
STIO_EXPORT stio_uint64_t stio_ntoh64 (
	stio_uint64_t x
);

STIO_EXPORT stio_uint64_t stio_hton64 (
	stio_uint64_t x
);
#endif

#if defined(STIO_HAVE_UINT128_T)
STIO_EXPORT stio_uint128_t stio_ntoh128 (
	stio_uint128_t x
);

STIO_EXPORT stio_uint128_t stio_hton128 (
	stio_uint128_t x
);
#endif

/* ========================================================================= */


#ifdef __cplusplus
}
#endif


#endif
