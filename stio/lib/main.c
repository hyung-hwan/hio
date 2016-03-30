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
#include <stio-arp.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

#include <netpacket/packet.h>
#include <net/if.h>

/* ========================================================================= */

struct mmgr_stat_t
{
	stio_size_t total_count;
};

typedef struct mmgr_stat_t mmgr_stat_t;

static mmgr_stat_t mmgr_stat;

static void* mmgr_alloc (stio_mmgr_t* mmgr, stio_size_t size)
{
	if (((mmgr_stat_t*)mmgr->ctx)->total_count > 100)
	{
printf ("CRITICAL ERROR ---> too many heap chunks...\n");
		return STIO_NULL;
	}

	void* x = malloc (size);
	if (x) ((mmgr_stat_t*)mmgr->ctx)->total_count++;
	return x;
}

static void* mmgr_realloc (stio_mmgr_t* mmgr, void* ptr, stio_size_t size)
{
	return realloc (ptr, size);
}

static void mmgr_free (stio_mmgr_t* mmgr, void* ptr)
{
	((mmgr_stat_t*)mmgr->ctx)->total_count--;
	return free (ptr);
}


static stio_mmgr_t mmgr = 
{
	mmgr_alloc,
	mmgr_realloc,
	mmgr_free,
	&mmgr_stat
};

/* ========================================================================= */

struct tcp_server_t
{
	int tally;
};
typedef struct tcp_server_t tcp_server_t;

