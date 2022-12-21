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
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>

#define FILE_ALLOW_UNLIMITED_REQ_CONTENT_LENGTH

enum file_res_mode_t
{
	FILE_RES_MODE_CLOSE,
	FILE_RES_MODE_LENGTH
};
typedef enum file_res_mode_t file_res_mode_t;

#define FILE_OVER_READ_FROM_CLIENT (1 << 0)
#define FILE_OVER_READ_FROM_PEER   (1 << 1)
#define FILE_OVER_WRITE_TO_CLIENT  (1 << 2)
#define FILE_OVER_WRITE_TO_PEER    (1 << 3)
#define FILE_OVER_ALL (FILE_OVER_READ_FROM_CLIENT | FILE_OVER_READ_FROM_PEER | FILE_OVER_WRITE_TO_CLIENT | FILE_OVER_WRITE_TO_PEER)

struct file_t
{
	HIO_SVC_HTTS_RSRC_HEADER;

	int options;
	hio_svc_htts_file_cbs_t* cbs;
	hio_oow_t num_pending_writes_to_client;
	hio_oow_t num_pending_writes_to_peer;
	int sendfile_ok;
	int peer;
	hio_foff_t total_size;
	hio_foff_t start_offset;
	hio_foff_t end_offset;
	hio_foff_t cur_offset;
	hio_bch_t peer_buf[8192];
	hio_tmridx_t peer_tmridx;
	hio_bch_t peer_etag[128];

	hio_svc_htts_cli_t* client;
	hio_http_version_t req_version; /* client request */
	hio_http_method_t req_method;

	unsigned int over: 4; /* must be large enough to accomodate FILE_OVER_ALL */
	unsigned int keep_alive: 1;
	unsigned int req_content_length_unlimited: 1;
	unsigned int ever_attempted_to_write_to_client: 1;
	unsigned int client_eof_detected: 1;
	unsigned int client_disconnected: 1;
	unsigned int client_htrd_recbs_changed: 1;
	unsigned int etag_match: 1;
	hio_oow_t req_content_length; /* client request content length */
	file_res_mode_t res_mode_to_cli;

	hio_dev_sck_on_read_t client_org_on_read;
	hio_dev_sck_on_write_t client_org_on_write;
	hio_dev_sck_on_disconnect_t client_org_on_disconnect;
	hio_htrd_recbs_t client_htrd_org_recbs;
};
typedef struct file_t file_t;

static int file_send_contents_to_client (file_t* file);

static void file_halt_participating_devices (file_t* file)
{
	HIO_ASSERT (file->client->htts->hio, file->client != HIO_NULL);
	HIO_ASSERT (file->client->htts->hio, file->client->sck != HIO_NULL);

	HIO_DEBUG3 (file->client->htts->hio, "HTTS(%p) - file(c=%d,p=%d) Halting participating devices\n", file->client->htts, (int)file->client->sck->hnd, (int)file->peer);

	/* only the client socket device. 
	 * the peer side is just a file descriptor - no hio-managed device */
	hio_dev_sck_halt (file->client->sck);
}

static int file_write_to_client (file_t* file, const void* data, hio_iolen_t dlen)
{
	file->ever_attempted_to_write_to_client = 1;

	file->num_pending_writes_to_client++;
	if (hio_dev_sck_write(file->client->sck, data, dlen, HIO_NULL, HIO_NULL) <= -1)  /* TODO: use sendfile here.. */
	{
		file->num_pending_writes_to_client--;
		return -1;
	}

	return 0;
}

static int file_sendfile_to_client (file_t* file, hio_foff_t foff, hio_iolen_t len)
{
	file->ever_attempted_to_write_to_client = 1;

	file->num_pending_writes_to_client++;
	if (hio_dev_sck_sendfile(file->client->sck, file->peer, foff, len, HIO_NULL) <= -1) 
	{
		file->num_pending_writes_to_client--;
		return -1;
	}

	return 0;
}

static int file_send_final_status_to_client (file_t* file, int status_code, int force_close)
{
	hio_svc_htts_cli_t* cli = file->client;
	hio_bch_t dtbuf[64];
	const hio_bch_t* status_msg;

	hio_svc_htts_fmtgmtime (cli->htts, HIO_NULL, dtbuf, HIO_COUNTOF(dtbuf));
	status_msg =  hio_http_status_to_bcstr(status_code);

	if (!force_close) force_close = !file->keep_alive;
	if (hio_becs_fmt(cli->sbuf, "HTTP/%d.%d %d %hs\r\nServer: %hs\r\nDate: %s\r\nConnection: %hs\r\nContent-Length: %zu\r\n\r\n%s",
		file->req_version.major, file->req_version.minor,
		status_code, status_msg,
		cli->htts->server_name, dtbuf,
		(force_close? "close": "keep-alive"),
		hio_count_bcstr(status_msg), status_msg) == (hio_oow_t)-1) return -1;

	return (file_write_to_client(file, HIO_BECS_PTR(cli->sbuf), HIO_BECS_LEN(cli->sbuf)) <= -1 ||
	        (force_close && file_write_to_client(file, HIO_NULL, 0) <= -1))? -1: 0;
}

