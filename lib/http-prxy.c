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
#include <hio-sck.h>
#include <hio-fmt.h>
#include <hio-chr.h>
#include <hio-dns.h>

#include <unistd.h> /* TODO: move file operations to sys-file.XXX */
#include <fcntl.h>
#include <sys/stat.h>
#include <stdlib.h> /* setenv, clearenv */

#if defined(HAVE_CRT_EXTERNS_H)
#	include <crt_externs.h> /* _NSGetEnviron */
#endif

#define PRXY_ALLOW_UNLIMITED_REQ_CONTENT_LENGTH


#define PRXY_PENDING_IO_THRESHOLD 5

#define PRXY_OVER_READ_FROM_CLIENT (1 << 0)
#define PRXY_OVER_READ_FROM_PEER   (1 << 1)
#define PRXY_OVER_WRITE_TO_CLIENT  (1 << 2)
#define PRXY_OVER_WRITE_TO_PEER    (1 << 3)
#define PRXY_OVER_ALL (PRXY_OVER_READ_FROM_CLIENT | PRXY_OVER_READ_FROM_PEER | PRXY_OVER_WRITE_TO_CLIENT | PRXY_OVER_WRITE_TO_PEER)

struct prxy_t
{
	HIO_SVC_HTTS_TASK_HEADER;

	hio_svc_htts_task_on_kill_t on_kill; /* user-provided on_kill callback */

	int options;
	hio_oow_t peer_pending_writes;
	hio_dev_sck_t* peer;
	hio_htrd_t* peer_htrd;

	unsigned int over: 4; /* must be large enough to accomodate PRXY_OVER_ALL */
	unsigned int client_htrd_recbs_changed: 1;

	hio_dev_sck_on_read_t client_org_on_read;
	hio_dev_sck_on_write_t client_org_on_write;
	hio_dev_sck_on_disconnect_t client_org_on_disconnect;
	hio_htrd_recbs_t client_htrd_org_recbs;
};
typedef struct prxy_t prxy_t;

struct prxy_peer_xtn_t
{
	prxy_t* prxy; /* back pointer to the prxy object */
};
typedef struct prxy_peer_xtn_t prxy_peer_xtn_t;

static void unbind_task_from_client (prxy_t* prxy, int rcdown);
static void unbind_task_from_peer (prxy_t* prxy, int rcdown);

static void prxy_halt_participating_devices (prxy_t* prxy)
{
	HIO_DEBUG5 (prxy->htts->hio, "HTTS(%p) - prxy(t=%p,c=%p(%d),p=%p) Halting participating devices\n", prxy->htts, prxy, prxy->task_csck, (prxy->task_csck? prxy->task_csck->hnd: -1), prxy->peer);

	if (prxy->task_csck) hio_dev_sck_halt (prxy->task_csck);

	/* check for peer as it may not have been started */
	if (prxy->peer) hio_dev_sck_halt (prxy->peer);
}

static int prxy_write_to_peer (prxy_t* prxy, const void* data, hio_iolen_t dlen)
{
	if (prxy->peer)
	{
		prxy->peer_pending_writes++;
		if (hio_dev_sck_write(prxy->peer, data, dlen, HIO_NULL, HIO_NULL) <= -1)
		{
			prxy->peer_pending_writes--;
			return -1;
		}

		if (prxy->peer_pending_writes > PRXY_PENDING_IO_THRESHOLD)
		{
			/* suspend input watching */
			if (prxy->task_csck && hio_dev_sck_read(prxy->task_csck, 0) <= -1) return -1;
		}
	}
	return 0;
}

