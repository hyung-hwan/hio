/*
 * $Id$
 *
    Copyright (c) 2015-2016 Chung, Hyung-Hwan. All rights reserved.

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


#include "stio-tcp.h"
#include "stio-prv.h"

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

static int tcp_make (stio_dev_t* dev, void* ctx)
{
	stio_dev_tcp_t* tcp = (stio_dev_tcp_t*)dev;
	stio_dev_tcp_make_t* arg = (stio_dev_tcp_make_t*)ctx;
	stio_scklen_t len;
	stio_sckfam_t family;
	int iv;

	if (stio_getsckadrinfo(dev->stio, &arg->addr, &len, &family) <= -1) return -1;

	tcp->sck = stio_openasyncsck (dev->stio, family, SOCK_STREAM, 0);
	if (tcp->sck == STIO_SCKHND_INVALID) goto oops;

/* TODO:
	setsockopt (udp->sck, SOL_SOCKET, SO_REUSEADDR, ...);
	 TRANSPARENT, ETC. 
 */
	iv = 1;
	if (setsockopt (tcp->sck, SOL_SOCKET, SO_REUSEADDR, &iv, STIO_SIZEOF(iv)) == -1 ||
	    bind (tcp->sck, (struct sockaddr*)&arg->addr, len) == -1) 
	{
		tcp->stio->errnum = stio_syserrtoerrnum(errno);
		goto oops;
	}

	tcp->dev_capa = STIO_DEV_CAPA_IN | STIO_DEV_CAPA_OUT | STIO_DEV_CAPA_STREAM;
	tcp->on_write = arg->on_write;
	tcp->on_read = arg->on_read; 
	tcp->tmridx_connect = STIO_TMRIDX_INVALID;
	return 0;

oops:
	if (tcp->sck != STIO_SCKHND_INVALID)
	{
		stio_closeasyncsck (tcp->stio, tcp->sck);
		tcp->sck = STIO_SCKHND_INVALID;
	}
	return -1;
}

static int tcp_make_accepted (stio_dev_t* dev, void* ctx)
{
	stio_dev_tcp_t* tcp = (stio_dev_tcp_t*)dev;
	stio_syshnd_t* sck = (stio_syshnd_t*)ctx;

	tcp->sck = *sck;
	if (stio_makesckasync (dev->stio, tcp->sck) <= -1) return -1;

	return 0;
}

static void tcp_kill (stio_dev_t* dev)
{
	stio_dev_tcp_t* tcp = (stio_dev_tcp_t*)dev;

	if (tcp->state & (STIO_DEV_TCP_ACCEPTED | STIO_DEV_TCP_CONNECTED | STIO_DEV_TCP_CONNECTING | STIO_DEV_TCP_LISTENING))
	{
		if (tcp->on_disconnect) tcp->on_disconnect (tcp);
	}

	if (tcp->tmridx_connect != STIO_TMRIDX_INVALID)
	{
		stio_deltmrjob (dev->stio, tcp->tmridx_connect);
		STIO_ASSERT (tcp->tmridx_connect == STIO_TMRIDX_INVALID);
	}

	if (tcp->sck != STIO_SCKHND_INVALID) 
	{
		stio_closeasyncsck (tcp->stio, tcp->sck);
		tcp->sck = STIO_SCKHND_INVALID;
	}
}

static stio_syshnd_t tcp_getsyshnd (stio_dev_t* dev)
{
	stio_dev_tcp_t* tcp = (stio_dev_tcp_t*)dev;
	return (stio_syshnd_t)tcp->sck;
}

static int tcp_read (stio_dev_t* dev, void* buf, stio_len_t* len)
{
	stio_dev_tcp_t* tcp = (stio_dev_tcp_t*)dev;
	ssize_t x;

	x = recv (tcp->sck, buf, *len, 0);
	if (x == -1)
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK) return 0;  /* no data available */
		if (errno == EINTR) return 0;
		tcp->stio->errnum = stio_syserrtoerrnum(errno);
		return -1;
	}

	*len = x;
	return 1;
}

static int tcp_write (stio_dev_t* dev, const void* data, stio_len_t* len)
{
	stio_dev_tcp_t* tcp = (stio_dev_tcp_t*)dev;
	ssize_t x;
	int flags = 0;

	if (*len <= 0)
	{
		/* it's a writing finish indicator. close the writing end of
		 * the socket, probably leaving it in the half-closed state */
		if (shutdown (tcp->sck, SHUT_WR) == -1)
		{
			tcp->stio->errnum = stio_syserrtoerrnum(errno);
			return -1;
		}

		return 1;
	}

	/* TODO: flags MSG_DONTROUTE, MSG_DONTWAIT, MSG_MORE, MSG_OOB, MSG_NOSIGNAL */
#if defined(MSG_NOSIGNAL)
	flags |= MSG_NOSIGNAL;
#endif
	x = sendto (tcp->sck, data, *len, flags, STIO_NULL, 0);
	if (x == -1) 
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK) return 0;  /* no data can be written */
		if (errno == EINTR) return 0;
		tcp->stio->errnum = stio_syserrtoerrnum(errno);
		return -1;
	}

	*len = x;
	return 1;
}

