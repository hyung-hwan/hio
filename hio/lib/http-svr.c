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
#include <hio-path.h>
#include <errno.h>

#define INVALID_LIDX HIO_TYPE_MAX(hio_oow_t)

/* ------------------------------------------------------------------------ */
static int client_htrd_peek_request (hio_htrd_t* htrd, hio_htre_t* req)
{
	hio_svc_htts_cli_htrd_xtn_t* htrdxtn = (hio_svc_htts_cli_htrd_xtn_t*)hio_htrd_getxtn(htrd);
	hio_svc_htts_cli_t* sckxtn = (hio_svc_htts_cli_t*)hio_dev_sck_getxtn(htrdxtn->sck);
	return sckxtn->htts->proc_req(sckxtn->htts, htrdxtn->sck, req);
}

static hio_htrd_recbs_t client_htrd_recbs =
{
	client_htrd_peek_request,
	HIO_NULL,
	HIO_NULL
};

static int init_client (hio_svc_htts_cli_t* cli, hio_dev_sck_t* sck)
{
	hio_svc_htts_cli_htrd_xtn_t* htrdxtn;

	/* the htts field must be filled with the same field in the listening socket upon accept() */
	HIO_ASSERT (sck->hio, cli->htts != HIO_NULL);
	HIO_ASSERT (sck->hio, cli->l_idx < cli->htts->l.count); /* at this point, it's still the listener's index as it's cloned */
	HIO_ASSERT (sck->hio, sck->hio == cli->htts->hio);

	cli->sck = sck;
	cli->l_idx = INVALID_LIDX; /* not a listening socket anymore */
	cli->htrd = HIO_NULL;
	cli->sbuf = HIO_NULL;
	cli->rsrc = HIO_NULL;
	/* keep this linked regardless of success or failure because the disconnect() callback 
	 * will call fini_client(). the error handler code after 'oops:' doesn't get this unlinked */
	HIO_SVC_HTTS_CLIL_APPEND_CLI (&cli->htts->cli, cli);

	cli->htrd = hio_htrd_open(sck->hio, HIO_SIZEOF(*htrdxtn));
	if (HIO_UNLIKELY(!cli->htrd)) goto oops;

	/* With HIO_HTRD_TRAILERS, htrd stores trailers in a separate place.
	 * Otherwise, it is merged to the headers. */
	/*hio_htrd_setoption (cli->htrd, HIO_HTRD_REQUEST | HIO_HTRD_TRAILERS);*/

	cli->sbuf = hio_becs_open(sck->hio, 0, 2048);
	if (HIO_UNLIKELY(!cli->sbuf)) goto oops;

	htrdxtn = hio_htrd_getxtn(cli->htrd);
	htrdxtn->sck = sck; /* TODO: remember cli instead? */

	hio_htrd_setrecbs (cli->htrd, &client_htrd_recbs);

	hio_gettime (sck->hio, &cli->last_active);
	HIO_DEBUG3 (sck->hio, "HTTS(%p) - initialized client %p socket %p\n", cli->htts, cli, sck);
	return 0;

oops:
	/* since this function is called in the on_connect() callback,
	 * fini_client() is eventually called by on_disconnect(). i don't do clean-up here.
	if (cli->sbuf) 
	{
		hio_becs_close(cli->sbuf);
		cli->sbuf = HIO_NULL;
	}
	if (cli->htrd)
	{
		hio_htrd_close (cli->htrd);
		cli->htrd = HIO_NULL;
	}*/
	return -1;
}

static void fini_client (hio_svc_htts_cli_t* cli)
{
	HIO_DEBUG3 (cli->sck->hio, "HTTS(%p) - finalizing client %p socket %p\n", cli->htts, cli, cli->sck);

	if (cli->rsrc)
	{
		hio_svc_htts_rsrc_kill (cli->rsrc);
		cli->rsrc = HIO_NULL;
	}

	if (cli->sbuf) 
	{
		hio_becs_close (cli->sbuf);
		cli->sbuf = HIO_NULL;
	}

	if (cli->htrd)
	{
		hio_htrd_close (cli->htrd);
		cli->htrd = HIO_NULL;
	}

	HIO_SVC_HTTS_CLIL_UNLINK_CLI_CLEAN (cli);

	/* are these needed? not symmetrical if done here. 
	 * these fields are copied from the listener socket upon accept.
	 * init_client() doesn't fill in these fields. let's comment out these lines
	cli->sck = HIO_NULL;
	cli->htts = HIO_NULL; 
	*/
}

