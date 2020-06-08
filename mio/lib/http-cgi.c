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
#include <mio-pro.h>
#include <mio-fmt.h>
#include <mio-chr.h>

#include <unistd.h> /* TODO: move file operations to sys-file.XXX */
#include <fcntl.h>
#include <sys/stat.h>
#include <stdlib.h> /* setenv, clearenv */

#define CGI_ALLOW_UNLIMITED_REQ_CONTENT_LENGTH

enum cgi_state_res_mode_t
{
	CGI_STATE_RES_MODE_CHUNKED,
	CGI_STATE_RES_MODE_CLOSE,
	CGI_STATE_RES_MODE_LENGTH
};
typedef enum cgi_state_res_mode_t cgi_state_res_mode_t;

#define CGI_STATE_PENDING_IO_THRESHOLD 5

#define CGI_STATE_OVER_READ_FROM_CLIENT (1 << 0)
#define CGI_STATE_OVER_READ_FROM_PEER   (1 << 1)
#define CGI_STATE_OVER_WRITE_TO_CLIENT  (1 << 2)
#define CGI_STATE_OVER_WRITE_TO_PEER    (1 << 3)
#define CGI_STATE_OVER_ALL (CGI_STATE_OVER_READ_FROM_CLIENT | CGI_STATE_OVER_READ_FROM_PEER | CGI_STATE_OVER_WRITE_TO_CLIENT | CGI_STATE_OVER_WRITE_TO_PEER)

struct cgi_state_t
{
	MIO_SVC_HTTS_RSRC_HEADER;

	mio_oow_t num_pending_writes_to_client;
	mio_oow_t num_pending_writes_to_peer;
	mio_dev_pro_t* peer;
	mio_htrd_t* peer_htrd;
	mio_svc_htts_cli_t* client;
	mio_http_version_t req_version; /* client request */

	unsigned int over: 4; /* must be large enough to accomodate CGI_STATE_OVER_ALL */
	unsigned int keep_alive: 1;
	unsigned int req_content_length_unlimited: 1;
	unsigned int ever_attempted_to_write_to_client: 1;
	unsigned int client_disconnected: 1;
	unsigned int client_htrd_recbs_changed: 1;
	mio_oow_t req_content_length; /* client request content length */
	cgi_state_res_mode_t res_mode_to_cli;

	mio_dev_sck_on_read_t client_org_on_read;
	mio_dev_sck_on_write_t client_org_on_write;
	mio_dev_sck_on_disconnect_t client_org_on_disconnect;
	mio_htrd_recbs_t client_htrd_org_recbs;
};
typedef struct cgi_state_t cgi_state_t;

struct cgi_peer_xtn_t
{
	cgi_state_t* state;
};
typedef struct cgi_peer_xtn_t cgi_peer_xtn_t;

static void cgi_state_halt_participating_devices (cgi_state_t* cgi_state)
{
	MIO_ASSERT (cgi_state->client->htts->mio, cgi_state->client != MIO_NULL);
	MIO_ASSERT (cgi_state->client->htts->mio, cgi_state->client->sck != MIO_NULL);

	MIO_DEBUG4 (cgi_state->client->htts->mio, "HTTS(%p) - Halting participating devices in cgi state %p(client=%p,peer=%p)\n", cgi_state->client->htts, cgi_state, cgi_state->client->sck, cgi_state->peer);


	mio_dev_sck_halt (cgi_state->client->sck);
	/* check for peer as it may not have been started */
	if (cgi_state->peer) mio_dev_pro_halt (cgi_state->peer);
}

static int cgi_state_write_to_client (cgi_state_t* cgi_state, const void* data, mio_iolen_t dlen)
{
	cgi_state->ever_attempted_to_write_to_client = 1;

	cgi_state->num_pending_writes_to_client++;
	if (mio_dev_sck_write(cgi_state->client->sck, data, dlen, MIO_NULL, MIO_NULL) <= -1) 
	{
		cgi_state->num_pending_writes_to_client--;
		return -1;
	}

	if (cgi_state->num_pending_writes_to_client > CGI_STATE_PENDING_IO_THRESHOLD)
	{
		if (mio_dev_pro_read(cgi_state->peer, MIO_DEV_PRO_OUT, 0) <= -1) return -1;
	}
	return 0;
}

static int cgi_state_writev_to_client (cgi_state_t* cgi_state, mio_iovec_t* iov, mio_iolen_t iovcnt)
{
	cgi_state->ever_attempted_to_write_to_client = 1;

	cgi_state->num_pending_writes_to_client++;
	if (mio_dev_sck_writev(cgi_state->client->sck, iov, iovcnt, MIO_NULL, MIO_NULL) <= -1) 
	{
		cgi_state->num_pending_writes_to_client--;
		return -1;
	}

	if (cgi_state->num_pending_writes_to_client > CGI_STATE_PENDING_IO_THRESHOLD)
	{
		if (mio_dev_pro_read(cgi_state->peer, MIO_DEV_PRO_OUT, 0) <= -1) return -1;
	}
	return 0;
}

static int cgi_state_send_final_status_to_client (cgi_state_t* cgi_state, int status_code, int force_close)
{
	mio_svc_htts_cli_t* cli = cgi_state->client;
	mio_bch_t dtbuf[64];

	mio_svc_htts_fmtgmtime (cli->htts, MIO_NULL, dtbuf, MIO_COUNTOF(dtbuf));

	if (!force_close) force_close = !cgi_state->keep_alive;
	if (mio_becs_fmt(cli->sbuf, "HTTP/%d.%d %d %hs\r\nServer: %hs\r\nDate: %s\r\nConnection: %hs\r\nContent-Length: 0\r\n\r\n",
		cgi_state->req_version.major, cgi_state->req_version.minor,
		status_code, mio_http_status_to_bcstr(status_code),
		cli->htts->server_name, dtbuf,
		(force_close? "close": "keep-alive")) == (mio_oow_t)-1) return -1;

	return (cgi_state_write_to_client(cgi_state, MIO_BECS_PTR(cli->sbuf), MIO_BECS_LEN(cli->sbuf)) <= -1 ||
	        (force_close && cgi_state_write_to_client(cgi_state, MIO_NULL, 0) <= -1))? -1: 0;
}


