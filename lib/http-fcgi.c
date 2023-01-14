#include "http-prv.h"
#include <hio-fmt.h>
#include <hio-chr.h>
#include <hio-fcgi.h>

#define FCGI_ALLOW_UNLIMITED_REQ_CONTENT_LENGTH

enum fcgi_res_mode_t
{
	FCGI_RES_MODE_CHUNKED,
	FCGI_RES_MODE_CLOSE,
	FCGI_RES_MODE_LENGTH
};
typedef enum fcgi_res_mode_t fcgi_res_mode_t;


#define FCGI_PENDING_IO_THRESHOLD 5

#define FCGI_OVER_READ_FROM_CLIENT (1 << 0)
#define FCGI_OVER_READ_FROM_PEER   (1 << 1)
#define FCGI_OVER_WRITE_TO_CLIENT  (1 << 2)
#define FCGI_OVER_WRITE_TO_PEER    (1 << 3)
#define FCGI_OVER_ALL (FCGI_OVER_READ_FROM_CLIENT | FCGI_OVER_READ_FROM_PEER | FCGI_OVER_WRITE_TO_CLIENT | FCGI_OVER_WRITE_TO_PEER)

struct fcgi_t
{
	HIO_SVC_HTTS_TASK_HEADER;

	hio_oow_t num_pending_writes_to_client;
	hio_oow_t num_pending_writes_to_peer;
	hio_svc_fcgic_sess_t* peer;

	hio_dev_sck_t* csck;
	hio_svc_htts_cli_t* client;
	hio_http_method_t req_method;
	hio_http_version_t req_version; /* client request */

	unsigned int over: 4; /* must be large enough to accomodate FCGI_OVER_ALL */
	unsigned int keep_alive: 1;
	unsigned int req_content_length_unlimited: 1;
	unsigned int ever_attempted_to_write_to_client: 1;
	unsigned int client_disconnected: 1;
	unsigned int client_htrd_recbs_changed: 1;
	hio_oow_t req_content_length; /* client request content length */
	fcgi_res_mode_t res_mode_to_cli;

	hio_dev_sck_on_read_t client_org_on_read;
	hio_dev_sck_on_write_t client_org_on_write;
	hio_dev_sck_on_disconnect_t client_org_on_disconnect;
	hio_htrd_recbs_t client_htrd_org_recbs;
};
typedef struct fcgi_t fcgi_t;


#if 0
static int begin_request ()
{
	struct fcgi_record_header* h;
	struct fcgi_begin_request_body* br;

	h->version = FCGI_VERSION;
	h->type = FCGI_BEGIN_REQUEST;

	h->id = HIO_CONST_HTON16(1);
	h->content_len = HIO_HTON16(HIO_SIZEOF(struct fcgi_begin_request_body));
	h->padding_len = 0;


	br->role = HIO_CONST_HTON16(FCGI_RESPONDER);
	br->flags = 0;


	h->type = FCGI_PARAMS;
	h->content_len = 0;


/*
	h->type = FCGI_STDIN;
*/

}

#endif

static void fcgi_halt_participating_devices (fcgi_t* fcgi)
{
	HIO_ASSERT (fcgi->htts->hio, fcgi->client != HIO_NULL);
	HIO_ASSERT (fcgi->htts->hio, fcgi->csck != HIO_NULL);

/* TODO: include fcgi session id in the output in place of peer??? */
	HIO_DEBUG3 (fcgi->htts->hio, "HTTS(%p) - Halting participating devices in fcgi state %p(client=%p)\n", fcgi->htts, fcgi, fcgi->csck);

	hio_dev_sck_halt (fcgi->csck);

#if 0
	/* check for peer as it may not have been started */
/* TODO: send abort if the transmission didn't end ... */
	if (fcgi->peer) hio_dev_pro_halt (fcgi->peer);
#endif
}

static int fcgi_write_to_peer (fcgi_t* fcgi, const void* data, hio_iolen_t dlen)
{
#if 0
	fcgi->num_pending_writes_to_peer++;
	if (hio_dev_pro_write(fcgi->peer, data, dlen, HIO_NULL) <= -1)
	{
		fcgi->num_pending_writes_to_peer--;
		return -1;
	}
#endif

/* TODO: check if it's already finished or something.. */
	if (fcgi->num_pending_writes_to_peer > FCGI_PENDING_IO_THRESHOLD)
	{
		/* disable input watching */
		if (hio_dev_sck_read(fcgi->csck, 0) <= -1) return -1;
	}
	return 0;
}

