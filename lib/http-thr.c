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

enum thr_res_mode_t
{
	THR_RES_MODE_CHUNKED,
	THR_RES_MODE_CLOSE,
	THR_RES_MODE_LENGTH
};
typedef enum thr_res_mode_t thr_res_mode_t;

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

struct thr_task_t
{
	HIO_SVC_HTTS_TASK_HEADER;

	hio_svc_htts_task_on_kill_t on_kill; /* user-provided on_kill callback */

	int options;
	hio_oow_t num_pending_writes_to_client;
	hio_oow_t num_pending_writes_to_peer;
	hio_dev_thr_t* peer;
	hio_htrd_t* peer_htrd;

	unsigned int over: 4; /* must be large enough to accomodate THR_OVER_ALL */
	unsigned int ever_attempted_to_write_to_client: 1;
	unsigned int client_eof_detected: 1;
	unsigned int client_disconnected: 1;
	unsigned int client_htrd_recbs_changed: 1;
	thr_res_mode_t res_mode_to_cli;

	hio_dev_sck_on_read_t client_org_on_read;
	hio_dev_sck_on_write_t client_org_on_write;
	hio_dev_sck_on_disconnect_t client_org_on_disconnect;
	hio_htrd_recbs_t client_htrd_org_recbs;
};

typedef struct thr_task_t thr_task_t;

struct thr_peer_xtn_t
{
	thr_task_t* task;
};
typedef struct thr_peer_xtn_t thr_peer_xtn_t;

static void thr_task_halt_participating_devices (thr_task_t* thr)
{
	HIO_DEBUG4 (thr->htts->hio, "HTTS(%p) - Halting participating devices in thr task %p(csck=%p,peer=%p)\n", thr->htts, thr, thr->task_csck, thr->peer);

	if (thr->task_csck) hio_dev_sck_halt (thr->task_csck);
	/* check for peer as it may not have been started */
	if (thr->peer) hio_dev_thr_halt (thr->peer);
}

static int thr_write_to_client (thr_task_t* thr, const void* data, hio_iolen_t dlen)
{
	if (thr->task_csck)
	{
		thr->ever_attempted_to_write_to_client = 1;

		thr->num_pending_writes_to_client++;
		if (hio_dev_sck_write(thr->task_csck, data, dlen, HIO_NULL, HIO_NULL) <= -1)
		{
			thr->num_pending_writes_to_client--;
			return -1;
		}

		if (thr->num_pending_writes_to_client > THR_PENDING_IO_THRESHOLD)
		{
			if (hio_dev_thr_read(thr->peer, 0) <= -1) return -1;
		}
	}
	return 0;
}

static int thr_writev_to_client (thr_task_t* thr, hio_iovec_t* iov, hio_iolen_t iovcnt)
{
	if (thr->task_csck)
	{
		thr->ever_attempted_to_write_to_client = 1;

		thr->num_pending_writes_to_client++;
		if (hio_dev_sck_writev(thr->task_csck, iov, iovcnt, HIO_NULL, HIO_NULL) <= -1)
		{
			thr->num_pending_writes_to_client--;
			return -1;
		}

		if (thr->num_pending_writes_to_client > THR_PENDING_IO_THRESHOLD)
		{
			if (hio_dev_thr_read(thr->peer, 0) <= -1) return -1;
		}
	}
	return 0;
}

static HIO_INLINE int thr_send_final_status_to_client (thr_task_t* thr, int status_code, int force_close)
{
	return hio_svc_htts_task_sendfinalres(thr, status_code, HIO_NULL, HIO_NULL, force_close);
}

static int thr_write_last_chunk_to_client (thr_task_t* thr)
{
	if (!thr->ever_attempted_to_write_to_client)
	{
		if (thr_send_final_status_to_client(thr, HIO_HTTP_STATUS_INTERNAL_SERVER_ERROR, 0) <= -1) return -1;
	}
	else
	{
		if (thr->res_mode_to_cli == THR_RES_MODE_CHUNKED &&
		    thr_write_to_client(thr, "0\r\n\r\n", 5) <= -1) return -1;
	}

	if (!thr->task_keep_client_alive && thr_write_to_client(thr, HIO_NULL, 0) <= -1) return -1;
	return 0;
}

