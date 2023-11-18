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
#include "http-prv.h"
#include <hio-fmt.h>
#include <hio-chr.h>

#define TXT_ALLOW_UNLIMITED_REQ_CONTENT_LENGTH

#define TXT_OVER_READ_FROM_CLIENT (1 << 0)
#define TXT_OVER_WRITE_TO_CLIENT  (1 << 1)
#define TXT_OVER_ALL (TXT_OVER_READ_FROM_CLIENT | TXT_OVER_WRITE_TO_CLIENT)

struct txt_t
{
	HIO_SVC_HTTS_TASK_HEADER;

	hio_svc_htts_task_on_kill_t on_kill; /* user-provided on_kill callback */

	int options;

	unsigned int over: 2; /* must be large enough to accomodate TXT_OVER_ALL */
	unsigned int client_htrd_recbs_changed: 1;

	hio_dev_sck_on_read_t client_org_on_read;
	hio_dev_sck_on_write_t client_org_on_write;
	hio_dev_sck_on_disconnect_t client_org_on_disconnect;
	hio_htrd_recbs_t client_htrd_org_recbs;
};
typedef struct txt_t txt_t;

static void unbind_task_from_client (txt_t* txt, int rcdown);

static void txt_halt_participating_devices (txt_t* txt)
{
	HIO_DEBUG3 (txt->htts->hio, "HTTS(%p) - Halting participating devices in txt state %p(client=%p)\n", txt->htts, txt, txt->task_csck);
	if (txt->task_csck) hio_dev_sck_halt (txt->task_csck);
}

static HIO_INLINE void txt_mark_over (txt_t* txt, int over_bits)
{
	unsigned int old_over;

	old_over = txt->over;
	txt->over |= over_bits;

	HIO_DEBUG4 (txt->htts->hio, "HTTS(%p) - client=%p new-bits=%x over=%x\n", txt->htts, txt->task_csck, (int)over_bits, (int)txt->over);

	if (!(old_over & TXT_OVER_READ_FROM_CLIENT) && (txt->over & TXT_OVER_READ_FROM_CLIENT))
	{
		if (hio_dev_sck_read(txt->task_csck, 0) <= -1)
		{
			HIO_DEBUG2 (txt->htts->hio, "HTTS(%p) - halting client(%p) for failure to disable input watching\n", txt->htts, txt->task_csck);
			hio_dev_sck_halt (txt->task_csck);
		}
	}

	if (old_over != TXT_OVER_ALL && txt->over == TXT_OVER_ALL)
	{
		/* ready to stop */
		if (txt->task_keep_client_alive)
		{
			HIO_ASSERT (txt->htts->hio, txt->task_client->task == (hio_svc_htts_task_t*)txt);
			unbind_task_from_client (txt, 1);
		}
		else
		{
			HIO_DEBUG2 (txt->htts->hio, "HTTS(%p) - halting client(%p) for no keep-alive\n", txt->htts, txt->task_csck);
			hio_dev_sck_shutdown (txt->task_csck, HIO_DEV_SCK_SHUTDOWN_WRITE);
			hio_dev_sck_halt (txt->task_csck);
		}
	}
}

static void txt_on_kill (hio_svc_htts_task_t* task)
{
	txt_t* txt = (txt_t*)task;
	hio_t* hio = txt->htts->hio;

	HIO_DEBUG2 (hio, "HTTS(%p) - killing txt client(%p)\n", txt->htts, txt->task_csck);

	if (txt->on_kill) txt->on_kill (task);

	if (txt->task_csck)
	{
		HIO_ASSERT (hio, txt->task_client != HIO_NULL);
		unbind_task_from_client (txt, 0);
	}

	if (txt->task_next) HIO_SVC_HTTS_TASKL_UNLINK_TASK (txt); /* detach from the htts service only if it's attached */
}

static int txt_client_htrd_poke (hio_htrd_t* htrd, hio_htre_t* req)
{
	/* client request got completed */
	hio_svc_htts_cli_htrd_xtn_t* htrdxtn = (hio_svc_htts_cli_htrd_xtn_t*)hio_htrd_getxtn(htrd);
	hio_dev_sck_t* sck = htrdxtn->sck;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	txt_t* txt = (txt_t*)cli->task;

	txt_mark_over (txt, TXT_OVER_READ_FROM_CLIENT);
	return 0;
}

static int txt_client_htrd_push_content (hio_htrd_t* htrd, hio_htre_t* req, const hio_bch_t* data, hio_oow_t dlen)
{
	/* discard all contents */
	return 0;
}

static hio_htrd_recbs_t txt_client_htrd_recbs =
{
	HIO_NULL,
	txt_client_htrd_poke,
	txt_client_htrd_push_content
};

