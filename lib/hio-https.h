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

#ifndef _HIO_HTTPS_H_
#define _HIO_HTTPS_H_

#include <hio-ecs.h>
#include <hio-sck.h>
#include <hio-htrd.h>
#include <hio-htre.h>
#include <hio-http.h>
#include <hio-thr.h>
#include <hio-fcgi.h>
#include <hio-ws.h>

/** \file
 * This file provides basic data types and functions for the http protocol.
 */

/** default for #HIO_SVC_HTTPS_CLIENT_IDLE_TMOUT, in seconds. unchanged from
 *  the constant it replaces. */
#define HIO_SVC_HTTPS_DFL_CLIENT_IDLE_TMOUT (10)

/** default for #HIO_SVC_HTTPS_CLIENT_HDR_TMOUT, in seconds. the same figure
 *  nginx uses for client_header_timeout. */
#define HIO_SVC_HTTPS_DFL_CLIENT_HDR_TMOUT (60)

enum hio_svc_https_option_t
{
        HIO_SVC_HTTPS_TASK_MAX,
        HIO_SVC_HTTPS_TASK_CGI_MAX,

        /** hio_ntime_t. how long a client may go without sending anything at
         *  all before it is halted. only applies while no task is bound, so a
         *  long-running task is never cut off for a client that has nothing
         *  left to say. a negative value turns it off. */
        HIO_SVC_HTTPS_CLIENT_IDLE_TMOUT,

        /** hio_ntime_t. how long a client may take to deliver a complete
         *  header block, measured from the start of the request rather than
         *  from the last octet received.
         *
         *  that distinction is the entire point. the idle timeout above is
         *  refreshed by every read, so a peer that sends one octet every few
         *  seconds resets it forever and is never reaped - which is what
         *  slowloris does. a deadline that runs from the start of the request
         *  cannot be pushed back by dribbling.
         *
         *  a negative value turns it off. */
        HIO_SVC_HTTPS_CLIENT_HDR_TMOUT
};

typedef enum hio_svc_https_option_t hio_svc_https_option_t;

/* -------------------------------------------------------------- */
typedef struct hio_svc_https_t hio_svc_https_t;

/* -------------------------------------------------------------- */

typedef struct hio_svc_https_task_t hio_svc_https_task_t;
typedef struct hio_svc_https_cli_t hio_svc_https_cli_t;

typedef void (*hio_svc_https_task_on_kill_t) (
	hio_svc_https_task_t* task
);

#define HIO_SVC_HTTPS_TASK_HEADER \
	HIO_RCO_HEADER; \
	hio_svc_https_t* https; \
	hio_oow_t task_size; \
	hio_svc_https_task_t* task_prev; \
	hio_svc_https_task_t* task_next; \
	hio_svc_https_task_on_kill_t task_on_kill; \
	hio_dev_sck_t* task_csck; \
	hio_svc_https_cli_t* task_client; \
	const hio_dev_sck_evcb_t* task_evcb; \
	hio_htrd_recbs_t task_client_htrd_org_recbs; \
	unsigned int task_client_htrd_recbs_changed: 1; \
	unsigned int task_keep_client_alive: 1; \
	unsigned int task_req_qpath_ending_with_slash: 1; \
	unsigned int task_req_qpath_is_root: 1; \
	unsigned int task_req_conlen_unlimited: 1; \
	unsigned int task_res_chunked: 1; \
	unsigned int task_res_started: 1; \
	unsigned int task_res_ended: 1; \
	unsigned int task_res_ever_sent: 1; \
	int task_req_flags; \
	hio_http_version_t task_req_version; \
	hio_http_method_t task_req_method; \
	hio_bch_t* task_req_qmth; \
	hio_bch_t* task_req_qpath; \
	hio_oow_t task_req_conlen; \
	hio_http_status_t task_status_code; \
	hio_ooi_t task_res_pending_writes;

struct hio_svc_https_task_t
{
	HIO_SVC_HTTPS_TASK_HEADER;
};

#define HIO_SVC_HTTPS_TASK_RC(task) HIO_RCO_RC(task)

#define HIO_SVC_HTTPS_TASK_RCUP(task) HIO_RCO_REF(task)