static void file_close_peer (file_t* file)
{
	hio_t* hio = file->htts->hio;

	if (file->peer_tmridx != HIO_TMRIDX_INVALID)
	{
		hio_deltmrjob (hio, file->peer_tmridx);
		HIO_ASSERT (hio, file->peer_tmridx == HIO_TMRIDX_INVALID);
	}

	if (file->peer >= 0)
	{
		close (file->peer);
		file->peer = -1;
	}
}

static void file_mark_over (file_t* file, int over_bits)
{
	unsigned int old_over;

	old_over = file->over;
	file->over |= over_bits;

	HIO_DEBUG6 (file->htts->hio, "HTTS(%p) - file(c=%d,p=%d) updating mark - old_over=%x | new-bits=%x => over=%x\n", file->htts, (int)file->client->sck->hnd, file->peer, (int)old_over, (int)over_bits, (int)file->over);

	if (!(old_over & FILE_OVER_READ_FROM_CLIENT) && (file->over & FILE_OVER_READ_FROM_CLIENT))
	{
		if (hio_dev_sck_read(file->client->sck, 0) <= -1) 
		{
			HIO_DEBUG3 (file->htts->hio, "HTTS(%p) - file(c=%d,p=%d) halting client for failure to disable input watching\n", file->htts, (int)file->client->sck->hnd, file->peer);
			hio_dev_sck_halt (file->client->sck);
		}
	}

#if 0
	if (!(old_over & FILE_OVER_READ_FROM_PEER) && (file->over & FILE_OVER_READ_FROM_PEER))
	{
		/* there is no partial close... keep it open */
	}
#endif

	if (old_over != FILE_OVER_ALL && file->over == FILE_OVER_ALL)
	{
		/* ready to stop */
		HIO_DEBUG3 (file->htts->hio, "HTTS(%p) - file(c=%d,p=%d) halting peer as it is unneeded\n", file->htts, (int)file->client->sck->hnd, file->peer);
		file_close_peer (file);

		if (file->keep_alive && !file->client_eof_detected) 
		{
		#if defined(TCP_CORK)
			int tcp_cork = 0;
			#if defined(SOL_TCP)
			hio_dev_sck_setsockopt(file->client->sck, SOL_TCP, TCP_CORK, &tcp_cork, HIO_SIZEOF(tcp_cork));
			#elif defined(IPPROTO_TCP)
			hio_dev_sck_setsockopt(file->client->sck, IPPROTO_TCP, TCP_CORK, &tcp_cork, HIO_SIZEOF(tcp_cork));
			#endif
		#endif

			/* how to arrange to delete this file object and put the socket back to the normal waiting state??? */
			HIO_ASSERT (file->htts->hio, file->client->rsrc == (hio_svc_htts_rsrc_t*)file);
			HIO_SVC_HTTS_RSRC_DETACH (file->client->rsrc);
			/* the file resource must not be accessed from here down as it could have been destroyed */
		}
		else
		{
			HIO_DEBUG4 (file->htts->hio, "HTTS(%p) - file(c=%d,p=%d) halting client for %hs\n", file->htts, (int)file->client->sck->hnd, file->peer, (file->client_eof_detected? "EOF detected": "no keep-alive"));
			hio_dev_sck_shutdown (file->client->sck, HIO_DEV_SCK_SHUTDOWN_WRITE);
			hio_dev_sck_halt (file->client->sck);
			/* the file resource will be detached from file->client->rsrc by the upstream disconnect handler in http_svr.c */
		}
	}
}

static int file_write_to_peer (file_t* file, const void* data, hio_iolen_t dlen)
{
	hio_t* hio = file->htts->hio;

	if (dlen <= 0)
	{
		file_mark_over (file, FILE_OVER_WRITE_TO_PEER);
	}
	else
	{
		hio_iolen_t pos, rem, n;
		if (file->req_method == HIO_HTTP_GET) return 0; 
		if (file->peer <= -1) return 0; /* peer open proabably failed */

		/* TODO: async file io -> create a file device?? */
		pos = 0;
		rem = dlen;
		while (rem > 0)
		{
			n = write(file->peer, &((const hio_uint8_t*)data)[pos], rem);
			if (n <= -1) return -1;
			rem -= n;
			pos += n;
		}
	}

	return 0;
}

static void file_on_kill (file_t* file)
{
	hio_t* hio = file->htts->hio;

	HIO_DEBUG3 (hio, "HTTS(%p) - file(c=%d,p=%d) on_kill\n", file->htts, (int)file->client->sck->hnd, file->peer);

	file_close_peer (file);

	if (file->client_org_on_read)
	{
		file->client->sck->on_read = file->client_org_on_read;
		file->client_org_on_read = HIO_NULL;
	}

	if (file->client_org_on_write)
	{
		file->client->sck->on_write = file->client_org_on_write;
		file->client_org_on_write = HIO_NULL;
	}

	if (file->client_org_on_disconnect)
	{
		file->client->sck->on_disconnect = file->client_org_on_disconnect;
		file->client_org_on_disconnect = HIO_NULL;
	}

	if (file->client_htrd_recbs_changed)
	{
		/* restore the callbacks */
		hio_htrd_setrecbs (file->client->htrd, &file->client_htrd_org_recbs);
	}

	if (!file->client_disconnected)
	{
		if (!file->keep_alive || hio_dev_sck_read(file->client->sck, 1) <= -1)
		{
			HIO_DEBUG3 (hio, "HTTS(%p) - file(c=%d,p=%d) halting client for failure to enable input watching\n", file->htts, (int)file->client->sck->hnd, file->peer);
			hio_dev_sck_halt (file->client->sck);
		}
	}
}