static void txt_client_on_disconnect (hio_dev_sck_t* sck)
{
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	txt_t* txt = (txt_t*)cli->task;

	if (txt)
	{
		HIO_SVC_HTTS_TASK_RCUP ((hio_svc_htts_task_t*)txt);

		unbind_task_from_client (txt, 1);

		/* call the parent handler*/
		/*if (txt->client_org_on_disconnect) txt->client_org_on_disconnect (sck);*/
		if (sck->on_disconnect) sck->on_disconnect (sck); /* restored to the orginal parent handler in unbind_task_from_client() */

		HIO_SVC_HTTS_TASK_RCDOWN ((hio_svc_htts_task_t*)txt);
	}
}

static int txt_client_on_read (hio_dev_sck_t* sck, const void* buf, hio_iolen_t len, const hio_skad_t* srcaddr)
{
	hio_t* hio = sck->hio;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	txt_t* txt = (txt_t*)cli->task;
	int n;

	HIO_ASSERT (hio, sck == cli->sck);

	n = txt->client_org_on_read? txt->client_org_on_read(sck, buf, len, srcaddr): 0;

	if (len <= -1)
	{
		/* read error */
		HIO_DEBUG2 (cli->htts->hio, "HTTPS(%p) - read error on client %p(%d)\n", sck, (int)sck->hnd);
		goto oops;
	}

	if (len == 0)
	{
		/* EOF on the client side. arrange to close */
		HIO_DEBUG3 (hio, "HTTPS(%p) - EOF from client %p(hnd=%d)\n", txt->htts, sck, (int)sck->hnd);

		if (!(txt->over & TXT_OVER_READ_FROM_CLIENT)) /* if this is true, EOF is received without txt_client_htrd_poke() */
		{
			txt_mark_over (txt, TXT_OVER_READ_FROM_CLIENT);
		}
	}

	if (n <= -1) goto oops;
	return 0;

oops:
	txt_halt_participating_devices (txt);
	return 0;
}

static int txt_client_on_write (hio_dev_sck_t* sck, hio_iolen_t wrlen, void* wrctx, const hio_skad_t* dstaddr)
{
	hio_t* hio = sck->hio;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	txt_t* txt = (txt_t*)cli->task;
	int n;

	n = txt->client_org_on_write? txt->client_org_on_write(sck, wrlen, wrctx, dstaddr): 0;

	if (wrlen == 0)
	{
		/* since EOF has been indicated to the client, it must not write to the client any further.
		 * this also means that i don't need any data from the peer side either.
		 * i don't need to enable input watching on the peer side */
		txt_mark_over (txt, TXT_OVER_WRITE_TO_CLIENT);
	}
	else if (wrlen > 0)
	{
		if (txt->task_res_pending_writes <= 0)
			txt_mark_over (txt, TXT_OVER_WRITE_TO_CLIENT);
	}

	if (n <= -1 || wrlen <= -1) txt_halt_participating_devices (txt);
	return 0;
}

/* ----------------------------------------------------------------------- */

static void bind_task_to_client (txt_t* txt, hio_dev_sck_t* csck)
{
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(csck);

	HIO_ASSERT (txt->htts->hio, cli->sck == csck);
	HIO_ASSERT (txt->htts->hio, cli->task == HIO_NULL);

	/* txt->task_client and txt->task_csck are set in hio_svc_htts_task_make() */

	/* remember the client socket's io event handlers */
	txt->client_org_on_read = csck->on_read;
	txt->client_org_on_write = csck->on_write;
	txt->client_org_on_disconnect = csck->on_disconnect;

	/* set new io events handlers on the client socket */
	csck->on_read = txt_client_on_read;
	csck->on_write = txt_client_on_write;
	csck->on_disconnect = txt_client_on_disconnect;

	cli->task = (hio_svc_htts_task_t*)txt;
	HIO_SVC_HTTS_TASK_RCUP (txt);
}

static void unbind_task_from_client (txt_t* txt, int rcdown)
{
	hio_dev_sck_t* csck = txt->task_csck;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(csck);

	if (cli->task) /* only if it's bound */
	{

		HIO_ASSERT (txt->htts->hio, txt->task_client != HIO_NULL);
		HIO_ASSERT (txt->htts->hio, txt->task_csck != HIO_NULL);
		HIO_ASSERT (txt->htts->hio, txt->task_client->task == (hio_svc_htts_task_t*)txt);
		HIO_ASSERT (txt->htts->hio, txt->task_client->htrd != HIO_NULL);

		if (txt->client_htrd_recbs_changed)
		{
			hio_htrd_setrecbs (txt->task_client->htrd, &txt->client_htrd_org_recbs);
			txt->client_htrd_recbs_changed = 0;
		}

		if (txt->client_org_on_read)
		{
			csck->on_read = txt->client_org_on_read;
			txt->client_org_on_read = HIO_NULL;
		}

		if (txt->client_org_on_write)
		{
			csck->on_write = txt->client_org_on_write;
			txt->client_org_on_write = HIO_NULL;
		}

		if (txt->client_org_on_disconnect)
		{
			csck->on_disconnect = txt->client_org_on_disconnect;
			txt->client_org_on_disconnect = HIO_NULL;
		}

		/* there is some ordering issue in using HIO_SVC_HTTS_TASK_UNREF()
		* because it can destroy the txt itself. so reset txt->task_client->task
		* to null and call RCDOWN() later */
		txt->task_client->task = HIO_NULL;

		/* these two lines are also done in csck_on_disconnect() in http-svr.c because the socket is destroyed.
		* the same lines here are because the task is unbound while the socket is still alive */
		txt->task_client = HIO_NULL;
		txt->task_csck = HIO_NULL;

		/* enable input watching on the socket being unbound */
		if (txt->task_keep_client_alive && hio_dev_sck_read(csck, 1) <= -1)
		{
			HIO_DEBUG2 (txt->htts->hio, "HTTS(%p) - halting client(%p) for failure to enable input watching\n", txt->htts, csck);
			hio_dev_sck_halt (csck);
		}

		if (rcdown) HIO_SVC_HTTS_TASK_RCDOWN ((hio_svc_htts_task_t*)txt);
	}
}