static int cgi_state_write_last_chunk_to_client (cgi_state_t* cgi_state)
{
	if (!cgi_state->ever_attempted_to_write_to_client)
	{
		if (cgi_state_send_final_status_to_client(cgi_state, 500, 0) <= -1) return -1;
	}
	else
	{
		if (cgi_state->res_mode_to_cli == CGI_STATE_RES_MODE_CHUNKED &&
		    cgi_state_write_to_client(cgi_state, "0\r\n\r\n", 5) <= -1) return -1;
	}

	if (!cgi_state->keep_alive && cgi_state_write_to_client(cgi_state, MIO_NULL, 0) <= -1) return -1;
	return 0;
}

static int cgi_state_write_to_peer (cgi_state_t* cgi_state, const void* data, mio_iolen_t dlen)
{
	cgi_state->num_pending_writes_to_peer++;
	if (mio_dev_pro_write(cgi_state->peer, data, dlen, MIO_NULL) <= -1) 
	{
		cgi_state->num_pending_writes_to_peer--;
		return -1;
	}

/* TODO: check if it's already finished or something.. */
	if (cgi_state->num_pending_writes_to_peer > CGI_STATE_PENDING_IO_THRESHOLD)
	{
		if (mio_dev_sck_read(cgi_state->client->sck, 0) <= -1) return -1;
	}
	return 0;
}

static MIO_INLINE void cgi_state_mark_over (cgi_state_t* cgi_state, int over_bits)
{
	unsigned int old_over;

	old_over = cgi_state->over;
	cgi_state->over |= over_bits;

	MIO_DEBUG5 (cgi_state->htts->mio, "HTTS(%p) - client=%p peer=%p new-bits=%x over=%x\n", cgi_state->htts, cgi_state->client->sck, cgi_state->peer, (int)over_bits, (int)cgi_state->over);

	if (!(old_over & CGI_STATE_OVER_READ_FROM_CLIENT) && (cgi_state->over & CGI_STATE_OVER_READ_FROM_CLIENT))
	{
		if (mio_dev_sck_read(cgi_state->client->sck, 0) <= -1) 
		{
			MIO_DEBUG2 (cgi_state->htts->mio, "HTTS(%p) - halting client(%p) for failure to disable input watching\n", cgi_state->htts, cgi_state->client->sck);
			mio_dev_sck_halt (cgi_state->client->sck);
		}
	}

	if (!(old_over & CGI_STATE_OVER_READ_FROM_PEER) && (cgi_state->over & CGI_STATE_OVER_READ_FROM_PEER))
	{
		if (cgi_state->peer && mio_dev_pro_read(cgi_state->peer, MIO_DEV_PRO_OUT, 0) <= -1) 
		{
			MIO_DEBUG2 (cgi_state->htts->mio, "HTTS(%p) - halting peer(%p) for failure to disable input watching\n", cgi_state->htts, cgi_state->peer);
			mio_dev_pro_halt (cgi_state->peer);
		}
	}

	if (old_over != CGI_STATE_OVER_ALL && cgi_state->over == CGI_STATE_OVER_ALL)
	{
		/* ready to stop */
		if (cgi_state->peer) 
		{
			MIO_DEBUG2 (cgi_state->htts->mio, "HTTS(%p) - halting peer(%p) as it is unneeded\n", cgi_state->htts, cgi_state->peer);
			mio_dev_pro_halt (cgi_state->peer);
		}

		if (cgi_state->keep_alive) 
		{
			/* how to arrange to delete this cgi_state object and put the socket back to the normal waiting state??? */
			MIO_ASSERT (cgi_state->htts->mio, cgi_state->client->rsrc == (mio_svc_htts_rsrc_t*)cgi_state);

printf ("DETACHING FROM THE MAIN CLIENT RSRC... state -> %p\n", cgi_state->client->rsrc);
			MIO_SVC_HTTS_RSRC_DETACH (cgi_state->client->rsrc);
			/* cgi_state must not be access from here down as it could have been destroyed */
		}
		else
		{
			MIO_DEBUG2 (cgi_state->htts->mio, "HTTS(%p) - halting client(%p) for no keep-alive\n", cgi_state->htts, cgi_state->client->sck);
			mio_dev_sck_shutdown (cgi_state->client->sck, MIO_DEV_SCK_SHUTDOWN_WRITE);
			mio_dev_sck_halt (cgi_state->client->sck);
		}
	}
}

static void cgi_state_on_kill (cgi_state_t* cgi_state)
{
printf ("**** CGI_STATE_ON_KILL \n");
	if (cgi_state->peer)
	{
		cgi_peer_xtn_t* cgi_peer = mio_dev_pro_getxtn(cgi_state->peer);
		cgi_peer->state = MIO_NULL;  /* cgi_peer->state many not be NULL if the resource is killed regardless of the reference count */

		mio_dev_pro_kill (cgi_state->peer);
		cgi_state->peer = MIO_NULL;
	}

	if (cgi_state->peer_htrd)
	{
		cgi_peer_xtn_t* cgi_peer = mio_htrd_getxtn(cgi_state->peer_htrd);
		cgi_peer->state = MIO_NULL; /* cgi_peer->state many not be NULL if the resource is killed regardless of the reference count */

		mio_htrd_close (cgi_state->peer_htrd);
		cgi_state->peer_htrd = MIO_NULL;
	}

	if (cgi_state->client_org_on_read)
	{
		cgi_state->client->sck->on_read = cgi_state->client_org_on_read;
		cgi_state->client_org_on_read = MIO_NULL;
	}

	if (cgi_state->client_org_on_write)
	{
		cgi_state->client->sck->on_write = cgi_state->client_org_on_write;
		cgi_state->client_org_on_write = MIO_NULL;
	}


	if (cgi_state->client_org_on_disconnect)
	{
		cgi_state->client->sck->on_disconnect = cgi_state->client_org_on_disconnect;
		cgi_state->client_org_on_disconnect = MIO_NULL;
	}

	if (cgi_state->client_htrd_recbs_changed)
	{
		/* restore the callbacks */
		mio_htrd_setrecbs (cgi_state->client->htrd, &cgi_state->client_htrd_org_recbs);
	}

	if (!cgi_state->client_disconnected)
	{
printf ("ENABLING INPUT WATCHING on CLIENT %p. \n", cgi_state->client->sck);
		if (!cgi_state->keep_alive || mio_dev_sck_read(cgi_state->client->sck, 1) <= -1)
		{
			MIO_DEBUG2 (cgi_state->htts->mio, "HTTS(%p) - halting client(%p) for failure to enable input watching\n", cgi_state->htts, cgi_state->client->sck);
			mio_dev_sck_halt (cgi_state->client->sck);
		}
	}

printf ("**** CGI_STATE_ON_KILL DONE\n");
}

