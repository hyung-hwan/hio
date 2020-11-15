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
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#define FILE_ALLOW_UNLIMITED_REQ_CONTENT_LENGTH

enum file_state_res_mode_t
{
	FILE_STATE_RES_MODE_CLOSE,
	FILE_STATE_RES_MODE_LENGTH
};
typedef enum file_state_res_mode_t file_state_res_mode_t;

#define FILE_STATE_PENDING_IO_THRESHOLD 5

#define FILE_STATE_OVER_READ_FROM_CLIENT (1 << 0)
#define FILE_STATE_OVER_READ_FROM_PEER   (1 << 1)
#define FILE_STATE_OVER_WRITE_TO_CLIENT  (1 << 2)
#define FILE_STATE_OVER_WRITE_TO_PEER    (1 << 3)
#define FILE_STATE_OVER_ALL (FILE_STATE_OVER_READ_FROM_CLIENT | FILE_STATE_OVER_READ_FROM_PEER | FILE_STATE_OVER_WRITE_TO_CLIENT | FILE_STATE_OVER_WRITE_TO_PEER)

struct file_state_t
{
	MIO_SVC_HTTS_RSRC_HEADER;

	mio_oow_t num_pending_writes_to_client;
	mio_oow_t num_pending_writes_to_peer;

	int sendfile_ok;
	int peer;
	mio_foff_t total_size;
	mio_foff_t start_offset;
	mio_foff_t end_offset;
	mio_foff_t cur_offset;
	mio_bch_t peer_buf[8192];
	mio_tmridx_t peer_tmridx;
	mio_bch_t peer_etag[128];

	mio_svc_htts_cli_t* client;
	mio_http_version_t req_version; /* client request */
	mio_http_method_t req_method;

	unsigned int over: 4; /* must be large enough to accomodate FILE_STATE_OVER_ALL */
	unsigned int keep_alive: 1;
	unsigned int req_content_length_unlimited: 1;
	unsigned int ever_attempted_to_write_to_client: 1;
	unsigned int client_disconnected: 1;
	unsigned int client_htrd_recbs_changed: 1;
	unsigned int etag_match: 1;
	mio_oow_t req_content_length; /* client request content length */
	file_state_res_mode_t res_mode_to_cli;

	mio_dev_sck_on_read_t client_org_on_read;
	mio_dev_sck_on_write_t client_org_on_write;
	mio_dev_sck_on_disconnect_t client_org_on_disconnect;
	mio_htrd_recbs_t client_htrd_org_recbs;

};
typedef struct file_state_t file_state_t;

static int file_state_send_contents_to_client (file_state_t* file_state);


static void file_state_halt_participating_devices (file_state_t* file_state)
{
	MIO_ASSERT (file_state->client->htts->mio, file_state->client != MIO_NULL);
	MIO_ASSERT (file_state->client->htts->mio, file_state->client->sck != MIO_NULL);

	MIO_DEBUG4 (file_state->client->htts->mio, "HTTS(%p) - Halting participating devices in file state %p(client=%p,peer=%d)\n", file_state->client->htts, file_state, file_state->client->sck, (int)file_state->peer);

	mio_dev_sck_halt (file_state->client->sck);
}

static int file_state_write_to_client (file_state_t* file_state, const void* data, mio_iolen_t dlen)
{
	file_state->ever_attempted_to_write_to_client = 1;

	file_state->num_pending_writes_to_client++;
	if (mio_dev_sck_write(file_state->client->sck, data, dlen, MIO_NULL, MIO_NULL) <= -1)  /* TODO: use sendfile here.. */
	{
		file_state->num_pending_writes_to_client--;
		return -1;
	}

	if (file_state->num_pending_writes_to_client > FILE_STATE_PENDING_IO_THRESHOLD)
	{
		/* STOP READING */
		/*if (mio_dev_pro_read(file_state->peer, MIO_DEV_PRO_OUT, 0) <= -1) return -1;*/
	}
	return 0;
}

static int file_state_sendfile_to_client (file_state_t* file_state, mio_foff_t foff, mio_iolen_t len)
{
	file_state->ever_attempted_to_write_to_client = 1;

	file_state->num_pending_writes_to_client++;
	if (mio_dev_sck_sendfile(file_state->client->sck, file_state->peer, foff, len, MIO_NULL) <= -1) 
	{
		file_state->num_pending_writes_to_client--;
		return -1;
	}

	if (file_state->num_pending_writes_to_client > FILE_STATE_PENDING_IO_THRESHOLD)
	{
		/* STOP READING */
		/*if (mio_dev_pro_read(file_state->peer, MIO_DEV_PRO_OUT, 0) <= -1) return -1;*/
	}
	return 0;
}