/* ----------------------------------------------------------------------- */

static int setup_for_content_length(txt_t* txt, hio_htre_t* req)
{
	int have_content;

#if defined(TXT_ALLOW_UNLIMITED_REQ_CONTENT_LENGTH)
	have_content = txt->task_req_conlen > 0 || txt->task_req_conlen_unlimited;
#else
	have_content = txt->task_req_conlen > 0;
#endif

	if (have_content)
	{
		/* change the callbacks to subscribe to contents to be uploaded */
		txt->client_htrd_org_recbs = *hio_htrd_getrecbs(txt->task_client->htrd);
		txt_client_htrd_recbs.peek = txt->client_htrd_org_recbs.peek;
		hio_htrd_setrecbs (txt->task_client->htrd, &txt_client_htrd_recbs);
		txt->client_htrd_recbs_changed = 1;
	}
	else
	{
		/* no content to be uploaded from the client */
		txt_mark_over (txt, TXT_OVER_READ_FROM_CLIENT);
	}

	return 0;
}

/* ----------------------------------------------------------------------- */

int hio_svc_htts_dotxt (hio_svc_htts_t* htts, hio_dev_sck_t* csck, hio_htre_t* req, int res_status_code, const hio_bch_t* content_type, const hio_bch_t* content_text, int options, hio_svc_htts_task_on_kill_t on_kill)
{
	hio_t* hio = htts->hio;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(csck);
	txt_t* txt = HIO_NULL;
	int status_code = HIO_HTTP_STATUS_INTERNAL_SERVER_ERROR;
	int bound_to_client = 0;

	/* ensure that you call this function before any contents is received */
	HIO_ASSERT (hio, hio_htre_getcontentlen(req) == 0);
	HIO_ASSERT (hio, cli->sck == csck);

	if (cli->task)
	{
		hio_seterrbfmt (hio, HIO_EPERM, "duplicate task request prohibited");
		goto oops;
	}

	txt = (txt_t*)hio_svc_htts_task_make(htts, HIO_SIZEOF(*txt), txt_on_kill, req, csck);
	if (HIO_UNLIKELY(!txt)) goto oops;
	HIO_SVC_HTTS_TASK_RCUP ((hio_svc_htts_task_t*)txt);

	txt->options = options;

	bind_task_to_client (txt, csck);
	bound_to_client = 1;

	if (hio_svc_htts_task_handleexpect100(txt, 1) <= -1) goto oops;
	if (setup_for_content_length(txt, req) <= -1) goto oops;

	/* TODO: store current input watching state and use it when destroying the txt data */
	if (hio_dev_sck_read(csck, !(txt->over & TXT_OVER_READ_FROM_CLIENT)) <= -1) goto oops;

	if (hio_svc_htts_task_sendfinalres(txt, res_status_code, content_type, content_text, 0) <= -1) goto oops;

	HIO_SVC_HTTS_TASKL_APPEND_TASK (&htts->task, (hio_svc_htts_task_t*)txt);
	HIO_SVC_HTTS_TASK_RCDOWN ((hio_svc_htts_task_t*)txt);

	/* set the on_kill callback only if this function can return success.
	 * the on_kill callback won't be executed if this function returns failure. */
	txt->on_kill = on_kill;
	return 0;

oops:
	HIO_DEBUG2 (hio, "HTTS(%p) - FAILURE in dotxt - socket(%p)\n", htts, csck);
	if (txt)
	{
		hio_svc_htts_task_sendfinalres(txt, status_code, HIO_NULL, HIO_NULL, 1);
		if (bound_to_client) unbind_task_from_client (txt, 1);
		txt_halt_participating_devices (txt);
		HIO_SVC_HTTS_TASK_RCDOWN ((hio_svc_htts_task_t*)txt);
	}
	return -1;
}
