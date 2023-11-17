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
#include <hio-thr.h>
#include <hio-fmt.h>
#include <hio-chr.h>

#include <pthread.h>

#define THR_ALLOW_UNLIMITED_REQ_CONTENT_LENGTH

#define THR_PENDING_IO_THRESHOLD 5

#define THR_OVER_READ_FROM_CLIENT (1 << 0)
#define THR_OVER_READ_FROM_PEER   (1 << 1)
#define THR_OVER_WRITE_TO_CLIENT  (1 << 2)
#define THR_OVER_WRITE_TO_PEER    (1 << 3)
#define THR_OVER_ALL (THR_OVER_READ_FROM_CLIENT | THR_OVER_READ_FROM_PEER | THR_OVER_WRITE_TO_CLIENT | THR_OVER_WRITE_TO_PEER)

struct thr_func_start_t
{
	hio_t* hio; /* for faster and safer access in case htts has been already destroyed */
	hio_svc_htts_t* htts;
	hio_svc_htts_thr_func_t thr_func;
	void* thr_ctx;
	hio_svc_htts_thr_func_info_t tfi;
};
typedef struct thr_func_start_t thr_func_start_t;

struct thr_t
{
	HIO_SVC_HTTS_TASK_HEADER;

	hio_svc_htts_task_on_kill_t on_kill; /* user-provided on_kill callback */

	int options;
	hio_oow_t num_pending_writes_to_peer;
	hio_dev_thr_t* peer;
	hio_htrd_t* peer_htrd;

	unsigned int over: 4; /* must be large enough to accomodate THR_OVER_ALL */
	unsigned int client_htrd_recbs_changed: 1;

	hio_dev_sck_on_read_t client_org_on_read;
	hio_dev_sck_on_write_t client_org_on_write;
	hio_dev_sck_on_disconnect_t client_org_on_disconnect;
	hio_htrd_recbs_t client_htrd_org_recbs;
};

typedef struct thr_t thr_t;

struct thr_peer_xtn_t
{
	thr_t* task;
};
typedef struct thr_peer_xtn_t thr_peer_xtn_t;

static void unbind_task_from_client (thr_t* thr, int rcdown);
static void unbind_task_from_peer (thr_t* thr, int rcdown);

static void thr_halt_participating_devices (thr_t* thr)
{
	HIO_DEBUG4 (thr->htts->hio, "HTTS(%p) - Halting participating devices in thr task %p(csck=%p,peer=%p)\n", thr->htts, thr, thr->task_csck, thr->peer);

	if (thr->task_csck) hio_dev_sck_halt (thr->task_csck);
	/* check for peer as it may not have been started */
	if (thr->peer) hio_dev_thr_halt (thr->peer);
}

static int thr_write_to_peer (thr_t* thr, const void* data, hio_iolen_t dlen)
{
	if (thr->peer)
	{
		thr->num_pending_writes_to_peer++;
		if (hio_dev_thr_write(thr->peer, data, dlen, HIO_NULL) <= -1)
		{
			thr->num_pending_writes_to_peer--;
			return -1;
		}

/* TODO: check if it's already finished or something.. */
		if (thr->num_pending_writes_to_peer > THR_PENDING_IO_THRESHOLD)
		{
			if (thr->task_csck && hio_dev_sck_read(thr->task_csck, 0) <= -1) return -1;
		}
	}
	return 0;
}