static void file_client_on_disconnect (hio_dev_sck_t* sck)
{
	hio_t* hio = sck->hio;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	file_t* file = (file_t*)cli->rsrc;

	HIO_ASSERT (hio, sck == cli->sck);
	HIO_ASSERT (hio, sck == file->client->sck);

	HIO_DEBUG3 (cli->htts->hio, "HTTS(%p) - file(c=%d,p=%d) client on_disconnect\n", file->client->htts, (int)sck->hnd, file->peer);

	file->client_disconnected = 1;
	file->client_org_on_disconnect (sck);

	/* the original disconnect handler (listener_on_disconnect in http-svr.c) 
	 * frees the file resource attached to the client. so it must not be accessed */
	HIO_ASSERT (hio, cli->rsrc == HIO_NULL);
}

static int file_client_on_read (hio_dev_sck_t* sck, const void* buf, hio_iolen_t len, const hio_skad_t* srcaddr)
{
	hio_t* hio = sck->hio;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	file_t* file = (file_t*)cli->rsrc;

	HIO_ASSERT (hio, sck == cli->sck);
	HIO_ASSERT (hio, sck == file->client->sck);

	if (len <= -1)
	{
		/* read error */
		HIO_DEBUG3 (cli->htts->hio, "HTTS(%p) - file(c=%d,p=%d) read error on client\n", file->client->htts, (int)sck->hnd, file->peer);
		goto oops;
	}

	if (file->peer <= -1)
	{
		/* the peer is gone or not even opened */
		HIO_DEBUG3 (cli->htts->hio, "HTTS(%p) - file(c=%d,p=%d) read on client, no peer to write\n", file->client->htts, (int)sck->hnd, file->peer);
		goto oops; /* do what?  just return 0? */
	}

	if (len == 0)
	{
		/* EOF on the client side. arrange to close */
		HIO_DEBUG3 (cli->htts->hio, "HTTS(%p) - file(c=%d,p=%d) EOF detected on client\n", file->client->htts, (int)sck->hnd, file->peer);
		file->client_eof_detected = 1;

		if (!(file->over & FILE_OVER_READ_FROM_CLIENT)) /* if this is true, EOF is received without file_client_htrd_poke() */
		{
			int n;
			n = file_write_to_peer(file, HIO_NULL, 0);
			file_mark_over (file, FILE_OVER_READ_FROM_CLIENT);
			if (n <= -1) goto oops;
		}
	}
	else
	{
		hio_oow_t rem;

		HIO_ASSERT (hio, !(file->over & FILE_OVER_READ_FROM_CLIENT));

		if (hio_htrd_feed(cli->htrd, buf, len, &rem) <= -1) goto oops;

		if (rem > 0)
		{
			/* TODO store this to client buffer. once the current resource is completed, arrange to call on_read() with it */
			HIO_DEBUG3 (cli->htts->hio, "HTTS(%p) - file(c=%d,p=%d) excessive data after contents on client\n", file->client->htts, (int)sck->hnd, file->peer);
		}
	}

	return 0;

oops:
	file_halt_participating_devices (file);
	return 0;
}

static int file_client_on_write (hio_dev_sck_t* sck, hio_iolen_t wrlen, void* wrctx, const hio_skad_t* dstaddr)
{
	hio_t* hio = sck->hio;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	file_t* file = (file_t*)cli->rsrc;

	HIO_ASSERT (hio, sck == cli->sck);
	HIO_ASSERT (hio, sck == file->client->sck);

	if (wrlen <= -1)
	{
		HIO_DEBUG3 (hio, "HTTS(%p) - file(c=%d,p=%d) unable to write to client\n", file->client->htts, (int)sck->hnd, file->peer);
		goto oops;
	}

	if (wrlen == 0)
	{
		/* if the connect is keep-alive, this part may not be called */
		file->num_pending_writes_to_client--;
		HIO_ASSERT (hio, file->num_pending_writes_to_client == 0);
		HIO_DEBUG3 (hio, "HTTS(%p) - file(c=%d,p=%d) indicated EOF to client\n", file->client->htts, (int)sck->hnd, file->peer);
		/* since EOF has been indicated to the client, it must not write to the client any further.
		 * this also means that i don't need any data from the peer side either.
		 * i don't need to enable input watching on the peer side */

		file_mark_over (file, FILE_OVER_WRITE_TO_CLIENT);
	}
	else
	{
		HIO_ASSERT (hio, file->num_pending_writes_to_client > 0);
		file->num_pending_writes_to_client--;

		if (file->req_method == HIO_HTTP_GET)
			file_send_contents_to_client (file);

		if ((file->over & FILE_OVER_READ_FROM_PEER) && file->num_pending_writes_to_client <= 0)
		{
			file_mark_over (file, FILE_OVER_WRITE_TO_CLIENT);
		}
	}

	return 0;

oops:
	file_halt_participating_devices (file);
	return 0;
}

