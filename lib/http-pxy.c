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

#define PXY_ALLOW_UNLIMITED_REQ_CONTENT_LENGTH


#define PXY_PEER_CONNECT_TMOUT (5)
#define PXY_PENDING_IO_THRESHOLD 5

#define PXY_OVER_READ_FROM_CLIENT (1 << 0)
#define PXY_OVER_READ_FROM_PEER   (1 << 1)
#define PXY_OVER_WRITE_TO_CLIENT  (1 << 2)
#define PXY_OVER_WRITE_TO_PEER    (1 << 3)
#define PXY_OVER_ALL (PXY_OVER_READ_FROM_CLIENT | PXY_OVER_READ_FROM_PEER | PXY_OVER_WRITE_TO_CLIENT | PXY_OVER_WRITE_TO_PEER)

struct pxy_t
{
	HIO_SVC_HTTS_TASK_HEADER;

	hio_svc_htts_task_on_kill_t on_kill; /* user-provided on_kill callback */

	int options;
	hio_oow_t peer_pending_writes;
	hio_dev_sck_t* peer;
	hio_htrd_t* peer_htrd;

	/* Everything bound for the peer before the connection completes is
	 * held here: the request head, built while the client's request is
	 * still around, plus any body bytes that arrive in the meantime.
	 * pxy_peer_on_connect() flushes it. */
	hio_becs_t* peer_buf;

	unsigned int over: 4; /* must be large enough to accomodate PXY_OVER_ALL */
	unsigned int peer_connected: 1;
	unsigned int peer_wr_ended: 1; /* the client side finished before we connected */

};
typedef struct pxy_t pxy_t;

struct pxy_peer_xtn_t
{
	pxy_t* pxy; /* back pointer to the pxy object */
};
typedef struct pxy_peer_xtn_t pxy_peer_xtn_t;

static void unbind_task_from_peer (pxy_t* pxy, int rcdown);

static void pxy_halt_participating_devices (pxy_t* pxy)
{
	hio_svc_htts_task_haltclient((hio_svc_htts_task_t*)pxy);
	if (pxy->peer) hio_dev_sck_halt(pxy->peer);
}

static int pxy_write_to_peer (pxy_t* pxy, const void* data, hio_iolen_t dlen)
{
	if (pxy->peer && !pxy->peer_connected)
	{
		/* the connection is still being established. hold the data back
		 * rather than writing into a socket that has no peer yet. */
		if (dlen <= 0)
		{
			pxy->peer_wr_ended = 1;
			return 0;
		}
		if (hio_becs_ncat(pxy->peer_buf, data, dlen) == (hio_oow_t)-1) return -1;
		return 0;
	}

	if (pxy->peer)
	{
		pxy->peer_pending_writes++;
		if (hio_dev_sck_write(pxy->peer, data, dlen, HIO_NULL, HIO_NULL) <= -1)
		{
			pxy->peer_pending_writes--;
			return -1;
		}

		if (pxy->peer_pending_writes > PXY_PENDING_IO_THRESHOLD)
		{
			/* suspend input watching */
			if (pxy->task_csck && hio_dev_sck_read(pxy->task_csck, 0) <= -1) return -1;
		}
	}
	return 0;
}

static void pxy_mark_over (pxy_t* pxy, int over_bits)
{
	unsigned int old_over;

	old_over = pxy->over;
	pxy->over |= over_bits;

	HIO_DEBUG4 (pxy->htts->hio, "HTTS(%p) - pxy(c=%p) updating mark - new-bits=%x => over=%x\n", pxy->htts, pxy->task_csck, (int)over_bits, (int)pxy->over);

	if (!(old_over & PXY_OVER_READ_FROM_CLIENT) && (pxy->over & PXY_OVER_READ_FROM_CLIENT))
		hio_svc_htts_task_stopreadingclient((hio_svc_htts_task_t*)pxy);

	if (old_over != PXY_OVER_ALL && pxy->over == PXY_OVER_ALL)
	{
		if (pxy->peer) hio_dev_sck_halt(pxy->peer);
		hio_svc_htts_task_finishclient((hio_svc_htts_task_t*)pxy);
	}
}

