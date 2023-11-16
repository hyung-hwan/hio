#include "http-prv.h"
#include <hio-fmt.h>
#include <hio-chr.h>
#include <hio-fcgi.h>
#include <unistd.h>

#define FCGI_ALLOW_UNLIMITED_REQ_CONTENT_LENGTH

#define FCGI_PENDING_IO_THRESHOLD_TO_CLIENT 50
#define FCGI_PENDING_IO_THRESHOLD_TO_PEER 50

#define FCGI_OVER_READ_FROM_CLIENT (1 << 0)
#define FCGI_OVER_READ_FROM_PEER   (1 << 1)
#define FCGI_OVER_WRITE_TO_CLIENT  (1 << 2)
#define FCGI_OVER_WRITE_TO_PEER    (1 << 3)
#define FCGI_OVER_ALL (FCGI_OVER_READ_FROM_CLIENT | FCGI_OVER_READ_FROM_PEER | FCGI_OVER_WRITE_TO_CLIENT | FCGI_OVER_WRITE_TO_PEER)

struct fcgi_t
{
	HIO_SVC_HTTS_TASK_HEADER;

	hio_svc_htts_task_on_kill_t on_kill; /* user-provided on_kill callback */

	hio_oow_t peer_pending_writes;
	hio_svc_fcgic_sess_t* peer;
	hio_htrd_t* peer_htrd;

	unsigned int over: 4; /* must be large enough to accomodate FCGI_OVER_ALL */
	unsigned int client_htrd_recbs_changed: 1;

	hio_dev_sck_on_read_t client_org_on_read;
	hio_dev_sck_on_write_t client_org_on_write;
	hio_dev_sck_on_disconnect_t client_org_on_disconnect;
	hio_htrd_recbs_t client_htrd_org_recbs;
};
typedef struct fcgi_t fcgi_t;

struct fcgi_peer_xtn_t
{
	fcgi_t* fcgi; /* back pointer to the fcgi object */
};
typedef struct fcgi_peer_xtn_t fcgi_peer_xtn_t;

static void unbind_task_from_client (fcgi_t* fcgi, int rcdown);
static void unbind_task_from_peer (fcgi_t* fcgi, int rcdown);

static void fcgi_halt_participating_devices (fcgi_t* fcgi)
{
/* TODO: include fcgi session id in the output in place of peer??? */
	HIO_DEBUG5 (fcgi->htts->hio, "HTTS(%p) - fcgi(t=%p,c=%p(%d),p=%p) Halting participating devices\n", fcgi->htts, fcgi, fcgi->task_csck, (fcgi->task_csck? fcgi->task_csck->hnd: -1), fcgi->peer);

	if (fcgi->task_csck) hio_dev_sck_halt (fcgi->task_csck);
	unbind_task_from_peer (fcgi, 1);
}

static int fcgi_write_stdin_to_peer (fcgi_t* fcgi, const void* data, hio_iolen_t dlen)
{
	if (fcgi->peer)
	{
		fcgi->peer_pending_writes++;
		if (hio_svc_fcgic_writestdin(fcgi->peer, data, dlen) <= -1) /* TODO: write STDIN, PARAM? */
		{
			fcgi->peer_pending_writes--;
			return -1;
		}
#if 0
	/* TODO: check if it's already finished or something.. */
		if (fcgi->peer_pending_writes > FCGI_PENDING_IO_THRESHOLD_TO_PEER)
		{
			/* disable input watching */
			if (hio_dev_sck_read(fcgi->task_csck, 0) <= -1) return -1;
		}
#endif
	}
	return 0;
}

