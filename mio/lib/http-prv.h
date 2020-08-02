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

#ifndef _MIO_HTTP_PRV_H_
#define _MIO_HTTP_PRV_H_

#include <mio-http.h>
#include <mio-htrd.h>
#include <mio-sck.h>
#include "mio-prv.h"

typedef struct mio_svc_htts_cli_t mio_svc_htts_cli_t;
struct mio_svc_htts_cli_t
{
	mio_svc_htts_cli_t* cli_prev;
	mio_svc_htts_cli_t* cli_next;

	/* a listener socket sets htts and sck fields only */
	/* a client sockets uses all the fields in this struct */
	mio_svc_htts_t* htts;
	mio_dev_sck_t* sck;

	mio_htrd_t* htrd;
	mio_becs_t* sbuf; /* temporary buffer for status line formatting */

	mio_svc_htts_rsrc_t* rsrc;
	mio_ntime_t last_active;
};

struct mio_svc_htts_cli_htrd_xtn_t
{
	mio_dev_sck_t* sck;
};
typedef struct mio_svc_htts_cli_htrd_xtn_t mio_svc_htts_cli_htrd_xtn_t;

struct mio_svc_htts_t
{
	MIO_SVC_HEADER;

	mio_svc_htts_proc_req_t proc_req;

	mio_dev_sck_t* lsck;
	mio_svc_htts_cli_t cli; /* list head for client list */
	mio_tmridx_t idle_tmridx;

	mio_bch_t* server_name;
	mio_bch_t server_name_buf[64];
};

struct mio_svc_httc_t
{
	MIO_SVC_HEADER;
};

#define MIO_SVC_HTTS_CLIL_APPEND_CLI(lh,cli) do { \
	(cli)->cli_next = (lh); \
	(cli)->cli_prev = (lh)->cli_prev; \
	(cli)->cli_prev->cli_next = (cli); \
	(lh)->cli_prev = (cli); \
} while(0)

#define MIO_SVC_HTTS_CLIL_UNLINK_CLI(cli) do { \
	(cli)->cli_prev->cli_next = (cli)->cli_next; \
	(cli)->cli_next->cli_prev = (cli)->cli_prev; \
} while (0)

#define MIO_SVC_HTTS_CLIL_UNLINK_CLI_CLEAN(cli) do { \
	(cli)->cli_prev->cli_next = (cli)->cli_next; \
	(cli)->cli_next->cli_prev = (cli)->cli_prev; \
	(cli)->cli_prev = (cli); \
	(cli)->cli_next = (cli); \
} while (0)

#define MIO_SVC_HTTS_CLIL_INIT(lh) ((lh)->cli_next = (lh)->cli_prev = lh)
#define MIO_SVC_HTTS_CLIL_FIRST_CLI(lh) ((lh)->cli_next)
#define MIO_SVC_HTTS_CLIL_LAST_CLI(lh) ((lh)->cli_prev)
#define MIO_SVC_HTTS_CLIL_IS_EMPTY(lh) (MIO_SVC_HTTS_CLIL_FIRST_CLI(lh) == (lh))
#define MIO_SVC_HTTS_CLIL_IS_NIL_CLI(lh,cli) ((cli) == (lh))

#endif