static void tmr_connect_handle (stio_t* stio, const stio_ntime_t* now, stio_tmrjob_t* job)
{
	stio_dev_tcp_t* tcp = (stio_dev_tcp_t*)job->ctx;

	if (tcp->state & STIO_DEV_TCP_CONNECTING)
	{
		/* the state check for STIO_DEV_TCP_CONNECTING is actually redundant
		 * as it must not be fired  after it gets connected. the timer job 
		 * doesn't need to be deleted when it gets connected for this check 
		 * here. this libarary, however, deletes the job when it gets 
		 * connected. */
		stio_dev_tcp_halt (tcp);
	}
}

#if defined(STIO_USE_TMRJOB_IDXPTR)
/* nothing to define */
#else
static void tmr_connect_update (stio_t* stio, stio_tmridx_t old_index, stio_tmridx_t new_index, stio_tmrjob_t* job)
{
	stio_dev_tcp_t* tcp = (stio_dev_tcp_t*)job->ctx;
	tcp->tmridx_connect = new_index;
}
#endif


static int tcp_ioctl (stio_dev_t* dev, int cmd, void* arg)
{
	stio_dev_tcp_t* tcp = (stio_dev_tcp_t*)dev;

	switch (cmd)
	{
		case STIO_DEV_TCP_BIND:
		{
			stio_dev_tcp_bind_t* bnd = (stio_dev_tcp_bind_t*)arg;
			struct sockaddr* sa = (struct sockaddr*)&bnd->addr;
			stio_scklen_t sl;
			stio_sckfam_t fam;
			int x;

			if (stio_getsckadrinfo (dev->stio, &bnd->addr, &sl, &fam) <= -1) return -1;

		#if defined(_WIN32)
			/* TODO */
		#else
			/* the socket is already non-blocking */
			x = bind (tcp->sck, sa, sl);
			if (x == -1)
			{
				tcp->stio->errnum = stio_syserrtoerrnum(errno);
				return -1;
			}

			return 0;
		#endif

		}

		case STIO_DEV_TCP_CONNECT:
		{
			stio_dev_tcp_connect_t* conn = (stio_dev_tcp_connect_t*)arg;
			struct sockaddr* sa = (struct sockaddr*)&conn->addr;
			stio_scklen_t sl;
			int x;

			if (sa->sa_family == AF_INET) sl = STIO_SIZEOF(struct sockaddr_in);
			else if (sa->sa_family == AF_INET6) sl = STIO_SIZEOF(struct sockaddr_in6);
			else 
			{
				dev->stio->errnum = STIO_EINVAL;
				return -1;
			}

		#if defined(_WIN32)
			/* TODO */
		#else
			/* the socket is already non-blocking */

			x = connect (tcp->sck, sa, sl);
			if (x == -1)
			{
				if (errno == EINPROGRESS || errno == EWOULDBLOCK)
				{
					if (stio_dev_watch ((stio_dev_t*)tcp, STIO_DEV_WATCH_UPDATE, STIO_DEV_EVENT_IN | STIO_DEV_EVENT_OUT) >= 0)
					{
						stio_tmrjob_t tmrjob;

						if (!stio_isnegtime(&conn->tmout))
						{
							STIO_MEMSET (&tmrjob, 0, STIO_SIZEOF(tmrjob));
							tmrjob.ctx = tcp;
							stio_gettime (&tmrjob.when);
							stio_addtime (&tmrjob.when, &conn->tmout, &tmrjob.when);
							tmrjob.handler = tmr_connect_handle;
						#if defined(STIO_USE_TMRJOB_IDXPTR)
							tmrjob.idxptr = &tcp->tmridx_connect;
						#else
							tmrjob.updater = tmr_connect_update;
						#endif

							STIO_ASSERT (tcp->tmridx_connect == STIO_TMRIDX_INVALID);
							tcp->tmridx_connect = stio_instmrjob (tcp->stio, &tmrjob);
							if (tcp->tmridx_connect == STIO_TMRIDX_INVALID)
							{
								stio_dev_watch ((stio_dev_t*)tcp, STIO_DEV_WATCH_UPDATE, STIO_DEV_EVENT_IN);
								/* event manipulation failure can't be handled properly. so ignore it. 
								 * anyway, it's already in a failure condition */
								return -1;
							}
						}

						tcp->state |= STIO_DEV_TCP_CONNECTING;
						tcp->peer = conn->addr;
						tcp->on_connect = conn->on_connect;
						tcp->on_disconnect = conn->on_disconnect;
						return 0;
					}
				}

				tcp->stio->errnum = stio_syserrtoerrnum(errno);
				return -1;
			}

			/* connected immediately */
			tcp->state |= STIO_DEV_TCP_CONNECTED;
			tcp->peer = conn->addr;
			tcp->on_connect = conn->on_connect;
			tcp->on_disconnect = conn->on_disconnect;
			return 0;
		#endif
		}

		case STIO_DEV_TCP_LISTEN:
		{
			stio_dev_tcp_listen_t* lstn = (stio_dev_tcp_listen_t*)arg;
			int x;

		#if defined(_WIN32)
			/* TODO */
		#else
			x = listen (tcp->sck, lstn->backlogs);
			if (x == -1) 
			{
				tcp->stio->errnum = stio_syserrtoerrnum(errno);
				return -1;
			}

			tcp->state |= STIO_DEV_TCP_LISTENING;
			tcp->on_connect = lstn->on_connect;
			tcp->on_disconnect = lstn->on_disconnect;
			return 0;
		#endif
		}
	}

	return 0;
}