/* ------------------------------------------------------------------------ */

static int listener_on_read (hio_dev_sck_t* sck, const void* buf, hio_iolen_t len, const hio_skad_t* srcaddr)
{
	/* unlike the function name, this callback is set on both the listener and the client.
	 * however, it must never be triggered for the listener */

	hio_t* hio = sck->hio;
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	hio_oow_t rem;
	int x;

	HIO_ASSERT (hio, cli->l_idx == INVALID_LIDX);

	/* if a resource has been set(cli->rsrc not NULL), the resource must take over
	 * this handler. this handler is never called unless the the overriding handler
	 * call this. */
	HIO_ASSERT (hio, cli->rsrc == HIO_NULL); 

	if (len <= -1)
	{
		HIO_DEBUG3 (hio, "HTTS(%p) - unable to read client %p(%d)\n", cli->htts, sck, (int)sck->hnd);
		goto oops;
	}

	if (len == 0) 
	{
		HIO_DEBUG3 (hio, "HTTS(%p) - EOF on client %p(%d)\n", cli->htts, sck, (int)sck->hnd);
		goto oops;
	}

	hio_gettime (hio, &cli->last_active);
	if ((x = hio_htrd_feed(cli->htrd, buf, len, &rem)) <= -1) 
	{
		HIO_DEBUG3 (hio, "HTTS(%p) - feed error onto client htrd %p(%d)\n", cli->htts, sck, (int)sck->hnd);
		goto oops;
	}

	if (rem > 0)
	{
		if (cli->rsrc)
		{
			/* TODO store this to client buffer. once the current resource is completed, arrange to call on_read() with it */
		}
		else
		{
			/* TODO: no resource in action. so feed one more time */
		}
	}

	return 0;

oops:
	hio_dev_sck_halt (sck);
	return 0; /* still return success here. instead call halt() */
}

static int listener_on_write (hio_dev_sck_t* sck, hio_iolen_t wrlen, void* wrctx, const hio_skad_t* dstaddr)
{
	/* don't get deluded by the name. it's set on both the listener and the client.
	 * it is not supposed to be triggered on the listener, however */
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck);
	HIO_ASSERT (sck->hio, cli->l_idx == INVALID_LIDX);

	/* if a resource has been set(cli->rsrc not NULL), the resource must take over
	 * this handler. this handler is never called unless the the overriding handler
	 * call this. */
	HIO_ASSERT (sck->hio, cli->rsrc == HIO_NULL);

	/* anyways, nothing to do upon write completion */

	return 0;
}

static void listener_on_connect (hio_dev_sck_t* sck)
{
	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(sck); /* the contents came from the listening socket */

	if (sck->state & HIO_DEV_SCK_ACCEPTED)
	{
		/* accepted a new client */
		HIO_DEBUG3 (sck->hio, "HTTS(%p) - accepted... %p %d \n", cli->htts, sck, sck->hnd);

		if (init_client(cli, sck) <= -1)
		{
			HIO_DEBUG2 (cli->htts->hio, "HTTS(%p) - halting client(%p) for client intiaialization failure\n", cli->htts, sck);
			hio_dev_sck_halt (sck);
		}
	}
	else if (sck->state & HIO_DEV_SCK_CONNECTED)
	{
		/* this will never be triggered as the listing socket never call hio_dev_sck_connect() */
		HIO_DEBUG3 (sck->hio, "** HTTS(%p) - connected... %p %d \n", cli->htts, sck, sck->hnd);
	}

	/* HIO_DEV_SCK_CONNECTED must not be seen here as this is only for the listener socket */
}