static void cgi_peer_on_close (mio_dev_pro_t* pro, mio_dev_pro_sid_t sid)
{
	mio_t* mio = pro->mio;
	cgi_peer_xtn_t* cgi_peer = mio_dev_pro_getxtn(pro);
	cgi_state_t* cgi_state = cgi_peer->state;

	if (!cgi_state) return; /* cgi state already gone */

	switch (sid)
	{
		case MIO_DEV_PRO_MASTER:
			MIO_DEBUG3 (mio, "HTTS(%p) - peer %p(pid=%d) closing master\n", cgi_state->client->htts, pro, (int)pro->child_pid);
			cgi_state->peer = MIO_NULL; /* clear this peer from the state */

			MIO_ASSERT (mio, cgi_peer->state != MIO_NULL);
printf ("DETACHING FROM CGI PEER DEVICE.....................%p   %d\n", cgi_peer->state, (int)cgi_peer->state->rsrc_refcnt);
			MIO_SVC_HTTS_RSRC_DETACH (cgi_peer->state);

			if (cgi_state->peer_htrd)
			{
				/* once this peer device is closed, peer's htrd is also never used.
				 * it's safe to detach the extra information attached on the htrd object. */
				cgi_peer = mio_htrd_getxtn(cgi_state->peer_htrd);
				MIO_ASSERT (mio, cgi_peer->state != MIO_NULL);
printf ("DETACHING FROM CGI PEER HTRD.....................%p   %d\n", cgi_peer->state, (int)cgi_peer->state->rsrc_refcnt);
				MIO_SVC_HTTS_RSRC_DETACH (cgi_peer->state);
			}

			break;

		case MIO_DEV_PRO_OUT:
			MIO_ASSERT (mio, cgi_state->peer == pro);
			MIO_DEBUG4 (mio, "HTTS(%p) - peer %p(pid=%d) closing slave[%d]\n", cgi_state->client->htts, pro, (int)pro->child_pid, sid);

			if (!(cgi_state->over & CGI_STATE_OVER_READ_FROM_PEER))
			{
				if (cgi_state_write_last_chunk_to_client(cgi_state) <= -1) 
					cgi_state_halt_participating_devices (cgi_state);
				else
					cgi_state_mark_over (cgi_state, CGI_STATE_OVER_READ_FROM_PEER);
			}
			break;

		case MIO_DEV_PRO_IN:
			cgi_state_mark_over (cgi_state, CGI_STATE_OVER_WRITE_TO_PEER);
			break;

		case MIO_DEV_PRO_ERR:
		default:
			MIO_DEBUG4 (mio, "HTTS(%p) - peer %p(pid=%d) closing slave[%d]\n", cgi_state->client->htts, pro, (int)pro->child_pid, sid);
			/* do nothing */
			break;
	}
}

static int cgi_peer_on_read (mio_dev_pro_t* pro, mio_dev_pro_sid_t sid, const void* data, mio_iolen_t dlen)
{
	mio_t* mio = pro->mio;
	cgi_peer_xtn_t* cgi_peer = mio_dev_pro_getxtn(pro);
	cgi_state_t* cgi_state = cgi_peer->state;

	MIO_ASSERT (mio, sid == MIO_DEV_PRO_OUT); /* since MIO_DEV_PRO_ERRTONUL is used, there should be no input from MIO_DEV_PRO_ERR */
	MIO_ASSERT (mio, cgi_state != MIO_NULL);

	if (dlen <= -1)
	{
		MIO_DEBUG3 (mio, "HTTPS(%p) - read error from peer %p(pid=%u)\n", cgi_state->client->htts, pro, (unsigned int)pro->child_pid);
		goto oops;
	}

	if (dlen == 0)
	{
		MIO_DEBUG3 (mio, "HTTPS(%p) - EOF from peer %p(pid=%u)\n", cgi_state->client->htts, pro, (unsigned int)pro->child_pid);

		if (!(cgi_state->over & CGI_STATE_OVER_READ_FROM_PEER))
		{
			/* the cgi script could be misbehaviing.
			 * it still has to read more but EOF is read.
			 * otherwise client_peer_htrd_poke() should have been called */
			if (cgi_state_write_last_chunk_to_client(cgi_state) <= -1) goto oops;
			cgi_state_mark_over (cgi_state, CGI_STATE_OVER_READ_FROM_PEER);
		}
	}
	else
	{
		mio_oow_t rem;

		MIO_ASSERT (mio, !(cgi_state->over & CGI_STATE_OVER_READ_FROM_PEER));

		if (mio_htrd_feed(cgi_state->peer_htrd, data, dlen, &rem) <= -1) 
		{
			MIO_DEBUG3 (mio, "HTTPS(%p) - unable to feed peer into to htrd - peer %p(pid=%u)\n", cgi_state->htts, pro, (unsigned int)pro->child_pid);

			if (!cgi_state->ever_attempted_to_write_to_client &&
			    !(cgi_state->over & CGI_STATE_OVER_WRITE_TO_CLIENT))
			{
				cgi_state_send_final_status_to_client (cgi_state, 500, 1); /* don't care about error because it jumps to oops below anyway */
			}

			goto oops; 
		}

		if (rem > 0) 
		{
			/* If the script specifies Content-Length and produces longer data, it will come here */
printf ("AAAAAAAAAAAAAAAAAa EEEEEXcessive DATA..................\n");
/* TODO: or drop this request?? */
		}
	}

	return 0;

oops:
	cgi_state_halt_participating_devices (cgi_state);
	return 0;
}

