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

enum thr_state_res_mode_t
{
	THR_STATE_RES_MODE_CHUNKED,
	THR_STATE_RES_MODE_CLOSE,
	THR_STATE_RES_MODE_LENGTH
};
typedef enum thr_state_res_mode_t thr_state_res_mode_t;

#define THR_STATE_PENDING_IO_THRESHOLD 5

#define THR_STATE_OVER_READ_FROM_CLIENT (1 << 0)
#define THR_STATE_OVER_READ_FROM_PEER   (1 << 1)
#define THR_STATE_OVER_WRITE_TO_CLIENT  (1 << 2)
#define THR_STATE_OVER_WRITE_TO_PEER    (1 << 3)
#define THR_STATE_OVER_ALL (THR_STATE_OVER_READ_FROM_CLIENT | THR_STATE_OVER_READ_FROM_PEER | THR_STATE_OVER_WRITE_TO_CLIENT | THR_STATE_OVER_WRITE_TO_PEER)

struct thr_func_start_t
{
	hio_t* hio;
	hio_svc_htts_thr_func_t thr_func;
	void* thr_ctx;
	hio_svc_htts_thr_func_info_t tfi;
};
typedef struct thr_func_start_t thr_func_start_t;

struct thr_state_t
{
	HIO_SVC_HTTS_RSRC_HEADER;

	hio_oow_t num_pending_writes_to_client;
	hio_oow_t num_pending_writes_to_peer;
	hio_dev_thr_t* peer;
	hio_htrd_t* peer_htrd;
	hio_svc_htts_cli_t* client;
	hio_http_version_t req_version; /* client request */

	unsigned int over: 4; /* must be large enough to accomodate THR_STATE_OVER_ALL */
	unsigned int keep_alive: 1;
	unsigned int req_content_length_unlimited: 1;
	unsigned int ever_attempted_to_write_to_client: 1;
	unsigned int client_disconnected: 1;
	unsigned int client_htrd_recbs_changed: 1;
	hio_oow_t req_content_length; /* client request content length */
	thr_state_res_mode_t res_mode_to_cli;

	hio_dev_sck_on_read_t client_org_on_read;
	hio_dev_sck_on_write_t client_org_on_write;
	hio_dev_sck_on_disconnect_t client_org_on_disconnect;
	hio_htrd_recbs_t client_htrd_org_recbs;
};

typedef struct thr_state_t thr_state_t;

struct thr_peer_xtn_t
{
	thr_state_t* state;
};
typedef struct thr_peer_xtn_t thr_peer_xtn_t;

static void thr_state_halt_participating_devices (thr_state_t* thr_state)
{
	HIO_ASSERT (thr_state->client->htts->hio, thr_state->client != HIO_NULL);
	HIO_ASSERT (thr_state->client->htts->hio, thr_state->client->sck != HIO_NULL);

	HIO_DEBUG4 (thr_state->client->htts->hio, "HTTS(%p) - Halting participating devices in thr state %p(client=%p,peer=%p)\n", thr_state->client->htts, thr_state, thr_state->client->sck, thr_state->peer);

	hio_dev_sck_halt (thr_state->client->sck);
	/* check for peer as it may not have been started */
	if (thr_state->peer) hio_dev_thr_halt (thr_state->peer);
}

static int thr_state_write_to_client (thr_state_t* thr_state, const void* data, hio_iolen_t dlen)
{
	thr_state->ever_attempted_to_write_to_client = 1;

	thr_state->num_pending_writes_to_client++;
	if (hio_dev_sck_write(thr_state->client->sck, data, dlen, HIO_NULL, HIO_NULL) <= -1) 
	{
		thr_state->num_pending_writes_to_client--;
		return -1;
	}

	if (thr_state->num_pending_writes_to_client > THR_STATE_PENDING_IO_THRESHOLD)
	{
		if (hio_dev_thr_read(thr_state->peer, 0) <= -1) return -1;
	}
	return 0;
}

static int thr_state_writev_to_client (thr_state_t* thr_state, hio_iovec_t* iov, hio_iolen_t iovcnt)
{
	thr_state->ever_attempted_to_write_to_client = 1;

	thr_state->num_pending_writes_to_client++;
	if (hio_dev_sck_writev(thr_state->client->sck, iov, iovcnt, HIO_NULL, HIO_NULL) <= -1) 
	{
		thr_state->num_pending_writes_to_client--;
		return -1;
	}

	if (thr_state->num_pending_writes_to_client > THR_STATE_PENDING_IO_THRESHOLD)
	{
		if (hio_dev_thr_read(thr_state->peer, 0) <= -1) return -1;
	}
	return 0;
}