static HIO_INLINE void fcgi_mark_over (fcgi_t* fcgi, int over_bits)
{
	hio_svc_htts_t* htts = fcgi->htts;
	hio_t* hio = htts->hio;
	unsigned int old_over;

	old_over = fcgi->over;
	fcgi->over |= over_bits;

	HIO_DEBUG8 (hio, "HTTS(%p) - fcgi(t=%p,c=%p[%d],p=%p) - old_over=%x | new-bits=%x => over=%x\n", fcgi->htts, fcgi, fcgi->task_client, (fcgi->task_csck? fcgi->task_csck->hnd: -1), fcgi->peer, (int)old_over, (int)over_bits, (int)fcgi->over);

	if (!(old_over & FCGI_OVER_READ_FROM_CLIENT) && (fcgi->over & FCGI_OVER_READ_FROM_CLIENT))
	{
		/* finished reading from the client. stop watching read */
		if (fcgi->task_csck && hio_dev_sck_read(fcgi->task_csck, 0) <= -1)
		{
			HIO_DEBUG2 (fcgi->htts->hio, "HTTS(%p) - halting client(%p) for failure to disable input watching\n", fcgi->htts, fcgi->task_csck);
			hio_dev_sck_halt (fcgi->task_csck);
		}
	}

	if (!(old_over & FCGI_OVER_READ_FROM_PEER) && (fcgi->over & FCGI_OVER_READ_FROM_PEER))
	{
		if (fcgi->peer)
		{
			hio_svc_fcgic_untie (fcgi->peer); /* the untie callback will reset fcgi->peer to HIO_NULL */
			HIO_SVC_HTTS_TASK_RCDOWN((hio_svc_htts_task_t*)fcgi); /* ref down from fcgi->peer->ctx. unable to use UNREF() */
		}
	}

	if (old_over != FCGI_OVER_ALL && fcgi->over == FCGI_OVER_ALL)
	{
		/* ready to stop */
		HIO_SVC_HTTS_TASK_RCUP ((hio_svc_htts_task_t*)fcgi);

		if (fcgi->peer)
		{
			hio_svc_fcgic_untie (fcgi->peer);
			HIO_SVC_HTTS_TASK_RCDOWN((hio_svc_htts_task_t*)fcgi); /* ref down from fcgi->peer->ctx. unable to use UNREF() */
		}

		if (fcgi->task_csck)
		{
			if (fcgi->task_keep_client_alive)
			{
				HIO_DEBUG2 (hio, "HTTS(%p) - keeping client(%p) alive\n", fcgi->htts, fcgi->task_csck);
				HIO_ASSERT (fcgi->htts->hio, fcgi->task_client->task == (hio_svc_htts_task_t*)fcgi);
				unbind_task_from_client (fcgi, 1);
			}
			else
			{
				HIO_DEBUG2 (hio, "HTTS(%p) - halting client(%p)\n", fcgi->htts, fcgi->task_csck);
				hio_dev_sck_shutdown (fcgi->task_csck, HIO_DEV_SCK_SHUTDOWN_WRITE);
				hio_dev_sck_halt (fcgi->task_csck);
			}
		}

		HIO_SVC_HTTS_TASK_RCDOWN ((hio_svc_htts_task_t*)fcgi); /* it may destroy fcgi here */
	}
}

static void fcgi_on_kill (hio_svc_htts_task_t* task)
{
	fcgi_t* fcgi = (fcgi_t*)task;
	hio_t* hio = fcgi->htts->hio;

	HIO_DEBUG5 (hio, "HTTS(%p) - fcgi(t=%p,c=%p[%d],p=%p) - killing the task\n", fcgi->htts, fcgi, fcgi->task_client, (fcgi->task_csck? fcgi->task_csck->hnd: -1), fcgi->peer);

	if (fcgi->on_kill) fcgi->on_kill (task);

	/* [NOTE] 
	 * 1. if hio_svc_htts_task_kill() is called, fcgi->peer, fcgi->peer_htrd, fcgi->task_csck, 
	 *    fcgi->task_client may not not null. 
	 * 2. this callback function doesn't decrement the reference count on fcgi because
	 *    it is the task destruction callback. (passing 0 to unbind_task_from_peer/client)
	 */
	unbind_task_from_peer (fcgi, 0);

	if (fcgi->task_csck)
	{
		HIO_ASSERT (hio, fcgi->task_client != HIO_NULL);
		unbind_task_from_client (fcgi, 0);
	}

	/* detach from the htts service only if it's attached */
	if (fcgi->task_next) HIO_SVC_HTTS_TASKL_UNLINK_TASK (fcgi); 

	HIO_DEBUG5 (hio, "HTTS(%p) - fcgi(t=%p,c=%p[%d],p=%p) - killed the task\n", fcgi->htts, fcgi, fcgi->task_client, (fcgi->task_csck? fcgi->task_csck->hnd: -1), fcgi->peer);
}

static void fcgi_peer_on_untie (hio_svc_fcgic_sess_t* peer, void* ctx)
{
	fcgi_t* fcgi = (fcgi_t*)ctx;
	hio_t* hio = fcgi->htts->hio;

	/* in case this untie event originates from the fcgi client itself.
	 * fcgi_halt_participating_devices() calls hio_svc_fcgi_untie() again 
	 * to cause an infinite loop if we don't reset fcgi->peer to HIO_NULL here */

	HIO_DEBUG5 (hio, "HTTS(%p) - fcgi(t=%p,c=%p[%d],p=%p) - untieing peer\n", fcgi->htts, fcgi, fcgi->task_client, (fcgi->task_csck? fcgi->task_csck->hnd: -1), fcgi->peer);

	fcgi->peer = HIO_NULL;  /* to avoid infinite loop as explained above */
	hio_svc_htts_task_endbody (fcgi);
	unbind_task_from_peer (fcgi, 1);

	HIO_DEBUG5 (hio, "HTTS(%p) - fcgi(t=%p,c=%p[%d],p=%p) - untied peer\n", fcgi->htts, fcgi, fcgi->task_client, (fcgi->task_csck? fcgi->task_csck->hnd: -1), fcgi->peer);
}