static int cgi_peer_capture_response_header (mio_htre_t* req, const mio_bch_t* key, const mio_htre_hdrval_t* val, void* ctx)
{
	mio_svc_htts_cli_t* cli = (mio_svc_htts_cli_t*)ctx;

	/* capture a header except Status, Connection, Transfer-Encoding, and Server */
	if (mio_comp_bcstr(key, "Status", 1) != 0 &&
	    mio_comp_bcstr(key, "Connection", 1) != 0 &&
	    mio_comp_bcstr(key, "Transfer-Encoding", 1) != 0 &&
	    mio_comp_bcstr(key, "Server", 1) != 0 &&
	    mio_comp_bcstr(key, "Date", 1) != 0)
	{
		do
		{
			if (mio_becs_cat(cli->sbuf, key) == (mio_oow_t)-1 ||
			    mio_becs_cat(cli->sbuf, ": ") == (mio_oow_t)-1 ||
			    mio_becs_cat(cli->sbuf, val->ptr) == (mio_oow_t)-1 ||
			    mio_becs_cat(cli->sbuf, "\r\n") == (mio_oow_t)-1)
			{
				return -1;
			}

			val = val->next;
		}
		while (val);
	}

	return 0;
}

static int cgi_peer_htrd_peek (mio_htrd_t* htrd, mio_htre_t* req)
{
	cgi_peer_xtn_t* cgi_peer = mio_htrd_getxtn(htrd);
	cgi_state_t* cgi_state = cgi_peer->state;
	mio_svc_htts_cli_t* cli = cgi_state->client;
	mio_bch_t dtbuf[64];
	int status_code = 200;

	if (req->attr.content_length)
	{
// TOOD: remove content_length if content_length is negative or not numeric.
		cgi_state->res_mode_to_cli = CGI_STATE_RES_MODE_LENGTH;
	}

	if (req->attr.status)
	{
		int is_sober;
		const mio_bch_t* endptr;
		mio_intmax_t v;

		v = mio_bchars_to_intmax(req->attr.status, mio_count_bcstr(req->attr.status), MIO_BCHARS_TO_INTMAX_MAKE_OPTION(0,0,10), &endptr, &is_sober);
		if (*endptr == '\0' && is_sober && v > 0 && v <= MIO_TYPE_MAX(int)) status_code = v;
	}

printf ("CGI PEER HTRD PEEK...\n");
	mio_svc_htts_fmtgmtime (cli->htts, MIO_NULL, dtbuf, MIO_COUNTOF(dtbuf));

	if (mio_becs_fmt(cli->sbuf, "HTTP/%d.%d %d %hs\r\nServer: %hs\r\nDate: %hs\r\n",
		cgi_state->req_version.major, cgi_state->req_version.minor,
		status_code, mio_http_status_to_bcstr(status_code),
		cli->htts->server_name, dtbuf) == (mio_oow_t)-1) return -1;

	if (mio_htre_walkheaders(req, cgi_peer_capture_response_header, cli) <= -1) return -1;

	switch (cgi_state->res_mode_to_cli)
	{
		case CGI_STATE_RES_MODE_CHUNKED:
			if (mio_becs_cat(cli->sbuf, "Transfer-Encoding: chunked\r\n") == (mio_oow_t)-1) return -1;
			/*if (mio_becs_cat(cli->sbuf, "Connection: keep-alive\r\n") == (mio_oow_t)-1) return -1;*/
			break;

		case CGI_STATE_RES_MODE_CLOSE:
			if (mio_becs_cat(cli->sbuf, "Connection: close\r\n") == (mio_oow_t)-1) return -1;
			break;

		case CGI_STATE_RES_MODE_LENGTH:
			if (mio_becs_cat(cli->sbuf, (cgi_state->keep_alive? "Connection: keep-alive\r\n": "Connection: close\r\n")) == (mio_oow_t)-1) return -1;
	}

	if (mio_becs_cat(cli->sbuf, "\r\n") == (mio_oow_t)-1) return -1;

	return cgi_state_write_to_client(cgi_state, MIO_BECS_PTR(cli->sbuf), MIO_BECS_LEN(cli->sbuf));
}

static int cgi_peer_htrd_poke (mio_htrd_t* htrd, mio_htre_t* req)
{
	/* client request got completed */
	cgi_peer_xtn_t* cgi_peer = mio_htrd_getxtn(htrd);
	cgi_state_t* cgi_state = cgi_peer->state;

printf (">> PEER RESPONSE COMPLETED\n");

	if (cgi_state_write_last_chunk_to_client(cgi_state) <= -1) return -1;

	cgi_state_mark_over (cgi_state, CGI_STATE_OVER_READ_FROM_PEER);
	return 0;
}

static int cgi_peer_htrd_push_content (mio_htrd_t* htrd, mio_htre_t* req, const mio_bch_t* data, mio_oow_t dlen)
{
	cgi_peer_xtn_t* cgi_peer = mio_htrd_getxtn(htrd);
	cgi_state_t* cgi_state = cgi_peer->state;

	MIO_ASSERT (cgi_state->client->htts->mio, htrd == cgi_state->peer_htrd);

	switch (cgi_state->res_mode_to_cli)
	{
		case CGI_STATE_RES_MODE_CHUNKED:
		{
			mio_iovec_t iov[3];
			mio_bch_t lbuf[16];
			mio_oow_t llen;

			/* mio_fmt_uintmax_to_bcstr() null-terminates the output. only MIO_COUNTOF(lbuf) - 1
			 * is enough to hold '\r' and '\n' at the back without '\0'. */ 
			llen = mio_fmt_uintmax_to_bcstr(lbuf, MIO_COUNTOF(lbuf) - 1, dlen, 16 | MIO_FMT_UINTMAX_UPPERCASE, 0, '\0', MIO_NULL);
			lbuf[llen++] = '\r';
			lbuf[llen++] = '\n';

			iov[0].iov_ptr = lbuf;
			iov[0].iov_len = llen;
			iov[1].iov_ptr = (void*)data;
			iov[1].iov_len = dlen;
			iov[2].iov_ptr = "\r\n";
			iov[2].iov_len = 2;

			if (cgi_state_writev_to_client(cgi_state, iov, MIO_COUNTOF(iov)) <= -1) goto oops;
			break;
		}

		case CGI_STATE_RES_MODE_CLOSE:
		case CGI_STATE_RES_MODE_LENGTH:
			if (cgi_state_write_to_client(cgi_state, data, dlen) <= -1) goto oops;
			break;
	}

	if (cgi_state->num_pending_writes_to_client > CGI_STATE_PENDING_IO_THRESHOLD)
	{
		if (mio_dev_pro_read(cgi_state->peer, MIO_DEV_PRO_OUT, 0) <= -1) goto oops;
	}

	return 0;

oops:
	return -1;
}