static HIO_INLINE void thr_mark_over (thr_t* thr, int over_bits)
{
	hio_svc_htts_t* htts = thr->htts;
	hio_t* hio = htts->hio;
	unsigned int old_over;

	old_over = thr->over;
	thr->over |= over_bits;

	HIO_DEBUG8 (hio, "HTTS(%p) - thr(t=%p,c=%p[%d],p=%p) - old_over=%x | new-bits=%x => over=%x\n", thr->htts, thr, thr->task_client, (thr->task_csck? thr->task_csck->hnd: -1), thr->peer, (int)old_over, (int)over_bits, (int)thr->over);

	if (!(old_over & THR_OVER_READ_FROM_CLIENT) && (thr->over & THR_OVER_READ_FROM_CLIENT))
	{
		if (thr->task_csck && hio_dev_sck_read(thr->task_csck, 0) <= -1)
		{
			HIO_DEBUG5 (hio, "HTTS(%p) - thr(t=%p,c=%p[%d],p=%p) - halting client for failure to disable input watching\n", thr->htts, thr, thr->task_client, (thr->task_csck? thr->task_csck->hnd: -1), thr->peer);
			hio_dev_sck_halt (thr->task_csck);
		}
	}

	if (!(old_over & THR_OVER_READ_FROM_PEER) && (thr->over & THR_OVER_READ_FROM_PEER))
	{
		if (thr->peer && hio_dev_thr_read(thr->peer, 0) <= -1)
		{
			HIO_DEBUG5 (hio, "HTTS(%p) - thr(t=%p,c=%p[%d],p=%p) - halting peer for failure to disable input watching\n", thr->htts, thr, thr->task_client, (thr->task_csck? thr->task_csck->hnd: -1), thr->peer);
			hio_dev_thr_halt (thr->peer);
		}
	}

	if (old_over != THR_OVER_ALL && thr->over == THR_OVER_ALL)
	{
		/* ready to stop */
		if (thr->peer)
		{
			HIO_DEBUG5 (hio, "HTTS(%p) - thr(t=%p,c=%p[%d],p=%p) - halting peer as it is unneeded\n", thr->htts, thr, thr->task_client, (thr->task_csck? thr->task_csck->hnd: -1), thr->peer);
			hio_dev_thr_halt (thr->peer);
		}

		if (thr->task_csck)
		{
			HIO_ASSERT (hio, thr->task_client != HIO_NULL);

			if (thr->task_keep_client_alive)
			{
				/* how to arrange to delete this thr object and put the socket back to the normal waiting state??? */
				HIO_ASSERT (thr->htts->hio, thr->task_client->task == (hio_svc_htts_task_t*)thr);
				unbind_task_from_client (thr, 1);
				/* IMPORTANT: thr must not be accessed from here down as it could have been destroyed */
			}
			else
			{
				HIO_DEBUG5 (hio, "HTTS(%p) - thr(t=%p,c=%p[%d],p=%p) - halting client for no keep-alive\n", thr->htts, thr, thr->task_client, (thr->task_csck? thr->task_csck->hnd: -1), thr->peer);
				hio_dev_sck_shutdown (thr->task_csck, HIO_DEV_SCK_SHUTDOWN_WRITE);
				hio_dev_sck_halt (thr->task_csck);
			}
		}
	}
}

static void thr_on_kill (hio_svc_htts_task_t* task)
{
	thr_t* thr = (thr_t*)task;
	hio_t* hio = thr->htts->hio;

	HIO_DEBUG5 (hio, "HTTS(%p) - thr(t=%p,c=%p[%d],p=%p) - killing the task\n", thr->htts, thr, thr->task_client, (thr->task_csck? thr->task_csck->hnd: -1), thr->peer);

	if (thr->on_kill) thr->on_kill (task);

	/* [NOTE]
	 * 1. if hio_svc_htts_task_kill() is called, thr->peer, thr->peer_htrd, thr->task_csck,
	 *    thr->task_client may not not null.
	 * 2. this callback function doesn't decrement the reference count on thr because
	 *    it is the task destruction callback. (passing 0 to unbind_task_from_peer/client)
	 */

	unbind_task_from_peer (thr, 0);

	if (thr->task_csck)
	{
		HIO_ASSERT (hio, thr->task_client != HIO_NULL);
		unbind_task_from_client (thr, 0);
	}

	if (thr->task_next) HIO_SVC_HTTS_TASKL_UNLINK_TASK (thr); /* detach from the htts service only if it's attached */
	HIO_DEBUG5 (hio, "HTTS(%p) - thr(t=%p,c=%p[%d],p=%p) - killed the task\n", thr->htts, thr, thr->task_client, (thr->task_csck? thr->task_csck->hnd: -1), thr->peer);
}

static void thr_peer_on_close (hio_dev_thr_t* peer, hio_dev_thr_sid_t sid)
{
	hio_t* hio = peer->hio;
	thr_peer_xtn_t* pxtn = (thr_peer_xtn_t*)hio_dev_thr_getxtn(peer);
	thr_t* thr = pxtn->task;

	if (!thr) return; /* thr task already gone */

	switch (sid)
	{
		case HIO_DEV_THR_MASTER:
			HIO_DEBUG2 (hio, "HTTS(%p) - peer %p closing master\n", thr->htts, peer);
			/* reset thr->peer before calling unbind_task_from_peer() because this is the peer close callback */
			thr->peer = HIO_NULL;
			unbind_task_from_peer (thr, 1);
			break;

		case HIO_DEV_THR_OUT:
			HIO_ASSERT (hio, thr->peer == peer);
			HIO_DEBUG3 (hio, "HTTS(%p) - peer %p closing slave[%d]\n", thr->htts, peer, sid);

			if (!(thr->over & THR_OVER_READ_FROM_PEER))
			{
				if (hio_svc_htts_task_endbody(thr) <= -1)
					thr_halt_participating_devices (thr);
				else
					thr_mark_over (thr, THR_OVER_READ_FROM_PEER);
			}
			break;

		case HIO_DEV_THR_IN:
			thr_mark_over (thr, THR_OVER_WRITE_TO_PEER);
			break;

		default:
			HIO_DEBUG3 (hio, "HTTS(%p) - peer %p closing slave[%d]\n", thr->htts, peer, sid);
			/* do nothing */
			break;
	}
}