static int file_state_send_final_status_to_client (file_state_t* file_state, int status_code, int force_close)
{
	mio_svc_htts_cli_t* cli = file_state->client;
	mio_bch_t dtbuf[64];

	mio_svc_htts_fmtgmtime (cli->htts, MIO_NULL, dtbuf, MIO_COUNTOF(dtbuf));

	if (!force_close) force_close = !file_state->keep_alive;
	if (mio_becs_fmt(cli->sbuf, "HTTP/%d.%d %d %hs\r\nServer: %hs\r\nDate: %s\r\nConnection: %hs\r\nContent-Length: 0\r\n\r\n",
		file_state->req_version.major, file_state->req_version.minor,
		status_code, mio_http_status_to_bcstr(status_code),
		cli->htts->server_name, dtbuf,
		(force_close? "close": "keep-alive")) == (mio_oow_t)-1) return -1;

	return (file_state_write_to_client(file_state, MIO_BECS_PTR(cli->sbuf), MIO_BECS_LEN(cli->sbuf)) <= -1 ||
	        (force_close && file_state_write_to_client(file_state, MIO_NULL, 0) <= -1))? -1: 0;
}

static void file_state_close_peer (file_state_t* file_state)
{
	mio_t* mio = file_state->htts->mio;

	if (file_state->peer_tmridx != MIO_TMRIDX_INVALID)
	{
		mio_deltmrjob (mio, file_state->peer_tmridx);
		MIO_ASSERT (mio, file_state->peer_tmridx == MIO_TMRIDX_INVALID);
	}

	if (file_state->peer >= 0)
	{
		close (file_state->peer);
		file_state->peer = -1;
	}
}

static void file_state_mark_over (file_state_t* file_state, int over_bits)
{
	unsigned int old_over;

	old_over = file_state->over;
	file_state->over |= over_bits;

	MIO_DEBUG5 (file_state->htts->mio, "HTTS(%p) - client=%p peer=%p new-bits=%x over=%x\n", file_state->htts, file_state->client->sck, file_state->peer, (int)over_bits, (int)file_state->over);

	if (!(old_over & FILE_STATE_OVER_READ_FROM_CLIENT) && (file_state->over & FILE_STATE_OVER_READ_FROM_CLIENT))
	{
		if (mio_dev_sck_read(file_state->client->sck, 0) <= -1) 
		{
			MIO_DEBUG2 (file_state->htts->mio, "HTTS(%p) - halting client(%p) for failure to disable input watching\n", file_state->htts, file_state->client->sck);
			mio_dev_sck_halt (file_state->client->sck);
		}
	}

#if 0
	if (!(old_over & FILE_STATE_OVER_READ_FROM_PEER) && (file_state->over & FILE_STATE_OVER_READ_FROM_PEER))
	{
		/* there is no partial close... keep it open */
	}
#endif

	if (old_over != FILE_STATE_OVER_ALL && file_state->over == FILE_STATE_OVER_ALL)
	{
		/* ready to stop */
		MIO_DEBUG2 (file_state->htts->mio, "HTTS(%p) - halting peer(%p) as it is unneeded\n", file_state->htts, file_state->peer);
		file_state_close_peer (file_state);

		if (file_state->keep_alive) 
		{
		#if defined(TCP_CORK)
			int tcp_cork = 0;
			#if defined(SOL_TCP)
			mio_dev_sck_setsockopt(file_state->client->sck, SOL_TCP, TCP_CORK, &tcp_cork, MIO_SIZEOF(tcp_cork));
			#elif defined(IPPROTO_TCP)
			mio_dev_sck_setsockopt(file_state->client->sck, IPPROTO_TCP, TCP_CORK, &tcp_cork, MIO_SIZEOF(tcp_cork));
			#endif
		#endif

			/* how to arrange to delete this file_state object and put the socket back to the normal waiting state??? */
			MIO_ASSERT (file_state->htts->mio, file_state->client->rsrc == (mio_svc_htts_rsrc_t*)file_state);

			MIO_SVC_HTTS_RSRC_DETACH (file_state->client->rsrc);
			/* file_state must not be accessed from here down as it could have been destroyed */
		}
		else
		{
			MIO_DEBUG2 (file_state->htts->mio, "HTTS(%p) - halting client(%p) for no keep-alive\n", file_state->htts, file_state->client->sck);
			mio_dev_sck_shutdown (file_state->client->sck, MIO_DEV_SCK_SHUTDOWN_WRITE);
			mio_dev_sck_halt (file_state->client->sck);
		}
	}
}


