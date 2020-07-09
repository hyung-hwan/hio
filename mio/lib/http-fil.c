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

#define FILE_ALLOW_UNLIMITED_REQ_CONTENT_LENGTH

enum file_state_res_mode_t
{
	FILE_STATE_RES_MODE_CHUNKED,
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
	int peer;
	mio_uintmax_t part_size;

	mio_svc_htts_cli_t* client;
	mio_http_version_t req_version; /* client request */

	unsigned int over: 4; /* must be large enough to accomodate FILE_STATE_OVER_ALL */
	unsigned int keep_alive: 1;
	unsigned int req_content_length_unlimited: 1;
	unsigned int ever_attempted_to_write_to_client: 1;
	unsigned int client_disconnected: 1;
	unsigned int client_htrd_recbs_changed: 1;
	mio_oow_t req_content_length; /* client request content length */
	file_state_res_mode_t res_mode_to_cli;

	mio_dev_sck_on_read_t client_org_on_read;
	mio_dev_sck_on_write_t client_org_on_write;
	mio_dev_sck_on_disconnect_t client_org_on_disconnect;
	mio_htrd_recbs_t client_htrd_org_recbs;

};
typedef struct file_state_t file_state_t;


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


#if 0

static int file_state_writev_to_client (file_state_t* file_state, mio_iovec_t* iov, mio_iolen_t iovcnt)
{
	file_state->ever_attempted_to_write_to_client = 1;

	file_state->num_pending_writes_to_client++;
	if (mio_dev_sck_writev(file_state->client->sck, iov, iovcnt, MIO_NULL, MIO_NULL) <= -1) 
	{
		file_state->num_pending_writes_to_client--;
		return -1;
	}

	if (file_state->num_pending_writes_to_client > FILE_STATE_PENDING_IO_THRESHOLD)
	{
		if (mio_dev_pro_read(file_state->peer, MIO_DEV_PRO_OUT, 0) <= -1) return -1;
	}
	return 0;
}

#endif

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


#if 0

static int file_state_write_last_chunk_to_client (file_state_t* file_state)
{
	if (!file_state->ever_attempted_to_write_to_client)
	{
		if (file_state_send_final_status_to_client(file_state, 500, 0) <= -1) return -1;
	}
	else
	{
		if (file_state->res_mode_to_cli == FILE_STATE_RES_MODE_CHUNKED &&
		    file_state_write_to_client(file_state, "0\r\n\r\n", 5) <= -1) return -1;
	}

	if (!file_state->keep_alive && file_state_write_to_client(file_state, MIO_NULL, 0) <= -1) return -1;
	return 0;
}
#endif

static int file_state_write_to_peer (file_state_t* file_state, const void* data, mio_iolen_t dlen)
{
	file_state->num_pending_writes_to_peer++;

#if 0
	if (mio_dev_pro_write(file_state->peer, data, dlen, MIO_NULL) <= -1) 
	{
		file_state->num_pending_writes_to_peer--;
		return -1;
	}
#else
	write(file_state->peer, data, dlen); // error check. also buffering if not all are written
#endif


/* TODO: check if it's already finished or something.. */
	if (file_state->num_pending_writes_to_peer > FILE_STATE_PENDING_IO_THRESHOLD)
	{
		if (mio_dev_sck_read(file_state->client->sck, 0) <= -1) return -1;
	}
	return 0;
}

