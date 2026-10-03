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


#include "https-prv.h"
#include <hio-ws.h>
#include <hio-sha1.h>
#include <hio-utl.h>
#include <hio-str.h>

/*
 * the websocket half of the http server.
 *
 * the handshake is an ordinary http request, so it arrives through the same
 * door every other task does. what is different is what happens after the
 * answer: the connection stops being http. the reader is switched to raw mode
 * and this task owns every octet that follows until the connection ends.
 *
 * the framing itself lives in ws-msg.c, which knows nothing of sockets. what
 * is here is everything that depends on the conversation so far - which
 * message is in progress, what was negotiated, who owes whom a pong.
 */

/* the value appended to the client's key before hashing, fixed by RFC 6455
 * section 1.3. it is not a secret; it is there so that a server which merely
 * echoes headers cannot appear to have understood the handshake. */
#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

/* the only version of the protocol this speaks */
#define WS_VERSION 13

/* a client's key is sixteen octets in base64 */
#define WS_KEY_LEN 16

/* once this many writes are outstanding, stop reading until they drain. a
 * peer that sends faster than it reads would otherwise grow the write queue
 * without bound. */
#define WS_MAX_PENDING_WRITES 64

struct ws_t
{
	HIO_SVC_HTTPS_TASK_HEADER;

	hio_svc_https_task_on_kill_t on_kill; /* user-provided on_kill callback */
	hio_svc_https_ws_cbs_t cbs;
	void* ctx;

	/* -- the frame being read -------------------------------------- */

	/* a header is up to fourteen octets and the network may deliver it one at
	 * a time, so it is gathered here until there is enough of it to parse */
	hio_uint8_t hdrbuf[HIO_WS_FRAME_HDR_MAX_LEN];
	hio_oow_t   hdrlen;

	hio_ws_frame_hdr_t fhdr;
	hio_oow_t   plen_left; /* octets of this frame's payload still to come */
	hio_oow_t   mask_off;  /* how far into the payload the mask key has been carried */

	/* a control frame is answered as a whole, and it is small enough by rule
	 * that holding it costs nothing */
	hio_uint8_t ctlbuf[HIO_WS_CONTROL_MAX_PLEN];
	hio_oow_t   ctllen;

	/* -- the message being read ------------------------------------ */

	int         msg_opcode; /* what the message in progress started as, 0 if none */
	hio_oow_t   msg_len;    /* how much of it has been seen */
	hio_oow_t   max_msg_size;

	/* -- keepalive -------------------------------------------------- */

	hio_tmridx_t tmridx;
	hio_ntime_t  ping_interval;
	hio_ntime_t  pong_timeout;

	unsigned int in_payload: 1;     /* the header is complete and its payload is being read */
	unsigned int msg_started: 1;    /* a piece of the message in progress has been handed over */
	unsigned int ping_pending: 1;   /* a ping is out and its pong has not come back */
	unsigned int close_sent: 1;
	unsigned int close_received: 1;
	unsigned int opened: 1;         /* on_open has been called */
	unsigned int reading_paused: 1; /* input was turned off to let writes drain */
};
typedef struct ws_t ws_t;

static void ws_schedule_ping (ws_t* ws, int after_ping);

/* ----------------------------------------------------------------------- */

static void ws_halt_participating_devices (ws_t* ws)
{
	hio_svc_https_task_haltclient((hio_svc_https_task_t*)ws);
}

/* the session is over. the application is told once, with whatever the peer
 * said on its way out, or that it said nothing. */
static void ws_report_close (ws_t* ws, int code)
{
	if (ws->opened && ws->cbs.on_close)
	{
		ws->opened = 0; /* once only, however the session ends */
		ws->cbs.on_close((hio_svc_https_ws_t*)ws, code);
	}
}

static void ws_unschedule_ping (ws_t* ws)
{
	if (ws->tmridx != HIO_TMRIDX_INVALID)
	{
		hio_deltmrjob (ws->https->hio, ws->tmridx);
		ws->tmridx = HIO_TMRIDX_INVALID;
	}
}

/* ----------------------------------------------------------------------- */
/* writing                                                                  */
/* ----------------------------------------------------------------------- */

/* a server never masks what it sends, so a frame is its header followed by
 * the payload untouched - two writes rather than a copy of the whole thing */