/* --------------------------------------------------------------------- */

static int file_client_htrd_poke (hio_htrd_t* htrd, hio_htre_t* req)
{
	/* client request got completed */
	hio_svc_htts_cli_htrd_xtn_t* htrdxtn = (hio_svc_htts_cli_htrd_xtn_t*)hio_htrd_getxtn(htrd);
	hio_dev_sck_t* sck = htrdxtn->sck;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	file_t* file = (file_t*)cli->rsrc;

	/* indicate EOF to the client peer */
	if (file_write_to_peer(file, HIO_NULL, 0) <= -1) return -1;

	if (file->req_method != HIO_HTTP_GET)
	{
		if (file_send_final_status_to_client(file, HIO_HTTP_STATUS_OK, 0) <= -1) return -1;
	}

	file_mark_over (file, FILE_OVER_READ_FROM_CLIENT);
	return 0;
}

static int file_client_htrd_push_content (hio_htrd_t* htrd, hio_htre_t* req, const hio_bch_t* data, hio_oow_t dlen)
{
	hio_svc_htts_cli_htrd_xtn_t* htrdxtn = (hio_svc_htts_cli_htrd_xtn_t*)hio_htrd_getxtn(htrd);
	hio_dev_sck_t* sck = htrdxtn->sck;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	file_t* file = (file_t*)cli->rsrc;

	HIO_ASSERT (sck->hio, cli->sck == sck);
	return file_write_to_peer(file, data, dlen);
}

static hio_htrd_recbs_t file_client_htrd_recbs =
{
	HIO_NULL,
	file_client_htrd_poke,
	file_client_htrd_push_content
};

/* --------------------------------------------------------------------- */

static int file_send_header_to_client (file_t* file, int status_code, int force_close, const hio_bch_t* mime_type)
{
	hio_svc_htts_cli_t* cli = file->client;
	hio_bch_t dtbuf[64];
	hio_foff_t content_length;

	hio_svc_htts_fmtgmtime (cli->htts, HIO_NULL, dtbuf, HIO_COUNTOF(dtbuf));

	if (!force_close) force_close = !file->keep_alive;

	content_length = file->end_offset - file->start_offset + 1;
	if (status_code == HIO_HTTP_STATUS_OK && file->total_size != content_length) status_code = HIO_HTTP_STATUS_PARTIAL_CONTENT;

	if (hio_becs_fmt(cli->sbuf, "HTTP/%d.%d %d %hs\r\nServer: %hs\r\nDate: %s\r\nConnection: %hs\r\nAccept-Ranges: bytes\r\n",
		file->req_version.major, file->req_version.minor,
		status_code, hio_http_status_to_bcstr(status_code),
		cli->htts->server_name, dtbuf,
		(force_close? "close": "keep-alive")) == (hio_oow_t)-1) return -1;

	/* Content-Type is not set if mime_type is null or blank */
	if (mime_type && mime_type[0] != '\0' &&
	    hio_becs_fcat(cli->sbuf, "Content-Type: %hs\r\n", mime_type) == (hio_oow_t)-1) return -1;
	
	if (file->req_method == HIO_HTTP_GET && hio_becs_fcat(cli->sbuf, "ETag: %hs\r\n", file->peer_etag) == (hio_oow_t)-1) return -1;
	if (status_code == HIO_HTTP_STATUS_PARTIAL_CONTENT && hio_becs_fcat(cli->sbuf, "Content-Ranges: bytes %ju-%ju/%ju\r\n", (hio_uintmax_t)file->start_offset, (hio_uintmax_t)file->end_offset, (hio_uintmax_t)file->total_size) == (hio_oow_t)-1) return -1;

/* ----- */
// TODO: Allow-Contents
// Allow-Headers... support custom headers...
	if (hio_becs_fcat(cli->sbuf, "Access-Control-Allow-Origin: *\r\n", (hio_uintmax_t)content_length) == (hio_oow_t)-1) return -1;
/* ----- */

	if (hio_becs_fcat(cli->sbuf, "Content-Length: %ju\r\n\r\n", (hio_uintmax_t)content_length) == (hio_oow_t)-1) return -1;

	return file_write_to_client(file, HIO_BECS_PTR(cli->sbuf), HIO_BECS_LEN(cli->sbuf));
}

static void send_contents_to_client_later (hio_t* hio, const hio_ntime_t* now, hio_tmrjob_t* tmrjob)
{
	file_t* file = (file_t*)tmrjob->ctx;
	if (file_send_contents_to_client(file) <= -1)
	{
		file_halt_participating_devices (file);
	}
}