static int thr_write_to_peer (thr_task_t* thr, const void* data, hio_iolen_t dlen)
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

static HIO_INLINE void thr_task_mark_over (thr_task_t* thr, int over_bits)
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

			if (thr->task_keep_client_alive && !thr->client_eof_detected)
			{
				/* how to arrange to delete this thr_task object and put the socket back to the normal waiting state??? */
				HIO_ASSERT (thr->htts->hio, thr->task_client->task == (hio_svc_htts_task_t*)thr);
				HIO_SVC_HTTS_TASK_UNREF (thr->task_client->task);
				/* IMPORTANT: thr_task must not be accessed from here down as it could have been destroyed */
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

static void thr_task_on_kill (hio_svc_htts_task_t* task)
{
	thr_task_t* thr = (thr_task_t*)task;
	hio_t* hio = thr->htts->hio;

	HIO_DEBUG5 (hio, "HTTS(%p) - thr(t=%p,c=%p[%d],p=%p) - killing the task\n", thr->htts, thr, thr->task_client, (thr->task_csck? thr->task_csck->hnd: -1), thr->peer);

	if (thr->on_kill) thr->on_kill (task);

	if (thr->peer)
	{
		thr_peer_xtn_t* peer_xtn = hio_dev_thr_getxtn(thr->peer);
		if (peer_xtn->task)
		{
			 /* peer_xtn->task may not be NULL if the resource is killed regardless of the reference count.
			  * anyway, don't use HIO_SVC_HTTS_TASK_UNREF (peer_xtn->task) because the resource itself
			  * is already being killed. */
			peer_xtn->task = HIO_NULL;
		}

		hio_dev_thr_kill (thr->peer);
		thr->peer = HIO_NULL;
	}

	if (thr->peer_htrd)
	{
		thr_peer_xtn_t* peer_xtn = hio_htrd_getxtn(thr->peer_htrd);
		if (peer_xtn->task) peer_xtn->task = HIO_NULL; // no HIO_SVC_HTTS_TASK_UNREF() for the same reason above

		hio_htrd_close (thr->peer_htrd);
		thr->peer_htrd = HIO_NULL;
	}

	if (thr->task_csck)
	{
		HIO_ASSERT (hio, thr->task_client != HIO_NULL);

		/* restore callbacks */
		if (thr->client_org_on_read) thr->task_csck->on_read = thr->client_org_on_read;
		if (thr->client_org_on_write) thr->task_csck->on_write = thr->client_org_on_write;
		if (thr->client_org_on_disconnect) thr->task_csck->on_disconnect = thr->client_org_on_disconnect;
		if (thr->client_htrd_recbs_changed) hio_htrd_setrecbs (thr->task_client->htrd, &thr->client_htrd_org_recbs);

		if (!thr->task_keep_client_alive || hio_dev_sck_read(thr->task_csck, 1) <= -1)
		{
			HIO_DEBUG5 (hio, "HTTS(%p) - thr(t=%p,c=%p[%d],p=%p) - halting client for failure to enable input watching\n", thr->htts, thr, thr->task_client, (thr->task_csck? thr->task_csck->hnd: -1), thr->peer);
			hio_dev_sck_halt (thr->task_csck);
		}
	}

	thr->client_org_on_read = HIO_NULL;
	thr->client_org_on_write = HIO_NULL;
	thr->client_org_on_disconnect = HIO_NULL;
	thr->client_htrd_recbs_changed = 0;

	if (thr->task_next) HIO_SVC_HTTS_TASKL_UNLINK_TASK (thr); /* detach from the htts service only if it's attached */
	HIO_DEBUG5 (hio, "HTTS(%p) - thr(t=%p,c=%p[%d],p=%p) - killed the task\n", thr->htts, thr, thr->task_client, (thr->task_csck? thr->task_csck->hnd: -1), thr->peer);
}

static void thr_peer_on_close (hio_dev_thr_t* peer, hio_dev_thr_sid_t sid)
{
	hio_t* hio = peer->hio;
	thr_peer_xtn_t* peer_xtn = (thr_peer_xtn_t*)hio_dev_thr_getxtn(peer);
	thr_task_t* thr = peer_xtn->task;

	if (!thr) return; /* thr task already gone */

	switch (sid)
	{
		case HIO_DEV_THR_MASTER:
			HIO_DEBUG2 (hio, "HTTS(%p) - peer %p closing master\n", thr->htts, peer);
			thr->peer = HIO_NULL; /* clear this peer from the state */

			HIO_ASSERT (hio, peer_xtn->task != HIO_NULL);
			HIO_SVC_HTTS_TASK_UNREF (peer_xtn->task);

			if (thr->peer_htrd)
			{
				/* once this peer device is closed, peer's htrd is also never used.
				 * it's safe to detach the extra information attached on the htrd object. */
				peer_xtn = hio_htrd_getxtn(thr->peer_htrd);
				HIO_ASSERT (hio, peer_xtn->task != HIO_NULL);
				HIO_SVC_HTTS_TASK_UNREF (peer_xtn->task);
			}

			break;

		case HIO_DEV_THR_OUT:
			HIO_ASSERT (hio, thr->peer == peer);
			HIO_DEBUG3 (hio, "HTTS(%p) - peer %p closing slave[%d]\n", thr->htts, peer, sid);

			if (!(thr->over & THR_OVER_READ_FROM_PEER))
			{
				if (thr_write_last_chunk_to_client(thr) <= -1)
					thr_task_halt_participating_devices (thr);
				else
					thr_task_mark_over (thr, THR_OVER_READ_FROM_PEER);
			}
			break;

		case HIO_DEV_THR_IN:
			thr_task_mark_over (thr, THR_OVER_WRITE_TO_PEER);
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
	thr_peer_xtn_t* peer_xtn = (thr_peer_xtn_t*)hio_dev_thr_getxtn(peer);
	thr_task_t* thr = peer_xtn->task;

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
			n = thr_write_last_chunk_to_client(thr);
			thr_task_mark_over (thr, THR_OVER_READ_FROM_PEER);
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

			if (!thr->ever_attempted_to_write_to_client &&
			    !(thr->over & THR_OVER_WRITE_TO_CLIENT))
			{
				thr_send_final_status_to_client (thr, HIO_HTTP_STATUS_INTERNAL_SERVER_ERROR, 1); /* don't care about error because it jumps to oops below anyway */
			}

			goto oops;
		}

		if (rem > 0)
		{
			/* If the script specifies Content-Length and produces longer data, it will come here */
/*printf ("AAAAAAAAAAAAAAAAAa EEEEEXcessive DATA..................\n");*/
/* TODO: or drop this request?? */
		}
	}

	return 0;

oops:
	thr_task_halt_participating_devices (thr);
	return 0;
}

