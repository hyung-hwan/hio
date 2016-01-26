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


#include "stio.h"
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

static void* mmgr_alloc (stio_mmgr_t* mmgr, stio_size_t size)
{
	return malloc (size);
}

static void mmgr_free (stio_mmgr_t* mmgr, void* ptr)
{
	return free (ptr);
}

static stio_mmgr_t mmgr = 
{
	mmgr_alloc,
	mmgr_free,
	STIO_NULL
};



static void tcp_on_connected (stio_dev_tcp_t* tcp)
{
	printf ("REALLY CONNECTED.......\n");
}

static void tcp_on_disconnected (stio_dev_tcp_t* tcp)
{
	if (tcp->state & STIO_DEV_TCP_LISTENING)
	{
		printf ("SHUTTING DOWN THE SERVER SOCKET(%d)...\n", tcp->sck);
	}
	else if (tcp->state & STIO_DEV_TCP_CONNECTED)
	{
		printf ("CLIENT ORIGINATING FROM HERE GOT DISCONNECTED(%d).......\n", tcp->sck);
	}
	else if (tcp->state & STIO_DEV_TCP_ACCEPTED)
	{
		printf ("CLIENT BEING SERVED GOT DISCONNECTED(%d).......\n", tcp->sck);
	}
	else
	{
		printf ("TCP DISCONNECTED - THIS MUST NOT HAPPEN (%d)\n", tcp->sck);
	}
}
static void tcp_on_accepted (stio_dev_tcp_t* tcp, stio_dev_tcp_t* clitcp)
{
	printf ("device accepted client device... ....\n");
}


static stio_t* g_stio;

static void handle_signal (int sig)
{
	if (g_stio) stio_stop (g_stio);
}

int main ()
{

	stio_t* stio;
	stio_dev_udp_t* udp;
	stio_dev_tcp_t* tcp;
	struct sockaddr_in sin;
	struct sigaction sigact;
	stio_dev_tcp_connect_t tcp_conn;
	stio_dev_tcp_listen_t tcp_lstn;


	stio = stio_open (&mmgr, 0, STIO_NULL);
	if (!stio)
	{
		printf ("Cannot open stio\n");
		return -1;
	}

	g_stio = stio;

	STIO_MEMSET (&sigact, 0, STIO_SIZEOF(sigact));
	sigact.sa_flags = SA_RESTART;
	sigact.sa_handler = handle_signal;
	sigaction (SIGINT, &sigact, STIO_NULL);

/*
	pkt = stio_pkt_open  (packet type, protocol type); // packet socket 
	arp = stio_arp_open (binding_addr); // raw socket - arp filter
	tcpd = stio_tcp_open (binding_addr); // crude tcp
	udpd= stio_udp_open (binding_addr); // crude udp
	httpd = stio_httpd_open (binding_addr);
	httpsd = stio_httpsd_open (binding_addr);
	radiusd = stio_radiusd_open (binding_addr); // udp - radius

	stio_register (stio, httpd);
	stio_register (stio, httpsd);
	stio_register (stio, radiusd);
*/

	STIO_MEMSET (&sin, 0, STIO_SIZEOF(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(1234);
/*
	udp = (stio_dev_udp_t*)stio_makedev (stio, STIO_SIZEOF(*udp), &udp_mth, &udp_evcb, &sin);
	if (!udp)
	{
		printf ("Cannot make udp\n");
		goto oops;
	}
*/

	STIO_MEMSET (&sin, 0, STIO_SIZEOF(sin));
	sin.sin_family = AF_INET;
#if 0
	
	tcp = (stio_dev_tcp_t*)stio_makedev (stio, STIO_SIZEOF(*tcp), &tcp_mth, &tcp_evcb, &sin);
	if (!tcp)
	{
		printf ("Cannot make tcp\n");
		goto oops;
	}

	{
		struct sockaddr_in* p;
		p = (struct sockaddr_in*)&tcp_conn.addr;
		p->sin_family = AF_INET;
		p->sin_port = htons(9999);
		inet_pton (p->sin_family, "127.0.0.1", &p->sin_addr);
		tcp_conn.on_connected = tcp_on_connected;
		tcp_conn.on_disconnected = tcp_on_disconnected;
		//tcp_conn.on_failure = .... (error code? etc???) or on_connect to access success or failure??? what is better??
	}
	if (stio_dev_tcp_connect (tcp, &tcp_conn) <= -1)
	{
		printf ("stio_dev_tcp_connect() failed....\n");
		goto oops;
	}
#else
	sin.sin_port = htons(1234);
	tcp = stio_dev_tcp_make (stio, (stio_sckadr_t*)&sin);
	if (!tcp)
	{
		printf ("Cannot make tcp\n");
		goto oops;
	}

	tcp_lstn.backlogs = 100;
	tcp_lstn.on_accepted = tcp_on_accepted;
	tcp_lstn.on_disconnected = tcp_on_disconnected;
	if (stio_dev_tcp_listen (tcp, &tcp_lstn) <= -1)
	{
		printf ("stio_dev_tcp_listen() failed....\n");
		goto oops;
	}

#endif

	////////////////////////////

	stio_loop (stio);
	////////////////////////////

	g_stio = STIO_NULL;
	stio_close (stio);
	return 0;

oops:
	g_stio = STIO_NULL;
	stio_close (stio);
	return -1;
}
