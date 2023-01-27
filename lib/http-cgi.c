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
#include <hio-pro.h>
#include <hio-fmt.h>
#include <hio-chr.h>

#include <unistd.h> /* TODO: move file operations to sys-file.XXX */
#include <fcntl.h>
#include <sys/stat.h>
#include <stdlib.h> /* setenv, clearenv */

#if defined(HAVE_CRT_EXTERNS_H)
#	include <crt_externs.h> /* _NSGetEnviron */
#endif

#define CGI_ALLOW_UNLIMITED_REQ_CONTENT_LENGTH

enum cgi_res_mode_t
{
	CGI_RES_MODE_CHUNKED,
	CGI_RES_MODE_CLOSE,
	CGI_RES_MODE_LENGTH
};
typedef enum cgi_res_mode_t cgi_res_mode_t;

#define CGI_PENDING_IO_THRESHOLD 5

#define CGI_OVER_READ_FROM_CLIENT (1 << 0)
#define CGI_OVER_READ_FROM_PEER   (1 << 1)
#define CGI_OVER_WRITE_TO_CLIENT  (1 << 2)
#define CGI_OVER_WRITE_TO_PEER    (1 << 3)
#define CGI_OVER_ALL (CGI_OVER_READ_FROM_CLIENT | CGI_OVER_READ_FROM_PEER | CGI_OVER_WRITE_TO_CLIENT | CGI_OVER_WRITE_TO_PEER)

struct cgi_t
{
	HIO_SVC_HTTS_TASK_HEADER;

	int options;
	hio_oow_t num_pending_writes_to_client;
	hio_oow_t num_pending_writes_to_peer;
	hio_dev_pro_t* peer;
	hio_htrd_t* peer_htrd;

	hio_dev_sck_t* csck;
	hio_svc_htts_cli_t* client;
	hio_http_version_t req_version; /* client request */
	hio_http_method_t req_method;

	unsigned int over: 4; /* must be large enough to accomodate CGI_OVER_ALL */
	unsigned int keep_alive: 1;
	unsigned int req_content_length_unlimited: 1;
	unsigned int ever_attempted_to_write_to_client: 1;
	unsigned int client_eof_detected: 1;
	unsigned int client_disconnected: 1;
	unsigned int client_htrd_recbs_changed: 1;
	hio_oow_t req_content_length; /* client request content length */
	cgi_res_mode_t res_mode_to_cli;

	hio_dev_sck_on_read_t client_org_on_read;
	hio_dev_sck_on_write_t client_org_on_write;
	hio_dev_sck_on_disconnect_t client_org_on_disconnect;
	hio_htrd_recbs_t client_htrd_org_recbs;
};
typedef struct cgi_t cgi_t;

struct cgi_peer_xtn_t
{
	cgi_t* cgi; /* back pointer to the cgi object */
};
typedef struct cgi_peer_xtn_t cgi_peer_xtn_t;

static void cgi_halt_participating_devices (cgi_t* cgi)
{
	HIO_ASSERT (cgi->htts->hio, cgi->client != HIO_NULL);
	HIO_ASSERT (cgi->htts->hio, cgi->csck != HIO_NULL);

	HIO_DEBUG5 (cgi->htts->hio, "HTTS(%p) - cgi(t=%p,c=%p(%d),p=%p) Halting participating devices\n", cgi->htts, cgi, cgi->csck, (cgi->csck? cgi->csck->hnd: -1), cgi->peer);

	if (cgi->csck) hio_dev_sck_halt (cgi->csck);

	/* check for peer as it may not have been started */
	if (cgi->peer) hio_dev_pro_halt (cgi->peer);
}

static int cgi_write_to_client (cgi_t* cgi, const void* data, hio_iolen_t dlen)
{
	if (cgi->csck)
	{
		cgi->ever_attempted_to_write_to_client = 1;

		cgi->num_pending_writes_to_client++;
		if (hio_dev_sck_write(cgi->csck, data, dlen, HIO_NULL, HIO_NULL) <= -1)
		{
			cgi->num_pending_writes_to_client--;
			return -1;
		}

		if (cgi->num_pending_writes_to_client > CGI_PENDING_IO_THRESHOLD)
		{
			/* disable reading on the output stream of the peer */
			if (hio_dev_pro_read(cgi->peer, HIO_DEV_PRO_OUT, 0) <= -1) return -1;
		}
	}
	return 0;
}

static int cgi_writev_to_client (cgi_t* cgi, hio_iovec_t* iov, hio_iolen_t iovcnt)
{
	if (cgi->csck)
	{
		cgi->ever_attempted_to_write_to_client = 1;

		cgi->num_pending_writes_to_client++;
		if (hio_dev_sck_writev(cgi->csck, iov, iovcnt, HIO_NULL, HIO_NULL) <= -1)
		{
			cgi->num_pending_writes_to_client--;
			return -1;
		}

		if (cgi->num_pending_writes_to_client > CGI_PENDING_IO_THRESHOLD)
		{
			if (hio_dev_pro_read(cgi->peer, HIO_DEV_PRO_OUT, 0) <= -1) return -1;
		}
	}
	return 0;
}