static HIO_INLINE void fcgi_mark_over (fcgi_t* fcgi, int over_bits)
{
	unsigned int old_over;

	old_over = fcgi->over;
	fcgi->over |= over_bits;

	HIO_DEBUG4 (fcgi->htts->hio, "HTTS(%p) - client=%p new-bits=%x over=%x\n", fcgi->htts, fcgi->csck, (int)over_bits, (int)fcgi->over);

	if (!(old_over & FCGI_OVER_READ_FROM_CLIENT) && (fcgi->over & FCGI_OVER_READ_FROM_CLIENT))
	{
printf (">>>>>>>>>>>> disableing client read watching ...................\n");
		if (hio_dev_sck_read(fcgi->csck, 0) <= -1)
		{
			HIO_DEBUG2 (fcgi->htts->hio, "HTTS(%p) - halting client(%p) for failure to disable input watching\n", fcgi->htts, fcgi->csck);
			hio_dev_sck_halt (fcgi->csck);
		}
	}

	if (!(old_over & FCGI_OVER_READ_FROM_PEER) && (fcgi->over & FCGI_OVER_READ_FROM_PEER))
	{
#if 0 // TODO:
		if (fcgi->peer && hio_dev_pro_read(fcgi->peer, HIO_DEV_PRO_OUT, 0) <= -1)
		{
			HIO_DEBUG2 (fcgi->htts->hio, "HTTS(%p) - halting peer(%p) for failure to disable input watching\n", fcgi->htts, fcgi->peer);
			hio_dev_pro_halt (fcgi->peer);
		}
#endif
	}

	if (old_over != FCGI_OVER_ALL && fcgi->over == FCGI_OVER_ALL)
	{
		/* ready to stop */
#if 0 // TODO:
		if (fcgi->peer)
		{
			HIO_DEBUG2 (fcgi->htts->hio, "HTTS(%p) - halting peer(%p) as it is unneeded\n", fcgi->htts, fcgi->peer);
			hio_dev_pro_halt (fcgi->peer);
		}
#endif

		if (fcgi->keep_alive)
		{
			/* how to arrange to delete this fcgi object and put the socket back to the normal waiting state??? */
			HIO_ASSERT (fcgi->htts->hio, fcgi->client->task == (hio_svc_htts_task_t*)fcgi);

/*printf ("DETACHING FROM THE MAIN CLIENT TASK... state -> %p\n", fcgi->client->task);*/
			HIO_SVC_HTTS_TASK_UNREF (fcgi->client->task);
			/* fcgi must not be accessed from here down as it could have been destroyed */
		}
		else
		{
			HIO_DEBUG2 (fcgi->htts->hio, "HTTS(%p) - halting client(%p) for no keep-alive\n", fcgi->htts, fcgi->csck);
			hio_dev_sck_shutdown (fcgi->csck, HIO_DEV_SCK_SHUTDOWN_WRITE);
			hio_dev_sck_halt (fcgi->csck);
		}
	}
}

static int fcgi_write_to_client (fcgi_t* fcgi, const void* data, hio_iolen_t dlen)
{
	fcgi->ever_attempted_to_write_to_client = 1;

	fcgi->num_pending_writes_to_client++;
	if (hio_dev_sck_write(fcgi->csck, data, dlen, HIO_NULL, HIO_NULL) <= -1)
	{
		fcgi->num_pending_writes_to_client--;
		return -1;
	}

	if (fcgi->num_pending_writes_to_client > FCGI_PENDING_IO_THRESHOLD)
	{
		/* disable reading on the output stream of the peer */
#if 0 // TODO
		if (hio_dev_pro_read(fcgi->peer, HIO_DEV_PRO_OUT, 0) <= -1) return -1;
#endif
	}
	return 0;
}