static int thr_state_send_final_status_to_client (thr_state_t* thr_state, int status_code, int force_close)
{
	hio_svc_htts_cli_t* cli = thr_state->client;
	hio_bch_t dtbuf[64];

	hio_svc_htts_fmtgmtime (cli->htts, HIO_NULL, dtbuf, HIO_COUNTOF(dtbuf));

	if (!force_close) force_close = !thr_state->keep_alive;
	if (hio_becs_fmt(cli->sbuf, "HTTP/%d.%d %d %hs\r\nServer: %hs\r\nDate: %s\r\nConnection: %hs\r\nContent-Length: 0\r\n\r\n",
		thr_state->req_version.major, thr_state->req_version.minor,
		status_code, hio_http_status_to_bcstr(status_code),
		cli->htts->server_name, dtbuf,
		(force_close? "close": "keep-alive")) == (hio_oow_t)-1) return -1;

	return (thr_state_write_to_client(thr_state, HIO_BECS_PTR(cli->sbuf), HIO_BECS_LEN(cli->sbuf)) <= -1 ||
	        (force_close && thr_state_write_to_client(thr_state, HIO_NULL, 0) <= -1))? -1: 0;
}


static int thr_state_write_last_chunk_to_client (thr_state_t* thr_state)
{
	if (!thr_state->ever_attempted_to_write_to_client)
	{
		if (thr_state_send_final_status_to_client(thr_state, 500, 0) <= -1) return -1;
	}
	else
	{
		if (thr_state->res_mode_to_cli == THR_STATE_RES_MODE_CHUNKED &&
		    thr_state_write_to_client(thr_state, "0\r\n\r\n", 5) <= -1) return -1;
	}

	if (!thr_state->keep_alive && thr_state_write_to_client(thr_state, HIO_NULL, 0) <= -1) return -1;
	return 0;
}

static int thr_state_write_to_peer (thr_state_t* thr_state, const void* data, hio_iolen_t dlen)
{
	thr_state->num_pending_writes_to_peer++;
	if (hio_dev_thr_write(thr_state->peer, data, dlen, HIO_NULL) <= -1) 
	{
		thr_state->num_pending_writes_to_peer--;
		return -1;
	}

/* TODO: check if it's already finished or something.. */
	if (thr_state->num_pending_writes_to_peer > THR_STATE_PENDING_IO_THRESHOLD)
	{
		if (hio_dev_sck_read(thr_state->client->sck, 0) <= -1) return -1;
	}
	return 0;
}

static HIO_INLINE void thr_state_mark_over (thr_state_t* thr_state, int over_bits)
{
	unsigned int old_over;

	old_over = thr_state->over;
	thr_state->over |= over_bits;

	HIO_DEBUG5 (thr_state->htts->hio, "HTTS(%p) - client=%p peer=%p new-bits=%x over=%x\n", thr_state->htts, thr_state->client->sck, thr_state->peer, (int)over_bits, (int)thr_state->over);

	if (!(old_over & THR_STATE_OVER_READ_FROM_CLIENT) && (thr_state->over & THR_STATE_OVER_READ_FROM_CLIENT))
	{
		if (hio_dev_sck_read(thr_state->client->sck, 0) <= -1) 
		{
			HIO_DEBUG2 (thr_state->htts->hio, "HTTS(%p) - halting client(%p) for failure to disable input watching\n", thr_state->htts, thr_state->client->sck);
			hio_dev_sck_halt (thr_state->client->sck);
		}
	}

	if (!(old_over & THR_STATE_OVER_READ_FROM_PEER) && (thr_state->over & THR_STATE_OVER_READ_FROM_PEER))
	{
		if (thr_state->peer && hio_dev_thr_read(thr_state->peer, 0) <= -1) 
		{
			HIO_DEBUG2 (thr_state->htts->hio, "HTTS(%p) - halting peer(%p) for failure to disable input watching\n", thr_state->htts, thr_state->peer);
			hio_dev_thr_halt (thr_state->peer);
		}
	}

	if (old_over != THR_STATE_OVER_ALL && thr_state->over == THR_STATE_OVER_ALL)
	{
		/* ready to stop */
		if (thr_state->peer) 
		{
			HIO_DEBUG2 (thr_state->htts->hio, "HTTS(%p) - halting peer(%p) as it is unneeded\n", thr_state->htts, thr_state->peer);
			hio_dev_thr_halt (thr_state->peer);
		}

		if (thr_state->keep_alive) 
		{
			/* how to arrange to delete this thr_state object and put the socket back to the normal waiting state??? */
			HIO_ASSERT (thr_state->htts->hio, thr_state->client->rsrc == (hio_svc_htts_rsrc_t*)thr_state);

			HIO_SVC_HTTS_RSRC_DETACH (thr_state->client->rsrc);
			/* thr_state must not be access from here down as it could have been destroyed */
		}
		else
		{
			HIO_DEBUG2 (thr_state->htts->hio, "HTTS(%p) - halting client(%p) for no keep-alive\n", thr_state->htts, thr_state->client->sck);
			hio_dev_sck_shutdown (thr_state->client->sck, HIO_DEV_SCK_SHUTDOWN_WRITE);
			hio_dev_sck_halt (thr_state->client->sck);
		}
	}
}