static int cgi_send_final_status_to_client (cgi_t* cgi, int status_code, int force_close)
{
	hio_svc_htts_cli_t* cli = cgi->client;
	hio_bch_t dtbuf[64];
	const hio_bch_t* status_msg;
	hio_oow_t content_len;

	hio_svc_htts_fmtgmtime (cli->htts, HIO_NULL, dtbuf, HIO_COUNTOF(dtbuf));
	status_msg = hio_http_status_to_bcstr(status_code);
	content_len = hio_count_bcstr(status_msg);

	if (!force_close) force_close = !cgi->keep_alive;
	if (hio_becs_fmt(cli->sbuf, "HTTP/%d.%d %d %hs\r\nServer: %hs\r\nDate: %hs\r\nConnection: %hs\r\n",
		cgi->req_version.major, cgi->req_version.minor,
		status_code, status_msg,
		cli->htts->server_name, dtbuf,
		(force_close? "close": "keep-alive")) == (hio_oow_t)-1) return -1;

	if (cgi->req_method == HIO_HTTP_HEAD)
	{
		if (status_code != HIO_HTTP_STATUS_OK) content_len = 0;
		status_msg = "";
	}

    if (hio_becs_fcat(cli->sbuf, "Content-Type: text/plain\r\nContent-Length: %zu\r\n\r\n%hs", content_len, status_msg) == (hio_oow_t)-1) return -1;

	return (cgi_write_to_client(cgi, HIO_BECS_PTR(cli->sbuf), HIO_BECS_LEN(cli->sbuf)) <= -1 ||
	        (force_close && cgi_write_to_client(cgi, HIO_NULL, 0) <= -1))? -1: 0;
}

static int cgi_write_last_chunk_to_client (cgi_t* cgi)
{
	if (!cgi->ever_attempted_to_write_to_client)
	{
		if (cgi_send_final_status_to_client(cgi, HIO_HTTP_STATUS_INTERNAL_SERVER_ERROR, 0) <= -1) return -1;
	}
	else
	{
		if (cgi->res_mode_to_cli == CGI_RES_MODE_CHUNKED &&
		    cgi_write_to_client(cgi, "0\r\n\r\n", 5) <= -1) return -1;
	}

	if (!cgi->keep_alive && cgi_write_to_client(cgi, HIO_NULL, 0) <= -1) return -1;
	return 0;
}

static int cgi_write_to_peer (cgi_t* cgi, const void* data, hio_iolen_t dlen)
{
	if (cgi->peer)
	{
		cgi->num_pending_writes_to_peer++;
		if (hio_dev_pro_write(cgi->peer, data, dlen, HIO_NULL) <= -1)
		{
			cgi->num_pending_writes_to_peer--;
			return -1;
		}

	/* TODO: check if it's already finished or something.. */
		if (cgi->num_pending_writes_to_peer > CGI_PENDING_IO_THRESHOLD)
		{
			/* suspend input watching */
			if (cgi->csck && hio_dev_sck_read(cgi->csck, 0) <= -1) return -1;
		}
	}
	return 0;
}

static HIO_INLINE void cgi_mark_over (cgi_t* cgi, int over_bits)
{
	hio_svc_htts_t* htts = cgi->htts;
	hio_t* hio = htts->hio;
	unsigned int old_over;

	old_over = cgi->over;
	cgi->over |= over_bits;

    HIO_DEBUG8 (hio, "HTTS(%p) - cgi(t=%p,c=%p[%d],p=%p) - old_over=%x | new-bits=%x => over=%x\n", cgi->htts, cgi, cgi->client, (cgi->csck? cgi->csck->hnd: -1), cgi->peer, (int)old_over, (int)over_bits, (int)cgi->over);

	if (!(old_over & CGI_OVER_READ_FROM_CLIENT) && (cgi->over & CGI_OVER_READ_FROM_CLIENT))
	{
		if (cgi->csck && hio_dev_sck_read(cgi->csck, 0) <= -1)
		{
			HIO_DEBUG5 (hio, "HTTS(%p) - cgi(t=%p,c=%p[%d],p=%p) - halting client for failure to disable input watching\n", cgi->htts, cgi, cgi->client, (cgi->csck? cgi->csck->hnd: -1), cgi->peer);
			hio_dev_sck_halt (cgi->csck);
		}
	}

	if (!(old_over & CGI_OVER_READ_FROM_PEER) && (cgi->over & CGI_OVER_READ_FROM_PEER))
	{
		if (cgi->peer && hio_dev_pro_read(cgi->peer, HIO_DEV_PRO_OUT, 0) <= -1)
		{
			HIO_DEBUG5 (hio, "HTTS(%p) - cgi(t=%p,c=%p[%d],p=%p) - halting peer for failure to disable input watching\n", cgi->htts, cgi, cgi->client, (cgi->csck? cgi->csck->hnd: -1), cgi->peer);
			hio_dev_pro_halt (cgi->peer);
		}
	}

	if (old_over != CGI_OVER_ALL && cgi->over == CGI_OVER_ALL)
	{
		/* ready to stop */
		if (cgi->peer)
		{
			HIO_DEBUG5 (hio, "HTTS(%p) - cgi(t=%p,c=%p[%d],p=%p) - halting unneeded peer\n", cgi->htts, cgi, cgi->client, (cgi->csck? cgi->csck->hnd: -1), cgi->peer);
			hio_dev_pro_halt (cgi->peer);
		}

		if (cgi->csck)
		{
			HIO_ASSERT (hio, cgi->client != HIO_NULL);

			if (cgi->keep_alive && !cgi->client_eof_detected)
			{
				/* how to arrange to delete this cgi object and put the socket back to the normal waiting state??? */
				HIO_ASSERT (cgi->htts->hio, cgi->client->task == (hio_svc_htts_task_t*)cgi);

				HIO_SVC_HTTS_TASK_UNREF (cgi->client->task);
				/* cgi must not be accessed from here down as it could have been destroyed */
			}
			else
			{
				HIO_DEBUG5 (hio, "HTTS(%p) - cgi(t=%p,c=%p[%d],p=%p) - halting peer for no keep-alive\n", cgi->htts, cgi, cgi->client, (cgi->csck? cgi->csck->hnd: -1), cgi->peer);
				hio_dev_sck_shutdown (cgi->csck, HIO_DEV_SCK_SHUTDOWN_WRITE);
				hio_dev_sck_halt (cgi->csck);
			}
		}
	}
}