static int fcgi_send_final_status_to_client (fcgi_t* fcgi, int status_code, int force_close)
{
	hio_svc_htts_cli_t* cli = fcgi->client;
	hio_bch_t dtbuf[64];
	const hio_bch_t* status_msg;
	hio_oow_t content_len;

	hio_svc_htts_fmtgmtime (cli->htts, HIO_NULL, dtbuf, HIO_COUNTOF(dtbuf));
	status_msg = hio_http_status_to_bcstr(status_code);
	content_len = hio_count_bcstr(status_msg);

	if (!force_close) force_close = !fcgi->keep_alive;
	if (hio_becs_fmt(cli->sbuf, "HTTP/%d.%d %d %hs\r\nServer: %hs\r\nDate: %hs\r\nConnection: %hs\r\n",
		fcgi->req_version.major, fcgi->req_version.minor,
		status_code, status_msg,
		cli->htts->server_name, dtbuf,
		(force_close? "close": "keep-alive")) == (hio_oow_t)-1) return -1;

	if (fcgi->req_method == HIO_HTTP_HEAD)
	{
		if (status_code != HIO_HTTP_STATUS_OK) content_len = 0;
		status_msg = "";
	}

	if (hio_becs_fcat(cli->sbuf, "Content-Type: text/plain\r\nContent-Length: %zu\r\n\r\n%hs", content_len, status_msg) == (hio_oow_t)-1) return -1;

	return (fcgi_write_to_client(fcgi, HIO_BECS_PTR(cli->sbuf), HIO_BECS_LEN(cli->sbuf)) <= -1 ||
	        (force_close && fcgi_write_to_client(fcgi, HIO_NULL, 0) <= -1))? -1: 0;
}

static int fcgi_client_htrd_poke (hio_htrd_t* htrd, hio_htre_t* req)
{
	/* the client request got completed including body.
	 * this callback is set and called only if there is content in the request */
	hio_svc_htts_cli_htrd_xtn_t* htrdxtn = (hio_svc_htts_cli_htrd_xtn_t*)hio_htrd_getxtn(htrd);
	hio_dev_sck_t* sck = htrdxtn->sck;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	fcgi_t* fcgi = (fcgi_t*)cli->task;

	/* indicate end of STDIN */
	if (hio_svc_fcgic_writestdin(fcgi->peer, HIO_NULL, 0) <= -1) return -1;

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
	return hio_svc_fcgic_writestdin(fcgi->peer, data, dlen);
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
	fcgi_t* fcgi = (fcgi_t*)cli->task;
	fcgi->client_disconnected = 1;
printf ("client disconnected ............................\n");
	fcgi->client_org_on_disconnect (sck);
}

static int fcgi_client_on_read (hio_dev_sck_t* sck, const void* buf, hio_iolen_t len, const hio_skad_t* srcaddr)
{
	hio_t* hio = sck->hio;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	fcgi_t* fcgi = (fcgi_t*)cli->task;

	HIO_ASSERT (hio, sck == cli->sck);

	if (len <= -1)
	{
		/* read error */
		HIO_DEBUG2 (cli->htts->hio, "HTTS(%p) - read error on client %p(%d)\n", sck, (int)sck->hnd);
		goto oops;
	}

#if 0
	if (!fcgi->peer)
	{
		/* the peer is gone */
		goto oops; /* do what?  just return 0? */
	}
#endif

	if (len == 0)
	{
		/* EOF on the client side. arrange to close */
		HIO_DEBUG3 (hio, "HTTS(%p) - EOF from client %p(hnd=%d)\n", fcgi->htts, sck, (int)sck->hnd);

		if (!(fcgi->over & FCGI_OVER_READ_FROM_CLIENT)) /* if this is true, EOF is received without fcgi_client_htrd_poke() */
		{
			/* indicate eof to the write side */
			if (hio_svc_fcgic_writestdin(fcgi->peer, HIO_NULL, 0) <= -1) goto oops;
			fcgi_mark_over (fcgi, FCGI_OVER_READ_FROM_CLIENT);
		}
	}
	else
	{
		hio_oow_t rem;

		HIO_ASSERT (hio, !(fcgi->over & FCGI_OVER_READ_FROM_CLIENT));

		if (hio_htrd_feed(cli->htrd, buf, len, &rem) <= -1) goto oops;

		if (rem > 0)
		{
			/* TODO store this to client buffer. once the current resource is completed, arrange to call on_read() with it */
			HIO_DEBUG3 (hio, "HTTS(%p) - excessive data after contents by fcgi client %p(%d)\n", sck->hio, sck, (int)sck->hnd);
		}
	}

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

	if (wrlen <= -1)
	{
		HIO_DEBUG3 (hio, "HTTS(%p) - unable to write to client %p(%d)\n", sck->hio, sck, (int)sck->hnd);
		goto oops;
	}

	if (wrlen == 0)
	{
		/* if the connect is keep-alive, this part may not be called */
		fcgi->num_pending_writes_to_client--;
		HIO_ASSERT (hio, fcgi->num_pending_writes_to_client == 0);
		HIO_DEBUG3 (hio, "HTTS(%p) - indicated EOF to client %p(%d)\n", fcgi->htts, sck, (int)sck->hnd);
		/* since EOF has been indicated to the client, it must not write to the client any further.
		 * this also means that i don't need any data from the peer side either.
		 * i don't need to enable input watching on the peer side */
		fcgi_mark_over (fcgi, FCGI_OVER_WRITE_TO_CLIENT);
	}
	else
	{
		HIO_ASSERT (hio, fcgi->num_pending_writes_to_client > 0);

#if 0 // TODO
		fcgi->num_pending_writes_to_client--;
		if (fcgi->peer && fcgi->num_pending_writes_to_client == FCGI_PENDING_IO_THRESHOLD)
		{
			if (!(fcgi->over & FCGI_OVER_READ_FROM_PEER) &&
			    hio_dev_pro_read(fcgi->peer, HIO_DEV_PRO_OUT, 1) <= -1) goto oops;
		}
#endif

		if ((fcgi->over & FCGI_OVER_READ_FROM_PEER) && fcgi->num_pending_writes_to_client <= 0)
		{
			fcgi_mark_over (fcgi, FCGI_OVER_WRITE_TO_CLIENT);
		}
	}

	return 0;

oops:
	fcgi_halt_participating_devices (fcgi);
	return 0;
}