static int ws_send_frame (ws_t* ws, int fin, int opcode, const void* ptr, hio_oow_t len)
{
	hio_uint8_t hdr[HIO_WS_FRAME_HDR_MAX_LEN];
	int hlen;

	if (!ws->task_csck) return -1; /* the connection is already gone */

	hlen = hio_ws_build_frame_hdr(hdr, HIO_SIZEOF(hdr), fin, opcode, HIO_NULL, len);
	if (hlen <= -1)
	{
		hio_seterrbfmt(ws->https->hio, HIO_EINVAL, "unable to frame a websocket message");
		return -1;
	}

	if (hio_svc_https_task_addresbody((hio_svc_https_task_t*)ws, hdr, hlen) <= -1) return -1;
	if (len > 0 && hio_svc_https_task_addresbody((hio_svc_https_task_t*)ws, ptr, len) <= -1) return -1;

	/* a peer that sends faster than it reads would grow the write queue
	 * without bound. stop taking its frames until what is queued goes out. */
	if (!ws->reading_paused && ws->task_res_pending_writes >= WS_MAX_PENDING_WRITES)
	{
		ws->reading_paused = 1;
		hio_svc_https_task_stopreadingclient((hio_svc_https_task_t*)ws);
	}

	return 0;
}

int hio_svc_https_ws_writeframe (hio_svc_https_ws_t* ws_, int fin, int opcode, const void* ptr, hio_oow_t len)
{
	ws_t* ws = (ws_t*)ws_;

	/* once a close has gone out nothing more may follow it */
	if (ws->close_sent)
	{
		hio_seterrbfmt(ws->https->hio, HIO_EPERM, "websocket is closing");
		return -1;
	}

	return ws_send_frame(ws, fin, opcode, ptr, len);
}

int hio_svc_https_ws_write (hio_svc_https_ws_t* ws, int opcode, const void* ptr, hio_oow_t len)
{
	return hio_svc_https_ws_writeframe(ws, 1, opcode, ptr, len);
}

int hio_svc_https_ws_close (hio_svc_https_ws_t* ws_, int code, const hio_bch_t* reason)
{
	ws_t* ws = (ws_t*)ws_;
	hio_uint8_t body[HIO_WS_CONTROL_MAX_PLEN];
	int blen;

	if (ws->close_sent) return 0; /* already said */

	blen = hio_ws_build_close_payload(body, HIO_SIZEOF(body), code, reason);
	if (blen <= -1)
	{
		hio_seterrbfmt(ws->https->hio, HIO_EINVAL, "invalid websocket close code or reason");
		return -1;
	}

	if (ws_send_frame(ws, 1, HIO_WS_OPCODE_CLOSE, body, blen) <= -1) return -1;
	ws->close_sent = 1;

	/* the closing handshake is a handshake: the peer is owed the chance to
	 * answer, and only then is the connection finished with. if the peer has
	 * already said its part, nothing is being waited for. */
	if (ws->close_received) hio_svc_https_task_finishclient((hio_svc_https_task_t*)ws);

	return 0;
}

void* hio_svc_https_ws_getctx (hio_svc_https_ws_t* ws)
{
	return ((ws_t*)ws)->ctx;
}

hio_t* hio_svc_https_ws_gethio (hio_svc_https_ws_t* ws)
{
	return ((ws_t*)ws)->https->hio;
}

/* ----------------------------------------------------------------------- */
/* the keepalive timer                                                      */
/* ----------------------------------------------------------------------- */

static void ws_on_ping_tmout (hio_t* hio, const hio_ntime_t* now, hio_tmrjob_t* job)
{
	ws_t* ws = (ws_t*)job->ctx;

	ws->tmridx = HIO_TMRIDX_INVALID;

	if (ws->ping_pending)
	{
		/* the ping went unanswered for the whole grace period. nothing else
		 * would ever notice - a websocket connection is exempt from the http
		 * deadlines for as long as it has a task bound to it. */
		HIO_DEBUG2(hio, "HTTPS(%p) - websocket client(%p) did not answer a ping\n", ws->https, ws->task_csck);
		ws_halt_participating_devices(ws);
		return;
	}

	if (ws_send_frame(ws, 1, HIO_WS_OPCODE_PING, HIO_NULL, 0) <= -1)
	{
		ws_halt_participating_devices(ws);
		return;
	}

	ws->ping_pending = 1;
	ws_schedule_ping(ws, 1);
}