static HIO_INLINE void prxy_mark_over (prxy_t* prxy, int over_bits)
{
	hio_svc_htts_t* htts = prxy->htts;
	hio_t* hio = htts->hio;
	unsigned int old_over;

	old_over = prxy->over;
	prxy->over |= over_bits;

    HIO_DEBUG8 (hio, "HTTS(%p) - prxy(t=%p,c=%p[%d],p=%p) - old_over=%x | new-bits=%x => over=%x\n", prxy->htts, prxy, prxy->task_client, (prxy->task_csck? prxy->task_csck->hnd: -1), prxy->peer, (int)old_over, (int)over_bits, (int)prxy->over);

	if (!(old_over & PRXY_OVER_READ_FROM_CLIENT) && (prxy->over & PRXY_OVER_READ_FROM_CLIENT))
	{
		if (prxy->task_csck && hio_dev_sck_read(prxy->task_csck, 0) <= -1)
		{
			HIO_DEBUG5 (hio, "HTTS(%p) - prxy(t=%p,c=%p[%d],p=%p) - halting client for failure to disable input watching\n", prxy->htts, prxy, prxy->task_client, (prxy->task_csck? prxy->task_csck->hnd: -1), prxy->peer);
			hio_dev_sck_halt (prxy->task_csck);
		}
	}

	if (!(old_over & PRXY_OVER_READ_FROM_PEER) && (prxy->over & PRXY_OVER_READ_FROM_PEER))
	{
		if (prxy->peer && hio_dev_sck_read(prxy->peer, 0) <= -1)
		{
			HIO_DEBUG5 (hio, "HTTS(%p) - prxy(t=%p,c=%p[%d],p=%p) - halting peer for failure to disable input watching\n", prxy->htts, prxy, prxy->task_client, (prxy->task_csck? prxy->task_csck->hnd: -1), prxy->peer);
			hio_dev_sck_halt (prxy->peer);
		}
	}

	if (old_over != PRXY_OVER_ALL && prxy->over == PRXY_OVER_ALL)
	{
		/* ready to stop */
		if (prxy->peer)
		{
			HIO_DEBUG5 (hio, "HTTS(%p) - prxy(t=%p,c=%p[%d],p=%p) - halting unneeded peer\n", prxy->htts, prxy, prxy->task_client, (prxy->task_csck? prxy->task_csck->hnd: -1), prxy->peer);
			hio_dev_sck_halt (prxy->peer);
		}

		if (prxy->task_csck)
		{
			HIO_ASSERT (hio, prxy->task_client != HIO_NULL);

			if (prxy->task_keep_client_alive)
			{
				HIO_DEBUG5 (hio, "HTTS(%p) - prxy(t=%p,c=%p[%d],p=%p) - keeping client alive\n", prxy->htts, prxy, prxy->task_client, (prxy->task_csck? prxy->task_csck->hnd: -1), prxy->peer);
				HIO_ASSERT (prxy->htts->hio, prxy->task_client->task == (hio_svc_htts_task_t*)prxy);
				unbind_task_from_client (prxy, 1);
				/* prxy must not be accessed from here down as it could have been destroyed */
			}
			else
			{
				HIO_DEBUG5 (hio, "HTTS(%p) - prxy(t=%p,c=%p[%d],p=%p) - halting client\n", prxy->htts, prxy, prxy->task_client, (prxy->task_csck? prxy->task_csck->hnd: -1), prxy->peer);
				hio_dev_sck_shutdown (prxy->task_csck, HIO_DEV_SCK_SHUTDOWN_WRITE);
				hio_dev_sck_halt (prxy->task_csck);
			}
		}
	}
}

static void prxy_on_kill (hio_svc_htts_task_t* task)
{
	prxy_t* prxy = (prxy_t*)task;
	hio_t* hio = prxy->htts->hio;

	HIO_DEBUG5 (hio, "HTTS(%p) - prxy(t=%p,c=%p[%d],p=%p) - killing the task\n", prxy->htts, prxy, prxy->task_client, (prxy->task_csck? prxy->task_csck->hnd: -1), prxy->peer);

	if (prxy->on_kill) prxy->on_kill (task);

	/* [NOTE]
	 * 1. if hio_svc_htts_task_kill() is called, prxy->peer, prxy->peer_htrd, prxy->task_csck,
	 *    prxy->task_client may not not null.
	 * 2. this callback function doesn't decrement the reference count on prxy because
	 *    it is the task destruction callback. (passing 0 to unbind_task_from_peer/client)
	 */

	unbind_task_from_peer (prxy, 0);

	if (prxy->task_csck)
	{
		HIO_ASSERT (hio, prxy->task_client != HIO_NULL);
		unbind_task_from_client (prxy, 0);
	}

	if (prxy->task_next) HIO_SVC_HTTS_TASKL_UNLINK_TASK (prxy); /* detach from the htts service only if it's attached */
	HIO_DEBUG5 (hio, "HTTS(%p) - prxy(t=%p,c=%p[%d],p=%p) - killed the task\n", prxy->htts, prxy, prxy->task_client, (prxy->task_csck? prxy->task_csck->hnd: -1), prxy->peer);
}

static void prxy_peer_on_connect (hio_dev_sck_t* sck)
{

}

static void prxy_peer_on_disconnect (hio_dev_sck_t* sck)
{
	hio_t* hio = sck->hio;
	prxy_peer_xtn_t* pxtn = hio_dev_sck_getxtn(sck);
	prxy_t* prxy = pxtn->prxy;

	if (!prxy) return; /* prxy task already gone */

	HIO_DEBUG3 (hio, "HTTS(%p) - peer %p(hnd=%d) disconnectd\n", prxy->htts, sck, (int)sck->hnd);

	/* reset prxy->peer before calling unbind_task_from_peer() because this is the peer close callback */
	prxy->peer = HIO_NULL;
	unbind_task_from_peer (prxy, 1);

	/*
			if (!(prxy->over & PRXY_OVER_READ_FROM_PEER))
			{
				if (hio_svc_htts_task_endbody(prxy) <= -1)
					prxy_halt_participating_devices (prxy);
				else
					prxy_mark_over (prxy, PRXY_OVER_READ_FROM_PEER);
			}
	*/
}