static int fcgi_peer_on_read (hio_svc_fcgic_sess_t* peer, const void* buf, hio_iolen_t len)
{
printf ("GOT FCGI DATA.............[%.*s]\n", (int)len, buf);
	return 0;
}

static void fcgi_on_kill (hio_svc_htts_task_t* task)
{
	fcgi_t* fcgi = (fcgi_t*)task;
	hio_t* hio = fcgi->htts->hio;

	HIO_DEBUG2 (hio, "HTTS(%p) - killing fcgi client(%p)\n", fcgi->htts, fcgi->csck);

#if 0
	if (fcgi->peer)
	{
		fcgi_peer_xtn_t* peer = hio_dev_pro_getxtn(fcgi->peer);
		peer->state = HIO_NULL;  /* peer->state many not be NULL if the resource is killed regardless of the reference count */

		hio_dev_pro_kill (fcgi->peer);
		fcgi->peer = HIO_NULL;
	}
#endif

	if (fcgi->csck)
	{
		HIO_ASSERT (hio, fcgi->client != HIO_NULL);

		if (fcgi->client_org_on_read) fcgi->csck->on_read = fcgi->client_org_on_read;
		if (fcgi->client_org_on_write) fcgi->csck->on_write = fcgi->client_org_on_write;
		if (fcgi->client_org_on_disconnect) fcgi->csck->on_disconnect = fcgi->client_org_on_disconnect;
		if (fcgi->client_htrd_recbs_changed) hio_htrd_setrecbs (fcgi->client->htrd, &fcgi->client_htrd_org_recbs);

		if (!fcgi->client_disconnected)
		{
			if (!fcgi->keep_alive || hio_dev_sck_read(fcgi->csck, 1) <= -1)
			{
				HIO_DEBUG2 (hio, "HTTS(%p) - halting client(%p) for failure to enable input watching\n", fcgi->htts, fcgi->csck);
				hio_dev_sck_halt (fcgi->csck);
			}
		}
	}

	fcgi->client_org_on_read = HIO_NULL;
	fcgi->client_org_on_write = HIO_NULL;
	fcgi->client_org_on_disconnect = HIO_NULL;
	fcgi->client_htrd_recbs_changed = 0;

	if (fcgi->task_next) HIO_SVC_HTTS_TASKL_UNLINK_TASK (fcgi); /* detach from the htts service only if it's attached */
}