static int file_state_write_to_peer (file_state_t* file_state, const void* data, mio_iolen_t dlen)
{
	mio_t* mio = file_state->htts->mio;

	if (dlen <= 0)
	{
		file_state_mark_over (file_state, FILE_STATE_OVER_WRITE_TO_PEER);
	}
	else
	{
		if (file_state->req_method == MIO_HTTP_GET) return 0; 

		MIO_ASSERT (mio, file_state->peer >= 0);
		return write(file_state->peer, data, dlen) <= -1? -1: 0;
	}

	return 0;
}

static void file_state_on_kill (file_state_t* file_state)
{
	mio_t* mio = file_state->htts->mio;

	MIO_DEBUG2 (mio, "HTTS(%p) - killing file client(%p)\n", file_state->htts, file_state->client->sck);

	file_state_close_peer (file_state);

	if (file_state->client_org_on_read)
	{
		file_state->client->sck->on_read = file_state->client_org_on_read;
		file_state->client_org_on_read = MIO_NULL;
	}

	if (file_state->client_org_on_write)
	{
		file_state->client->sck->on_write = file_state->client_org_on_write;
		file_state->client_org_on_write = MIO_NULL;
	}

	if (file_state->client_org_on_disconnect)
	{
		file_state->client->sck->on_disconnect = file_state->client_org_on_disconnect;
		file_state->client_org_on_disconnect = MIO_NULL;
	}

	if (file_state->client_htrd_recbs_changed)
	{
		/* restore the callbacks */
		mio_htrd_setrecbs (file_state->client->htrd, &file_state->client_htrd_org_recbs);
	}

	if (!file_state->client_disconnected)
	{
		if (!file_state->keep_alive || mio_dev_sck_read(file_state->client->sck, 1) <= -1)
		{
			MIO_DEBUG2 (mio, "HTTS(%p) - halting client(%p) for failure to enable input watching\n", file_state->htts, file_state->client->sck);
			mio_dev_sck_halt (file_state->client->sck);
		}
	}
}

static void file_client_on_disconnect (mio_dev_sck_t* sck)
{
	mio_svc_htts_cli_t* cli = mio_dev_sck_getxtn(sck);
	file_state_t* file_state = (file_state_t*)cli->rsrc;
	file_state->client_disconnected = 1;
	file_state->client_org_on_disconnect (sck);
}

static int file_client_on_read (mio_dev_sck_t* sck, const void* buf, mio_iolen_t len, const mio_skad_t* srcaddr)
{
	mio_t* mio = sck->mio;
	mio_svc_htts_cli_t* cli = mio_dev_sck_getxtn(sck);
	file_state_t* file_state = (file_state_t*)cli->rsrc;

	MIO_ASSERT (mio, sck == cli->sck);

	if (len <= -1)
	{
		/* read error */
		MIO_DEBUG2 (cli->htts->mio, "HTTPS(%p) - read error on client %p(%d)\n", sck, (int)sck->hnd);
		goto oops;
	}

	if (!file_state->peer)
	{
		/* the peer is gone */
		goto oops; /* do what?  just return 0? */
	}

	if (len == 0)
	{
		/* EOF on the client side. arrange to close */
		MIO_DEBUG3 (mio, "HTTPS(%p) - EOF from client %p(hnd=%d)\n", file_state->client->htts, sck, (int)sck->hnd);

		if (!(file_state->over & FILE_STATE_OVER_READ_FROM_CLIENT)) /* if this is true, EOF is received without file_client_htrd_poke() */
		{
			if (file_state_write_to_peer(file_state, MIO_NULL, 0) <= -1) goto oops;
			file_state_mark_over (file_state, FILE_STATE_OVER_READ_FROM_CLIENT);
		}
	}
	else
	{
		mio_oow_t rem;

		MIO_ASSERT (mio, !(file_state->over & FILE_STATE_OVER_READ_FROM_CLIENT));

		if (mio_htrd_feed(cli->htrd, buf, len, &rem) <= -1) goto oops;

		if (rem > 0)
		{
			/* TODO store this to client buffer. once the current resource is completed, arrange to call on_read() with it */
			MIO_DEBUG3 (mio, "HTTPS(%p) - excessive data after contents by file client %p(%d)\n", sck->mio, sck, (int)sck->hnd);
		}
	}

	return 0;

oops:
	file_state_halt_participating_devices (file_state);
	return 0;
}

