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
#include "http-prv.h"
#include <hio-fmt.h>
#include <hio-chr.h>

#define TXT_OVER_READ_FROM_CLIENT (1 << 0)
#define TXT_OVER_WRITE_TO_CLIENT  (1 << 1)
#define TXT_OVER_ALL (TXT_OVER_READ_FROM_CLIENT | TXT_OVER_WRITE_TO_CLIENT)

struct txt_t
{
	HIO_SVC_HTTS_RSRC_HEADER;

	hio_oow_t num_pending_writes_to_client;
	hio_svc_htts_cli_t* client;
	hio_http_version_t req_version; /* client request */

	unsigned int over: 2; /* must be large enough to accomodate TXT_OVER_ALL */
	unsigned int keep_alive: 1;
	unsigned int req_content_length_unlimited: 1;
	unsigned int client_disconnected: 1;
	unsigned int client_htrd_recbs_changed: 1;
	hio_oow_t req_content_length; /* client request content length */

	hio_dev_sck_on_read_t client_org_on_read;
	hio_dev_sck_on_write_t client_org_on_write;
	hio_dev_sck_on_disconnect_t client_org_on_disconnect;
	hio_htrd_recbs_t client_htrd_org_recbs;
};
typedef struct txt_t txt_t;

static void txt_halt_participating_devices (txt_t* txt)
{
	HIO_ASSERT (txt->client->htts->hio, txt->client != HIO_NULL);
	HIO_ASSERT (txt->client->htts->hio, txt->client->sck != HIO_NULL);
	HIO_DEBUG3 (txt->client->htts->hio, "HTTS(%p) - Halting participating devices in txt state %p(client=%p)\n", txt->client->htts, txt, txt->client->sck);
	hio_dev_sck_halt (txt->client->sck);
}

static int txt_write_to_client (txt_t* txt, const void* data, hio_iolen_t dlen)
{
	txt->num_pending_writes_to_client++;
	if (hio_dev_sck_write(txt->client->sck, data, dlen, HIO_NULL, HIO_NULL) <= -1) 
	{
		txt->num_pending_writes_to_client--;
		return -1;
	}
	return 0;
}

#if 0
static int txt_writev_to_client (txt_t* txt, hio_iovec_t* iov, hio_iolen_t iovcnt)
{
	txt->num_pending_writes_to_client++;
	if (hio_dev_sck_writev(txt->client->sck, iov, iovcnt, HIO_NULL, HIO_NULL) <= -1) 
	{
		txt->num_pending_writes_to_client--;
		return -1;
	}
	return 0;
}
#endif

static int txt_send_final_status_to_client (txt_t* txt, int status_code, const char* content_type, const char* content_text, int force_close)
{
	hio_svc_htts_cli_t* cli = txt->client;
	hio_bch_t dtbuf[64];
	hio_oow_t content_text_len = 0;

	hio_svc_htts_fmtgmtime (cli->htts, HIO_NULL, dtbuf, HIO_COUNTOF(dtbuf));

	if (!force_close) force_close = !txt->keep_alive;

	if (hio_becs_fmt(cli->sbuf, "HTTP/%d.%d %d %hs\r\nServer: %hs\r\nDate: %s\r\nConnection: %hs\r\n",
		txt->req_version.major, txt->req_version.minor,
		status_code, hio_http_status_to_bcstr(status_code),
		cli->htts->server_name, dtbuf,
		(force_close? "close": "keep-alive"),
		(content_text? hio_count_bcstr(content_text): 0), (content_text? content_text: "")) == (hio_oow_t)-1) return -1;

	if (content_text)
	{
		content_text_len = hio_count_bcstr(content_text);
		if (content_type && hio_becs_fcat(cli->sbuf, "Content-Type: %hs\r\n", content_type) == (hio_oow_t)-1) return -1;
	}
	if (hio_becs_fcat(cli->sbuf, "Content-Length: %zu\r\n\r\n", content_text_len) == (hio_oow_t)-1) return -1;

	return (txt_write_to_client(txt, HIO_BECS_PTR(cli->sbuf), HIO_BECS_LEN(cli->sbuf)) <= -1 ||
	        (content_text && txt_write_to_client(txt, content_text, content_text_len) <= -1) ||
	        (force_close && txt_write_to_client(txt, HIO_NULL, 0) <= -1))? -1: 0;
}