static MIO_INLINE void file_state_mark_over (file_state_t* file_state, int over_bits)
{
	unsigned int old_over;

	old_over = file_state->over;
	file_state->over |= over_bits;

	MIO_DEBUG5 (file_state->htts->mio, "HTTS(%p) - client=%p peer=%p new-bits=%x over=%x\n", file_state->htts, file_state->client->sck, file_state->peer, (int)over_bits, (int)file_state->over);

#if 0
	if (!(old_over & FILE_STATE_OVER_READ_FROM_CLIENT) && (file_state->over & FILE_STATE_OVER_READ_FROM_CLIENT))
	{
		if (mio_dev_sck_read(file_state->client->sck, 0) <= -1) 
		{
			MIO_DEBUG2 (file_state->htts->mio, "HTTS(%p) - halting client(%p) for failure to disable input watching\n", file_state->htts, file_state->client->sck);
			mio_dev_sck_halt (file_state->client->sck);
		}
	}

	if (!(old_over & FILE_STATE_OVER_READ_FROM_PEER) && (file_state->over & FILE_STATE_OVER_READ_FROM_PEER))
	{
		if (file_state->peer && mio_dev_pro_read(file_state->peer, MIO_DEV_PRO_OUT, 0) <= -1) 
		{
			MIO_DEBUG2 (file_state->htts->mio, "HTTS(%p) - halting peer(%p) for failure to disable input watching\n", file_state->htts, file_state->peer);
			mio_dev_pro_halt (file_state->peer);
		}
	}

	if (old_over != FILE_STATE_OVER_ALL && file_state->over == FILE_STATE_OVER_ALL)
	{
		/* ready to stop */
		if (file_state->peer) 
		{
			MIO_DEBUG2 (file_state->htts->mio, "HTTS(%p) - halting peer(%p) as it is unneeded\n", file_state->htts, file_state->peer);
			mio_dev_pro_halt (file_state->peer);
		}

		if (file_state->keep_alive) 
		{
			/* how to arrange to delete this file_state object and put the socket back to the normal waiting state??? */
			MIO_ASSERT (file_state->htts->mio, file_state->client->rsrc == (mio_svc_htts_rsrc_t*)file_state);

printf ("DETACHING FROM THE MAIN CLIENT RSRC... state -> %p\n", file_state->client->rsrc);
			MIO_SVC_HTTS_RSRC_DETACH (file_state->client->rsrc);
			/* file_state must not be access from here down as it could have been destroyed */
		}
		else
		{
			MIO_DEBUG2 (file_state->htts->mio, "HTTS(%p) - halting client(%p) for no keep-alive\n", file_state->htts, file_state->client->sck);
			mio_dev_sck_shutdown (file_state->client->sck, MIO_DEV_SCK_SHUTDOWN_WRITE);
			mio_dev_sck_halt (file_state->client->sck);
		}
	}
#endif
}



static void file_state_on_kill (file_state_t* file_state)
{
	mio_t* mio = file_state->htts->mio;

	MIO_DEBUG2 (mio, "HTTS(%p) - killing file_state client(%p)\n", file_state->htts, file_state->client->sck);

	if (file_state->peer >= 0)
	{
		close (file_state->peer);
		file_state->peer = -1;
	}


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
/*printf ("ENABLING INPUT WATCHING on CLIENT %p. \n", file_state->client->sck);*/
		if (!file_state->keep_alive || mio_dev_sck_read(file_state->client->sck, 1) <= -1)
		{
			MIO_DEBUG2 (mio, "HTTS(%p) - halting client(%p) for failure to enable input watching\n", file_state->htts, file_state->client->sck);
			mio_dev_sck_halt (file_state->client->sck);
		}
	}

/*printf ("**** FILE_STATE_ON_KILL DONE\n");*/
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

#if 0
XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
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
printf ("UUUUUUUUUUUUUUUUUUUUUUUUUUGGGGGHHHHHHHHHHHH .......... FILE CLIENT GIVING EXCESSIVE DATA AFTER CONTENTS...\n");
		}
	}
#endif

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
printf ("QQQQQQQQQQQQQ  %lu\n", (long)file_state->num_pending_writes_to_client);
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

#if 0
XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
		if (file_state->peer && file_state->num_pending_writes_to_client == FILE_STATE_PENDING_IO_THRESHOLD)
		{
			if (!(file_state->over & FILE_STATE_OVER_READ_FROM_PEER) &&
			    mio_dev_pro_read(file_state->peer, MIO_DEV_PRO_OUT, 1) <= -1) goto oops;
		}
