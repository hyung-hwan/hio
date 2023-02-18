#include "http-prv.h"
#include <hio-fmt.h>
#include <hio-chr.h>
#include <hio-fcgi.h>
#include <unistd.h>

#define FCGI_ALLOW_UNLIMITED_REQ_CONTENT_LENGTH

enum fcgi_res_mode_t
{
	FCGI_RES_MODE_CHUNKED,
	FCGI_RES_MODE_CLOSE,
	FCGI_RES_MODE_LENGTH
};
typedef enum fcgi_res_mode_t fcgi_res_mode_t;


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

	hio_oow_t num_pending_writes_to_client;
	hio_oow_t num_pending_writes_to_peer;
	hio_svc_fcgic_sess_t* peer;
	hio_htrd_t* peer_htrd;

	hio_dev_sck_t* csck;
	hio_svc_htts_cli_t* client;
	hio_http_method_t req_method;
	hio_http_version_t req_version; /* client request */

	unsigned int over: 4; /* must be large enough to accomodate FCGI_OVER_ALL */
	unsigned int keep_alive: 1;
	unsigned int req_content_length_unlimited: 1;
	unsigned int ever_attempted_to_write_to_client: 1;
	unsigned int last_chunk_sent: 1;
	unsigned int client_disconnected: 1;
	unsigned int client_eof_detected: 1;
	unsigned int client_htrd_recbs_changed: 1;
	hio_oow_t req_content_length; /* client request content length */
	fcgi_res_mode_t res_mode_to_cli;

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
	HIO_DEBUG5 (fcgi->htts->hio, "HTTS(%p) - cgi(t=%p,c=%p(%d),p=%p) Halting participating devices\n", fcgi->htts, fcgi, fcgi->csck, (fcgi->csck? fcgi->csck->hnd: -1), fcgi->peer);

	if (fcgi->csck) hio_dev_sck_halt (fcgi->csck);
	unbind_task_from_peer (fcgi, 1);
}

static int fcgi_write_to_client (fcgi_t* fcgi, const void* data, hio_iolen_t dlen)
{
	if (fcgi->csck)
	{
		fcgi->ever_attempted_to_write_to_client = 1;

		fcgi->num_pending_writes_to_client++;
		if (hio_dev_sck_write(fcgi->csck, data, dlen, HIO_NULL, HIO_NULL) <= -1)
		{
			fcgi->num_pending_writes_to_client--;
			return -1;
		}

	#if 0
		if (fcgi->num_pending_writes_to_client > FCGI_PENDING_IO_THRESHOLD_TO_CLIENT)
		{

			/* the fcgic service is shared. whent the client side is stuck,
			 * it's not natural to stop reading from the whole service. */
			/* if (hio_svc_fcgic_read(fcgi->peer, 0) <= -1) return -1; */

			/* do nothing for now. TODO: but should the slow client connection be aborted??? */
		}
	#endif
	}
	return 0;
}