/* 'after_ping' picks which of the two waits this is: the quiet period before
 * a ping is due, or the grace period for its answer */
static void ws_schedule_ping (ws_t* ws, int after_ping)
{
	hio_t* hio = ws->https->hio;
	hio_tmrjob_t tmrjob;
	const hio_ntime_t* delay;

	delay = after_ping? &ws->pong_timeout: &ws->ping_interval;
	if (HIO_IS_NEG_NTIME(delay) || (delay->sec == 0 && delay->nsec == 0)) return; /* not wanted */

	ws_unschedule_ping(ws);

	HIO_MEMSET(&tmrjob, 0, HIO_SIZEOF(tmrjob));
	hio_gettime(hio, &tmrjob.when);
	HIO_ADD_NTIME(&tmrjob.when, &tmrjob.when, delay);
	tmrjob.ctx = ws;
	tmrjob.handler = ws_on_ping_tmout;
	tmrjob.idxptr = &ws->tmridx;

	ws->tmridx = hio_instmrjob(hio, &tmrjob);
	if (ws->tmridx == HIO_TMRIDX_INVALID)
	{
		HIO_DEBUG1(hio, "HTTPS(%p) - unable to schedule a websocket ping. carrying on without one\n", ws->https);
	}
}

/* ----------------------------------------------------------------------- */
/* reading                                                                  */
/* ----------------------------------------------------------------------- */

/* the connection cannot continue. the peer is told why and then nothing more
 * is read from it - but the close frame is left queued rather than the socket
 * dropped, so the peer learns the reason instead of seeing the line go dead. */
static int ws_fail (ws_t* ws, int code)
{
	HIO_DEBUG3 (ws->https->hio, "HTTPS(%p) - websocket client(%p) protocol failure, closing with %d\n", ws->https, ws->task_csck, code);

	if (!ws->close_sent) hio_svc_https_ws_close((hio_svc_https_ws_t*)ws, code, HIO_NULL);
	ws_report_close(ws, code);

	/* whatever else is in the buffer was written before the peer could know,
	 * and none of it can be trusted now that the framing is in question */
	ws->close_received = 1;
	hio_svc_https_task_stopreadingclient((hio_svc_https_task_t*)ws);
	return 0;
}

/* a control frame has arrived whole */
static int ws_handle_control (ws_t* ws)
{
	switch (ws->fhdr.opcode)
	{
		case HIO_WS_OPCODE_PING:
			/* the body of a ping comes back unchanged in the pong */
			return ws_send_frame(ws, 1, HIO_WS_OPCODE_PONG, ws->ctlbuf, ws->ctllen);

		case HIO_WS_OPCODE_PONG:
			/* solicited or not, it says the peer is answering */
			ws->ping_pending = 0;
			ws_schedule_ping(ws, 0);
			return 0;

		case HIO_WS_OPCODE_CLOSE:
		{
			int code = HIO_WS_STATUS_NO_STATUS;

			if (hio_ws_parse_close_payload(ws->ctlbuf, ws->ctllen, &code, HIO_NULL, HIO_NULL) <= -1)
				return ws_fail(ws, HIO_WS_STATUS_PROTOCOL_ERROR);

			ws->close_received = 1;
			ws_report_close(ws, code);

			/* the closing handshake is answered in kind, with the code the
			 * peer used - unless a close was already sent, in which case this
			 * is the answer to it and the exchange is complete */
			if (!ws->close_sent)
			{
				hio_svc_https_ws_close((hio_svc_https_ws_t*)ws, code == HIO_WS_STATUS_NO_STATUS? HIO_WS_STATUS_NORMAL: code, HIO_NULL);
			}

			hio_svc_https_task_stopreadingclient((hio_svc_https_task_t*)ws);
			return 0;
		}
	}

	return 0;
}

/* the header of the frame about to be read has just been parsed. what the
 * codec cannot judge is checked here, because it depends on what was
 * negotiated and on which message is in progress. */
