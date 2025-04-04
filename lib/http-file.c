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

#define FILE_ALLOW_UNLIMITED_REQ_CONTENT_LENGTH

#define FILE_OVER_READ_FROM_CLIENT (1 << 0)
#define FILE_OVER_READ_FROM_PEER   (1 << 1)
#define FILE_OVER_WRITE_TO_CLIENT  (1 << 2)
#define FILE_OVER_WRITE_TO_PEER    (1 << 3)
#define FILE_OVER_ALL (FILE_OVER_READ_FROM_CLIENT | FILE_OVER_READ_FROM_PEER | FILE_OVER_WRITE_TO_CLIENT | FILE_OVER_WRITE_TO_PEER)

struct file_t
{
	HIO_SVC_HTTS_TASK_HEADER;

	hio_svc_htts_task_on_kill_t on_kill; /* user-provided on_kill callback */

	int options;
	hio_svc_htts_file_cbs_t* cbs;
	int csck_tcp_cork;

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

	unsigned int over: 4; /* must be large enough to accomodate FILE_OVER_ALL */
	unsigned int client_htrd_recbs_changed: 1;
	unsigned int etag_match: 1;

	hio_dev_sck_on_read_t client_org_on_read;
	hio_dev_sck_on_write_t client_org_on_write;
	hio_dev_sck_on_disconnect_t client_org_on_disconnect;
	hio_htrd_recbs_t client_htrd_org_recbs;
};
typedef struct file_t file_t;

static void unbind_task_from_client (file_t* file, int rcdown);
static void unbind_task_from_peer (file_t* file, int rcdown);
static int file_send_contents_to_client (file_t* file);

static HIO_INLINE int get_tcp_cork (hio_dev_sck_t* sck)
{
	int n = -1;
	hio_scklen_t len = HIO_SIZEOF(n);
#if defined(TCP_CORK)
	#if defined(SOL_TCP)
	hio_dev_sck_getsockopt (sck, SOL_TCP, TCP_CORK, &n, &len);
	#elif defined(IPPROTO_TCP)
	hio_dev_sck_getsockopt (sck, IPPROTO_TCP, TCP_CORK, &n, &len);
	#endif
#endif
	return n;
}

static HIO_INLINE void set_tcp_cork (hio_dev_sck_t* sck, int tcp_cork)
{
#if defined(TCP_CORK)
	#if defined(SOL_TCP)
	hio_dev_sck_setsockopt (sck, SOL_TCP, TCP_CORK, &tcp_cork, HIO_SIZEOF(tcp_cork));
	#elif defined(IPPROTO_TCP)
	hio_dev_sck_setsockopt (sck, IPPROTO_TCP, TCP_CORK, &tcp_cork, HIO_SIZEOF(tcp_cork));
	#endif
#endif
}

static void file_halt_participating_devices (file_t* file)
{
	hio_dev_sck_t* csck = file->task_csck;

	HIO_DEBUG3 (file->htts->hio, "HTTS(%p) - file(c=%d,p=%d) Halting participating devices\n", file->htts, (int)(csck? csck->hnd: -1), (int)file->peer);

	if (csck) hio_dev_sck_halt (csck);
	unbind_task_from_peer (file, 1);
}