static void tcp_on_disconnect (stio_dev_tcp_t* tcp)
{
	if (tcp->state & STIO_DEV_TCP_CONNECTING)
	{
		printf ("TCP DISCONNECTED - FAILED TO CONNECT (%d) TO REMOTE SERVER\n", (int)tcp->sck);
	}
	else if (tcp->state & STIO_DEV_TCP_LISTENING)
	{
		printf ("SHUTTING DOWN THE SERVER SOCKET(%d)...\n", (int)tcp->sck);
	}
	else if (tcp->state & STIO_DEV_TCP_CONNECTED)
	{
		printf ("CLIENT ORIGINATING FROM HERE GOT DISCONNECTED(%d).......\n", (int)tcp->sck);
	}
	else if (tcp->state & STIO_DEV_TCP_ACCEPTED)
	{
		printf ("CLIENT BEING SERVED GOT DISCONNECTED(%d).......\n", (int)tcp->sck);
	}
	else
	{
		printf ("TCP DISCONNECTED - THIS MUST NOT HAPPEN (%d - %x)\n", (int)tcp->sck, (unsigned int)tcp->state);
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


static int tcp_on_write (stio_dev_tcp_t* tcp, stio_len_t wrlen, void* wrctx)
{
	tcp_server_t* ts;

if (wrlen <= -1)
{
printf ("SEDING TIMED OUT...........\n");
	stio_dev_tcp_halt(tcp);
}
else
{
	ts = (tcp_server_t*)(tcp + 1);
	printf (">>> SENT MESSAGE %d of length %ld\n", ts->tally, (long int)wrlen);

	ts->tally++;
//	if (ts->tally >= 2) stio_dev_tcp_halt (tcp);

printf ("ENABLING READING..............................\n");
	stio_dev_tcp_read (tcp, 1);
}
	return 0;
}

static int tcp_on_read (stio_dev_tcp_t* tcp, const void* buf, stio_len_t len)
{
	if (len <= 0)
	{
		printf ("STREAM DEVICE: EOF RECEIVED...\n");
		/* no outstanding request. but EOF */
		stio_dev_tcp_halt (tcp);
		return 0;
	}

printf ("on read %d\n", (int)len);

stio_ntime_t tmout;
int n;
static char a ='A';
char* xxx = malloc (1000000);
memset (xxx, a++ ,1000000);
	//return stio_dev_tcp_write  (tcp, "HELLO", 5, STIO_NULL);
	stio_inittime (&tmout, 1, 0);
	n = stio_dev_tcp_timedwrite  (tcp, xxx, 1000000, &tmout, STIO_NULL);
free (xxx);

	if (n <= -1) return -1;

	/* post the write finisher */
	n = stio_dev_tcp_write  (tcp, STIO_NULL, 0, STIO_NULL);
	if (n <= -1) return -1;

printf ("DISABLING READING..............................\n");
	stio_dev_tcp_read (tcp, 0);
	return 0;

/* return 1; let the main loop to read more greedily without consulting the multiplexer */
}

/* ========================================================================= */

static int arp_sck_on_read (stio_dev_sck_t* dev, const void* data, stio_len_t dlen, const stio_adr_t* srcadr)
{
	stio_etharp_pkt_t* eap;

	if (dlen < STIO_SIZEOF(*eap)) return 0; /* drop */

	eap = (stio_etharp_pkt_t*)data;
	printf ("ARP OPCODE: %d", ntohs(eap->arphdr.opcode));
	printf (" SHA: %02X:%02X:%02X:%02X:%02X:%02X", eap->arppld.sha[0], eap->arppld.sha[1], eap->arppld.sha[2], eap->arppld.sha[3], eap->arppld.sha[4], eap->arppld.sha[5]);
	printf (" SPA: %d.%d.%d.%d", eap->arppld.spa[0], eap->arppld.spa[1], eap->arppld.spa[2], eap->arppld.spa[3]);
	printf (" THA: %02X:%02X:%02X:%02X:%02X:%02X", eap->arppld.tha[0], eap->arppld.tha[1], eap->arppld.tha[2], eap->arppld.tha[3], eap->arppld.tha[4], eap->arppld.tha[5]);
	printf (" TPA: %d.%d.%d.%d", eap->arppld.tpa[0], eap->arppld.tpa[1], eap->arppld.tpa[2], eap->arppld.tpa[3]);
	printf ("\n");
	return 0;
}

static int arp_sck_on_write (stio_dev_sck_t* dev, stio_len_t wrlen, void* wrctx)
{
	return 0;
}


/* ========================================================================= */

static stio_t* g_stio;

static void handle_signal (int sig)
{
	if (g_stio) stio_stop (g_stio);
}

int main ()
{

	stio_t* stio;
	stio_dev_udp_t* udp;
	stio_dev_sck_t* sck;
	stio_dev_tcp_t* tcp[2];
	struct sockaddr_in sin;
	struct sigaction sigact;
	stio_dev_tcp_connect_t tcp_conn;
	stio_dev_tcp_listen_t tcp_lstn;
	stio_dev_tcp_make_t tcp_make;
	stio_dev_sck_make_t sck_make;
	tcp_server_t* ts;

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
	tcp[0] = stio_dev_tcp_make (stio, STIO_SIZEOF(tcp_server_t), &tcp_make);
	if (!tcp[0])
	{
		printf ("Cannot make tcp\n");
		goto oops;
	}

	ts = (tcp_server_t*)(tcp[0] + 1);
	ts->tally = 0;

	memset (&sin, 0, STIO_SIZEOF(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(9999);
	inet_pton (sin.sin_family, "192.168.1.4", &sin.sin_addr);
	//inet_pton (sin.sin_family, "127.0.0.1", &sin.sin_addr);

	memset (&tcp_conn, 0, STIO_SIZEOF(tcp_conn));
	memcpy (&tcp_conn.addr, &sin, STIO_SIZEOF(sin));
	tcp_conn.tmout.sec = 5;
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

	tcp[1] = stio_dev_tcp_make (stio, STIO_SIZEOF(tcp_server_t), &tcp_make);
	if (!tcp[1])
	{
		printf ("Cannot make tcp\n");
		goto oops;
	}
	ts = (tcp_server_t*)(tcp[1] + 1);
	ts->tally = 0;

	tcp_lstn.backlogs = 100;
	tcp_lstn.on_connect = tcp_on_connect;
	tcp_lstn.on_disconnect = tcp_on_disconnect;
	if (stio_dev_tcp_listen (tcp[1], &tcp_lstn) <= -1)
	{
		printf ("stio_dev_tcp_listen() failed....\n");
		goto oops;
	}


	memset (&sck_make, 0, STIO_SIZEOF(sck_make));
	sck_make.type = STIO_DEV_SCK_ARP;
	//sck_make.type = STIO_DEV_SCK_ARP_DGRAM;
	sck_make.on_write = arp_sck_on_write;
	sck_make.on_read = arp_sck_on_read;
	sck = stio_dev_sck_make (stio, 0, &sck_make);
	if (!sck)
	{
		printf ("Cannot make socket device\n");
		goto oops;
	}

#if 1
{
	stio_etharp_pkt_t etharp;
	struct sockaddr_ll sll;
	stio_adr_t adr;

	memset (&sll, 0, sizeof(sll));
	adr.ptr = &sll;
	adr.len = sizeof(sll);
	sll.sll_family = AF_PACKET;
//	sll.sll_protocol = STIO_CONST_HTON16(STIO_ETHHDR_PROTO_ARP);
//	sll.sll_hatype = STIO_CONST_HTON16(STIO_ARPHDR_HTYPE_ETH);
	sll.sll_halen = STIO_ETHADR_LEN;
	memcpy (sll.sll_addr, "\xFF\xFF\xFF\xFF\xFF\xFF", sll.sll_halen);
	sll.sll_ifindex = if_nametoindex ("enp0s25.3");

	/* if unicast ... */
	//sll.sll_pkttype = PACKET_OTHERHOST;
//	sll.sll_pkttype = PACKET_BROADCAST;

	memset (&etharp, 0, sizeof(etharp));

	memcpy (etharp.ethhdr.source, "\xB8\x6B\x23\x9C\x10\x76", STIO_ETHADR_LEN);
	memcpy (etharp.ethhdr.dest, "\xFF\xFF\xFF\xFF\xFF\xFF", STIO_ETHADR_LEN);
	etharp.ethhdr.proto = STIO_CONST_HTON16(STIO_ETHHDR_PROTO_ARP);

	etharp.arphdr.htype = STIO_CONST_HTON16(STIO_ARPHDR_HTYPE_ETH);
	etharp.arphdr.ptype = STIO_CONST_HTON16(STIO_ARPHDR_PTYPE_IP4);
	etharp.arphdr.hlen = STIO_ETHADR_LEN;
	etharp.arphdr.plen = STIO_IP4ADR_LEN;
	etharp.arphdr.opcode = STIO_CONST_HTON16(STIO_ARPHDR_OPCODE_REQUEST);

	memcpy (etharp.arppld.sha, "\xB8\x6B\x23\x9C\x10\x76", STIO_ETHADR_LEN);

	if (stio_dev_sck_write (sck, &etharp, sizeof(etharp), NULL, &adr) <= -1)
	//if (stio_dev_sck_write (sck, &etharp.arphdr, sizeof(etharp) - sizeof(etharp.ethhdr), NULL, &adr) <= -1)
	{
		printf ("CANNOT WRITE ARP...\n");
	}
}
#endif

	stio_loop (stio);

	g_stio = STIO_NULL;
	stio_close (stio);

	return 0;

oops:
	g_stio = STIO_NULL;
	stio_close (stio);
	return -1;
}