static mio_htrd_recbs_t cgi_peer_htrd_recbs =
{
	cgi_peer_htrd_peek,
	cgi_peer_htrd_poke,
	cgi_peer_htrd_push_content
};

static int cgi_client_htrd_poke (mio_htrd_t* htrd, mio_htre_t* req)
{
	/* client request got completed */
	mio_svc_htts_cli_htrd_xtn_t* htrdxtn = (mio_svc_htts_cli_htrd_xtn_t*)mio_htrd_getxtn(htrd);
	mio_dev_sck_t* sck = htrdxtn->sck;
	mio_svc_htts_cli_t* cli = mio_dev_sck_getxtn(sck);
	cgi_state_t* cgi_state = (cgi_state_t*)cli->rsrc;

printf (">> CLIENT REQUEST COMPLETED\n");

	/* indicate EOF to the client peer */
	if (cgi_state_write_to_peer(cgi_state, MIO_NULL, 0) <= -1) return -1;

	cgi_state_mark_over (cgi_state, CGI_STATE_OVER_READ_FROM_CLIENT);
	return 0;
}

static int cgi_client_htrd_push_content (mio_htrd_t* htrd, mio_htre_t* req, const mio_bch_t* data, mio_oow_t dlen)
{
	mio_svc_htts_cli_htrd_xtn_t* htrdxtn = (mio_svc_htts_cli_htrd_xtn_t*)mio_htrd_getxtn(htrd);
	mio_dev_sck_t* sck = htrdxtn->sck;
	mio_svc_htts_cli_t* cli = mio_dev_sck_getxtn(sck);
	cgi_state_t* cgi_state = (cgi_state_t*)cli->rsrc;

	MIO_ASSERT (sck->mio, cli->sck == sck);
	return cgi_state_write_to_peer(cgi_state, data, dlen);
}

static mio_htrd_recbs_t cgi_client_htrd_recbs =
{
	MIO_NULL,
	cgi_client_htrd_poke,
	cgi_client_htrd_push_content
};

static int cgi_peer_on_write (mio_dev_pro_t* pro, mio_iolen_t wrlen, void* wrctx)
{
	mio_t* mio = pro->mio;
	cgi_peer_xtn_t* cgi_peer = mio_dev_pro_getxtn(pro);
	cgi_state_t* cgi_state = cgi_peer->state;

	if (cgi_state == MIO_NULL) return 0; /* there is nothing i can do. the cgi_state is being cleared or has been cleared already. */

	MIO_ASSERT (mio, cgi_state->peer == pro);

	if (wrlen <= -1)
	{
		MIO_DEBUG3 (mio, "HTTS(%p) - unable to write to peer %p(pid=%u)\n", cgi_state->client->htts, pro, (int)pro->child_pid);
		goto oops;
	}
	else if (wrlen == 0)
	{
		/* indicated EOF */
		/* do nothing here as i didn't incremented num_pending_writes_to_peer when making the write request */

		cgi_state->num_pending_writes_to_peer--;
		MIO_ASSERT (mio, cgi_state->num_pending_writes_to_peer == 0);
		MIO_DEBUG3 (mio, "HTTS(%p) - indicated EOF to peer %p(pid=%u)\n", cgi_state->client->htts, pro, (int)pro->child_pid);
		/* indicated EOF to the peer side. i need no more data from the client side.
		 * i don't need to enable input watching in the client side either */
		cgi_state_mark_over (cgi_state, CGI_STATE_OVER_WRITE_TO_PEER);
	}
	else
	{
		MIO_ASSERT (mio, cgi_state->num_pending_writes_to_peer > 0);

		cgi_state->num_pending_writes_to_peer--;
		if (cgi_state->num_pending_writes_to_peer == CGI_STATE_PENDING_IO_THRESHOLD)
		{
			if (!(cgi_state->over & CGI_STATE_OVER_READ_FROM_CLIENT) &&
			    mio_dev_sck_read(cgi_state->client->sck, 1) <= -1) goto oops;
		}

		if ((cgi_state->over & CGI_STATE_OVER_READ_FROM_CLIENT) && cgi_state->num_pending_writes_to_peer <= 0)
		{
			cgi_state_mark_over (cgi_state, CGI_STATE_OVER_WRITE_TO_PEER);
		}
	}

	return 0;

oops:
	cgi_state_halt_participating_devices (cgi_state);
	return 0;
}

static void cgi_client_on_disconnect (mio_dev_sck_t* sck)
{
	mio_svc_htts_cli_t* cli = mio_dev_sck_getxtn(sck);
	cgi_state_t* cgi_state = (cgi_state_t*)cli->rsrc;
	cgi_state->client_disconnected = 1;
	cgi_state->client_org_on_disconnect (sck);
}