static void listener_on_disconnect (hio_dev_sck_t* sck)
{
	hio_t* hio = sck->hio;
	hio_svc_htts_cli_t* xtn = hio_dev_sck_getxtn(sck);

	switch (HIO_DEV_SCK_GET_PROGRESS(sck))
	{
		case HIO_DEV_SCK_CONNECTING:
			/* only for connecting sockets */
			HIO_DEBUG1 (hio, "OUTGOING SESSION DISCONNECTED - FAILED TO CONNECT (%d) TO REMOTE SERVER\n", (int)sck->hnd);
			break;

		case HIO_DEV_SCK_CONNECTING_SSL:
			/* only for connecting sockets */
			HIO_DEBUG1 (hio, "OUTGOING SESSION DISCONNECTED - FAILED TO SSL-CONNECT (%d) TO REMOTE SERVER\n", (int)sck->hnd);
			break;

		case HIO_DEV_SCK_CONNECTED:
			/* only for connecting sockets */
			HIO_DEBUG1 (hio, "OUTGOING CLIENT CONNECTION GOT TORN DOWN %p(%d).......\n", (int)sck->hnd);
			break;

		case HIO_DEV_SCK_LISTENING:
			HIO_DEBUG2 (hio, "LISTNER SOCKET %p(%d) - SHUTTUING DOWN\n", sck, (int)sck->hnd);
			break;

		case HIO_DEV_SCK_ACCEPTING_SSL: /* special case. */
			/* this progress code indicates that the ssl-level accept failed.
			 * on_disconnected() with this code is called without corresponding on_connect(). 
			 * the cli extension are is not initialized yet */
			HIO_ASSERT (hio, sck != xtn->sck);
			//HIO_ASSERT (hio, cli->sck == cli->htts->lsck); /* the field is a copy of the extension are of the listener socket. so it should point to the listner socket */
			HIO_DEBUG2 (hio, "LISTENER UNABLE TO SSL-ACCEPT CLIENT %p(%d) ....%p\n", sck, (int)sck->hnd);
			return;

		case HIO_DEV_SCK_ACCEPTED:
			/* only for sockets accepted by the listeners. will never come here because
			 * the disconnect call for such sockets have been changed in listener_on_connect() */
			HIO_DEBUG2 (hio, "ACCEPTED CLIENT SOCKET %p(%d) GOT DISCONNECTED.......\n", sck, (int)sck->hnd);
			break;

		default:
			HIO_DEBUG2 (hio, "SOCKET %p(%d) DISCONNECTED AFTER ALL.......\n", sck, (int)sck->hnd);
			break;
	}

	if (xtn->l_idx < xtn->htts->l.count)
	{
		/* the listener socket has these fields set to NULL */
		HIO_ASSERT (hio, xtn->htrd == HIO_NULL);
		HIO_ASSERT (hio, xtn->sbuf == HIO_NULL);

		HIO_DEBUG2 (hio, "HTTS(%p) - listener socket disconnect %p\n", xtn->htts, sck);
		xtn->htts->l.sck[xtn->l_idx] = HIO_NULL; /* let the htts service forget about this listening socket */
	}
	else
	{
		/* client socket */
		HIO_DEBUG2 (hio, "HTTS(%p) - client socket disconnect %p\n", xtn->htts, sck);
		HIO_ASSERT (hio, xtn->sck == sck);
		fini_client (xtn);
	}
}

/* ------------------------------------------------------------------------ */
#define MAX_CLIENT_IDLE 10