static int peer_capture_response_header (hio_htre_t* req, const hio_bch_t* key, const hio_htre_hdrval_t* val, void* ctx)
{
	return hio_svc_htts_task_addreshdrs((thr_task_t*)ctx, key, val);
}

static int thr_peer_htrd_peek (hio_htrd_t* htrd, hio_htre_t* req)
{
	thr_peer_xtn_t* peer = hio_htrd_getxtn(htrd);
	thr_task_t* thr = peer->task;
	hio_svc_htts_cli_t* cli = thr->task_client;
	int status_code = HIO_HTTP_STATUS_OK;
	const hio_bch_t* status_desc = HIO_NULL;
	int chunked;

	if (HIO_UNLIKELY(!cli))
	{
		/* client disconnected or not connectd */
		return 0;
	}

	// TOOD: remove content_length if content_length is negative or not numeric.
	if (req->attr.content_length) thr->res_mode_to_cli = THR_RES_MODE_LENGTH;
	if (req->attr.status) hio_parse_http_status_header_value(req->attr.status, &status_code, &status_desc);

	chunked = thr->task_keep_client_alive && !req->attr.content_length;

	if (hio_svc_htts_task_startreshdr(thr, status_code, status_desc, chunked) <= -1 ||
	    hio_htre_walkheaders(req, peer_capture_response_header, thr) <= -1 ||
	    hio_svc_htts_task_endreshdr(thr) <= -1) return -1;

	return 0;
}