#define HIO_SVC_HTTPS_TASK_RCDOWN(task_var) HIO_RCO_UNREF(task_var)

#define HIO_SVC_HTTPS_TASK_REF(task, var) do { \
	(var) = (task); \
	HIO_SVC_HTTPS_TASK_RCUP(task); \
} while(0)

#define HIO_SVC_HTTPS_TASK_UNREF(task_var) HIO_RCO_UNREF_CLEAR(task_var)

/* -------------------------------------------------------------- */

typedef int (*hio_svc_https_proc_req_t) (
	hio_svc_https_t* https,
	hio_dev_sck_t*  sck,
	hio_htre_t*     req
);

/* -------------------------------------------------------------- */
struct hio_svc_https_thr_func_info_t
{
	hio_http_method_t  req_method;
	hio_http_version_t req_version;
	hio_bch_t*         req_path;
	hio_bch_t*         req_param;
	int                req_x_http_method_override; /* -1 or hio_http_method_t */

	/* TODO: header table */

	hio_skad_t         client_addr;
	hio_skad_t         server_addr;
};
typedef struct hio_svc_https_thr_func_info_t hio_svc_https_thr_func_info_t;

typedef void (*hio_svc_https_thr_func_t) (
	hio_svc_https_t*               https,
	hio_dev_thr_iopair_t*         iop,
	hio_svc_https_thr_func_info_t* tfi,
	void*                         ctx
);

/* -------------------------------------------------------------- */

struct hio_svc_https_fun_func_info_t
{
	hio_http_method_t  req_method;
	hio_http_version_t req_version;
	hio_bch_t*         req_path;
	hio_bch_t*         req_param;
	int                req_x_http_method_override; /* -1 or hio_http_method_t */

	/* TODO: header table */

	hio_skad_t         client_addr;
	hio_skad_t         server_addr;
};
typedef struct hio_svc_https_fun_func_info_t hio_svc_https_fun_func_info_t;

typedef void (*hio_svc_https_fun_func_t) (
	hio_svc_https_t*    https,
	void*              ctx
);

/* -------------------------------------------------------------- */

#if 0
enum hio_svc_https_cgi_option_t
{
	/* no option yet */
};

enum hio_svc_https_fcgi_option_t
{
	/* no option yet */
};
#endif

enum hio_svc_https_file_option_t
{
	HIO_SVC_HTTPS_FILE_READ_ONLY        = (1 << 0)
};

#if 0
enum hio_svc_https_thr_option_t
{
	/* no option yet */
};


enum hio_svc_https_txt_option_t
{
	/* no option yet */
};
#endif

enum hio_svc_https_file_bfmt_dir_type_t
{
	HIO_SVC_HTTPS_FILE_BFMT_DIR_HEADER,
	HIO_SVC_HTTPS_FILE_BFMT_DIR_ENTRY,
	HIO_SVC_HTTPS_FILE_BFMT_DIR_FOOTER
};
typedef enum hio_svc_https_file_bfmt_dir_type_t hio_svc_https_file_bfmt_dir_type_t;

struct hio_svc_https_file_cbs_t
{
	const hio_bch_t* (*get_mime_type) (hio_svc_https_t* https, const hio_bch_t* qpath, const hio_bch_t* file_path, void* ctx);
	int (*open_dir_list) (hio_svc_https_t* https, const hio_bch_t* qpath, const hio_bch_t* dir_path, const hio_bch_t** res_mime_type, void* ctx);
	void *ctx;
};
typedef struct hio_svc_https_file_cbs_t hio_svc_https_file_cbs_t;

#if defined(__cplusplus)
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* HTTP SERVER SERVICE                                                       */
/* ------------------------------------------------------------------------- */

/**
 * Which transport to run on a listening address.
 *
 * The concrete socket type is worked out from this together with the address
 * family, because the family alone cannot distinguish tcp from sctp - which is
 * why this exists. #HIO_SVC_HTTPS_BIND_PROTO_DEFAULT is 0, so a zeroed
 * descriptor behaves exactly as before this field existed.
 *
 * [NOTE] the concrete type is deliberately not named here. #HIO_DEV_SCK_QX is
 * 0 in hio_dev_sck_type_t, so a hio_dev_sck_type_t field could not use 0 to
 * mean "work it out" - and a caller should not have to know which of twenty
 * device types corresponds to an address family anyway.
 */