#endif

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


#if 0
int mio_svc_htts_dofile (mio_svc_htts_t* htts, mio_dev_sck_t* csck, mio_htre_t* req, const mio_bch_t* docroot, const mio_bch_t* file)
{
	switch (mio_htre_getqmethodtype(req))
	{
		case MIO_HTTP_OPTIONS:
			mio_htre_discardcontent (req);
			/* TODO: return 200 with Allow: OPTIONS, HEAD, GET, POST, PUT, DELETE... */
			/* However, if there is access configuration, the item list must reflect it */
			break;

		case MIO_HTTP_HEAD:
		case MIO_HTTP_GET:
		case MIO_HTTP_POST:
			mio_htre_discardcontent (req);
			break;

		case MIO_HTTP_PUT:
			break;

		case MIO_HTTP_DELETE:
			mio_htre_discardcontent (req);
			break;

		default:
			/* method not allowed */
			mio_htre_discardcontent (req);
			/* schedule to send 405  */
			break;
	}

	return 0;
}
#endif

static int file_state_send_header_to_client (file_state_t* file_state, int status_code, int force_close)
{
	mio_svc_htts_cli_t* cli = file_state->client;
	mio_bch_t dtbuf[64];

	mio_svc_htts_fmtgmtime (cli->htts, MIO_NULL, dtbuf, MIO_COUNTOF(dtbuf));

	if (!force_close) force_close = !file_state->keep_alive;

	if (mio_becs_fmt(cli->sbuf, "HTTP/%d.%d %d %hs\r\nServer: %hs\r\nDate: %s\r\nConnection: %hs\r\nContent-Length: %ju\r\n\r\n",
		file_state->req_version.major, file_state->req_version.minor,
		status_code, mio_http_status_to_bcstr(status_code),
		cli->htts->server_name, dtbuf,
		(force_close? "close": "keep-alive"),
		file_state->part_size) == (mio_oow_t)-1) return -1;

	return file_state_write_to_client(file_state, MIO_BECS_PTR(cli->sbuf), MIO_BECS_LEN(cli->sbuf));
}

static int file_state_send_contents_to_client (file_state_t* file_state)
{
/* TODO: implement mio_dev_sck_sendfile(0 or enhance mio_dev_sck_write() to emulate sendfile
 * 
 *      mio_dev_sck_setsendfile (ON);
 *      mio_dev_sck_write(sck, data_required_for_sendfile_operation, 0, MIO_NULL);....
 */

	mio_bch_t buf[8192]; /* TODO: declare this in file_state?? */
	int i;
	ssize_t n;

	for (i = 0; i < FILE_STATE_PENDING_IO_THRESHOLD; i++)
	{
		n = read(file_state->peer, buf, MIO_SIZEOF(buf));
		if (n == -1)
		{
			if (errno == EAGAIN)
			{
			}
			else if (errno == EINTR)
			{
			}
		}
		else if (n == 0)
		{
			/* no more data to read */
			file_state_mark_over (file_state, FILE_STATE_OVER_READ_FROM_CLIENT);
			break;
		}
		
		if (file_state_write_to_client(file_state, buf, n) <= -1) 
		{
			return -1;
		}
	}


	return 0;
}