static void thr_state_on_kill (thr_state_t* thr_state)
{
	hio_t* hio = thr_state->htts->hio;

	HIO_DEBUG2 (hio, "HTTS(%p) - killing thr_state client(%p)\n", thr_state->htts, thr_state->client->sck);

	if (thr_state->peer)
	{
		thr_peer_xtn_t* thr_peer = hio_dev_thr_getxtn(thr_state->peer);
		thr_peer->state = HIO_NULL;  /* thr_peer->state many not be NULL if the resource is killed regardless of the reference count */

		hio_dev_thr_kill (thr_state->peer);
		thr_state->peer = HIO_NULL;
	}

	if (thr_state->peer_htrd)
	{
		thr_peer_xtn_t* thr_peer = hio_htrd_getxtn(thr_state->peer_htrd);
		thr_peer->state = HIO_NULL; /* thr_peer->state many not be NULL if the resource is killed regardless of the reference count */

		hio_htrd_close (thr_state->peer_htrd);
		thr_state->peer_htrd = HIO_NULL;
	}

	if (thr_state->client_org_on_read)
	{
		thr_state->client->sck->on_read = thr_state->client_org_on_read;
		thr_state->client_org_on_read = HIO_NULL;
	}

	if (thr_state->client_org_on_write)
	{
		thr_state->client->sck->on_write = thr_state->client_org_on_write;
		thr_state->client_org_on_write = HIO_NULL;
	}


	if (thr_state->client_org_on_disconnect)
	{
		thr_state->client->sck->on_disconnect = thr_state->client_org_on_disconnect;
		thr_state->client_org_on_disconnect = HIO_NULL;
	}

	if (thr_state->client_htrd_recbs_changed)
	{
		/* restore the callbacks */
		hio_htrd_setrecbs (thr_state->client->htrd, &thr_state->client_htrd_org_recbs); 
	}

	if (!thr_state->client_disconnected)
	{
/*printf ("ENABLING INPUT WATCHING on CLIENT %p. \n", thr_state->client->sck);*/
		if (!thr_state->keep_alive || hio_dev_sck_read(thr_state->client->sck, 1) <= -1)
		{
			HIO_DEBUG2 (hio, "HTTS(%p) - halting client(%p) for failure to enable input watching\n", thr_state->htts, thr_state->client->sck);
			hio_dev_sck_halt (thr_state->client->sck);
		}
	}

/*printf ("**** THR_STATE_ON_KILL DONE\n");*/
}

static void thr_peer_on_close (hio_dev_thr_t* thr, hio_dev_thr_sid_t sid)
{
	hio_t* hio = thr->hio;
	thr_peer_xtn_t* thr_peer = (thr_peer_xtn_t*)hio_dev_thr_getxtn(thr);
	thr_state_t* thr_state = thr_peer->state;

	if (!thr_state) return; /* thr state already gone */

	switch (sid)
	{
		case HIO_DEV_THR_MASTER:
			HIO_DEBUG2 (hio, "HTTS(%p) - peer %p closing master\n", thr_state->client->htts, thr);
			thr_state->peer = HIO_NULL; /* clear this peer from the state */

			HIO_ASSERT (hio, thr_peer->state != HIO_NULL);
			HIO_SVC_HTTS_RSRC_DETACH (thr_peer->state);

			if (thr_state->peer_htrd)
			{
				/* once this peer device is closed, peer's htrd is also never used.
				 * it's safe to detach the extra information attached on the htrd object. */
				thr_peer = hio_htrd_getxtn(thr_state->peer_htrd);
				HIO_ASSERT (hio, thr_peer->state != HIO_NULL);
				HIO_SVC_HTTS_RSRC_DETACH (thr_peer->state);
			}

			break;

		case HIO_DEV_THR_OUT:
			HIO_ASSERT (hio, thr_state->peer == thr);
			HIO_DEBUG3 (hio, "HTTS(%p) - peer %p closing slave[%d]\n", thr_state->client->htts, thr, sid);

			if (!(thr_state->over & THR_STATE_OVER_READ_FROM_PEER))
			{
				if (thr_state_write_last_chunk_to_client(thr_state) <= -1) 
					thr_state_halt_participating_devices (thr_state);
				else
					thr_state_mark_over (thr_state, THR_STATE_OVER_READ_FROM_PEER);
			}
			break;

		case HIO_DEV_THR_IN:
			thr_state_mark_over (thr_state, THR_STATE_OVER_WRITE_TO_PEER);
			break;

		default:
			HIO_DEBUG3 (hio, "HTTS(%p) - peer %p closing slave[%d]\n", thr_state->client->htts, thr, sid);
			/* do nothing */
			break;
	}
}