static int file_send_contents_to_client (file_t* file)
{
	hio_t* hio = file->htts->hio;
	hio_foff_t lim;

	if (file->cur_offset > file->end_offset)
	{
		/* reached the end */
		file_mark_over (file, FILE_OVER_READ_FROM_PEER);
		return 0;
	}

	lim = file->end_offset - file->cur_offset + 1;
	if (file->sendfile_ok)
	{
		if (lim > 0x7FFF0000) lim = 0x7FFF0000; /* TODO: change this... */
		if (file_sendfile_to_client(file, file->cur_offset, lim) <= -1) return -1;
		file->cur_offset += lim;
	}
	else
	{
		ssize_t n;

		n = read(file->peer, file->peer_buf, (lim < HIO_SIZEOF(file->peer_buf)? lim: HIO_SIZEOF(file->peer_buf)));
		if (n == -1)
		{
			if ((errno == EAGAIN || errno == EINTR) && file->peer_tmridx == HIO_TMRIDX_INVALID)
			{
				hio_tmrjob_t tmrjob;
				/* use a timer job for a new sending attempt */
				HIO_MEMSET (&tmrjob, 0, HIO_SIZEOF(tmrjob));
				tmrjob.ctx = file;
				/*tmrjob.when = leave it at 0 for immediate firing.*/
				tmrjob.handler = send_contents_to_client_later;
				tmrjob.idxptr = &file->peer_tmridx;
				return hio_instmrjob(hio, &tmrjob) == HIO_TMRIDX_INVALID? -1: 0;
			}

			return -1;
		}
		else if (n == 0)
		{
			/* no more data to read - this must not happen unless file size changed while the file is open. */
	/* TODO: I probably must close the connection by force??? */
			file_mark_over (file, FILE_OVER_READ_FROM_PEER); 
			return -1;
		}

		if (file_write_to_client(file, file->peer_buf, n) <= -1) return -1;

		file->cur_offset += n;

	/*	if (file->cur_offset > file->end_offset)  should i check this or wait until this function is invoked?
			file_mark_over (file, FILE_OVER_READ_FROM_PEER);*/
	}

	return 0;
}

#define ERRNO_TO_STATUS_CODE(x) ( \
	((x) == ENOENT)? HIO_HTTP_STATUS_NOT_FOUND: \
	((x) == EPERM || (x) == EACCES)? HIO_HTTP_STATUS_FORBIDDEN: HIO_HTTP_STATUS_INTERNAL_SERVER_ERROR \
)

static HIO_INLINE int process_range_header (file_t* file, hio_htre_t* req, int* error_status)
{
	struct stat st;
	const hio_htre_hdrval_t* tmp;
	hio_oow_t etag_len;

	if (fstat(file->peer, &st) <= -1) 
	{
		*error_status = ERRNO_TO_STATUS_CODE(errno);
		return -1;
	}

	if ((st.st_mode & S_IFMT) != S_IFREG)
	{
		/* TODO: support directory listing if S_IFDIR? still disallow special files. */
		*error_status = HIO_HTTP_STATUS_FORBIDDEN;
		return -1;
	}

	if (file->req_method == HIO_HTTP_GET)
	{
	#if defined(HAVE_STRUCT_STAT_MTIM)
		etag_len = hio_fmt_uintmax_to_bcstr(&file->peer_etag[0], HIO_COUNTOF(file->peer_etag), st.st_mtim.tv_sec, 16, -1, '\0', HIO_NULL);
		file->peer_etag[etag_len++] = '-';
		#if defined(HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC)
		etag_len += hio_fmt_uintmax_to_bcstr(&file->peer_etag[etag_len], HIO_COUNTOF(file->peer_etag), st.st_mtim.tv_nsec, 16, -1, '\0', HIO_NULL);
		file->peer_etag[etag_len++] = '-';
		#endif
	#else
		etag_len = hio_fmt_uintmax_to_bcstr(&file->peer_etag[0], HIO_COUNTOF(file->peer_etag), st.st_mtime, 16, -1, '\0', HIO_NULL);
		file->peer_etag[etag_len++] = '-';
	#endif
		etag_len += hio_fmt_uintmax_to_bcstr(&file->peer_etag[etag_len], HIO_COUNTOF(file->peer_etag) - etag_len, st.st_size, 16, -1, '\0', HIO_NULL);
		file->peer_etag[etag_len++] = '-';
		etag_len += hio_fmt_uintmax_to_bcstr(&file->peer_etag[etag_len], HIO_COUNTOF(file->peer_etag) - etag_len, st.st_ino, 16, -1, '\0', HIO_NULL);
		file->peer_etag[etag_len++] = '-';
		hio_fmt_uintmax_to_bcstr (&file->peer_etag[etag_len], HIO_COUNTOF(file->peer_etag) - etag_len, st.st_dev, 16, -1, '\0', HIO_NULL);

		tmp = hio_htre_getheaderval(req, "If-None-Match");
		if (tmp && hio_comp_bcstr(file->peer_etag, tmp->ptr, 0) == 0) file->etag_match = 1;
	}
	file->end_offset = st.st_size;

	tmp = hio_htre_getheaderval(req, "Range"); /* TODO: support multiple ranges? */
	if (tmp)
	{
		hio_http_range_t range;

		if (hio_parse_http_range_bcstr(tmp->ptr, &range) <= -1)
		{
		range_not_satisifiable:
			*error_status = HIO_HTTP_STATUS_RANGE_NOT_SATISFIABLE;
			return -1;
		}

		switch (range.type)
		{
			case HIO_HTTP_RANGE_PROPER:
				/* Range XXXX-YYYY */
				if (range.to >= st.st_size) goto range_not_satisifiable;
				file->start_offset = range.from;
				file->end_offset = range.to;
				break;

			case HIO_HTTP_RANGE_PREFIX:
				/* Range: XXXX- */
				if (range.from >= st.st_size) goto range_not_satisifiable;
				file->start_offset = range.from;
				file->end_offset = st.st_size - 1;
				break;

			case HIO_HTTP_RANGE_SUFFIX:
				/* Range: -XXXX */
				if (range.to >= st.st_size) goto range_not_satisifiable;
				file->start_offset = st.st_size - range.to;
				file->end_offset = st.st_size - 1;
				break;
		}

		if (file->start_offset > 0) 
		{
			if (lseek(file->peer, file->start_offset, SEEK_SET) <= -1)
			{
				*error_status = ERRNO_TO_STATUS_CODE(errno);
				return -1;
			}
		}
	}
	else
	{
		file->start_offset = 0;
		file->end_offset = st.st_size - 1;
	}

	file->cur_offset = file->start_offset;
	file->total_size = st.st_size;
	return 0;
}

