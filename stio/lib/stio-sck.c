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


#include "stio-sck.h"
#include "stio-prv.h"

#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* ========================================================================= */
void stio_closeasyncsck (stio_t* stio, stio_sckhnd_t sck)
{
#if defined(_WIN32)
	closesocket (sck);
#else
	close (sck);
#endif
}

#if 0
int  stio_shutasyncsck (stio_t* stio, stio_sckhnd_t sck, int how)
{
	shutdown (sck, how);
}
#endif

int stio_makesckasync (stio_t* stio, stio_sckhnd_t sck)
{
	return stio_makesyshndasync (stio, (stio_syshnd_t)sck);
}

stio_sckhnd_t stio_openasyncsck (stio_t* stio, int domain, int type, int proto)
{
	stio_sckhnd_t sck;

#if defined(_WIN32)
	sck = WSASocket (domain, type, proto, NULL, 0, WSA_FLAG_OVERLAPPED /*| WSA_FLAG_NO_HANDLE_INHERIT*/);
	if (sck == STIO_SCKHND_INVALID) 
	{
		/* stio_seterrnum (dev->stio, STIO_ESYSERR); or translate errno to stio errnum */
		return STIO_SCKHND_INVALID;
	}
#else
	sck = socket (domain, type, proto); 
	if (sck == STIO_SCKHND_INVALID) 
	{
		stio->errnum = stio_syserrtoerrnum(errno);
		return STIO_SCKHND_INVALID;
	}

	if (stio_makesckasync (stio, sck) <= -1)
	{
		close (sck);
		return STIO_SCKHND_INVALID;
	}

	/* TODO: set CLOEXEC if it's defined */
#endif

	return sck;
}

int stio_getsckadrinfo (stio_t* stio, const stio_sckadr_t* addr, stio_scklen_t* len, stio_sckfam_t* family)
{
	struct sockaddr* saddr = (struct sockaddr*)addr;

	if (saddr->sa_family == AF_INET) 
	{
		if (len) *len = STIO_SIZEOF(struct sockaddr_in);
		if (family) *family = AF_INET;
		return 0;
	}
	else if (saddr->sa_family == AF_INET6)
	{
		if (len) *len =  STIO_SIZEOF(struct sockaddr_in6);
		if (family) *family = AF_INET6;
		return 0;
	}

	stio->errnum = STIO_EINVAL;
	return -1;
}


/* ========================================================================= */

#define IS_STATEFUL(sck) ((sck)->dev_capa & STIO_DEV_CAPA_STREAM)

struct sck_type_map_t
{
	int domain;
	int type;
	int proto;
	int extra_dev_capa;
	int sck_capa;
};

static struct sck_type_map_t sck_type_map[] =
{
	{ AF_INET,    SOCK_STREAM,    0,                         STIO_DEV_CAPA_STREAM  | STIO_DEV_CAPA_OUT_QUEUED },
	{ AF_INET6,   SOCK_STREAM,    0,                         STIO_DEV_CAPA_STREAM  | STIO_DEV_CAPA_OUT_QUEUED },
	{ AF_INET,    SOCK_DGRAM,     0,                         0                                                },
	{ AF_INET6,   SOCK_DGRAM,     0,                         0                                                },

	{ AF_PACKET,  SOCK_RAW,       STIO_CONST_HTON16(0x0806), 0                                                },
	{ AF_PACKET,  SOCK_DGRAM,     STIO_CONST_HTON16(0x0806), 0                                                } 
};

/* ======================================================================== */

static void tmr_connect_handle (stio_t* stio, const stio_ntime_t* now, stio_tmrjob_t* job)
{
	stio_dev_sck_t* rdev = (stio_dev_sck_t*)job->ctx;

	STIO_ASSERT (IS_STATEFUL(rdev));

	if (rdev->state & STIO_DEV_SCK_CONNECTING)
	{
		/* the state check for STIO_DEV_TCP_CONNECTING is actually redundant
		 * as it must not be fired  after it gets connected. the timer job 
		 * doesn't need to be deleted when it gets connected for this check 
		 * here. this libarary, however, deletes the job when it gets 
		 * connected. */
		stio_dev_sck_halt (rdev);
	}
}