static int thr_peer_on_read (hio_dev_thr_t* thr, const void* data, hio_iolen_t dlen)
{
	hio_t* hio = thr->hio;
	thr_peer_xtn_t* thr_peer = (thr_peer_xtn_t*)hio_dev_thr_getxtn(thr);
	thr_state_t* thr_state = thr_peer->state;

	HIO_ASSERT (hio, thr_state != HIO_NULL);

	if (dlen <= -1)
	{
		HIO_DEBUG2 (hio, "HTTPS(%p) - read error from peer %p\n", thr_state->client->htts, thr);
		goto oops;
	}

	if (dlen == 0)
	{
		HIO_DEBUG2 (hio, "HTTPS(%p) - EOF from peer %p\n", thr_state->client->htts, thr);

		if (!(thr_state->over & THR_STATE_OVER_READ_FROM_PEER))
		{
			/* the thr script could be misbehaviing.
			 * it still has to read more but EOF is read.
			 * otherwise client_peer_htrd_poke() should have been called */
			if (thr_state_write_last_chunk_to_client(thr_state) <= -1) goto oops;
			thr_state_mark_over (thr_state, THR_STATE_OVER_READ_FROM_PEER);
		}
	}
	else
	{
		hio_oow_t rem;

		HIO_ASSERT (hio, !(thr_state->over & THR_STATE_OVER_READ_FROM_PEER));

		if (hio_htrd_feed(thr_state->peer_htrd, data, dlen, &rem) <= -1) 
		{
			HIO_DEBUG2 (hio, "HTTPS(%p) - unable to feed peer htrd - peer %p\n", thr_state->htts, thr);

			if (!thr_state->ever_attempted_to_write_to_client &&
			    !(thr_state->over & THR_STATE_OVER_WRITE_TO_CLIENT))
			{
				thr_state_send_final_status_to_client (thr_state, 500, 1); /* don't care about error because it jumps to oops below anyway */
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
	thr_state_halt_participating_devices (thr_state);
	return 0;
}

static int thr_peer_capture_response_header (hio_htre_t* req, const hio_bch_t* key, const hio_htre_hdrval_t* val, void* ctx)
{
	hio_svc_htts_cli_t* cli = (hio_svc_htts_cli_t*)ctx;

	/* capture a header except Status, Connection, Transfer-Encoding, and Server */
	if (hio_comp_bcstr(key, "Status", 1) != 0 &&
	    hio_comp_bcstr(key, "Connection", 1) != 0 &&
	    hio_comp_bcstr(key, "Transfer-Encoding", 1) != 0 &&
	    hio_comp_bcstr(key, "Server", 1) != 0 &&
	    hio_comp_bcstr(key, "Date", 1) != 0)
	{
		do
		{
			if (hio_becs_cat(cli->sbuf, key) == (hio_oow_t)-1 ||
			    hio_becs_cat(cli->sbuf, ": ") == (hio_oow_t)-1 ||
			    hio_becs_cat(cli->sbuf, val->ptr) == (hio_oow_t)-1 ||
			    hio_becs_cat(cli->sbuf, "\r\n") == (hio_oow_t)-1)
			{
				return -1;
			}

			val = val->next;
		}
		while (val);
	}

	return 0;
}

static int thr_peer_htrd_peek (hio_htrd_t* htrd, hio_htre_t* req)
{
	thr_peer_xtn_t* thr_peer = hio_htrd_getxtn(htrd);
	thr_state_t* thr_state = thr_peer->state;
	hio_svc_htts_cli_t* cli = thr_state->client;
	hio_bch_t dtbuf[64];
	int status_code = 200;

	if (req->attr.content_length)
	{
// TOOD: remove content_length if content_length is negative or not numeric.
		thr_state->res_mode_to_cli = THR_STATE_RES_MODE_LENGTH;
	}

	if (req->attr.status)
	{
		int is_sober;
		const hio_bch_t* endptr;
		hio_intmax_t v;

		v = hio_bchars_to_intmax(req->attr.status, hio_count_bcstr(req->attr.status), HIO_BCHARS_TO_INTMAX_MAKE_OPTION(0,0,0,10), &endptr, &is_sober);
		if (*endptr == '\0' && is_sober && v > 0  && v <= HIO_TYPE_MAX(int)) status_code = v;
	}

	hio_svc_htts_fmtgmtime (cli->htts, HIO_NULL, dtbuf, HIO_COUNTOF(dtbuf));

	if (hio_becs_fmt(cli->sbuf, "HTTP/%d.%d %d %hs\r\nServer: %hs\r\nDate: %hs\r\n",
		thr_state->req_version.major, thr_state->req_version.minor,
		status_code, hio_http_status_to_bcstr(status_code),
		cli->htts->server_name, dtbuf) == (hio_oow_t)-1) return -1;

	if (hio_htre_walkheaders(req, thr_peer_capture_response_header, cli) <= -1) return -1;

	switch (thr_state->res_mode_to_cli)
	{
		case THR_STATE_RES_MODE_CHUNKED:
			if (hio_becs_cat(cli->sbuf, "Transfer-Encoding: chunked\r\n") == (hio_oow_t)-1) return -1;
			/*if (hio_becs_cat(cli->sbuf, "Connection: keep-alive\r\n") == (hio_oow_t)-1) return -1;*/
			break;

		case THR_STATE_RES_MODE_CLOSE:
			if (hio_becs_cat(cli->sbuf, "Connection: close\r\n") == (hio_oow_t)-1) return -1;
			break;

		case THR_STATE_RES_MODE_LENGTH:
			if (hio_becs_cat(cli->sbuf, (thr_state->keep_alive? "Connection: keep-alive\r\n": "Connection: close\r\n")) == (hio_oow_t)-1) return -1;
	}

	if (hio_becs_cat(cli->sbuf, "\r\n") == (hio_oow_t)-1) return -1;

	return thr_state_write_to_client(thr_state, HIO_BECS_PTR(cli->sbuf), HIO_BECS_LEN(cli->sbuf));
}

static int thr_peer_htrd_poke (hio_htrd_t* htrd, hio_htre_t* req)
{
	/* client request got completed */
	thr_peer_xtn_t* thr_peer = hio_htrd_getxtn(htrd);
	thr_state_t* thr_state = thr_peer->state;

	if (thr_state_write_last_chunk_to_client(thr_state) <= -1) return -1;

	thr_state_mark_over (thr_state, THR_STATE_OVER_READ_FROM_PEER);
	return 0;
}

static int thr_peer_htrd_push_content (hio_htrd_t* htrd, hio_htre_t* req, const hio_bch_t* data, hio_oow_t dlen)
{
	thr_peer_xtn_t* thr_peer = hio_htrd_getxtn(htrd);
	thr_state_t* thr_state = thr_peer->state;

	HIO_ASSERT (thr_state->client->htts->hio, htrd == thr_state->peer_htrd);

	switch (thr_state->res_mode_to_cli)
	{
		case THR_STATE_RES_MODE_CHUNKED:
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

			if (thr_state_writev_to_client(thr_state, iov, HIO_COUNTOF(iov)) <= -1) 
			{
				goto oops;
			}
			break;
		}

		case THR_STATE_RES_MODE_CLOSE:
		case THR_STATE_RES_MODE_LENGTH:
			if (thr_state_write_to_client(thr_state, data, dlen) <= -1) 
			{
				goto oops;
			}
			break;
	}

	if (thr_state->num_pending_writes_to_client > THR_STATE_PENDING_IO_THRESHOLD)
	{
		if (hio_dev_thr_read(thr_state->peer, 0) <= -1) 
		{
			goto oops;
		}
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
	thr_state_t* thr_state = (thr_state_t*)cli->rsrc;

	/* indicate EOF to the client peer */
	if (thr_state_write_to_peer(thr_state, HIO_NULL, 0) <= -1) return -1;

	thr_state_mark_over (thr_state, THR_STATE_OVER_READ_FROM_CLIENT);
	return 0;
}

static int thr_client_htrd_push_content (hio_htrd_t* htrd, hio_htre_t* req, const hio_bch_t* data, hio_oow_t dlen)
{
	hio_svc_htts_cli_htrd_xtn_t* htrdxtn = (hio_svc_htts_cli_htrd_xtn_t*)hio_htrd_getxtn(htrd);
	hio_dev_sck_t* sck = htrdxtn->sck;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	thr_state_t* thr_state = (thr_state_t*)cli->rsrc;

	HIO_ASSERT (sck->hio, cli->sck == sck);
	return thr_state_write_to_peer(thr_state, data, dlen);
}

static hio_htrd_recbs_t thr_client_htrd_recbs =
{
	HIO_NULL,
	thr_client_htrd_poke,
	thr_client_htrd_push_content
};

static int thr_peer_on_write (hio_dev_thr_t* thr, hio_iolen_t wrlen, void* wrctx)
{
	hio_t* hio = thr->hio;
	thr_peer_xtn_t* thr_peer = (thr_peer_xtn_t*)hio_dev_thr_getxtn(thr);
	thr_state_t* thr_state = thr_peer->state;

	if (thr_state == HIO_NULL) return 0; /* there is nothing i can do. the thr_state is being cleared or has been cleared already. */

	HIO_ASSERT (hio, thr_state->peer == thr);

	if (wrlen <= -1)
	{
		HIO_DEBUG2 (hio, "HTTS(%p) - unable to write to peer %p\n", thr_state->client->htts, thr);
		goto oops;
	}
	else if (wrlen == 0)
	{
		/* indicated EOF */
		/* do nothing here as i didn't incremented num_pending_writes_to_peer when making the write request */

		thr_state->num_pending_writes_to_peer--;
		HIO_ASSERT (hio, thr_state->num_pending_writes_to_peer == 0);
		HIO_DEBUG2 (hio, "HTTS(%p) - indicated EOF to peer %p\n", thr_state->client->htts, thr);
		/* indicated EOF to the peer side. i need no more data from the client side.
		 * i don't need to enable input watching in the client side either */
		thr_state_mark_over (thr_state, THR_STATE_OVER_WRITE_TO_PEER);
	}
	else
	{
		HIO_ASSERT (hio, thr_state->num_pending_writes_to_peer > 0);

		thr_state->num_pending_writes_to_peer--;
		if (thr_state->num_pending_writes_to_peer == THR_STATE_PENDING_IO_THRESHOLD)
		{
			if (!(thr_state->over & THR_STATE_OVER_READ_FROM_CLIENT) &&
			    hio_dev_sck_read(thr_state->client->sck, 1) <= -1) goto oops;
		}

		if ((thr_state->over & THR_STATE_OVER_READ_FROM_CLIENT) && thr_state->num_pending_writes_to_peer <= 0)
		{
			thr_state_mark_over (thr_state, THR_STATE_OVER_WRITE_TO_PEER);
		}
	}

	return 0;

oops:
	thr_state_halt_participating_devices (thr_state);
	return 0;
}

static void thr_client_on_disconnect (hio_dev_sck_t* sck)
{
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	thr_state_t* thr_state = (thr_state_t*)cli->rsrc;
	thr_state->client_disconnected = 1;
	thr_state->client_org_on_disconnect (sck);
}

static int thr_client_on_read (hio_dev_sck_t* sck, const void* buf, hio_iolen_t len, const hio_skad_t* srcaddr)
{
	hio_t* hio = sck->hio;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	thr_state_t* thr_state = (thr_state_t*)cli->rsrc;

	HIO_ASSERT (hio, sck == cli->sck);

	if (len <= -1)
	{
		/* read error */
		HIO_DEBUG2 (cli->htts->hio, "HTTPS(%p) - read error on client %p(%d)\n", sck, (int)sck->hnd);
		goto oops;
	}

	if (!thr_state->peer)
	{
		/* the peer is gone */
		goto oops; /* do what?  just return 0? */
	}

	if (len == 0)
	{
		/* EOF on the client side. arrange to close */
		HIO_DEBUG3 (hio, "HTTPS(%p) - EOF from client %p(hnd=%d)\n", thr_state->client->htts, sck, (int)sck->hnd);

		if (!(thr_state->over & THR_STATE_OVER_READ_FROM_CLIENT)) /* if this is true, EOF is received without thr_client_htrd_poke() */
		{
			if (thr_state_write_to_peer(thr_state, HIO_NULL, 0) <= -1) goto oops;
			thr_state_mark_over (thr_state, THR_STATE_OVER_READ_FROM_CLIENT);
		}
	}
	else
	{
		hio_oow_t rem;

		HIO_ASSERT (hio, !(thr_state->over & THR_STATE_OVER_READ_FROM_CLIENT));

		if (hio_htrd_feed(cli->htrd, buf, len, &rem) <= -1) goto oops;

		if (rem > 0)
		{
			/* TODO store this to client buffer. once the current resource is completed, arrange to call on_read() with it */
			HIO_DEBUG3 (hio, "HTTPS(%p) - excessive data after contents by thr client %p(%d)\n", sck->hio, sck, (int)sck->hnd);
		}
	}

	return 0;

oops:
	thr_state_halt_participating_devices (thr_state);
	return 0;
}

static int thr_client_on_write (hio_dev_sck_t* sck, hio_iolen_t wrlen, void* wrctx, const hio_skad_t* dstaddr)
{
	hio_t* hio = sck->hio;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	thr_state_t* thr_state = (thr_state_t*)cli->rsrc;

	if (wrlen <= -1)
	{
		HIO_DEBUG3 (hio, "HTTPS(%p) - unable to write to client %p(%d)\n", sck->hio, sck, (int)sck->hnd);
		goto oops;
	}

	if (wrlen == 0)
	{
		/* if the connect is keep-alive, this part may not be called */
		thr_state->num_pending_writes_to_client--;
		HIO_ASSERT (hio, thr_state->num_pending_writes_to_client == 0);
		HIO_DEBUG3 (hio, "HTTS(%p) - indicated EOF to client %p(%d)\n", thr_state->client->htts, sck, (int)sck->hnd);
		/* since EOF has been indicated to the client, it must not write to the client any further.
		 * this also means that i don't need any data from the peer side either.
		 * i don't need to enable input watching on the peer side */
		thr_state_mark_over (thr_state, THR_STATE_OVER_WRITE_TO_CLIENT);
	}
	else
	{
		HIO_ASSERT (hio, thr_state->num_pending_writes_to_client > 0);

		thr_state->num_pending_writes_to_client--;
		if (thr_state->peer && thr_state->num_pending_writes_to_client == THR_STATE_PENDING_IO_THRESHOLD)
		{
			if (!(thr_state->over & THR_STATE_OVER_READ_FROM_PEER) &&
			    hio_dev_thr_read(thr_state->peer, 1) <= -1) goto oops;
		}

		if ((thr_state->over & THR_STATE_OVER_READ_FROM_PEER) && thr_state->num_pending_writes_to_client <= 0)
		{
			thr_state_mark_over (thr_state, THR_STATE_OVER_WRITE_TO_CLIENT);
		}
	}

	return 0;

oops:
	thr_state_halt_participating_devices (thr_state);
	return 0;
}

static void free_thr_start_info (void* ctx)
{
	thr_func_start_t* tfs = (thr_func_start_t*)ctx;
	if (tfs->tfi.req_path) hio_freemem (tfs->hio, tfs->tfi.req_path);
	if (tfs->tfi.req_param) hio_freemem (tfs->hio, tfs->tfi.req_param);
	hio_freemem (tfs->hio, tfs);
}

static void thr_func (hio_t* hio, hio_dev_thr_iopair_t* iop, void* ctx)
{
	thr_func_start_t* tfs = (thr_func_start_t*)ctx;
	pthread_cleanup_push (free_thr_start_info, tfs);
	tfs->thr_func (hio, iop, &tfs->tfi, tfs->thr_ctx);
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

int hio_svc_htts_dothr (hio_svc_htts_t* htts, hio_dev_sck_t* csck, hio_htre_t* req, hio_svc_htts_thr_func_t func, void* ctx)
{
	hio_t* hio = htts->hio;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(csck);
	thr_state_t* thr_state = HIO_NULL;
	thr_peer_xtn_t* thr_peer;
	hio_dev_thr_make_t mi;
	thr_func_start_t* tfs;

	/* ensure that you call this function before any contents is received */
	HIO_ASSERT (hio, hio_htre_getcontentlen(req) == 0);

	tfs = hio_callocmem(hio, HIO_SIZEOF(*tfs));
	if (!tfs) goto oops;

	tfs->hio = hio;
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

	thr_state = (thr_state_t*)hio_svc_htts_rsrc_make(htts, HIO_SIZEOF(*thr_state), thr_state_on_kill);
	if (HIO_UNLIKELY(!thr_state)) goto oops;

	thr_state->client = cli;
	/*thr_state->num_pending_writes_to_client = 0;
	thr_state->num_pending_writes_to_peer = 0;*/
	thr_state->req_version = *hio_htre_getversion(req);
	thr_state->req_content_length_unlimited = hio_htre_getreqcontentlen(req, &thr_state->req_content_length);

	thr_state->client_org_on_read = csck->on_read;
	thr_state->client_org_on_write = csck->on_write;
	thr_state->client_org_on_disconnect = csck->on_disconnect;
	csck->on_read = thr_client_on_read;
	csck->on_write = thr_client_on_write;
	csck->on_disconnect = thr_client_on_disconnect;

	HIO_ASSERT (hio, cli->rsrc == HIO_NULL);
	HIO_SVC_HTTS_RSRC_ATTACH (thr_state, cli->rsrc);

	thr_state->peer = hio_dev_thr_make(hio, HIO_SIZEOF(*thr_peer), &mi);
	if (HIO_UNLIKELY(!thr_state->peer)) 
	{ 
		HIO_DEBUG3 (hio, "HTTS(%p) - failed to create thread for %p(%d)\n", htts, csck, (int)csck->hnd);
		goto oops; 
	}

	tfs = HIO_NULL; /* mark that tfs is delegated to the thread */

	thr_peer = hio_dev_thr_getxtn(thr_state->peer);
	HIO_SVC_HTTS_RSRC_ATTACH (thr_state, thr_peer->state);

	thr_state->peer_htrd = hio_htrd_open(hio, HIO_SIZEOF(*thr_peer));
	if (HIO_UNLIKELY(!thr_state->peer_htrd)) goto oops;
	hio_htrd_setoption (thr_state->peer_htrd, HIO_HTRD_SKIP_INITIAL_LINE | HIO_HTRD_RESPONSE);
	hio_htrd_setrecbs (thr_state->peer_htrd, &thr_peer_htrd_recbs);

	thr_peer = hio_htrd_getxtn(thr_state->peer_htrd);
	HIO_SVC_HTTS_RSRC_ATTACH (thr_state, thr_peer->state);

#if !defined(THR_ALLOW_UNLIMITED_REQ_CONTENT_LENGTH)
	if (thr_state->req_content_length_unlimited)
	{
		/* Transfer-Encoding is chunked. no content-length is known in advance. */
		
		/* option 1. buffer contents. if it gets too large, send 413 Request Entity Too Large.
		 * option 2. send 411 Length Required immediately
		 * option 3. set Content-Length to -1 and use EOF to indicate the end of content [Non-Standard] */

		if (thr_state_send_final_status_to_client(thr_state, 411, 1) <= -1) goto oops;
	}
#endif

	if (req->flags & HIO_HTRE_ATTR_EXPECT100)
	{
		/* TODO: Expect: 100-continue? who should handle this? thr? or the http server? */
		/* CAN I LET the thr SCRIPT handle this? */
		if (hio_comp_http_version_numbers(&req->version, 1, 1) >= 0 && 
		   (thr_state->req_content_length_unlimited || thr_state->req_content_length > 0)) 
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

			msglen = hio_fmttobcstr(hio, msgbuf, HIO_COUNTOF(msgbuf), "HTTP/%d.%d 100 Continue\r\n\r\n", thr_state->req_version.major, thr_state->req_version.minor);
			if (thr_state_write_to_client(thr_state, msgbuf, msglen) <= -1) goto oops;
			thr_state->ever_attempted_to_write_to_client = 0; /* reset this as it's polluted for 100 continue */
		}
	}
	else if (req->flags & HIO_HTRE_ATTR_EXPECT)
	{
		/* 417 Expectation Failed */
		thr_state_send_final_status_to_client(thr_state, 417, 1);
		goto oops;
	}

#if defined(THR_ALLOW_UNLIMITED_REQ_CONTENT_LENGTH)
	if (thr_state->req_content_length_unlimited)
	{
		/* change the callbacks to subscribe to contents to be uploaded */
		thr_state->client_htrd_org_recbs = *hio_htrd_getrecbs(thr_state->client->htrd);
		thr_client_htrd_recbs.peek = thr_state->client_htrd_org_recbs.peek;
		hio_htrd_setrecbs (thr_state->client->htrd, &thr_client_htrd_recbs);
		thr_state->client_htrd_recbs_changed = 1;
	}
	else
	{
#endif
		if (thr_state->req_content_length > 0)
		{
			/* change the callbacks to subscribe to contents to be uploaded */
			thr_state->client_htrd_org_recbs = *hio_htrd_getrecbs(thr_state->client->htrd);
			thr_client_htrd_recbs.peek = thr_state->client_htrd_org_recbs.peek;
			hio_htrd_setrecbs (thr_state->client->htrd, &thr_client_htrd_recbs);
			thr_state->client_htrd_recbs_changed = 1;
		}
		else
		{
			/* no content to be uploaded from the client */
			/* indicate EOF to the peer and disable input wathching from the client */
			if (thr_state_write_to_peer(thr_state, HIO_NULL, 0) <= -1) goto oops;
			thr_state_mark_over (thr_state, THR_STATE_OVER_READ_FROM_CLIENT | THR_STATE_OVER_WRITE_TO_PEER);
		}
#if defined(THR_ALLOW_UNLIMITED_REQ_CONTENT_LENGTH)
	}
#endif

	/* this may change later if Content-Length is included in the thr output */
	if (req->flags & HIO_HTRE_ATTR_KEEPALIVE)
	{
		thr_state->keep_alive = 1;
		thr_state->res_mode_to_cli = THR_STATE_RES_MODE_CHUNKED; 
		/* the mode still can get switched to THR_STATE_RES_MODE_LENGTH if the thr script emits Content-Length */
	}
	else
	{
		thr_state->keep_alive = 0;
		thr_state->res_mode_to_cli = THR_STATE_RES_MODE_CLOSE;
	}

	/* TODO: store current input watching state and use it when destroying the thr_state data */
	if (hio_dev_sck_read(csck, !(thr_state->over & THR_STATE_OVER_READ_FROM_CLIENT)) <= -1) goto oops;
	return 0;

oops:
	HIO_DEBUG2 (hio, "HTTS(%p) - FAILURE in dothr - socket(%p)\n", htts, csck);
	if (tfs) free_thr_start_info (tfs);
	if (thr_state) thr_state_halt_participating_devices (thr_state);
	return -1;
}