static int file_client_on_write (mio_dev_sck_t* sck, mio_iolen_t wrlen, void* wrctx, const mio_skad_t* dstaddr)
{
	mio_t* mio = sck->mio;
	mio_svc_htts_cli_t* cli = mio_dev_sck_getxtn(sck);
	file_state_t* file_state = (file_state_t*)cli->rsrc;

	if (wrlen <= -1)
	{
		MIO_DEBUG3 (mio, "HTTPS(%p) - unable to write to client %p(%d)\n", sck->mio, sck, (int)sck->hnd);
		goto oops;
	}

	if (wrlen == 0)
	{
		/* if the connect is keep-alive, this part may not be called */
		file_state->num_pending_writes_to_client--;
		MIO_ASSERT (mio, file_state->num_pending_writes_to_client == 0);
		MIO_DEBUG3 (mio, "HTTS(%p) - indicated EOF to client %p(%d)\n", file_state->client->htts, sck, (int)sck->hnd);
		/* since EOF has been indicated to the client, it must not write to the client any further.
		 * this also means that i don't need any data from the peer side either.
		 * i don't need to enable input watching on the peer side */

		file_state_mark_over (file_state, FILE_STATE_OVER_WRITE_TO_CLIENT);
	}
	else
	{
		MIO_ASSERT (mio, file_state->num_pending_writes_to_client > 0);
		file_state->num_pending_writes_to_client--;

		if (file_state->req_method == MIO_HTTP_GET)
			file_state_send_contents_to_client (file_state);

		if ((file_state->over & FILE_STATE_OVER_READ_FROM_PEER) && file_state->num_pending_writes_to_client <= 0)
		{
			file_state_mark_over (file_state, FILE_STATE_OVER_WRITE_TO_CLIENT);
		}
	}

	return 0;

oops:
	file_state_halt_participating_devices (file_state);
	return 0;
}


/* --------------------------------------------------------------------- */

static int file_client_htrd_poke (mio_htrd_t* htrd, mio_htre_t* req)
{
	/* client request got completed */
	mio_svc_htts_cli_htrd_xtn_t* htrdxtn = (mio_svc_htts_cli_htrd_xtn_t*)mio_htrd_getxtn(htrd);
	mio_dev_sck_t* sck = htrdxtn->sck;
	mio_svc_htts_cli_t* cli = mio_dev_sck_getxtn(sck);
	file_state_t* file_state = (file_state_t*)cli->rsrc;

	/* indicate EOF to the client peer */
	if (file_state_write_to_peer(file_state, MIO_NULL, 0) <= -1) return -1;

	if (file_state->req_method != MIO_HTTP_GET)
	{
		if (file_state_send_final_status_to_client(file_state, 200, 0) <= -1) return -1;
	}

	file_state_mark_over (file_state, FILE_STATE_OVER_READ_FROM_CLIENT);
	return 0;
}

static int file_client_htrd_push_content (mio_htrd_t* htrd, mio_htre_t* req, const mio_bch_t* data, mio_oow_t dlen)
{
	mio_svc_htts_cli_htrd_xtn_t* htrdxtn = (mio_svc_htts_cli_htrd_xtn_t*)mio_htrd_getxtn(htrd);
	mio_dev_sck_t* sck = htrdxtn->sck;
	mio_svc_htts_cli_t* cli = mio_dev_sck_getxtn(sck);
	file_state_t* file_state = (file_state_t*)cli->rsrc;

	MIO_ASSERT (sck->mio, cli->sck == sck);
	return file_state_write_to_peer(file_state, data, dlen);
}

static mio_htrd_recbs_t file_client_htrd_recbs =
{
	MIO_NULL,
	file_client_htrd_poke,
	file_client_htrd_push_content
};

/* --------------------------------------------------------------------- */

static int file_state_send_header_to_client (file_state_t* file_state, int status_code, int force_close, const mio_bch_t* mime_type)
{
	mio_svc_htts_cli_t* cli = file_state->client;
	mio_bch_t dtbuf[64];
	mio_foff_t content_length;

	mio_svc_htts_fmtgmtime (cli->htts, MIO_NULL, dtbuf, MIO_COUNTOF(dtbuf));

	if (!force_close) force_close = !file_state->keep_alive;

	content_length = file_state->end_offset - file_state->start_offset + 1;
	if (status_code == 200 && file_state->total_size != content_length) status_code = 206;

	if (mio_becs_fmt(cli->sbuf, "HTTP/%d.%d %d %hs\r\nServer: %hs\r\nDate: %s\r\nConnection: %hs\r\nAccept-Ranges: bytes\r\nContent-Type: %hs\r\n",
		file_state->req_version.major, file_state->req_version.minor,
		status_code, mio_http_status_to_bcstr(status_code),
		cli->htts->server_name, dtbuf,
		(force_close? "close": "keep-alive"), mime_type) == (mio_oow_t)-1) return -1;

	if (file_state->req_method == MIO_HTTP_GET && mio_becs_fcat(cli->sbuf, "ETag: %hs\r\n", file_state->peer_etag) == (mio_oow_t)-1) return -1;
	if (status_code == 206 && mio_becs_fcat(cli->sbuf, "Content-Ranges: bytes %ju-%ju/%ju\r\n", (mio_uintmax_t)file_state->start_offset, (mio_uintmax_t)file_state->end_offset, (mio_uintmax_t)file_state->total_size) == (mio_oow_t)-1) return -1;

/* ----- */
// TODO: Allow-Contents
// Allow-Headers... support custom headers...
	if (mio_becs_fcat(cli->sbuf, "Access-Control-Allow-Origin: *\r\n", (mio_uintmax_t)content_length) == (mio_oow_t)-1) return -1;
/* ----- */

	if (mio_becs_fcat(cli->sbuf, "Content-Length: %ju\r\n\r\n", (mio_uintmax_t)content_length) == (mio_oow_t)-1) return -1;

	return file_state_write_to_client(file_state, MIO_BECS_PTR(cli->sbuf), MIO_BECS_LEN(cli->sbuf));
}

