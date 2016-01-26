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


#include "stio-prv.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>


static int tcp_make (stio_dev_t* dev, void* ctx)
{
/* NOTE: this can be extended to use ctx to tell between INET and INET6 or other types of sockets without creating a new dev method set. */

	stio_dev_tcp_t* tcp = (stio_dev_tcp_t*)dev;
	struct sockaddr* saddr = (struct sockaddr*)ctx;

	tcp->sck = stio_openasyncsck (AF_INET, SOCK_STREAM);
	if (tcp->sck == STIO_SCKHND_INVALID) goto oops;

	if (saddr)
	{
		stio_scklen_t len;
		int iv;

		if (saddr->sa_family == AF_INET) 
			len = STIO_SIZEOF(struct sockaddr_in);
		else if (saddr->sa_family == AF_INET6)
			len = STIO_SIZEOF(struct sockaddr_in6);
		else	
		{
			dev->stio->errnum = STIO_EINVAL;
			goto oops;
		}

		//setsockopt (udp->sck, SOL_SOCKET, SO_REUSEADDR, ...);
		// TRANSPARENT, ETC.

		iv = 1;
		if (setsockopt (tcp->sck, SOL_SOCKET, SO_REUSEADDR, &iv, STIO_SIZEOF(iv)) == -1 ||
		    bind (tcp->sck, saddr, len) == -1) 
		{
			//dev->stio->errnum = STIO_EINVAL; TODO:
			goto oops;
		}
	}

	return 0;

oops:
	if (tcp->sck != STIO_SCKHND_INVALID)
	{
		stio_closeasyncsck (tcp->sck);
		tcp->sck = STIO_SCKHND_INVALID;
	}
	return -1;
}

static int tcp_make_accepted (stio_dev_t* dev, void* ctx)
{
	stio_dev_tcp_t* tcp = (stio_dev_tcp_t*)dev;
	stio_syshnd_t* sck = (stio_syshnd_t*)ctx;

	tcp->sck = *sck;
	if (stio_makesckasync (tcp->sck) <= -1) return -1;

	return 0;
}


static void tcp_kill (stio_dev_t* dev)
{
	stio_dev_tcp_t* tcp = (stio_dev_tcp_t*)dev;

	if (tcp->state | (STIO_DEV_TCP_ACCEPTED | STIO_DEV_TCP_CONNECTED))
	{
		if (tcp->on_disconnected) tcp->on_disconnected (tcp);
	}

	if (tcp->sck != STIO_SCKHND_INVALID) 
	{
		stio_closeasyncsck (tcp->sck);
		tcp->sck = STIO_SCKHND_INVALID;
	}
}

static stio_syshnd_t tcp_getsyshnd (stio_dev_t* dev)
{
	stio_dev_tcp_t* tcp = (stio_dev_tcp_t*)dev;
	return (stio_syshnd_t)tcp->sck;
}


static int tcp_recv (stio_dev_t* dev, void* buf, stio_len_t* len)
{
	stio_dev_tcp_t* tcp = (stio_dev_tcp_t*)dev;
	int x;

printf ("TCP RECV...\n");
	x = recv (tcp->sck, buf, *len, 0);
	if (x <= -1)
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK) return 0;  /* no data available */
		return -1;
	}

	*len = x;
	return 1;
}

static int tcp_send (stio_dev_t* dev, const void* data, stio_len_t* len)
{
	stio_dev_tcp_t* tcp = (stio_dev_tcp_t*)tcp;
	ssize_t x;

#if 0
	x = sendto (tcp->sck, data, *len, skad, stio_getskadlen(skad));
	if (x <= -1) 
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK) return 0;  /* no data can be written */
		return -1;
	}

/* for UDP, if the data chunk can't be written at one go, it's actually a failure */
	if (x != *len) return -1; /* TODO: can i hava an indicator for this in stio? */

	*len = x;
#endif
	return 1;
}


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
			x = bind (tcp->sck, sa, sl);
			if (x == -1)
			{
				/* TODO: set dev->errnum from errno */
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
					if (stio_dev_event ((stio_dev_t*)tcp, STIO_DEV_EVENT_MOD, STIO_DEV_EVENT_IN | STIO_DEV_EVENT_OUT) >= 0)
					{
						tcp->state |= STIO_DEV_TCP_CONNECTING;
						tcp->peer = conn->addr;
						tcp->on_connected = conn->on_connected;
						tcp->on_disconnected = conn->on_disconnected;
						return 0;
					}
				}

				/* TODO: set dev->errnum from errno */
				return -1;
			}

			/* connected immediately */
			tcp->state |= STIO_DEV_TCP_CONNECTED;
			tcp->peer = conn->addr;
			tcp->on_connected = conn->on_connected;
			tcp->on_disconnected = conn->on_disconnected;
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
				/* TODO: set tcp->stio->errnum */
				return -1;
			}

			tcp->state |= STIO_DEV_TCP_LISTENING;
			tcp->on_accepted = lstn->on_accepted;
			tcp->on_disconnected = lstn->on_disconnected;
			return 0;
		#endif
		}
	}

	return 0;
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


static stio_dev_mth_t tcp_mth = 
{
	tcp_make,
	tcp_kill,
	tcp_getsyshnd,
	tcp_ioctl, 
	tcp_recv,
	tcp_send
};

/* accepted tcp socket */
static stio_dev_mth_t tcp_acc_mth =
{
	tcp_make_accepted,
	tcp_kill,
	tcp_getsyshnd,
	tcp_ioctl, 
	tcp_recv,
	tcp_send
};