int mio_svc_htts_dofile (mio_svc_htts_t* htts, mio_dev_sck_t* csck, mio_htre_t* req, const mio_bch_t* docroot, const mio_bch_t* file)
{
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
	/*file_state->num_pending_writes_to_client = 0;
	file_state->num_pending_writes_to_peer = 0;*/
	file_state->req_version = *mio_htre_getversion(req);
	file_state->req_content_length_unlimited = mio_htre_getreqcontentlen(req, &file_state->req_content_length);

	file_state->client_org_on_read = csck->on_read;
	file_state->client_org_on_write = csck->on_write;
	file_state->client_org_on_disconnect = csck->on_disconnect;
	csck->on_read = file_client_on_read;
	csck->on_write = file_client_on_write;
	csck->on_disconnect = file_client_on_disconnect;

	MIO_ASSERT (mio, cli->rsrc == MIO_NULL);
	MIO_SVC_HTTS_RSRC_ATTACH (file_state, cli->rsrc);

	if (access(actual_file, R_OK) == -1)
	{
		file_state_send_final_status_to_client (file_state, 403, 1); /* 403 Forbidden */
		goto oops; /* TODO: must not go to oops.  just destroy the file_state and finalize the request .. */
	}

/* TODO: if method is for download... open it in the read mode. if PUT or POST, open it in RDWR mode? */
	file_state->peer = open(actual_file, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (MIO_UNLIKELY(file_state->peer <= -1)) goto oops;


	{
		struct stat st;
		if (fstat(file_state->peer, &st) <= -1) goto oops;
		file_state->part_size = st.st_size;
file_state->part_size = MIO_TYPE_MAX(mio_intmax_t);
	}

/*
	file_peer = mio_dev_pro_getxtn(file_state->peer);
	MIO_SVC_HTTS_RSRC_ATTACH (file_state, file_peer->state);
*/


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
/* TODO: check method. if GET, file contents can be transmitted without 100 continue ... */
		if (mio_comp_http_version_numbers(&req->version, 1, 1) >= 0 && 
		   (file_state->req_content_length_unlimited || file_state->req_content_length > 0)) 
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

			msglen = mio_fmttobcstr(mio, msgbuf, MIO_COUNTOF(msgbuf), "HTTP/%d.%d 100 Continue\r\n\r\n", file_state->req_version.major, file_state->req_version.minor);
			if (file_state_write_to_client(file_state, msgbuf, msglen) <= -1) goto oops;
			file_state->ever_attempted_to_write_to_client = 0; /* reset this as it's polluted for 100 continue */
		}
	}
	else if (req->flags & MIO_HTRE_ATTR_EXPECT)
	{
		/* 417 Expectation Failed */
		file_state_send_final_status_to_client(file_state, 417, 1);
		goto oops;
	}

#if defined(FILE_ALLOW_UNLIMITED_REQ_CONTENT_LENGTH)
	if (file_state->req_content_length_unlimited)
	{
		/* change the callbacks to subscribe to contents to be uploaded */
#if 0
XXXXXXXXXXXX
		file_state->client_htrd_org_recbs = *mio_htrd_getrecbs(file_state->client->htrd);
		file_client_htrd_recbs.peek = file_state->client_htrd_org_recbs.peek;
		mio_htrd_setrecbs (file_state->client->htrd, &file_client_htrd_recbs);
		file_state->client_htrd_recbs_changed = 1;
#endif
	}
	else
	{
#endif
		if (file_state->req_content_length > 0)
		{
#if 0
XXXXXXXXXXXX
			/* change the callbacks to subscribe to contents to be uploaded */
			file_state->client_htrd_org_recbs = *mio_htrd_getrecbs(file_state->client->htrd);
			file_client_htrd_recbs.peek = file_state->client_htrd_org_recbs.peek;
			mio_htrd_setrecbs (file_state->client->htrd, &file_client_htrd_recbs);
			file_state->client_htrd_recbs_changed = 1;
#endif
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
		file_state->res_mode_to_cli = FILE_STATE_RES_MODE_CHUNKED; 
		/* the mode still can get switched to FILE_STATE_RES_MODE_LENGTH if the file emits Content-Length */
	}
	else
	{
		file_state->keep_alive = 0;
		file_state->res_mode_to_cli = FILE_STATE_RES_MODE_CLOSE;
	}

	if (file_state_send_header_to_client(file_state, 200, 0) <= -1) goto oops;
	if (file_state_send_contents_to_client(file_state) <= -1) goto oops;

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