static void pxy_on_kill (hio_svc_htts_task_t* task)
{
	pxy_t* pxy = (pxy_t*)task;
	hio_t* hio = pxy->htts->hio;

	HIO_DEBUG5(hio, "HTTS(%p) - pxy(t=%p,c=%p[%d],p=%p) - killing the task\n", pxy->htts, pxy, pxy->task_client, (pxy->task_csck? pxy->task_csck->hnd: -1), pxy->peer);

	if (pxy->on_kill) pxy->on_kill(task);

	if (pxy->peer_buf)
	{
		hio_becs_close(pxy->peer_buf);
		pxy->peer_buf = HIO_NULL;
	}

	/* [NOTE]
	 * 1. if hio_svc_htts_task_kill() is called, pxy->peer, pxy->peer_htrd, pxy->task_csck,
	 *    pxy->task_client may not not null.
	 * 2. this callback function doesn't decrement the reference count on pxy because
	 *    it is the task destruction callback. (passing 0 to unbind_task_from_peer/client)
	 */

	unbind_task_from_peer (pxy, 0);

	if (pxy->task_csck)
	{
		HIO_ASSERT(hio, pxy->task_client != HIO_NULL);
		hio_svc_htts_task_unbindfromclient((hio_svc_htts_task_t*)pxy, 0);
	}

	if (pxy->task_next) HIO_SVC_HTTS_TASKL_UNLINK_TASK(pxy); /* detach from the htts service only if it's attached */
	HIO_DEBUG5(hio, "HTTS(%p) - pxy(t=%p,c=%p[%d],p=%p) - killed the task\n", pxy->htts, pxy, pxy->task_client, (pxy->task_csck? pxy->task_csck->hnd: -1), pxy->peer);
}

/* hand the buffered request head - and anything the client sent while we
 * were connecting - to the peer */
static int flush_to_peer (pxy_t* pxy)
{
	hio_oow_t len = HIO_BECS_LEN(pxy->peer_buf);

	if (len > 0)
	{
		/* [NOTE] write before clearing. hio_becs_clear() terminates the
		 * buffer at offset 0, which would blank the first byte being sent.
		 * hio_dev_sck_write() either sends synchronously or copies into
		 * the write queue, so the buffer is free once it returns. */
		if (pxy_write_to_peer(pxy, HIO_BECS_PTR(pxy->peer_buf), len) <= -1) return -1;
		hio_becs_clear(pxy->peer_buf);
	}

	if (pxy->peer_wr_ended)
	{
		pxy->peer_wr_ended = 0;
		if (pxy_write_to_peer(pxy, HIO_NULL, 0) <= -1) return -1;
	}

	return 0;
}

static void pxy_peer_on_connect (hio_dev_sck_t* sck)
{
	pxy_peer_xtn_t* pxtn = hio_dev_sck_getxtn(sck);
	pxy_t* pxy = pxtn->pxy;

	if (HIO_UNLIKELY(!pxy)) return;

	pxy->peer_connected = 1;
	if (flush_to_peer(pxy) <= -1)
	{
		HIO_DEBUG1(sck->hio, "HTTS(%p) - pxy unable to send the request to the peer\n", pxy->htts);
		pxy_halt_participating_devices(pxy);
	}
}

static void pxy_peer_on_disconnect (hio_dev_sck_t* sck)
{
	hio_t* hio = sck->hio;
	pxy_peer_xtn_t* pxtn = hio_dev_sck_getxtn(sck);
	pxy_t* pxy = pxtn->pxy;

	if (!pxy) return; /* pxy task already gone */

	HIO_DEBUG3(hio, "HTTS(%p) - peer %p(hnd=%d) disconnectd\n", pxy->htts, sck, (int)sck->hnd);

	/* reset pxy->peer before calling unbind_task_from_peer() because this is the peer close callback */
	pxy->peer = HIO_NULL;
	unbind_task_from_peer (pxy, 1);

	/*
			if (!(pxy->over & PXY_OVER_READ_FROM_PEER))
			{
				if (hio_svc_htts_task_endbody(pxy) <= -1)
					pxy_halt_participating_devices(pxy);
				else
					pxy_mark_over(pxy, PXY_OVER_READ_FROM_PEER);
			}
	*/
}