static int ws_accept_frame_hdr (ws_t* ws)
{
	/* the reserved bits mean whatever an extension says they mean, and no
	 * extension was negotiated */
	if (ws->fhdr.rsv != 0) return ws_fail(ws, HIO_WS_STATUS_PROTOCOL_ERROR);

	/* every frame from a client is masked. a server that accepts an unmasked
	 * one lets a peer place chosen bytes on the wire, which is what the
	 * masking is there to prevent. */
	if (!ws->fhdr.masked) return ws_fail(ws, HIO_WS_STATUS_PROTOCOL_ERROR);

	if (!HIO_WS_OPCODE_IS_CONTROL(ws->fhdr.opcode))
	{
		if (ws->fhdr.opcode == HIO_WS_OPCODE_CONT)
		{
			/* a continuation with nothing to continue */
			if (ws->msg_opcode == 0) return ws_fail(ws, HIO_WS_STATUS_PROTOCOL_ERROR);
		}
		else
		{
			/* a new message while one is still unfinished. only control
			 * frames may appear in that gap. */
			if (ws->msg_opcode != 0) return ws_fail(ws, HIO_WS_STATUS_PROTOCOL_ERROR);
			ws->msg_opcode = ws->fhdr.opcode;
			ws->msg_len = 0;
			ws->msg_started = 0;
		}

		if (ws->max_msg_size > 0 &&
		    (ws->fhdr.plen > ws->max_msg_size || ws->msg_len > ws->max_msg_size - ws->fhdr.plen))
		{
			return ws_fail(ws, HIO_WS_STATUS_MESSAGE_TOO_BIG);
		}
	}

	return 0;
}

static int ws_client_htrd_push_content (hio_htrd_t* htrd, hio_htre_t* req, const hio_bch_t* data, hio_oow_t dlen)
{
	hio_svc_https_cli_htrd_xtn_t* htrdxtn = (hio_svc_https_cli_htrd_xtn_t*)hio_htrd_getxtn(htrd);
	hio_dev_sck_t* sck = htrdxtn->sck;
	hio_svc_https_cli_t* cli = (hio_svc_https_cli_t*)hio_dev_sck_getxtn(sck);
	ws_t* ws = (ws_t*)cli->task;
	const hio_uint8_t* p = (const hio_uint8_t*)data;
	hio_oow_t rem = dlen;

	if (HIO_UNLIKELY(!ws)) return 0;

	while (rem > 0)
	{
		/* once the connection is finished with, what is still in the buffer
		 * was sent before the peer could know that and is not acted on */
		if (ws->close_received) return 0;

		if (!ws->in_payload)
		{
			hio_oow_t want = HIO_SIZEOF(ws->hdrbuf) - ws->hdrlen;
			hio_oow_t take = (rem < want)? rem: want;
			int n;

			HIO_MEMCPY (&ws->hdrbuf[ws->hdrlen], p, take);
			n = hio_ws_parse_frame_hdr(ws->hdrbuf, ws->hdrlen + take, &ws->fhdr);

			if (n == 0)
			{
				/* not a whole header yet. everything copied is kept and the
				 * rest of it is waited for. */
				ws->hdrlen += take;
				p += take;
				rem -= take;
				continue;
			}

			if (n <= -1) return ws_fail(ws, ws->fhdr.status);

			/* only the header's own octets are consumed - the payload begins
			 * where it ends, which may be inside what was just copied */
			p += ws->fhdr.hlen - ws->hdrlen;
			rem -= ws->fhdr.hlen - ws->hdrlen;
			ws->hdrlen = 0;

			if (ws_accept_frame_hdr(ws) <= -1 || ws->close_received) return 0;

			ws->in_payload = 1;
			ws->plen_left = ws->fhdr.plen;
			ws->mask_off = 0;
			ws->ctllen = 0;
		}

		if (ws->in_payload && ws->plen_left > 0)
		{
			/* the payload arrives masked and the buffer it arrives in belongs
			 * to the loop, so it is unmasked into scratch rather than in
			 * place. a fixed amount at a time keeps this off the heap however
			 * large the frame is. */
			hio_uint8_t scratch[2048];
			hio_oow_t avail = (rem < ws->plen_left)? rem: ws->plen_left;

			while (avail > 0)
			{
				hio_oow_t n = (avail > HIO_SIZEOF(scratch))? HIO_SIZEOF(scratch): avail;

				HIO_MEMCPY (scratch, p, n);
				ws->mask_off = hio_ws_mask(scratch, n, ws->fhdr.mask, ws->mask_off);

				p += n;
				rem -= n;
				avail -= n;
				ws->plen_left -= n;

				if (HIO_WS_OPCODE_IS_CONTROL(ws->fhdr.opcode))
				{
					/* held whole; the codec has already refused anything that
					 * would not fit */
					HIO_MEMCPY (&ws->ctlbuf[ws->ctllen], scratch, n);
					ws->ctllen += n;
				}
				else
				{
					ws->msg_len += n;
					if (ws->cbs.on_data)
					{
						/* what the message started as is reported once and
						 * continuation after that, which is the shape the
						 * frames themselves take and the shape these pieces
						 * would be written back in.
						 *
						 * the message ends where its last frame does, not
						 * where a piece of that frame happens to end. */
						int op = ws->msg_started? HIO_WS_OPCODE_CONT: ws->msg_opcode;
						int fin = (ws->fhdr.fin && ws->plen_left == 0);

						ws->msg_started = 1;
						if (ws->cbs.on_data((hio_svc_https_ws_t*)ws, op, fin, scratch, n) <= -1) return -1;
					}
				}
			}
		}

		if (ws->in_payload && ws->plen_left == 0)
		{
			ws->in_payload = 0;

			if (HIO_WS_OPCODE_IS_CONTROL(ws->fhdr.opcode))
			{
				if (ws_handle_control(ws) <= -1) return -1;
			}
			else
			{
				/* a frame with no payload still has to reach the application:
				 * an empty message is a message, and a fragmented one is
				 * commonly ended by an empty continuation frame carrying
				 * nothing but the fin bit. it is a continuation if anything
				 * of this message has already gone over, whatever the frame
				 * that started the message was. */
				if (ws->fhdr.plen == 0 && ws->cbs.on_data)
				{
					int op = ws->msg_started? HIO_WS_OPCODE_CONT: ws->msg_opcode;

					ws->msg_started = 1;
					if (ws->cbs.on_data((hio_svc_https_ws_t*)ws, op, ws->fhdr.fin, HIO_NULL, 0) <= -1) return -1;
				}
				if (ws->fhdr.fin)
				{
					ws->msg_opcode = 0;
					ws->msg_len = 0;
					ws->msg_started = 0;
				}
			}
		}
	}

	return 0;
}