static HIO_INLINE void txt_mark_over (txt_t* txt, int over_bits)
{
	unsigned int old_over;

	old_over = txt->over;
	txt->over |= over_bits;

	HIO_DEBUG4 (txt->htts->hio, "HTTS(%p) - client=%p new-bits=%x over=%x\n", txt->htts, txt->client->sck, (int)over_bits, (int)txt->over);

	if (!(old_over & TXT_OVER_READ_FROM_CLIENT) && (txt->over & TXT_OVER_READ_FROM_CLIENT))
	{
		if (hio_dev_sck_read(txt->client->sck, 0) <= -1) 
		{
			HIO_DEBUG2 (txt->htts->hio, "HTTS(%p) - halting client(%p) for failure to disable input watching\n", txt->htts, txt->client->sck);
			hio_dev_sck_halt (txt->client->sck);
		}
	}

	if (old_over != TXT_OVER_ALL && txt->over == TXT_OVER_ALL)
	{
		/* ready to stop */
		if (txt->keep_alive) 
		{
			/* how to arrange to delete this txt object and put the socket back to the normal waiting state??? */
			HIO_ASSERT (txt->htts->hio, txt->client->rsrc == (hio_svc_htts_rsrc_t*)txt);

printf ("DETACHING FROM THE MAIN CLIENT RSRC... state -> %p\n", txt->client->rsrc);
			HIO_SVC_HTTS_RSRC_DETACH (txt->client->rsrc);
			/* txt must not be access from here down as it could have been destroyed */
		}
		else
		{
			HIO_DEBUG2 (txt->htts->hio, "HTTS(%p) - halting client(%p) for no keep-alive\n", txt->htts, txt->client->sck);
			hio_dev_sck_shutdown (txt->client->sck, HIO_DEV_SCK_SHUTDOWN_WRITE);
			hio_dev_sck_halt (txt->client->sck);
		}
	}
}

static void txt_on_kill (txt_t* txt)
{
	hio_t* hio = txt->htts->hio;

	HIO_DEBUG2 (hio, "HTTS(%p) - killing txt client(%p)\n", txt->htts, txt->client->sck);

	if (txt->client_org_on_read)
	{
		txt->client->sck->on_read = txt->client_org_on_read;
		txt->client_org_on_read = HIO_NULL;
	}

	if (txt->client_org_on_write)
	{
		txt->client->sck->on_write = txt->client_org_on_write;
		txt->client_org_on_write = HIO_NULL;
	}

	if (txt->client_org_on_disconnect)
	{
		txt->client->sck->on_disconnect = txt->client_org_on_disconnect;
		txt->client_org_on_disconnect = HIO_NULL;
	}

	if (txt->client_htrd_recbs_changed)
	{
		/* restore the callbacks */
		hio_htrd_setrecbs (txt->client->htrd, &txt->client_htrd_org_recbs); 
	}

	if (!txt->client_disconnected)
	{
/*printf ("ENABLING INPUT WATCHING on CLIENT %p. \n", txt->client->sck);*/
		if (!txt->keep_alive || hio_dev_sck_read(txt->client->sck, 1) <= -1)
		{
			HIO_DEBUG2 (hio, "HTTS(%p) - halting client(%p) for failure to enable input watching\n", txt->htts, txt->client->sck);
			hio_dev_sck_halt (txt->client->sck);
		}
	}

/*printf ("**** TXT_ON_KILL DONE\n");*/
}

static int txt_client_htrd_poke (hio_htrd_t* htrd, hio_htre_t* req)
{
	/* client request got completed */
	hio_svc_htts_cli_htrd_xtn_t* htrdxtn = (hio_svc_htts_cli_htrd_xtn_t*)hio_htrd_getxtn(htrd);
	hio_dev_sck_t* sck = htrdxtn->sck;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	txt_t* txt = (txt_t*)cli->rsrc;

printf (">> CLIENT REQUEST COMPLETED\n");

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
	txt_t* txt = (txt_t*)cli->rsrc;
	txt->client_disconnected = 1;
	txt->client_org_on_disconnect (sck);
}