static int pxy_peer_on_read (hio_dev_sck_t* sck, const void* data, hio_iolen_t dlen, const hio_skad_t* srcaddr)
{
	hio_t* hio = sck->hio;
	pxy_peer_xtn_t* peer = hio_dev_sck_getxtn(sck);
	pxy_t* pxy = peer->pxy;

	HIO_ASSERT(hio, pxy != HIO_NULL);

	if (dlen <= -1)
	{
		HIO_DEBUG3(hio, "HTTS(%p) - read error from peer %p(hnd=%d)\n", pxy->htts, sck, (unsigned int)sck->hnd);
		goto oops;
	}

	if (dlen == 0)
	{
		HIO_DEBUG3(hio, "HTTS(%p) - EOF from peer %p(hnd=%d)\n", pxy->htts, sck, (int)sck->hnd);

		if (!(pxy->over & PXY_OVER_READ_FROM_PEER))
		{
			int n;
			/* the pxy script could be misbehaving.
			 * it still has to read more but EOF is read.
			 * otherwise peer_htrd_poke() should have been called */
			n = hio_svc_htts_task_endbody((hio_svc_htts_task_t*)pxy);
			pxy_mark_over(pxy, PXY_OVER_READ_FROM_PEER);
			if (n <= -1) goto oops;
		}
	}
	else
	{
		hio_oow_t rem;

		HIO_ASSERT(hio, !(pxy->over & PXY_OVER_READ_FROM_PEER));

		if (hio_htrd_feed(pxy->peer_htrd, data, dlen, &rem) <= -1)
		{
			HIO_DEBUG3(hio, "HTTS(%p) - unable to feed peer htrd - peer %p(hnd=%d)\n", pxy->htts, sck, (int)sck->hnd);

			if (!pxy->task_res_started && !(pxy->over & PXY_OVER_WRITE_TO_CLIENT))
			{
				hio_svc_htts_task_sendfinalres ((hio_svc_htts_task_t*)pxy, HIO_HTTP_STATUS_BAD_GATEWAY, HIO_NULL, HIO_NULL, 1); /* don't care about error because it jumps to oops below anyway */
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
	pxy_halt_participating_devices(pxy);
	return 0;
}

static int pxy_peer_on_write (hio_dev_sck_t* sck, hio_iolen_t wrlen, void* wrctx, const hio_skad_t* dstaddr)
{
	hio_t* hio = sck->hio;
	pxy_peer_xtn_t* peer = hio_dev_sck_getxtn(sck);
	pxy_t* pxy = peer->pxy;

	if (!pxy) return 0; /* there is nothing i can do. the pxy is being cleared or has been cleared already. */

	HIO_ASSERT(hio, pxy->peer == sck);

	if (wrlen <= -1)
	{
		HIO_DEBUG3(hio, "HTTS(%p) - unable to write to peer %p(hnd=%d)\n", pxy->htts, sck, (int)sck->hnd);
		goto oops;
	}
	else if (wrlen == 0)
	{
		/* indicated EOF */
		/* do nothing here as i didn't increment peer_pending_writes when making the write request */

		pxy->peer_pending_writes--;
		HIO_ASSERT(hio, pxy->peer_pending_writes == 0);
		HIO_DEBUG3(hio, "HTTS(%p) - indicated EOF to peer %p(hnd=%d)\n", pxy->htts, sck, (int)sck->hnd);
		/* indicated EOF to the peer side. i need no more data from the client side.
		 * i don't need to enable input watching in the client side either */
		pxy_mark_over(pxy, PXY_OVER_WRITE_TO_PEER);
	}
	else
	{
		HIO_ASSERT(hio, pxy->peer_pending_writes > 0);

		pxy->peer_pending_writes--;
		if (pxy->peer_pending_writes == PXY_PENDING_IO_THRESHOLD)
		{
			if (!(pxy->over & PXY_OVER_READ_FROM_CLIENT) &&
			    hio_dev_sck_read(pxy->task_csck, 1) <= -1) goto oops;
		}

		if ((pxy->over & PXY_OVER_READ_FROM_CLIENT) && pxy->peer_pending_writes <= 0)
		{
			pxy_mark_over(pxy, PXY_OVER_WRITE_TO_PEER);
		}
	}

	return 0;

oops:
	pxy_halt_participating_devices(pxy);
	return 0;
}


static int peer_capture_response_header (hio_htre_t* req, const hio_bch_t* key, const hio_htre_hdrval_t* val, void* ctx)
{
	return hio_svc_htts_task_addreshdrs((hio_svc_htts_task_t*)(pxy_t*)ctx, key, val);
}

static int peer_htrd_peek (hio_htrd_t* htrd, hio_htre_t* req)
{
	pxy_peer_xtn_t* peer = hio_htrd_getxtn(htrd);
	pxy_t* pxy = peer->pxy;
	hio_svc_htts_cli_t* cli = pxy->task_client;

	if (HIO_LIKELY(cli))
	{
		int status_code = HIO_HTTP_STATUS_OK;
		const hio_bch_t* status_desc = HIO_NULL;
		int chunked;

		/* [NOTE] a proxied response reports its status on the status line.
		 * attr.status is the cgi 'Status:' header convention and does not
		 * apply to an upstream speaking real http. */
		if (hio_htre_getscodeval(req) > 0)
		{
			status_code = hio_htre_getscodeval(req);
			status_desc = hio_htre_getscodestr(req);
		}

		chunked = pxy->task_keep_client_alive && !req->attr.content_length;

		if (hio_svc_htts_task_startreshdr((hio_svc_htts_task_t*)pxy, status_code, status_desc, chunked) <= -1 ||
			hio_htre_walkheaders(req, peer_capture_response_header, pxy) <= -1 ||
			hio_svc_htts_task_endreshdr((hio_svc_htts_task_t*)pxy) <= -1) return -1;
	}

	return 0;
}

static int peer_htrd_poke (hio_htrd_t* htrd, hio_htre_t* req)
{
	/* peer response got completed */
	pxy_peer_xtn_t* peer = hio_htrd_getxtn(htrd);
	pxy_t* pxy = peer->pxy;
	int n;

	n = hio_svc_htts_task_endbody((hio_svc_htts_task_t*)pxy);
	pxy_mark_over(pxy, PXY_OVER_READ_FROM_PEER);
	return n;
}

static int peer_htrd_push_content (hio_htrd_t* htrd, hio_htre_t* req, const hio_bch_t* data, hio_oow_t dlen)
{
	pxy_peer_xtn_t* peer = hio_htrd_getxtn(htrd);
	pxy_t* pxy = peer->pxy;
	int n;

	HIO_ASSERT(pxy->htts->hio, htrd == pxy->peer_htrd);

	n = hio_svc_htts_task_addresbody((hio_svc_htts_task_t*)pxy, data, dlen);
	if (pxy->task_res_pending_writes > PXY_PENDING_IO_THRESHOLD)
	{
		if (hio_dev_sck_read(pxy->peer, 0) <= -1) n = -1;
	}

	return n;
}

static hio_htrd_recbs_t peer_htrd_recbs =
{
	peer_htrd_peek,
	peer_htrd_poke,
	peer_htrd_push_content
};

static int pxy_client_htrd_poke (hio_htrd_t* htrd, hio_htre_t* req)
{
	/* client request got completed */
	hio_svc_htts_cli_htrd_xtn_t* htrdxtn = (hio_svc_htts_cli_htrd_xtn_t*)hio_htrd_getxtn(htrd);
	hio_dev_sck_t* sck = htrdxtn->sck;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	pxy_t* pxy = (pxy_t*)cli->task;

	/* indicate EOF to the client peer */
	if (pxy_write_to_peer(pxy, HIO_NULL, 0) <= -1) return -1;

	pxy_mark_over(pxy, PXY_OVER_READ_FROM_CLIENT);
	return 0;
}

static int pxy_client_htrd_push_content (hio_htrd_t* htrd, hio_htre_t* req, const hio_bch_t* data, hio_oow_t dlen)
{
	hio_svc_htts_cli_htrd_xtn_t* htrdxtn = (hio_svc_htts_cli_htrd_xtn_t*)hio_htrd_getxtn(htrd);
	hio_dev_sck_t* sck = htrdxtn->sck;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	pxy_t* pxy = (pxy_t*)cli->task;

	HIO_ASSERT(sck->hio, cli->sck == sck);
	return pxy_write_to_peer(pxy, data, dlen);
}

static int pxy_client_on_read (hio_dev_sck_t* sck, const void* buf, hio_iolen_t len, const hio_skad_t* srcaddr);
static int pxy_client_on_write (hio_dev_sck_t* sck, hio_iolen_t wrlen, void* wrctx, const hio_skad_t* dstaddr);
static void pxy_client_on_disconnect (hio_dev_sck_t* sck);

/* the handler set this task layers onto the client socket */
static hio_dev_sck_evcb_t pxy_client_evcb =
{
	pxy_client_on_read,
	pxy_client_on_write,
	pxy_client_on_disconnect
};

static hio_htrd_recbs_t pxy_client_htrd_recbs =
{
	HIO_NULL, /* this shall be set to an actual peer handler before hio_htrd_setrecbs() */
	pxy_client_htrd_poke,
	pxy_client_htrd_push_content
};

static void pxy_client_on_disconnect (hio_dev_sck_t* sck)
{
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	hio_svc_htts_t* htts = cli->htts;
	pxy_t* pxy = (pxy_t*)cli->task;
	hio_t* hio = sck->hio;

	HIO_ASSERT(hio, sck == pxy->task_csck);
	HIO_DEBUG4(hio, "HTTS(%p) - pxy(t=%p,c=%p,csck=%p) - client socket disconnect notified\n", htts, pxy, cli, sck);

	if (pxy)
	{
		HIO_SVC_HTTS_TASK_RCUP((hio_svc_htts_task_t*)pxy);

		/* detach the task from the client and the client socket */
		hio_svc_htts_task_unbindfromclient((hio_svc_htts_task_t*)pxy, 1);

		/* call the parent handler*/
		/*if (fpxy->client_org_on_disconnect) fpxy->client_org_on_disconnect (sck);*/
		hio_svc_htts_client_default_on_disconnect(sck); /* restored to the orginal parent handler in unbind_task_from_client() */

		HIO_SVC_HTTS_TASK_RCDOWN((hio_svc_htts_task_t*)pxy);
	}

	HIO_DEBUG4(hio, "HTTS(%p) - pxy(t=%p,c=%p,csck=%p) - client socket disconnect handled\n", htts, pxy, cli, sck);
	/* Note: after this callback, the actual device pointed to by 'sck' will be freed in the main loop. */
}

static int pxy_client_on_read (hio_dev_sck_t* sck, const void* buf, hio_iolen_t len, const hio_skad_t* srcaddr)
{
	hio_t* hio = sck->hio;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	pxy_t* pxy = (pxy_t*)cli->task;
	int n;

	HIO_ASSERT(hio, sck == cli->sck);

	n = hio_svc_htts_client_default_on_read(sck, buf, len, srcaddr);

	if (len <= -1)
	{
		/* read error */
		HIO_DEBUG3(cli->htts->hio, "HTTS(%p) - read error on client %p(%d)\n", pxy->htts, sck, (int)sck->hnd);
		goto oops;
	}

	if (len == 0)
	{
		/* EOF on the client side. arrange to close */
		HIO_DEBUG3(hio, "HTTS(%p) - EOF from client %p(hnd=%d)\n", pxy->htts, sck, (int)sck->hnd);

		if (!(pxy->over & PXY_OVER_READ_FROM_CLIENT)) /* if this is true, EOF is received without pxy_client_htrd_poke() */
		{
			int x;
			x = pxy_write_to_peer(pxy, HIO_NULL, 0);
			pxy_mark_over(pxy, PXY_OVER_READ_FROM_CLIENT);
			if (x <= -1) goto oops;
		}
	}

	if (n <= -1) goto oops;
	return 0;

oops:
	pxy_halt_participating_devices(pxy);
	return 0;
}

static int pxy_client_on_write (hio_dev_sck_t* sck, hio_iolen_t wrlen, void* wrctx, const hio_skad_t* dstaddr)
{
	hio_t* hio = sck->hio;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	pxy_t* pxy = (pxy_t*)cli->task;
	int n;

	n = hio_svc_htts_client_default_on_write(sck, wrlen, wrctx, dstaddr);

	if (wrlen == 0)
	{
		/* if the connect is keep-alive, this part may not be called */
		HIO_DEBUG3(hio, "HTTS(%p) - indicated EOF to client %p(%d)\n", pxy->htts, sck, (int)sck->hnd);
		/* since EOF has been indicated to the client, it must not write to the client any further.
		 * this also means that i don't need any data from the peer side either.
		 * i don't need to enable input watching on the peer side */
		pxy_mark_over(pxy, PXY_OVER_WRITE_TO_CLIENT);
	}
	else if (wrlen > 0)
	{
		if (pxy->peer && pxy->task_res_pending_writes == PXY_PENDING_IO_THRESHOLD)
		{
			/* enable input watching */
			if (!(pxy->over & PXY_OVER_READ_FROM_PEER) &&
			    hio_dev_sck_read(pxy->peer, 1) <= -1) n = -1;
		}

		if ((pxy->over & PXY_OVER_READ_FROM_PEER) && pxy->task_res_pending_writes <= 0)
		{
			pxy_mark_over(pxy, PXY_OVER_WRITE_TO_CLIENT);
		}
	}

	if (n <= -1 || wrlen <= -1) pxy_halt_participating_devices(pxy);
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
	pxy_t* pxy = (pxy_t*)ctx;

	/* hop-by-hop headers belong to the client connection, not to the one
	 * we are about to make. Content-Length is re-stated below from what
	 * the task actually knows. */
	if (hio_comp_bcstr(key, "Connection", 1) == 0 ||
	    hio_comp_bcstr(key, "Transfer-Encoding", 1) == 0 ||
	    hio_comp_bcstr(key, "Content-Length", 1) == 0 ||
	    hio_comp_bcstr(key, "Keep-Alive", 1) == 0 ||
	    hio_comp_bcstr(key, "TE", 1) == 0 ||
	    hio_comp_bcstr(key, "Trailer", 1) == 0 ||
	    hio_comp_bcstr(key, "Upgrade", 1) == 0 ||
	    hio_comp_bcstr(key, "Expect", 1) == 0 ||
	    hio_comp_bcstr_limited(key, "Proxy-", 6, 1) == 0) return 0;

	while (val)
	{
		if (hio_becs_fcat(pxy->peer_buf, "%hs: %hs\r\n", key, val->ptr) == (hio_oow_t)-1) return -1;
		val = val->next;
	}

	return 0;
}


/* ----------------------------------------------------------------------- */



/* ----------------------------------------------------------------------- */

static void on_peer_ipaddr_resolved (hio_svc_dnc_t* dnc, hio_dns_msg_t* reqmsg, hio_errnum_t status, const void* data, hio_oow_t len)
{
	// initiate connect.
	// enable read if necessary...
}

/* ----------------------------------------------------------------------- */

/* Lay out the request line and headers for the upstream. This runs while
 * the client's request is still live; the bytes wait in peer_buf until the
 * connection is up. */
static int build_request_head (pxy_t* pxy, hio_htre_t* req)
{
	hio_oow_t conlen;

	if (hio_becs_fcat(pxy->peer_buf, "%hs %hs HTTP/%d.%d\r\n",
	                  pxy->task_req_qmth, pxy->task_req_qpath,
	                  (int)pxy->task_req_version.major,
	                  (int)pxy->task_req_version.minor) == (hio_oow_t)-1) return -1;

	if (hio_htre_walkheaders(req, peer_capture_request_header, pxy) <= -1) return -1;

	/* re-state the body length ourselves rather than forwarding the
	 * client's header, so it always agrees with what we actually send */
	if (hio_htre_getreqcontentlen(req, &conlen) == 0)
	{
		if (hio_becs_fcat(pxy->peer_buf, "Content-Length: %ju\r\n", (hio_uintmax_t)conlen) == (hio_oow_t)-1) return -1;
	}

	/* [TODO] keep the upstream connection alive and reuse it. closing
	 * after each response is correct but wasteful. */
	if (hio_becs_cat(pxy->peer_buf, "Connection: close\r\n") == (hio_oow_t)-1) return -1;
	if (hio_becs_cat(pxy->peer_buf, "\r\n") == (hio_oow_t)-1) return -1;

	return 0;
}

static int bind_task_to_peer (pxy_t* pxy, hio_dev_sck_t* csck, hio_htre_t* req, const hio_skad_t* skad)
{
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(csck);
	hio_svc_htts_t* htts = pxy->htts;
	hio_t* hio = htts->hio;
	hio_dev_sck_make_t m;
	hio_dev_sck_connect_t c;
	hio_dev_sck_t* sck = HIO_NULL;
	hio_htrd_t* htrd = HIO_NULL;
	pxy_peer_xtn_t* pxtn;
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

	HIO_MEMSET(&m, 0, HIO_SIZEOF(m));
	if (hio_get_stream_sck_type_from_skad(skad, &m.type) <= -1)
	{
		hio_seterrnum(hio, HIO_EINVAL);
		goto oops;
	}

	m.on_write = pxy_peer_on_write;
	m.on_read = pxy_peer_on_read;
	m.on_connect = pxy_peer_on_connect;
	m.on_disconnect = pxy_peer_on_disconnect;

	sck = hio_dev_sck_make(hio, HIO_SIZEOF(*pxtn), &m);
	htrd = hio_htrd_open(hio, HIO_SIZEOF(*pxtn));
	if (HIO_UNLIKELY(!sck || !htrd)) goto oops;

	hio_htrd_setoption (htrd, HIO_HTRD_RESPONSE);
	hio_htrd_setrecbs (htrd, &peer_htrd_recbs);

	pxy->peer = sck;
	pxy->peer_htrd = htrd;

	pxtn = hio_dev_sck_getxtn(pxy->peer);
	pxtn->pxy = pxy;
	HIO_SVC_HTTS_TASK_RCUP(pxy);

	pxtn = hio_htrd_getxtn(pxy->peer_htrd);
	pxtn->pxy = pxy;
	HIO_SVC_HTTS_TASK_RCUP(pxy);

	/* Serialize the request now, while 'req' is still valid. It is only
	 * handed over once the connection completes. */
	if (build_request_head(pxy, req) <= -1) goto oops;

	HIO_MEMSET (&c, 0, HIO_SIZEOF(c));
	c.remoteaddr = *skad;
	HIO_INIT_NTIME (&c.connect_tmout, PXY_PEER_CONNECT_TMOUT, 0);
	if (hio_dev_sck_connect(pxy->peer, &c) <= -1) goto oops;

	return 0;

oops:
	if (htrd) hio_htrd_close(htrd);
	if (sck) hio_dev_sck_kill(sck);
	return -1;
}

static void unbind_task_from_peer (pxy_t* pxy, int rcdown)
{
	int n = 0;

	if (pxy->peer_htrd)
	{
		hio_htrd_close(pxy->peer_htrd);
		pxy->peer_htrd = HIO_NULL;
		n++;
	}

	if (pxy->peer)
	{
		pxy_peer_xtn_t* peer_xtn;
		peer_xtn = hio_dev_sck_getxtn(pxy->peer);
		peer_xtn->pxy = HIO_NULL;

		hio_dev_sck_kill(pxy->peer);
		pxy->peer = HIO_NULL;
		n++;
	}

	if (rcdown)
	{
		while (n > 0)
		{
			n--;
			HIO_SVC_HTTS_TASK_RCDOWN((hio_svc_htts_task_t*)pxy);
		}
	}
}

/* ----------------------------------------------------------------------- */

static int setup_for_content_length(pxy_t* pxy, hio_htre_t* req)
{
	int have_content;

#if defined(PXY_ALLOW_UNLIMITED_REQ_CONTENT_LENGTH)
	have_content = pxy->task_req_conlen > 0 || pxy->task_req_conlen_unlimited;
#else
	have_content = pxy->task_req_conlen > 0;
#endif

	if (have_content)
	{
		/* change the callbacks to subscribe to contents to be uploaded */
		pxy->task_client_htrd_org_recbs = *hio_htrd_getrecbs(pxy->task_client->htrd);
		pxy_client_htrd_recbs.peek = pxy->task_client_htrd_org_recbs.peek;
		hio_htrd_setrecbs (pxy->task_client->htrd, &pxy_client_htrd_recbs);
		pxy->task_client_htrd_recbs_changed = 1;
	}
	else
	{
		/* no content to be uploaded from the client */
		/* indicate EOF to the peer and disable input wathching from the client */
		if (pxy_write_to_peer(pxy, HIO_NULL, 0) <= -1) return -1;
		pxy_mark_over(pxy, PXY_OVER_READ_FROM_CLIENT | PXY_OVER_WRITE_TO_PEER);
	}

	return 0;
}

/* ----------------------------------------------------------------------- */

int hio_svc_htts_dopxy (hio_svc_htts_t* htts, hio_dev_sck_t* csck, hio_htre_t* req, const hio_skad_t* tgt_addr, int options, hio_svc_htts_task_on_kill_t on_kill)
{
	hio_t* hio = htts->hio;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(csck);
	pxy_t* pxy = HIO_NULL;
	int n, status_code = HIO_HTTP_STATUS_INTERNAL_SERVER_ERROR;
	int bound_to_client = 0, bound_to_peer = 0;

	/* ensure that you call this function before any contents is received */
	HIO_ASSERT(hio, hio_htre_getcontentlen(req) == 0);
	HIO_ASSERT(hio, cli->sck == csck);

	if (cli->task)
	{
		hio_seterrbfmt(hio, HIO_EPERM, "duplicate task request prohibited");
		goto oops;
	}

	pxy = (pxy_t*)hio_svc_htts_task_make(htts, HIO_SIZEOF(*pxy), pxy_on_kill, req, csck);
	if (HIO_UNLIKELY(!pxy)) goto oops;
	HIO_SVC_HTTS_TASK_RCUP((hio_svc_htts_task_t*)pxy);

	pxy->options = options;

	hio_svc_htts_task_bindtoclient((hio_svc_htts_task_t*)pxy, csck, &pxy_client_evcb);
	bound_to_client = 1;

	pxy->peer_buf = hio_becs_open(hio, 0, 512);
	if (HIO_UNLIKELY(!pxy->peer_buf)) goto oops;

	if ((n = bind_task_to_peer(pxy, csck, req, tgt_addr)) <= -1)
	{
		hio_svc_htts_task_sendfinalres((hio_svc_htts_task_t*)pxy, (n == 2? HIO_HTTP_STATUS_FORBIDDEN: HIO_HTTP_STATUS_INTERNAL_SERVER_ERROR), HIO_NULL, HIO_NULL, 1);
		goto oops; /* TODO: must not go to oops.  just destroy the pxy and finalize the request .. */
	}
	bound_to_peer = 1;

	if (hio_svc_htts_task_handleexpect100((hio_svc_htts_task_t*)pxy, 0) <= -1) goto oops;
	if (setup_for_content_length(pxy, req) <= -1) goto oops;

	/* TODO: store current input watching state and use it when destroying the pxy data */
	if (hio_dev_sck_read(csck, !(pxy->over & PXY_OVER_READ_FROM_CLIENT)) <= -1) goto oops;

	HIO_SVC_HTTS_TASKL_APPEND_TASK(&htts->task, (hio_svc_htts_task_t*)pxy);
	HIO_SVC_HTTS_TASK_RCDOWN((hio_svc_htts_task_t*)pxy);

	/* set the on_kill callback only if this function can return success.
	 * the on_kill callback won't be executed if this function returns failure. */
	pxy->on_kill = on_kill;
	return 0;

oops:
	HIO_DEBUG2(hio, "HTTS(%p) - FAILURE in dopxy - socket(%p)\n", htts, csck);
	if (pxy)
	{
		hio_svc_htts_task_sendfinalres((hio_svc_htts_task_t*)pxy, status_code, HIO_NULL, HIO_NULL, 1);
		if (bound_to_peer) unbind_task_from_peer (pxy, 1);
		if (bound_to_client) hio_svc_htts_task_unbindfromclient((hio_svc_htts_task_t*)pxy, 1);
		pxy_halt_participating_devices(pxy);
		HIO_SVC_HTTS_TASK_RCDOWN((hio_svc_htts_task_t*)pxy);
	}
	return -1;
}