static int fcgi_peer_on_read (hio_svc_fcgic_sess_t* peer, const void* data, hio_iolen_t dlen, void* ctx)
{
	fcgi_t* fcgi = (fcgi_t*)ctx;
	hio_svc_htts_t* htts = fcgi->htts;
	hio_t* hio = htts->hio;
	
	if (dlen <= -1)
	{
		HIO_DEBUG2 (hio, "HTTS(%p) - read error from peer %p\n", htts, peer);
		goto oops;
	}

	if (dlen == 0)
	{
		HIO_DEBUG2 (hio, "HTTS(%p) - EOF from peer %p\n", htts, peer);

		if (!(fcgi->over & FCGI_OVER_READ_FROM_PEER))
		{
			int n;
			/* the fcgi script could be misbehaving.
			 * it still has to read more but EOF is read.
			 * otherwise peer_htrd_poke() should have been called */
			n = hio_svc_htts_task_endbody(fcgi);
			fcgi_mark_over (fcgi, FCGI_OVER_READ_FROM_PEER);
			if (n <= -1) goto oops;
		}
	}
	else
	{
		hio_oow_t rem;

		HIO_ASSERT (hio, !(fcgi->over & FCGI_OVER_READ_FROM_PEER));

		if (hio_htrd_feed(fcgi->peer_htrd, data, dlen, &rem) <= -1)
		{
			HIO_DEBUG2 (hio, "HTTS(%p) - unable to feed peer htrd - peer %p\n", htts, peer);

			if (!fcgi->task_res_started && !(fcgi->over & FCGI_OVER_WRITE_TO_CLIENT))
			{
				hio_svc_htts_task_sendfinalres (fcgi, HIO_HTTP_STATUS_BAD_GATEWAY, HIO_NULL, HIO_NULL, 1); /* don't care about error because it jumps to oops below anyway */
			}

			goto oops;
		}

		if (rem > 0)
		{
			/* If the script specifies Content-Length and produces longer data, it will come here */
			/* ezcessive data ... */
			/* TODO: or drop this request?? */
		}
	}

	return 0;

oops:
	fcgi_halt_participating_devices (fcgi); /* TODO: kill the session only??? */
	return 0;
}

static int fcgi_peer_on_write (hio_svc_fcgic_sess_t* peer, hio_fcgi_req_type_t rqtype, hio_iolen_t wrlen, void* wrctx)
{
	fcgi_t* fcgi = (fcgi_t*)wrctx;

	if (wrlen <= -1) goto oops;

	if (rqtype == HIO_FCGI_STDIN && wrlen == 0)
	{
		/* completely wrote end of stdin to the cgi server */
		fcgi_mark_over (fcgi, FCGI_OVER_WRITE_TO_PEER);
	}
	return 0;

oops:
	fcgi_halt_participating_devices (fcgi);
	return 0;
}

static int peer_capture_response_header (hio_htre_t* req, const hio_bch_t* key, const hio_htre_hdrval_t* val, void* ctx)
{
	return hio_svc_htts_task_addreshdrs((fcgi_t*)ctx, key, val);
}

static int peer_htrd_peek (hio_htrd_t* htrd, hio_htre_t* req)
{
	/* response header received from the peer */
	fcgi_peer_xtn_t* peer = hio_htrd_getxtn(htrd);
	fcgi_t* fcgi = peer->fcgi;
	hio_svc_htts_cli_t* cli = fcgi->task_client;

	if (HIO_LIKELY(cli)) /* only if the client is still connected */
	{
		int status_code = HIO_HTTP_STATUS_OK;
		const hio_bch_t* status_desc = HIO_NULL;
		int chunked;

		if (req->attr.status) hio_parse_http_status_header_value(req->attr.status, &status_code, &status_desc);

		chunked = fcgi->task_keep_client_alive && !req->attr.content_length;

		if (hio_svc_htts_task_startreshdr(fcgi, status_code, status_desc, chunked) <= -1 ||
			hio_htre_walkheaders(req, peer_capture_response_header, fcgi) <= -1 ||
			hio_svc_htts_task_endreshdr(fcgi) <= -1) return -1;
	}

	return 0;
}

static int peer_htrd_poke (hio_htrd_t* htrd, hio_htre_t* req)
{
	/* complete response received from the peer */
	fcgi_peer_xtn_t* peer = hio_htrd_getxtn(htrd);
	fcgi_t* fcgi = peer->fcgi;
	int n;

	n = hio_svc_htts_task_endbody(fcgi);
	fcgi_mark_over (fcgi, FCGI_OVER_READ_FROM_PEER);
	return n;
}