static int thr_peer_htrd_poke (hio_htrd_t* htrd, hio_htre_t* req)
{
	/* client request got completed */
	thr_peer_xtn_t* peer_xtn = hio_htrd_getxtn(htrd);
	thr_task_t* thr = peer_xtn->task;

	if (thr_write_last_chunk_to_client(thr) <= -1) return -1;

	thr_task_mark_over (thr, THR_OVER_READ_FROM_PEER);
	return 0;
}

static int thr_peer_htrd_push_content (hio_htrd_t* htrd, hio_htre_t* req, const hio_bch_t* data, hio_oow_t dlen)
{
	thr_peer_xtn_t* peer_xtn = hio_htrd_getxtn(htrd);
	thr_task_t* thr = peer_xtn->task;

	HIO_ASSERT (thr->htts->hio, htrd == thr->peer_htrd);

	switch (thr->res_mode_to_cli)
	{
		case THR_RES_MODE_CHUNKED:
		{
			hio_iovec_t iov[3];
			hio_bch_t lbuf[16];
			hio_oow_t llen;

			/* hio_fmt_uintmax_to_bcstr() null-terminates the output. only HIO_COUNTOF(lbuf) - 1
			 * is enough to hold '\r' and '\n' at the back without '\0'. */
			llen = hio_fmt_uintmax_to_bcstr(lbuf, HIO_COUNTOF(lbuf) - 1, dlen, 16 | HIO_FMT_UINTMAX_UPPERCASE, 0, '\0', HIO_NULL);
			lbuf[llen++] = '\r';
			lbuf[llen++] = '\n';

			iov[0].iov_ptr = lbuf;
			iov[0].iov_len = llen;
			iov[1].iov_ptr = (void*)data;
			iov[1].iov_len = dlen;
			iov[2].iov_ptr = "\r\n";
			iov[2].iov_len = 2;

			if (thr_writev_to_client(thr, iov, HIO_COUNTOF(iov)) <= -1) goto oops;
			break;
		}

		case THR_RES_MODE_CLOSE:
		case THR_RES_MODE_LENGTH:
			if (thr_write_to_client(thr, data, dlen) <= -1) goto oops;
			break;
	}

	if (thr->num_pending_writes_to_client > THR_PENDING_IO_THRESHOLD)
	{
		if (hio_dev_thr_read(thr->peer, 0) <= -1) goto oops;
	}

	return 0;

oops:
	return -1;
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
	thr_task_t* thr = (thr_task_t*)cli->task;

	/* indicate EOF to the client peer */
	if (thr_write_to_peer(thr, HIO_NULL, 0) <= -1) return -1;

	thr_task_mark_over (thr, THR_OVER_READ_FROM_CLIENT);
	return 0;
}

static int thr_client_htrd_push_content (hio_htrd_t* htrd, hio_htre_t* req, const hio_bch_t* data, hio_oow_t dlen)
{
	hio_svc_htts_cli_htrd_xtn_t* htrdxtn = (hio_svc_htts_cli_htrd_xtn_t*)hio_htrd_getxtn(htrd);
	hio_dev_sck_t* sck = htrdxtn->sck;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	thr_task_t* thr = (thr_task_t*)cli->task;

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
	thr_peer_xtn_t* peer_xtn = (thr_peer_xtn_t*)hio_dev_thr_getxtn(peer);
	thr_task_t* thr = peer_xtn->task;

	if (!thr) return 0; /* there is nothing i can do. the thr_task is being cleared or has been cleared already. */

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
		thr_task_mark_over (thr, THR_OVER_WRITE_TO_PEER);
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
			thr_task_mark_over (thr, THR_OVER_WRITE_TO_PEER);
		}
	}

	return 0;