static void halt_idle_clients (hio_t* hio, const hio_ntime_t* now, hio_tmrjob_t* job)
{
/* TODO: this idle client detector is far away from being accurate.
 *       enhance htrd to specify timeout on feed() and utilize it... 
 *       and remove this timer job */
	hio_svc_htts_t* htts = (hio_svc_htts_t*)job->ctx;
	hio_svc_htts_cli_t* cli;
	hio_ntime_t t;

	static hio_ntime_t max_client_idle = { MAX_CLIENT_IDLE, 0 };

	for (cli = HIO_SVC_HTTS_CLIL_FIRST_CLI(&htts->cli); !HIO_SVC_HTTS_CLIL_IS_NIL_CLI(&htts->cli, cli); cli = cli->cli_next)
	{
		if (!cli->rsrc)
		{
			hio_ntime_t t;
			HIO_SUB_NTIME(&t, now, &cli->last_active);

			if (HIO_CMP_NTIME(&t, &max_client_idle) >= 0) 
			{
				HIO_DEBUG3 (hio, "HTTS(%p) - Halting idle client socket %p(client=%p)\n", htts, cli->sck, cli);
				hio_dev_sck_halt (cli->sck);
			}
		}
	}

	HIO_INIT_NTIME (&t, MAX_CLIENT_IDLE, 0);
	HIO_ADD_NTIME (&t, &t, now);
	if (hio_schedtmrjobat(hio, &t, halt_idle_clients, &htts->idle_tmridx, htts) <= -1)
	{
		HIO_INFO1 (hio, "HTTS(%p) - unable to reschedule idle client detector. continuting\n", htts);
	}
}

/* ------------------------------------------------------------------------ */