static stio_dev_mth_t tcp_mth = 
{
	tcp_make,
	tcp_kill,
	tcp_getsyshnd,
	tcp_read,
	tcp_write,
	tcp_ioctl, 
};

/* accepted tcp socket */
static stio_dev_mth_t tcp_acc_mth =
{
	tcp_make_accepted,
	tcp_kill,
	tcp_getsyshnd,
	tcp_read,
	tcp_write,
	tcp_ioctl
};

/* ------------------------------------------------------------------------ */

static int tcp_ready (stio_dev_t* dev, int events)
{
	stio_dev_tcp_t* tcp = (stio_dev_tcp_t*)dev;
printf ("TCP READY...%p\n", dev);

	if (events & STIO_DEV_EVENT_ERR)
	{
		int errcode;
		stio_scklen_t len;

		len = STIO_SIZEOF(errcode);
		if (getsockopt (tcp->sck, SOL_SOCKET, SO_ERROR, (char*)&errcode, &len) == -1)
		{
			/* the error number is set to the socket error code.
			 * errno resulting from getsockopt() doesn't reflect the actual
			 * socket error. so errno is not used to set the error number.
			 * instead, the generic device error STIO_EDEVERRR is used */
			tcp->stio->errnum = STIO_EDEVERR;
		}
		else
		{
			tcp->stio->errnum = stio_syserrtoerrnum (errcode);
		}
		return -1;
	}

	if (tcp->state & STIO_DEV_TCP_CONNECTING)
	{
		if (events & STIO_DEV_EVENT_HUP)
		{
			/* device hang-up */
			tcp->stio->errnum = STIO_EDEVHUP;
			return -1;
		}
		else if (events & (STIO_DEV_EVENT_PRI | STIO_DEV_EVENT_IN))
		{
			/* invalid event masks. generic device error */
			tcp->stio->errnum = STIO_EDEVERR;
			return -1;
		}
		else if (events & STIO_DEV_EVENT_OUT)
		{
			int errcode;
			stio_scklen_t len;

			STIO_ASSERT (!(tcp->state & STIO_DEV_TCP_CONNECTED));

			len = STIO_SIZEOF(errcode);
			if (getsockopt (tcp->sck, SOL_SOCKET, SO_ERROR, (char*)&errcode, &len) == -1)
			{
				tcp->stio->errnum = stio_syserrtoerrnum(errno);
				return -1;
			}
			else if (errcode == 0)
			{
				tcp->state &= ~STIO_DEV_TCP_CONNECTING;
				tcp->state |= STIO_DEV_TCP_CONNECTED;

				if (stio_dev_watch ((stio_dev_t*)tcp, STIO_DEV_WATCH_RENEW, 0) <= -1) return -1;

				if (tcp->tmridx_connect != STIO_TMRIDX_INVALID)
				{
					stio_deltmrjob (tcp->stio, tcp->tmridx_connect);
					STIO_ASSERT (tcp->tmridx_connect == STIO_TMRIDX_INVALID);
				}

				if (tcp->on_connect (tcp) <= -1) 
				{
					printf ("ON_CONNECTE HANDLER RETURNEF FAILURE...\n");
					return -1;
				}
			}
			else if (errcode == EINPROGRESS || errcode == EWOULDBLOCK)
			{
				/* still in progress */
			}
			else
			{
				tcp->stio->errnum = stio_syserrtoerrnum(errcode);
				return -1;
			}
		}

		return 0; /* success but don't invoke on_read() */ 
	}
	else if (tcp->state & STIO_DEV_TCP_LISTENING)
	{
		if (events & STIO_DEV_EVENT_HUP)
		{
			/* device hang-up */
			tcp->stio->errnum = STIO_EDEVHUP;
			return -1;
		}
		else if (events & (STIO_DEV_EVENT_PRI | STIO_DEV_EVENT_OUT))
		{
			tcp->stio->errnum = STIO_EDEVERR;
			return -1;
		}
		else if (events & STIO_DEV_EVENT_IN)
		{
			stio_sckhnd_t clisck;
			stio_sckadr_t peer;
			stio_scklen_t addrlen;
			stio_dev_tcp_t* clitcp;

			/* this is a server(lisening) socket */

			addrlen = STIO_SIZEOF(peer);
			clisck = accept (tcp->sck, (struct sockaddr*)&peer, &addrlen);
			if (clisck == STIO_SCKHND_INVALID)
			{
				if (errno == EINPROGRESS || errno == EWOULDBLOCK) return 0;
				if (errno == EINTR) return 0; /* if interrupted by a signal, treat it as if it's EINPROGRESS */

				tcp->stio->errnum = stio_syserrtoerrnum(errno);
				return -1;
			}

			/* use tcp->dev_size when instantiating a client tcp device
			 * instead of STIO_SIZEOF(stio_dev_tcp_t). therefore, the 
			 * extension area as big as that of the master tcp device
			 * is created in the client tcp device */
			clitcp = (stio_dev_tcp_t*)stio_makedev (tcp->stio, tcp->dev_size, &tcp_acc_mth, tcp->dev_evcb, &clisck); 
			if (!clitcp) 
			{
				close (clisck);
				return -1;
			}

			clitcp->dev_capa |= STIO_DEV_CAPA_IN | STIO_DEV_CAPA_OUT | STIO_DEV_CAPA_STREAM;
			clitcp->state |= STIO_DEV_TCP_ACCEPTED;
			clitcp->peer = peer;
			/*clitcp->parent = tcp;*/

			/* inherit some event handlers from the parent.
			 * you can still change them inside the on_connect handler */
			clitcp->on_connect = tcp->on_connect;
			clitcp->on_disconnect = tcp->on_disconnect; 
			clitcp->on_write = tcp->on_write;
			clitcp->on_read = tcp->on_read;

			clitcp->tmridx_connect = STIO_TMRIDX_INVALID;
			if (clitcp->on_connect (clitcp) <= -1) stio_dev_tcp_halt (clitcp);

			return 0; /* success but don't invoke on_read() */ 
		}
	}
	else if (events & STIO_DEV_EVENT_HUP)
	{
		if (events & (STIO_DEV_EVENT_PRI | STIO_DEV_EVENT_IN | STIO_DEV_EVENT_OUT)) 
		{
			/* probably half-open? */
			return 1;
		}

		tcp->stio->errnum = STIO_EDEVHUP;
		return -1;
	}

	return 1; /* the device is ok. carry on reading or writing */
}

