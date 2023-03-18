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

#define TXT_OVER_READ_FROM_CLIENT (1 << 0)
#define TXT_OVER_WRITE_TO_CLIENT  (1 << 1)
#define TXT_OVER_ALL (TXT_OVER_READ_FROM_CLIENT | TXT_OVER_WRITE_TO_CLIENT)

struct txt_t
{
	HIO_SVC_HTTS_TASK_HEADER;

	hio_svc_htts_task_on_kill_t on_kill; /* user-provided on_kill callback */

	int options;
	hio_oow_t num_pending_writes_to_client;

	unsigned int over: 2; /* must be large enough to accomodate TXT_OVER_ALL */
	unsigned int client_eof_detected: 1;
	unsigned int client_disconnected: 1;
	unsigned int client_htrd_recbs_changed: 1;

	hio_dev_sck_on_read_t client_org_on_read;
	hio_dev_sck_on_write_t client_org_on_write;
	hio_dev_sck_on_disconnect_t client_org_on_disconnect;
	hio_htrd_recbs_t client_htrd_org_recbs;
};
typedef struct txt_t txt_t;

static void txt_halt_participating_devices (txt_t* txt)
{
	HIO_DEBUG3 (txt->htts->hio, "HTTS(%p) - Halting participating devices in txt state %p(client=%p)\n", txt->htts, txt, txt->task_csck);
	if (txt->task_csck) hio_dev_sck_halt (txt->task_csck);
}

static int txt_write_to_client (txt_t* txt, const void* data, hio_iolen_t dlen)
{
	if (txt->task_csck)
	{
		txt->num_pending_writes_to_client++;
		if (hio_dev_sck_write(txt->task_csck, data, dlen, HIO_NULL, HIO_NULL) <= -1)
		{
			txt->num_pending_writes_to_client--;
			return -1;
		}
	}
	return 0;
}

static int txt_send_final_status_to_client (txt_t* txt, int status_code, const hio_bch_t* content_type, const hio_bch_t* content_text, int force_close)
{
	return hio_svc_htts_task_sendfinalres(txt, status_code, content_type, content_text, force_close);
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
		if (txt->task_keep_client_alive && !txt->client_eof_detected)
		{
			/* how to arrange to delete this txt object and put the socket back to the normal waiting state??? */
			HIO_ASSERT (txt->htts->hio, txt->task_client->task == (hio_svc_htts_task_t*)txt);

/*printf ("DETACHING FROM THE MAIN CLIENT TASK... state -> %p\n", txt->task_client->task);*/
			HIO_SVC_HTTS_TASK_UNREF (txt->task_client->task);
			/* txt must not be access from here down as it could have been destroyed */
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

		if (txt->client_org_on_read) txt->task_csck->on_read = txt->client_org_on_read;
		if (txt->client_org_on_write) txt->task_csck->on_write = txt->client_org_on_write;
		if (txt->client_org_on_disconnect) txt->task_csck->on_disconnect = txt->client_org_on_disconnect;
		if (txt->client_htrd_recbs_changed)
		hio_htrd_setrecbs (txt->task_client->htrd, &txt->client_htrd_org_recbs);

		if (!txt->client_disconnected)
		{
			if (!txt->task_keep_client_alive || hio_dev_sck_read(txt->task_csck, 1) <= -1)
			{
				HIO_DEBUG2 (hio, "HTTS(%p) - halting client(%p) for failure to enable input watching\n", txt->htts, txt->task_csck);
				hio_dev_sck_halt (txt->task_csck);
			}
		}
	}

	txt->client_org_on_read = HIO_NULL;
	txt->client_org_on_write = HIO_NULL;
	txt->client_org_on_disconnect = HIO_NULL;
	txt->client_htrd_recbs_changed = 0;

	if (txt->task_next) HIO_SVC_HTTS_TASKL_UNLINK_TASK (txt); /* detach from the htts service only if it's attached */
}

static int txt_client_htrd_poke (hio_htrd_t* htrd, hio_htre_t* req)
{
	/* client request got completed */
	hio_svc_htts_cli_htrd_xtn_t* htrdxtn = (hio_svc_htts_cli_htrd_xtn_t*)hio_htrd_getxtn(htrd);
	hio_dev_sck_t* sck = htrdxtn->sck;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	txt_t* txt = (txt_t*)cli->task;

/*printf (">> CLIENT REQUEST COMPLETED\n");*/

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
	txt->client_disconnected = 1;
	txt->client_org_on_disconnect (sck);
}