static int peer_htrd_push_content (hio_htrd_t* htrd, hio_htre_t* req, const hio_bch_t* data, hio_oow_t dlen)
{
	fcgi_peer_xtn_t* peer = hio_htrd_getxtn(htrd);
	fcgi_t* fcgi = peer->fcgi;
	HIO_ASSERT (fcgi->htts->hio, htrd == fcgi->peer_htrd);
	return hio_svc_htts_task_addresbody(fcgi, data, dlen);
}

static hio_htrd_recbs_t peer_htrd_recbs =
{
	peer_htrd_peek,
	peer_htrd_poke,
	peer_htrd_push_content
};

static int fcgi_client_htrd_poke (hio_htrd_t* htrd, hio_htre_t* req)
{
	/* the client request got completed including body.
	 * this callback is set and called only if there is content in the request */
	hio_svc_htts_cli_htrd_xtn_t* htrdxtn = (hio_svc_htts_cli_htrd_xtn_t*)hio_htrd_getxtn(htrd);
	hio_dev_sck_t* sck = htrdxtn->sck;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	fcgi_t* fcgi = (fcgi_t*)cli->task;

	/* indicate end of STDIN */
	if (fcgi_write_stdin_to_peer(fcgi, HIO_NULL, 0) <= -1) return -1;

	fcgi_mark_over (fcgi, FCGI_OVER_READ_FROM_CLIENT);
	return 0;
}

static int fcgi_client_htrd_push_content (hio_htrd_t* htrd, hio_htre_t* req, const hio_bch_t* data, hio_oow_t dlen)
{
	hio_svc_htts_cli_htrd_xtn_t* htrdxtn = (hio_svc_htts_cli_htrd_xtn_t*)hio_htrd_getxtn(htrd);
	hio_dev_sck_t* sck = htrdxtn->sck;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	fcgi_t* fcgi = (fcgi_t*)cli->task;

	HIO_ASSERT (sck->hio, cli->sck == sck);

	/* write the contents to fcgi server as stdin*/
	return fcgi_write_stdin_to_peer(fcgi, data, dlen);
}

static hio_htrd_recbs_t fcgi_client_htrd_recbs =
{
	HIO_NULL, /* this shall be set to an actual peer handler before hio_htrd_setrecbs() */
	fcgi_client_htrd_poke,
	fcgi_client_htrd_push_content
};

static void fcgi_client_on_disconnect (hio_dev_sck_t* sck)
{
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	hio_t* hio = sck->hio;
	hio_svc_htts_t* htts = cli->htts;
	fcgi_t* fcgi = (fcgi_t*)cli->task;

	HIO_DEBUG4 (hio, "HTTS(%p) - fcgi(t=%p,c=%p,csck=%p) - handling client socket disconnect\n", htts, fcgi, cli, sck);

	/* fcgi may be null if there is no associated task or 
	 * the previously associated one is already gone */
	if (fcgi)
	{
		HIO_ASSERT (hio, sck == fcgi->task_csck);

		HIO_SVC_HTTS_TASK_RCUP (fcgi);

		/* detach the task from the client and the client socket */
		unbind_task_from_client (fcgi, 1);

		/* call the parent handler*/
		/*if (fcgi->client_org_on_disconnect) fcgi->client_org_on_disconnect (sck);*/
		if (sck->on_disconnect) sck->on_disconnect (sck); /* restored to the orginal parent handelr in unbind_task_from_client() */

		HIO_SVC_HTTS_TASK_RCDOWN (fcgi);
	}

	HIO_DEBUG4 (hio, "HTTS(%p) - fcgi(t=%p,c=%p,csck=%p) - handled client socket disconnect\n", htts, fcgi, cli, sck);
	/* Note: after this callback, the actual device pointed to by 'sck' will be freed in the main loop. */
}

static int fcgi_client_on_read (hio_dev_sck_t* sck, const void* buf, hio_iolen_t len, const hio_skad_t* srcaddr)
{
	hio_t* hio = sck->hio;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	fcgi_t* fcgi = (fcgi_t*)cli->task;
	int n;

	HIO_ASSERT (hio, sck == cli->sck);

	n = fcgi->client_org_on_read? fcgi->client_org_on_read(sck, buf, len, srcaddr): 0;

	if (len <= -1)
	{
		/* read error */
		HIO_DEBUG3 (cli->htts->hio, "HTTS(%p) - read error on client %p(%d)\n", fcgi->htts, sck, (int)sck->hnd);
		goto oops;
	}

	if (len == 0)
	{
		/* EOF on the client side. arrange to close */
		HIO_DEBUG3 (hio, "HTTS(%p) - EOF from client %p(hnd=%d)\n", fcgi->htts, sck, (int)sck->hnd);

		if (!(fcgi->over & FCGI_OVER_READ_FROM_CLIENT)) /* if this is true, EOF is received without fcgi_client_htrd_poke() */
		{
			/* indicate eof to the write side */
			int x;
			x = fcgi_write_stdin_to_peer(fcgi, HIO_NULL, 0);
			fcgi_mark_over (fcgi, FCGI_OVER_READ_FROM_CLIENT);
			if (x <= -1) goto oops;
		}
	}

	if (n <= -1) goto oops;
	return 0;

oops:
	fcgi_halt_participating_devices (fcgi);
	return 0;
}