static int thr_peer_on_read (hio_dev_thr_t* peer, const void* data, hio_iolen_t dlen)
{
	hio_t* hio = peer->hio;
	thr_peer_xtn_t* pxtn = (thr_peer_xtn_t*)hio_dev_thr_getxtn(peer);
	thr_t* thr = pxtn->task;

	HIO_ASSERT (hio, thr != HIO_NULL);

	if (dlen <= -1)
	{
		HIO_DEBUG2 (hio, "HTTPS(%p) - read error from peer %p\n", thr->htts, peer);
		goto oops;
	}

	if (dlen == 0)
	{
		HIO_DEBUG2 (hio, "HTTPS(%p) - EOF from peer %p\n", thr->htts, peer);

		if (!(thr->over & THR_OVER_READ_FROM_PEER))
		{
			int n;
			/* the thr script could be misbehaviing.
			 * it still has to read more but EOF is read.
			 * otherwise client_peer_htrd_poke() should have been called */
			n = hio_svc_htts_task_endbody(thr);
			thr_mark_over (thr, THR_OVER_READ_FROM_PEER);
			if (n <= -1) goto oops;
		}
	}
	else
	{
		hio_oow_t rem;

		HIO_ASSERT (hio, !(thr->over & THR_OVER_READ_FROM_PEER));

		if (hio_htrd_feed(thr->peer_htrd, data, dlen, &rem) <= -1)
		{
			HIO_DEBUG2 (hio, "HTTPS(%p) - unable to feed peer htrd - peer %p\n", thr->htts, peer);

			if (!thr->task_res_started && !(thr->over & THR_OVER_WRITE_TO_CLIENT))
			{
				hio_svc_htts_task_sendfinalres (thr, HIO_HTTP_STATUS_BAD_GATEWAY, HIO_NULL, HIO_NULL, 1); /* don't care about error because it jumps to oops below anyway */
			}

			goto oops;
		}

	#if 0
		if (rem > 0)
		{
			/* If the script specifies Content-Length and produces longer data, it will come here */
		}
	#endif
	}

	return 0;

oops:
	thr_halt_participating_devices (thr);
	return 0;
}

static int peer_capture_response_header (hio_htre_t* req, const hio_bch_t* key, const hio_htre_hdrval_t* val, void* ctx)
{
	thr_t* thr = (thr_t*)ctx;
	return hio_svc_htts_task_addreshdrs(thr, key, val);
}

static int thr_peer_htrd_peek (hio_htrd_t* htrd, hio_htre_t* req)
{
	thr_peer_xtn_t* peer = hio_htrd_getxtn(htrd);
	thr_t* thr = peer->task;
	hio_svc_htts_cli_t* cli = thr->task_client;

	if (HIO_LIKELY(cli))
	{
		int status_code = HIO_HTTP_STATUS_OK;
		const hio_bch_t* status_desc = HIO_NULL;
		int chunked;

		if (req->attr.status) hio_parse_http_status_header_value(req->attr.status, &status_code, &status_desc);

		chunked = thr->task_keep_client_alive && !req->attr.content_length;

		if (hio_svc_htts_task_startreshdr(thr, status_code, status_desc, chunked) <= -1 ||
			hio_htre_walkheaders(req, peer_capture_response_header, thr) <= -1 ||
			hio_svc_htts_task_endreshdr(thr) <= -1) return -1;
	}

	return 0;
}

static int thr_peer_htrd_poke (hio_htrd_t* htrd, hio_htre_t* req)
{
	/* client request got completed */
	thr_peer_xtn_t* pxtn = hio_htrd_getxtn(htrd);
	thr_t* thr = pxtn->task;
	int n;

	n = hio_svc_htts_task_endbody(thr);
	thr_mark_over (thr, THR_OVER_READ_FROM_PEER);
	return n;
}