static void send_contents_to_client_later (mio_t* mio, const mio_ntime_t* now, mio_tmrjob_t* tmrjob)
{
	file_state_t* file_state = (file_state_t*)tmrjob->ctx;
	if (file_state_send_contents_to_client(file_state) <= -1)
	{
		file_state_halt_participating_devices (file_state);
	}
}

static int file_state_send_contents_to_client (file_state_t* file_state)
{
	mio_t* mio = file_state->htts->mio;
	mio_foff_t lim;

	if (file_state->cur_offset > file_state->end_offset)
	{
		/* reached the end */
		file_state_mark_over (file_state, FILE_STATE_OVER_READ_FROM_PEER);
		return 0;
	}

	lim = file_state->end_offset - file_state->cur_offset + 1;
	if (file_state->sendfile_ok)
	{
		if (lim > 0x7FFF0000) lim = 0x7FFF0000; /* TODO: change this... */
		if (file_state_sendfile_to_client(file_state, file_state->cur_offset, lim) <= -1) return -1;
		file_state->cur_offset += lim;
	}
	else
	{
		ssize_t n;

		n = read(file_state->peer, file_state->peer_buf, (lim < MIO_SIZEOF(file_state->peer_buf)? lim: MIO_SIZEOF(file_state->peer_buf)));
		if (n == -1)
		{
			if ((errno == EAGAIN || errno == EINTR) && file_state->peer_tmridx == MIO_TMRIDX_INVALID)
			{
				mio_tmrjob_t tmrjob;
				/* use a timer job for a new sending attempt */
				MIO_MEMSET (&tmrjob, 0, MIO_SIZEOF(tmrjob));
				tmrjob.ctx = file_state;
				/*tmrjob.when = leave it at 0 for immediate firing.*/
				tmrjob.handler = send_contents_to_client_later;
				tmrjob.idxptr = &file_state->peer_tmridx;
				return mio_instmrjob(mio, &tmrjob) == MIO_TMRIDX_INVALID? -1: 0;
			}

			return -1;
		}
		else if (n == 0)
		{
			/* no more data to read - this must not happend unless file size changed while the file is open. */
	/* TODO: I probably must close the connection by force??? */
			file_state_mark_over (file_state, FILE_STATE_OVER_READ_FROM_PEER); 
			return -1;
		}

		if (file_state_write_to_client(file_state, file_state->peer_buf, n) <= -1) return -1;

		file_state->cur_offset += n;

	/*	if (file_state->cur_offset > file_state->end_offset)  should i check this or wait until this function is invoked?
			file_state_mark_over (file_state, FILE_STATE_OVER_READ_FROM_PEER);*/
	}

	return 0;
}