/* ----------------------------------------------------------------------- */
/* the task's place on the client socket                                    */
/* ----------------------------------------------------------------------- */

static int ws_client_on_read (hio_dev_sck_t* sck, const void* buf, hio_iolen_t len, const hio_skad_t* srcaddr);
static int ws_client_on_write (hio_dev_sck_t* sck, hio_iolen_t wrlen, void* wrctx, const hio_skad_t* dstaddr);
static void ws_client_on_disconnect (hio_dev_sck_t* sck);

static hio_dev_sck_evcb_t ws_client_evcb =
{
	ws_client_on_read,
	ws_client_on_write,
	ws_client_on_disconnect
};

/* the reader is in raw mode from the handshake onward, so only push_content is
 * ever reached. peek is left as it was because it is called without a null
 * check, and would be reached again if the reader were ever taken out of raw
 * mode on this connection. */
static hio_htrd_recbs_t ws_client_htrd_recbs =
{
	HIO_NULL,
	HIO_NULL,
	ws_client_htrd_push_content
};

static void ws_on_kill (hio_svc_https_task_t* task)
{
	ws_t* ws = (ws_t*)task;
	hio_t* hio = ws->https->hio;

	HIO_DEBUG2(hio, "HTTPS(%p) - killing websocket client(%p)\n", ws->https, ws->task_csck);

	ws_unschedule_ping(ws);

	/* a session that ends without a closing handshake ends abnormally, and
	 * the application is told so rather than left to assume */
	ws_report_close(ws, HIO_WS_STATUS_ABNORMAL);

	if (ws->on_kill) ws->on_kill(task);

	if (ws->task_csck)
	{
		HIO_ASSERT(hio, ws->task_client != HIO_NULL);
		hio_svc_https_task_unbindfromclient((hio_svc_https_task_t*)ws, 0);
	}

	if (ws->task_next) HIO_SVC_HTTPS_TASKL_UNLINK_TASK (ws);
}