static int thr_peer_htrd_push_content (hio_htrd_t* htrd, hio_htre_t* req, const hio_bch_t* data, hio_oow_t dlen)
{
	thr_peer_xtn_t* pxtn = hio_htrd_getxtn(htrd);
	thr_t* thr = pxtn->task;
	int n;

	HIO_ASSERT (thr->htts->hio, htrd == thr->peer_htrd);

	n = hio_svc_htts_task_addresbody(thr, data, dlen);
	if (thr->task_res_pending_writes > THR_PENDING_IO_THRESHOLD)
	{
		if (hio_dev_thr_read(thr->peer, 0) <= -1) n = -1;
	}

	return n;
}

static hio_htrd_recbs_t thr_peer_htrd_recbs =
{
	thr_peer_htrd_peek,
	thr_peer_htrd_poke,
	thr_peer_htrd_push_content
};

static int thr_client_htrd_poke (hio_htrd_t* htrd, hio_htre_t* req)
{
	/* client request got completed */
	hio_svc_htts_cli_htrd_xtn_t* htrdxtn = (hio_svc_htts_cli_htrd_xtn_t*)hio_htrd_getxtn(htrd);
	hio_dev_sck_t* sck = htrdxtn->sck;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	thr_t* thr = (thr_t*)cli->task;

	/* indicate EOF to the client peer */
	if (thr_write_to_peer(thr, HIO_NULL, 0) <= -1) return -1;

	thr_mark_over (thr, THR_OVER_READ_FROM_CLIENT);
	return 0;
}

static int thr_client_htrd_push_content (hio_htrd_t* htrd, hio_htre_t* req, const hio_bch_t* data, hio_oow_t dlen)
{
	hio_svc_htts_cli_htrd_xtn_t* htrdxtn = (hio_svc_htts_cli_htrd_xtn_t*)hio_htrd_getxtn(htrd);
	hio_dev_sck_t* sck = htrdxtn->sck;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	thr_t* thr = (thr_t*)cli->task;

	HIO_ASSERT (sck->hio, cli->sck == sck);
	return thr_write_to_peer(thr, data, dlen);
}

static hio_htrd_recbs_t thr_client_htrd_recbs =
{
	HIO_NULL,
	thr_client_htrd_poke,
	thr_client_htrd_push_content
};

static int thr_peer_on_write (hio_dev_thr_t* peer, hio_iolen_t wrlen, void* wrctx)
{
	hio_t* hio = peer->hio;
	thr_peer_xtn_t* pxtn = (thr_peer_xtn_t*)hio_dev_thr_getxtn(peer);
	thr_t* thr = pxtn->task;

	if (!thr) return 0; /* there is nothing i can do. the thr is being cleared or has been cleared already. */

	HIO_ASSERT (hio, thr->peer == peer);

	if (wrlen <= -1)
	{
		HIO_DEBUG2 (hio, "HTTS(%p) - unable to write to peer %p\n", thr->htts, peer);
		goto oops;
	}
	else if (wrlen == 0)
	{
		/* indicated EOF */
		/* do nothing here as i didn't incremented num_pending_writes_to_peer when making the write request */

		thr->num_pending_writes_to_peer--;
		HIO_ASSERT (hio, thr->num_pending_writes_to_peer == 0);
		HIO_DEBUG2 (hio, "HTTS(%p) - indicated EOF to peer %p\n", thr->htts, peer);
		/* indicated EOF to the peer side. i need no more data from the client side.
		 * i don't need to enable input watching in the client side either */
		thr_mark_over (thr, THR_OVER_WRITE_TO_PEER);
	}
	else
	{
		HIO_ASSERT (hio, thr->num_pending_writes_to_peer > 0);

		thr->num_pending_writes_to_peer--;
		if (thr->num_pending_writes_to_peer == THR_PENDING_IO_THRESHOLD)
		{
			if (!(thr->over & THR_OVER_READ_FROM_CLIENT) &&
			    hio_dev_sck_read(thr->task_csck, 1) <= -1) goto oops;
		}

		if ((thr->over & THR_OVER_READ_FROM_CLIENT) && thr->num_pending_writes_to_peer <= 0)
		{
			thr_mark_over (thr, THR_OVER_WRITE_TO_PEER);
		}
	}

	return 0;

oops:
	thr_halt_participating_devices (thr);
	return 0;
}

