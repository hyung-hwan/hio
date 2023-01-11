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

#ifndef _HIO_HTTP_PRV_H_
#define _HIO_HTTP_PRV_H_

#include <hio-http.h>
#include <hio-htrd.h>
#include <hio-sck.h>
#include "hio-prv.h"

typedef struct hio_svc_htts_cli_t hio_svc_htts_cli_t;
struct hio_svc_htts_cli_t
{
	hio_svc_htts_cli_t* cli_prev;
	hio_svc_htts_cli_t* cli_next;

	/* a listener socket sets htts, sck, and l_idx fields only */
	/* a client sockets uses all the fields in this struct */
	hio_svc_htts_t* htts;
	hio_dev_sck_t* sck;
	hio_skad_t cli_addr;
	hio_bch_t cli_addr_bcstr[HIO_SKAD_IP_STRLEN + 1];
	hio_oow_t l_idx; /* listening socket: < htts->l.count, client socket: >= htts->l.count */

	hio_htrd_t* htrd;
	hio_becs_t* sbuf; /* temporary buffer for status line formatting */

	hio_svc_htts_task_t* task;
	hio_ntime_t last_active;
};

struct hio_svc_htts_cli_htrd_xtn_t
{
	hio_dev_sck_t* sck;
};
typedef struct hio_svc_htts_cli_htrd_xtn_t hio_svc_htts_cli_htrd_xtn_t;

struct hio_svc_htts_t
{
	HIO_SVC_HEADER;

	hio_svc_htts_proc_req_t proc_req;

	struct
	{
		hio_dev_sck_t** sck;
		hio_oow_t count;
	} l;
	/*hio_dev_sck_t* lsck;*/
	hio_svc_fcgic_t* fcgic;

	hio_svc_htts_cli_t cli; /* list head for client list */
	hio_svc_htts_task_t task; /* list head for task list */
	hio_tmridx_t idle_tmridx;

	hio_bch_t* server_name;
	hio_bch_t server_name_buf[64];
};

struct hio_svc_httc_t
{
	HIO_SVC_HEADER;
};

/* client list */
#define HIO_SVC_HTTS_CLIL_APPEND_CLI(lh,cli) do { \
	(cli)->cli_next = (lh); \
	(cli)->cli_prev = (lh)->cli_prev; \
	(cli)->cli_prev->cli_next = (cli); \
	(lh)->cli_prev = (cli); \
} while(0)

#define HIO_SVC_HTTS_CLIL_UNLINK_CLI(cli) do { \
	(cli)->cli_prev->cli_next = (cli)->cli_next; \
	(cli)->cli_next->cli_prev = (cli)->cli_prev; \
} while (0)

#define HIO_SVC_HTTS_CLIL_UNLINK_CLI_CLEAN(cli) do { \
	(cli)->cli_prev->cli_next = (cli)->cli_next; \
	(cli)->cli_next->cli_prev = (cli)->cli_prev; \
	(cli)->cli_prev = (cli); \
	(cli)->cli_next = (cli); \
} while (0)

#define HIO_SVC_HTTS_CLIL_INIT(lh) ((lh)->cli_next = (lh)->cli_prev = lh)
#define HIO_SVC_HTTS_CLIL_FIRST_CLI(lh) ((lh)->cli_next)
#define HIO_SVC_HTTS_CLIL_LAST_CLI(lh) ((lh)->cli_prev)
#define HIO_SVC_HTTS_CLIL_IS_EMPTY(lh) (HIO_SVC_HTTS_CLIL_FIRST_CLI(lh) == (lh))
#define HIO_SVC_HTTS_CLIL_IS_NIL_CLI(lh,cli) ((cli) == (lh))


/* task list */
#define HIO_SVC_HTTS_TASKL_APPEND_TASK(lh,task) do { \
	(task)->task_next = (lh); \
	(task)->task_prev = (lh)->task_prev; \
	(task)->task_prev->task_next = (task); \
	(lh)->task_prev = (task); \
} while(0)

#define HIO_SVC_HTTS_TASKL_UNLINK_TASK(task) do { \
	(task)->task_prev->task_next = (task)->task_next; \
	(task)->task_next->task_prev = (task)->task_prev; \
} while (0)

#define HIO_SVC_HTTS_TASKL_UNLINK_TASK_CLEAN(task) do { \
	(task)->task_prev->task_next = (task)->task_next; \
	(task)->task_next->task_prev = (task)->task_prev; \
	(task)->task_prev = (task); \
	(task)->task_next = (task); \
} while (0)

#define HIO_SVC_HTTS_TASKL_INIT(lh) ((lh)->task_next = (lh)->task_prev = lh)
#define HIO_SVC_HTTS_TASKL_FIRST_TASK(lh) ((lh)->task_next)
#define HIO_SVC_HTTS_TASKL_LAST_TASK(lh) ((lh)->task_prev)
#define HIO_SVC_HTTS_TASKL_IS_EMPTY(lh) (HIO_SVC_HTTS_TASKL_FIRST_TASK(lh) == (lh))
#define HIO_SVC_HTTS_TASKL_IS_NIL_TASK(lh,task) ((task) == (lh))

#endif