/* ======================================================================== */

static int dev_sck_make (stio_dev_t* dev, void* ctx)
{
	stio_dev_sck_t* rdev = (stio_dev_sck_t*)dev;
	stio_dev_sck_make_t* arg = (stio_dev_sck_make_t*)ctx;

	STIO_ASSERT (arg->type >= 0 && arg->type < STIO_COUNTOF(sck_type_map));

	rdev->sck = stio_openasyncsck (dev->stio, sck_type_map[arg->type].domain, sck_type_map[arg->type].type, sck_type_map[arg->type].proto);
	if (rdev->sck == STIO_SCKHND_INVALID) goto oops;

	rdev->dev_capa = STIO_DEV_CAPA_IN | STIO_DEV_CAPA_OUT | sck_type_map[arg->type].extra_dev_capa;
	rdev->on_write = arg->on_write;
	rdev->on_read = arg->on_read;
	rdev->type = arg->type;

	rdev->tmridx_connect = STIO_TMRIDX_INVALID;
	return 0;

oops:
	if (rdev->sck != STIO_SCKHND_INVALID)
	{
		stio_closeasyncsck (rdev->stio, rdev->sck);
		rdev->sck = STIO_SCKHND_INVALID;
	}
	return -1;
}

static int dev_sck_make_client (stio_dev_t* dev, void* ctx)
{
	stio_dev_sck_t* rdev = (stio_dev_sck_t*)dev;
	stio_syshnd_t* sck = (stio_syshnd_t*)ctx;

	rdev->sck = *sck;
	if (stio_makesckasync (rdev->stio, rdev->sck) <= -1) return -1;

	return 0;
}

static void dev_sck_kill (stio_dev_t* dev)
{
	stio_dev_sck_t* rdev = (stio_dev_sck_t*)dev;

	if (IS_STATEFUL(rdev))
	{
		if (rdev->state & (STIO_DEV_SCK_ACCEPTED | STIO_DEV_SCK_CONNECTED | STIO_DEV_SCK_CONNECTING | STIO_DEV_SCK_LISTENING))
		{
			if (rdev->on_disconnect) rdev->on_disconnect (rdev);
		}

		if (rdev->tmridx_connect != STIO_TMRIDX_INVALID)
		{
			stio_deltmrjob (dev->stio, rdev->tmridx_connect);
			STIO_ASSERT (rdev->tmridx_connect == STIO_TMRIDX_INVALID);
		}
	}
	else
	{
		STIO_ASSERT (rdev->state == 0);
		STIO_ASSERT (rdev->tmridx_connect == STIO_TMRIDX_INVALID);
	}

	if (rdev->sck != STIO_SCKHND_INVALID) 
	{
		stio_closeasyncsck (rdev->stio, rdev->sck);
		rdev->sck = STIO_SCKHND_INVALID;
	}
}

static stio_syshnd_t dev_sck_getsyshnd (stio_dev_t* dev)
{
	stio_dev_sck_t* rdev = (stio_dev_sck_t*)dev;
	return (stio_syshnd_t)rdev->sck;
}

static int dev_sck_read_stateful (stio_dev_t* dev, void* buf, stio_len_t* len, stio_adr_t* srcadr)
{
	stio_dev_sck_t* tcp = (stio_dev_sck_t*)dev;
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

static int dev_sck_read_stateless (stio_dev_t* dev, void* buf, stio_len_t* len, stio_adr_t* srcadr)
{
	stio_dev_sck_t* rdev = (stio_dev_sck_t*)dev;
	stio_scklen_t srcadrlen;
	ssize_t x;

	srcadrlen = srcadr->len;
	x = recvfrom (rdev->sck, buf, *len, 0, srcadr->ptr, &srcadrlen);
	if (x <= -1)
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK) return 0;  /* no data available */
		if (errno == EINTR) return 0;
		rdev->stio->errnum = stio_syserrtoerrnum(errno);
		return -1;
	}

	srcadr->len = srcadrlen;
	*len = x;
	return 1;
}