enum hio_svc_https_bind_proto_t
{
	/** tcp for AF_INET/AF_INET6, and the only sensible transport for the
	 *  other families */
	HIO_SVC_HTTPS_BIND_PROTO_DEFAULT = 0,

	/** sctp. valid for AF_INET and AF_INET6 only; a bind requesting it for
	 *  any other family is skipped. */
	HIO_SVC_HTTPS_BIND_PROTO_SCTP
};
typedef enum hio_svc_https_bind_proto_t hio_svc_https_bind_proto_t;

/**
 * One listening address for hio_svc_https_start().
 *
 * This wraps hio_dev_sck_bind_t rather than extending it, because some of what
 * the service needs is settled when the socket is *made* and not when it is
 * bound - the transport, and the sctp stream counts. A caller who drives
 * hio_dev_sck_make() directly passes those through hio_dev_sck_make_t; a
 * caller of this service never sees that struct, so they have to travel here.
 */
struct hio_svc_https_bind_t
{
	hio_svc_https_bind_proto_t proto;

	/** the address and the bind-time options, including the ssl certificate */
	hio_dev_sck_bind_t bind;

	/** number of outbound sctp streams to request, 0 for the system default.
	 *  ignored unless proto is #HIO_SVC_HTTPS_BIND_PROTO_SCTP. */
	hio_uint16_t sctp_ostreams;
	/** maximum number of inbound sctp streams to accept, 0 for the default. */
	hio_uint16_t sctp_instreams;
};
typedef struct hio_svc_https_bind_t hio_svc_https_bind_t;

HIO_EXPORT hio_svc_https_t* hio_svc_https_start (
	hio_t*                       hio,
	hio_oow_t                    xtnsize,
	hio_svc_https_bind_t*        binds,
	hio_oow_t                    nbinds,
	hio_svc_https_proc_req_t     proc_req
);

HIO_EXPORT void hio_svc_https_stop (
	hio_svc_https_t* https
);

HIO_EXPORT void* hio_svc_https_getxtn (
	hio_svc_https_t* https
);

#if defined(HIO_HAVE_INLINE)
static HIO_INLINE hio_t* hio_svc_https_gethio(hio_svc_https_t* svc) { return hio_svc_gethio((hio_svc_t*)svc); }
#else
#	define hio_svc_https_gethio(svc) hio_svc_gethio(svc)
#endif

HIO_EXPORT int hio_svc_https_getoption (
	hio_svc_https_t*       https,
	hio_svc_https_option_t id,
	void*                  value
);

HIO_EXPORT int hio_svc_https_setoption (
	hio_svc_https_t*       https,
	hio_svc_https_option_t id,
	const void*            value
);

HIO_EXPORT int hio_svc_https_enablefcgic (
	hio_svc_https_t*       https,
	hio_svc_fcgic_tmout_t* tmout
);

HIO_EXPORT int hio_svc_https_writetosidechan (
	hio_svc_https_t* https,
	hio_oow_t        idx, /* listener index */
	const void*      dptr,
	hio_oow_t        dlen
);

HIO_EXPORT int hio_svc_https_setservernamewithbcstr (
	hio_svc_https_t* https,
	const hio_bch_t* server_name
);

/* return the listening device at the given position.
 * not all devices may be up and running */
HIO_EXPORT hio_dev_sck_t* hio_svc_https_getlistendev (
	hio_svc_https_t* https,
	hio_oow_t        idx
);

/* return the total number of listening devices requested to start.
 * not all devices may be up and running */
HIO_EXPORT hio_oow_t hio_sv_https_getnlistendevs (
	hio_svc_https_t* https
);

HIO_EXPORT int hio_svc_https_getsockaddr (
	hio_svc_https_t* https,
	hio_oow_t        idx, /* listener index */
	hio_skad_t*      skad
);

HIO_EXPORT int hio_svc_https_docgi (
	hio_svc_https_t*             https,
	hio_dev_sck_t*               csck,
	hio_htre_t*                  req,
	const hio_bch_t*             docroot,
	const hio_bch_t*             script,
	int                          options,
	hio_svc_https_task_on_kill_t on_kill
);