static void ws_client_on_disconnect (hio_dev_sck_t* sck)
{
	hio_svc_https_cli_t* cli = (hio_svc_https_cli_t*)hio_dev_sck_getxtn(sck);
	ws_t* ws = (ws_t*)cli->task;

	if (ws)
	{
		HIO_SVC_HTTPS_TASK_RCUP((hio_svc_https_task_t*)ws);
		hio_svc_https_task_unbindfromclient((hio_svc_https_task_t*)ws, 1);
		hio_svc_https_client_default_on_disconnect(sck);
		HIO_SVC_HTTPS_TASK_RCDOWN((hio_svc_https_task_t*)ws);
	}
}

static int ws_client_on_read (hio_dev_sck_t* sck, const void* buf, hio_iolen_t len, const hio_skad_t* srcaddr)
{
	hio_t* hio = sck->hio;
	hio_svc_https_cli_t* cli = (hio_svc_https_cli_t*)hio_dev_sck_getxtn(sck);
	ws_t* ws = (ws_t*)cli->task;
	int n;

	HIO_ASSERT(hio, sck == cli->sck);

	/* the octets go to the reader as every other task's do. it is in raw mode,
	 * so they arrive at ws_client_htrd_push_content() rather than being
	 * parsed as http - and end of file and read errors are recognised in the
	 * one place that recognises them for every task. */
	n = hio_svc_https_client_default_on_read(sck, buf, len, srcaddr);

	if (len <= 0)
	{
		/* the peer went away. if it did so without closing, the session ended
		 * abnormally and ws_on_kill() reports that. */
		HIO_DEBUG3(hio, "HTTPS(%p) - EOF or error from websocket client %p(hnd=%d)\n", ws->https, sck, (int)sck->hnd);
		goto oops;
	}

	if (n <= -1) goto oops;
	return 0;

oops:
	ws_halt_participating_devices(ws);
	return 0;
}

static int ws_client_on_write (hio_dev_sck_t* sck, hio_iolen_t wrlen, void* wrctx, const hio_skad_t* dstaddr)
{
	hio_svc_https_cli_t* cli = (hio_svc_https_cli_t*)hio_dev_sck_getxtn(sck);
	ws_t* ws = (ws_t*)cli->task;
	int n;

	n = hio_svc_https_client_default_on_write(sck, wrlen, wrctx, dstaddr);

	if (n <= -1 || wrlen <= -1)
	{
		ws_halt_participating_devices(ws);
		return 0;
	}

	if (ws->task_res_pending_writes <= 0)
	{
		/* everything queued has gone out. if a close was among it, the peer
		 * has now been told and there is nothing left to say. */
		if (ws->close_sent && ws->close_received)
		{
			hio_svc_https_task_finishclient((hio_svc_https_task_t*)ws);
			return 0;
		}

		/* the queue drained, so the peer's frames can be taken again */
		if (ws->reading_paused)
		{
			ws->reading_paused = 0;
			if (ws->task_csck && hio_dev_sck_read(ws->task_csck, 1) <= -1) ws_halt_participating_devices(ws);
		}
	}

	return 0;
}

/* ----------------------------------------------------------------------- */
/* the handshake                                                            */
/* ----------------------------------------------------------------------- */

/* a header may be given more than once, and a value may be a list, so each
 * one is searched in turn rather than only the first */
static int hdr_has_word (hio_htre_t* req, const hio_bch_t* name, const hio_bch_t* word)
{
	const hio_htre_hdrval_t* v;

	for (v = hio_htre_getheaderval(req, name); v; v = v->next)
	{
		if (hio_find_bcstr_word_in_bcstr(v->ptr, word, ',', 1)) return 1;
	}

	return 0;
}

/* returns 0 if this is a handshake this can answer, or the status to refuse
 * it with */