static void thr_client_on_disconnect (hio_dev_sck_t* sck)
{
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	thr_t* thr = (thr_t*)cli->task;
	hio_svc_htts_t* htts = thr->htts;
	hio_t* hio = sck->hio;

	HIO_ASSERT (hio, sck = thr->task_csck);
	HIO_DEBUG4 (hio, "HTTS(%p) - thr(t=%p,c=%p,csck=%p) - client socket disconnect notified\n", htts, thr, cli, sck);

	if (thr)
	{
		HIO_SVC_HTTS_TASK_RCUP (thr);

		unbind_task_from_client (thr, 1);

		/* call the parent handler*/
		/*if (thr->client_org_on_disconnect) thr->client_org_on_disconnect (sck);*/
		if (sck->on_disconnect) sck->on_disconnect (sck); /* restored to the orginal parent handler in unbind_task_from_client() */

		HIO_SVC_HTTS_TASK_RCDOWN (thr);
	}

	HIO_DEBUG4 (hio, "HTTS(%p) - thr(t=%p,c=%p,csck=%p) - client socket disconnect handled\n", htts, thr, cli, sck);
	/* Note: after this callback, the actual device pointed to by 'sck' will be freed in the main loop. */
}

static int thr_client_on_read (hio_dev_sck_t* sck, const void* buf, hio_iolen_t len, const hio_skad_t* srcaddr)
{
	hio_t* hio = sck->hio;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	thr_t* thr = (thr_t*)cli->task;
	int n;

	HIO_ASSERT (hio, sck == cli->sck);

	n = thr->client_org_on_read? thr->client_org_on_read(sck, buf, len, srcaddr): 0;

	if (len <= -1)
	{
		/* read error */
		HIO_DEBUG2 (cli->htts->hio, "HTTPS(%p) - read error on client %p(%d)\n", sck, (int)sck->hnd);
		goto oops;
	}

	if (len == 0)
	{
		/* EOF on the client side. arrange to close */
		HIO_DEBUG3 (hio, "HTTPS(%p) - EOF from client %p(hnd=%d)\n", thr->htts, sck, (int)sck->hnd);

		if (!(thr->over & THR_OVER_READ_FROM_CLIENT)) /* if this is true, EOF is received without thr_client_htrd_poke() */
		{
			int n;
			n = thr_write_to_peer(thr, HIO_NULL, 0);
			thr_mark_over (thr, THR_OVER_READ_FROM_CLIENT);
			if (n <= -1) goto oops;
		}
	}

	if (n <= -1) goto oops;
	return 0;

oops:
	thr_halt_participating_devices (thr);
	return 0;
}

static int thr_client_on_write (hio_dev_sck_t* sck, hio_iolen_t wrlen, void* wrctx, const hio_skad_t* dstaddr)
{
	hio_t* hio = sck->hio;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	thr_t* thr = (thr_t*)cli->task;
	int n;

	n = thr->client_org_on_write? thr->client_org_on_write(sck, wrlen, wrctx, dstaddr): 0;

	if (wrlen == 0)
	{
		/* since EOF has been indicated to the client, it must not write to the client any further.
		 * this also means that i don't need any data from the peer side either.
		 * i don't need to enable input watching on the peer side */
		thr_mark_over (thr, THR_OVER_WRITE_TO_CLIENT);
	}
	else if (wrlen > 0)
	{
		if (thr->peer && thr->task_res_pending_writes == THR_PENDING_IO_THRESHOLD)
		{
			/* enable input watching */
			if (!(thr->over & THR_OVER_READ_FROM_PEER) &&
			    hio_dev_thr_read(thr->peer, 1) <= -1) n = -1;
		}

		if ((thr->over & THR_OVER_READ_FROM_PEER) && thr->task_res_pending_writes <= 0)
		{
			thr_mark_over (thr, THR_OVER_WRITE_TO_CLIENT);
		}
	}

	if (n <= -1 || wrlen <= -1) thr_halt_participating_devices (thr);
	return 0;
}