HIO_EXPORT int hio_svc_https_dofcgi (
	hio_svc_https_t*             https,
	hio_dev_sck_t*               csck,
	hio_htre_t*                  req,
	const hio_skad_t*            fcgis_addr,
	const hio_bch_t*             docroot,
	const hio_bch_t*             script,
	int                          options, /**< 0 or bitwise-Ored of #hio_svc_https_file_option_t enumerators */
	hio_svc_https_task_on_kill_t on_kill
);

HIO_EXPORT int hio_svc_https_dofile (
	hio_svc_https_t*             https,
	hio_dev_sck_t*               csck,
	hio_htre_t*                  req,
	const hio_bch_t*             docroot,
	const hio_bch_t*             filepath,
	const hio_bch_t*             mime_type,
	int                          options,
	hio_svc_https_task_on_kill_t on_kill,
	hio_svc_https_file_cbs_t*    cbs
);

HIO_EXPORT int hio_svc_https_dofcgi (
	hio_svc_https_t*             https,
	hio_dev_sck_t*               csck,
	hio_htre_t*                  req,
	const hio_skad_t*            fcgis_addr,
	const hio_bch_t*             docroot,
	const hio_bch_t*             script,
	int                          options, /**< 0 or bitwise-Ored of #hio_svc_https_file_option_t enumerators */
	hio_svc_https_task_on_kill_t on_kill
);

HIO_EXPORT int hio_svc_https_dopxy (
	hio_svc_https_t*             https,
	hio_dev_sck_t*               csck,
	hio_htre_t*                  req,
	const hio_skad_t*            tgt_addr,
	int                          options,
	hio_svc_https_task_on_kill_t on_kill
);

HIO_EXPORT int hio_svc_https_dothr (
	hio_svc_https_t*             https,
	hio_dev_sck_t*               csck,
	hio_htre_t*                  req,
	hio_svc_https_thr_func_t     func,
	void*                        ctx,
	int                          options,
	hio_svc_https_task_on_kill_t on_kill
);

/* -------------------------------------------------------------- */

typedef struct hio_svc_https_ws_t hio_svc_https_ws_t;

typedef struct hio_svc_https_ws_cbs_t hio_svc_https_ws_cbs_t;
struct hio_svc_https_ws_cbs_t
{
	/** the handshake succeeded and frames may now be sent. optional. */
	int (*on_open) (hio_svc_https_ws_t* ws);

	/**
	 * a piece of a message. \a opcode is #HIO_WS_OPCODE_TEXT or
	 * #HIO_WS_OPCODE_BIN on the first piece of a message and
	 * #HIO_WS_OPCODE_CONT on every piece after it; \a fin is set on the last.
	 * Those are the same three values hio_svc_https_ws_writeframe() takes, so a
	 * piece can be passed straight back out in the shape it arrived in.
	 *
	 * Nothing is accumulated on the way here: a message arrives in as many
	 * pieces as the network delivered it in, which is what lets a large one be
	 * handled without room for all of it at once. Assemble it if you need it
	 * whole. Returning -1 closes the connection.
	 */
	int (*on_data) (hio_svc_https_ws_t* ws, int opcode, int fin, const void* ptr, hio_oow_t len);

	/** the session has ended, with the close code the peer gave or
	 *  #HIO_WS_STATUS_ABNORMAL if it gave none. optional. */
	void (*on_close) (hio_svc_https_ws_t* ws, int code);
};

typedef struct hio_svc_https_ws_opt_t hio_svc_https_ws_opt_t;
struct hio_svc_https_ws_opt_t
{
	/** the subprotocol to answer with, or #HIO_NULL to name none. It must be
	 *  one the client offered; this is not checked here. */
	const hio_bch_t* subproto;

	/** refuse a message larger than this. 0 leaves it unlimited, which is a
	 *  choice about a peer you trust rather than a default worth having. */
	hio_oow_t max_msg_size;

	/**
	 * how long to wait before pinging an idle connection, and how long to
	 * wait for the answer. A websocket connection is exempt from the http
	 * idle deadline for as long as it lives, so without this nothing ever
	 * notices a peer that stopped answering. Zero seconds leaves it off.
	 */
	hio_ntime_t ping_interval;
	hio_ntime_t pong_timeout;
};

