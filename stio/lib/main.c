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
#include <stio-sck.h>
#include <stio-pro.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

#include <net/if.h>

#include <assert.h>

#if defined(HAVE_OPENSSL_SSL_H) && defined(HAVE_SSL)
#	include <openssl/ssl.h>
#	if defined(HAVE_OPENSSL_ERR_H)
#		include <openssl/err.h>
#	endif
#	if defined(HAVE_OPENSSL_ENGINE_H)
#		include <openssl/engine.h>
#	endif
#	define USE_SSL
#endif

/* ========================================================================= */

struct mmgr_stat_t
{
	stio_size_t total_count;
};

typedef struct mmgr_stat_t mmgr_stat_t;

static mmgr_stat_t mmgr_stat;

static void* mmgr_alloc (stio_mmgr_t* mmgr, stio_size_t size)
{
	void* x;

	if (((mmgr_stat_t*)mmgr->ctx)->total_count > 100)
	{
printf ("CRITICAL ERROR ---> too many heap chunks...\n");
		return STIO_NULL;
	}

	x = malloc (size);
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

#if defined(USE_SSL)
static void cleanup_openssl ()
{
	/* ERR_remove_state() should be called for each thread if the application is thread */
	ERR_remove_state (0);
#if defined(HAVE_ENGINE_CLEANUP)
	ENGINE_cleanup ();
#endif
	ERR_free_strings ();
	EVP_cleanup ();
#if defined(HAVE_CRYPTO_CLEANUP_ALL_EX_DATA)
	CRYPTO_cleanup_all_ex_data ();
#endif
}
#endif

struct tcp_server_t
{
	int tally;
};
typedef struct tcp_server_t tcp_server_t;

static void tcp_sck_on_disconnect (stio_dev_sck_t* tcp)
{
	switch (STIO_DEV_SCK_GET_PROGRESS(tcp))
	{
		case STIO_DEV_SCK_CONNECTING:
			printf ("OUTGOING SESSION DISCONNECTED - FAILED TO CONNECT (%d) TO REMOTE SERVER\n", (int)tcp->sck);
			break;

		case STIO_DEV_SCK_CONNECTING_SSL:
			printf ("OUTGOING SESSION DISCONNECTED - FAILED TO SSL-CONNECT (%d) TO REMOTE SERVER\n", (int)tcp->sck);
			break;

		case STIO_DEV_SCK_LISTENING:
			printf ("SHUTTING DOWN THE SERVER SOCKET(%d)...\n", (int)tcp->sck);
			break;

		case STIO_DEV_SCK_CONNECTED:
			printf ("OUTGOING CLIENT CONNECTION GOT TORN DOWN(%d).......\n", (int)tcp->sck);
			break;

		case STIO_DEV_SCK_ACCEPTING_SSL:
			printf ("INCOMING SSL-ACCEPT GOT DISCONNECTED(%d) ....\n", (int)tcp->sck);
			break;

		case STIO_DEV_SCK_ACCEPTED:
			printf ("INCOMING CLIENT BEING SERVED GOT DISCONNECTED(%d).......\n", (int)tcp->sck);
			break;

		default:
			printf ("SOCKET DEVICE DISCONNECTED (%d - %x)\n", (int)tcp->sck, (unsigned int)tcp->state);
			break;
	}
}
static int tcp_sck_on_connect (stio_dev_sck_t* tcp)
{

	stio_sckfam_t fam;
	stio_scklen_t len;
	stio_mchar_t buf1[128], buf2[128];

	memset (buf1, 0, STIO_SIZEOF(buf1));
	memset (buf2, 0, STIO_SIZEOF(buf2));

	stio_getsckaddrinfo (tcp->stio, &tcp->localaddr, &len, &fam);
	inet_ntop (fam, tcp->localaddr.data, buf1, STIO_COUNTOF(buf1));

	stio_getsckaddrinfo (tcp->stio, &tcp->remoteaddr, &len, &fam);
	inet_ntop (fam, tcp->remoteaddr.data, buf2, STIO_COUNTOF(buf2));

	if (tcp->state & STIO_DEV_SCK_CONNECTED)
	{

printf ("device connected to a remote server... LOCAL %s:%d REMOTE %s:%d.", buf1, stio_getsckaddrport(&tcp->localaddr), buf2, stio_getsckaddrport(&tcp->remoteaddr));

	}
	else if (tcp->state & STIO_DEV_SCK_ACCEPTED)
	{
printf ("device accepted client device... .LOCAL %s:%d REMOTE %s:%d\n", buf1, stio_getsckaddrport(&tcp->localaddr), buf2, stio_getsckaddrport(&tcp->remoteaddr));
	}

	return stio_dev_sck_write  (tcp, "hello", 5, STIO_NULL, STIO_NULL);
}

static int tcp_sck_on_write (stio_dev_sck_t* tcp, stio_iolen_t wrlen, void* wrctx, const stio_sckaddr_t* dstaddr)
{
	tcp_server_t* ts;

if (wrlen <= -1)
{
printf ("SEDING TIMED OUT...........\n");
	stio_dev_sck_halt(tcp);
}
else
{
	ts = (tcp_server_t*)(tcp + 1);
	printf (">>> SENT MESSAGE %d of length %ld\n", ts->tally, (long int)wrlen);

	ts->tally++;
//	if (ts->tally >= 2) stio_dev_sck_halt (tcp);

printf ("ENABLING READING..............................\n");
	stio_dev_sck_read (tcp, 1);

	//stio_dev_sck_timedread (tcp, 1, 1000);
}
	return 0;
}

static int tcp_sck_on_read (stio_dev_sck_t* tcp, const void* buf, stio_iolen_t len, const stio_sckaddr_t* srcaddr)
{
	int n;

	if (len <= 0)
	{
		printf ("STREAM DEVICE: EOF RECEIVED...\n");
		/* no outstanding request. but EOF */
		stio_dev_sck_halt (tcp);
		return 0;
	}

printf ("on read %d\n", (int)len);

{
stio_ntime_t tmout;

static char a ='A';
char* xxx = malloc (1000000);
memset (xxx, a++ ,1000000);

	//return stio_dev_sck_write  (tcp, "HELLO", 5, STIO_NULL);
	stio_inittime (&tmout, 5, 0);
	n = stio_dev_sck_timedwrite  (tcp, xxx, 1000000, &tmout, STIO_NULL, STIO_NULL);
free (xxx);


	if (n <= -1) return -1;
}


printf ("DISABLING READING..............................\n");
	stio_dev_sck_read (tcp, 0);

	/* post the write finisher */
	n = stio_dev_sck_write  (tcp, STIO_NULL, 0, STIO_NULL, STIO_NULL);
	if (n <= -1) return -1;

	return 0;

/* return 1; let the main loop to read more greedily without consulting the multiplexer */
}

/* ========================================================================= */

static void pro_on_close (stio_dev_pro_t* dev, stio_dev_pro_sid_t sid)
{
printf (">>>>>>>>>>>>> ON CLOSE OF SLAVE %d.\n", sid);
}

static int pro_on_read (stio_dev_pro_t* dev, const void* data, stio_iolen_t dlen, stio_dev_pro_sid_t sid)
{
printf ("PROCESS READ DATA on SLAVE[%d]... [%.*s]\n", (int)sid, (int)dlen, (char*)data);
	return 0;
}


static int pro_on_write (stio_dev_pro_t* dev, stio_iolen_t wrlen, void* wrctx)
{
printf ("PROCESS WROTE DATA...\n");
	return 0;
}

/* ========================================================================= */

static int arp_sck_on_read (stio_dev_sck_t* dev, const void* data, stio_iolen_t dlen, const stio_sckaddr_t* srcaddr)
{
	stio_etharp_pkt_t* eap;


	if (dlen < STIO_SIZEOF(*eap)) return 0; /* drop */

	eap = (stio_etharp_pkt_t*)data;

	printf ("ARP ON IFINDEX %d OPCODE: %d", stio_getsckaddrifindex(srcaddr), ntohs(eap->arphdr.opcode));

	printf (" SHA: %02X:%02X:%02X:%02X:%02X:%02X", eap->arppld.sha[0], eap->arppld.sha[1], eap->arppld.sha[2], eap->arppld.sha[3], eap->arppld.sha[4], eap->arppld.sha[5]);
	printf (" SPA: %d.%d.%d.%d", eap->arppld.spa[0], eap->arppld.spa[1], eap->arppld.spa[2], eap->arppld.spa[3]);
	printf (" THA: %02X:%02X:%02X:%02X:%02X:%02X", eap->arppld.tha[0], eap->arppld.tha[1], eap->arppld.tha[2], eap->arppld.tha[3], eap->arppld.tha[4], eap->arppld.tha[5]);
	printf (" TPA: %d.%d.%d.%d", eap->arppld.tpa[0], eap->arppld.tpa[1], eap->arppld.tpa[2], eap->arppld.tpa[3]);
	printf ("\n");
	return 0;
}

static int arp_sck_on_write (stio_dev_sck_t* dev, stio_iolen_t wrlen, void* wrctx, const stio_sckaddr_t* dstaddr)
{
	return 0;
}

static void arp_sck_on_disconnect (stio_dev_sck_t* dev)
{
printf ("SHUTTING DOWN ARP SOCKET %d...\n", dev->sck);
}

static int setup_arp_tester (stio_t* stio)
{
	stio_sckaddr_t ethdst;
	stio_etharp_pkt_t etharp;
	stio_dev_sck_make_t sck_make;
	stio_dev_sck_t* sck;

	memset (&sck_make, 0, STIO_SIZEOF(sck_make));
	sck_make.type = STIO_DEV_SCK_ARP;
	//sck_make.type = STIO_DEV_SCK_ARP_DGRAM;
	sck_make.on_write = arp_sck_on_write;
	sck_make.on_read = arp_sck_on_read;
	sck_make.on_disconnect = arp_sck_on_disconnect;
	sck = stio_dev_sck_make (stio, 0, &sck_make);
	if (!sck)
	{
		printf ("Cannot make socket device\n");
		return -1;
	}

	//stio_sckaddr_initforeth (&ethdst, if_nametoindex("enp0s25.3"), (stio_ethaddr_t*)"\xFF\xFF\xFF\xFF\xFF\xFF");
	stio_sckaddr_initforeth (&ethdst, if_nametoindex("enp0s25.3"), (stio_ethaddr_t*)"\xAA\xBB\xFF\xCC\xDD\xFF");

	memset (&etharp, 0, sizeof(etharp));

	memcpy (etharp.ethhdr.source, "\xB8\x6B\x23\x9C\x10\x76", STIO_ETHADDR_LEN);
	//memcpy (etharp.ethhdr.dest, "\xFF\xFF\xFF\xFF\xFF\xFF", STIO_ETHADDR_LEN);
	memcpy (etharp.ethhdr.dest, "\xAA\xBB\xFF\xCC\xDD\xFF", STIO_ETHADDR_LEN);
	etharp.ethhdr.proto = STIO_CONST_HTON16(STIO_ETHHDR_PROTO_ARP);

	etharp.arphdr.htype = STIO_CONST_HTON16(STIO_ARPHDR_HTYPE_ETH);
	etharp.arphdr.ptype = STIO_CONST_HTON16(STIO_ARPHDR_PTYPE_IP4);
	etharp.arphdr.hlen = STIO_ETHADDR_LEN;
	etharp.arphdr.plen = STIO_IP4ADDR_LEN;
	etharp.arphdr.opcode = STIO_CONST_HTON16(STIO_ARPHDR_OPCODE_REQUEST);

	memcpy (etharp.arppld.sha, "\xB8\x6B\x23\x9C\x10\x76", STIO_ETHADDR_LEN);

	if (stio_dev_sck_write (sck, &etharp, sizeof(etharp), NULL, &ethdst) <= -1)
	//if (stio_dev_sck_write (sck, &etharp.arphdr, sizeof(etharp) - sizeof(etharp.ethhdr), NULL, &ethaddr) <= -1)
	{
		printf ("CANNOT WRITE ARP...\n");
	}


	return 0;
}

/* ========================================================================= */

struct icmpxtn_t
{
	stio_uint16_t icmp_seq;
	stio_tmridx_t tmout_jobidx;
	int reply_received;
};

typedef struct icmpxtn_t icmpxtn_t;

static int schedule_icmp_wait (stio_dev_sck_t* dev);

static void send_icmp (stio_dev_sck_t* dev, stio_uint16_t seq)
{
	stio_sckaddr_t dstaddr;
	stio_ip4addr_t ia;
	stio_icmphdr_t* icmphdr;
	stio_uint8_t buf[512];

	inet_pton (AF_INET, "192.168.1.131", &ia);
	stio_sckaddr_initforip4 (&dstaddr, 0, &ia);

	memset(buf, 0, STIO_SIZEOF(buf));
	icmphdr = (stio_icmphdr_t*)buf;
	icmphdr->type = STIO_ICMP_ECHO_REQUEST;
	icmphdr->u.echo.id = STIO_CONST_HTON16(100);
	icmphdr->u.echo.seq = stio_hton16(seq);

	memset (&buf[STIO_SIZEOF(*icmphdr)], 'A', STIO_SIZEOF(buf) - STIO_SIZEOF(*icmphdr));
	icmphdr->checksum = stio_checksumip (icmphdr, STIO_SIZEOF(buf));

	if (stio_dev_sck_write (dev, buf, STIO_SIZEOF(buf), NULL, &dstaddr) <= -1)
	{
		printf ("CANNOT WRITE ICMP...\n");
		stio_dev_sck_halt (dev);
	}

	if (schedule_icmp_wait (dev) <= -1)
	{
		printf ("CANNOT SCHEDULE ICMP WAIT...\n");
		stio_dev_sck_halt (dev);
	}
}

static void on_icmp_due (stio_t* stio, const stio_ntime_t* now, stio_tmrjob_t* tmrjob)
{
	stio_dev_sck_t* dev;
	icmpxtn_t* icmpxtn;

	dev = tmrjob->ctx;
	icmpxtn = (icmpxtn_t*)(dev + 1);

	if (icmpxtn->reply_received)
		icmpxtn->reply_received = 0;
	else
		printf ("NO ICMP REPLY RECEIVED....\n");

	send_icmp (dev, ++icmpxtn->icmp_seq);
}

static int schedule_icmp_wait (stio_dev_sck_t* dev)
{
	icmpxtn_t* icmpxtn;
	stio_tmrjob_t tmrjob;
	stio_ntime_t fire_after;

	icmpxtn = (icmpxtn_t*)(dev + 1);
	stio_inittime (&fire_after, 2, 0);

	memset (&tmrjob, 0, STIO_SIZEOF(tmrjob));
	tmrjob.ctx = dev;
	stio_gettime (&tmrjob.when);
	stio_addtime (&tmrjob.when, &fire_after, &tmrjob.when);
	tmrjob.handler = on_icmp_due;
	tmrjob.idxptr = &icmpxtn->tmout_jobidx;

	assert (icmpxtn->tmout_jobidx == STIO_TMRIDX_INVALID);

	return (stio_instmrjob (dev->stio, &tmrjob) == STIO_TMRIDX_INVALID)? -1: 0;
}

static int icmp_sck_on_read (stio_dev_sck_t* dev, const void* data, stio_iolen_t dlen, const stio_sckaddr_t* srcaddr)
{
	icmpxtn_t* icmpxtn;
	stio_iphdr_t* iphdr;
	stio_icmphdr_t* icmphdr;

	/* when received, the data contains the IP header.. */
	icmpxtn = (icmpxtn_t*)(dev + 1);

	if (dlen < STIO_SIZEOF(*iphdr) + STIO_SIZEOF(*icmphdr))
	{
		printf ("INVALID ICMP PACKET.. TOO SHORT...%d\n", (int)dlen);
	}
	else
	{
		/* TODO: consider IP options... */
		iphdr = (stio_iphdr_t*)data;
		icmphdr = (stio_icmphdr_t*)((stio_uint8_t*)data + (iphdr->ihl * 4));

		/* TODO: check srcaddr against target */

		if (icmphdr->type == STIO_ICMP_ECHO_REPLY && 
		    stio_ntoh16(icmphdr->u.echo.seq) == icmpxtn->icmp_seq) /* TODO: more check.. echo.id.. */
		{
			icmpxtn->reply_received = 1;
			printf ("ICMP REPLY RECEIVED...ID %d SEQ %d\n", (int)stio_ntoh16(icmphdr->u.echo.id), (int)stio_ntoh16(icmphdr->u.echo.seq));
		}
		else
		{
			printf ("GARBAGE ICMP PACKET...LEN %d SEQ %d,%d\n", (int)dlen, (int)icmpxtn->icmp_seq, (int)stio_ntoh16(icmphdr->u.echo.seq));
		}
	}
	return 0;
}


static int icmp_sck_on_write (stio_dev_sck_t* dev, stio_iolen_t wrlen, void* wrctx, const stio_sckaddr_t* dstaddr)
{
	/*icmpxtn_t* icmpxtn;

	icmpxtn = (icmpxtn_t*)(dev + 1); */

	return 0;
}

static void icmp_sck_on_disconnect (stio_dev_sck_t* dev)
{
	icmpxtn_t* icmpxtn;

	icmpxtn = (icmpxtn_t*)(dev + 1);

printf ("SHUTTING DOWN ICMP SOCKET %d...\n", dev->sck);
	if (icmpxtn->tmout_jobidx != STIO_TMRIDX_INVALID)
	{

		stio_deltmrjob (dev->stio, icmpxtn->tmout_jobidx);
		icmpxtn->tmout_jobidx = STIO_TMRIDX_INVALID;
	}
}

static int setup_ping4_tester (stio_t* stio)
{
	stio_dev_sck_make_t sck_make;
	stio_dev_sck_t* sck;
	icmpxtn_t* icmpxtn;

	memset (&sck_make, 0, STIO_SIZEOF(sck_make));
	sck_make.type = STIO_DEV_SCK_ICMP4;
	sck_make.on_write = icmp_sck_on_write;
	sck_make.on_read = icmp_sck_on_read;
	sck_make.on_disconnect = icmp_sck_on_disconnect;

	sck = stio_dev_sck_make (stio, STIO_SIZEOF(icmpxtn_t), &sck_make);
	if (!sck)
	{
		printf ("Cannot make ICMP4 socket device\n");
		return -1;
	}

	icmpxtn = (icmpxtn_t*)(sck + 1);
	icmpxtn->tmout_jobidx = STIO_TMRIDX_INVALID;
	icmpxtn->icmp_seq = 0;

	/*TODO: stio_dev_sck_setbroadcast (sck, 1);*/

	send_icmp (sck, ++icmpxtn->icmp_seq);

	return 0;
}


/* ========================================================================= */

static stio_t* g_stio;

static void handle_signal (int sig)
{
	if (g_stio) stio_stop (g_stio, STIO_STOPREQ_TERMINATION);
}

int main ()
{
	int i;

	stio_t* stio;
	stio_dev_sck_t* tcp[3];

	struct sigaction sigact;
	stio_dev_sck_connect_t tcp_conn;
	stio_dev_sck_listen_t tcp_lstn;
	stio_dev_sck_bind_t tcp_bind;
	stio_dev_sck_make_t tcp_make;

	tcp_server_t* ts;

#if defined(USE_SSL)
	SSL_load_error_strings ();
	SSL_library_init ();
#endif

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

/*
	memset (&sigact, 0, STIO_SIZEOF(sigact));
	sigact.sa_handler = SIG_IGN;
	sigaction (SIGCHLD, &sigact, STIO_NULL);
*/

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

	memset (&tcp_make, 0, STIO_SIZEOF(&tcp_make));
	tcp_make.type = STIO_DEV_SCK_TCP4;
	tcp_make.on_write = tcp_sck_on_write;
	tcp_make.on_read = tcp_sck_on_read;
	tcp_make.on_disconnect = tcp_sck_on_disconnect;
	tcp[0] = stio_dev_sck_make (stio, STIO_SIZEOF(tcp_server_t), &tcp_make);
	if (!tcp[0])
	{
		printf ("Cannot make tcp\n");
		goto oops;
	}

	ts = (tcp_server_t*)(tcp[0] + 1);
	ts->tally = 0;


	memset (&tcp_conn, 0, STIO_SIZEOF(tcp_conn));
{
	in_addr_t ia = inet_addr("192.168.1.119");
	stio_sckaddr_initforip4 (&tcp_conn.remoteaddr, 9999, (stio_ip4addr_t*)&ia);
}

	stio_inittime (&tcp_conn.connect_tmout, 5, 0);
	tcp_conn.on_connect = tcp_sck_on_connect;
	tcp_conn.options = STIO_DEV_SCK_CONNECT_SSL;
	if (stio_dev_sck_connect (tcp[0], &tcp_conn) <= -1)
	{
		printf ("stio_dev_sck_connect() failed....\n");
		/* carry on regardless of failure */
	}

	/* -------------------------------------------------------------- */
	memset (&tcp_make, 0, STIO_SIZEOF(&tcp_make));
	tcp_make.type = STIO_DEV_SCK_TCP4;
	tcp_make.on_write = tcp_sck_on_write;
	tcp_make.on_read = tcp_sck_on_read;
	tcp_make.on_disconnect = tcp_sck_on_disconnect;

	tcp[1] = stio_dev_sck_make (stio, STIO_SIZEOF(tcp_server_t), &tcp_make);
	if (!tcp[1])
	{
		printf ("Cannot make tcp\n");
		goto oops;
	}
	ts = (tcp_server_t*)(tcp[1] + 1);
	ts->tally = 0;

	memset (&tcp_bind, 0, STIO_SIZEOF(tcp_bind));
	stio_sckaddr_initforip4 (&tcp_bind.localaddr, 1234, STIO_NULL);
	tcp_bind.options = STIO_DEV_SCK_BIND_REUSEADDR;

	if (stio_dev_sck_bind (tcp[1],&tcp_bind) <= -1)
	{
		printf ("stio_dev_sck_bind() failed....\n");
		goto oops;
	}


	tcp_lstn.backlogs = 100;
	tcp_lstn.on_connect = tcp_sck_on_connect;
	if (stio_dev_sck_listen (tcp[1], &tcp_lstn) <= -1)
	{
		printf ("stio_dev_sck_listen() failed....\n");
		goto oops;
	}

	/* -------------------------------------------------------------- */
	memset (&tcp_make, 0, STIO_SIZEOF(&tcp_make));
	tcp_make.type = STIO_DEV_SCK_TCP4;
	tcp_make.on_write = tcp_sck_on_write;
	tcp_make.on_read = tcp_sck_on_read;
	tcp_make.on_disconnect = tcp_sck_on_disconnect;

	tcp[2] = stio_dev_sck_make (stio, STIO_SIZEOF(tcp_server_t), &tcp_make);
	if (!tcp[2])
	{
		printf ("Cannot make tcp\n");
		goto oops;
	}
	ts = (tcp_server_t*)(tcp[2] + 1);
	ts->tally = 0;

	memset (&tcp_bind, 0, STIO_SIZEOF(tcp_bind));
	stio_sckaddr_initforip4 (&tcp_bind.localaddr, 1235, STIO_NULL);
	tcp_bind.options = STIO_DEV_SCK_BIND_REUSEADDR | /*STIO_DEV_SCK_BIND_REUSEPORT |*/ STIO_DEV_SCK_BIND_SSL; 
	tcp_bind.ssl_certfile = STIO_MT("localhost.crt");
	tcp_bind.ssl_keyfile = STIO_MT("localhost.key");
	stio_inittime (&tcp_bind.accept_tmout, 5, 1);

	if (stio_dev_sck_bind (tcp[2],&tcp_bind) <= -1)
	{
		printf ("stio_dev_sck_bind() failed....\n");
		goto oops;
	}

	tcp_lstn.backlogs = 100;
	tcp_lstn.on_connect = tcp_sck_on_connect;
	if (stio_dev_sck_listen (tcp[2], &tcp_lstn) <= -1)
	{
		printf ("stio_dev_sck_listen() failed....\n");
		goto oops;
	}

	//stio_dev_sck_sendfile (tcp[2], fd, offset, count);

	if (setup_arp_tester(stio) <= -1) goto oops;
	if (setup_ping4_tester(stio) <= -1) goto oops;


for (i = 0; i < 5; i++)
{
	stio_dev_pro_t* pro;
	stio_dev_pro_make_t pro_make;

	memset (&pro_make, 0, STIO_SIZEOF(pro_make));
	pro_make.flags = STIO_DEV_PRO_READOUT | STIO_DEV_PRO_READERR | STIO_DEV_PRO_WRITEIN /*| STIO_DEV_PRO_FORGET_CHILD*/;
	//pro_make.cmd = "/bin/ls -laF /usr/bin";
	//pro_make.cmd = "/bin/ls -laF";
	pro_make.cmd = "./a";
	pro_make.on_read = pro_on_read;
	pro_make.on_write = pro_on_write;
	pro_make.on_close = pro_on_close;

	pro = stio_dev_pro_make (stio, 0, &pro_make);
	if (!pro)
	{
		printf ("CANNOT CREATE PROCESS PIPE\n");
		goto oops;
	}

	stio_dev_pro_write (pro, "MY STIO LIBRARY\n", 16, STIO_NULL);
//stio_dev_pro_killchild (pro); 
//stio_dev_pro_close (pro, STIO_DEV_PRO_IN); 
//stio_dev_pro_close (pro, STIO_DEV_PRO_OUT); 
//stio_dev_pro_close (pro, STIO_DEV_PRO_ERR); 
}

	stio_loop (stio);

	g_stio = STIO_NULL;
	stio_close (stio);
#if defined(USE_SSL)
	cleanup_openssl ();
#endif

	return 0;

oops:
	g_stio = STIO_NULL;
	stio_close (stio);
#if defined(USE_SSL)
	cleanup_openssl ();
#endif
	return -1;
}