static int fcgi_client_on_write (hio_dev_sck_t* sck, hio_iolen_t wrlen, void* wrctx, const hio_skad_t* dstaddr)
{
	hio_t* hio = sck->hio;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	fcgi_t* fcgi = (fcgi_t*)cli->task;
	int n;

	n = fcgi->client_org_on_write? fcgi->client_org_on_write(sck, wrlen, wrctx, dstaddr): 0;

	if (wrlen == 0)
	{
		fcgi_mark_over (fcgi, FCGI_OVER_WRITE_TO_CLIENT);
	}
	else if (wrlen > 0)
	{
	#if 0
		if (fcgi->peer && fcgi->task_res_pending_writes == FCGI_PENDING_IO_THRESHOLD_TO_CLIENT)
		{
			/* enable reading from fcgi */
			if (!(fcgi->over & FCGI_OVER_READ_FROM_PEER) && hio_svc_fcgic_read(fcgi->peer, 1) <= -1) goto oops;
		}
	#endif

		if ((fcgi->over & FCGI_OVER_READ_FROM_PEER) && fcgi->task_res_pending_writes <= 0)
		{
			fcgi_mark_over (fcgi, FCGI_OVER_WRITE_TO_CLIENT);
		}
	}

	if (n <= -1 || wrlen <= -1) fcgi_halt_participating_devices (fcgi);
	return 0;
}

static int peer_capture_request_header (hio_htre_t* req, const hio_bch_t* key, const hio_htre_hdrval_t* val, void* ctx)
{
	fcgi_t* fcgi = (fcgi_t*)ctx;
	hio_svc_htts_t* htts = fcgi->htts;
	hio_t* hio = htts->hio;

	if (hio_comp_bcstr(key, "Connection", 1) != 0 &&
	    hio_comp_bcstr(key, "Transfer-Encoding", 1) != 0 &&
	    hio_comp_bcstr(key, "Content-Length", 1) != 0 &&
	    hio_comp_bcstr(key, "Expect", 1) != 0)
	{
		hio_oow_t val_offset;
		hio_bch_t* ptr;

		if (hio_comp_bcstr(key, "Content-Type", 1) == 0)
		{
			/* don't prefix CONTENT_TYPE with HTTP_ */
			hio_becs_clear (htts->becbuf);
		}
		else
		{
			if (hio_becs_cpy(htts->becbuf, "HTTP_") == (hio_oow_t)-1) return -1;
		}

		if (hio_becs_cat(htts->becbuf, key) == (hio_oow_t)-1 ||
		    hio_becs_ccat(htts->becbuf, '\0') == (hio_oow_t)-1) return -1;

		for (ptr = HIO_BECS_PTR(htts->becbuf); *ptr; ptr++)
		{
			*ptr = hio_to_bch_upper(*ptr);
			if (*ptr =='-') *ptr = '_';
		}

		val_offset = HIO_BECS_LEN(htts->becbuf);
		if (hio_becs_cat(htts->becbuf, val->ptr) == (hio_oow_t)-1) return -1;
		val = val->next;
		while (val)
		{
			if (hio_becs_cat(htts->becbuf, ",") == (hio_oow_t)-1 ||
			    hio_becs_cat(htts->becbuf, val->ptr) == (hio_oow_t)-1) return -1;
			val = val->next;
		}

		hio_svc_fcgic_writeparam(fcgi->peer, HIO_BECS_PTR(htts->becbuf), val_offset - 1, HIO_BECS_CPTR(htts->becbuf, val_offset), HIO_BECS_LEN(htts->becbuf) - val_offset);
		/* TODO: error handling? */
	}

	return 0;
}