static MIO_INLINE int process_range_header (file_state_t* file_state, mio_htre_t* req)
{
	struct stat st;
	const mio_htre_hdrval_t* tmp;
	mio_oow_t etag_len;

	if (fstat(file_state->peer, &st) <= -1) 
	{
		file_state_send_final_status_to_client (file_state, 500, 1);
		return -1;
	}

	if ((st.st_mode & S_IFMT) != S_IFREG)
	{
		/* TODO: support directory listing if S_IFDIR? still disallow special files. */
		file_state_send_final_status_to_client (file_state, 403, 1); /* forbidden */
		return -1;
	}

	if (file_state->req_method == MIO_HTTP_GET)
	{
		etag_len = mio_fmt_uintmax_to_bcstr(&file_state->peer_etag[0], MIO_COUNTOF(file_state->peer_etag), st.st_mtim.tv_sec, 16, -1, '\0', MIO_NULL);
		file_state->peer_etag[etag_len++] = '-';
		etag_len += mio_fmt_uintmax_to_bcstr(&file_state->peer_etag[etag_len], MIO_COUNTOF(file_state->peer_etag), st.st_mtim.tv_nsec, 16, -1, '\0', MIO_NULL);
		file_state->peer_etag[etag_len++] = '-';
		etag_len += mio_fmt_uintmax_to_bcstr(&file_state->peer_etag[etag_len], MIO_COUNTOF(file_state->peer_etag) - etag_len, st.st_size, 16, -1, '\0', MIO_NULL);
		file_state->peer_etag[etag_len++] = '-';
		etag_len += mio_fmt_uintmax_to_bcstr(&file_state->peer_etag[etag_len], MIO_COUNTOF(file_state->peer_etag) - etag_len, st.st_ino, 16, -1, '\0', MIO_NULL);
		file_state->peer_etag[etag_len++] = '-';
		mio_fmt_uintmax_to_bcstr (&file_state->peer_etag[etag_len], MIO_COUNTOF(file_state->peer_etag) - etag_len, st.st_dev, 16, -1, '\0', MIO_NULL);

		tmp = mio_htre_getheaderval(req, "If-None-Match");
		if (tmp && mio_comp_bcstr(file_state->peer_etag, tmp->ptr, 0) == 0) file_state->etag_match = 1;
	}
	file_state->end_offset = st.st_size;

	tmp = mio_htre_getheaderval(req, "Range"); /* TODO: support multiple ranges? */
	if (tmp)
	{
		mio_http_range_t range;

		if (mio_parse_http_range_bcstr(tmp->ptr, &range) <= -1)
		{
		range_not_satisifiable:
			file_state_send_final_status_to_client (file_state, 416, 1); /* 406 Requested Range Not Satisfiable */
			return -1;
		}

		switch (range.type)
		{
			case MIO_HTTP_RANGE_PROPER:
				/* Range XXXX-YYYY */
				if (range.to >= st.st_size) goto range_not_satisifiable;
				file_state->start_offset = range.from;
				file_state->end_offset = range.to;
				break;

			case MIO_HTTP_RANGE_PREFIX:
				/* Range: XXXX- */
				if (range.from >= st.st_size) goto range_not_satisifiable;
				file_state->start_offset = range.from;
				file_state->end_offset = st.st_size - 1;
				break;

			case MIO_HTTP_RANGE_SUFFIX:
				/* Range: -XXXX */
				if (range.to >= st.st_size) goto range_not_satisifiable;
				file_state->start_offset = st.st_size - range.to;
				file_state->end_offset = st.st_size - 1;
				break;
		}

		if (file_state->start_offset > 0) 
			lseek(file_state->peer, file_state->start_offset, SEEK_SET);

	}
	else
	{
		file_state->start_offset = 0;
		file_state->end_offset = st.st_size - 1;
	}

	file_state->cur_offset = file_state->start_offset;
	file_state->total_size = st.st_size;
	return 0;
}

#define ERRNO_TO_STATUS_CODE(x) ( \
	((x) == ENOENT)? 404: \
	((x) == EPERM || (x) == EACCES)? 403: 500 \
)

static int open_peer (file_state_t* file_state, const mio_bch_t* actual_file)
{
	switch (file_state->req_method)
	{
		case MIO_HTTP_GET:
		case MIO_HTTP_HEAD:
		{
			int flags;

			if (access(actual_file, R_OK) == -1)
			{
				file_state_send_final_status_to_client (file_state, ERRNO_TO_STATUS_CODE(errno), 1); /* 404 not found 403 Forbidden */
				return -1;
			}

			flags = O_RDONLY | O_NONBLOCK;
		#if defined(O_CLOEXEC)
			flags |= O_CLOEXEC;
		#endif
		#if defined(O_LARGEFILE)
			flags |= O_LARGEFILE;
		#endif
			file_state->peer = open(actual_file, flags);

			if (MIO_UNLIKELY(file_state->peer <= -1)) 
			{
				file_state_send_final_status_to_client (file_state, ERRNO_TO_STATUS_CODE(errno), 1);
				return -1;
			}

			return 0;
		}

#if 0
		case MIO_HTTP_PUT:
		case MIO_HTTP_POST:
/* TOOD: this is destructive. jump to default if not allowed by flags... */
			file_state->peer = open(actual_file, O_WRONLY | O_TRUNC | O_CREAT | O_NONBLOCK | O_CLOEXEC, 0644);
			if (MIO_UNLIKELY(file_state->peer <= -1)) 
			{
				file_state_send_final_status_to_client (file_state, ERRNO_TO_STATUS_CODE(errno), 1);
				return -1;
			}
			return 0;

		case MIO_HTTP_PATCH:
/* TOOD: this is destructive. jump to default if not allowed by flags... */
			file_state->peer = open(actual_file, O_WRONLY | O_NONBLOCK | O_CLOEXEC, 0644);
			if (MIO_UNLIKELY(file_state->peer <= -1)) 
			{
				file_state_send_final_status_to_client (file_state, ERRNO_TO_STATUS_CODE(errno), 1);
				return -1;
			}
			return 0;
#endif

#if 0
		case MIO_HTTP_DELETE:
			/* TODO: */
#endif
	}

	file_state_send_final_status_to_client (file_state, 405, 1); /* 405: method not allowed */
	return -1;
}