static void free_thr_start_info (void* ctx)
{
	/* this function is a thread cleanup handler.
	 * it can get invoked after htts is destroyed by hio_svc_htts_stop() because
	 * hio_dev_thr_kill() pushes back the job using hio_addcfmb() and the
	 * actual cfmb clean-up is performed after the service stop.
	 * it is not realiable to use tfs->htts or tfs->htts->hio. use tfs->hio only here.
==3845396== Invalid read of size 8
==3845396==    at 0x40A7D5: free_thr_start_info (http-thr.c:804)
==3845396==    by 0x40A7D5: thr_func (http-thr.c:815)
==3845396==    by 0x41AE46: run_thr_func (thr.c:127)
==3845396==    by 0x4A132A4: start_thread (in /usr/lib64/libpthread-2.33.so)
==3845396==    by 0x4B2B322: clone (in /usr/lib64/libc-2.33.so)
==3845396==  Address 0x4c38b00 is 0 bytes inside a block of size 464 free'd
==3845396==    at 0x48430E4: free (vg_replace_malloc.c:872)
==3845396==    by 0x4091EE: hio_svc_htts_stop (http-svr.c:555)
==3845396==    by 0x40F5BE: hio_fini (hio.c:185)
==3845396==    by 0x40F848: hio_close (hio.c:101)
==3845396==    by 0x402CB4: main (webs.c:511)
==3845396==  Block was alloc'd at
==3845396==    at 0x484086F: malloc (vg_replace_malloc.c:381)
==3845396==    by 0x412873: hio_callocmem (hio.c:2019)
==3845396==    by 0x40978E: hio_svc_htts_start (http-svr.c:350)
==3845396==    by 0x403900: webs_start (webs.c:385)
==3845396==    by 0x402C6C: main (webs.c:498)
	 */
	thr_func_start_t* tfs = (thr_func_start_t*)ctx;
	hio_t* hio = tfs->hio;
	if (tfs->tfi.req_path) hio_freemem (hio, tfs->tfi.req_path);
	if (tfs->tfi.req_param) hio_freemem (hio, tfs->tfi.req_param);
	hio_freemem (hio, tfs);
}

static void thr_func (hio_t* hio, hio_dev_thr_iopair_t* iop, void* ctx)
{
	thr_func_start_t* tfs = (thr_func_start_t*)ctx;
	pthread_cleanup_push (free_thr_start_info, tfs);
	tfs->thr_func (tfs->htts, iop, &tfs->tfi, tfs->thr_ctx);
	pthread_cleanup_pop (1);
}

static int thr_capture_request_header (hio_htre_t* req, const hio_bch_t* key, const hio_htre_hdrval_t* val, void* ctx)
{
	thr_func_start_t* tfs = (thr_func_start_t*)ctx;

	if (hio_comp_bcstr(key, "X-HTTP-Method-Override", 1) == 0)
	{
		tfs->tfi.req_x_http_method_override = hio_bchars_to_http_method(val->ptr, val->len); /* don't care about multiple values */
	}

#if 0
	if (hio_comp_bcstr(key, "Connection", 1) != 0 &&
	    hio_comp_bcstr(key, "Transfer-Encoding", 1) != 0 &&
	    hio_comp_bcstr(key, "Content-Length", 1) != 0 &&
	    hio_comp_bcstr(key, "Expect", 1) != 0)
	{
		do
		{
			/* TODO: ... */
			val = val->next;
		}
		while (val);
	}
#endif

	return 0;
}

/* ----------------------------------------------------------------------- */

static void bind_task_to_client (thr_t* thr, hio_dev_sck_t* csck)
{
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(csck);

	HIO_ASSERT (thr->htts->hio, cli->sck == csck);
	HIO_ASSERT (thr->htts->hio, cli->task == HIO_NULL);

	thr->client_org_on_read = csck->on_read;
	thr->client_org_on_write = csck->on_write;
	thr->client_org_on_disconnect = csck->on_disconnect;
	csck->on_read = thr_client_on_read;
	csck->on_write = thr_client_on_write;
	csck->on_disconnect = thr_client_on_disconnect;

	cli->task = (hio_svc_htts_task_t*)thr;
	HIO_SVC_HTTS_TASK_RCUP (thr);
}


static void unbind_task_from_client (thr_t* thr, int rcdown)
{
	hio_dev_sck_t* csck = thr->task_csck;

	HIO_ASSERT (thr->htts->hio, thr->task_client != HIO_NULL);
	HIO_ASSERT (thr->htts->hio, thr->task_csck != HIO_NULL);
	HIO_ASSERT (thr->htts->hio, thr->task_client->task == (hio_svc_htts_task_t*)thr);
	HIO_ASSERT (thr->htts->hio, thr->task_client->htrd != HIO_NULL);

	if (thr->client_htrd_recbs_changed)
	{
		hio_htrd_setrecbs (thr->task_client->htrd, &thr->client_htrd_org_recbs);
		thr->client_htrd_recbs_changed = 0;
	}

	if (thr->client_org_on_read)
	{
		csck->on_read = thr->client_org_on_read;
		thr->client_org_on_read = HIO_NULL;
	}

	if (thr->client_org_on_write)
	{
		csck->on_write = thr->client_org_on_write;
		thr->client_org_on_write = HIO_NULL;
	}

	if (thr->client_org_on_disconnect)
	{
		csck->on_disconnect = thr->client_org_on_disconnect;
		thr->client_org_on_disconnect = HIO_NULL;
	}

	/* there is some ordering issue in using HIO_SVC_HTTS_TASK_UNREF()
	 * because it can destroy the thr itself. so reset thr->task_client->task
	 * to null and call RCDOWN() later */
	thr->task_client->task = HIO_NULL;

	/* these two lines are also done in csck_on_disconnect() in http-svr.c because the socket is destroyed.
	 * the same lines here are because the task is unbound while the socket is still alive */
	thr->task_client = HIO_NULL;
	thr->task_csck = HIO_NULL;

	/* enable input watching on the socket being unbound */
	if (thr->task_keep_client_alive && hio_dev_sck_read(csck, 1) <= -1)
	{
		HIO_DEBUG2 (thr->htts->hio, "HTTS(%p) - halting client(%p) for failure to enable input watching\n", thr->htts, csck);
		hio_dev_sck_halt (csck);
	}

	if (rcdown) HIO_SVC_HTTS_TASK_RCDOWN ((hio_svc_htts_task_t*)thr);
}