static int write_params (fcgi_t* fcgi, hio_dev_sck_t* csck, hio_htre_t* req, const hio_bch_t* docroot, const hio_bch_t* script)
{
	hio_t* hio = fcgi->htts->hio;
	hio_bch_t tmp[256];
	hio_oow_t len;
	const hio_bch_t* qparam;
	hio_oow_t content_length;
	hio_bch_t* actual_script = HIO_NULL;
	hio_becs_t dbuf;

	HIO_ASSERT (hio, fcgi->task_csck == csck);

	actual_script = hio_svc_htts_dupmergepaths(fcgi->htts, docroot, script);
	if (!actual_script) goto oops;

	if (hio_svc_fcgic_writeparam(fcgi->peer, "GATEWAY_INTERFACE", 17, "FCGI/1.1", 7) <= -1) goto oops;

	len = hio_fmttobcstr(hio, tmp, HIO_COUNTOF(tmp), "HTTP/%d.%d", (int)hio_htre_getmajorversion(req), (int)hio_htre_getminorversion(req));
	if (hio_svc_fcgic_writeparam(fcgi->peer, "SERVER_PROTOCOL", 15, tmp, len) <= -1) goto oops;

	if (hio_svc_fcgic_writeparam(fcgi->peer, "DOCUMENT_ROOT", 13, docroot, hio_count_bcstr(docroot)) <= -1) goto oops;
	if (hio_svc_fcgic_writeparam(fcgi->peer, "SCRIPT_NAME", 11, script, hio_count_bcstr(script)) <= -1) goto oops;
	if (hio_svc_fcgic_writeparam(fcgi->peer, "SCRIPT_FILENAME", 15, actual_script, hio_count_bcstr(actual_script)) <= -1) goto oops;
// TODO: PATH_INFO

	if (hio_svc_fcgic_writeparam(fcgi->peer, "REQUEST_METHOD", 14, hio_htre_getqmethodname(req), hio_htre_getqmethodlen(req)) <= -1) goto oops;
	if (hio_svc_fcgic_writeparam(fcgi->peer, "REQUEST_URI", 11, hio_htre_getqpath(req), hio_htre_getqpathlen(req)) <= -1) goto oops;

    qparam = hio_htre_getqparam(req);
	if (!qparam) qparam = "";
	if (hio_svc_fcgic_writeparam(fcgi->peer, "QUERY_STRING", 12, qparam, hio_count_bcstr(qparam)) <= -1) goto oops;

	if (hio_htre_getreqcontentlen(req, &content_length) == 0)
	{
		/* content length is known and fixed */
		len = hio_fmt_uintmax_to_bcstr(tmp, HIO_COUNTOF(tmp), content_length, 10, 0, '\0', HIO_NULL);
		if (hio_svc_fcgic_writeparam(fcgi->peer, "CONTENT_LENGTH", 14, tmp, len) <= -1) goto oops;
	}

	if (hio_svc_fcgic_writeparam(fcgi->peer, "SERVER_SOFTWARE", 15, fcgi->htts->server_name, hio_count_bcstr(fcgi->htts->server_name)) <= -1) goto oops;

	len = hio_skadtobcstr (hio, &csck->localaddr, tmp, HIO_COUNTOF(tmp), HIO_SKAD_TO_BCSTR_ADDR);
	if (hio_svc_fcgic_writeparam(fcgi->peer, "SERVER_ADDR", 11, tmp, len) <= -1) goto oops;

	gethostname (tmp, HIO_COUNTOF(tmp)); /* if this fails, i assume tmp contains the ip address set by hio_skadtobcstr() above */
	if (hio_svc_fcgic_writeparam(fcgi->peer, "SERVER_NAME", 11, tmp, hio_count_bcstr(tmp)) <= -1) goto oops;

	len = hio_skadtobcstr (hio, &csck->localaddr, tmp, HIO_COUNTOF(tmp), HIO_SKAD_TO_BCSTR_PORT);
	if (hio_svc_fcgic_writeparam(fcgi->peer, "SERVER_PORT", 11, tmp, len) <= -1) goto oops;

	len = hio_skadtobcstr (hio, &csck->remoteaddr, tmp, HIO_COUNTOF(tmp), HIO_SKAD_TO_BCSTR_ADDR);
	if (hio_svc_fcgic_writeparam(fcgi->peer, "REMOTE_ADDR", 11, tmp, len) <= -1) goto oops;

	len = hio_skadtobcstr (hio, &csck->remoteaddr, tmp, HIO_COUNTOF(tmp), HIO_SKAD_TO_BCSTR_PORT);
	if (hio_svc_fcgic_writeparam(fcgi->peer,  "REMOTE_PORT", 11, tmp, len) <= -1) goto oops;

	hio_htre_walkheaders (req, peer_capture_request_header, fcgi);
	/* [NOTE] trailers are not available when this cgi resource is started. let's not call hio_htre_walktrailers() */

	hio_freemem (hio, actual_script);
	return 0;

oops:
	if (actual_script) hio_freemem (hio, actual_script);
	return -1;
}

/* ----------------------------------------------------------------------- */