oops:
	thr_task_halt_participating_devices (thr);
	return 0;
}

static void thr_client_on_disconnect (hio_dev_sck_t* sck)
{
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	thr_task_t* thr = (thr_task_t*)cli->task;
	hio_svc_htts_t* htts = thr->htts;
	hio_t* hio = sck->hio;

	HIO_ASSERT (hio, sck = thr->task_csck);
	HIO_DEBUG4 (hio, "HTTS(%p) - thr(t=%p,c=%p,csck=%p) - client socket disconnect notified\n", htts, thr, cli, sck);

	thr->client_disconnected = 1;
	thr->task_csck = HIO_NULL;
	thr->task_client = HIO_NULL;
	if (thr->client_org_on_disconnect)
	{
		thr->client_org_on_disconnect (sck);
		/* this original callback destroys the associated resource.
		 * thr_task must not be accessed from here down */
	}

	HIO_DEBUG4 (hio, "HTTS(%p) - thr(t=%p,c=%p,csck=%p) - client socket disconnect handled\n", htts, thr, cli, sck);
	/* Note: after this callback, the actual device pointed to by 'sck' will be freed in the main loop. */
}

static int thr_client_on_read (hio_dev_sck_t* sck, const void* buf, hio_iolen_t len, const hio_skad_t* srcaddr)
{
	hio_t* hio = sck->hio;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	thr_task_t* thr = (thr_task_t*)cli->task;

	HIO_ASSERT (hio, sck == cli->sck);

	if (len <= -1)
	{
		/* read error */
		HIO_DEBUG2 (cli->htts->hio, "HTTPS(%p) - read error on client %p(%d)\n", sck, (int)sck->hnd);
		goto oops;
	}

	if (!thr->peer)
	{
		/* the peer is gone */
		goto oops; /* do what?  just return 0? */
	}

	if (len == 0)
	{
		/* EOF on the client side. arrange to close */
		HIO_DEBUG3 (hio, "HTTPS(%p) - EOF from client %p(hnd=%d)\n", thr->htts, sck, (int)sck->hnd);
		thr->client_eof_detected = 1;

		if (!(thr->over & THR_OVER_READ_FROM_CLIENT)) /* if this is true, EOF is received without thr_client_htrd_poke() */
		{
			int n;
			n = thr_write_to_peer(thr, HIO_NULL, 0);
			thr_task_mark_over (thr, THR_OVER_READ_FROM_CLIENT);
			if (n <= -1) goto oops;
		}
	}
	else
	{
		hio_oow_t rem;

		HIO_ASSERT (hio, !(thr->over & THR_OVER_READ_FROM_CLIENT));

		if (hio_htrd_feed(cli->htrd, buf, len, &rem) <= -1) goto oops;

		if (rem > 0)
		{
			/* TODO store this to client buffer. once the current resource is completed, arrange to call on_read() with it */
			HIO_DEBUG3 (hio, "HTTPS(%p) - excessive data after contents by thr client %p(%d)\n", sck->hio, sck, (int)sck->hnd);
		}
	}

	return 0;

oops:
	thr_task_halt_participating_devices (thr);
	return 0;
}

