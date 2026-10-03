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

#ifndef _HIO_HTTPC_H_
#define _HIO_HTTPC_H_

#include <hio-http.h>

enum hio_svc_httpc_conn_proto_t
{
	/** tcp for AF_INET/AF_INET6, and the only sensible transport for the
	 *  other families */
	HIO_SVC_HTTPS_BIND_PROTO_DEFAULT = 0,

	/** sctp. valid for AF_INET and AF_INET6 only; a conn requesting it for
	 *  any other family is skipped. */
	HIO_SVC_HTTPS_BIND_PROTO_SCTP
};
typedef enum hio_svc_httpc_conn_proto_t hio_svc_httpc_conn_proto_t;

/**
 * One listening address for hio_svc_httpc_start().
 *
 * This wraps hio_dev_sck_conn_t rather than extending it, because some of what
 * the service needs is settled when the socket is *made* and not when it is
 * bound - the transport, and the sctp stream counts. A caller who drives
 * hio_dev_sck_make() directly passes those through hio_dev_sck_make_t; a
 * caller of this service never sees that struct, so they have to travel here.
 */
struct hio_svc_httpc_conn_t
{
	hio_svc_httpc_conn_proto_t proto;

	/** the address and the conn-time options, including the ssl certificate */
	hio_dev_sck_connect_t conn;

	/** number of outbound sctp streams to request, 0 for the system default.
	 *  ignored unless proto is #HIO_SVC_HTTPS_BIND_PROTO_SCTP. */
	hio_uint16_t sctp_ostreams;
	/** maximum number of inbound sctp streams to accept, 0 for the default. */
	hio_uint16_t sctp_instreams;
};
typedef struct hio_svc_httpc_conn_t hio_svc_httpc_conn_t;

typedef struct hio_svc_httpc_t hio_svc_httpc_t;

#if defined(__cplusplus)
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* HTTP CLIENT SERVICE                                                       */
/* ------------------------------------------------------------------------- */

HIO_EXPORT hio_svc_httpc_t* hio_svc_httpc_start (
	hio_t*                       hio,
	hio_oow_t                    xtnsize,
	hio_svc_httpc_conn_t*        conns,
	hio_oow_t                    nconns/*,
	hio_svc_httpc_proc_req_t     proc_req*/
);

HIO_EXPORT void hio_svc_httpc_stop (
	hio_svc_httpc_t* httpc
);

HIO_EXPORT void* hio_svc_httpc_getxtn (
	hio_svc_httpc_t* httpc
);

#if defined(HIO_HAVE_INLINE)
static HIO_INLINE hio_t* hio_svc_httpc_gethio(hio_svc_httpc_t* svc) { return hio_svc_gethio((hio_svc_t*)svc); }
#else
#	define hio_svc_httpc_gethio(svc) hio_svc_gethio(svc)
#endif

/*
HIO_EXPORT int hio_svc_httpc_getoption (
	hio_svc_httpc_t*       httpc,
	hio_svc_httpc_option_t id,
	void*                  value
);

HIO_EXPORT int hio_svc_httpc_setoption (
	hio_svc_httpc_t*       httpc,
	hio_svc_httpc_option_t id,
	const void*            value
);
*/

#if defined(__cplusplus)
}
#endif


#endif