static MIO_INLINE void fadvise_on_peer (file_state_t* file_state)
{
#if defined(HAVE_POSIX_FADVISE)
	if (file_state->req_method == MIO_HTTP_GET)
		posix_fadvise (file_state->peer, file_state->start_offset, file_state->end_offset - file_state->start_offset + 1, POSIX_FADV_SEQUENTIAL);
#endif
}

int mio_svc_htts_dofile (mio_svc_htts_t* htts, mio_dev_sck_t* csck, mio_htre_t* req, const mio_bch_t* docroot, const mio_bch_t* file, const mio_bch_t* mime_type)
{
/* TODO: ETag, Last-Modified... */

	mio_t* mio = htts->mio;
	mio_svc_htts_cli_t* cli = mio_dev_sck_getxtn(csck);
	file_state_t* file_state = MIO_NULL;
	mio_bch_t* actual_file = MIO_NULL;

	/* ensure that you call this function before any contents is received */
	MIO_ASSERT (mio, mio_htre_getcontentlen(req) == 0);

	actual_file = mio_svc_htts_dupmergepaths(htts, docroot, file);
	if (MIO_UNLIKELY(!actual_file)) goto oops;

	file_state = (file_state_t*)mio_svc_htts_rsrc_make(htts, MIO_SIZEOF(*file_state), file_state_on_kill);
	if (MIO_UNLIKELY(!file_state)) goto oops;

	file_state->client = cli;
	file_state->sendfile_ok = mio_dev_sck_sendfileok(cli->sck);
	/*file_state->num_pending_writes_to_client = 0;
	file_state->num_pending_writes_to_peer = 0;*/
	file_state->req_version = *mio_htre_getversion(req);
	file_state->req_method = mio_htre_getqmethodtype(req);
	file_state->req_content_length_unlimited = mio_htre_getreqcontentlen(req, &file_state->req_content_length);

	file_state->client_org_on_read = csck->on_read;
	file_state->client_org_on_write = csck->on_write;
	file_state->client_org_on_disconnect = csck->on_disconnect;
	csck->on_read = file_client_on_read;
	csck->on_write = file_client_on_write;
	csck->on_disconnect = file_client_on_disconnect;

	MIO_ASSERT (mio, cli->rsrc == MIO_NULL); /* you must not call this function while cli->rsrc is not MIO_NULL */
	MIO_SVC_HTTS_RSRC_ATTACH (file_state, cli->rsrc);

	file_state->peer_tmridx = MIO_TMRIDX_INVALID;
	file_state->peer = -1;

	if (open_peer(file_state, actual_file) <= -1 || 
	    process_range_header(file_state, req) <= -1) goto oops;

	fadvise_on_peer (file_state);

#if !defined(FILE_ALLOW_UNLIMITED_REQ_CONTENT_LENGTH)
	if (file_state->req_content_length_unlimited)
	{
		/* Transfer-Encoding is chunked. no content-length is known in advance. */
		
		/* option 1. buffer contents. if it gets too large, send 413 Request Entity Too Large.
		 * option 2. send 411 Length Required immediately
		 * option 3. set Content-Length to -1 and use EOF to indicate the end of content [Non-Standard] */

		if (file_state_send_final_status_to_client(file_state, 411, 1) <= -1) goto oops;
	}
#endif

	if (req->flags & MIO_HTRE_ATTR_EXPECT100)
	{
		if (mio_comp_http_version_numbers(&req->version, 1, 1) >= 0 && 
		   (file_state->req_content_length_unlimited || file_state->req_content_length > 0) &&
		   (file_state->req_method != MIO_HTTP_GET && file_state->req_method != MIO_HTTP_HEAD))  
		{
			mio_bch_t msgbuf[64];
			mio_oow_t msglen;

			msglen = mio_fmttobcstr(mio, msgbuf, MIO_COUNTOF(msgbuf), "HTTP/%d.%d 100 Continue\r\n\r\n", file_state->req_version.major, file_state->req_version.minor);
			if (file_state_write_to_client(file_state, msgbuf, msglen) <= -1) goto oops;
			file_state->ever_attempted_to_write_to_client = 0; /* reset this as it's polluted for 100 continue */
		}
	}
	else if (req->flags & MIO_HTRE_ATTR_EXPECT)
	{
		/* 417 Expectation Failed */
		file_state_send_final_status_to_client (file_state, 417, 1);
		goto oops;
	}

#if defined(FILE_ALLOW_UNLIMITED_REQ_CONTENT_LENGTH)
	if (file_state->req_content_length_unlimited)
	{
		/* change the callbacks to subscribe to contents to be uploaded */
		file_state->client_htrd_org_recbs = *mio_htrd_getrecbs(file_state->client->htrd);
		file_client_htrd_recbs.peek = file_state->client_htrd_org_recbs.peek;
		mio_htrd_setrecbs (file_state->client->htrd, &file_client_htrd_recbs);
		file_state->client_htrd_recbs_changed = 1;
	}
	else
	{
#endif
		if (file_state->req_content_length > 0)
		{
			/* change the callbacks to subscribe to contents to be uploaded */
			file_state->client_htrd_org_recbs = *mio_htrd_getrecbs(file_state->client->htrd);
			file_client_htrd_recbs.peek = file_state->client_htrd_org_recbs.peek;
			mio_htrd_setrecbs (file_state->client->htrd, &file_client_htrd_recbs);
			file_state->client_htrd_recbs_changed = 1;
		}
		else
		{
			/* no content to be uploaded from the client */
			/* indicate EOF to the peer and disable input wathching from the client */
			if (file_state_write_to_peer(file_state, MIO_NULL, 0) <= -1) goto oops;
			file_state_mark_over (file_state, FILE_STATE_OVER_READ_FROM_CLIENT | FILE_STATE_OVER_WRITE_TO_PEER);
		}
#if defined(FILE_ALLOW_UNLIMITED_REQ_CONTENT_LENGTH)
	}
#endif

	/* this may change later if Content-Length is included in the file output */
	if (req->flags & MIO_HTRE_ATTR_KEEPALIVE)
	{
		file_state->keep_alive = 1;
		file_state->res_mode_to_cli = FILE_STATE_RES_MODE_LENGTH;
	}
	else
	{
		file_state->keep_alive = 0;
		file_state->res_mode_to_cli = FILE_STATE_RES_MODE_CLOSE;
	}

	if (file_state->req_method == MIO_HTTP_GET)
	{
		if (file_state->etag_match)
		{
			/* 304 not modified */
			if (file_state_send_final_status_to_client(file_state, 304, 0) <= -1) goto oops;
			file_state_mark_over (file_state, FILE_STATE_OVER_READ_FROM_PEER | FILE_STATE_OVER_WRITE_TO_PEER);
		}
		else
		{
			/* normal full transfer */
		#if defined(TCP_CORK)
			int tcp_cork = 1;
			#if defined(SOL_TCP)
			mio_dev_sck_setsockopt(file_state->client->sck, SOL_TCP, TCP_CORK, &tcp_cork, MIO_SIZEOF(tcp_cork));
			#elif defined(IPPROTO_TCP)
			mio_dev_sck_setsockopt(file_state->client->sck, IPPROTO_TCP, TCP_CORK, &tcp_cork, MIO_SIZEOF(tcp_cork));
			#endif
		#endif

			if (file_state_send_header_to_client(file_state, 200, 0, mime_type) <= -1 ||
			    file_state_send_contents_to_client(file_state) <= -1) goto oops;
		}
	}
	else if (file_state->req_method == MIO_HTTP_HEAD)
	{
		if (file_state_send_header_to_client(file_state, 200, 0, mime_type) <= -1) goto oops;
		file_state_mark_over (file_state, FILE_STATE_OVER_READ_FROM_PEER | FILE_STATE_OVER_WRITE_TO_PEER);
	}

	/* TODO: store current input watching state and use it when destroying the file_state data */
	if (mio_dev_sck_read(csck, !(file_state->over & FILE_STATE_OVER_READ_FROM_CLIENT)) <= -1) goto oops;
	mio_freemem (mio, actual_file);
	return 0;

oops:
	MIO_DEBUG2 (mio, "HTTS(%p) - FAILURE in dofile - socket(%p)\n", htts, csck);
	if (file_state) file_state_halt_participating_devices (file_state);
	if (actual_file) mio_freemem (mio, actual_file);
	return -1;
}