hio_svc_htts_t* hio_svc_htts_start (hio_t* hio, hio_dev_sck_bind_t* binds, hio_oow_t nbinds, hio_svc_htts_proc_req_t proc_req)
{
	hio_svc_htts_t* htts = HIO_NULL;
	union
	{
		hio_dev_sck_make_t m;
		hio_dev_sck_listen_t l;
	} info;
	hio_svc_htts_cli_t* cli;
	hio_oow_t i, noks;

	if (HIO_UNLIKELY(nbinds <= 0)) 
	{
		hio_seterrnum (hio, HIO_EINVAL);
		goto oops;
	}

	htts = (hio_svc_htts_t*)hio_callocmem(hio, HIO_SIZEOF(*htts));
	if (HIO_UNLIKELY(!htts)) goto oops;

	HIO_DEBUG1 (hio, "HTTS - STARTING SERVICE %p\n", htts);

	htts->hio = hio;
	htts->svc_stop = hio_svc_htts_stop;
	htts->proc_req = proc_req;
	htts->idle_tmridx = HIO_TMRIDX_INVALID;

	htts->l.sck = (hio_dev_sck_t**)hio_callocmem(hio, HIO_SIZEOF(*htts->l.sck) * nbinds);
	if (HIO_UNLIKELY(!htts->l.sck)) goto oops;
	htts->l.count = nbinds;

	for (i = 0, noks = 0; i < nbinds; i++)
	{
		hio_dev_sck_t* sck;

		HIO_MEMSET (&info, 0, HIO_SIZEOF(info));
		switch (hio_skad_get_family(&binds[i].localaddr))
		{
			case HIO_AF_INET:
				info.m.type = HIO_DEV_SCK_TCP4;
				break;
	
			case HIO_AF_INET6:
				info.m.type = HIO_DEV_SCK_TCP6;
				break;
	
		#if defined(HIO_AF_UNIX)
			case HIO_AF_UNIX:
				info.m.type = HIO_DEV_SCK_UNIX;
				break;
		#endif

			case HIO_AF_QX:
				info.m.type = HIO_DEV_SCK_QX;
				break;

			default:
				/* ignore this */
				HIO_DEBUG3 (hio, "HTTS(%p) - [%zu] unsupported bind address type %d\n", htts, i, (int)hio_skad_get_family(&binds[i].localaddr));
				continue;
		}
		info.m.options = HIO_DEV_SCK_MAKE_LENIENT;
		info.m.on_write = listener_on_write;
		info.m.on_read = listener_on_read;
		info.m.on_connect = listener_on_connect;
		info.m.on_disconnect = listener_on_disconnect;
		sck = hio_dev_sck_make(hio, HIO_SIZEOF(*cli), &info.m);
		if (HIO_UNLIKELY(!sck)) continue;
	
		/* the name 'cli' for the listening socket is awkward.
		 * the listening socket will use the htts, sck, and listening fields for tracking only.
		 * each accepted client socket gets the extension size for this size as well.
		 * most of other fields are used for client management. init_client() will set
		 * the sck field to the client socket and the listening field to 0. */
		cli = (hio_svc_htts_cli_t*)hio_dev_sck_getxtn(sck);
		cli->htts = htts; 
		cli->sck = sck;
		cli->l_idx = i;

		if (sck->type != HIO_DEV_SCK_QX)
		{
			if (hio_dev_sck_bind(sck, &binds[i]) <= -1)
			{
				if (HIO_LOG_ENABLED(hio, HIO_LOG_DEBUG))
				{
					hio_bch_t tmpbuf[HIO_SKAD_IP_STRLEN + 1];
					hio_skadtobcstr(hio, &binds[i].localaddr, tmpbuf, HIO_COUNTOF(tmpbuf), HIO_SKAD_TO_BCSTR_ADDR | HIO_SKAD_TO_BCSTR_PORT);
					HIO_DEBUG3 (hio, "HTTS(%p) - [%zu] unable to bind to %hs\n", htts, i, tmpbuf);
				}

				hio_dev_sck_kill (sck);
				continue;
			}

			HIO_MEMSET (&info, 0, HIO_SIZEOF(info));
			info.l.backlogs = 4096; /* TODO: use configuration? */
			HIO_INIT_NTIME (&info.l.accept_tmout, 5, 1); /* usedd for ssl accept */
			if (hio_dev_sck_listen(sck, &info.l) <= -1) 
			{
				if (HIO_LOG_ENABLED(hio, HIO_LOG_DEBUG))
				{
					hio_bch_t tmpbuf[HIO_SKAD_IP_STRLEN + 1];
					hio_skadtobcstr(hio, &binds[i].localaddr, tmpbuf, HIO_COUNTOF(tmpbuf), HIO_SKAD_TO_BCSTR_ADDR | HIO_SKAD_TO_BCSTR_PORT);
					HIO_DEBUG3 (hio, "HTTS(%p) - [%zu] unable to bind to %hs\n", htts, i, tmpbuf);
				}

				hio_dev_sck_kill (sck);
				goto oops;
			}
		}

		if (HIO_LOG_ENABLED(hio, HIO_LOG_DEBUG))
		{
			hio_skad_t tmpad;
			hio_bch_t tmpbuf[HIO_SKAD_IP_STRLEN + 1];
			hio_dev_sck_getsockaddr(sck, &tmpad);
			hio_skadtobcstr(hio, &tmpad, tmpbuf, HIO_COUNTOF(tmpbuf), HIO_SKAD_TO_BCSTR_ADDR | HIO_SKAD_TO_BCSTR_PORT);
			HIO_DEBUG3 (hio, "HTTS(%p) - [%zu] listening on %hs\n", htts, i, tmpbuf);
		}

		htts->l.sck[i] = sck;
		noks++;
	}

	if (noks <= 0) goto oops;

	hio_fmttobcstr (htts->hio, htts->server_name_buf, HIO_COUNTOF(htts->server_name_buf), "%s-%d.%d.%d", 
		HIO_PACKAGE_NAME, (int)HIO_PACKAGE_VERSION_MAJOR, (int)HIO_PACKAGE_VERSION_MINOR, (int)HIO_PACKAGE_VERSION_PATCH);
	htts->server_name = htts->server_name_buf;

	HIO_SVCL_APPEND_SVC (&hio->actsvc, (hio_svc_t*)htts);
	HIO_SVC_HTTS_CLIL_INIT (&htts->cli);

	HIO_DEBUG1 (hio, "HTTS - STARTED SERVICE %p\n", htts);

	{
		hio_ntime_t t;

		HIO_INIT_NTIME (&t, MAX_CLIENT_IDLE, 0);
		if (hio_schedtmrjobafter(hio, &t, halt_idle_clients, &htts->idle_tmridx, htts) <= -1)
		{
			HIO_INFO1 (hio, "HTTS(%p) - unable to schedule idle client detector. continuting\n", htts);
			/* don't care about failure */
		}
	}

	return htts;

oops:
	if (htts)
	{
		if (htts->l.sck) 
		{
			for (i = 0; i < htts->l.count; i++)
			{
				if (htts->l.sck[i]) hio_dev_sck_kill (htts->l.sck[i]);
			}
			hio_freemem (hio, htts->l.sck);
		}

		hio_freemem (hio, htts);
	}
	return HIO_NULL;
}