/* ----------------------------------------------------------------------- */

static int bind_task_to_peer (thr_t* thr, hio_dev_sck_t* csck, hio_htre_t* req, hio_svc_htts_thr_func_t func, void* ctx)
{
	hio_svc_htts_t* htts = thr->htts;
	hio_t* hio = htts->hio;
	thr_peer_xtn_t* pxtn;
	hio_dev_thr_make_t mi;
	thr_func_start_t* tfs = HIO_NULL;
	hio_htrd_t* htrd = HIO_NULL;

	tfs = hio_callocmem(hio, HIO_SIZEOF(*tfs));
	if (!tfs) goto oops;

	tfs->hio = hio;
	tfs->htts = htts;
	tfs->thr_func = func;
	tfs->thr_ctx = ctx;

	tfs->tfi.req_method = hio_htre_getqmethodtype(req);
	tfs->tfi.req_version = *hio_htre_getversion(req);
	tfs->tfi.req_path = hio_dupbcstr(hio, hio_htre_getqpath(req), HIO_NULL);
	if (!tfs->tfi.req_path) goto oops;
	if (hio_htre_getqparam(req))
	{
		tfs->tfi.req_param = hio_dupbcstr(hio, hio_htre_getqparam(req), HIO_NULL);
		if (!tfs->tfi.req_param) goto oops;
	}

	tfs->tfi.req_x_http_method_override = -1;
	if (hio_htre_walkheaders(req, thr_capture_request_header, tfs) <= -1) goto oops;

	tfs->tfi.server_addr = csck->localaddr;
	tfs->tfi.client_addr = csck->remoteaddr;

	HIO_MEMSET (&mi, 0, HIO_SIZEOF(mi));
	mi.thr_func = thr_func;
	mi.thr_ctx = tfs;
	mi.on_read = thr_peer_on_read;
	mi.on_write = thr_peer_on_write;
	mi.on_close = thr_peer_on_close;

	htrd = hio_htrd_open(hio, HIO_SIZEOF(*pxtn));
	if (HIO_UNLIKELY(!htrd)) goto oops;
	hio_htrd_setoption (htrd, HIO_HTRD_SKIP_INITIAL_LINE | HIO_HTRD_RESPONSE);
	hio_htrd_setrecbs (htrd, &thr_peer_htrd_recbs);

	thr->peer = hio_dev_thr_make(hio, HIO_SIZEOF(*pxtn), &mi);
	if (HIO_UNLIKELY(!thr->peer))
	{
		/* no need to detach the attached task here because that is handled
		 * in the kill/disconnect callbacks of relevant devices */
		HIO_DEBUG3 (hio, "HTTS(%p) - failed to create thread for %p(%d)\n", htts, csck, (int)csck->hnd);
		goto oops;
	}

	tfs = HIO_NULL; /* mark that tfs is delegated to the thread */
	thr->peer_htrd = htrd;

	/* attach the thr task to the peer thread device */
	pxtn = hio_dev_thr_getxtn(thr->peer);
	pxtn->task = thr;

	/* attach the thr task to the htrd parser set on the peer thread device */
	pxtn = hio_htrd_getxtn(thr->peer_htrd);
	pxtn->task = thr;

	HIO_SVC_HTTS_TASK_RCUP (thr); /* for thr */
	HIO_SVC_HTTS_TASK_RCUP (thr); /* for peer_htrd */
	return 0;

oops:
	if (htrd) hio_htrd_close (htrd);
	if (tfs) free_thr_start_info (tfs);
	return -1;
}