static int prxy_peer_on_read (hio_dev_sck_t* sck, const void* data, hio_iolen_t dlen, const hio_skad_t* srcaddr)
{
	hio_t* hio = sck->hio;
	prxy_peer_xtn_t* peer = hio_dev_sck_getxtn(sck);
	prxy_t* prxy = peer->prxy;

	HIO_ASSERT (hio, prxy != HIO_NULL);

	if (dlen <= -1)
	{
		HIO_DEBUG3 (hio, "HTTS(%p) - read error from peer %p(hnd=%d)\n", prxy->htts, sck, (unsigned int)sck->hnd);
		goto oops;
	}

	if (dlen == 0)
	{
		HIO_DEBUG3 (hio, "HTTS(%p) - EOF from peer %p(hnd=%d)\n", prxy->htts, sck, (int)sck->hnd);

		if (!(prxy->over & PRXY_OVER_READ_FROM_PEER))
		{
			int n;
			/* the prxy script could be misbehaving.
			 * it still has to read more but EOF is read.
			 * otherwise peer_htrd_poke() should have been called */
			n = hio_svc_htts_task_endbody((hio_svc_htts_task_t*)prxy);
			prxy_mark_over (prxy, PRXY_OVER_READ_FROM_PEER);
			if (n <= -1) goto oops;
		}
	}
	else
	{
		hio_oow_t rem;

		HIO_ASSERT (hio, !(prxy->over & PRXY_OVER_READ_FROM_PEER));

		if (hio_htrd_feed(prxy->peer_htrd, data, dlen, &rem) <= -1)
		{
			HIO_DEBUG3 (hio, "HTTS(%p) - unable to feed peer htrd - peer %p(hnd=%d)\n", prxy->htts, sck, (int)sck->hnd);

			if (!prxy->task_res_started && !(prxy->over & PRXY_OVER_WRITE_TO_CLIENT))
			{
				hio_svc_htts_task_sendfinalres ((hio_svc_htts_task_t*)prxy, HIO_HTTP_STATUS_BAD_GATEWAY, HIO_NULL, HIO_NULL, 1); /* don't care about error because it jumps to oops below anyway */
			}

			goto oops;
		}

		if (rem > 0)
		{
			/* If the script specifies Content-Length and produces longer data, it will come here */
		}
	}

	return 0;

oops:
	prxy_halt_participating_devices (prxy);
	return 0;
}

static int prxy_peer_on_write (hio_dev_sck_t* sck, hio_iolen_t wrlen, void* wrctx, const hio_skad_t* dstaddr)
{
	hio_t* hio = sck->hio;
	prxy_peer_xtn_t* peer = hio_dev_sck_getxtn(sck);
	prxy_t* prxy = peer->prxy;

	if (!prxy) return 0; /* there is nothing i can do. the prxy is being cleared or has been cleared already. */

	HIO_ASSERT (hio, prxy->peer == sck);

	if (wrlen <= -1)
	{
		HIO_DEBUG3 (hio, "HTTS(%p) - unable to write to peer %p(hnd=%d)\n", prxy->htts, sck, (int)sck->hnd);
		goto oops;
	}
	else if (wrlen == 0)
	{
		/* indicated EOF */
		/* do nothing here as i didn't increment peer_pending_writes when making the write request */

		prxy->peer_pending_writes--;
		HIO_ASSERT (hio, prxy->peer_pending_writes == 0);
		HIO_DEBUG3 (hio, "HTTS(%p) - indicated EOF to peer %p(hnd=%d)\n", prxy->htts, sck, (int)sck->hnd);
		/* indicated EOF to the peer side. i need no more data from the client side.
		 * i don't need to enable input watching in the client side either */
		prxy_mark_over (prxy, PRXY_OVER_WRITE_TO_PEER);
	}
	else
	{
		HIO_ASSERT (hio, prxy->peer_pending_writes > 0);

		prxy->peer_pending_writes--;
		if (prxy->peer_pending_writes == PRXY_PENDING_IO_THRESHOLD)
		{
			if (!(prxy->over & PRXY_OVER_READ_FROM_CLIENT) &&
			    hio_dev_sck_read(prxy->task_csck, 1) <= -1) goto oops;
		}

		if ((prxy->over & PRXY_OVER_READ_FROM_CLIENT) && prxy->peer_pending_writes <= 0)
		{
			prxy_mark_over (prxy, PRXY_OVER_WRITE_TO_PEER);
		}
	}

	return 0;

oops:
	prxy_halt_participating_devices (prxy);
	return 0;
}


static int peer_capture_response_header (hio_htre_t* req, const hio_bch_t* key, const hio_htre_hdrval_t* val, void* ctx)
{
	return hio_svc_htts_task_addreshdrs((hio_svc_htts_task_t*)(prxy_t*)ctx, key, val);
}

static int peer_htrd_peek (hio_htrd_t* htrd, hio_htre_t* req)
{
	prxy_peer_xtn_t* peer = hio_htrd_getxtn(htrd);
	prxy_t* prxy = peer->prxy;
	hio_svc_htts_cli_t* cli = prxy->task_client;

	if (HIO_LIKELY(cli))
	{
		int status_code = HIO_HTTP_STATUS_OK;
		const hio_bch_t* status_desc = HIO_NULL;
		int chunked;

		if (req->attr.status) hio_parse_http_status_header_value(req->attr.status, &status_code, &status_desc);

		chunked = prxy->task_keep_client_alive && !req->attr.content_length;

		if (hio_svc_htts_task_startreshdr((hio_svc_htts_task_t*)prxy, status_code, status_desc, chunked) <= -1 ||
			hio_htre_walkheaders(req, peer_capture_response_header, prxy) <= -1 ||
			hio_svc_htts_task_endreshdr((hio_svc_htts_task_t*)prxy) <= -1) return -1;
	}

	return 0;
}