static int cgi_client_on_read (mio_dev_sck_t* sck, const void* buf, mio_iolen_t len, const mio_skad_t* srcaddr)
{
	mio_t* mio = sck->mio;
	mio_svc_htts_cli_t* cli = mio_dev_sck_getxtn(sck);
	cgi_state_t* cgi_state = (cgi_state_t*)cli->rsrc;

	MIO_ASSERT (mio, sck == cli->sck);

	if (len <= -1)
	{
		/* read error */
		MIO_DEBUG2 (cli->htts->mio, "HTTPS(%p) - read error on client %p(%d)\n", sck, (int)sck->hnd);
		goto oops;
	}

	if (!cgi_state->peer)
	{
		/* the peer is gone */
		goto oops; /* do what?  just return 0? */
	}

	if (len == 0)
	{
		/* EOF on the client side. arrange to close */
		MIO_DEBUG3 (mio, "HTTPS(%p) - EOF from client %p(hnd=%d)\n", cgi_state->client->htts, sck, (int)sck->hnd);

		if (!(cgi_state->over & CGI_STATE_OVER_READ_FROM_CLIENT)) /* if this is true, EOF is received without cgi_client_htrd_poke() */
		{
			if (cgi_state_write_to_peer(cgi_state, MIO_NULL, 0) <= -1) goto oops;
			cgi_state_mark_over (cgi_state, CGI_STATE_OVER_READ_FROM_CLIENT);
		}
	}
	else
	{
		mio_oow_t rem;

		MIO_ASSERT (mio, !(cgi_state->over & CGI_STATE_OVER_READ_FROM_CLIENT));

		if (mio_htrd_feed(cli->htrd, buf, len, &rem) <= -1) goto oops;

		if (rem > 0)
		{
			/* TODO store this to client buffer. once the current resource is completed, arrange to call on_read() with it */
printf ("UUUUUUUUUUUUUUUUUUUUUUUUUUGGGGGHHHHHHHHHHHH .......... CGI CLIENT GIVING EXCESSIVE DATA AFTER CONTENTS...\n");
		}
	}

	return 0;

oops:
	cgi_state_halt_participating_devices (cgi_state);
	return 0;
}

static int cgi_client_on_write (mio_dev_sck_t* sck, mio_iolen_t wrlen, void* wrctx, const mio_skad_t* dstaddr)
{
	mio_t* mio = sck->mio;
	mio_svc_htts_cli_t* cli = mio_dev_sck_getxtn(sck);
	cgi_state_t* cgi_state = (cgi_state_t*)cli->rsrc;

	if (wrlen <= -1)
	{
		MIO_DEBUG3 (mio, "HTTPS(%p) - unable to write to client %p(%d)\n", sck->mio, sck, (int)sck->hnd);
		goto oops;
	}

	if (wrlen == 0)
	{
		/* if the connect is keep-alive, this part may not be called */
		cgi_state->num_pending_writes_to_client--;
		MIO_ASSERT (mio, cgi_state->num_pending_writes_to_client == 0);
		MIO_DEBUG3 (mio, "HTTS(%p) - indicated EOF to client %p(%d)\n", cgi_state->client->htts, sck, (int)sck->hnd);
		/* since EOF has been indicated to the client, it must not write to the client any further.
		 * this also means that i don't need any data from the peer side either.
		 * i don't need to enable input watching on the peer side */
		cgi_state_mark_over (cgi_state, CGI_STATE_OVER_WRITE_TO_CLIENT);
	}
	else
	{
		MIO_ASSERT (mio, cgi_state->num_pending_writes_to_client > 0);

		cgi_state->num_pending_writes_to_client--;
		if (cgi_state->peer && cgi_state->num_pending_writes_to_client == CGI_STATE_PENDING_IO_THRESHOLD)
		{
			if (!(cgi_state->over & CGI_STATE_OVER_READ_FROM_PEER) &&
			    mio_dev_pro_read(cgi_state->peer, MIO_DEV_PRO_OUT, 1) <= -1) goto oops;
		}

		if ((cgi_state->over & CGI_STATE_OVER_READ_FROM_PEER) && cgi_state->num_pending_writes_to_client <= 0)
		{
			cgi_state_mark_over (cgi_state, CGI_STATE_OVER_WRITE_TO_CLIENT);
		}
	}

	return 0;

oops:
	cgi_state_halt_participating_devices (cgi_state);
	return 0;
}

struct cgi_peer_fork_ctx_t
{
	mio_svc_htts_cli_t* cli;
	mio_htre_t* req;
	const mio_bch_t* docroot;
	const mio_bch_t* script;
	mio_bch_t* actual_script;
};
typedef struct cgi_peer_fork_ctx_t cgi_peer_fork_ctx_t;

static int cgi_peer_capture_request_header (mio_htre_t* req, const mio_bch_t* key, const mio_htre_hdrval_t* val, void* ctx)
{
	mio_becs_t* dbuf = (mio_becs_t*)ctx;

	if (mio_comp_bcstr(key, "Connection", 1) != 0 &&
	    mio_comp_bcstr(key, "Transfer-Encoding", 1) != 0 &&
	    mio_comp_bcstr(key, "Content-Length", 1) != 0 &&
	    mio_comp_bcstr(key, "Expect", 1) != 0)
	{
		mio_oow_t val_offset;
		mio_bch_t* ptr;

		mio_becs_clear (dbuf);
		if (mio_becs_cpy(dbuf, "HTTP_") == (mio_oow_t)-1 ||
		    mio_becs_cat(dbuf, key) == (mio_oow_t)-1 ||
		    mio_becs_ccat(dbuf, '\0') == (mio_oow_t)-1) return -1;

		for (ptr = MIO_BECS_PTR(dbuf); *ptr; ptr++)
		{
			*ptr = mio_to_bch_upper(*ptr);
			if (*ptr =='-') *ptr = '_';
		}

		val_offset = MIO_BECS_LEN(dbuf);
		if (mio_becs_cat(dbuf, val->ptr) == (mio_oow_t)-1) return -1;
		val = val->next;
		while (val)
		{
			if (mio_becs_cat(dbuf, ",") == (mio_oow_t)-1 ||
			    mio_becs_cat(dbuf, val->ptr) == (mio_oow_t)-1) return -1;
			val = val->next;
		}

		setenv (MIO_BECS_PTR(dbuf), MIO_BECS_CPTR(dbuf, val_offset), 1);
	}

	return 0;
}