static int txt_client_on_read (hio_dev_sck_t* sck, const void* buf, hio_iolen_t len, const hio_skad_t* srcaddr)
{
	hio_t* hio = sck->hio;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	txt_t* txt = (txt_t*)cli->rsrc;

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
		HIO_DEBUG3 (hio, "HTTPS(%p) - EOF from client %p(hnd=%d)\n", txt->client->htts, sck, (int)sck->hnd);

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
printf ("UUUUUUUUUUUUUUUUUUUUUUUUUUGGGGGHHHHHHHHHHHH .......... TXT CLIENT GIVING EXCESSIVE DATA AFTER CONTENTS...\n");
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
	txt_t* txt = (txt_t*)cli->rsrc;

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
		HIO_DEBUG3 (hio, "HTTS(%p) - indicated EOF to client %p(%d)\n", txt->client->htts, sck, (int)sck->hnd);
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

int hio_svc_htts_dotxt (hio_svc_htts_t* htts, hio_dev_sck_t* csck, hio_htre_t* req, int status_code, const hio_bch_t* content_type, const hio_bch_t* content_text)
{
	hio_t* hio = htts->hio;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(csck);
	txt_t* txt = HIO_NULL;

	/* ensure that you call this function before any contents is received */
	HIO_ASSERT (hio, hio_htre_getcontentlen(req) == 0);

	txt = (txt_t*)hio_svc_htts_rsrc_make(htts, HIO_SIZEOF(*txt), txt_on_kill);
	if (HIO_UNLIKELY(!txt)) goto oops;

	txt->client = cli;
	/*txt->num_pending_writes_to_client = 0;*/
	txt->req_version = *hio_htre_getversion(req);
	txt->req_content_length_unlimited = hio_htre_getreqcontentlen(req, &txt->req_content_length);

	txt->client_org_on_read = csck->on_read;
	txt->client_org_on_write = csck->on_write;
	txt->client_org_on_disconnect = csck->on_disconnect;
	csck->on_read = txt_client_on_read;
	csck->on_write = txt_client_on_write;
	csck->on_disconnect = txt_client_on_disconnect;

	HIO_ASSERT (hio, cli->rsrc == HIO_NULL);
	HIO_SVC_HTTS_RSRC_ATTACH (txt, cli->rsrc);

	if (req->flags & HIO_HTRE_ATTR_EXPECT100)
	{
		/* don't send 100-Continue. If the client posts data regardless, ignore them later */
	}
	else if (req->flags & HIO_HTRE_ATTR_EXPECT)
	{
		/* 417 Expectation Failed */
		txt_send_final_status_to_client(txt, 417, HIO_NULL, HIO_NULL, 1);
		goto oops;
	}

	if (txt->req_content_length_unlimited || txt->req_content_length > 0)
	{
		/* change the callbacks to subscribe to contents to be uploaded */
		txt->client_htrd_org_recbs = *hio_htrd_getrecbs(txt->client->htrd);
		txt_client_htrd_recbs.peek = txt->client_htrd_org_recbs.peek;
		hio_htrd_setrecbs (txt->client->htrd, &txt_client_htrd_recbs);
		txt->client_htrd_recbs_changed = 1;
	}
	else
	{
		/* no content to be uploaded from the client */
		/* indicate EOF to the peer and disable input wathching from the client */
		txt_mark_over (txt, TXT_OVER_READ_FROM_CLIENT);
	}

	/* this may change later if Content-Length is included in the txt output */
	txt->keep_alive = !!(req->flags & HIO_HTRE_ATTR_KEEPALIVE);

	/* TODO: store current input watching state and use it when destroying the txt data */
	if (hio_dev_sck_read(csck, !(txt->over & TXT_OVER_READ_FROM_CLIENT)) <= -1) goto oops;

	if (txt_send_final_status_to_client(txt, status_code, content_type, content_text, 0) <= -1) goto oops;
	return 0;

oops:
	HIO_DEBUG2 (hio, "HTTS(%p) - FAILURE in dotxt - socket(%p)\n", htts, csck);
	if (txt) txt_halt_participating_devices (txt);
	return -1;
}