static int peer_htrd_poke (hio_htrd_t* htrd, hio_htre_t* req)
{
	/* peer response got completed */
	prxy_peer_xtn_t* peer = hio_htrd_getxtn(htrd);
	prxy_t* prxy = peer->prxy;
	int n;

	n = hio_svc_htts_task_endbody((hio_svc_htts_task_t*)prxy);
	prxy_mark_over (prxy, PRXY_OVER_READ_FROM_PEER);
	return n;
}

static int peer_htrd_push_content (hio_htrd_t* htrd, hio_htre_t* req, const hio_bch_t* data, hio_oow_t dlen)
{
	prxy_peer_xtn_t* peer = hio_htrd_getxtn(htrd);
	prxy_t* prxy = peer->prxy;
	int n;

	HIO_ASSERT (prxy->htts->hio, htrd == prxy->peer_htrd);

	n = hio_svc_htts_task_addresbody((hio_svc_htts_task_t*)prxy, data, dlen);
	if (prxy->task_res_pending_writes > PRXY_PENDING_IO_THRESHOLD)
	{
		if (hio_dev_sck_read(prxy->peer, 0) <= -1) n = -1;
	}

	return n;
}

static hio_htrd_recbs_t peer_htrd_recbs =
{
	peer_htrd_peek,
	peer_htrd_poke,
	peer_htrd_push_content
};

static int prxy_client_htrd_poke (hio_htrd_t* htrd, hio_htre_t* req)
{
	/* client request got completed */
	hio_svc_htts_cli_htrd_xtn_t* htrdxtn = (hio_svc_htts_cli_htrd_xtn_t*)hio_htrd_getxtn(htrd);
	hio_dev_sck_t* sck = htrdxtn->sck;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	prxy_t* prxy = (prxy_t*)cli->task;

	/* indicate EOF to the client peer */
	if (prxy_write_to_peer(prxy, HIO_NULL, 0) <= -1) return -1;

	prxy_mark_over (prxy, PRXY_OVER_READ_FROM_CLIENT);
	return 0;
}

static int prxy_client_htrd_push_content (hio_htrd_t* htrd, hio_htre_t* req, const hio_bch_t* data, hio_oow_t dlen)
{
	hio_svc_htts_cli_htrd_xtn_t* htrdxtn = (hio_svc_htts_cli_htrd_xtn_t*)hio_htrd_getxtn(htrd);
	hio_dev_sck_t* sck = htrdxtn->sck;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	prxy_t* prxy = (prxy_t*)cli->task;

	HIO_ASSERT (sck->hio, cli->sck == sck);
	return prxy_write_to_peer(prxy, data, dlen);
}

static hio_htrd_recbs_t prxy_client_htrd_recbs =
{
	HIO_NULL, /* this shall be set to an actual peer handler before hio_htrd_setrecbs() */
	prxy_client_htrd_poke,
	prxy_client_htrd_push_content
};

static void prxy_client_on_disconnect (hio_dev_sck_t* sck)
{
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	hio_svc_htts_t* htts = cli->htts;
	prxy_t* prxy = (prxy_t*)cli->task;
	hio_t* hio = sck->hio;

	HIO_ASSERT (hio, sck == prxy->task_csck);
	HIO_DEBUG4 (hio, "HTTS(%p) - prxy(t=%p,c=%p,csck=%p) - client socket disconnect notified\n", htts, prxy, cli, sck);

	if (prxy)
	{
		HIO_SVC_HTTS_TASK_RCUP ((hio_svc_htts_task_t*)prxy);

		/* detach the task from the client and the client socket */
		unbind_task_from_client (prxy, 1);

		/* call the parent handler*/
		/*if (fprxy->client_org_on_disconnect) fprxy->client_org_on_disconnect (sck);*/
		if (sck->on_disconnect) sck->on_disconnect (sck); /* restored to the orginal parent handler in unbind_task_from_client() */

		HIO_SVC_HTTS_TASK_RCDOWN ((hio_svc_htts_task_t*)prxy);
	}

	HIO_DEBUG4 (hio, "HTTS(%p) - prxy(t=%p,c=%p,csck=%p) - client socket disconnect handled\n", htts, prxy, cli, sck);
	/* Note: after this callback, the actual device pointed to by 'sck' will be freed in the main loop. */
}

static int prxy_client_on_read (hio_dev_sck_t* sck, const void* buf, hio_iolen_t len, const hio_skad_t* srcaddr)
{
	hio_t* hio = sck->hio;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	prxy_t* prxy = (prxy_t*)cli->task;
	int n;

	HIO_ASSERT (hio, sck == cli->sck);

	n = prxy->client_org_on_read? prxy->client_org_on_read(sck, buf, len, srcaddr): 0;

	if (len <= -1)
	{
		/* read error */
		HIO_DEBUG3 (cli->htts->hio, "HTTS(%p) - read error on client %p(%d)\n", prxy->htts, sck, (int)sck->hnd);
		goto oops;
	}

	if (len == 0)
	{
		/* EOF on the client side. arrange to close */
		HIO_DEBUG3 (hio, "HTTS(%p) - EOF from client %p(hnd=%d)\n", prxy->htts, sck, (int)sck->hnd);

		if (!(prxy->over & PRXY_OVER_READ_FROM_CLIENT)) /* if this is true, EOF is received without prxy_client_htrd_poke() */
		{
			int x;
			x = prxy_write_to_peer(prxy, HIO_NULL, 0);
			prxy_mark_over (prxy, PRXY_OVER_READ_FROM_CLIENT);
			if (x <= -1) goto oops;
		}
	}

	if (n <= -1) goto oops;
	return 0;

oops:
	prxy_halt_participating_devices (prxy);
	return 0;
}