static int fcgi_writev_to_client (fcgi_t* fcgi, hio_iovec_t* iov, hio_iolen_t iovcnt)
{
	if (fcgi->csck)
	{
		fcgi->ever_attempted_to_write_to_client = 1;

		fcgi->num_pending_writes_to_client++;
		if (hio_dev_sck_writev(fcgi->csck, iov, iovcnt, HIO_NULL, HIO_NULL) <= -1)
		{
			fcgi->num_pending_writes_to_client--;
			return -1;
		}

	#if 0
		if (fcgi->num_pending_writes_to_client > FCGI_PENDING_IO_THRESHOLD_TO_CLIENT)
		{

			/* the fcgic service is shared. whent the client side is stuck,
		     * it's not natural to stop reading from the whole service. */
			/* if (hio_svc_fcgic_read(fcgi->peer, 0) <= -1) return -1; */

			/* do nothing for now. TODO: but should the slow client connection be aborted??? */
		}
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

	if (!cli) return; /* client unbound or no binding client */

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

static int fcgi_write_last_chunk_to_client (fcgi_t* fcgi)
{
	if (!fcgi->last_chunk_sent)
	{
		fcgi->last_chunk_sent = 1;

		if (!fcgi->ever_attempted_to_write_to_client)
		{
			if (fcgi_send_final_status_to_client(fcgi, HIO_HTTP_STATUS_INTERNAL_SERVER_ERROR, 0) <= -1) return -1;
		}
		else
		{
			if (fcgi->res_mode_to_cli == FCGI_RES_MODE_CHUNKED &&
				fcgi_write_to_client(fcgi, "0\r\n\r\n", 5) <= -1) return -1;
		}

		if (!fcgi->keep_alive && fcgi_write_to_client(fcgi, HIO_NULL, 0) <= -1) return -1;
	}
	return 0;
}

static int fcgi_write_stdin_to_peer (fcgi_t* fcgi, const void* data, hio_iolen_t dlen)
{
	if (fcgi->peer)
	{
		fcgi->num_pending_writes_to_peer++;
		if (hio_svc_fcgic_writestdin(fcgi->peer, data, dlen) <= -1) /* TODO: write STDIN, PARAM? */
		{
			fcgi->num_pending_writes_to_peer--;
			return -1;
		}

#if 0
	/* TODO: check if it's already finished or something.. */
		if (fcgi->num_pending_writes_to_peer > FCGI_PENDING_IO_THRESHOLD_TO_PEER)
		{
			/* disable input watching */
			if (hio_dev_sck_read(fcgi->csck, 0) <= -1) return -1;
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

	HIO_DEBUG8 (hio, "HTTS(%p) - fcgi(t=%p,c=%p[%d],p=%p) - old_over=%x | new-bits=%x => over=%x\n", fcgi->htts, fcgi, fcgi->client, (fcgi->csck? fcgi->csck->hnd: -1), fcgi->peer, (int)old_over, (int)over_bits, (int)fcgi->over);

	if (!(old_over & FCGI_OVER_READ_FROM_CLIENT) && (fcgi->over & FCGI_OVER_READ_FROM_CLIENT))
	{
		if (fcgi->csck && hio_dev_sck_read(fcgi->csck, 0) <= -1)
		{
			HIO_DEBUG2 (fcgi->htts->hio, "HTTS(%p) - halting client(%p) for failure to disable input watching\n", fcgi->htts, fcgi->csck);
			hio_dev_sck_halt (fcgi->csck);
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
		if (fcgi->peer)
		{
			hio_svc_fcgic_untie (fcgi->peer);
			HIO_SVC_HTTS_TASK_RCDOWN((hio_svc_htts_task_t*)fcgi); /* ref down from fcgi->peer->ctx. unable to use UNREF() */
		}

		if (fcgi->csck)
		{
			if (fcgi->keep_alive && !fcgi->client_eof_detected)
			{
/* how to arrange to delete this fcgi object and put the socket back to the normal waiting state??? */
				HIO_ASSERT (fcgi->htts->hio, fcgi->client->task == (hio_svc_htts_task_t*)fcgi);
				unbind_task_from_client (fcgi, 1);

				/* fcgi must not be accessed from here down as it could have been destroyed in unbind_task_from_client() */
			}
			else
			{
				HIO_DEBUG2 (hio, "HTTS(%p) - halting client(%p) for no keep-alive\n", fcgi->htts, fcgi->csck);
				hio_dev_sck_shutdown (fcgi->csck, HIO_DEV_SCK_SHUTDOWN_WRITE);
				hio_dev_sck_halt (fcgi->csck);
			}
		}
	}
}

static void fcgi_on_kill (hio_svc_htts_task_t* task)
{
	fcgi_t* fcgi = (fcgi_t*)task;
	hio_t* hio = fcgi->htts->hio;

	HIO_DEBUG5 (hio, "HTTS(%p) - fcgi(t=%p,c=%p[%d],p=%p) - killing the task\n", fcgi->htts, fcgi, fcgi->client, (fcgi->csck? fcgi->csck->hnd: -1), fcgi->peer);

	/* [NOTE] 
	 * 1. if hio_svc_htts_task_kill() is called, fcgi->peer, fcgi->peer_htrd, fcgi->csck, 
	 *    fcgi->client may not not null. 
	 * 2. this call-back function doesn't decrement the reference count on fcgi because 
	 *     this is the fcgi destruction call-back.
	 */

	if (fcgi->peer) hio_svc_fcgic_untie (fcgi->peer);

	if (fcgi->peer_htrd)
	{
		fcgi_peer_xtn_t* pxtn = hio_htrd_getxtn(fcgi->peer_htrd);
		pxtn->fcgi = HIO_NULL; 

		hio_htrd_close (fcgi->peer_htrd);
		fcgi->peer_htrd = HIO_NULL;
	}

	if (fcgi->csck)
	{
		HIO_ASSERT (hio, fcgi->client != HIO_NULL);
		unbind_task_from_client (fcgi, 0);
	}

	/* detach from the htts service only if it's attached */
	if (fcgi->task_next) HIO_SVC_HTTS_TASKL_UNLINK_TASK (fcgi); 

	HIO_DEBUG5 (hio, "HTTS(%p) - fcgi(t=%p,c=%p[%d],p=%p) - killed the task\n", fcgi->htts, fcgi, fcgi->client, (fcgi->csck? fcgi->csck->hnd: -1), fcgi->peer);
}

static void fcgi_peer_on_untie (hio_svc_fcgic_sess_t* peer, void* ctx)
{
	fcgi_t* fcgi = (fcgi_t*)ctx;

	/* in case this untie event originates from the fcgi client itself.
	 * fcgi_halt_participating_devices() calls hio_svc_fcgi_untie() again 
	 * to cause an infinite loop if we don't reset fcgi->peer to HIO_NULL here */
	fcgi->peer = HIO_NULL; 
	fcgi_write_last_chunk_to_client (fcgi);
	fcgi_halt_participating_devices (fcgi); /* TODO: kill the session only??? */
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
			n = fcgi_write_last_chunk_to_client(fcgi);
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

			if (!fcgi->ever_attempted_to_write_to_client &&
			    !(fcgi->over & FCGI_OVER_WRITE_TO_CLIENT))
			{
				fcgi_send_final_status_to_client (fcgi, HIO_HTTP_STATUS_INTERNAL_SERVER_ERROR, 1); /* don't care about error because it jumps to oops below anyway */
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
	fcgi_peer_xtn_t* peer = hio_htrd_getxtn(htrd);
	fcgi_t* fcgi = peer->fcgi;
	hio_svc_htts_cli_t* cli = fcgi->client;
	hio_bch_t dtbuf[64];
	int status_code = HIO_HTTP_STATUS_OK;

	if (req->attr.content_length)
	{
// TOOD: remove content_length if content_length is negative or not numeric.
		fcgi->res_mode_to_cli = FCGI_RES_MODE_LENGTH;
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
		fcgi->req_version.major, fcgi->req_version.minor,
		status_code, hio_http_status_to_bcstr(status_code),
		cli->htts->server_name, dtbuf) == (hio_oow_t)-1) return -1;

	if (hio_htre_walkheaders(req, peer_capture_response_header, cli) <= -1) return -1;

	switch (fcgi->res_mode_to_cli)
	{
		case FCGI_RES_MODE_CHUNKED:
			if (hio_becs_cat(cli->sbuf, "Transfer-Encoding: chunked\r\n") == (hio_oow_t)-1) return -1;
			/*if (hio_becs_cat(cli->sbuf, "Connection: keep-alive\r\n") == (hio_oow_t)-1) return -1;*/
			break;

		case FCGI_RES_MODE_CLOSE:
			if (hio_becs_cat(cli->sbuf, "Connection: close\r\n") == (hio_oow_t)-1) return -1;
			break;

		case FCGI_RES_MODE_LENGTH:
			if (hio_becs_cat(cli->sbuf, (fcgi->keep_alive? "Connection: keep-alive\r\n": "Connection: close\r\n")) == (hio_oow_t)-1) return -1;
	}

	if (hio_becs_cat(cli->sbuf, "\r\n") == (hio_oow_t)-1) return -1;

	return fcgi_write_to_client(fcgi, HIO_BECS_PTR(cli->sbuf), HIO_BECS_LEN(cli->sbuf));
}

static int peer_htrd_poke (hio_htrd_t* htrd, hio_htre_t* req)
{
	fcgi_peer_xtn_t* peer = hio_htrd_getxtn(htrd);
	fcgi_t* fcgi = peer->fcgi;

	if (fcgi_write_last_chunk_to_client(fcgi) <= -1) return -1;

	fcgi_mark_over (fcgi, FCGI_OVER_READ_FROM_PEER);
	return 0;
}

static int peer_htrd_push_content (hio_htrd_t* htrd, hio_htre_t* req, const hio_bch_t* data, hio_oow_t dlen)
{
	fcgi_peer_xtn_t* peer = hio_htrd_getxtn(htrd);
	fcgi_t* fcgi = peer->fcgi;

	HIO_ASSERT (fcgi->htts->hio, htrd == fcgi->peer_htrd);

	switch (fcgi->res_mode_to_cli)
	{
		case FCGI_RES_MODE_CHUNKED:
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

			if (fcgi_writev_to_client(fcgi, iov, HIO_COUNTOF(iov)) <= -1) goto oops;
			break;
		}

		case FCGI_RES_MODE_CLOSE:
		case FCGI_RES_MODE_LENGTH:
			if (fcgi_write_to_client(fcgi, data, dlen) <= -1) goto oops;
			break;
	}

#if 0
	if (fcgi->num_pending_writes_to_client > FCGI_PENDING_IO_THRESHOLD_TO_CLIENT)
	{
		if (hio_svc_fcgic_read(fcgi->peer, 0) <= -1) goto oops;
	}
#endif

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

	HIO_DEBUG4 (hio, "HTTS(%p) - fcgi(t=%p,c=%p,csck=%p) - client socket disconnect handled\n", htts, fcgi, cli, sck);

	/* fcgi may be null if there is no associated task or 
	 * the previously associated one is already gone */
	if (fcgi)
	{
		HIO_ASSERT (hio, sck == fcgi->csck);

		/* set fcgi->client_disconnect to 1 before unbind_task_from_client() 
		 * because fcgi can be destroyed if its reference count hits 0 */
		fcgi->client_disconnected = 1; 
		unbind_task_from_client (fcgi, 1);

		/* this is the original callback restored in unbind_task_from_client() */
		if (sck->on_disconnect) sck->on_disconnect (sck);
	}

	HIO_DEBUG4 (hio, "HTTS(%p) - fcgi(t=%p,c=%p,csck=%p) - client socket disconnect handled\n", htts, fcgi, cli, sck);
	/* Note: after this callback, the actual device pointed to by 'sck' will be freed in the main loop. */
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

	if (!fcgi->peer)
	{
		/* the peer is gone */
		goto oops; /* do what?  just return 0? */
	}

	if (len == 0)
	{
		/* EOF on the client side. arrange to close */
		HIO_DEBUG3 (hio, "HTTS(%p) - EOF from client %p(hnd=%d)\n", fcgi->htts, sck, (int)sck->hnd);
		fcgi->client_eof_detected = 1;

		if (!(fcgi->over & FCGI_OVER_READ_FROM_CLIENT)) /* if this is true, EOF is received without fcgi_client_htrd_poke() */
		{
			/* indicate eof to the write side */
			int n;
			n = fcgi_write_stdin_to_peer(fcgi, HIO_NULL, 0);
			fcgi_mark_over (fcgi, FCGI_OVER_READ_FROM_CLIENT);
			if (n <= -1) goto oops;
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

		fcgi->num_pending_writes_to_client--;
		if (fcgi->peer && fcgi->num_pending_writes_to_client == FCGI_PENDING_IO_THRESHOLD_TO_CLIENT)
		{
		#if 0 // TODO
			if (!(fcgi->over & FCGI_OVER_READ_FROM_PEER) &&
			    hio_svc_fcgic_read(fcgi->peer, 1) <= -1) goto oops;
		#endif
		}

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

static void bind_task_to_client (fcgi_t* fcgi, hio_dev_sck_t* csck)
{
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(csck);

	HIO_ASSERT (fcgi->htts->hio, cli->sck == csck);
	HIO_ASSERT (fcgi->htts->hio, cli->task == HIO_NULL);

	fcgi->csck = csck;
	fcgi->client = cli;

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
	hio_dev_sck_t* csck = fcgi->csck;

	HIO_ASSERT (fcgi->htts->hio, fcgi->client != HIO_NULL);
	HIO_ASSERT (fcgi->htts->hio, fcgi->csck != HIO_NULL);
	HIO_ASSERT (fcgi->htts->hio, fcgi->client->task == (hio_svc_htts_task_t*)fcgi);
	HIO_ASSERT (fcgi->htts->hio, fcgi->client->htrd != HIO_NULL);

	if (fcgi->client_htrd_recbs_changed) 
	{
		hio_htrd_setrecbs (fcgi->client->htrd, &fcgi->client_htrd_org_recbs);
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
	 * because it can destroy the fcgi itself. so reset fcgi->client->task
	 * to null and call RCDOWN() later */
	fcgi->client->task = HIO_NULL;
	fcgi->client = HIO_NULL;
	fcgi->csck = HIO_NULL;

	if (!fcgi->client_disconnected && (!fcgi->keep_alive || hio_dev_sck_read(csck, 1) <= -1))
	{
		HIO_DEBUG2 (fcgi->htts->hio, "HTTS(%p) - halting client(%p) for failure to enable input watching\n", fcgi->htts, csck);
		hio_dev_sck_halt (csck);
	}

	if (rcdown) HIO_SVC_HTTS_TASK_RCDOWN ((hio_svc_htts_task_t*)fcgi);
}

static int bind_task_to_peer (fcgi_t* fcgi, const hio_skad_t* fcgis_addr)
{
	hio_htrd_t* htrd;
	fcgi_peer_xtn_t* pxtn;

	htrd = hio_htrd_open(fcgi->htts->hio, HIO_SIZEOF(*pxtn));
	if (HIO_UNLIKELY(!htrd)) return -1;

	hio_htrd_setoption (htrd, HIO_HTRD_SKIP_INITIAL_LINE | HIO_HTRD_RESPONSE);
	hio_htrd_setrecbs (htrd, &peer_htrd_recbs);

	fcgi->peer = hio_svc_fcgic_tie(fcgi->htts->fcgic, fcgis_addr, fcgi_peer_on_read, fcgi_peer_on_untie, fcgi);
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
 		/* hio-svc_fcgic_untie() is not a delayed operation unlike hio_dev_sck_halt(). 
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

static int setup_for_expect100 (fcgi_t* fcgi, hio_htre_t* req, int options)
{
#if !defined(FCGI_ALLOW_UNLIMITED_REQ_CONTENT_LENGTH)
	if (fcgi->req_content_length_unlimited)
	{
		/* Transfer-Encoding is chunked. no content-length is known in advance. */
		/* option 1. buffer contents. if it gets too large, send 413 Request Entity Too Large.
		 * option 2. send 411 Length Required immediately
		 * option 3. set Content-Length to -1 and use EOF to indicate the end of content [Non-Standard] */
		if (fcgi_send_final_status_to_client(fcgi, HIO_HTTP_STATUS_LENGTH_REQUIRED, 1) <= -1) return -1;
	}
#endif

	if (req->flags & HIO_HTRE_ATTR_EXPECT100)
	{
		/* TODO: Expect: 100-continue? who should handle this? fcgi? or the http server? */
		/* CAN I LET the fcgi SCRIPT handle this? */
		if (!(options & HIO_SVC_HTTS_FCGI_NO_100_CONTINUE) &&
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

			msglen = hio_fmttobcstr(fcgi->htts->hio, msgbuf, HIO_COUNTOF(msgbuf), "HTTP/%d.%d %d %hs\r\n\r\n", fcgi->req_version.major, fcgi->req_version.minor, HIO_HTTP_STATUS_CONTINUE, hio_http_status_to_bcstr(HIO_HTTP_STATUS_CONTINUE));
			if (fcgi_write_to_client(fcgi, msgbuf, msglen) <= -1) return -1;
			fcgi->ever_attempted_to_write_to_client = 0; /* reset this as it's polluted for 100 continue */
		}
	}
	else if (req->flags & HIO_HTRE_ATTR_EXPECT)
	{
		/* 417 Expectation Failed */
		fcgi_send_final_status_to_client(fcgi, HIO_HTTP_STATUS_EXPECTATION_FAILED, 1);
		return -1;
	}

	return 0;
}

static int setup_for_content_length(fcgi_t* fcgi, hio_htre_t* req)
{
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
			if (fcgi_write_stdin_to_peer(fcgi, HIO_NULL, 0) <= -1) return -1;
			fcgi_mark_over (fcgi, FCGI_OVER_READ_FROM_CLIENT | FCGI_OVER_WRITE_TO_PEER);
		}
#if defined(FCGI_ALLOW_UNLIMITED_REQ_CONTENT_LENGTH)
	}
#endif

	/* this may change later if Content-Length is included in the fcgi output */
	if (req->flags & HIO_HTRE_ATTR_KEEPALIVE)
	{
		fcgi->keep_alive = 1;
		fcgi->res_mode_to_cli = FCGI_RES_MODE_CHUNKED;
		/* the mode still can get switched to FCGI_RES_MODE_LENGTH if the fcgi script emits Content-Length */
	}
	else
	{
		fcgi->keep_alive = 0;
		fcgi->res_mode_to_cli = FCGI_RES_MODE_CLOSE;
	}

	return 0;
}
int hio_svc_htts_dofcgi (hio_svc_htts_t* htts, hio_dev_sck_t* csck, hio_htre_t* req, const hio_skad_t* fcgis_addr, const hio_bch_t* docroot, const hio_bch_t* script, int options)
{
	hio_t* hio = htts->hio;
	fcgi_t* fcgi = HIO_NULL;

	/* ensure that you call this function before any contents is received */
	HIO_ASSERT (hio, hio_htre_getcontentlen(req) == 0);

	if (HIO_UNLIKELY(!htts->fcgic))
	{
		hio_seterrbfmt (hio, HIO_ENOCAPA, "fcgi client service not enabled");
		goto oops;
	}

	fcgi = (fcgi_t*)hio_svc_htts_task_make(htts, HIO_SIZEOF(*fcgi), fcgi_on_kill);
	if (HIO_UNLIKELY(!fcgi)) goto oops;

	/*fcgi->num_pending_writes_to_client = 0;
	fcgi->num_pending_writes_to_peer = 0;*/
	fcgi->req_method = hio_htre_getqmethodtype(req);
	fcgi->req_version = *hio_htre_getversion(req);
	fcgi->req_content_length_unlimited = hio_htre_getreqcontentlen(req, &fcgi->req_content_length);

	bind_task_to_client (fcgi, csck);
	if (bind_task_to_peer(fcgi, fcgis_addr) <= -1) goto oops;

	if (setup_for_expect100(fcgi, req, options) <= -1) goto oops;
	if (setup_for_content_length(fcgi, req) <= -1) goto oops;

	/* TODO: store current input watching state and use it when destroying the fcgi data */
	if (hio_dev_sck_read(csck, !(fcgi->over & FCGI_OVER_READ_FROM_CLIENT)) <= -1) goto oops;

	/* send FCGI_BEGIN_REQUEST */
	if (hio_svc_fcgic_beginrequest(fcgi->peer) <= -1) goto oops;
	/* write FCGI_PARAM */
	if (write_params(fcgi, csck, req, docroot, script) <= -1) goto oops;
	if (hio_svc_fcgic_writeparam(fcgi->peer, HIO_NULL, 0, HIO_NULL, 0) <= -1) goto oops; /* end of params */

	HIO_SVC_HTTS_TASKL_APPEND_TASK (&htts->task, (hio_svc_htts_task_t*)fcgi);
	return 0;

oops:
	HIO_DEBUG2 (hio, "HTTS(%p) - FAILURE in dofcgi - socket(%p)\n", htts, csck);
	if (fcgi) fcgi_halt_participating_devices (fcgi);
	return -1;
}