static void cgi_on_kill (hio_svc_htts_task_t* task)
{
	cgi_t* cgi = (cgi_t*)task;
	hio_t* hio = cgi->htts->hio;

	HIO_DEBUG5 (hio, "HTTS(%p) - cgi(t=%p,c=%p[%d],p=%p) - killing the task\n", cgi->htts, cgi, cgi->client, (cgi->csck? cgi->csck->hnd: -1), cgi->peer);

	if (cgi->peer)
	{
		cgi_peer_xtn_t* peer = hio_dev_pro_getxtn(cgi->peer);
		peer->cgi = HIO_NULL;  /* peer->cgi many not be NULL if the resource is killed regardless of the reference count */

		hio_dev_pro_kill (cgi->peer);
		cgi->peer = HIO_NULL;
	}

	if (cgi->peer_htrd)
	{
		cgi_peer_xtn_t* peer = hio_htrd_getxtn(cgi->peer_htrd);
		peer->cgi = HIO_NULL; /* peer->cgi many not be NULL if the resource is killed regardless of the reference count */

		hio_htrd_close (cgi->peer_htrd);
		cgi->peer_htrd = HIO_NULL;
	}

	if (cgi->csck)
	{
		HIO_ASSERT (hio, cgi->client != HIO_NULL);

		if (cgi->client_org_on_read) cgi->csck->on_read = cgi->client_org_on_read;
		if (cgi->client_org_on_write) cgi->csck->on_write = cgi->client_org_on_write;
		if (cgi->client_org_on_disconnect) cgi->csck->on_disconnect = cgi->client_org_on_disconnect;
		if (cgi->client_htrd_recbs_changed) hio_htrd_setrecbs (cgi->client->htrd, &cgi->client_htrd_org_recbs);

		if (!cgi->client_disconnected)
		{
			if (!cgi->keep_alive || hio_dev_sck_read(cgi->csck, 1) <= -1)
			{
				HIO_DEBUG5 (hio, "HTTS(%p) - cgi(t=%p,c=%p[%d],p=%p) - halting client for failure to enable input watching\n", cgi->htts, cgi, cgi->client, (cgi->csck? cgi->csck->hnd: -1), cgi->peer);
				hio_dev_sck_halt (cgi->csck);
			}
		}

		cgi->client_org_on_read = HIO_NULL;
		cgi->client_org_on_write = HIO_NULL;
		cgi->client_org_on_disconnect = HIO_NULL;
		cgi->client_htrd_recbs_changed = 0;
	}

	if (cgi->task_next) HIO_SVC_HTTS_TASKL_UNLINK_TASK (cgi); /* detach from the htts service only if it's attached */
	HIO_DEBUG5 (hio, "HTTS(%p) - thr(t=%p,c=%p[%d],p=%p) - killed the task\n", cgi->htts, cgi, cgi->client, (cgi->csck? cgi->csck->hnd: -1), cgi->peer);
}

static void cgi_peer_on_close (hio_dev_pro_t* pro, hio_dev_pro_sid_t sid)
{
	hio_t* hio = pro->hio;
	cgi_peer_xtn_t* peer_xtn = hio_dev_pro_getxtn(pro);
	cgi_t* cgi = peer_xtn->cgi;

	if (!cgi) return; /* cgi state already gone */

	switch (sid)
	{
		case HIO_DEV_PRO_MASTER:
			HIO_DEBUG3 (hio, "HTTS(%p) - peer %p(pid=%d) closing master\n", cgi->htts, pro, (int)pro->child_pid);
			cgi->peer = HIO_NULL; /* clear this peer from the state */

			HIO_ASSERT (hio, peer_xtn->cgi != HIO_NULL);
			HIO_SVC_HTTS_TASK_UNREF (peer_xtn->cgi);

			if (cgi->peer_htrd)
			{
				/* once this peer device is closed, peer's htrd is also never used.
				 * it's safe to detach the extra information attached on the htrd object. */
				peer_xtn = hio_htrd_getxtn(cgi->peer_htrd);
				HIO_ASSERT (hio, peer_xtn->cgi != HIO_NULL);
				HIO_SVC_HTTS_TASK_UNREF (peer_xtn->cgi);
			}

			break;

		case HIO_DEV_PRO_OUT:
			HIO_ASSERT (hio, cgi->peer == pro);
			HIO_DEBUG4 (hio, "HTTS(%p) - peer %p(pid=%d) closing slave[%d]\n", cgi->htts, pro, (int)pro->child_pid, sid);

			if (!(cgi->over & CGI_OVER_READ_FROM_PEER))
			{
				if (cgi_write_last_chunk_to_client(cgi) <= -1)
					cgi_halt_participating_devices (cgi);
				else
					cgi_mark_over (cgi, CGI_OVER_READ_FROM_PEER);
			}
			break;

		case HIO_DEV_PRO_IN:
			HIO_DEBUG4 (hio, "HTTS(%p) - peer %p(pid=%d) closing slave[%d]\n", cgi->htts, pro, (int)pro->child_pid, sid);
			cgi_mark_over (cgi, CGI_OVER_WRITE_TO_PEER);
			break;

		case HIO_DEV_PRO_ERR:
		default:
			HIO_DEBUG4 (hio, "HTTS(%p) - peer %p(pid=%d) closing slave[%d]\n", cgi->htts, pro, (int)pro->child_pid, sid);
			/* do nothing */
			break;
	}
}