static int dev_sck_write_stateful (stio_dev_t* dev, const void* data, stio_len_t* len, const stio_adr_t* dstadr)
{
	stio_dev_sck_t* rdev = (stio_dev_sck_t*)dev;
	ssize_t x;
	int flags = 0;

	if (*len <= 0)
	{
		/* it's a writing finish indicator. close the writing end of
		 * the socket, probably leaving it in the half-closed state */
		if (shutdown (rdev->sck, SHUT_WR) == -1)
		{
			rdev->stio->errnum = stio_syserrtoerrnum(errno);
			return -1;
		}

		return 1;
	}

	/* TODO: flags MSG_DONTROUTE, MSG_DONTWAIT, MSG_MORE, MSG_OOB, MSG_NOSIGNAL */
#if defined(MSG_NOSIGNAL)
	flags |= MSG_NOSIGNAL;
#endif
	x = send (rdev->sck, data, *len, flags);
	if (x == -1) 
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK) return 0;  /* no data can be written */
		if (errno == EINTR) return 0;
		rdev->stio->errnum = stio_syserrtoerrnum(errno);
		return -1;
	}

	*len = x;
	return 1;
}

static int dev_sck_write_stateless (stio_dev_t* dev, const void* data, stio_len_t* len, const stio_adr_t* dstadr)
{
	stio_dev_sck_t* rdev = (stio_dev_sck_t*)dev;
	ssize_t x;

	x = sendto (rdev->sck, data, *len, 0, dstadr->ptr, dstadr->len);
	if (x <= -1) 
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK) return 0;  /* no data can be written */
		if (errno == EINTR) return 0;
		rdev->stio->errnum = stio_syserrtoerrnum(errno);
		return -1;
	}

	*len = x;
	return 1;
}

static int dev_sck_ioctl (stio_dev_t* dev, int cmd, void* arg)
{
	stio_dev_sck_t* rdev = (stio_dev_sck_t*)dev;

	switch (cmd)
	{
		case STIO_DEV_SCK_BIND:
		{
			stio_dev_sck_bind_t* bnd = (stio_dev_sck_bind_t*)arg;
			struct sockaddr* sa = (struct sockaddr*)&bnd->addr;
			stio_scklen_t sl;
			stio_sckfam_t fam;
			int x;

			if (stio_getsckadrinfo (dev->stio, &bnd->addr, &sl, &fam) <= -1) return -1;

			/* the socket is already non-blocking */
			x = bind (rdev->sck, sa, sl);
			if (x == -1)
			{
				rdev->stio->errnum = stio_syserrtoerrnum(errno);
				return -1;
			}

			return 0;
		}

		case STIO_DEV_SCK_CONNECT:
		{
			stio_dev_sck_connect_t* conn = (stio_dev_sck_connect_t*)arg;
			struct sockaddr* sa = (struct sockaddr*)&conn->addr;
			stio_scklen_t sl;
			int x;

			if (!IS_STATEFUL(rdev)) 
			{
				dev->stio->errnum = STIO_ENOCAPA;
				return -1;
			}

			if (sa->sa_family == AF_INET) sl = STIO_SIZEOF(struct sockaddr_in);
			else if (sa->sa_family == AF_INET6) sl = STIO_SIZEOF(struct sockaddr_in6);
			else 
			{
				dev->stio->errnum = STIO_EINVAL;
				return -1;
			}

			/* the socket is already non-blocking */
			x = connect (rdev->sck, sa, sl);
			if (x == -1)
			{
				if (errno == EINPROGRESS || errno == EWOULDBLOCK)
				{
					if (stio_dev_watch ((stio_dev_t*)rdev, STIO_DEV_WATCH_UPDATE, STIO_DEV_EVENT_IN | STIO_DEV_EVENT_OUT) >= 0)
					{
						stio_tmrjob_t tmrjob;

						if (!stio_isnegtime(&conn->tmout))
						{
							STIO_MEMSET (&tmrjob, 0, STIO_SIZEOF(tmrjob));
							tmrjob.ctx = rdev;
							stio_gettime (&tmrjob.when);
							stio_addtime (&tmrjob.when, &conn->tmout, &tmrjob.when);
							tmrjob.handler = tmr_connect_handle;
							tmrjob.idxptr = &rdev->tmridx_connect;

							STIO_ASSERT (rdev->tmridx_connect == STIO_TMRIDX_INVALID);
							rdev->tmridx_connect = stio_instmrjob (rdev->stio, &tmrjob);
							if (rdev->tmridx_connect == STIO_TMRIDX_INVALID)
							{
								stio_dev_watch ((stio_dev_t*)rdev, STIO_DEV_WATCH_UPDATE, STIO_DEV_EVENT_IN);
								/* event manipulation failure can't be handled properly. so ignore it. 
								 * anyway, it's already in a failure condition */
								return -1;
							}
						}

						rdev->state |= STIO_DEV_SCK_CONNECTING;
						rdev->peer = conn->addr;
						rdev->on_connect = conn->on_connect;
						rdev->on_disconnect = conn->on_disconnect;
						return 0;
					}
				}

				rdev->stio->errnum = stio_syserrtoerrnum(errno);
				return -1;
			}

			/* connected immediately */
			rdev->state |= STIO_DEV_SCK_CONNECTED;
			rdev->peer = conn->addr;
			rdev->on_connect = conn->on_connect;
			rdev->on_disconnect = conn->on_disconnect;
			return 0;
		}

		case STIO_DEV_SCK_LISTEN:
		{
			stio_dev_sck_listen_t* lstn = (stio_dev_sck_listen_t*)arg;
			int x;

			if (!IS_STATEFUL(rdev)) 
			{
				dev->stio->errnum = STIO_ENOCAPA;
				return -1;
			}

			x = listen (rdev->sck, lstn->backlogs);
			if (x == -1) 
			{
				rdev->stio->errnum = stio_syserrtoerrnum(errno);
				return -1;
			}

			rdev->state |= STIO_DEV_SCK_LISTENING;
			rdev->on_connect = lstn->on_connect;
			rdev->on_disconnect = lstn->on_disconnect;
			return 0;
		}
	}

	return 0;
}