static void bind_task_to_client (fcgi_t* fcgi, hio_dev_sck_t* csck)
{
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(csck);

	HIO_ASSERT (fcgi->htts->hio, cli->sck == csck);
	HIO_ASSERT (fcgi->htts->hio, cli->task == HIO_NULL);

	/* fcgi->task_client and fcgi->task_csck are set in hio_svc_htts_task_make() */

	/* remember the client socket's io event handlers */
	fcgi->client_org_on_read = csck->on_read;
	fcgi->client_org_on_write = csck->on_write;
	fcgi->client_org_on_disconnect = csck->on_disconnect;

	/* set new io events handlers on the client socket */
	csck->on_read = fcgi_client_on_read;
	csck->on_write = fcgi_client_on_write;
	csck->on_disconnect = fcgi_client_on_disconnect;

	cli->task = (hio_svc_htts_task_t*)fcgi;
	HIO_SVC_HTTS_TASK_RCUP (fcgi);
}

static void unbind_task_from_client (fcgi_t* fcgi, int rcdown)
{
	hio_dev_sck_t* csck = fcgi->task_csck;

	HIO_ASSERT (fcgi->htts->hio, fcgi->task_client != HIO_NULL);
	HIO_ASSERT (fcgi->htts->hio, fcgi->task_csck != HIO_NULL);
	HIO_ASSERT (fcgi->htts->hio, fcgi->task_client->task == (hio_svc_htts_task_t*)fcgi);
	HIO_ASSERT (fcgi->htts->hio, fcgi->task_client->htrd != HIO_NULL);

	if (fcgi->client_htrd_recbs_changed) 
	{
		hio_htrd_setrecbs (fcgi->task_client->htrd, &fcgi->client_htrd_org_recbs);
		fcgi->client_htrd_recbs_changed = 0;
	}

	if (fcgi->client_org_on_read)
	{
		csck->on_read = fcgi->client_org_on_read;
		fcgi->client_org_on_read = HIO_NULL;
	}

	if (fcgi->client_org_on_write)
	{
		csck->on_write = fcgi->client_org_on_write;
		fcgi->client_org_on_write = HIO_NULL;
	}

	if (fcgi->client_org_on_disconnect)
	{
		csck->on_disconnect = fcgi->client_org_on_disconnect;
		fcgi->client_org_on_disconnect = HIO_NULL;
	}

	/* there is some ordering issue in using HIO_SVC_HTTS_TASK_UNREF()
	 * because it can destroy the fcgi itself. so reset fcgi->task_client->task
	 * to null and call RCDOWN() later */
	fcgi->task_client->task = HIO_NULL;

	/* these two lines are also done in csck_on_disconnect() in http-svr.c because the socket is destroyed.
	 * the same lines here are because the task is unbound while the socket is still alive */
	fcgi->task_client = HIO_NULL;
	fcgi->task_csck = HIO_NULL;

	/* enable input watching on the socket being unbound */
	if (fcgi->task_keep_client_alive && hio_dev_sck_read(csck, 1) <= -1)
	{
		HIO_DEBUG2 (fcgi->htts->hio, "HTTS(%p) - halting client(%p) for failure to enable input watching\n", fcgi->htts, csck);
		hio_dev_sck_halt (csck);
	}

	if (rcdown) HIO_SVC_HTTS_TASK_RCDOWN ((hio_svc_htts_task_t*)fcgi);
}

/* ----------------------------------------------------------------------- */

static int bind_task_to_peer (fcgi_t* fcgi, const hio_skad_t* fcgis_addr)
{
	hio_htrd_t* htrd;
	fcgi_peer_xtn_t* pxtn;

	htrd = hio_htrd_open(fcgi->htts->hio, HIO_SIZEOF(*pxtn));
	if (HIO_UNLIKELY(!htrd)) return -1;

	hio_htrd_setoption (htrd, HIO_HTRD_SKIP_INITIAL_LINE | HIO_HTRD_RESPONSE);
	hio_htrd_setrecbs (htrd, &peer_htrd_recbs);

	fcgi->peer = hio_svc_fcgic_tie(fcgi->htts->fcgic, fcgis_addr, fcgi_peer_on_read, fcgi_peer_on_write, fcgi_peer_on_untie, fcgi);
	if (HIO_UNLIKELY(!fcgi->peer)) 
	{
		hio_htrd_close (htrd);
		return -1;
	}

	pxtn = hio_htrd_getxtn(htrd);
	pxtn->fcgi = fcgi;
	fcgi->peer_htrd = htrd;

	HIO_SVC_HTTS_TASK_RCUP (fcgi); /* for peer_htrd extension */
	HIO_SVC_HTTS_TASK_RCUP (fcgi); /* for fcgi->peer->ctx in the tie() */

	return 0;
}

static void unbind_task_from_peer (fcgi_t* fcgi, int rcdown)
{
	int n = 0;

	if (fcgi->peer_htrd)
	{
		hio_htrd_close (fcgi->peer_htrd);
		fcgi->peer_htrd = HIO_NULL;
		n++;
	}

	if (fcgi->peer)
	{
		/* hio_svc_fcgic_untie() is not a delayed operation unlike hio_dev_sck_halt().
		 * TODO: check if this is a buggy idea */
		hio_svc_fcgic_untie (fcgi->peer);
		fcgi->peer = HIO_NULL;
		n++;
	}

	if (rcdown)
	{
		while (n > 0)
		{
			n--;
			HIO_SVC_HTTS_TASK_RCDOWN((hio_svc_htts_task_t*)fcgi);
		}
	}
}