/* ------------------------------------------------------------------------ */

static int tcp_ready (stio_dev_t* dev, int events)
{
	stio_dev_tcp_t* tcp = (stio_dev_tcp_t*)dev;
printf ("TCP READY...%p\n", dev);

	if (tcp->state & STIO_DEV_TCP_CONNECTING)
	{
		if (events & (STIO_DEV_EVENT_ERR | STIO_DEV_EVENT_HUP | STIO_DEV_EVENT_PRI | STIO_DEV_EVENT_IN))
		{
			int errcode;
			stio_scklen_t len;

			len = STIO_SIZEOF(errcode);
			if (getsockopt (tcp->sck, SOL_SOCKET, SO_ERROR, (char*)&errcode, &len) == 0)
			{
				printf ("CANNOT CONNECT ERRORCODE - %s\n", strerror(errcode));
			}

printf ("Cannot connect....\n");
			return -1;
		}
		else if (events & STIO_DEV_EVENT_OUT)
		{
			int errcode;
			stio_scklen_t len;

			STIO_ASSERT (!(tcp->state & STIO_DEV_TCP_CONNECTED));

	printf ("XXXXXXXXXXXXXXX CONNECTED...\n");
			len = STIO_SIZEOF(errcode);
			if (getsockopt (tcp->sck, SOL_SOCKET, SO_ERROR, (char*)&errcode, &len) == -1)
			{
			}
			else if (errcode == 0)
			{
				tcp->state &= ~STIO_DEV_TCP_CONNECTING;
				tcp->state |= STIO_DEV_TCP_CONNECTED;

				if (stio_dev_event ((stio_dev_t*)tcp, STIO_DEV_EVENT_MOD, STIO_DEV_EVENT_IN) <= -1)
				{
					printf ("CAANOT MANIPULTE EVENT ... KILL DEVICE...\n");
					return -1;
				}

				if (tcp->on_connected) tcp->on_connected (tcp);
			}
			else if (errcode == EINPROGRESS || errcode == EWOULDBLOCK)
			{
				/* still in progress */
			}
			else
			{
				printf ("failed to get SOCKET PROGRESS CODE...\n");
				return -1;
			}
		}

		return 0; /* success but don't invoke on_recv() */ 
	}
	else if (tcp->state & STIO_DEV_TCP_LISTENING)
	{
		stio_sckhnd_t clisck;
		stio_sckadr_t peer;
		stio_scklen_t addrlen;

		/* this is a server(lisening) socket */

		addrlen = STIO_SIZEOF(peer);
		clisck = accept (tcp->sck, (struct sockaddr*)&peer, &addrlen);
		if (clisck == -1)
		{
			if (errno == EINPROGRESS || errno == EWOULDBLOCK) return 0;

			/* TODO: set tcp->stio->errnum from errno */
			return -1;
		}

		if (tcp->on_accepted) 
		{
			stio_dev_tcp_t* clitcp;

			/* addr is the address of the peer */
			/* local addresss is inherited from the server */

			clitcp = (stio_dev_tcp_t*)stio_makedev (tcp->stio, STIO_SIZEOF(*tcp), &tcp_acc_mth, tcp->evcb, &clisck); 
			if (!clitcp) 
			{
				close (clisck);
				return -1;
			}

			clitcp->state |= STIO_DEV_TCP_ACCEPTED;
			clitcp->peer = peer;
			clitcp->parent = tcp;

			/* inherit the parent's on_disconnected() handler.
			 * you can still change it inside the on_accepted handler */
			clitcp->on_disconnected = tcp->on_disconnected; 

			tcp->on_accepted (tcp, clitcp);
		}
		else 
		{
			/* no on_accepted callback is set. close the client socket 
			 * without doing anything meaningful */
			close (clisck);
		}

		return 0; /* success but don't invoke on_recv() */ 
	}

printf ("READY WITH %d\n", events);


	if (events & (STIO_DEV_EVENT_ERR | STIO_DEV_EVENT_HUP))
	{
printf ("DISCONNECTED or ERROR \n");
		stio_killdev (dev->stio, dev);
		return 0;
	}

	return 1; /* the device is ok. carry on reading or writing */
}

static int tcp_on_recv (stio_dev_t* dev, const void* data, stio_len_t len)
{
	stio_dev_tcp_t* tcp = (stio_dev_tcp_t*)dev;

printf ("TCP dATA received %d bytes\n", (int)len);

	return 0;

}

static int tcp_on_sent (stio_dev_t* dev, const void* data, stio_len_t len)
{
	stio_dev_tcp_t* tcp = (stio_dev_tcp_t*)dev;

	/* TODO: do something */
printf ("TCP dATA sent %d bytes\n", (int)len);

	return 0;
}

static stio_dev_evcb_t tcp_evcb =
{
	tcp_ready,
	tcp_on_recv,
	tcp_on_sent
};


stio_dev_tcp_t* stio_dev_tcp_make (stio_t* stio, stio_sckadr_t* addr)
{
	stio_dev_tcp_t* tcp;

	tcp = (stio_dev_tcp_t*)stio_makedev (stio, STIO_SIZEOF(*tcp), &tcp_mth, &tcp_evcb, addr);

	return tcp;
}


void stio_dev_tcp_kill (stio_dev_tcp_t* tcp)
{
	stio_killdev (tcp->stio, (stio_dev_t*)tcp);
}