static int cgi_peer_on_fork (mio_dev_pro_t* pro, void* fork_ctx)
{
	mio_t* mio = pro->mio; /* in this callback, the pro device is not fully up. however, the mio field is guaranteed to be available */
	cgi_peer_fork_ctx_t* fc = (cgi_peer_fork_ctx_t*)fork_ctx;
	mio_oow_t content_length;
	const mio_bch_t* qparam;
	mio_bch_t* path, * lang;
	mio_bch_t tmp[256];
	mio_becs_t dbuf;

	qparam = mio_htre_getqparam(fc->req);

	path = mio_dupbcstr(mio, getenv("PATH"), MIO_NULL);
	lang = mio_dupbcstr(mio, getenv("LANG"), MIO_NULL);
#if defined(HAVE_CLEARENV)
	clearenv ();
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
		mio_freemem (mio, path);
	}

	if (lang) 
	{
		setenv ("LANG", lang, 1);
		mio_freemem (mio, lang);
	}

	setenv ("GATEWAY_INTERFACE", "CGI/1.1", 1);

	mio_fmttobcstr (mio, tmp, MIO_COUNTOF(tmp), "HTTP/%d.%d", (int)mio_htre_getmajorversion(fc->req), (int)mio_htre_getminorversion(fc->req));
	setenv ("SERVER_PROTOCOL", tmp, 1);

	setenv ("DOCUMENT_ROOT", fc->docroot, 1);
	setenv ("SCRIPT_NAME", fc->script, 1);
	setenv ("SCRIPT_FILENAME", fc->actual_script, 1);
	/* TODO: PATH_INFO */

	setenv ("REQUEST_METHOD", mio_htre_getqmethodname(fc->req), 1);
	setenv ("REQUEST_URI", mio_htre_getqpath(fc->req), 1);
	if (qparam) setenv ("QUERY_STRING", qparam, 1);

	if (mio_htre_getreqcontentlen(fc->req, &content_length) == 0)
	{
		mio_fmt_uintmax_to_bcstr(tmp, MIO_COUNTOF(tmp), content_length, 10, 0, '\0', MIO_NULL);
		setenv ("CONTENT_LENGTH", tmp, 1);
	}
	else
	{
		/* content length unknown, neither is it 0 - this is not standard */
		setenv ("CONTENT_LENGTH", "-1", 1);
	}
	setenv ("SERVER_SOFTWARE", fc->cli->htts->server_name, 1);

	mio_skadtobcstr (mio, &fc->cli->sck->localaddr, tmp, MIO_COUNTOF(tmp), MIO_SKAD_TO_BCSTR_ADDR);
	setenv ("SERVER_ADDR", tmp, 1);

	gethostname (tmp, MIO_COUNTOF(tmp)); /* if this fails, i assume tmp contains the ip address set by mio_skadtobcstr() above */
	setenv ("SERVER_NAME", tmp, 1);

	mio_skadtobcstr (mio, &fc->cli->sck->localaddr, tmp, MIO_COUNTOF(tmp), MIO_SKAD_TO_BCSTR_PORT);
	setenv ("SERVER_PORT", tmp, 1);

	mio_skadtobcstr (mio, &fc->cli->sck->remoteaddr, tmp, MIO_COUNTOF(tmp), MIO_SKAD_TO_BCSTR_ADDR);
	setenv ("REMOTE_ADDR", tmp, 1);

	mio_skadtobcstr (mio, &fc->cli->sck->remoteaddr, tmp, MIO_COUNTOF(tmp), MIO_SKAD_TO_BCSTR_PORT);
	setenv ("REMOTE_PORT", tmp, 1);

	if (mio_becs_init(&dbuf, mio, 256) >= 0)
	{
		mio_htre_walkheaders (fc->req,  cgi_peer_capture_request_header, &dbuf);
		/* [NOTE] trailers are not available when this cgi resource is started. let's not call mio_htre_walktrailers() */
		mio_becs_fini (&dbuf);
	}

	return 0;
}