static int prxy_client_on_write (hio_dev_sck_t* sck, hio_iolen_t wrlen, void* wrctx, const hio_skad_t* dstaddr)
{
	hio_t* hio = sck->hio;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	prxy_t* prxy = (prxy_t*)cli->task;
	int n;

	n = prxy->client_org_on_write? prxy->client_org_on_write(sck, wrlen, wrctx, dstaddr): 0;

	if (wrlen == 0)
	{
		/* if the connect is keep-alive, this part may not be called */
		HIO_DEBUG3 (hio, "HTTS(%p) - indicated EOF to client %p(%d)\n", prxy->htts, sck, (int)sck->hnd);
		/* since EOF has been indicated to the client, it must not write to the client any further.
		 * this also means that i don't need any data from the peer side either.
		 * i don't need to enable input watching on the peer side */
		prxy_mark_over (prxy, PRXY_OVER_WRITE_TO_CLIENT);
	}
	else if (wrlen > 0)
	{
		if (prxy->peer && prxy->task_res_pending_writes == PRXY_PENDING_IO_THRESHOLD)
		{
			/* enable input watching */
			if (!(prxy->over & PRXY_OVER_READ_FROM_PEER) &&
			    hio_dev_sck_read(prxy->peer, 1) <= -1) n = -1;
		}

		if ((prxy->over & PRXY_OVER_READ_FROM_PEER) && prxy->task_res_pending_writes <= 0)
		{
			prxy_mark_over (prxy, PRXY_OVER_WRITE_TO_CLIENT);
		}
	}

	if (n <= -1 || wrlen <= -1) prxy_halt_participating_devices (prxy);
	return 0;
}

/* ----------------------------------------------------------------------- */

struct peer_fork_ctx_t
{
	hio_svc_htts_cli_t* cli;
	hio_htre_t* req;
	const hio_bch_t* docroot;
	const hio_bch_t* script;
	hio_bch_t* actual_script;
};
typedef struct peer_fork_ctx_t peer_fork_ctx_t;

static int peer_capture_request_header (hio_htre_t* req, const hio_bch_t* key, const hio_htre_hdrval_t* val, void* ctx)
{
	hio_becs_t* dbuf = (hio_becs_t*)ctx;

	if (hio_comp_bcstr(key, "Connection", 1) != 0 &&
	    hio_comp_bcstr(key, "Transfer-Encoding", 1) != 0 &&
	    hio_comp_bcstr(key, "Content-Length", 1) != 0 &&
	    hio_comp_bcstr(key, "Expect", 1) != 0)
	{
		hio_oow_t val_offset;
		hio_bch_t* ptr;

		hio_becs_clear (dbuf);
		if (hio_becs_cpy(dbuf, "HTTP_") == (hio_oow_t)-1 ||
		    hio_becs_cat(dbuf, key) == (hio_oow_t)-1 ||
		    hio_becs_ccat(dbuf, '\0') == (hio_oow_t)-1) return -1;

		for (ptr = HIO_BECS_PTR(dbuf); *ptr; ptr++)
		{
			*ptr = hio_to_bch_upper(*ptr);
			if (*ptr =='-') *ptr = '_';
		}

		val_offset = HIO_BECS_LEN(dbuf);
		if (hio_becs_cat(dbuf, val->ptr) == (hio_oow_t)-1) return -1;
		val = val->next;
		while (val)
		{
			if (hio_becs_cat(dbuf, ",") == (hio_oow_t)-1 ||
			    hio_becs_cat(dbuf, val->ptr) == (hio_oow_t)-1) return -1;
			val = val->next;
		}

		setenv (HIO_BECS_PTR(dbuf), HIO_BECS_CPTR(dbuf, val_offset), 1);
	}

	return 0;
}