static int write_params (fcgi_t* fcgi, hio_dev_sck_t* csck, hio_htre_t* req, const hio_bch_t* docroot, const hio_bch_t* script)
{
	hio_t* hio = fcgi->htts->hio;
	hio_bch_t tmp[256];
	hio_oow_t len;
	const hio_bch_t* qparam;
	hio_oow_t content_length;
	hio_bch_t* actual_script = HIO_NULL;

	HIO_ASSERT (hio, fcgi->csck == csck);

	actual_script = hio_svc_htts_dupmergepaths(fcgi->htts, docroot, script);
	if (!actual_script) goto oops;

	if (hio_svc_fcgic_writeparam(fcgi->peer, "GATEWAY_INTERFACE", 17, "CGI/1.1", 7) <= -1) goto oops;

	len = hio_fmttobcstr(hio, tmp, HIO_COUNTOF(tmp), "HTTP/%d.%d", (int)hio_htre_getmajorversion(req), (int)hio_htre_getminorversion(req));
	if (hio_svc_fcgic_writeparam(fcgi->peer, "SERVER_PROTOCOL", 15, tmp, len) <= -1) goto oops;

	if (hio_svc_fcgic_writeparam(fcgi->peer, "DOCUMENT_ROOT", 13, docroot, hio_count_bcstr(docroot)) <= -1) goto oops;
	if (hio_svc_fcgic_writeparam(fcgi->peer, "SCRIPT_NAME", 11, script, hio_count_bcstr(script)) <= -1) goto oops;
	if (hio_svc_fcgic_writeparam(fcgi->peer, "SCRIPT_FILENAME", 15, actual_script, hio_count_bcstr(actual_script)) <= -1) goto oops;
// TODO: PATH_INFO

	if (hio_svc_fcgic_writeparam(fcgi->peer, "REQUEST_METHOD", 14, hio_htre_getqmethodname(req), hio_htre_getqmethodlen(req)) <= -1) goto oops;
	if (hio_svc_fcgic_writeparam(fcgi->peer, "REQUEST_URI", 11, hio_htre_getqpath(req), hio_htre_getqpathlen(req)) <= -1) goto oops;

    qparam = hio_htre_getqparam(req);
	if (qparam && hio_svc_fcgic_writeparam(fcgi->peer, "QUERY_STRING", 12, qparam, hio_count_bcstr(qparam)) <= -1) goto oops;

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

	//hio_htre_walkheaders (req,)

	hio_freemem (hio, actual_script);
	return 0;

oops:
	if (actual_script) hio_freemem (hio, actual_script);
	return -1;
}
int hio_svc_htts_dofcgi (hio_svc_htts_t* htts, hio_dev_sck_t* csck, hio_htre_t* req, const hio_skad_t* fcgis_addr, const hio_bch_t* docroot, const hio_bch_t* script, int options)
{
	hio_t* hio = htts->hio;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(csck);
	fcgi_t* fcgi = HIO_NULL;
	//fcgi_peer_xtn_t* peer_xtn;

	/* ensure that you call this function before any contents is received */
	HIO_ASSERT (hio, hio_htre_getcontentlen(req) == 0);

	if (HIO_UNLIKELY(!htts->fcgic))
	{
		hio_seterrbfmt (hio, HIO_ENOCAPA, "fcgi client service not enabled");
		goto oops;
	}

	fcgi = (fcgi_t*)hio_svc_htts_task_make(htts, HIO_SIZEOF(*fcgi), fcgi_on_kill);
	if (HIO_UNLIKELY(!fcgi)) goto oops;

	fcgi->csck = csck;
	fcgi->client = cli;
	/*fcgi->num_pending_writes_to_client = 0;
	fcgi->num_pending_writes_to_peer = 0;*/
	fcgi->req_method = hio_htre_getqmethodtype(req);
	fcgi->req_version = *hio_htre_getversion(req);
	fcgi->req_content_length_unlimited = hio_htre_getreqcontentlen(req, &fcgi->req_content_length);

	/* remember the client socket's io event handlers */
	fcgi->client_org_on_read = csck->on_read;
	fcgi->client_org_on_write = csck->on_write;
	fcgi->client_org_on_disconnect = csck->on_disconnect;
	/* set new io events handlers on the client socket */
	csck->on_read = fcgi_client_on_read;
	csck->on_write = fcgi_client_on_write;
	csck->on_disconnect = fcgi_client_on_disconnect;

	HIO_ASSERT (hio, cli->task == HIO_NULL);
	HIO_SVC_HTTS_TASK_REF ((hio_svc_htts_task_t*)fcgi, cli->task); /* cli->task = fcgi */

	/* create a session in in the fcgi client service */
	fcgi->peer = hio_svc_fcgic_tie(htts->fcgic, fcgis_addr, fcgi_peer_on_read);
	if (HIO_UNLIKELY(!fcgi->peer)) goto oops;

	/* send FCGI_BEGIN_REQUEST */
	if (hio_svc_fcgic_beginrequest(fcgi->peer) <= -1) goto oops;
	/* write FCGI_PARAM */
	if (write_params(fcgi, csck, req, docroot, script) <= -1) goto oops;
	if (hio_svc_fcgic_writeparam(fcgi->peer, HIO_NULL, 0, HIO_NULL, 0) <= -1) goto oops; /* end of params */

#if !defined(FCGI_ALLOW_UNLIMITED_REQ_CONTENT_LENGTH)
	if (fcgi->req_content_length_unlimited)
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
		/* TODO: Expect: 100-continue? who should handle this? fcgi? or the http server? */
		/* CAN I LET the cgi SCRIPT handle this? */
		if (!(options & HIO_SVC_HTTS_CGI_NO_100_CONTINUE) &&
		    hio_comp_http_version_numbers(&req->version, 1, 1) >= 0 &&
		   (fcgi->req_content_length_unlimited || fcgi->req_content_length > 0))
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

			msglen = hio_fmttobcstr(hio, msgbuf, HIO_COUNTOF(msgbuf), "HTTP/%d.%d %d %hs\r\n\r\n", fcgi->req_version.major, fcgi->req_version.minor, HIO_HTTP_STATUS_CONTINUE, hio_http_status_to_bcstr(HIO_HTTP_STATUS_CONTINUE));
			if (fcgi_write_to_client(fcgi, msgbuf, msglen) <= -1) goto oops;
			fcgi->ever_attempted_to_write_to_client = 0; /* reset this as it's polluted for 100 continue */
		}
	}
	else if (req->flags & HIO_HTRE_ATTR_EXPECT)
	{
		/* 417 Expectation Failed */
		fcgi_send_final_status_to_client(fcgi, HIO_HTTP_STATUS_EXPECTATION_FAILED, 1);
		goto oops;
	}