static int ws_check_request (hio_htre_t* req, const hio_bch_t** key)
{
	const hio_htre_hdrval_t* v;

	/* RFC 6455 section 4.2.1 - the method and version are fixed */
	if (hio_htre_getqmethodtype(req) != HIO_HTTP_GET) return HIO_HTTP_STATUS_BAD_REQUEST;
	if (hio_htre_getversion(req)->major < 1 ||
	    (hio_htre_getversion(req)->major == 1 && hio_htre_getversion(req)->minor < 1)) return HIO_HTTP_STATUS_BAD_REQUEST;

	if (!hdr_has_word(req, "Upgrade", "websocket")) return HIO_HTTP_STATUS_BAD_REQUEST;

	/* Connection is a list of tokens, and Upgrade is one of them rather than
	 * the whole of it - a browser sends 'keep-alive, Upgrade' */
	if (!hdr_has_word(req, "Connection", "Upgrade")) return HIO_HTTP_STATUS_BAD_REQUEST;

	v = hio_htre_getheaderval(req, "Sec-WebSocket-Version");
	if (!v || hio_comp_bcstr(v->ptr, "13", 0) != 0)
	{
		/* the answer names what this does speak, which is what lets a client
		 * that speaks another version know what to try */
		return 426; /* Upgrade Required */
	}

	v = hio_htre_getheaderval(req, "Sec-WebSocket-Key");
	if (!v) return HIO_HTTP_STATUS_BAD_REQUEST;
	*key = v->ptr;

	return 0;
}

/* the value that proves the handshake was understood: the client's key, as
 * written, concatenated with a fixed string, hashed, and encoded. the key is
 * used as the text it arrived as - decoding it first is the classic way to
 * get this wrong. */
static int ws_compute_accept (const hio_bch_t* key, hio_bch_t* buf, hio_oow_t capa)
{
	hio_sha1_ctx_t sha;
	hio_uint8_t digest[HIO_SHA1_DIGEST_LEN];
	hio_oow_t dlen = HIO_SIZEOF(digest), blen = capa;

	hio_sha1_init(&sha);
	hio_sha1_update(&sha, key, hio_count_bcstr(key));
	hio_sha1_update(&sha, WS_GUID, hio_count_bcstr(WS_GUID));
	hio_sha1_final(&sha, digest);

	if (hio_conv_bin_to_base64(digest, &dlen, buf, &blen, 0) <= -1) return -1;
	buf[blen] = '\0';
	return 0;
}

/* ----------------------------------------------------------------------- */