/**
 * The hio_svc_https_dows() function answers a websocket handshake and takes the
 * connection over for the session that follows. Call it from the request
 * handler, as with the other task starters.
 *
 * The request is checked against RFC 6455 section 4.2.1 and refused with a 400,
 * or a 426 naming the version this speaks, if it is not a handshake this can
 * answer. Ping and pong and the closing handshake are answered here; \a cbs
 * sees the message frames.
 *
 * \return 0 on success, -1 on failure.
 */
HIO_EXPORT int hio_svc_https_dows (
	hio_svc_https_t*                https,
	hio_dev_sck_t*                  csck,
	hio_htre_t*                     req,
	const hio_svc_https_ws_opt_t*   opt,
	const hio_svc_https_ws_cbs_t*   cbs,
	void*                           ctx,
	hio_svc_https_task_on_kill_t    on_kill
);

/** the context handed to hio_svc_https_dows() */
HIO_EXPORT void* hio_svc_https_ws_getctx (
	hio_svc_https_ws_t* ws
);

HIO_EXPORT hio_t* hio_svc_https_ws_gethio (
	hio_svc_https_ws_t* ws
);

/**
 * The hio_svc_https_ws_write() function sends one whole message as a single
 * frame. \a opcode is #HIO_WS_OPCODE_TEXT or #HIO_WS_OPCODE_BIN.
 *
 * \return 0 on success, -1 on failure.
 */
HIO_EXPORT int hio_svc_https_ws_write (
	hio_svc_https_ws_t* ws,
	int                 opcode,
	const void*         ptr,
	hio_oow_t           len
);

/**
 * The hio_svc_https_ws_writeframe() function sends one frame, which lets a
 * message be sent in pieces: \a fin marks the last of them, and every piece
 * after the first carries #HIO_WS_OPCODE_CONT.
 */
HIO_EXPORT int hio_svc_https_ws_writeframe (
	hio_svc_https_ws_t* ws,
	int                 fin,
	int                 opcode,
	const void*         ptr,
	hio_oow_t           len
);

/**
 * The hio_svc_https_ws_close() function begins the closing handshake. The
 * connection stays open until the peer answers or the task is killed, because
 * the peer is entitled to the rest of what it was sent.
 */
HIO_EXPORT int hio_svc_https_ws_close (
	hio_svc_https_ws_t* ws,
	int                 code,
	const hio_bch_t*    reason
);

/* -------------------------------------------------------------- */

HIO_EXPORT int hio_svc_https_dotxt (
	hio_svc_https_t*             https,
	hio_dev_sck_t*               csck,
	hio_htre_t*                  req,
	int                          res_status_code,
	const hio_bch_t*             content_type,
	const hio_bch_t*             content_text,
	int                          options,
	hio_svc_https_task_on_kill_t on_kill
);

HIO_EXPORT hio_svc_https_task_t* hio_svc_https_task_make (
	hio_svc_https_t*             https,
	hio_oow_t                    task_size,
	hio_svc_https_task_on_kill_t on_kill,
	hio_htre_t*                  req,
	hio_dev_sck_t*               csck
);

/* Take over the client socket for the lifetime of this task: layer 'evcb'
 * on top of the socket's current handlers and make the task the client's
 * current one. Undone by hio_svc_https_task_unbindfromclient(). */
HIO_EXPORT void hio_svc_https_task_bindtoclient (
	hio_svc_https_task_t*     task,
	hio_dev_sck_t*            csck,
	const hio_dev_sck_evcb_t* evcb
);

/* The http service's own client handling. While a task is bound its
 * handlers run instead of these; a task that wants the default behaviour
 * as well calls the matching function explicitly. */
HIO_EXPORT int hio_svc_https_client_default_on_read (
	hio_dev_sck_t*    sck,
	const void*       buf,
	hio_iolen_t       len,
	const hio_skad_t* srcaddr
);

HIO_EXPORT int hio_svc_https_client_default_on_write (
	hio_dev_sck_t*    sck,
	hio_iolen_t       wrlen,
	void*             wrctx,
	const hio_skad_t* dstaddr
);