static int thr_client_on_write (hio_dev_sck_t* sck, hio_iolen_t wrlen, void* wrctx, const hio_skad_t* dstaddr)
{
	hio_t* hio = sck->hio;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	thr_task_t* thr = (thr_task_t*)cli->task;

	if (wrlen <= -1)
	{
		HIO_DEBUG3 (hio, "HTTPS(%p) - unable to write to client %p(%d)\n", sck->hio, sck, (int)sck->hnd);
		goto oops;
	}

	if (wrlen == 0)
	{
		/* if the connect is keep-alive, this part may not be called */
		thr->num_pending_writes_to_client--;
		HIO_ASSERT (hio, thr->num_pending_writes_to_client == 0);
		HIO_DEBUG3 (hio, "HTTS(%p) - indicated EOF to client %p(%d)\n", thr->htts, sck, (int)sck->hnd);
		/* since EOF has been indicated to the client, it must not write to the client any further.
		 * this also means that i don't need any data from the peer side either.
		 * i don't need to enable input watching on the peer side */
		thr_task_mark_over (thr, THR_OVER_WRITE_TO_CLIENT);
	}
	else
	{
		HIO_ASSERT (hio, thr->num_pending_writes_to_client > 0);

		thr->num_pending_writes_to_client--;
		if (thr->peer && thr->num_pending_writes_to_client == THR_PENDING_IO_THRESHOLD)
		{
			if (!(thr->over & THR_OVER_READ_FROM_PEER) &&
			    hio_dev_thr_read(thr->peer, 1) <= -1) goto oops;
		}

		if ((thr->over & THR_OVER_READ_FROM_PEER) && thr->num_pending_writes_to_client <= 0)
		{
			thr_task_mark_over (thr, THR_OVER_WRITE_TO_CLIENT);
		}
	}

	return 0;

oops:
	thr_task_halt_participating_devices (thr);
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

int hio_svc_htts_dothr (hio_svc_htts_t* htts, hio_dev_sck_t* csck, hio_htre_t* req, hio_svc_htts_thr_func_t func, void* ctx, int options, hio_svc_htts_task_on_kill_t on_kill)
{
	hio_t* hio = htts->hio;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(csck);
	thr_task_t* thr = HIO_NULL;
	thr_peer_xtn_t* peer_xtn;
	hio_dev_thr_make_t mi;
	thr_func_start_t* tfs;
	int have_content;

	/* ensure that you call this function before any contents is received */
	HIO_ASSERT (hio, hio_htre_getcontentlen(req) == 0);

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

	tfs->tfi.server_addr = cli->sck->localaddr;
	tfs->tfi.client_addr = cli->sck->remoteaddr;

	HIO_MEMSET (&mi, 0, HIO_SIZEOF(mi));
	mi.thr_func = thr_func;
	mi.thr_ctx = tfs;
	mi.on_read = thr_peer_on_read;
	mi.on_write = thr_peer_on_write;
	mi.on_close = thr_peer_on_close;

	thr = (thr_task_t*)hio_svc_htts_task_make(htts, HIO_SIZEOF(*thr), thr_task_on_kill, req, csck);
	if (HIO_UNLIKELY(!thr)) goto oops;

	thr->on_kill = on_kill;
	thr->options = options;

	thr->client_org_on_read = csck->on_read;
	thr->client_org_on_write = csck->on_write;
	thr->client_org_on_disconnect = csck->on_disconnect;
	csck->on_read = thr_client_on_read;
	csck->on_write = thr_client_on_write;
	csck->on_disconnect = thr_client_on_disconnect;

	/* attach the thr task to the client socket via the task field in the extended space of the socket */
	HIO_ASSERT (hio, cli->task == HIO_NULL);
	HIO_SVC_HTTS_TASK_REF ((hio_svc_htts_task_t*)thr, cli->task);

	thr->peer = hio_dev_thr_make(hio, HIO_SIZEOF(*peer_xtn), &mi);
	if (HIO_UNLIKELY(!thr->peer))
	{
		/* no need to detach the attached task here because that is handled
		 * in the kill/disconnect callbacks of relevant devices */
		HIO_DEBUG3 (hio, "HTTS(%p) - failed to create thread for %p(%d)\n", htts, csck, (int)csck->hnd);
		goto oops;
	}

	tfs = HIO_NULL; /* mark that tfs is delegated to the thread */

	/* attach the thr task to the peer thread device */
	peer_xtn = hio_dev_thr_getxtn(thr->peer);
	HIO_SVC_HTTS_TASK_REF (thr, peer_xtn->task);

	thr->peer_htrd = hio_htrd_open(hio, HIO_SIZEOF(*peer_xtn));
	if (HIO_UNLIKELY(!thr->peer_htrd)) goto oops;
	hio_htrd_setoption (thr->peer_htrd, HIO_HTRD_SKIP_INITIAL_LINE | HIO_HTRD_RESPONSE);
	hio_htrd_setrecbs (thr->peer_htrd, &thr_peer_htrd_recbs);

	/* attach the thr task to the htrd parser set on the peer thread device */
	peer_xtn = hio_htrd_getxtn(thr->peer_htrd);
	HIO_SVC_HTTS_TASK_REF (thr, peer_xtn->task);

#if !defined(THR_ALLOW_UNLIMITED_REQ_CONTENT_LENGTH)
	if (thr->task_req_conlen_unlimited)
	{
		/* Transfer-Encoding is chunked. no content-length is known in advance. */

		/* option 1. buffer contents. if it gets too large, send 413 Request Entity Too Large.
		 * option 2. send 411 Length Required immediately
		 * option 3. set Content-Length to -1 and use EOF to indicate the end of content [Non-Standard] */

		if (thr_send_final_status_to_client(thr, HIO_HTTP_STATUS_LENGTH_REQUIRED, 1) <= -1) goto oops;
	}
#endif

	if (req->flags & HIO_HTRE_ATTR_EXPECT100)
	{
		/* TODO: Expect: 100-continue? who should handle this? thr? or the http server? */
		/* CAN I LET the thr SCRIPT handle this? */
		if (hio_comp_http_version_numbers(&req->version, 1, 1) >= 0 &&
		   (thr->task_req_conlen_unlimited || thr->task_req_conlen > 0))
		{
			/*
			 * Don't send 100 Continue if http verions is lower than 1.1
			 * [RFC7231]
			 *  A server that receives a 100-continue expectation in an HTTP/1.0
			 *  request MUST ignore that expectation.
			 *
			 * Don't send 100 Continue if expected content lenth is 0.
			 * [RFC7231]
			 *  A server MAY omit sending a 100 (Continue) response if it has
			 *  already received some or all of the message body for the
			 *  corresponding request, or if the framing indicates that there is
			 *  no message body.
			 */
			hio_bch_t msgbuf[64];
			hio_oow_t msglen;

			msglen = hio_fmttobcstr(hio, msgbuf, HIO_COUNTOF(msgbuf), "HTTP/%d.%d %d %hs\r\n\r\n", thr->task_req_version.major, thr->task_req_version.minor, HIO_HTTP_STATUS_CONTINUE, hio_http_status_to_bcstr(HIO_HTTP_STATUS_CONTINUE));
			if (thr_write_to_client(thr, msgbuf, msglen) <= -1) goto oops;
			thr->ever_attempted_to_write_to_client = 0; /* reset this as it's polluted for 100 continue */
		}
	}
	else if (req->flags & HIO_HTRE_ATTR_EXPECT)
	{
		/* 417 Expectation Failed */
		thr_send_final_status_to_client(thr, HIO_HTTP_STATUS_EXPECTATION_FAILED, 1);
		goto oops;
	}

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
		if (thr_write_to_peer(thr, HIO_NULL, 0) <= -1) goto oops;
		thr_task_mark_over (thr, THR_OVER_READ_FROM_CLIENT | THR_OVER_WRITE_TO_PEER);
	}

	thr->res_mode_to_cli = thr->task_keep_client_alive? THR_RES_MODE_CHUNKED: THR_RES_MODE_CLOSE;
	/* the mode still can get switched from THR_RES_MODE_CHUNKED to THR_RES_MODE_LENGTH 
	   if the thread function emits Content-Length */

	/* TODO: store current input watching state and use it when destroying the thr_task data */
	if (hio_dev_sck_read(csck, !(thr->over & THR_OVER_READ_FROM_CLIENT)) <= -1) goto oops;

	HIO_SVC_HTTS_TASKL_APPEND_TASK (&htts->task, (hio_svc_htts_task_t*)thr);
	return 0;

oops:
	HIO_DEBUG2 (hio, "HTTS(%p) - FAILURE in dothr - socket(%p)\n", htts, csck);
	if (tfs) free_thr_start_info (tfs);
	if (thr) thr_task_halt_participating_devices (thr);
	return -1;
}