void hio_svc_htts_stop (hio_svc_htts_t* htts)
{
	hio_t* hio = htts->hio;
	hio_oow_t i;

	HIO_DEBUG1 (hio, "HTTS - STOPPING SERVICE %p\n", htts);

	for (i = 0; i < htts->l.count; i++)
	{
		/* the socket may be null:
		 *  if it has been destroyed for operation errors and forgotten in the disconnect callback thereafter
		 *  if it has never been created successfully */
		if (htts->l.sck[i]) hio_dev_sck_kill (htts->l.sck[i]);
	}

	while (!HIO_SVC_HTTS_CLIL_IS_EMPTY(&htts->cli))
	{
		hio_svc_htts_cli_t* cli = HIO_SVC_HTTS_CLIL_FIRST_CLI(&htts->cli);
		hio_dev_sck_kill (cli->sck);
	}

	HIO_SVCL_UNLINK_SVC (htts);
	if (htts->server_name && htts->server_name != htts->server_name_buf) hio_freemem (hio, htts->server_name);

	if (htts->idle_tmridx != HIO_TMRIDX_INVALID) hio_deltmrjob (hio, htts->idle_tmridx);

	if (htts->l.sck) hio_freemem (hio, htts->l.sck);
	hio_freemem (hio, htts);
}

int hio_svc_htts_setservernamewithbcstr (hio_svc_htts_t* htts, const hio_bch_t* name)
{
	hio_t* hio = htts->hio;
	hio_bch_t* tmp;

	if (hio_copy_bcstr(htts->server_name_buf, HIO_COUNTOF(htts->server_name_buf), name) == hio_count_bcstr(name))
	{
		tmp = htts->server_name_buf;
	}
	else
	{
		tmp = hio_dupbcstr(hio, name, HIO_NULL);
		if (!tmp) return -1;
	}

	if (htts->server_name && htts->server_name != htts->server_name_buf) hio_freemem (hio, htts->server_name);
	htts->server_name = tmp;
	return 0;
}

hio_dev_sck_t* hio_svc_htts_getlistendev (hio_svc_htts_t* htts, hio_oow_t idx)
{
	if (idx >= htts->l.count)
	{
		hio_seterrbfmt (htts->hio, HIO_EINVAL, "index out of range");
		return HIO_NULL;
	}

	if (!htts->l.sck[idx])
	{
		hio_seterrbfmt (htts->hio, HIO_EINVAL, "no listener at the given index");
		return HIO_NULL;
	}

	return htts->l.sck[idx];
}

hio_oow_t hio_svc_htts_getnlistendevs (hio_svc_htts_t* htts)
{
	/* return the total number of listening socket devices.
	 * not all devices may be up and working */
	return htts->l.count;
}

int hio_svc_htts_getsockaddr (hio_svc_htts_t* htts, hio_oow_t idx, hio_skad_t* skad)
{
	/* return the socket address of the listening socket. */

	if (idx >= htts->l.count)
	{
		hio_seterrbfmt (htts->hio, HIO_EINVAL, "index out of range");
		return -1;
	}

	if (!htts->l.sck[idx])
	{
		hio_seterrbfmt (htts->hio, HIO_EINVAL, "no listener at the given index");
		return -1;
	}

	return hio_dev_sck_getsockaddr(htts->l.sck[idx], skad);
}

/* ----------------------------------------------------------------- */