static int cgi_peer_on_read (hio_dev_pro_t* pro, hio_dev_pro_sid_t sid, const void* data, hio_iolen_t dlen)
{
	hio_t* hio = pro->hio;
	cgi_peer_xtn_t* peer = hio_dev_pro_getxtn(pro);
	cgi_t* cgi = peer->cgi;

	HIO_ASSERT (hio, sid == HIO_DEV_PRO_OUT); /* since HIO_DEV_PRO_ERRTONUL is used, there should be no input from HIO_DEV_PRO_ERR */
	HIO_ASSERT (hio, cgi != HIO_NULL);

	if (dlen <= -1)
	{
		HIO_DEBUG3 (hio, "HTTS(%p) - read error from peer %p(pid=%u)\n", cgi->htts, pro, (unsigned int)pro->child_pid);
		goto oops;
	}

	if (dlen == 0)
	{
		HIO_DEBUG3 (hio, "HTTS(%p) - EOF from peer %p(pid=%u)\n", cgi->htts, pro, (unsigned int)pro->child_pid);

		if (!(cgi->over & CGI_OVER_READ_FROM_PEER))
		{
			int n;
			/* the cgi script could be misbehaving.
			 * it still has to read more but EOF is read.
			 * otherwise peer_htrd_poke() should have been called */
			n = cgi_write_last_chunk_to_client(cgi);
			cgi_mark_over (cgi, CGI_OVER_READ_FROM_PEER);
			if (n <= -1) goto oops;
		}
	}
	else
	{
		hio_oow_t rem;

		HIO_ASSERT (hio, !(cgi->over & CGI_OVER_READ_FROM_PEER));

		if (hio_htrd_feed(cgi->peer_htrd, data, dlen, &rem) <= -1)
		{
			HIO_DEBUG3 (hio, "HTTS(%p) - unable to feed peer htrd - peer %p(pid=%u)\n", cgi->htts, pro, (unsigned int)pro->child_pid);

			if (!cgi->ever_attempted_to_write_to_client &&
			    !(cgi->over & CGI_OVER_WRITE_TO_CLIENT))
			{
				cgi_send_final_status_to_client (cgi, HIO_HTTP_STATUS_INTERNAL_SERVER_ERROR, 1); /* don't care about error because it jumps to oops below anyway */
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
	cgi_halt_participating_devices (cgi);
	return 0;
}

static int peer_capture_response_header (hio_htre_t* req, const hio_bch_t* key, const hio_htre_hdrval_t* val, void* ctx)
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

static int peer_htrd_peek (hio_htrd_t* htrd, hio_htre_t* req)
{
	cgi_peer_xtn_t* peer = hio_htrd_getxtn(htrd);
	cgi_t* cgi = peer->cgi;
	hio_svc_htts_cli_t* cli = cgi->client;
	hio_bch_t dtbuf[64];
	int status_code = HIO_HTTP_STATUS_OK;

	if (req->attr.content_length)
	{
// TOOD: remove content_length if content_length is negative or not numeric.
		cgi->res_mode_to_cli = CGI_RES_MODE_LENGTH;
	}

	if (req->attr.status)
	{
		int is_sober;
		const hio_bch_t* endptr;
		hio_intmax_t v;

		v = hio_bchars_to_intmax(req->attr.status, hio_count_bcstr(req->attr.status), HIO_BCHARS_TO_INTMAX_MAKE_OPTION(0,0,0,10), &endptr, &is_sober);
		if (*endptr == '\0' && is_sober && v > 0 && v <= HIO_TYPE_MAX(int)) status_code = v;
	}

	hio_svc_htts_fmtgmtime (cli->htts, HIO_NULL, dtbuf, HIO_COUNTOF(dtbuf));

	if (hio_becs_fmt(cli->sbuf, "HTTP/%d.%d %d %hs\r\nServer: %hs\r\nDate: %hs\r\n",
		cgi->req_version.major, cgi->req_version.minor,
		status_code, hio_http_status_to_bcstr(status_code),
		cli->htts->server_name, dtbuf) == (hio_oow_t)-1) return -1;

	if (hio_htre_walkheaders(req, peer_capture_response_header, cli) <= -1) return -1;

	switch (cgi->res_mode_to_cli)
	{
		case CGI_RES_MODE_CHUNKED:
			if (hio_becs_cat(cli->sbuf, "Transfer-Encoding: chunked\r\n") == (hio_oow_t)-1) return -1;
			/*if (hio_becs_cat(cli->sbuf, "Connection: keep-alive\r\n") == (hio_oow_t)-1) return -1;*/
			break;

		case CGI_RES_MODE_CLOSE:
			if (hio_becs_cat(cli->sbuf, "Connection: close\r\n") == (hio_oow_t)-1) return -1;
			break;

		case CGI_RES_MODE_LENGTH:
			if (hio_becs_cat(cli->sbuf, (cgi->keep_alive? "Connection: keep-alive\r\n": "Connection: close\r\n")) == (hio_oow_t)-1) return -1;
	}

	if (hio_becs_cat(cli->sbuf, "\r\n") == (hio_oow_t)-1) return -1;

	return cgi_write_to_client(cgi, HIO_BECS_PTR(cli->sbuf), HIO_BECS_LEN(cli->sbuf));
}

static int peer_htrd_poke (hio_htrd_t* htrd, hio_htre_t* req)
{
	/* client request got completed */
	cgi_peer_xtn_t* peer = hio_htrd_getxtn(htrd);
	cgi_t* cgi = peer->cgi;

	if (cgi_write_last_chunk_to_client(cgi) <= -1) return -1;

	cgi_mark_over (cgi, CGI_OVER_READ_FROM_PEER);
	return 0;
}

static int peer_htrd_push_content (hio_htrd_t* htrd, hio_htre_t* req, const hio_bch_t* data, hio_oow_t dlen)
{
	cgi_peer_xtn_t* peer = hio_htrd_getxtn(htrd);
	cgi_t* cgi = peer->cgi;

	HIO_ASSERT (cgi->htts->hio, htrd == cgi->peer_htrd);

	switch (cgi->res_mode_to_cli)
	{
		case CGI_RES_MODE_CHUNKED:
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

			if (cgi_writev_to_client(cgi, iov, HIO_COUNTOF(iov)) <= -1) goto oops;
			break;
		}

		case CGI_RES_MODE_CLOSE:
		case CGI_RES_MODE_LENGTH:
			if (cgi_write_to_client(cgi, data, dlen) <= -1) goto oops;
			break;
	}

	if (cgi->num_pending_writes_to_client > CGI_PENDING_IO_THRESHOLD)
	{
		if (hio_dev_pro_read(cgi->peer, HIO_DEV_PRO_OUT, 0) <= -1) goto oops;
	}

	return 0;

oops:
	return -1;
}

static hio_htrd_recbs_t peer_htrd_recbs =
{
	peer_htrd_peek,
	peer_htrd_poke,
	peer_htrd_push_content
};

static int cgi_client_htrd_poke (hio_htrd_t* htrd, hio_htre_t* req)
{
	/* client request got completed */
	hio_svc_htts_cli_htrd_xtn_t* htrdxtn = (hio_svc_htts_cli_htrd_xtn_t*)hio_htrd_getxtn(htrd);
	hio_dev_sck_t* sck = htrdxtn->sck;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	cgi_t* cgi = (cgi_t*)cli->task;

	/* indicate EOF to the client peer */
	if (cgi_write_to_peer(cgi, HIO_NULL, 0) <= -1) return -1;

	cgi_mark_over (cgi, CGI_OVER_READ_FROM_CLIENT);
	return 0;
}

static int cgi_client_htrd_push_content (hio_htrd_t* htrd, hio_htre_t* req, const hio_bch_t* data, hio_oow_t dlen)
{
	hio_svc_htts_cli_htrd_xtn_t* htrdxtn = (hio_svc_htts_cli_htrd_xtn_t*)hio_htrd_getxtn(htrd);
	hio_dev_sck_t* sck = htrdxtn->sck;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	cgi_t* cgi = (cgi_t*)cli->task;

	HIO_ASSERT (sck->hio, cli->sck == sck);
	return cgi_write_to_peer(cgi, data, dlen);
}

static hio_htrd_recbs_t cgi_client_htrd_recbs =
{
	HIO_NULL, /* this shall be set to an actual peer handler before hio_htrd_setrecbs() */
	cgi_client_htrd_poke,
	cgi_client_htrd_push_content
};

static int cgi_peer_on_write (hio_dev_pro_t* pro, hio_iolen_t wrlen, void* wrctx)
{
	hio_t* hio = pro->hio;
	cgi_peer_xtn_t* peer = hio_dev_pro_getxtn(pro);
	cgi_t* cgi = peer->cgi;

	if (cgi == HIO_NULL) return 0; /* there is nothing i can do. the cgi is being cleared or has been cleared already. */

	HIO_ASSERT (hio, cgi->peer == pro);

	if (wrlen <= -1)
	{
		HIO_DEBUG3 (hio, "HTTS(%p) - unable to write to peer %p(pid=%u)\n", cgi->htts, pro, (int)pro->child_pid);
		goto oops;
	}
	else if (wrlen == 0)
	{
		/* indicated EOF */
		/* do nothing here as i didn't increment num_pending_writes_to_peer when making the write request */

		cgi->num_pending_writes_to_peer--;
		HIO_ASSERT (hio, cgi->num_pending_writes_to_peer == 0);
		HIO_DEBUG3 (hio, "HTTS(%p) - indicated EOF to peer %p(pid=%u)\n", cgi->htts, pro, (int)pro->child_pid);
		/* indicated EOF to the peer side. i need no more data from the client side.
		 * i don't need to enable input watching in the client side either */
		cgi_mark_over (cgi, CGI_OVER_WRITE_TO_PEER);
	}
	else
	{
		HIO_ASSERT (hio, cgi->num_pending_writes_to_peer > 0);

		cgi->num_pending_writes_to_peer--;
		if (cgi->num_pending_writes_to_peer == CGI_PENDING_IO_THRESHOLD)
		{
			if (!(cgi->over & CGI_OVER_READ_FROM_CLIENT) &&
			    hio_dev_sck_read(cgi->csck, 1) <= -1) goto oops;
		}

		if ((cgi->over & CGI_OVER_READ_FROM_CLIENT) && cgi->num_pending_writes_to_peer <= 0)
		{
			cgi_mark_over (cgi, CGI_OVER_WRITE_TO_PEER);
		}
	}

	return 0;

oops:
	cgi_halt_participating_devices (cgi);
	return 0;
}

static void cgi_client_on_disconnect (hio_dev_sck_t* sck)
{
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	hio_svc_htts_t* htts = cli->htts;
	cgi_t* cgi = (cgi_t*)cli->task;
	hio_t* hio = sck->hio;

	HIO_ASSERT (hio, sck == cgi->csck);
	HIO_DEBUG4 (hio, "HTTS(%p) - cgi(t=%p,c=%p,csck=%p) - client socket disconnect notified\n", htts, cgi, cli, sck);

	cgi->client_disconnected = 1;
	cgi->csck = HIO_NULL;
	cgi->client = HIO_NULL;
	if (cgi->client_org_on_disconnect)
	{
		cgi->client_org_on_disconnect (sck);
		/* this original callback destroys the associated resource.
		 * cgi must not be accessed from here down */
	}

	HIO_DEBUG4 (hio, "HTTS(%p) - cgi(t=%p,c=%p,csck=%p) - client socket disconnect handled\n", htts, cgi, cli, sck);
	/* Note: after this callback, the actual device pointed to by 'sck' will be freed in the main loop. */
}

static int cgi_client_on_read (hio_dev_sck_t* sck, const void* buf, hio_iolen_t len, const hio_skad_t* srcaddr)
{
	hio_t* hio = sck->hio;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	cgi_t* cgi = (cgi_t*)cli->task;

	HIO_ASSERT (hio, sck == cli->sck);

	if (len <= -1)
	{
		/* read error */
		HIO_DEBUG2 (cli->htts->hio, "HTTS(%p) - read error on client %p(%d)\n", sck, (int)sck->hnd);
		goto oops;
	}

	if (!cgi->peer)
	{
		/* the peer is gone */
		goto oops; /* do what?  just return 0? */
	}

	if (len == 0)
	{
		/* EOF on the client side. arrange to close */
		HIO_DEBUG3 (hio, "HTTS(%p) - EOF from client %p(hnd=%d)\n", cgi->htts, sck, (int)sck->hnd);
		cgi->client_eof_detected = 1;

		if (!(cgi->over & CGI_OVER_READ_FROM_CLIENT)) /* if this is true, EOF is received without cgi_client_htrd_poke() */
		{
			int n;
			n = cgi_write_to_peer(cgi, HIO_NULL, 0);
			cgi_mark_over (cgi, CGI_OVER_READ_FROM_CLIENT);
			if (n <= -1) goto oops;
		}
	}
	else
	{
		hio_oow_t rem;

		HIO_ASSERT (hio, !(cgi->over & CGI_OVER_READ_FROM_CLIENT));

		if (hio_htrd_feed(cli->htrd, buf, len, &rem) <= -1) goto oops;

		if (rem > 0)
		{
			/* TODO store this to client buffer. once the current resource is completed, arrange to call on_read() with it */
			HIO_DEBUG3 (hio, "HTTS(%p) - excessive data after contents by cgi client %p(%d)\n", sck->hio, sck, (int)sck->hnd);
		}
	}

	return 0;

oops:
	cgi_halt_participating_devices (cgi);
	return 0;
}

static int cgi_client_on_write (hio_dev_sck_t* sck, hio_iolen_t wrlen, void* wrctx, const hio_skad_t* dstaddr)
{
	hio_t* hio = sck->hio;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	cgi_t* cgi = (cgi_t*)cli->task;

	if (wrlen <= -1)
	{
		HIO_DEBUG3 (hio, "HTTS(%p) - unable to write to client %p(%d)\n", sck->hio, sck, (int)sck->hnd);
		goto oops;
	}

	if (wrlen == 0)
	{
		/* if the connect is keep-alive, this part may not be called */
		cgi->num_pending_writes_to_client--;
		HIO_ASSERT (hio, cgi->num_pending_writes_to_client == 0);
		HIO_DEBUG3 (hio, "HTTS(%p) - indicated EOF to client %p(%d)\n", cgi->htts, sck, (int)sck->hnd);
		/* since EOF has been indicated to the client, it must not write to the client any further.
		 * this also means that i don't need any data from the peer side either.
		 * i don't need to enable input watching on the peer side */
		cgi_mark_over (cgi, CGI_OVER_WRITE_TO_CLIENT);
	}
	else
	{
		HIO_ASSERT (hio, cgi->num_pending_writes_to_client > 0);

		cgi->num_pending_writes_to_client--;
		if (cgi->peer && cgi->num_pending_writes_to_client == CGI_PENDING_IO_THRESHOLD)
		{
			if (!(cgi->over & CGI_OVER_READ_FROM_PEER) &&
			    hio_dev_pro_read(cgi->peer, HIO_DEV_PRO_OUT, 1) <= -1) goto oops;
		}

		if ((cgi->over & CGI_OVER_READ_FROM_PEER) && cgi->num_pending_writes_to_client <= 0)
		{
			cgi_mark_over (cgi, CGI_OVER_WRITE_TO_CLIENT);
		}
	}

	return 0;

oops:
	cgi_halt_participating_devices (cgi);
	return 0;
}

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

static int cgi_peer_on_fork (hio_dev_pro_t* pro, void* fork_ctx)
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

	setenv ("GATEWAY_INTERFACE", "CGI/1.1", 1);

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
		/* [NOTE] trailers are not available when this cgi resource is started. let's not call hio_htre_walktrailers() */
		hio_becs_fini (&dbuf);
	}

	return 0;
}

