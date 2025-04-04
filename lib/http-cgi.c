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
#include <errno.h>

#if defined(HAVE_CRT_EXTERNS_H)
#	include <crt_externs.h> /* _NSGetEnviron */
#endif

#define CGI_ALLOW_UNLIMITED_REQ_CONTENT_LENGTH


#define CGI_PENDING_IO_THRESHOLD 5

#define CGI_OVER_READ_FROM_CLIENT (1 << 0)
#define CGI_OVER_READ_FROM_PEER   (1 << 1)
#define CGI_OVER_WRITE_TO_CLIENT  (1 << 2)
#define CGI_OVER_WRITE_TO_PEER    (1 << 3)
#define CGI_OVER_ALL (CGI_OVER_READ_FROM_CLIENT | CGI_OVER_READ_FROM_PEER | CGI_OVER_WRITE_TO_CLIENT | CGI_OVER_WRITE_TO_PEER)

/* ----------------------------------------------------------------------- */

struct cgi_t
{
	HIO_SVC_HTTS_TASK_HEADER;

	hio_svc_htts_task_on_kill_t on_kill; /* user-provided on_kill callback */

	int options;
	hio_oow_t peer_pending_writes;
	hio_dev_pro_t* peer;
	hio_htrd_t* peer_htrd;

	unsigned int over: 4; /* must be large enough to accomodate CGI_OVER_ALL */
	unsigned int client_htrd_recbs_changed: 1;
	unsigned int ntask_cgis_inced: 1;

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

/* ----------------------------------------------------------------------- */

static void unbind_task_from_client (cgi_t* cgi, int rcdown);
static void unbind_task_from_peer (cgi_t* cgi, int rcdown);

/* ----------------------------------------------------------------------- */

static int inc_ntask_cgis (hio_svc_htts_t* htts)
{
#if !(defined(HCL_ATOMIC_LOAD) && defined(HCL_ATOMIC_CMP_XCHG))
	hio_spl_lock (&htts->stat.spl_ntask_cgis);
	if (htts->stat.ntask_cgis >= htts->option.task_cgi_max)
	{
		hio_spl_unlock (&htts->stat.spl_ntask_cgis);
		hio_seterrbfmt (htts->hio, HIO_ENOCAPA, "too many cgi tasks");
		return -1;
	}
	htts->stat.ntask_cgis++;
	hio_spl_unlock (&htts->stat.spl_ntask_cgis);
#else
	int ok;
	do
	{
		hio_oow_t ntask_cgis;
		ntask_cgis = HCL_ATOMIC_LOAD(&htts->stat.ntask_cgis);
		if (ntask_cgis >= htts->option.task_cgi_max)
		{
			hio_seterrbfmt (htts->hio, HIO_ENOCAPA, "too many cgi tasks");
			return -1;
		}
		ok = HCL_ATOMIC_CMP_XCHG(&htts->stat.ntask_cgis, &ntask_cgis, ntask_cgis + 1);
	}
	while (!ok);
#endif
	return 0;
}

static void dec_ntask_cgis (hio_svc_htts_t* htts)
{
#if !(defined(HCL_ATOMIC_LOAD) && defined(HCL_ATOMIC_CMP_XCHG))
	hio_spl_lock (&htts->stat.spl_ntask_cgis);
	htts->stat.ntask_cgis--;
	hio_spl_unlock (&htts->stat.spl_ntask_cgis);
#else
	int ok;
	do
	{
		hio_oow_t ntask_cgis;
		ntask_cgis = HCL_ATOMIC_LOAD(&htts->stat.ntask_cgis);
		ok = HCL_ATOMIC_CMP_XCHG(&htts->stat.ntask_cgis, &ntask_cgis, ntask_cgis - 1);
	}
	while (!ok);
#endif
}

/* ----------------------------------------------------------------------- */

static void cgi_halt_participating_devices (cgi_t* cgi)
{
	HIO_DEBUG5 (cgi->htts->hio, "HTTS(%p) - cgi(t=%p,c=%p(%d),p=%p) Halting participating devices\n", cgi->htts, cgi, cgi->task_csck, (cgi->task_csck? cgi->task_csck->hnd: -1), cgi->peer);

	if (cgi->task_csck) hio_dev_sck_halt (cgi->task_csck);

	/* check for peer as it may not have been started */
	if (cgi->peer) hio_dev_pro_halt (cgi->peer);
}

static int cgi_write_to_peer (cgi_t* cgi, const void* data, hio_iolen_t dlen)
{
	if (cgi->peer)
	{
		cgi->peer_pending_writes++;
		if (hio_dev_pro_write(cgi->peer, data, dlen, HIO_NULL) <= -1)
		{
			cgi->peer_pending_writes--;
			return -1;
		}

		if (cgi->peer_pending_writes > CGI_PENDING_IO_THRESHOLD)
		{
			/* suspend input watching */
			if (cgi->task_csck && hio_dev_sck_read(cgi->task_csck, 0) <= -1) return -1;
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

	HIO_DEBUG8 (hio, "HTTS(%p) - cgi(t=%p,c=%p[%d],p=%p) - old_over=%x | new-bits=%x => over=%x\n", cgi->htts, cgi, cgi->task_client, (cgi->task_csck? cgi->task_csck->hnd: -1), cgi->peer, (int)old_over, (int)over_bits, (int)cgi->over);

	if (!(old_over & CGI_OVER_READ_FROM_CLIENT) && (cgi->over & CGI_OVER_READ_FROM_CLIENT))
	{
		if (cgi->task_csck && hio_dev_sck_read(cgi->task_csck, 0) <= -1)
		{
			HIO_DEBUG5 (hio, "HTTS(%p) - cgi(t=%p,c=%p[%d],p=%p) - halting client for failure to disable input watching\n", cgi->htts, cgi, cgi->task_client, (cgi->task_csck? cgi->task_csck->hnd: -1), cgi->peer);
			hio_dev_sck_halt (cgi->task_csck);
		}
	}

	if (!(old_over & CGI_OVER_READ_FROM_PEER) && (cgi->over & CGI_OVER_READ_FROM_PEER))
	{
		if (cgi->peer && hio_dev_pro_read(cgi->peer, HIO_DEV_PRO_OUT, 0) <= -1)
		{
			HIO_DEBUG5 (hio, "HTTS(%p) - cgi(t=%p,c=%p[%d],p=%p) - halting peer for failure to disable input watching\n", cgi->htts, cgi, cgi->task_client, (cgi->task_csck? cgi->task_csck->hnd: -1), cgi->peer);
			hio_dev_pro_halt (cgi->peer);
		}
	}

	if (old_over != CGI_OVER_ALL && cgi->over == CGI_OVER_ALL)
	{
		/* ready to stop */
		if (cgi->peer)
		{
			HIO_DEBUG5 (hio, "HTTS(%p) - cgi(t=%p,c=%p[%d],p=%p) - halting unneeded peer\n", cgi->htts, cgi, cgi->task_client, (cgi->task_csck? cgi->task_csck->hnd: -1), cgi->peer);
			hio_dev_pro_halt (cgi->peer);
		}

		if (cgi->task_csck)
		{
			HIO_ASSERT (hio, cgi->task_client != HIO_NULL);

			if (cgi->task_keep_client_alive)
			{
				HIO_DEBUG5 (hio, "HTTS(%p) - cgi(t=%p,c=%p[%d],p=%p) - keeping client alive\n", cgi->htts, cgi, cgi->task_client, (cgi->task_csck? cgi->task_csck->hnd: -1), cgi->peer);
				HIO_ASSERT (cgi->htts->hio, cgi->task_client->task == (hio_svc_htts_task_t*)cgi);
				unbind_task_from_client (cgi, 1);
				/* cgi must not be accessed from here down as it could have been destroyed */
			}
			else
			{
				HIO_DEBUG5 (hio, "HTTS(%p) - cgi(t=%p,c=%p[%d],p=%p) - halting client\n", cgi->htts, cgi, cgi->task_client, (cgi->task_csck? cgi->task_csck->hnd: -1), cgi->peer);
				hio_dev_sck_shutdown (cgi->task_csck, HIO_DEV_SCK_SHUTDOWN_WRITE);
				hio_dev_sck_halt (cgi->task_csck);
			}
		}
	}
}

static void cgi_on_kill (hio_svc_htts_task_t* task)
{
	cgi_t* cgi = (cgi_t*)task;
	hio_t* hio = cgi->htts->hio;

	HIO_DEBUG5 (hio, "HTTS(%p) - cgi(t=%p,c=%p[%d],p=%p) - killing the task\n", cgi->htts, cgi, cgi->task_client, (cgi->task_csck? cgi->task_csck->hnd: -1), cgi->peer);

	if (cgi->on_kill) cgi->on_kill (task);

	/* [NOTE]
	 * 1. if hio_svc_htts_task_kill() is called, cgi->peer, cgi->peer_htrd, cgi->task_csck,
	 *    cgi->task_client may not not null.
	 * 2. this callback function doesn't decrement the reference count on cgi because
	 *    it is the task destruction callback. (passing 0 to unbind_task_from_peer/client)
	 */

	unbind_task_from_peer (cgi, 0);

	if (cgi->task_csck)
	{
		HIO_ASSERT (hio, cgi->task_client != HIO_NULL);
		unbind_task_from_client (cgi, 0);
	}

	if (cgi->task_next) HIO_SVC_HTTS_TASKL_UNLINK_TASK (cgi); /* detach from the htts service only if it's attached */

	if (cgi->ntask_cgis_inced)
	{
		dec_ntask_cgis (cgi->htts);
		cgi->ntask_cgis_inced = 0;
	}

	HIO_DEBUG5 (hio, "HTTS(%p) - cgi(t=%p,c=%p[%d],p=%p) - killed the task\n", cgi->htts, cgi, cgi->task_client, (cgi->task_csck? cgi->task_csck->hnd: -1), cgi->peer);
}

static void cgi_peer_on_close (hio_dev_pro_t* pro, hio_dev_pro_sid_t sid)
{
	hio_t* hio = pro->hio;
	cgi_peer_xtn_t* peer_xtn = hio_dev_pro_getxtn(pro);
	cgi_t* cgi = peer_xtn->cgi;

	if (!cgi) return; /* cgi task already gone */

	switch (sid)
	{
		case HIO_DEV_PRO_MASTER:
			HIO_DEBUG3 (hio, "HTTS(%p) - peer %p(pid=%d) closing master\n", cgi->htts, pro, (int)pro->child_pid);

			/* reset cgi->peer before calling unbind_task_from_peer() because this is the peer close callback */
			cgi->peer = HIO_NULL;
			HIO_SVC_HTTS_TASK_RCDOWN((hio_svc_htts_task_t*)cgi); /* if not reset, this would be done in unbind_task_from_peer */

			unbind_task_from_peer (cgi, 1);
			break;

		case HIO_DEV_PRO_OUT:
			/* the output from peer closing. input to the client must be ended */
			HIO_ASSERT (hio, cgi->peer == pro);
			HIO_DEBUG4 (hio, "HTTS(%p) - peer %p(pid=%d) closing slave[%d]\n", cgi->htts, pro, (int)pro->child_pid, sid);

			if (!(cgi->over & CGI_OVER_READ_FROM_PEER))
			{
				if (hio_svc_htts_task_endbody((hio_svc_htts_task_t*)cgi) <= -1)
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
			n = hio_svc_htts_task_endbody((hio_svc_htts_task_t*)cgi);
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

			if (!cgi->task_res_started && !(cgi->over & CGI_OVER_WRITE_TO_CLIENT))
			{
				hio_svc_htts_task_sendfinalres ((hio_svc_htts_task_t*)cgi, HIO_HTTP_STATUS_BAD_GATEWAY, HIO_NULL, HIO_NULL, 1); /* don't care about error because it jumps to oops below anyway */
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
	cgi_halt_participating_devices (cgi);
	return 0;
}

static int cgi_peer_on_write (hio_dev_pro_t* pro, hio_iolen_t wrlen, void* wrctx)
{
	hio_t* hio = pro->hio;
	cgi_peer_xtn_t* peer = hio_dev_pro_getxtn(pro);
	cgi_t* cgi = peer->cgi;

	if (!cgi) return 0; /* there is nothing i can do. the cgi is being cleared or has been cleared already. */

	HIO_ASSERT (hio, cgi->peer == pro);

	if (wrlen <= -1)
	{
		HIO_DEBUG3 (hio, "HTTS(%p) - unable to write to peer %p(pid=%u)\n", cgi->htts, pro, (int)pro->child_pid);
		goto oops;
	}
	else if (wrlen == 0)
	{
		/* indicated EOF */
		/* do nothing here as i didn't increment peer_pending_writes when making the write request */

		cgi->peer_pending_writes--;
		HIO_ASSERT (hio, cgi->peer_pending_writes == 0);
		HIO_DEBUG3 (hio, "HTTS(%p) - indicated EOF to peer %p(pid=%u)\n", cgi->htts, pro, (int)pro->child_pid);
		/* indicated EOF to the peer side. i need no more data from the client side.
		 * i don't need to enable input watching in the client side either */
		cgi_mark_over (cgi, CGI_OVER_WRITE_TO_PEER);
	}
	else
	{
		HIO_ASSERT (hio, cgi->peer_pending_writes > 0);

		cgi->peer_pending_writes--;
		if (cgi->peer_pending_writes == CGI_PENDING_IO_THRESHOLD)
		{
			if (!(cgi->over & CGI_OVER_READ_FROM_CLIENT) &&
			    hio_dev_sck_read(cgi->task_csck, 1) <= -1) goto oops;
		}

		if ((cgi->over & CGI_OVER_READ_FROM_CLIENT) && cgi->peer_pending_writes <= 0)
		{
			cgi_mark_over (cgi, CGI_OVER_WRITE_TO_PEER);
		}
	}

	return 0;

oops:
	cgi_halt_participating_devices (cgi);
	return 0;
}


static int peer_capture_response_header (hio_htre_t* req, const hio_bch_t* key, const hio_htre_hdrval_t* val, void* ctx)
{
	return hio_svc_htts_task_addreshdrs((hio_svc_htts_task_t*)(cgi_t*)ctx, key, val);
}

static int peer_htrd_peek (hio_htrd_t* htrd, hio_htre_t* req)
{
	cgi_peer_xtn_t* peer = hio_htrd_getxtn(htrd);
	cgi_t* cgi = peer->cgi;
	hio_svc_htts_cli_t* cli = cgi->task_client;

	if (HIO_LIKELY(cli))
	{
		int status_code = HIO_HTTP_STATUS_OK;
		const hio_bch_t* status_desc = HIO_NULL;
		int chunked;

		if (req->attr.status) hio_parse_http_status_header_value(req->attr.status, &status_code, &status_desc);

		chunked = cgi->task_keep_client_alive && !req->attr.content_length;

		if (hio_svc_htts_task_startreshdr((hio_svc_htts_task_t*)cgi, status_code, status_desc, chunked) <= -1 ||
		    hio_htre_walkheaders(req, peer_capture_response_header, cgi) <= -1 ||
		    hio_svc_htts_task_endreshdr((hio_svc_htts_task_t*)cgi) <= -1) return -1;
	}

	return 0;
}

static int peer_htrd_poke (hio_htrd_t* htrd, hio_htre_t* req)
{
	/* peer response got completed */
	cgi_peer_xtn_t* peer = hio_htrd_getxtn(htrd);
	cgi_t* cgi = peer->cgi;
	int n;

	n = hio_svc_htts_task_endbody((hio_svc_htts_task_t*)cgi);
	cgi_mark_over (cgi, CGI_OVER_READ_FROM_PEER);
	return n;
}

static int peer_htrd_push_content (hio_htrd_t* htrd, hio_htre_t* req, const hio_bch_t* data, hio_oow_t dlen)
{
	cgi_peer_xtn_t* peer = hio_htrd_getxtn(htrd);
	cgi_t* cgi = peer->cgi;
	int n;

	HIO_ASSERT (cgi->htts->hio, htrd == cgi->peer_htrd);

	n = hio_svc_htts_task_addresbody((hio_svc_htts_task_t*)cgi, data, dlen);
	if (cgi->task_res_pending_writes > CGI_PENDING_IO_THRESHOLD)
	{
		if (hio_dev_pro_read(cgi->peer, HIO_DEV_PRO_OUT, 0) <= -1) n = -1;
	}

	return n;
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

static void cgi_client_on_disconnect (hio_dev_sck_t* sck)
{
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	hio_svc_htts_t* htts = cli->htts;
	cgi_t* cgi = (cgi_t*)cli->task;
	hio_t* hio = sck->hio;

	HIO_ASSERT (hio, sck == cgi->task_csck);
	HIO_DEBUG4 (hio, "HTTS(%p) - cgi(t=%p,c=%p,csck=%p) - client socket disconnect notified\n", htts, cgi, cli, sck);

	if (cgi)
	{
		HIO_SVC_HTTS_TASK_RCUP ((hio_svc_htts_task_t*)cgi); /* for temporary protection */

		/* detach the task from the client and the client socket */
		unbind_task_from_client (cgi, 1);

		/* call the parent handler*/
		/*if (fcgi->client_org_on_disconnect) fcgi->client_org_on_disconnect (sck);*/
		if (sck->on_disconnect) sck->on_disconnect (sck); /* restored to the orginal parent handler in unbind_task_from_client() */

		/* if the client side is closed, the data from the child process is not read and the write() of the child process call may block.
		 * just close the input side of the pipe to the child to prevense this situation */
		if (cgi->peer) hio_dev_pro_close(cgi->peer, HIO_DEV_PRO_IN);
		if (cgi->peer) hio_dev_pro_close(cgi->peer, HIO_DEV_PRO_OUT);

		HIO_SVC_HTTS_TASK_RCDOWN ((hio_svc_htts_task_t*)cgi);
	}

	HIO_DEBUG4 (hio, "HTTS(%p) - cgi(t=%p,c=%p,csck=%p) - client socket disconnect handled\n", htts, cgi, cli, sck);
	/* Note: after this callback, the actual device pointed to by 'sck' will be freed in the main loop. */
}

static int cgi_client_on_read (hio_dev_sck_t* sck, const void* buf, hio_iolen_t len, const hio_skad_t* srcaddr)
{
	hio_t* hio = sck->hio;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	cgi_t* cgi = (cgi_t*)cli->task;
	int n;

	HIO_ASSERT (hio, sck == cli->sck);

	n = cgi->client_org_on_read? cgi->client_org_on_read(sck, buf, len, srcaddr): 0;

	if (len <= -1)
	{
		/* read error */
		HIO_DEBUG3 (cli->htts->hio, "HTTS(%p) - read error on client %p(%d)\n", cgi->htts, sck, (int)sck->hnd);
		goto oops;
	}

	if (len == 0)
	{
		/* EOF on the client side. arrange to close */
		HIO_DEBUG3 (hio, "HTTS(%p) - EOF from client %p(hnd=%d)\n", cgi->htts, sck, (int)sck->hnd);

		if (!(cgi->over & CGI_OVER_READ_FROM_CLIENT)) /* if this is true, EOF is received without cgi_client_htrd_poke() */
		{
			int x;
			x = cgi_write_to_peer(cgi, HIO_NULL, 0);
			cgi_mark_over (cgi, CGI_OVER_READ_FROM_CLIENT);
			if (x <= -1) goto oops;
		}
	}

	if (n <= -1) goto oops;
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
	int n;

	n = cgi->client_org_on_write? cgi->client_org_on_write(sck, wrlen, wrctx, dstaddr): 0;

	if (wrlen == 0)
	{
		/* if the connect is keep-alive, this part may not be called */
		HIO_DEBUG3 (hio, "HTTS(%p) - indicated EOF to client %p(%d)\n", cgi->htts, sck, (int)sck->hnd);
		/* since EOF has been indicated to the client, it must not write to the client any further.
		 * this also means that i don't need any data from the peer side either.
		 * i don't need to enable input watching on the peer side */
		cgi_mark_over (cgi, CGI_OVER_WRITE_TO_CLIENT);
	}
	else if (wrlen > 0)
	{
		if (cgi->peer && cgi->task_res_pending_writes == CGI_PENDING_IO_THRESHOLD)
		{
			/* enable input watching */
			if (!(cgi->over & CGI_OVER_READ_FROM_PEER) &&
			    hio_dev_pro_read(cgi->peer, HIO_DEV_PRO_OUT, 1) <= -1) n = -1;
		}

		if ((cgi->over & CGI_OVER_READ_FROM_PEER) && cgi->task_res_pending_writes <= 0)
		{
			cgi_mark_over (cgi, CGI_OVER_WRITE_TO_CLIENT);
		}
	}

	if (n <= -1 || wrlen <= -1) cgi_halt_participating_devices (cgi);
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

/* ----------------------------------------------------------------------- */

static void bind_task_to_client (cgi_t* cgi, hio_dev_sck_t* csck)
{
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(csck);

	HIO_ASSERT (cgi->htts->hio, cli->sck == csck);
	HIO_ASSERT (cgi->htts->hio, cli->task == HIO_NULL);

	/* cgi->task_client and cgi->task_csck are set in hio_svc_htts_task_make() */

	/* remember the client socket's io event handlers */
	cgi->client_org_on_read = csck->on_read;
	cgi->client_org_on_write = csck->on_write;
	cgi->client_org_on_disconnect = csck->on_disconnect;

	/* set new io events handlers on the client socket */
	csck->on_read = cgi_client_on_read;
	csck->on_write = cgi_client_on_write;
	csck->on_disconnect = cgi_client_on_disconnect;

	cli->task = (hio_svc_htts_task_t*)cgi;
	HIO_SVC_HTTS_TASK_RCUP (cgi);
}

static void unbind_task_from_client (cgi_t* cgi, int rcdown)
{
	hio_dev_sck_t* csck = cgi->task_csck;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(csck);

	if (cli->task) /* only if it's bound */
	{
		HIO_ASSERT (cgi->htts->hio, cgi->task_client != HIO_NULL);
		HIO_ASSERT (cgi->htts->hio, cgi->task_csck != HIO_NULL); /* cgi->task_csck is set by hio_svc_htts_task_make() */
		HIO_ASSERT (cgi->htts->hio, cgi->task_client->task == (hio_svc_htts_task_t*)cgi);
		HIO_ASSERT (cgi->htts->hio, cgi->task_client->htrd != HIO_NULL);

		if (cgi->client_htrd_recbs_changed)
		{
			hio_htrd_setrecbs (cgi->task_client->htrd, &cgi->client_htrd_org_recbs);
			cgi->client_htrd_recbs_changed = 0;
		}

		if (cgi->client_org_on_read)
		{
			csck->on_read = cgi->client_org_on_read;
			cgi->client_org_on_read = HIO_NULL;
		}

		if (cgi->client_org_on_write)
		{
			csck->on_write = cgi->client_org_on_write;
			cgi->client_org_on_write = HIO_NULL;
		}

		if (cgi->client_org_on_disconnect)
		{
			csck->on_disconnect = cgi->client_org_on_disconnect;
			cgi->client_org_on_disconnect = HIO_NULL;
		}

		/* there is some ordering issue in using HIO_SVC_HTTS_TASK_UNREF()
		 * because it can destroy the cgi itself. so reset cgi->task_client->task
		 * to null and call RCDOWN() later */
		cgi->task_client->task = HIO_NULL;

		/* these two lines are also done in csck_on_disconnect() in http-svr.c because the socket is destroyed.
		 * the same lines here are because the task is unbound while the socket is still alive */
		cgi->task_client = HIO_NULL;
		cgi->task_csck = HIO_NULL;

		/* enable input watching on the socket being unbound */
		if (cgi->task_keep_client_alive && hio_dev_sck_read(csck, 1) <= -1)
		{
			HIO_DEBUG2 (cgi->htts->hio, "HTTS(%p) - halting client(%p) for failure to enable input watching\n", cgi->htts, csck);
			hio_dev_sck_halt (csck);
		}

		if (rcdown) HIO_SVC_HTTS_TASK_RCDOWN ((hio_svc_htts_task_t*)cgi);

	}
}

/* ----------------------------------------------------------------------- */

static int bind_task_to_peer (cgi_t* cgi, hio_dev_sck_t* csck, hio_htre_t* req, const hio_bch_t* docroot, const hio_bch_t* script)
{
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(csck);
	hio_svc_htts_t* htts = cgi->htts;
	hio_t* hio = htts->hio;
	peer_fork_ctx_t fc;
	hio_dev_pro_make_t mi;
	cgi_peer_xtn_t* peer_xtn;

	HIO_MEMSET (&fc, 0, HIO_SIZEOF(fc));
	fc.cli = cli;
	fc.req = req;
	fc.docroot = docroot;
	fc.script = script;
	fc.actual_script = hio_svc_htts_dupmergepaths(htts, docroot, script);
	if (!fc.actual_script) return -1;

	HIO_MEMSET (&mi, 0, HIO_SIZEOF(mi));
	mi.flags = HIO_DEV_PRO_READOUT | HIO_DEV_PRO_ERRTONUL | HIO_DEV_PRO_WRITEIN /*| HIO_DEV_PRO_FORGET_CHILD*/;
	mi.cmd = fc.actual_script;
	mi.on_read = cgi_peer_on_read;
	mi.on_write = cgi_peer_on_write;
	mi.on_close = cgi_peer_on_close;
	mi.on_fork = cgi_peer_on_fork;
	mi.fork_ctx = &fc;

	if (access(mi.cmd, X_OK) == -1)
	{
		/* not executable */
		hio_seterrwithsyserr (hio, 0, errno);
		hio_freemem (hio, fc.actual_script);
		return -2;
	}

	cgi->peer = hio_dev_pro_make(hio, HIO_SIZEOF(*peer_xtn), &mi);
	if (HIO_UNLIKELY(!cgi->peer))
	{
		hio_freemem (hio, fc.actual_script);
		return -1;
	}

	hio_freemem (hio, fc.actual_script);

	cgi->peer_htrd = hio_htrd_open(hio, HIO_SIZEOF(*peer_xtn));
	if (HIO_UNLIKELY(!cgi->peer_htrd))
	{
		hio_freemem (hio, fc.actual_script);
		hio_dev_pro_kill (cgi->peer);
		cgi->peer = HIO_NULL;
		return -1;
	}

	hio_htrd_setoption (cgi->peer_htrd, HIO_HTRD_SKIP_INITIAL_LINE | HIO_HTRD_RESPONSE);
	hio_htrd_setrecbs (cgi->peer_htrd, &peer_htrd_recbs);

	peer_xtn = hio_dev_pro_getxtn(cgi->peer);
	peer_xtn->cgi = cgi;
	HIO_SVC_HTTS_TASK_RCUP (cgi);

	peer_xtn = hio_htrd_getxtn(cgi->peer_htrd);
	peer_xtn->cgi = cgi;
	HIO_SVC_HTTS_TASK_RCUP (cgi);

	return 0;
}

static void unbind_task_from_peer (cgi_t* cgi, int rcdown)
{
	int n = 0;

	if (cgi->peer_htrd)
	{
		hio_htrd_close (cgi->peer_htrd);
		cgi->peer_htrd = HIO_NULL;
		n++;
	}

	if (cgi->peer)
	{
		cgi_peer_xtn_t* peer_xtn;
		peer_xtn = hio_dev_pro_getxtn(cgi->peer);
		peer_xtn->cgi = HIO_NULL;

		hio_dev_pro_kill (cgi->peer);
		cgi->peer = HIO_NULL;
		n++;
	}

	if (rcdown)
	{
		while (n > 0)
		{
			n--;
			HIO_SVC_HTTS_TASK_RCDOWN((hio_svc_htts_task_t*)cgi);
		}
	}
}

/* ----------------------------------------------------------------------- */

static int setup_for_content_length(cgi_t* cgi, hio_htre_t* req)
{
	int have_content;

#if defined(CGI_ALLOW_UNLIMITED_REQ_CONTENT_LENGTH)
	have_content = cgi->task_req_conlen > 0 || cgi->task_req_conlen_unlimited;
#else
	have_content = cgi->task_req_conlen > 0;
#endif

	if (have_content)
	{
		/* change the callbacks to subscribe to contents to be uploaded */
		cgi->client_htrd_org_recbs = *hio_htrd_getrecbs(cgi->task_client->htrd);
		cgi_client_htrd_recbs.peek = cgi->client_htrd_org_recbs.peek;
		hio_htrd_setrecbs (cgi->task_client->htrd, &cgi_client_htrd_recbs);
		cgi->client_htrd_recbs_changed = 1;
	}
	else
	{
		/* no content to be uploaded from the client */
		/* indicate EOF to the peer and disable input wathching from the client */
		if (cgi_write_to_peer(cgi, HIO_NULL, 0) <= -1) return -1;
		cgi_mark_over (cgi, CGI_OVER_READ_FROM_CLIENT | CGI_OVER_WRITE_TO_PEER);
	}

	return 0;
}

/* ----------------------------------------------------------------------- */

int hio_svc_htts_docgi (hio_svc_htts_t* htts, hio_dev_sck_t* csck, hio_htre_t* req, const hio_bch_t* docroot, const hio_bch_t* script, int options, hio_svc_htts_task_on_kill_t on_kill)
{
	hio_t* hio = htts->hio;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(csck);
	cgi_t* cgi = HIO_NULL;
	int n, status_code = HIO_HTTP_STATUS_INTERNAL_SERVER_ERROR;
	int bound_to_client = 0, bound_to_peer = 0, ntask_cgi_inced = 0;

	/* ensure that you call this function before any contents is received */
	HIO_ASSERT (hio, hio_htre_getcontentlen(req) == 0);
	HIO_ASSERT (hio, cli->sck == csck);

	if (cli->task)
	{
		hio_seterrbfmt (hio, HIO_EPERM, "duplicate task request prohibited");
		goto oops;
	}

	cgi = (cgi_t*)hio_svc_htts_task_make(htts, HIO_SIZEOF(*cgi), cgi_on_kill, req, csck);
	if (HIO_UNLIKELY(!cgi)) goto oops;
	HIO_SVC_HTTS_TASK_RCUP((hio_svc_htts_task_t*)cgi);
	if (inc_ntask_cgis(htts) <= -1)
	{
		status_code = HIO_HTTP_STATUS_SERVICE_UNAVAILABLE;
		goto oops;
	}
	cgi->ntask_cgis_inced = 1;
	cgi->options = options;

	bind_task_to_client (cgi, csck);
	bound_to_client = 1;

	if ((n = bind_task_to_peer(cgi, csck, req, docroot, script)) <= -1)
	{
		if (n == -2) status_code = HIO_HTTP_STATUS_FORBIDDEN;
		goto oops;
	}
	bound_to_peer = 1;

	if (hio_svc_htts_task_handleexpect100((hio_svc_htts_task_t*)cgi, 0) <= -1) goto oops;
	if (setup_for_content_length(cgi, req) <= -1) goto oops;

	/* TODO: store current input watching state and use it when destroying the cgi data */
	if (hio_dev_sck_read(csck, !(cgi->over & CGI_OVER_READ_FROM_CLIENT)) <= -1) goto oops;

	HIO_SVC_HTTS_TASKL_APPEND_TASK (&htts->task, (hio_svc_htts_task_t*)cgi);
	HIO_SVC_HTTS_TASK_RCDOWN((hio_svc_htts_task_t*)cgi);

	/* set the on_kill callback only if this function can return success.
	 * the on_kill callback won't be executed if this function returns failure.
	 * however, the internal callback cgi_on_kill is still called */
	cgi->on_kill = on_kill;
	return 0;

oops:
	HIO_DEBUG3 (hio, "HTTS(%p) - FAILURE in docgi - socket(%p) - %js\n", htts, csck, hio_geterrmsg(hio));
	if (cgi)
	{
		hio_svc_htts_task_sendfinalres((hio_svc_htts_task_t*)cgi, status_code, HIO_NULL, HIO_NULL, 1);
		if (bound_to_peer) unbind_task_from_peer (cgi, 1);
		if (bound_to_client) unbind_task_from_client (cgi, 1);
		cgi_halt_participating_devices (cgi);
		HIO_SVC_HTTS_TASK_RCDOWN((hio_svc_htts_task_t*)cgi);
	}
	return -1;
}