static void unbind_task_from_peer (thr_t* thr, int rcdown)
{
	int n = 0;

	if (thr->peer_htrd)
	{
		thr_peer_xtn_t* pxtn = hio_htrd_getxtn(thr->peer_htrd);
		if (pxtn->task) pxtn->task = HIO_NULL;
		hio_htrd_close (thr->peer_htrd);
		thr->peer_htrd = HIO_NULL;
		n++;
	}

	if (thr->peer)
	{
		thr_peer_xtn_t* pxtn = hio_dev_thr_getxtn(thr->peer);
		if (pxtn->task) pxtn->task = HIO_NULL;
		hio_dev_thr_kill (thr->peer);
		thr->peer = HIO_NULL;
		n++;
	}

	if (rcdown)
	{
		while (n > 0)
		{
			n--;
			HIO_SVC_HTTS_TASK_RCDOWN((hio_svc_htts_task_t*)thr);
		}
	}
}

/* ----------------------------------------------------------------------- */

static int setup_for_content_length(thr_t* thr, hio_htre_t* req)
{
	int have_content;

#if defined(THR_ALLOW_UNLIMITED_REQ_CONTENT_LENGTH)
	have_content = thr->task_req_conlen > 0 || thr->task_req_conlen_unlimited;
#else
	have_content = thr->task_req_conlen > 0;
#endif

	if (have_content)
	{
		/* change the callbacks to subscribe to contents to be uploaded */
		thr->client_htrd_org_recbs = *hio_htrd_getrecbs(thr->task_client->htrd);
		thr_client_htrd_recbs.peek = thr->client_htrd_org_recbs.peek;
		hio_htrd_setrecbs (thr->task_client->htrd, &thr_client_htrd_recbs);
		thr->client_htrd_recbs_changed = 1;
	}
	else
	{
		/* no content to be uploaded from the client */
		/* indicate EOF to the peer and disable input wathching from the client */
		if (thr_write_to_peer(thr, HIO_NULL, 0) <= -1) return -1;
		thr_mark_over (thr, THR_OVER_READ_FROM_CLIENT | THR_OVER_WRITE_TO_PEER);
	}

	return 0;
}

/* ----------------------------------------------------------------------- */

int hio_svc_htts_dothr (hio_svc_htts_t* htts, hio_dev_sck_t* csck, hio_htre_t* req, hio_svc_htts_thr_func_t func, void* ctx, int options, hio_svc_htts_task_on_kill_t on_kill)
{
	hio_t* hio = htts->hio;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(csck);
	thr_t* thr = HIO_NULL;
	int bound_to_client = 0, bound_to_peer = 0;

	/* ensure that you call this function before any contents is received */
	HIO_ASSERT (hio, hio_htre_getcontentlen(req) == 0);

	thr = (thr_t*)hio_svc_htts_task_make(htts, HIO_SIZEOF(*thr), thr_on_kill, req, csck);
	if (HIO_UNLIKELY(!thr)) goto oops;
	HIO_SVC_HTTS_TASK_RCUP ((hio_svc_htts_task_t*)thr);

	thr->on_kill = on_kill;
	thr->options = options;

	bind_task_to_client (thr, csck);
	bound_to_client = 1;

	if (bind_task_to_peer(thr, csck, req, func, ctx) <= -1) goto oops;
	bound_to_peer = 1;

	if (hio_svc_htts_task_handleexpect100(thr) <= -1) goto oops;
	if (setup_for_content_length(thr, req) <= -1) goto oops;

	/* TODO: store current input watching state and use it when destroying the thr data */
	if (hio_dev_sck_read(csck, !(thr->over & THR_OVER_READ_FROM_CLIENT)) <= -1) goto oops;

	HIO_SVC_HTTS_TASKL_APPEND_TASK (&htts->task, (hio_svc_htts_task_t*)thr);
	HIO_SVC_HTTS_TASK_RCDOWN ((hio_svc_htts_task_t*)thr);
	return 0;

oops:
	HIO_DEBUG2 (hio, "HTTS(%p) - FAILURE in dothr - socket(%p)\n", htts, csck);
	if (thr)
	{
		if (bound_to_peer) unbind_task_from_peer (thr, 1);
		if (bound_to_client) unbind_task_from_client (thr, 1);
		thr_halt_participating_devices (thr);
		HIO_SVC_HTTS_TASK_RCDOWN ((hio_svc_htts_task_t*)thr);
	}
	return -1;
}