static stio_dev_mth_t dev_mth_sck_stateless = 
{
	dev_sck_make,
	dev_sck_kill,
	dev_sck_getsyshnd,

	dev_sck_read_stateless,
	dev_sck_write_stateless,
	dev_sck_ioctl,     /* ioctl */
};


static stio_dev_mth_t dev_mth_sck_stateful = 
{
	dev_sck_make,
	dev_sck_kill,
	dev_sck_getsyshnd,

	dev_sck_read_stateful,
	dev_sck_write_stateful,
	dev_sck_ioctl,     /* ioctl */
};


static stio_dev_mth_t dev_mth_clisck =
{
	dev_sck_make_client,
	dev_sck_kill,
	dev_sck_getsyshnd,

	dev_sck_read_stateful,
	dev_sck_write_stateful,
	dev_sck_ioctl
};
/* ========================================================================= */

static int dev_evcb_sck_ready_stateful (stio_dev_t* dev, int events)
{
	stio_dev_sck_t* rdev = (stio_dev_sck_t*)dev;

	if (events & STIO_DEV_EVENT_ERR)
	{
		int errcode;
		stio_scklen_t len;

		len = STIO_SIZEOF(errcode);
		if (getsockopt (rdev->sck, SOL_SOCKET, SO_ERROR, (char*)&errcode, &len) == -1)
		{
			/* the error number is set to the socket error code.
			 * errno resulting from getsockopt() doesn't reflect the actual
			 * socket error. so errno is not used to set the error number.
			 * instead, the generic device error STIO_EDEVERRR is used */
			rdev->stio->errnum = STIO_EDEVERR;
		}
		else
		{
			rdev->stio->errnum = stio_syserrtoerrnum (errcode);
		}
		return -1;
	}

	/* this socket can connect */
	if (rdev->state & STIO_DEV_SCK_CONNECTING)
	{
		if (events & STIO_DEV_EVENT_HUP)
		{
			/* device hang-up */
			rdev->stio->errnum = STIO_EDEVHUP;
			return -1;
		}
		else if (events & (STIO_DEV_EVENT_PRI | STIO_DEV_EVENT_IN))
		{
			/* invalid event masks. generic device error */
			rdev->stio->errnum = STIO_EDEVERR;
			return -1;
		}
		else if (events & STIO_DEV_EVENT_OUT)
		{
			int errcode;
			stio_scklen_t len;

			STIO_ASSERT (!(rdev->state & STIO_DEV_SCK_CONNECTED));

			len = STIO_SIZEOF(errcode);
			if (getsockopt (rdev->sck, SOL_SOCKET, SO_ERROR, (char*)&errcode, &len) == -1)
			{
				rdev->stio->errnum = stio_syserrtoerrnum(errno);
				return -1;
			}
			else if (errcode == 0)
			{
				rdev->state &= ~STIO_DEV_SCK_CONNECTING;
				rdev->state |= STIO_DEV_SCK_CONNECTED;

				if (stio_dev_watch ((stio_dev_t*)rdev, STIO_DEV_WATCH_RENEW, 0) <= -1) return -1;

				if (rdev->tmridx_connect != STIO_TMRIDX_INVALID)
				{
					stio_deltmrjob (rdev->stio, rdev->tmridx_connect);
					STIO_ASSERT (rdev->tmridx_connect == STIO_TMRIDX_INVALID);
				}

				if (rdev->on_connect (rdev) <= -1) 
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
				rdev->stio->errnum = stio_syserrtoerrnum(errcode);
				return -1;
			}
		}

		return 0; /* success but don't invoke on_read() */ 
	}
	else if (rdev->state & STIO_DEV_SCK_LISTENING)
	{
		if (events & STIO_DEV_EVENT_HUP)
		{
			/* device hang-up */
			rdev->stio->errnum = STIO_EDEVHUP;
			return -1;
		}
		else if (events & (STIO_DEV_EVENT_PRI | STIO_DEV_EVENT_OUT))
		{
			rdev->stio->errnum = STIO_EDEVERR;
			return -1;
		}
		else if (events & STIO_DEV_EVENT_IN)
		{
			stio_sckhnd_t clisck;
			stio_sckadr_t peer;
			stio_scklen_t addrlen;
			stio_dev_sck_t* clidev;

			/* this is a server(lisening) socket */

			addrlen = STIO_SIZEOF(peer);
			clisck = accept (rdev->sck, (struct sockaddr*)&peer, &addrlen);
			if (clisck == STIO_SCKHND_INVALID)
			{
				if (errno == EINPROGRESS || errno == EWOULDBLOCK) return 0;
				if (errno == EINTR) return 0; /* if interrupted by a signal, treat it as if it's EINPROGRESS */

				rdev->stio->errnum = stio_syserrtoerrnum(errno);
				return -1;
			}

			/* use rdev->dev_size when instantiating a client sck device
			 * instead of STIO_SIZEOF(stio_dev_sck_t). therefore, the 
			 * extension area as big as that of the master sck device
			 * is created in the client sck device */
			clidev = (stio_dev_sck_t*)stio_makedev (rdev->stio, rdev->dev_size, &dev_mth_clisck, rdev->dev_evcb, &clisck); 
			if (!clidev) 
			{
				close (clisck);
				return -1;
			}

			clidev->dev_capa |= STIO_DEV_CAPA_IN | STIO_DEV_CAPA_OUT | STIO_DEV_CAPA_STREAM;
			clidev->state |= STIO_DEV_SCK_ACCEPTED;
			clidev->peer = peer;
			/*clidev->parent = sck;*/

			/* inherit some event handlers from the parent.
			 * you can still change them inside the on_connect handler */
			clidev->on_connect = rdev->on_connect;
			clidev->on_disconnect = rdev->on_disconnect; 
			clidev->on_write = rdev->on_write;
			clidev->on_read = rdev->on_read;

			clidev->tmridx_connect = STIO_TMRIDX_INVALID;
			if (clidev->on_connect(clidev) <= -1) stio_dev_sck_halt (clidev);

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

		rdev->stio->errnum = STIO_EDEVHUP;
		return -1;
	}

	return 1; /* the device is ok. carry on reading or writing */
}

static int dev_evcb_sck_ready_stateless (stio_dev_t* dev, int events)
{
	stio_dev_sck_t* rdev = (stio_dev_sck_t*)dev;

	if (events & STIO_DEV_EVENT_ERR)
	{
		int errcode;
		stio_scklen_t len;

		len = STIO_SIZEOF(errcode);
		if (getsockopt (rdev->sck, SOL_SOCKET, SO_ERROR, (char*)&errcode, &len) == -1)
		{
			/* the error number is set to the socket error code.
			 * errno resulting from getsockopt() doesn't reflect the actual
			 * socket error. so errno is not used to set the error number.
			 * instead, the generic device error STIO_EDEVERRR is used */
			rdev->stio->errnum = STIO_EDEVERR;
		}
		else
		{
			rdev->stio->errnum = stio_syserrtoerrnum (errcode);
		}
		return -1;
	}
	else if (events & STIO_DEV_EVENT_HUP)
	{
		rdev->stio->errnum = STIO_EDEVHUP;
		return -1;
	}

	return 1; /* the device is ok. carry on reading or writing */
}

static int dev_evcb_sck_on_read (stio_dev_t* dev, const void* data, stio_len_t dlen, const stio_adr_t* adr)
{
	stio_dev_sck_t* rdev = (stio_dev_sck_t*)dev;
	return rdev->on_read (rdev, data, dlen, adr);
}

static int dev_evcb_sck_on_write (stio_dev_t* dev, stio_len_t wrlen, void* wrctx, const stio_adr_t* adr)
{
	stio_dev_sck_t* rdev = (stio_dev_sck_t*)dev;
	return rdev->on_write (rdev, wrlen, wrctx);
}

static stio_dev_evcb_t dev_evcb_sck_stateful =
{
	dev_evcb_sck_ready_stateful,
	dev_evcb_sck_on_read,
	dev_evcb_sck_on_write
};

static stio_dev_evcb_t dev_evcb_sck_stateless =
{
	dev_evcb_sck_ready_stateless,
	dev_evcb_sck_on_read,
	dev_evcb_sck_on_write
};

/* ========================================================================= */

stio_dev_sck_t* stio_dev_sck_make (stio_t* stio, stio_size_t xtnsize, const stio_dev_sck_make_t* mkinf)
{
	stio_dev_sck_t* rdev;


	if (mkinf->type < 0 && mkinf->type >= STIO_COUNTOF(sck_type_map))
	{
		stio->errnum = STIO_EINVAL;
		return STIO_NULL;
	}

	if (sck_type_map[mkinf->type].extra_dev_capa & STIO_DEV_CAPA_STREAM) /* can't use the IS_STATEFUL() macro yet */
	{
		rdev = (stio_dev_sck_t*)stio_makedev (stio, STIO_SIZEOF(stio_dev_sck_t) + xtnsize, &dev_mth_sck_stateful, &dev_evcb_sck_stateful, (void*)mkinf);
	}
	else
	{
		rdev = (stio_dev_sck_t*)stio_makedev (stio, STIO_SIZEOF(stio_dev_sck_t) + xtnsize, &dev_mth_sck_stateless, &dev_evcb_sck_stateless, (void*)mkinf);
	}

	return rdev;
}

int stio_dev_sck_bind (stio_dev_sck_t* dev, stio_dev_sck_bind_t* info)
{
	return stio_dev_ioctl ((stio_dev_t*)dev, STIO_DEV_SCK_BIND, info);
}

int stio_dev_sck_connect (stio_dev_sck_t* dev, stio_dev_sck_connect_t* info)
{
	return stio_dev_ioctl ((stio_dev_t*)dev, STIO_DEV_SCK_CONNECT, info);
}

int stio_dev_sck_listen (stio_dev_sck_t* dev, stio_dev_sck_listen_t* info)
{
	return stio_dev_ioctl ((stio_dev_t*)dev, STIO_DEV_SCK_LISTEN, info);
}

int stio_dev_sck_write (stio_dev_sck_t* dev, const void* data, stio_len_t dlen, void* wrctx, const stio_adr_t* dstadr)
{
	return stio_dev_write ((stio_dev_t*)dev, data, dlen, wrctx, dstadr);
}

int stio_dev_sck_timedwrite (stio_dev_sck_t* dev, const void* data, stio_len_t dlen, const stio_ntime_t* tmout, void* wrctx, const stio_adr_t* dstadr)
{
	return stio_dev_write ((stio_dev_t*)dev, data, dlen, wrctx, dstadr);
}
