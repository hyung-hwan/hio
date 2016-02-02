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


#include <stio.h>
#include <stio-tcp.h>
#include <stio-udp.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

static void* mmgr_alloc (stio_mmgr_t* mmgr, stio_size_t size)
{
	return malloc (size);
}

static void* mmgr_realloc (stio_mmgr_t* mmgr, void* ptr, stio_size_t size)
{
	return realloc (ptr, size);
}

static void mmgr_free (stio_mmgr_t* mmgr, void* ptr)
{
	return free (ptr);
}

static stio_mmgr_t mmgr = 
{
	mmgr_alloc,
	mmgr_realloc,
	mmgr_free,
	STIO_NULL
};

static void tcp_on_disconnect (stio_dev_tcp_t* tcp)
{
	if (tcp->state & STIO_DEV_TCP_CONNECTING)
	{
		printf ("TCP DISCONNECTED - FAILED TO CONNECT (%d) TO REMOTE SERVER\n", tcp->sck);
	}
	else if (tcp->state & STIO_DEV_TCP_LISTENING)
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
static int tcp_on_connect (stio_dev_tcp_t* tcp)
{

	if (tcp->state & STIO_DEV_TCP_CONNECTED)
	{
printf ("device connected to a remote server... .asdfjkasdfkljasdlfkjasdj...\n");
	}
	else if (tcp->state & STIO_DEV_TCP_ACCEPTED)
	{
printf ("device accepted client device... .asdfjkasdfkljasdlfkjasdj...\n");
	}

	return stio_dev_tcp_write  (tcp, "hello", 5, STIO_NULL);
}


static int tcp_on_write (stio_dev_tcp_t* tcp, void* wrctx)
{
	printf (">>> TCP SENT MESSAGE\n");
	return 0;
}

static int tcp_on_read (stio_dev_tcp_t* tcp, const void* buf, stio_len_t len)
{
int n;
static char a ='A';
char* xxx = malloc (1000000);
memset (xxx, a++ ,1000000);
	//return stio_dev_tcp_write  (tcp, "HELLO", 5, STIO_NULL);
	n = stio_dev_tcp_write  (tcp, xxx, 1000000, STIO_NULL);
free (xxx);
return n;
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
	stio_dev_tcp_t* tcp[2];
	struct sockaddr_in sin;
	struct sigaction sigact;
	stio_dev_tcp_connect_t tcp_conn;
	stio_dev_tcp_listen_t tcp_lstn;
	stio_dev_tcp_make_t tcp_make;

	stio = stio_open (&mmgr, 0, 512, STIO_NULL);
	if (!stio)
	{
		printf ("Cannot open stio\n");
		return -1;
	}

	g_stio = stio;

	memset (&sigact, 0, STIO_SIZEOF(sigact));
	sigact.sa_flags = SA_RESTART;
	sigact.sa_handler = handle_signal;
	sigaction (SIGINT, &sigact, STIO_NULL);

	memset (&sigact, 0, STIO_SIZEOF(sigact));
	sigact.sa_handler = SIG_IGN;
	sigaction (SIGPIPE, &sigact, STIO_NULL);

	/*memset (&sin, 0, STIO_SIZEOF(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(1234); */
/*
	udp = (stio_dev_udp_t*)stio_makedev (stio, STIO_SIZEOF(*udp), &udp_mth, &udp_evcb, &sin);
	if (!udp)
	{
		printf ("Cannot make udp\n");
		goto oops;
	}
*/

	memset (&sin, 0, STIO_SIZEOF(sin));
	sin.sin_family = AF_INET;
	memset (&tcp_make, 0, STIO_SIZEOF(&tcp_make));
	memcpy (&tcp_make.addr, &sin, STIO_SIZEOF(sin));
	tcp_make.on_write = tcp_on_write;
	tcp_make.on_read = tcp_on_read;
	tcp[0] = stio_dev_tcp_make (stio, 0, &tcp_make);
	if (!tcp[0])
	{
		printf ("Cannot make tcp\n");
		goto oops;
	}


	memset (&sin, 0, STIO_SIZEOF(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(9999);
	inet_pton (sin.sin_family, "192.168.1.1", &sin.sin_addr);
	//inet_pton (sin.sin_family, "127.0.0.1", &sin.sin_addr);

	memset (&tcp_conn, 0, STIO_SIZEOF(tcp_conn));
	memcpy (&tcp_conn.addr, &sin, STIO_SIZEOF(sin));
	tcp_conn.timeout.sec = 5;
	tcp_conn.on_connect = tcp_on_connect;
	tcp_conn.on_disconnect = tcp_on_disconnect;
	if (stio_dev_tcp_connect (tcp[0], &tcp_conn) <= -1)
	{
		printf ("stio_dev_tcp_connect() failed....\n");
		goto oops;
	}

	memset (&sin, 0, STIO_SIZEOF(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(1234);
	memset (&tcp_make, 0, STIO_SIZEOF(&tcp_make));
	memcpy (&tcp_make.addr, &sin, STIO_SIZEOF(sin));
	tcp_make.on_write = tcp_on_write;
	tcp_make.on_read = tcp_on_read;

	tcp[1] = stio_dev_tcp_make (stio, 0, &tcp_make);
	if (!tcp[1])
	{
		printf ("Cannot make tcp\n");
		goto oops;
	}

	tcp_lstn.backlogs = 100;
	tcp_lstn.on_connect = tcp_on_connect;
	tcp_lstn.on_disconnect = tcp_on_disconnect;
	if (stio_dev_tcp_listen (tcp[1], &tcp_lstn) <= -1)
	{
		printf ("stio_dev_tcp_listen() failed....\n");
		goto oops;
	}


	stio_loop (stio);

	g_stio = STIO_NULL;
	stio_close (stio);

	return 0;

oops:
	g_stio = STIO_NULL;
	stio_close (stio);
	return -1;
}