static int open_peer_with_mode (file_t* file, const hio_bch_t* actual_file, int flags, int* error_status, const hio_bch_t** actual_mime_type)
{
	struct stat st;
	const hio_bch_t* opened_file;

	flags |= O_NONBLOCK;
#if defined(O_CLOEXEC)
	flags |= O_CLOEXEC;
#endif
#if defined(O_LARGEFILE)
	flags |= O_LARGEFILE;
#endif

	file->peer = open(actual_file, flags, 0644);
	if (HIO_UNLIKELY(file->peer <= -1))
	{
		*error_status = ERRNO_TO_STATUS_CODE(errno);
		return -1;
	}

	opened_file = actual_file;
	if ((flags | O_RDONLY) && fstat(file->peer, &st) >= 0 && S_ISDIR(st.st_mode)) /* only for read operation */
	{
		hio_bch_t* alt_file;
		int alt_fd;

		alt_file = hio_svc_htts_dupmergepaths(file->htts, actual_file, "index.html"); /* TODO: make the default index files configurable */
		if (alt_file)
		{
			alt_fd = open(alt_file, flags, 0644);
			if (alt_fd >= 0)
			{
				close (file->peer);
				file->peer = alt_fd;
				opened_file = alt_file;
			}
			else
			{
				hio_freemem (file->htts->hio, alt_file);

				if (file->options & HIO_SVC_HTTS_FILE_LIST_DIR)
				{
					/* switch to directory listing */
					DIR* dp;

					dp = opendir(actual_file);
					if (dp)
					{
						alt_file = hio_dupbcstr(file->htts->hio, "/tmp/.XXXXXX", HIO_NULL);
						if (alt_file)
						{
							/* TOOD: mkostemp instead and specify O_CLOEXEC and O_LARGEFILE? */
							alt_fd = mkstemp(alt_file);
							if (alt_fd >= 0)
							{
									struct dirent* de;

									unlink (alt_file);
									while ((de = readdir(dp)))
									{
										/* TODO: do buffering ... */
									#if 0
									/* TODO: call a directory entry formatter callback??  */
										if (file->cbs && file->cbs->bfmt_dir)
										{
											file->cbs->bfmt_dir(file->htts, de->d_name);
										}
									#endif
										if (strcmp(de->d_name, ".") != 0)
										{
											write (alt_fd, de->d_name, strlen(de->d_name));
											write (alt_fd, "\n", 1);
										}
									}

									lseek (alt_fd, SEEK_SET, 0);

									close (file->peer);
									file->peer = alt_fd;
									opened_file = alt_file;
							}
							else
							{
								hio_freemem (file->htts->hio, alt_file);
							}
						}
						closedir (dp);
					}
				}
			}
		}
	}

	if (actual_mime_type)
	{
		const hio_bch_t* dot;
		dot = hio_rfind_bchar_in_bcstr(opened_file, '.');
		if (dot)
		{
			const hio_bch_t* mt;
			mt = hio_get_mime_type_by_ext(dot + 1);
			if (mt) *actual_mime_type = mt;
		}
	}

	if (opened_file != actual_file)
		hio_freemem (file->htts->hio, (hio_bch_t*)opened_file);

	return 0;
}

static HIO_INLINE void set_tcp_cork (hio_dev_sck_t* sck)
{
#if defined(TCP_CORK)
	int tcp_cork = 1;
	#if defined(SOL_TCP)
	hio_dev_sck_setsockopt (sck, SOL_TCP, TCP_CORK, &tcp_cork, HIO_SIZEOF(tcp_cork));
	#elif defined(IPPROTO_TCP)
	hio_dev_sck_setsockopt (sck, IPPROTO_TCP, TCP_CORK, &tcp_cork, HIO_SIZEOF(tcp_cork));
	#endif
#endif
}