HIO_EXPORT void hio_svc_https_client_default_on_disconnect (
	hio_dev_sck_t*    sck
);

/* Release the client socket. Pass a non-zero 'rcdown' to drop the reference
 * the binding took; pass 0 when the caller is already inside the task's own
 * destruction path. */
HIO_EXPORT void hio_svc_https_task_unbindfromclient (
	hio_svc_https_task_t* task,
	int                   rcdown
);

/* Stop watching the client for input. For a task that has read all it
 * needs from the request. */
HIO_EXPORT void hio_svc_https_task_stopreadingclient (
	hio_svc_https_task_t* task
);

/* Give up on the client connection, without trying to keep it alive. */
HIO_EXPORT void hio_svc_https_task_haltclient (
	hio_svc_https_task_t* task
);

/* Release the client: keep the connection for the next request on it, or
 * shut it down. The task may be destroyed by this call. */
HIO_EXPORT void hio_svc_https_task_finishclient (
	hio_svc_https_task_t* task
);

HIO_EXPORT void hio_svc_https_task_kill (
	hio_svc_https_task_t* task
);

HIO_EXPORT int hio_svc_https_task_sendfinalres (
	hio_svc_https_task_t* task,
	int                   status_code,
	const hio_bch_t*      content_type,
	const hio_bch_t*      content_text,
	int                   force_close
);

enum hio_svc_https_task_reshdr_flag_t
{
	/** send the body with chunked transfer-encoding */
	HIO_SVC_HTTPS_TASK_RESHDR_CHUNKED       = (1 << 0),

	/**
	 * write \b Connection: \b Upgrade rather than keep-alive or close. A 101
	 * needs it: the connection neither stays alive for another request nor
	 * closes, so neither of the other two words is true of it. It is set here
	 * rather than through hio_svc_https_task_addreshdr(), which refuses the
	 * Connection header so that what becomes of the connection is decided in
	 * one place.
	 */
	HIO_SVC_HTTPS_TASK_RESHDR_UPGRADE = (1 << 1)
};
typedef enum hio_svc_https_task_reshdr_flag_t hio_svc_https_task_reshdr_flag_t;

HIO_EXPORT int hio_svc_https_task_startreshdr (
	hio_svc_https_task_t* task,
	int                   status_code,
	const hio_bch_t*      status_desc,
	int                   flags /**< 0 or bitwise-OR'ed #hio_svc_https_task_reshdr_flag_t enumerators */
);

HIO_EXPORT int hio_svc_https_task_addreshdrs (
	hio_svc_https_task_t*    task,
	const hio_bch_t*         key,
	const hio_htre_hdrval_t* value
);

HIO_EXPORT int hio_svc_https_task_addreshdr (
	hio_svc_https_task_t* task,
	const hio_bch_t*      key,
	const hio_bch_t*      value
);

HIO_EXPORT int hio_svc_https_task_addreshdrfmt (
	hio_svc_https_task_t* task,
	const hio_bch_t*      key,
	const hio_bch_t*      vfmt,
	...
);

HIO_EXPORT int hio_svc_https_task_endreshdr (
	hio_svc_https_task_t* task
);

HIO_EXPORT int hio_svc_https_task_addresbody (
	hio_svc_https_task_t* task,
	const void*           data,
	hio_iolen_t           dlen
);

HIO_EXPORT int hio_svc_https_task_addresbodyfromfile (
	hio_svc_https_task_t* task,
	int                   fd,
	hio_foff_t            foff,
	hio_iolen_t           len
);

HIO_EXPORT int hio_svc_https_task_endbody (
	hio_svc_https_task_t* task
);

HIO_EXPORT int hio_svc_https_task_handleexpect100 (
	hio_svc_https_task_t* task,
	int                   no_continue
);

HIO_EXPORT void hio_svc_https_fmtgmtime (
	hio_svc_https_t*    https,
	const hio_ntime_t*  nt,
	hio_bch_t*          buf,
	hio_oow_t           len
);

HIO_EXPORT hio_bch_t* hio_svc_https_dupmergepaths (
	hio_svc_https_t* https,
	const hio_bch_t* base,
	const hio_bch_t* path
);

#if defined(__cplusplus)
}
#endif


#endif