#if defined(FCGI_ALLOW_UNLIMITED_REQ_CONTENT_LENGTH)
	if (fcgi->req_content_length_unlimited)
	{
		/* change the callbacks to subscribe to contents to be uploaded */
		fcgi->client_htrd_org_recbs = *hio_htrd_getrecbs(fcgi->client->htrd);
		fcgi_client_htrd_recbs.peek = fcgi->client_htrd_org_recbs.peek;
		hio_htrd_setrecbs (fcgi->client->htrd, &fcgi_client_htrd_recbs);
		fcgi->client_htrd_recbs_changed = 1;
	}
	else
	{
#endif
		if (fcgi->req_content_length > 0)
		{
			/* change the callbacks to subscribe to contents to be uploaded */
			fcgi->client_htrd_org_recbs = *hio_htrd_getrecbs(fcgi->client->htrd);
			fcgi_client_htrd_recbs.peek = fcgi->client_htrd_org_recbs.peek;
			hio_htrd_setrecbs (fcgi->client->htrd, &fcgi_client_htrd_recbs);
			fcgi->client_htrd_recbs_changed = 1;
		}
		else
		{
			/* no content to be uploaded from the client */
			/* indicate end of stdin to the peer and disable input wathching from the client */
			if (hio_svc_fcgic_writestdin(fcgi->peer, HIO_NULL, 0) <= -1) goto oops;
			fcgi_mark_over (fcgi, FCGI_OVER_READ_FROM_CLIENT | FCGI_OVER_WRITE_TO_PEER);
		}
#if defined(FCGI_ALLOW_UNLIMITED_REQ_CONTENT_LENGTH)
	}
#endif

	/* this may change later if Content-Length is included in the cgi output */
	if (req->flags & HIO_HTRE_ATTR_KEEPALIVE)
	{
		fcgi->keep_alive = 1;
		fcgi->res_mode_to_cli = FCGI_RES_MODE_CHUNKED;
		/* the mode still can get switched to FCGI_RES_MODE_LENGTH if the cgi script emits Content-Length */
	}
	else
	{
		fcgi->keep_alive = 0;
		fcgi->res_mode_to_cli = FCGI_RES_MODE_CLOSE;
	}

	/* TODO: store current input watching state and use it when destroying the cgi data */
	if (hio_dev_sck_read(csck, !(fcgi->over & FCGI_OVER_READ_FROM_CLIENT)) <= -1) goto oops;

	HIO_SVC_HTTS_TASKL_APPEND_TASK (&htts->task, fcgi);
	return 0;

oops:
	HIO_DEBUG2 (hio, "HTTS(%p) - FAILURE in dofcgi - socket(%p)\n", htts, csck);
	if (fcgi) fcgi_halt_participating_devices (fcgi);
	return -1;
}