int mio_svc_htts_docgi (mio_svc_htts_t* htts, mio_dev_sck_t* csck, mio_htre_t* req, const mio_bch_t* docroot, const mio_bch_t* script)
{
	mio_t* mio = htts->mio;
	mio_svc_htts_cli_t* cli = mio_dev_sck_getxtn(csck);
	cgi_state_t* cgi_state = MIO_NULL;
	cgi_peer_xtn_t* cgi_peer;
	mio_dev_pro_make_t mi;
	cgi_peer_fork_ctx_t fc;

	/* ensure that you call this function before any contents is received */
	MIO_ASSERT (mio, mio_htre_getcontentlen(req) == 0);

	MIO_MEMSET (&fc, 0, MIO_SIZEOF(fc));
	fc.cli = cli;
	fc.req = req;
	fc.docroot = docroot;
	fc.script = script;
	fc.actual_script = mio_svc_htts_dupmergepaths(htts, docroot, script);
	if (!fc.actual_script) goto oops;

	MIO_MEMSET (&mi, 0, MIO_SIZEOF(mi));
	mi.flags = MIO_DEV_PRO_READOUT | MIO_DEV_PRO_ERRTONUL | MIO_DEV_PRO_WRITEIN /*| MIO_DEV_PRO_FORGET_CHILD*/;
	mi.cmd = fc.actual_script;
	mi.on_read = cgi_peer_on_read;
	mi.on_write = cgi_peer_on_write;
	mi.on_close = cgi_peer_on_close;
	mi.on_fork = cgi_peer_on_fork;
	mi.fork_ctx = &fc;

	cgi_state = (cgi_state_t*)mio_svc_htts_rsrc_make(htts, MIO_SIZEOF(*cgi_state), cgi_state_on_kill);
	if (MIO_UNLIKELY(!cgi_state)) goto oops;

	cgi_state->client = cli;
	/*cgi_state->num_pending_writes_to_client = 0;
	cgi_state->num_pending_writes_to_peer = 0;*/
	cgi_state->req_version = *mio_htre_getversion(req);
	cgi_state->req_content_length_unlimited = mio_htre_getreqcontentlen(req, &cgi_state->req_content_length);

	cgi_state->client_org_on_read = csck->on_read;
	cgi_state->client_org_on_write = csck->on_write;
	cgi_state->client_org_on_disconnect = csck->on_disconnect;
	csck->on_read = cgi_client_on_read;
	csck->on_write = cgi_client_on_write;
	csck->on_disconnect = cgi_client_on_disconnect;

	MIO_ASSERT (mio, cli->rsrc == MIO_NULL);
	MIO_SVC_HTTS_RSRC_ATTACH (cgi_state, cli->rsrc);

	if (access(mi.cmd, X_OK) == -1)
	{
		cgi_state_send_final_status_to_client (cgi_state, 403, 1); /* 403 Forbidden */
		goto oops; /* TODO: must not go to oops.  just destroy the cgi_state and finalize the request .. */
	}

	cgi_state->peer = mio_dev_pro_make(mio, MIO_SIZEOF(*cgi_peer), &mi);
	if (MIO_UNLIKELY(!cgi_state->peer)) goto oops;
	cgi_peer = mio_dev_pro_getxtn(cgi_state->peer);
	MIO_SVC_HTTS_RSRC_ATTACH (cgi_state, cgi_peer->state);

	cgi_state->peer_htrd = mio_htrd_open(mio, MIO_SIZEOF(*cgi_peer));
	if (MIO_UNLIKELY(!cgi_state->peer_htrd)) goto oops;
	mio_htrd_setopt (cgi_state->peer_htrd, MIO_HTRD_SKIPINITIALLINE | MIO_HTRD_RESPONSE);
	mio_htrd_setrecbs (cgi_state->peer_htrd, &cgi_peer_htrd_recbs);

	cgi_peer = mio_htrd_getxtn(cgi_state->peer_htrd);
	MIO_SVC_HTTS_RSRC_ATTACH (cgi_state, cgi_peer->state);

#if !defined(CGI_ALLOW_UNLIMITED_REQ_CONTENT_LENGTH)
	if (cgi_state->req_content_length_unlimited)
	{
		/* Transfer-Encoding is chunked. no content-length is known in advance. */
		
		/* option 1. buffer contents. if it gets too large, send 413 Request Entity Too Large.
		 * option 2. send 411 Length Required immediately
		 * option 3. set Content-Length to -1 and use EOF to indicate the end of content [Non-Standard] */

		if (cgi_state_send_final_status_to_client(cgi_state, 411, 1) <= -1) goto oops;
	}
#endif

	if (req->flags & MIO_HTRE_ATTR_EXPECT100)
	{
		/* TODO: Expect: 100-continue? who should handle this? cgi? or the http server? */
		/* CAN I LET the cgi SCRIPT handle this? */
		if (mio_comp_http_version_numbers(&req->version, 1, 1) >= 0 && 
		   (cgi_state->req_content_length_unlimited || cgi_state->req_content_length > 0)) 
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
			mio_bch_t msgbuf[64];
			mio_oow_t msglen;

			msglen = mio_fmttobcstr(mio, msgbuf, MIO_COUNTOF(msgbuf), "HTTP/%d.%d 100 Continue\r\n\r\n", cgi_state->req_version.major, cgi_state->req_version.minor);
			if (cgi_state_write_to_client(cgi_state, msgbuf, msglen) <= -1) goto oops;
			cgi_state->ever_attempted_to_write_to_client = 0; /* reset this as it's polluted for 100 continue */
		}
	}
	else if (req->flags & MIO_HTRE_ATTR_EXPECT)
	{
		/* 417 Expectation Failed */
		cgi_state_send_final_status_to_client(cgi_state, 417, 1);
		goto oops;
	}

#if defined(CGI_ALLOW_UNLIMITED_REQ_CONTENT_LENGTH)
	if (cgi_state->req_content_length_unlimited)
	{
		/* change the callbacks to subscribe to contents to be uploaded */
		cgi_state->client_htrd_org_recbs = *mio_htrd_getrecbs(cgi_state->client->htrd);
		cgi_client_htrd_recbs.peek = cgi_state->client_htrd_org_recbs.peek;
		mio_htrd_setrecbs (cgi_state->client->htrd, &cgi_client_htrd_recbs);
		cgi_state->client_htrd_recbs_changed = 1;
	}
	else
	{
#endif
		if (cgi_state->req_content_length > 0)
		{
			/* change the callbacks to subscribe to contents to be uploaded */
			cgi_state->client_htrd_org_recbs = *mio_htrd_getrecbs(cgi_state->client->htrd);
			cgi_client_htrd_recbs.peek = cgi_state->client_htrd_org_recbs.peek;
			mio_htrd_setrecbs (cgi_state->client->htrd, &cgi_client_htrd_recbs);
			cgi_state->client_htrd_recbs_changed = 1;
		}
		else
		{
			/* no content to be uploaded from the client */
			/* indicate EOF to the peer and disable input wathching from the client */
			if (cgi_state_write_to_peer(cgi_state, MIO_NULL, 0) <= -1) goto oops;
			cgi_state_mark_over (cgi_state, CGI_STATE_OVER_READ_FROM_CLIENT | CGI_STATE_OVER_WRITE_TO_PEER);
		}
#if defined(CGI_ALLOW_UNLIMITED_REQ_CONTENT_LENGTH)
	}
#endif

	/* this may change later if Content-Length is included in the cgi output */
	if (req->flags & MIO_HTRE_ATTR_KEEPALIVE)
	{
		cgi_state->keep_alive = 1;
		cgi_state->res_mode_to_cli = CGI_STATE_RES_MODE_CHUNKED; 
		/* the mode still can get switched to CGI_STATE_RES_MODE_LENGTH if the cgi script emits Content-Length */
	}
	else
	{
		cgi_state->keep_alive = 0;
		cgi_state->res_mode_to_cli = CGI_STATE_RES_MODE_CLOSE;
	}

	/* TODO: store current input watching state and use it when destroying the cgi_state data */
	if (mio_dev_sck_read(csck, !(cgi_state->over & CGI_STATE_OVER_READ_FROM_CLIENT)) <= -1) goto oops;
	mio_freemem (mio, fc.actual_script);
	return 0;

oops:
	MIO_DEBUG2 (mio, "HTTS(%p) - FAILURE in docgi - socket(%p)\n", htts, csck);
	if (cgi_state) cgi_state_halt_participating_devices (cgi_state);
	if (fc.actual_script) mio_freemem (mio, fc.actual_script);
	return -1;
}