static int txt_client_on_read (hio_dev_sck_t* sck, const void* buf, hio_iolen_t len, const hio_skad_t* srcaddr)
{
	hio_t* hio = sck->hio;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	txt_t* txt = (txt_t*)cli->task;

	HIO_ASSERT (hio, sck == cli->sck);

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
		txt->client_eof_detected = 1;

		if (!(txt->over & TXT_OVER_READ_FROM_CLIENT)) /* if this is true, EOF is received without txt_client_htrd_poke() */
		{
			txt_mark_over (txt, TXT_OVER_READ_FROM_CLIENT);
		}
	}
	else
	{
		hio_oow_t rem;

		HIO_ASSERT (hio, !(txt->over & TXT_OVER_READ_FROM_CLIENT));

		if (hio_htrd_feed(cli->htrd, buf, len, &rem) <= -1) goto oops;

		if (rem > 0)
		{
			/* TODO store this to client buffer. once the current resource is completed, arrange to call on_read() with it */
/*printf ("UUUUUUUUUUUUUUUUUUUUUUUUUUGGGGGHHHHHHHHHHHH .......... TXT CLIENT GIVING EXCESSIVE DATA AFTER CONTENTS...\n");*/
		}
	}

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

	if (wrlen <= -1)
	{
		HIO_DEBUG3 (hio, "HTTPS(%p) - unable to write to client %p(%d)\n", sck->hio, sck, (int)sck->hnd);
		goto oops;
	}

	if (wrlen == 0)
	{
		/* if the connect is keep-alive, this part may not be called */
		txt->num_pending_writes_to_client--;
		HIO_ASSERT (hio, txt->num_pending_writes_to_client == 0);
		HIO_DEBUG3 (hio, "HTTS(%p) - indicated EOF to client %p(%d)\n", txt->htts, sck, (int)sck->hnd);
		/* since EOF has been indicated to the client, it must not write to the client any further.
		 * this also means that i don't need any data from the peer side either.
		 * i don't need to enable input watching on the peer side */
		txt_mark_over (txt, TXT_OVER_WRITE_TO_CLIENT);
	}
	else
	{
		HIO_ASSERT (hio, txt->num_pending_writes_to_client > 0);
		txt->num_pending_writes_to_client--;
		if (txt->num_pending_writes_to_client <= 0)
		{
			txt_mark_over (txt, TXT_OVER_WRITE_TO_CLIENT);
		}
	}

	return 0;

oops:
	txt_halt_participating_devices (txt);
	return 0;
}

int hio_svc_htts_dotxt (hio_svc_htts_t* htts, hio_dev_sck_t* csck, hio_htre_t* req, int status_code, const hio_bch_t* content_type, const hio_bch_t* content_text, int options, hio_svc_htts_task_on_kill_t on_kill)
{
	hio_t* hio = htts->hio;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(csck);
	txt_t* txt = HIO_NULL;

	/* ensure that you call this function before any contents is received */
	HIO_ASSERT (hio, hio_htre_getcontentlen(req) == 0);
	HIO_ASSERT (hio, cli->sck == csck);

	txt = (txt_t*)hio_svc_htts_task_make(htts, HIO_SIZEOF(*txt), txt_on_kill, req, csck);
	if (HIO_UNLIKELY(!txt)) goto oops;

	txt->on_kill = on_kill;
	txt->options = options;

	txt->client_org_on_read = csck->on_read;
	txt->client_org_on_write = csck->on_write;
	txt->client_org_on_disconnect = csck->on_disconnect;
	csck->on_read = txt_client_on_read;
	csck->on_write = txt_client_on_write;
	csck->on_disconnect = txt_client_on_disconnect;

	HIO_ASSERT (hio, cli->task == HIO_NULL);
	HIO_SVC_HTTS_TASK_REF ((hio_svc_htts_task_t*)txt, cli->task);

	if (req->flags & HIO_HTRE_ATTR_EXPECT100)
	{
		/* don't send 100-Continue. If the client posts data regardless, ignore them later */
	}
	else if (req->flags & HIO_HTRE_ATTR_EXPECT)
	{
		/* 417 Expectation Failed */
		txt_send_final_status_to_client(txt, HIO_HTTP_STATUS_EXPECTATION_FAILED, HIO_NULL, HIO_NULL, 1);
		goto oops;
	}

	if (txt->task_req_conlen_unlimited || txt->task_req_conlen > 0)
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
		/* indicate EOF to the peer and disable input wathching from the client */
		txt_mark_over (txt, TXT_OVER_READ_FROM_CLIENT);
	}

	/* TODO: store current input watching state and use it when destroying the txt data */
	if (hio_dev_sck_read(csck, !(txt->over & TXT_OVER_READ_FROM_CLIENT)) <= -1) goto oops;

	if (txt_send_final_status_to_client(txt, status_code, content_type, content_text, 0) <= -1) goto oops;

	HIO_SVC_HTTS_TASKL_APPEND_TASK (&htts->task, (hio_svc_htts_task_t*)txt);
	return 0;

oops:
	HIO_DEBUG2 (hio, "HTTS(%p) - FAILURE in dotxt - socket(%p)\n", htts, csck);
	if (txt) txt_halt_participating_devices (txt);
	return -1;
}