static int prxy_peer_on_fork (hio_dev_sck_t* pro, void* fork_ctx)
{
	hio_t* hio = pro->hio; /* in this callback, the pro device is not fully up. however, the hio field is guaranteed to be available */
	peer_fork_ctx_t* fc = (peer_fork_ctx_t*)fork_ctx;
	hio_oow_t content_length;
	const hio_bch_t* qparam;
	const hio_bch_t* tmpstr;
	hio_bch_t* path, * lang;
	hio_bch_t tmp[256];
	hio_becs_t dbuf;

	qparam = hio_htre_getqparam(fc->req);
	/* the anchor/fragment is never part of the server-side URL.
	 * the client must discard that part before sending to the server.
	 * hio_htre_getqanchor() is just disregarded here. */

	tmpstr = getenv("PATH");
	if (!tmpstr) tmpstr = "";
	path = hio_dupbcstr(hio, tmpstr, HIO_NULL);

	tmpstr = getenv("LANG");
	if (!tmpstr) tmpstr = "";
	lang = hio_dupbcstr(hio, tmpstr, HIO_NULL);

#if defined(HAVE_CLEARENV)
	clearenv ();
#elif defined(HAVE_CRT_EXTERNS_H)
	{
		char** environ = *_NSGetEnviron();
		if (environ) environ[0] = '\0';
	}

#else
	{
		extern char** environ;
		/* environ = NULL; this crashed this program on NetBSD */
		if (environ) environ[0] = '\0';
	}
#endif
	if (path)
	{
		setenv ("PATH", path, 1);
		hio_freemem (hio, path);
	}

	if (lang)
	{
		setenv ("LANG", lang, 1);
		hio_freemem (hio, lang);
	}

	setenv ("GATEWAY_INTERFACE", "PRXY/1.1", 1);

	hio_fmttobcstr (hio, tmp, HIO_COUNTOF(tmp), "HTTP/%d.%d", (int)hio_htre_getmajorversion(fc->req), (int)hio_htre_getminorversion(fc->req));
	setenv ("SERVER_PROTOCOL", tmp, 1);

	setenv ("DOCUMENT_ROOT", fc->docroot, 1);
	setenv ("SCRIPT_NAME", fc->script, 1);
	setenv ("SCRIPT_FILENAME", fc->actual_script, 1);
	/* TODO: PATH_INFO */

	setenv ("REQUEST_METHOD", hio_htre_getqmethodname(fc->req), 1);
	setenv ("REQUEST_URI", hio_htre_getqpath(fc->req), 1);

	if (qparam) setenv ("QUERY_STRING", qparam, 1);

	if (hio_htre_getreqcontentlen(fc->req, &content_length) == 0)
	{
		hio_fmt_uintmax_to_bcstr(tmp, HIO_COUNTOF(tmp), content_length, 10, 0, '\0', HIO_NULL);
		setenv ("CONTENT_LENGTH", tmp, 1);
	}
	else
	{
		/* content length unknown, neither is it 0 - this is not standard */
		setenv ("CONTENT_LENGTH", "-1", 1);
	}
	setenv ("SERVER_SOFTWARE", fc->cli->htts->server_name, 1);

	hio_skadtobcstr (hio, &fc->cli->sck->localaddr, tmp, HIO_COUNTOF(tmp), HIO_SKAD_TO_BCSTR_ADDR);
	setenv ("SERVER_ADDR", tmp, 1);

	gethostname (tmp, HIO_COUNTOF(tmp)); /* if this fails, i assume tmp contains the ip address set by hio_skadtobcstr() above */
	setenv ("SERVER_NAME", tmp, 1);

	hio_skadtobcstr (hio, &fc->cli->sck->localaddr, tmp, HIO_COUNTOF(tmp), HIO_SKAD_TO_BCSTR_PORT);
	setenv ("SERVER_PORT", tmp, 1);

	hio_skadtobcstr (hio, &fc->cli->sck->remoteaddr, tmp, HIO_COUNTOF(tmp), HIO_SKAD_TO_BCSTR_ADDR);
	setenv ("REMOTE_ADDR", tmp, 1);

	hio_skadtobcstr (hio, &fc->cli->sck->remoteaddr, tmp, HIO_COUNTOF(tmp), HIO_SKAD_TO_BCSTR_PORT);
	setenv ("REMOTE_PORT", tmp, 1);

	if (hio_becs_init(&dbuf, hio, 256) >= 0)
	{
		hio_htre_walkheaders (fc->req,  peer_capture_request_header, &dbuf);
		/* [NOTE] trailers are not available when this prxy resource is started. let's not call hio_htre_walktrailers() */
		hio_becs_fini (&dbuf);
	}

	return 0;
}

/* ----------------------------------------------------------------------- */

static void bind_task_to_client (prxy_t* prxy, hio_dev_sck_t* csck)
{
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(csck);

	HIO_ASSERT (prxy->htts->hio, cli->sck == csck);
	HIO_ASSERT (prxy->htts->hio, cli->task == HIO_NULL);

	/* prxy->task_client and prxy->task_csck are set in hio_svc_htts_task_make() */

	/* remember the client socket's io event handlers */
	prxy->client_org_on_read = csck->on_read;
	prxy->client_org_on_write = csck->on_write;
	prxy->client_org_on_disconnect = csck->on_disconnect;

	/* set new io events handlers on the client socket */
	csck->on_read = prxy_client_on_read;
	csck->on_write = prxy_client_on_write;
	csck->on_disconnect = prxy_client_on_disconnect;

	cli->task = (hio_svc_htts_task_t*)prxy;
	HIO_SVC_HTTS_TASK_RCUP (prxy);
}