hio_svc_htts_rsrc_t* hio_svc_htts_rsrc_make (hio_svc_htts_t* htts, hio_oow_t rsrc_size, hio_svc_htts_rsrc_on_kill_t on_kill)
{
	hio_t* hio = htts->hio;
	hio_svc_htts_rsrc_t* rsrc;

	rsrc = hio_callocmem(hio, rsrc_size);
	if (HIO_UNLIKELY(!rsrc)) return HIO_NULL;

	rsrc->htts = htts;
	rsrc->rsrc_size = rsrc_size;
	rsrc->rsrc_refcnt = 0;
	rsrc->rsrc_on_kill = on_kill;

	return rsrc;
}

void hio_svc_htts_rsrc_kill (hio_svc_htts_rsrc_t* rsrc)
{
	hio_t* hio = rsrc->htts->hio;
	if (rsrc->rsrc_on_kill) rsrc->rsrc_on_kill (rsrc);
	hio_freemem (hio, rsrc);
}

#if defined(HIO_HAVE_INLINE)
static HIO_INLINE void* hio_svc_htts_rsrc_getxtn (hio_svc_htts_rsrc_t* rsrc) { return rsrc + 1; }
#else
#define hio_svc_htts_rsrc_getxtn(rsrc) ((void*)((hio_svc_htts_rsrc_t*)rsrc + 1))
#endif

/* ----------------------------------------------------------------- */


/* ----------------------------------------------------------------- */

int hio_svc_htts_doproxy (hio_svc_htts_t* htts, hio_dev_sck_t* csck, hio_htre_t* req, const hio_bch_t* upstream)
{
#if 0
	1. attempt to connect to the proxy target...
	2. in the mean time, 
	hio_dev_watch (csck, HIO_DEV_WATCH_UPDATE, 0); /* no input, no output watching */

	3. once connected,
	hio_dev_watch (csck, HIO_DEV_WATCH_RENEW, HIO_DEV_EVENT_IN); /* enable input watching. if needed, enable output watching */

	4. start proxying


	5. if one side is stalled, donot read from another side... let the kernel slow the connection...
	   i need to know how may bytes are pending for this.
	   if pending too high, disable read watching... hio_dev_watch (csck, HIO_DEV_WATCH_RENEW, 0);
#endif
	return 0;
}

/* ----------------------------------------------------------------- */

void hio_svc_htts_fmtgmtime (hio_svc_htts_t* htts, const hio_ntime_t* nt, hio_bch_t* buf, hio_oow_t len)
{
	hio_ntime_t now;

	if (!nt) 
	{
		hio_sys_getrealtime(htts->hio, &now);
		nt = &now;
	}

	hio_fmt_http_time_to_bcstr(nt, buf, len);
}

hio_bch_t* hio_svc_htts_dupmergepaths (hio_svc_htts_t* htts, const hio_bch_t* base, const hio_bch_t* path)
{
	hio_bch_t* xpath;
	const hio_bch_t* ta[4];
	hio_oow_t idx = 0;

	ta[idx++] = base;
	if (path[0] != '\0')
	{
		ta[idx++] = "/";
		ta[idx++] = path;
	}
	ta[idx++] = HIO_NULL;
	xpath = hio_dupbcstrs(htts->hio, ta, HIO_NULL);
	if (HIO_UNLIKELY(!xpath)) return HIO_NULL;

	hio_canon_bcstr_path (xpath, xpath, 0);
	return xpath;
}

int hio_svc_htts_writetosidechan (hio_svc_htts_t* htts, hio_oow_t idx, const void* dptr, hio_oow_t dlen)
{
	if (idx >= htts->l.count)
	{
		/* don't set the error information - TODO: change hio_seterrbfmt thread-safe?
		 *hio_seterrbfmt (htts->hio, HIO_EINVAL, "index out of range");*/ 
		errno = EINVAL;
		return -1;
	}

	if (!htts->l.sck[idx])
	{
		/* don't set the error information 
		 *hio_seterrbfmt (htts->hio, HIO_EINVAL, "no listener at the given index"); */
		errno = EINVAL;
		return -1;
	}

	return hio_dev_sck_writetosidechan(htts->l.sck[idx], dptr, dlen);
}