int hio_svc_htts_dofile (hio_svc_htts_t* htts, hio_dev_sck_t* csck, hio_htre_t* req, const hio_bch_t* docroot, const hio_bch_t* filepath, const hio_bch_t* mime_type, int options, hio_svc_htts_file_cbs_t* cbs)
{
	hio_t* hio = htts->hio;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(csck);
	file_t* file = HIO_NULL;
	hio_bch_t* actual_file = HIO_NULL;
	int status_code;

	/* ensure that you call this function before any contents is received */
	HIO_ASSERT (hio, hio_htre_getcontentlen(req) == 0);
	HIO_ASSERT (hio, cli->sck == csck);

	HIO_DEBUG5 (hio, "HTTS(%p) - file(c=%d) - [%hs] %hs%hs\n", htts, (int)csck->hnd, cli->cli_addr_bcstr, (docroot[0] == '/' && docroot[1] == '\0' && filepath[0] == '/'? "": docroot), filepath);

	actual_file = hio_svc_htts_dupmergepaths(htts, docroot, filepath);
	if (HIO_UNLIKELY(!actual_file)) goto oops;

	file = (file_t*)hio_svc_htts_rsrc_make(htts, HIO_SIZEOF(*file), file_on_kill);
	if (HIO_UNLIKELY(!file)) goto oops;

	file->options = options;
	file->cbs = cbs; /* the given pointer must outlive the lifespan of the while file handling cycle. */
	file->client = cli;
	file->sendfile_ok = hio_dev_sck_sendfileok(cli->sck);
	/*file->num_pending_writes_to_client = 0;
	file->num_pending_writes_to_peer = 0;*/
	file->req_version = *hio_htre_getversion(req);
	file->req_method = hio_htre_getqmethodtype(req);
	file->req_content_length_unlimited = hio_htre_getreqcontentlen(req, &file->req_content_length);

	file->client_org_on_read = csck->on_read;
	file->client_org_on_write = csck->on_write;
	file->client_org_on_disconnect = csck->on_disconnect;
	csck->on_read = file_client_on_read;
	csck->on_write = file_client_on_write;
	csck->on_disconnect = file_client_on_disconnect;

	HIO_ASSERT (hio, cli->rsrc == HIO_NULL); /* you must not call this function while cli->rsrc is not HIO_NULL */
	HIO_SVC_HTTS_RSRC_ATTACH (file, cli->rsrc); /* cli->rsrc = file with ref-count up */

	file->peer_tmridx = HIO_TMRIDX_INVALID;
	file->peer = -1;

#if !defined(FILE_ALLOW_UNLIMITED_REQ_CONTENT_LENGTH)
	if (file->req_content_length_unlimited)
	{
		/* Transfer-Encoding is chunked. no content-length is known in advance. */
		
		/* option 1. buffer contents. if it gets too large, send 413 Request Entity Too Large.
		 * option 2. send 411 Length Required immediately
		 * option 3. set Content-Length to -1 and use EOF to indicate the end of content [Non-Standard] */
		if (file_send_final_status_to_client(file, HIO_HTTP_STATUS_LENGTH_REQUIRED, 1) <= -1) goto oops;
	}
#endif

	if (req->flags & HIO_HTRE_ATTR_EXPECT100)
	{
		if (!(options & HIO_SVC_HTTS_FILE_NO_100_CONTINUE) &&
		    hio_comp_http_version_numbers(&req->version, 1, 1) >= 0 &&
		   (file->req_content_length_unlimited || file->req_content_length > 0) &&
		   (file->req_method != HIO_HTTP_GET && file->req_method != HIO_HTTP_HEAD))  
		{
			hio_bch_t msgbuf[64];
			hio_oow_t msglen;

			msglen = hio_fmttobcstr(hio, msgbuf, HIO_COUNTOF(msgbuf), "HTTP/%d.%d %d %hs\r\n\r\n", file->req_version.major, file->req_version.minor, HIO_HTTP_STATUS_CONTINUE, hio_http_status_to_bcstr(HIO_HTTP_STATUS_CONTINUE));
			if (file_write_to_client(file, msgbuf, msglen) <= -1) goto oops;
			file->ever_attempted_to_write_to_client = 0; /* reset this as it's polluted for 100 continue */
		}
	}
	else if (req->flags & HIO_HTRE_ATTR_EXPECT)
	{
		/* 417 Expectation Failed */
		file_send_final_status_to_client (file, HIO_HTTP_STATUS_EXPECTATION_FAILED, 1);
		goto oops;
	}

#if defined(FILE_ALLOW_UNLIMITED_REQ_CONTENT_LENGTH)
	if (file->req_content_length_unlimited)
	{
		/* change the callbacks to subscribe to contents to be uploaded */
		file->client_htrd_org_recbs = *hio_htrd_getrecbs(file->client->htrd);
		file_client_htrd_recbs.peek = file->client_htrd_org_recbs.peek;
		hio_htrd_setrecbs (file->client->htrd, &file_client_htrd_recbs);
		file->client_htrd_recbs_changed = 1;
	}
	else
	{
#endif
		if (file->req_content_length > 0)
		{
			/* change the callbacks to subscribe to contents to be uploaded */
			file->client_htrd_org_recbs = *hio_htrd_getrecbs(file->client->htrd);
			file_client_htrd_recbs.peek = file->client_htrd_org_recbs.peek;
			hio_htrd_setrecbs (file->client->htrd, &file_client_htrd_recbs);
			file->client_htrd_recbs_changed = 1;
		}
		else
		{
			/* no content to be uploaded from the client */
		#if 0
			/* indicate EOF to the peer and disable input wathching from the client */
			if (file_write_to_peer(file, HIO_NULL, 0) <= -1) goto oops;
			HIO_ASSERT (hio, file->over | FILE_OVER_WRITE_TO_PEER); /* must be set by the call to file_write_to_peer() above */
			file_mark_over (file, FILE_OVER_READ_FROM_CLIENT);
		#else
			/* no peer is open yet. so simply set the mars forcibly instead of calling file_write_to_peer() with null data */
			file_mark_over (file, FILE_OVER_READ_FROM_CLIENT | FILE_OVER_WRITE_TO_PEER);
		#endif
		}
#if defined(FILE_ALLOW_UNLIMITED_REQ_CONTENT_LENGTH)
	}
#endif

	/* this may change later if Content-Length is included in the file output */
	if (req->flags & HIO_HTRE_ATTR_KEEPALIVE)
	{
		file->keep_alive = 1;
		file->res_mode_to_cli = FILE_RES_MODE_LENGTH;
	}
	else
	{
		file->keep_alive = 0;
		file->res_mode_to_cli = FILE_RES_MODE_CLOSE;
	}

	switch (file->req_method)
	{
		case HIO_HTTP_GET:
		{
			const hio_bch_t* actual_mime_type = mime_type;

			if (open_peer_with_mode(file, actual_file, O_RDONLY, &status_code, (mime_type? HIO_NULL: &actual_mime_type)) <= -1) goto done_with_status_code;
			if (process_range_header(file, req, &status_code) <= -1) goto done_with_status_code;

			if (file->etag_match)
			{
				status_code = HIO_HTTP_STATUS_NOT_MODIFIED;
				goto done_with_status_code;
			}

			/* normal full transfer */
		#if defined(HAVE_POSIX_FADVISE)
			posix_fadvise (file->peer, file->start_offset, file->end_offset - file->start_offset + 1, POSIX_FADV_SEQUENTIAL);
		#endif
			set_tcp_cork (file->client->sck);

			if (file_send_header_to_client(file, HIO_HTTP_STATUS_OK, 0, actual_mime_type) <= -1 ||
			    file_send_contents_to_client(file) <= -1) goto oops;

			break;
		}

		case HIO_HTTP_HEAD:
			if (open_peer_with_mode(file, actual_file, O_RDONLY, &status_code, HIO_NULL) <= -1) goto done_with_status_code;
			if (process_range_header(file, req, &status_code) <= -1) goto done_with_status_code;
			status_code = HIO_HTTP_STATUS_OK;
			goto done_with_status_code;

		case HIO_HTTP_POST:
		case HIO_HTTP_PUT:
			if (file->options & HIO_SVC_HTTS_FILE_READ_ONLY)
			{
				status_code = HIO_HTTP_STATUS_METHOD_NOT_ALLOWED;
				goto done_with_status_code;
			}

			if (open_peer_with_mode(file, actual_file, O_WRONLY | O_TRUNC | O_CREAT, &status_code, HIO_NULL) <= -1) goto done_with_status_code;

			/* the client input must be written to the peer side */
			file_mark_over (file, FILE_OVER_READ_FROM_PEER);
			break;

		case HIO_HTTP_DELETE:
			if (file->options & HIO_SVC_HTTS_FILE_READ_ONLY) 
			{
				status_code = HIO_HTTP_STATUS_METHOD_NOT_ALLOWED;
				goto done_with_status_code;
			}

			if (unlink(actual_file) <= -1)
			{
				if (errno != EISDIR || (errno == EISDIR && rmdir(actual_file) <= -1))
				{
					status_code = ERRNO_TO_STATUS_CODE(errno);
					goto done_with_status_code;
				}
			}

			status_code = HIO_HTTP_STATUS_OK;
			goto done_with_status_code;

		default:
			status_code = HIO_HTTP_STATUS_METHOD_NOT_ALLOWED;
		done_with_status_code:
			if (file_send_final_status_to_client(file, status_code, 0) <= -1) goto oops;
			file_mark_over (file, FILE_OVER_READ_FROM_PEER | FILE_OVER_WRITE_TO_PEER);
			break;
	}

	/* TODO: store current input watching state and use it when destroying the file data */
	if (hio_dev_sck_read(csck, !(file->over & FILE_OVER_READ_FROM_CLIENT)) <= -1) goto oops;
	hio_freemem (hio, actual_file);
	return 0;

oops:
	HIO_DEBUG2 (hio, "HTTS(%p) - file(c=%d) failure\n", htts, csck->hnd);
	if (file) file_halt_participating_devices (file);
	if (actual_file) hio_freemem (hio, actual_file);
	return -1;
}