/* ----------------------------------------------------------------------- */

static int setup_for_content_length(fcgi_t* fcgi, hio_htre_t* req)
{
	int have_content;

#if defined(FCGI_ALLOW_UNLIMITED_REQ_CONTENT_LENGTH)
	have_content = fcgi->task_req_conlen > 0 || fcgi->task_req_conlen_unlimited;
#else
	have_content = fcgi->task_req_conlen > 0;
#endif

	if (have_content)
	{
		/* change the callbacks to subscribe to contents to be uploaded */
		fcgi->client_htrd_org_recbs = *hio_htrd_getrecbs(fcgi->task_client->htrd);
		fcgi_client_htrd_recbs.peek = fcgi->client_htrd_org_recbs.peek;
		hio_htrd_setrecbs (fcgi->task_client->htrd, &fcgi_client_htrd_recbs);
		fcgi->client_htrd_recbs_changed = 1;
	}
	else
	{
		/* no content to be uploaded from the client */
		/* indicate end of stdin to the peer and disable input wathching from the client */
		if (fcgi_write_stdin_to_peer(fcgi, HIO_NULL, 0) <= -1) return -1;
		fcgi_mark_over (fcgi, FCGI_OVER_READ_FROM_CLIENT | FCGI_OVER_WRITE_TO_PEER);
	}

	return 0;
}

/* ----------------------------------------------------------------------- */

int hio_svc_htts_dofcgi (hio_svc_htts_t* htts, hio_dev_sck_t* csck, hio_htre_t* req, const hio_skad_t* fcgis_addr, const hio_bch_t* docroot, const hio_bch_t* script, int options, hio_svc_htts_task_on_kill_t on_kill)
{
	hio_t* hio = htts->hio;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(csck);
	fcgi_t* fcgi = HIO_NULL;
	int bound_to_client = 0, bound_to_peer = 0;

	/* ensure that you call this function before any contents is received */
	HIO_ASSERT (hio, hio_htre_getcontentlen(req) == 0);

	if (HIO_UNLIKELY(!htts->fcgic))
	{
		hio_seterrbfmt (hio, HIO_ENOCAPA, "fcgi client service not enabled");
		goto oops;
	}

	fcgi = (fcgi_t*)hio_svc_htts_task_make(htts, HIO_SIZEOF(*fcgi), fcgi_on_kill, req, csck);
	if (HIO_UNLIKELY(!fcgi)) goto oops;
	HIO_SVC_HTTS_TASK_RCUP ((hio_svc_htts_task_t*)fcgi);

	fcgi->on_kill = on_kill; /* custom on_kill handler by the caller */

	bind_task_to_client (fcgi, csck);
	bound_to_client = 1;

	if (bind_task_to_peer(fcgi, fcgis_addr) <= -1) goto oops;
	bound_to_peer = 1;

	if (hio_svc_htts_task_handleexpect100(fcgi) <= -1) goto oops;
	if (setup_for_content_length(fcgi, req) <= -1) goto oops;

	/* TODO: store current input watching state and use it when destroying the fcgi data */
	if (hio_dev_sck_read(csck, !(fcgi->over & FCGI_OVER_READ_FROM_CLIENT)) <= -1) goto oops;

	/* send FCGI_BEGIN_REQUEST */
	if (hio_svc_fcgic_beginrequest(fcgi->peer) <= -1) goto oops;
	/* write FCGI_PARAM */
	if (write_params(fcgi, csck, req, docroot, script) <= -1) goto oops;
	if (hio_svc_fcgic_writeparam(fcgi->peer, HIO_NULL, 0, HIO_NULL, 0) <= -1) goto oops; /* end of params */

	HIO_SVC_HTTS_TASKL_APPEND_TASK (&htts->task, (hio_svc_htts_task_t*)fcgi);
	HIO_SVC_HTTS_TASK_RCDOWN ((hio_svc_htts_task_t*)fcgi);
	return 0;

oops:
	HIO_DEBUG2 (hio, "HTTS(%p) - FAILURE in dofcgi - socket(%p)\n", htts, csck);
	if (fcgi)
	{
		if (bound_to_peer) unbind_task_from_peer (fcgi, 1);
		if (bound_to_client) unbind_task_from_client (fcgi, 1);
		fcgi_halt_participating_devices ((hio_svc_htts_task_t*)fcgi);
		HIO_SVC_HTTS_TASK_RCDOWN ((hio_svc_htts_task_t*)fcgi);
	}
	return -1;
}