static void unbind_task_from_client (prxy_t* prxy, int rcdown)
{
	hio_dev_sck_t* csck = prxy->task_csck;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(csck);

	if (cli->task) /* only if it's bound */
	{
		HIO_ASSERT (prxy->htts->hio, prxy->task_client != HIO_NULL);
		HIO_ASSERT (prxy->htts->hio, prxy->task_csck != HIO_NULL);
		HIO_ASSERT (prxy->htts->hio, prxy->task_client->task == (hio_svc_htts_task_t*)prxy);
		HIO_ASSERT (prxy->htts->hio, prxy->task_client->htrd != HIO_NULL);

		if (prxy->client_htrd_recbs_changed)
		{
			hio_htrd_setrecbs (prxy->task_client->htrd, &prxy->client_htrd_org_recbs);
			prxy->client_htrd_recbs_changed = 0;
		}

		if (prxy->client_org_on_read)
		{
			csck->on_read = prxy->client_org_on_read;
			prxy->client_org_on_read = HIO_NULL;
		}

		if (prxy->client_org_on_write)
		{
			csck->on_write = prxy->client_org_on_write;
			prxy->client_org_on_write = HIO_NULL;
		}

		if (prxy->client_org_on_disconnect)
		{
			csck->on_disconnect = prxy->client_org_on_disconnect;
			prxy->client_org_on_disconnect = HIO_NULL;
		}

		/* there is some ordering issue in using HIO_SVC_HTTS_TASK_UNREF()
		* because it can destroy the prxy itself. so reset prxy->task_client->task
		* to null and call RCDOWN() later */
		prxy->task_client->task = HIO_NULL;

		/* these two lines are also done in csck_on_disconnect() in http-svr.c because the socket is destroyed.
		* the same lines here are because the task is unbound while the socket is still alive */
		prxy->task_client = HIO_NULL;
		prxy->task_csck = HIO_NULL;

		/* enable input watching on the socket being unbound */
		if (prxy->task_keep_client_alive && hio_dev_sck_read(csck, 1) <= -1)
		{
			HIO_DEBUG2 (prxy->htts->hio, "HTTS(%p) - halting client(%p) for failure to enable input watching\n", prxy->htts, csck);
			hio_dev_sck_halt (csck);
		}

		if (rcdown) HIO_SVC_HTTS_TASK_RCDOWN ((hio_svc_htts_task_t*)prxy);
	}
}

/* ----------------------------------------------------------------------- */

static void on_peer_ipaddr_resolved (hio_svc_dnc_t* dnc, hio_dns_msg_t* reqmsg, hio_errnum_t status, const void* data, hio_oow_t len)
{
	// initiate connect.
	// enable read if necessary...
}

/* ----------------------------------------------------------------------- */

static int bind_task_to_peer (prxy_t* prxy, hio_dev_sck_t* csck, hio_htre_t* req, const hio_skad_t* skad)
{
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(csck);
	hio_svc_htts_t* htts = prxy->htts;
	hio_t* hio = htts->hio;
	hio_dev_sck_make_t m;
	hio_dev_sck_t* sck = HIO_NULL;
	hio_htrd_t* htrd = HIO_NULL;
	prxy_peer_xtn_t* pxtn;
	hio_skad_t resolved_skad;

	if (!skad)
	{
		const hio_bch_t* qpath = hio_htre_getqpath(req);

		/* TODO: https not supported yet */
		if (hio_comp_bcstr_limited(qpath, "http://", 7, 0) == 0)
		{
			const hio_bch_t* host = qpath + 7;
			if (hio_bcstrtoskad(hio, host, &resolved_skad) <= -1)
			{
				/*
				if (hio_svc_dnc_resolve(htts->dnc, qpath + , qtype, 0, on_peer_ipaddr_resolved, 0) <= -1)
				{
				}*/
			}
		}
	}

	HIO_MEMSET (&m, 0, HIO_SIZEOF(m));
	if (hio_get_stream_sck_type_from_skad(skad, &m.type) <= -1)
	{
		hio_seterrnum (hio, HIO_EINVAL);
		goto oops;
	}

	m.on_write = prxy_peer_on_write;
	m.on_read = prxy_peer_on_read;
	m.on_connect = prxy_peer_on_connect;
	m.on_disconnect = prxy_peer_on_disconnect;

	sck = hio_dev_sck_make(hio, HIO_SIZEOF(*pxtn), &m);
	htrd = hio_htrd_open(hio, HIO_SIZEOF(*pxtn));
	if (HIO_UNLIKELY(!sck || !htrd)) goto oops;

	hio_htrd_setoption (htrd, HIO_HTRD_RESPONSE);
	hio_htrd_setrecbs (htrd, &peer_htrd_recbs);

	prxy->peer = sck;
	prxy->peer_htrd = htrd;

	pxtn = hio_dev_sck_getxtn(prxy->peer);
	pxtn->prxy = prxy;
	HIO_SVC_HTTS_TASK_RCUP (prxy);

	pxtn = hio_htrd_getxtn(prxy->peer_htrd);
	pxtn->prxy = prxy;
	HIO_SVC_HTTS_TASK_RCUP (prxy);

	return 0;

oops:
	if (htrd) hio_htrd_close (htrd);
	if (sck) hio_dev_sck_kill (sck);
	return -1;
}