int hio_svc_htts_docgi (hio_svc_htts_t* htts, hio_dev_sck_t* csck, hio_htre_t* req, const hio_bch_t* docroot, const hio_bch_t* script, int options)
{
	hio_t* hio = htts->hio;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(csck);
	cgi_t* cgi = HIO_NULL;
	cgi_peer_xtn_t* peer_xtn;
	hio_dev_pro_make_t mi;
	peer_fork_ctx_t fc;

	/* ensure that you call this function before any contents is received */
	HIO_ASSERT (hio, hio_htre_getcontentlen(req) == 0);

	HIO_MEMSET (&fc, 0, HIO_SIZEOF(fc));
	fc.cli = cli;
	fc.req = req;
	fc.docroot = docroot;
	fc.script = script;
	fc.actual_script = hio_svc_htts_dupmergepaths(htts, docroot, script);
	if (!fc.actual_script) goto oops;

	HIO_MEMSET (&mi, 0, HIO_SIZEOF(mi));
	mi.flags = HIO_DEV_PRO_READOUT | HIO_DEV_PRO_ERRTONUL | HIO_DEV_PRO_WRITEIN /*| HIO_DEV_PRO_FORGET_CHILD*/;
	mi.cmd = fc.actual_script;
	mi.on_read = cgi_peer_on_read;
	mi.on_write = cgi_peer_on_write;
	mi.on_close = cgi_peer_on_close;
	mi.on_fork = cgi_peer_on_fork;
	mi.fork_ctx = &fc;

	cgi = (cgi_t*)hio_svc_htts_task_make(htts, HIO_SIZEOF(*cgi), cgi_on_kill);
	if (HIO_UNLIKELY(!cgi)) goto oops;

	cgi->options = options;
	cgi->csck = csck;
	cgi->client = cli;
	/*cgi->num_pending_writes_to_client = 0;
	cgi->num_pending_writes_to_peer = 0;*/
	cgi->req_method = hio_htre_getqmethodtype(req);
	cgi->req_version = *hio_htre_getversion(req);
	cgi->req_content_length_unlimited = hio_htre_getreqcontentlen(req, &cgi->req_content_length);

	/* remember the client socket's io event handlers */
	cgi->client_org_on_read = csck->on_read;
	cgi->client_org_on_write = csck->on_write;
	cgi->client_org_on_disconnect = csck->on_disconnect;
	/* set new io events handlers on the client socket */
	csck->on_read = cgi_client_on_read;
	csck->on_write = cgi_client_on_write;
	csck->on_disconnect = cgi_client_on_disconnect;

	HIO_ASSERT (hio, cli->task == HIO_NULL);
	HIO_SVC_HTTS_TASK_REF ((hio_svc_htts_task_t*)cgi, cli->task); /* cli->task = cgi */

	if (access(mi.cmd, X_OK) == -1)
	{
		cgi_send_final_status_to_client (cgi, HIO_HTTP_STATUS_FORBIDDEN, 1); /* 403 Forbidden */
		goto oops; /* TODO: must not go to oops.  just destroy the cgi and finalize the request .. */
	}

	cgi->peer = hio_dev_pro_make(hio, HIO_SIZEOF(*peer_xtn), &mi);
	if (HIO_UNLIKELY(!cgi->peer)) goto oops;
	peer_xtn = hio_dev_pro_getxtn(cgi->peer);
	HIO_SVC_HTTS_TASK_REF (cgi, peer_xtn->cgi); /* peer->cgi in pro = cgi */

	cgi->peer_htrd = hio_htrd_open(hio, HIO_SIZEOF(*peer_xtn));
	if (HIO_UNLIKELY(!cgi->peer_htrd)) goto oops;
	hio_htrd_setoption (cgi->peer_htrd, HIO_HTRD_SKIP_INITIAL_LINE | HIO_HTRD_RESPONSE);
	hio_htrd_setrecbs (cgi->peer_htrd, &peer_htrd_recbs);

	peer_xtn = hio_htrd_getxtn(cgi->peer_htrd);
	HIO_SVC_HTTS_TASK_REF (cgi, peer_xtn->cgi); /* peer->cgi in htrd = cgi */

#if !defined(CGI_ALLOW_UNLIMITED_REQ_CONTENT_LENGTH)
	if (cgi->req_content_length_unlimited)
	{
		/* Transfer-Encoding is chunked. no content-length is known in advance. */

		/* option 1. buffer contents. if it gets too large, send 413 Request Entity Too Large.
		 * option 2. send 411 Length Required immediately
		 * option 3. set Content-Length to -1 and use EOF to indicate the end of content [Non-Standard] */

		if (cgi_send_final_status_to_client(cgi, HIO_HTTP_STATUS_LENGTH_REQUIRED, 1) <= -1) goto oops;
	}
#endif

	if (req->flags & HIO_HTRE_ATTR_EXPECT100)
	{
		/* TODO: Expect: 100-continue? who should handle this? cgi? or the http server? */
		/* CAN I LET the cgi SCRIPT handle this? */
		if (!(options & HIO_SVC_HTTS_CGI_NO_100_CONTINUE) &&
		    hio_comp_http_version_numbers(&req->version, 1, 1) >= 0 &&
		   (cgi->req_content_length_unlimited || cgi->req_content_length > 0))
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

			msglen = hio_fmttobcstr(hio, msgbuf, HIO_COUNTOF(msgbuf), "HTTP/%d.%d %d %hs\r\n\r\n", cgi->req_version.major, cgi->req_version.minor, HIO_HTTP_STATUS_CONTINUE, hio_http_status_to_bcstr(HIO_HTTP_STATUS_CONTINUE));
			if (cgi_write_to_client(cgi, msgbuf, msglen) <= -1) goto oops;
			cgi->ever_attempted_to_write_to_client = 0; /* reset this as it's polluted for 100 continue */
		}
	}
	else if (req->flags & HIO_HTRE_ATTR_EXPECT)
	{
		/* 417 Expectation Failed */
		cgi_send_final_status_to_client(cgi, HIO_HTTP_STATUS_EXPECTATION_FAILED, 1);
		goto oops;
	}