static void file_mark_over (file_t* file, int over_bits)
{
	hio_svc_htts_t* htts = file->htts;
	hio_t* hio = htts->hio;
	unsigned int old_over;

	old_over = file->over;
	file->over |= over_bits;

	HIO_DEBUG6 (hio, "HTTS(%p) - file(c=%d,p=%d) updating mark - old_over=%x | new-bits=%x => over=%x\n", htts, (int)file->task_csck->hnd, file->peer, (int)old_over, (int)over_bits, (int)file->over);

	if (!(old_over & FILE_OVER_READ_FROM_CLIENT) && (file->over & FILE_OVER_READ_FROM_CLIENT))
	{
		if (file->task_csck && hio_dev_sck_read(file->task_csck, 0) <= -1)
		{
			HIO_DEBUG3 (hio, "HTTS(%p) - file(c=%d,p=%d) halting client for failure to disable input watching\n", htts, (int)file->task_csck->hnd, file->peer);
			hio_dev_sck_halt (file->task_csck);
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
		HIO_DEBUG3 (hio, "HTTS(%p) - file(c=%d,p=%d) halting peer as it is unneeded\n", htts, (int)file->task_csck->hnd, file->peer);
		unbind_task_from_peer (file, 1);

		if (file->task_csck)
		{
			if (file->task_keep_client_alive)
			{
				if (file->csck_tcp_cork >= 0) set_tcp_cork (file->task_csck, file->csck_tcp_cork);

				/* the file task must not be accessed from here down as it could have been destroyed */
				HIO_DEBUG2 (hio, "HTTS(%p) - keeping client(%p) alive\n", htts, file->task_csck);
				HIO_ASSERT (hio, file->task_client->task == (hio_svc_htts_task_t*)file);
				unbind_task_from_client (file, 1);
			}
			else
			{
				HIO_DEBUG2 (hio, "HTTS(%p) - halting client(%p)\n", htts, file->task_csck);
				hio_dev_sck_shutdown (file->task_csck, HIO_DEV_SCK_SHUTDOWN_WRITE);
				hio_dev_sck_halt (file->task_csck);
				/* the file task will be detached from file->task_client->task by the upstream disconnect handler in http_svr.c */
			}
		}
	}
}

static int file_write_to_peer (file_t* file, const void* data, hio_iolen_t dlen)
{
	/* hio_t* hio = file->htts->hio; */

	if (dlen <= 0)
	{
		file_mark_over (file, FILE_OVER_WRITE_TO_PEER);
	}
	else
	{
		hio_iolen_t pos, rem, n;
		if (file->task_req_method == HIO_HTTP_GET) return 0;
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

static void file_on_kill (hio_svc_htts_task_t* task)
{
	file_t* file = (file_t*)task;
	hio_t* hio = file->htts->hio;

	HIO_DEBUG5 (hio, "HTTS(%p) - file(t=%p,c=%p[%d],p=%d) - killing the task\n", file->htts, file, file->task_client, (file->task_csck? file->task_csck->hnd: -1), file->peer);

	if (file->on_kill) file->on_kill (task);

	/* this callback function doesn't decrement the reference count on file because
	 * it is the task destruction callback. (passing 0 to unbind_task_from_peer/client) */
	unbind_task_from_peer (file, 0);

	if (file->task_csck)
	{
		HIO_ASSERT (hio, file->task_client != HIO_NULL);
		unbind_task_from_client (file, 0);
	}

	if (file->task_next) HIO_SVC_HTTS_TASKL_UNLINK_TASK (file); /* detach from the htts service only if it's attached */

	HIO_DEBUG5 (hio, "HTTS(%p) - file(t=%p,c=%p[%d],p=%d) - killed the task\n", file->htts, file, file->task_client, (file->task_csck? file->task_csck->hnd: -1), file->peer);
}

static void file_client_on_disconnect (hio_dev_sck_t* sck)
{
	hio_t* hio = sck->hio;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	file_t* file = (file_t*)cli->task;
	hio_svc_htts_t* htts = file->htts;

	HIO_ASSERT (hio, sck == cli->sck);
	HIO_ASSERT (hio, sck == file->task_csck);

	HIO_DEBUG4 (hio, "HTTS(%p) - file(t=%p,c=%p,csck=%p) - client socket disconnect notified\n", htts, file, sck, cli);

	if (file)
	{
		HIO_SVC_HTTS_TASK_RCUP ((hio_svc_htts_task_t*)file);

		/* detach the task from the client and the client socket */
		unbind_task_from_client (file, 1);

		/* the current file peer implemenation is not async. so there is no IO event associated
		 * when the client side is disconnected, simple close the peer side as it's not needed.
		 * this behavior is different from http-fcgi or http-cgi */
		unbind_task_from_peer (file, 1);

		/* call the parent handler*/
		/*if (file->client_org_on_disconnect) file->client_org_on_disconnect (sck);*/
		if (sck->on_disconnect) sck->on_disconnect (sck); /* restored to the orginal parent handelr in unbind_task_from_client() */

		HIO_SVC_HTTS_TASK_RCDOWN ((hio_svc_htts_task_t*)file);
	}

	HIO_DEBUG4 (hio, "HTTS(%p) - file(t=%p,c=%p,csck=%p) - client socket disconnect handled\n", htts, file, sck, cli);
	/* Note: after this callback, the actual device pointed to by 'sck' will be freed in the main loop. */
}

static int file_client_on_read (hio_dev_sck_t* sck, const void* buf, hio_iolen_t len, const hio_skad_t* srcaddr)
{
	hio_t* hio = sck->hio;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	file_t* file = (file_t*)cli->task;

	HIO_ASSERT (hio, sck == cli->sck);
	HIO_ASSERT (hio, sck == file->task_csck);

	if (len <= -1)
	{
		/* read error */
		HIO_DEBUG3 (cli->htts->hio, "HTTS(%p) - file(c=%d,p=%d) read error on client\n", file->htts, (int)sck->hnd, file->peer);
		goto oops;
	}

	if (file->peer <= -1)
	{
		/* the peer is gone or not even opened */
		HIO_DEBUG3 (cli->htts->hio, "HTTS(%p) - file(c=%d,p=%d) read on client, no peer to write\n", file->htts, (int)sck->hnd, file->peer);
		goto oops; /* do what?  just return 0? */
	}

	if (len == 0)
	{
		/* EOF on the client side. arrange to close */
		HIO_DEBUG3 (cli->htts->hio, "HTTS(%p) - file(c=%d,p=%d) EOF detected on client\n", file->htts, (int)sck->hnd, file->peer);

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
			HIO_DEBUG3 (cli->htts->hio, "HTTS(%p) - file(c=%d,p=%d) excessive data after contents on client\n", file->htts, (int)sck->hnd, file->peer);
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
	file_t* file = (file_t*)cli->task;
	int n;

	n = file->client_org_on_write? file->client_org_on_write(sck, wrlen, wrctx, dstaddr): 0;

	if (wrlen == 0)
	{
		file_mark_over (file, FILE_OVER_WRITE_TO_CLIENT);
	}
	else if (wrlen > 0)
	{
		if (file->task_req_method == HIO_HTTP_GET)
		{
			if (file_send_contents_to_client (file) <= -1) n = -1;
		}

		if ((file->over & FILE_OVER_READ_FROM_PEER) && file->task_res_pending_writes <= 0)
		{
			file_mark_over (file, FILE_OVER_WRITE_TO_CLIENT);
		}
	}

	if (n <= -1 || wrlen <= -1) file_halt_participating_devices (file);
	return 0;
}

/* --------------------------------------------------------------------- */

static int file_client_htrd_poke (hio_htrd_t* htrd, hio_htre_t* req)
{
	/* client request got completed */
	hio_svc_htts_cli_htrd_xtn_t* htrdxtn = (hio_svc_htts_cli_htrd_xtn_t*)hio_htrd_getxtn(htrd);
	hio_dev_sck_t* sck = htrdxtn->sck;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	file_t* file = (file_t*)cli->task;

	/* indicate EOF to the client peer */
	if (file_write_to_peer(file, HIO_NULL, 0) <= -1) return -1;

	if (file->task_req_method != HIO_HTTP_GET)
	{
		if (hio_svc_htts_task_sendfinalres((hio_svc_htts_task_t*)file, HIO_HTTP_STATUS_OK, HIO_NULL, HIO_NULL, 0) <= -1) return -1;
	}

	file_mark_over (file, FILE_OVER_READ_FROM_CLIENT);
	return 0;
}

static int file_client_htrd_push_content (hio_htrd_t* htrd, hio_htre_t* req, const hio_bch_t* data, hio_oow_t dlen)
{
	hio_svc_htts_cli_htrd_xtn_t* htrdxtn = (hio_svc_htts_cli_htrd_xtn_t*)hio_htrd_getxtn(htrd);
	hio_dev_sck_t* sck = htrdxtn->sck;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	file_t* file = (file_t*)cli->task;

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
	hio_svc_htts_cli_t* cli = file->task_client;
	hio_foff_t content_length;

	if (HIO_UNLIKELY(!cli))
	{
		/* client disconnected or not connectd */
		return 0;
	}

	content_length = file->end_offset - file->start_offset + 1;
	if (status_code == HIO_HTTP_STATUS_OK && file->total_size != content_length) status_code = HIO_HTTP_STATUS_PARTIAL_CONTENT;

	if (hio_svc_htts_task_startreshdr((hio_svc_htts_task_t*)file, status_code, HIO_NULL, 0) <= -1) return -1;

	if (mime_type && mime_type[0] != '\0' && hio_svc_htts_task_addreshdr((hio_svc_htts_task_t*)file, "Content-Type", mime_type) <= -1) return -1;

	if ((file->task_req_method == HIO_HTTP_GET || file->task_req_method == HIO_HTTP_HEAD) &&
	    hio_svc_htts_task_addreshdr((hio_svc_htts_task_t*)file, "ETag", file->peer_etag) <= -1) return -1;

	if (status_code == HIO_HTTP_STATUS_PARTIAL_CONTENT &&
	    hio_svc_htts_task_addreshdrfmt((hio_svc_htts_task_t*)file, "Content-Ranges", "bytes %ju-%ju/%ju", (hio_uintmax_t)file->start_offset, (hio_uintmax_t)file->end_offset, (hio_uintmax_t)file->total_size) <= -1) return -1;

/* ----- */
// TODO: Allow-Contents
// Allow-Headers... support custom headers...
	if (hio_svc_htts_task_addreshdr((hio_svc_htts_task_t*)file, "Access-Control-Allow-Origin", "*") <= -1) return -1;
/* ----- */

	if (hio_svc_htts_task_addreshdrfmt((hio_svc_htts_task_t*)file, "Content-Length", "%ju", (hio_uintmax_t)content_length) <= -1) return -1;

	if (hio_svc_htts_task_endreshdr((hio_svc_htts_task_t*)file) <= -1) return -1;

	return 0;
}

static void send_contents_to_client_later (hio_t* hio, const hio_ntime_t* now, hio_tmrjob_t* tmrjob)
{
	file_t* file = (file_t*)tmrjob->ctx;
	if (file_send_contents_to_client(file) <= -1) file_halt_participating_devices (file);
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
		if (hio_svc_htts_task_addresbodyfromfile((hio_svc_htts_task_t*)file, file->peer, file->cur_offset, lim) <= -1) return -1;
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
		/*if (file_write_to_client(file, file->peer_buf, n) <= -1) return -1;*/
		if (hio_svc_htts_task_addresbody((hio_svc_htts_task_t*)file, file->peer_buf, n) <= -1) return -1;

		file->cur_offset += n;

	/*	if (file->cur_offset > file->end_offset)  should i check this or wait until this function is invoked?
			file_mark_over (file, FILE_OVER_READ_FROM_PEER);*/
	}

	return 0;
}

#define ERRNO_TO_STATUS_CODE(x) ( \
	((x) == ENOENT || (x) == ENOTDIR)? HIO_HTTP_STATUS_NOT_FOUND: \
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

static int open_peer_with_mode (file_t* file, const hio_bch_t* actual_file, int flags, int* error_status, const hio_bch_t** res_mime_type)
{
	struct stat st;

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

	if (((flags | O_RDONLY) && fstat(file->peer, &st) >= 0 && S_ISDIR(st.st_mode)) && /* only for read operation */
	    (file->cbs && file->cbs->open_dir_list)) /* directory listing is enabled */
	{
		int alt_fd;

		if (!file->task_req_qpath_ending_with_slash)
		{
			*error_status = HIO_HTTP_STATUS_MOVED_PERMANENTLY;
			close (file->peer);
			file->peer = -1;
			return -1;
		}

		alt_fd = file->cbs->open_dir_list(file->htts, file->task_req_qpath, actual_file, res_mime_type, file->cbs->ctx);
		if (alt_fd >= 0)
		{
			close (file->peer);
			file->peer = alt_fd;
		}
	}
	else
	{
		if (res_mime_type && file->cbs && file->cbs->get_mime_type)
		{
			const hio_bch_t* mime_type;
			mime_type = file->cbs->get_mime_type(file->htts, file->task_req_qpath, actual_file, file->cbs->ctx);
			if (mime_type) *res_mime_type = mime_type;
		}
	}

	return 0;
}

/* ----------------------------------------------------------------------- */

static void bind_task_to_client (file_t* file, hio_dev_sck_t* csck)
{
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(csck);

	HIO_ASSERT (file->htts->hio, cli->sck == csck);
	HIO_ASSERT (file->htts->hio, cli->task == HIO_NULL);

	/* file->task_client and file->task_csck are set in hio_svc_htts_task_make() */

	/* remember the client socket's io event handlers */
	file->client_org_on_read = csck->on_read;
	file->client_org_on_write = csck->on_write;
	file->client_org_on_disconnect = csck->on_disconnect;

	/* set new io events handlers on the client socket */
	csck->on_read = file_client_on_read;
	csck->on_write = file_client_on_write;
	csck->on_disconnect = file_client_on_disconnect;

	cli->task = (hio_svc_htts_task_t*)file;
	HIO_SVC_HTTS_TASK_RCUP (file);
}

static void unbind_task_from_client (file_t* file, int rcdown)
{
	hio_dev_sck_t* csck = file->task_csck;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(csck);

	if (cli) /* only if it's bound */
	{
		HIO_ASSERT (file->htts->hio, file->task_client != HIO_NULL);
		HIO_ASSERT (file->htts->hio, file->task_csck != HIO_NULL);
		HIO_ASSERT (file->htts->hio, file->task_client->task == (hio_svc_htts_task_t*)file);
		HIO_ASSERT (file->htts->hio, file->task_client->htrd != HIO_NULL);

		if (file->client_htrd_recbs_changed)
		{
			hio_htrd_setrecbs (file->task_client->htrd, &file->client_htrd_org_recbs);
			file->client_htrd_recbs_changed = 0;
		}

		if (file->client_org_on_read)
		{
			csck->on_read = file->client_org_on_read;
			file->client_org_on_read = HIO_NULL;
		}

		if (file->client_org_on_write)
		{
			csck->on_write = file->client_org_on_write;
			file->client_org_on_write = HIO_NULL;
		}

		if (file->client_org_on_disconnect)
		{
			csck->on_disconnect = file->client_org_on_disconnect;
			file->client_org_on_disconnect = HIO_NULL;
		}

		/* there is some ordering issue in using HIO_SVC_HTTS_TASK_UNREF()
		 * because it can destroy the file itself. so reset file->task_client->task
		 * to null and call RCDOWN() later */
		file->task_client->task = HIO_NULL;

		/* these two lines are also done in csck_on_disconnect() in http-svr.c because the socket is destroyed.
		 * the same lines here are because the task is unbound while the socket is still alive */
		file->task_client = HIO_NULL;
		file->task_csck = HIO_NULL;

		/* enable input watching on the socket being unbound */
		if (file->task_keep_client_alive && hio_dev_sck_read(csck, 1) <= -1)
		{
			HIO_DEBUG2 (file->htts->hio, "HTTS(%p) - halting client(%p) for failure to enable input watching\n", file->htts, csck);
			hio_dev_sck_halt (csck);
		}

		if (rcdown) HIO_SVC_HTTS_TASK_RCDOWN ((hio_svc_htts_task_t*)file);
	}
}

/* ----------------------------------------------------------------------- */

static int bind_task_to_peer (file_t* file, hio_htre_t* req, const hio_bch_t* file_path, const hio_bch_t* mime_type)
{
	int status_code;

	switch (file->task_req_method)
	{
		case HIO_HTTP_GET:
		case HIO_HTTP_HEAD:
		{
			const hio_bch_t* actual_mime_type = mime_type;

			if (open_peer_with_mode(file, file_path, O_RDONLY, &status_code, (mime_type? HIO_NULL: &actual_mime_type)) <= -1 ||
			    process_range_header(file, req, &status_code) <= -1) goto oops_with_status_code;

			if (HIO_LIKELY(file->task_req_method == HIO_HTTP_GET))
			{
				if (file->etag_match)
				{
					status_code = HIO_HTTP_STATUS_NOT_MODIFIED;
					goto oops_with_status_code;
				}

				/* normal full transfer */
			#if defined(HAVE_POSIX_FADVISE)
				posix_fadvise (file->peer, file->start_offset, file->end_offset - file->start_offset + 1, POSIX_FADV_SEQUENTIAL);
			#endif
				/* TODO: store the current value and let the program restore to the current value when exiting.. */
				file->csck_tcp_cork = get_tcp_cork (file->task_csck);
				if (file->csck_tcp_cork >= 0) set_tcp_cork (file->task_csck, 1);

				if (file_send_header_to_client(file, HIO_HTTP_STATUS_OK, 0, actual_mime_type) <= -1) goto oops;
				if (file_send_contents_to_client(file) <= -1) goto oops;
			}
			else
			{
				if (file_send_header_to_client(file, HIO_HTTP_STATUS_OK, 0, actual_mime_type) <= -1) goto oops;
				/* no content must be transmitted for HEAD despite Content-Length in the header. */
				goto oops_with_status_code_2;
			}
			break;
		}

		case HIO_HTTP_POST:
		case HIO_HTTP_PUT:
			if (file->options & HIO_SVC_HTTS_FILE_READ_ONLY)
			{
				status_code = HIO_HTTP_STATUS_METHOD_NOT_ALLOWED;
				goto oops_with_status_code;
			}

			if (open_peer_with_mode(file, file_path, O_WRONLY | O_TRUNC | O_CREAT, &status_code, HIO_NULL) <= -1) goto oops_with_status_code;

			/* the client input must be written to the peer side */
			file_mark_over (file, FILE_OVER_READ_FROM_PEER);
			break;

		case HIO_HTTP_DELETE:
			if (file->options & HIO_SVC_HTTS_FILE_READ_ONLY)
			{
				status_code = HIO_HTTP_STATUS_METHOD_NOT_ALLOWED;
				goto oops_with_status_code;
			}

			if (unlink(file_path) <= -1)
			{
				if (errno != EISDIR || (errno == EISDIR && rmdir(file_path) <= -1))
				{
					status_code = ERRNO_TO_STATUS_CODE(errno);
					goto oops_with_status_code;
				}
			}

			status_code = HIO_HTTP_STATUS_OK;
			goto oops_with_status_code;

		default:
			status_code = HIO_HTTP_STATUS_METHOD_NOT_ALLOWED;
			goto oops_with_status_code;
	}

	HIO_SVC_HTTS_TASK_RCUP (file); /* for file->peer opened */
	return 0;


	/* the task can be terminated because the requested job has been
	 * completed or it can't proceed for various reasons */
oops_with_status_code:
	hio_svc_htts_task_sendfinalres((hio_svc_htts_task_t*)file, status_code, HIO_NULL, HIO_NULL, 0);
oops_with_status_code_2:
	file_mark_over (file, FILE_OVER_READ_FROM_PEER | FILE_OVER_WRITE_TO_PEER);
oops:
	return -1;
}

static void unbind_task_from_peer (file_t* file, int rcdown)
{
	hio_svc_htts_t* htts = file->htts;
	hio_t* hio = htts->hio;
	int n = 0;

	if (file->peer_tmridx != HIO_TMRIDX_INVALID)
	{
		hio_deltmrjob (hio, file->peer_tmridx);
		HIO_ASSERT (hio, file->peer_tmridx == HIO_TMRIDX_INVALID);
	}

	if (file->peer >= 0)
	{
		close (file->peer);
		file->peer = -1;
		n++;
	}

	if (rcdown)
	{
		while (n > 0)
		{
			n--;
			HIO_SVC_HTTS_TASK_RCDOWN((hio_svc_htts_task_t*)file);
		}
	}
}

/* ----------------------------------------------------------------------- */

static int setup_for_content_length(file_t* file, hio_htre_t* req)
{
	int have_content;

#if defined(FILE_ALLOW_UNLIMITED_REQ_CONTENT_LENGTH)
	have_content = file->task_req_conlen > 0 || file->task_req_conlen_unlimited;
#else
	have_content = file->task_req_conlen > 0;
#endif

	if (have_content)
	{
		/* change the callbacks to subscribe to contents to be uploaded */
		file->client_htrd_org_recbs = *hio_htrd_getrecbs(file->task_client->htrd);
		file_client_htrd_recbs.peek = file->client_htrd_org_recbs.peek;
		hio_htrd_setrecbs (file->task_client->htrd, &file_client_htrd_recbs);
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

	return 0;
}

int hio_svc_htts_dofile (hio_svc_htts_t* htts, hio_dev_sck_t* csck, hio_htre_t* req, const hio_bch_t* docroot, const hio_bch_t* filepath, const hio_bch_t* mime_type, int options, hio_svc_htts_task_on_kill_t on_kill, hio_svc_htts_file_cbs_t* cbs)
{
	hio_t* hio = htts->hio;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(csck);
	file_t* file = HIO_NULL;
	hio_bch_t* actual_file = HIO_NULL;
	int status_code = HIO_HTTP_STATUS_INTERNAL_SERVER_ERROR;
	int bound_to_client = 0, bound_to_peer = 0;

	/* ensure that you call this function before any contents is received */
	HIO_ASSERT (hio, hio_htre_getcontentlen(req) == 0);
	HIO_ASSERT (hio, cli->sck == csck);

	HIO_DEBUG5 (hio, "HTTS(%p) - file(c=%d) - [%hs] %hs%hs\n", htts, (int)csck->hnd, cli->cli_addr_bcstr, (docroot[0] == '/' && docroot[1] == '\0' && filepath[0] == '/'? "": docroot), filepath);

	if (cli->task)
	{
		hio_seterrbfmt (hio, HIO_EPERM, "duplicate task request prohibited");
		goto oops;
	}

	file = (file_t*)hio_svc_htts_task_make(htts, HIO_SIZEOF(*file), file_on_kill, req, csck);
	if (HIO_UNLIKELY(!file)) goto oops;
	HIO_SVC_HTTS_TASK_RCUP ((hio_svc_htts_task_t*)file); /* for temporary protection */

	actual_file = hio_svc_htts_dupmergepaths(htts, docroot, filepath);
	if (HIO_UNLIKELY(!actual_file)) goto oops;

	file->options = options;
	file->cbs = cbs; /* the given pointer must outlive the lifespan of the while file handling cycle. */
	file->sendfile_ok = hio_dev_sck_sendfileok(csck);
	file->peer_tmridx = HIO_TMRIDX_INVALID;
	file->peer = -1;

	bind_task_to_client (file, csck); /* the file task's reference count is incremented */
	bound_to_client = 1;

	if (hio_svc_htts_task_handleexpect100((hio_svc_htts_task_t*)file, 0) <= -1) goto oops;
	if (setup_for_content_length(file, req) <= -1) goto oops;

	if (bind_task_to_peer(file, req, actual_file, mime_type) <= -1) goto oops;
	bound_to_peer = 1;

	/* TODO: store current input watching state and use it when destroying the file data */
	if (hio_dev_sck_read(csck, !(file->over & FILE_OVER_READ_FROM_CLIENT)) <= -1) goto oops;
	hio_freemem (hio, actual_file);

	HIO_SVC_HTTS_TASKL_APPEND_TASK (&htts->task, (hio_svc_htts_task_t*)file);
	HIO_SVC_HTTS_TASK_RCDOWN ((hio_svc_htts_task_t*)file);

	/* set the on_kill callback only if this function can return success.
	 * the on_kill callback won't be executed if this function returns failure. */
	file->on_kill = on_kill;
	return 0;

oops:
	HIO_DEBUG2 (hio, "HTTS(%p) - file(c=%d) failure\n", htts, csck->hnd);
	if (file)
	{
		hio_svc_htts_task_sendfinalres((hio_svc_htts_task_t*)file, status_code, HIO_NULL, HIO_NULL, 1);
		if (bound_to_peer) unbind_task_from_peer (file, 0);
		if (bound_to_client) unbind_task_from_client (file, 0);
		file_halt_participating_devices (file);
		if (actual_file) hio_freemem (hio, actual_file);
		HIO_SVC_HTTS_TASK_RCDOWN ((hio_svc_htts_task_t*)file);
	}
	return -1;
}