static void unbind_task_from_peer (prxy_t* prxy, int rcdown)
{
	int n = 0;

	if (prxy->peer_htrd)
	{
		hio_htrd_close (prxy->peer_htrd);
		prxy->peer_htrd = HIO_NULL;
		n++;
	}

	if (prxy->peer)
	{
		prxy_peer_xtn_t* peer_xtn;
		peer_xtn = hio_dev_sck_getxtn(prxy->peer);
		peer_xtn->prxy = HIO_NULL;

		hio_dev_sck_kill (prxy->peer);
		prxy->peer = HIO_NULL;
		n++;
	}

	if (rcdown)
	{
		while (n > 0)
		{
			n--;
			HIO_SVC_HTTS_TASK_RCDOWN((hio_svc_htts_task_t*)prxy);
		}
	}
}

/* ----------------------------------------------------------------------- */

static int setup_for_content_length(prxy_t* prxy, hio_htre_t* req)
{
	int have_content;

#if defined(PRXY_ALLOW_UNLIMITED_REQ_CONTENT_LENGTH)
	have_content = prxy->task_req_conlen > 0 || prxy->task_req_conlen_unlimited;
#else
	have_content = prxy->task_req_conlen > 0;
#endif

	if (have_content)
	{
		/* change the callbacks to subscribe to contents to be uploaded */
		prxy->client_htrd_org_recbs = *hio_htrd_getrecbs(prxy->task_client->htrd);
		prxy_client_htrd_recbs.peek = prxy->client_htrd_org_recbs.peek;
		hio_htrd_setrecbs (prxy->task_client->htrd, &prxy_client_htrd_recbs);
		prxy->client_htrd_recbs_changed = 1;
	}
	else
	{
		/* no content to be uploaded from the client */
		/* indicate EOF to the peer and disable input wathching from the client */
		if (prxy_write_to_peer(prxy, HIO_NULL, 0) <= -1) return -1;
		prxy_mark_over (prxy, PRXY_OVER_READ_FROM_CLIENT | PRXY_OVER_WRITE_TO_PEER);
	}

	return 0;
}

/* ----------------------------------------------------------------------- */

int hio_svc_htts_doprxy (hio_svc_htts_t* htts, hio_dev_sck_t* csck, hio_htre_t* req, const hio_skad_t* tgt_addr, int options, hio_svc_htts_task_on_kill_t on_kill)
{
	hio_t* hio = htts->hio;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(csck);
	prxy_t* prxy = HIO_NULL;
	int n, status_code = HIO_HTTP_STATUS_INTERNAL_SERVER_ERROR;
	int bound_to_client = 0, bound_to_peer = 0;

	/* ensure that you call this function before any contents is received */
	HIO_ASSERT (hio, hio_htre_getcontentlen(req) == 0);
	HIO_ASSERT (hio, cli->sck == csck);

	if (cli->task)
	{
		hio_seterrbfmt (hio, HIO_EPERM, "duplicate task request prohibited");
		goto oops;
	}

	prxy = (prxy_t*)hio_svc_htts_task_make(htts, HIO_SIZEOF(*prxy), prxy_on_kill, req, csck);
	if (HIO_UNLIKELY(!prxy)) goto oops;
	HIO_SVC_HTTS_TASK_RCUP ((hio_svc_htts_task_t*)prxy);

	prxy->options = options;

	bind_task_to_client (prxy, csck);
	bound_to_client = 1;

	if ((n = bind_task_to_peer(prxy, csck, req, tgt_addr)) <= -1)
	{
		hio_svc_htts_task_sendfinalres((hio_svc_htts_task_t*)prxy, (n == 2? HIO_HTTP_STATUS_FORBIDDEN: HIO_HTTP_STATUS_INTERNAL_SERVER_ERROR), HIO_NULL, HIO_NULL, 1);
		goto oops; /* TODO: must not go to oops.  just destroy the prxy and finalize the request .. */
	}
	bound_to_peer = 1;

	if (hio_svc_htts_task_handleexpect100((hio_svc_htts_task_t*)prxy, 0) <= -1) goto oops;
	if (setup_for_content_length(prxy, req) <= -1) goto oops;

	/* TODO: store current input watching state and use it when destroying the prxy data */
	if (hio_dev_sck_read(csck, !(prxy->over & PRXY_OVER_READ_FROM_CLIENT)) <= -1) goto oops;

	HIO_SVC_HTTS_TASKL_APPEND_TASK (&htts->task, (hio_svc_htts_task_t*)prxy);
	HIO_SVC_HTTS_TASK_RCDOWN ((hio_svc_htts_task_t*)prxy);

	/* set the on_kill callback only if this function can return success.
	 * the on_kill callback won't be executed if this function returns failure. */
	prxy->on_kill = on_kill;
	return 0;

oops:
	HIO_DEBUG2 (hio, "HTTS(%p) - FAILURE in doprxy - socket(%p)\n", htts, csck);
	if (prxy)
	{
		hio_svc_htts_task_sendfinalres((hio_svc_htts_task_t*)prxy, status_code, HIO_NULL, HIO_NULL, 1);
		if (bound_to_peer) unbind_task_from_peer (prxy, 1);
		if (bound_to_client) unbind_task_from_client (prxy, 1);
		prxy_halt_participating_devices (prxy);
		HIO_SVC_HTTS_TASK_RCDOWN ((hio_svc_htts_task_t*)prxy);
	}
	return -1;
}