int hio_svc_https_dows (hio_svc_https_t* https, hio_dev_sck_t* csck, hio_htre_t* req, const hio_svc_https_ws_opt_t* opt, const hio_svc_https_ws_cbs_t* cbs, void* ctx, hio_svc_https_task_on_kill_t on_kill)
{
	hio_t* hio = https->hio;
	hio_svc_https_cli_t* cli = (hio_svc_https_cli_t*)hio_dev_sck_getxtn(csck);
	ws_t* ws = HIO_NULL;
	const hio_bch_t* key = HIO_NULL;
	hio_bch_t accept[64];
	int status_code;
	int bound_to_client = 0;

	HIO_ASSERT(hio, cli->sck == csck);

	if (cli->task)
	{
		hio_seterrbfmt(hio, HIO_EPERM, "duplicate task request prohibited");
		return -1;
	}

	/* whether this is a handshake at all is decided before anything is
	 * allocated, so a request that is not one costs only the answer */
	status_code = ws_check_request(req, &key);

	ws = (ws_t*)hio_svc_https_task_make(https, HIO_SIZEOF(*ws), ws_on_kill, req, csck);
	if (HIO_UNLIKELY(!ws)) goto oops;
	HIO_SVC_HTTPS_TASK_RCUP((hio_svc_https_task_t*)ws);

	ws->tmridx = HIO_TMRIDX_INVALID;
	if (cbs) ws->cbs = *cbs;
	ws->ctx = ctx;
	if (opt)
	{
		ws->max_msg_size = opt->max_msg_size;
		ws->ping_interval = opt->ping_interval;
		ws->pong_timeout = opt->pong_timeout;
	}

	hio_svc_https_task_bindtoclient((hio_svc_https_task_t*)ws, csck, &ws_client_evcb);
	bound_to_client = 1;

	if (status_code != 0)
	{
		/* not a handshake this can answer. the version header on a 426 is
		 * what tells the client which one to come back with. */
		if (status_code == 426)
		{
			if (hio_svc_https_task_startreshdr((hio_svc_https_task_t*)ws, status_code, "Upgrade Required", 0) <= -1 ||
			    hio_svc_https_task_addreshdrfmt((hio_svc_https_task_t*)ws, "Sec-WebSocket-Version", "%d", WS_VERSION) <= -1 ||
			    hio_svc_https_task_addreshdr((hio_svc_https_task_t*)ws, "Content-Length", "0") <= -1 ||
			    hio_svc_https_task_endreshdr((hio_svc_https_task_t*)ws) <= -1) goto oops;
			ws->task_res_ended = 1;
		}
		else
		{
			if (hio_svc_https_task_sendfinalres((hio_svc_https_task_t*)ws, status_code, HIO_NULL, HIO_NULL, 1) <= -1) goto oops;
		}

		/* this connection never becomes a websocket, so it is finished with
		 * the way any other refused request is */
		hio_svc_https_task_unbindfromclient((hio_svc_https_task_t*)ws, 1);
		HIO_SVC_HTTPS_TASK_RCDOWN((hio_svc_https_task_t*)ws);
		return 0;
	}

	if (ws_compute_accept(key, accept, HIO_SIZEOF(accept)) <= -1) goto oops;

	/* the connection is not http after this answer, so it is never handed
	 * back for another request on it */
	ws->task_keep_client_alive = 0;

	if (hio_svc_https_task_startreshdr((hio_svc_https_task_t*)ws, HIO_HTTP_STATUS_SWITCH_PROTOCOL, HIO_NULL, HIO_SVC_HTTPS_TASK_RESHDR_UPGRADE) <= -1 ||
	    hio_svc_https_task_addreshdr((hio_svc_https_task_t*)ws, "Upgrade", "websocket") <= -1 ||
	    hio_svc_https_task_addreshdr((hio_svc_https_task_t*)ws, "Sec-WebSocket-Accept", accept) <= -1) goto oops;

	if (opt && opt->subproto &&
	    hio_svc_https_task_addreshdr((hio_svc_https_task_t*)ws, "Sec-WebSocket-Protocol", opt->subproto) <= -1) goto oops;

	if (hio_svc_https_task_endreshdr((hio_svc_https_task_t*)ws) <= -1) goto oops;
	ws->task_res_ended = 1;

	/* from here the connection carries frames rather than requests. the
	 * reader is switched to raw mode, which makes every octet after the
	 * handshake - including any the client sent along with it - arrive at
	 * this task's content handler instead of being parsed as http. */
	hio_htrd_setrecbs (cli->htrd, &ws_client_htrd_recbs);
	ws->task_client_htrd_recbs_changed = 1;
	hio_htrd_dummify (cli->htrd);

	if (hio_dev_sck_read(csck, 1) <= -1) goto oops;

	HIO_SVC_HTTPS_TASKL_APPEND_TASK (&https->task, (hio_svc_https_task_t*)ws);

	ws->opened = 1;
	if (ws->cbs.on_open && ws->cbs.on_open((hio_svc_https_ws_t*)ws) <= -1)
	{
		ws->opened = 0;
		goto oops;
	}

	ws_schedule_ping(ws, 0);

	HIO_SVC_HTTPS_TASK_RCDOWN((hio_svc_https_task_t*)ws);

	/* set only once this cannot fail - the callback is not to be run for a
	 * task whose creation did not succeed */
	ws->on_kill = on_kill;
	return 0;

oops:
	HIO_DEBUG2(hio, "HTTPS(%p) - FAILURE in dows - socket(%p)\n", https, csck);
	if (ws)
	{
		if (!ws->task_res_ended)
			hio_svc_https_task_sendfinalres((hio_svc_https_task_t*)ws, HIO_HTTP_STATUS_INTERNAL_SERVER_ERROR, HIO_NULL, HIO_NULL, 1);
		if (bound_to_client) hio_svc_https_task_unbindfromclient((hio_svc_https_task_t*)ws, 1);
		ws_halt_participating_devices(ws);
		HIO_SVC_HTTPS_TASK_RCDOWN((hio_svc_https_task_t*)ws);
	}
	return -1;
}