#if defined(CGI_ALLOW_UNLIMITED_REQ_CONTENT_LENGTH)
	if (cgi->req_content_length_unlimited)
	{
		/* change the callbacks to subscribe to contents to be uploaded */
		cgi->client_htrd_org_recbs = *hio_htrd_getrecbs(cgi->client->htrd);
		cgi_client_htrd_recbs.peek = cgi->client_htrd_org_recbs.peek;
		hio_htrd_setrecbs (cgi->client->htrd, &cgi_client_htrd_recbs);
		cgi->client_htrd_recbs_changed = 1;
	}
	else
	{
#endif
		if (cgi->req_content_length > 0)
		{
			/* change the callbacks to subscribe to contents to be uploaded */
			cgi->client_htrd_org_recbs = *hio_htrd_getrecbs(cgi->client->htrd);
			cgi_client_htrd_recbs.peek = cgi->client_htrd_org_recbs.peek;
			hio_htrd_setrecbs (cgi->client->htrd, &cgi_client_htrd_recbs);
			cgi->client_htrd_recbs_changed = 1;
		}
		else
		{
			/* no content to be uploaded from the client */
			/* indicate EOF to the peer and disable input wathching from the client */
			if (cgi_write_to_peer(cgi, HIO_NULL, 0) <= -1) goto oops;
			cgi_mark_over (cgi, CGI_OVER_READ_FROM_CLIENT | CGI_OVER_WRITE_TO_PEER);
		}
#if defined(CGI_ALLOW_UNLIMITED_REQ_CONTENT_LENGTH)
	}
#endif

	/* this may change later if Content-Length is included in the cgi output */
	if (req->flags & HIO_HTRE_ATTR_KEEPALIVE)
	{
		cgi->keep_alive = 1;
		cgi->res_mode_to_cli = CGI_RES_MODE_CHUNKED;
		/* the mode still can get switched to CGI_RES_MODE_LENGTH if the cgi script emits Content-Length */
	}
	else
	{
		cgi->keep_alive = 0;
		cgi->res_mode_to_cli = CGI_RES_MODE_CLOSE;
	}

	/* TODO: store current input watching state and use it when destroying the cgi data */
	if (hio_dev_sck_read(csck, !(cgi->over & CGI_OVER_READ_FROM_CLIENT)) <= -1) goto oops;
	hio_freemem (hio, fc.actual_script);

	HIO_SVC_HTTS_TASKL_APPEND_TASK (&htts->task, (hio_svc_htts_task_t*)cgi);
	return 0;

oops:
	HIO_DEBUG2 (hio, "HTTS(%p) - FAILURE in docgi - socket(%p)\n", htts, csck);
	if (cgi) cgi_halt_participating_devices (cgi);
	if (fc.actual_script) hio_freemem (hio, fc.actual_script);
	return -1;
}