static int tcp_on_read (stio_dev_t* dev, const void* data, stio_len_t len)
{
	stio_dev_tcp_t* tcp = (stio_dev_tcp_t*)dev;
	return tcp->on_read (tcp, data, len);
}

static int tcp_on_write (stio_dev_t* dev, stio_len_t wrlen, void* wrctx)
{
	stio_dev_tcp_t* tcp = (stio_dev_tcp_t*)dev;
	return tcp->on_write (tcp, wrlen, wrctx);
}

static stio_dev_evcb_t tcp_evcb =
{
	tcp_ready,
	tcp_on_read,
	tcp_on_write
};

stio_dev_tcp_t* stio_dev_tcp_make (stio_t* stio, stio_size_t xtnsize, const stio_dev_tcp_make_t* data)
{
	return (stio_dev_tcp_t*)stio_makedev (stio, STIO_SIZEOF(stio_dev_tcp_t) + xtnsize, &tcp_mth, &tcp_evcb, (void*)data);
}

int stio_dev_tcp_bind (stio_dev_tcp_t* tcp, stio_dev_tcp_bind_t* bind)
{
	return stio_dev_ioctl ((stio_dev_t*)tcp, STIO_DEV_TCP_BIND, bind);
}

int stio_dev_tcp_connect (stio_dev_tcp_t* tcp, stio_dev_tcp_connect_t* conn)
{
	return stio_dev_ioctl ((stio_dev_t*)tcp, STIO_DEV_TCP_CONNECT, conn);
}

int stio_dev_tcp_listen (stio_dev_tcp_t* tcp, stio_dev_tcp_listen_t* lstn)
{
	return stio_dev_ioctl ((stio_dev_t*)tcp, STIO_DEV_TCP_LISTEN, lstn);
}

