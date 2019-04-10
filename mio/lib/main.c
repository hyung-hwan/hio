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


#include <mio.h>
#include <mio-utl.h>
#include <mio-sck.h>
#include <mio-pro.h>
#include <mio-dns.h>

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
	mio_oow_t total_count;
};

typedef struct mmgr_stat_t mmgr_stat_t;

static mmgr_stat_t mmgr_stat;

static void* mmgr_alloc (mio_mmgr_t* mmgr, mio_oow_t size)
{
	void* x;

	if (((mmgr_stat_t*)mmgr->ctx)->total_count > 300)
	{
printf ("CRITICAL ERROR ---> too many heap chunks...\n");
		return MIO_NULL;
	}

	x = malloc (size);
	if (x) ((mmgr_stat_t*)mmgr->ctx)->total_count++;
	return x;
}

static void* mmgr_realloc (mio_mmgr_t* mmgr, void* ptr, mio_oow_t size)
{
	return realloc (ptr, size);
}

static void mmgr_free (mio_mmgr_t* mmgr, void* ptr)
{
	((mmgr_stat_t*)mmgr->ctx)->total_count--;
	return free (ptr);
}


static mio_mmgr_t mmgr = 
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

static void tcp_sck_on_disconnect (mio_dev_sck_t* tcp)
{
	switch (MIO_DEV_SCK_GET_PROGRESS(tcp))
	{
		case MIO_DEV_SCK_CONNECTING:
			MIO_INFO1 (tcp->mio, "OUTGOING SESSION DISCONNECTED - FAILED TO CONNECT (%d) TO REMOTE SERVER\n", (int)tcp->sck);
			break;

		case MIO_DEV_SCK_CONNECTING_SSL:
			MIO_INFO1 (tcp->mio, "OUTGOING SESSION DISCONNECTED - FAILED TO SSL-CONNECT (%d) TO REMOTE SERVER\n", (int)tcp->sck);
			break;

		case MIO_DEV_SCK_LISTENING:
			MIO_INFO1 (tcp->mio, "SHUTTING DOWN THE SERVER SOCKET(%d)...\n", (int)tcp->sck);
			break;

		case MIO_DEV_SCK_CONNECTED:
			MIO_INFO1 (tcp->mio, "OUTGOING CLIENT CONNECTION GOT TORN DOWN(%d).......\n", (int)tcp->sck);
			break;

		case MIO_DEV_SCK_ACCEPTING_SSL:
			MIO_INFO1 (tcp->mio, "INCOMING SSL-ACCEPT GOT DISCONNECTED(%d) ....\n", (int)tcp->sck);
			break;

		case MIO_DEV_SCK_ACCEPTED:
			MIO_INFO1 (tcp->mio, "INCOMING CLIENT BEING SERVED GOT DISCONNECTED(%d).......\n", (int)tcp->sck);
			break;

		default:
			MIO_INFO2 (tcp->mio, "SOCKET DEVICE DISCONNECTED (%d - %x)\n", (int)tcp->sck, (unsigned int)tcp->state);
			break;
	}
}

static void tcp_sck_on_connect (mio_dev_sck_t* tcp)
{
	mio_sckfam_t fam;
	mio_scklen_t len;
	mio_bch_t buf1[128], buf2[128];

	memset (buf1, 0, MIO_SIZEOF(buf1));
	memset (buf2, 0, MIO_SIZEOF(buf2));

	mio_getsckaddrinfo (tcp->mio, &tcp->localaddr, &len, &fam);
	inet_ntop (fam, tcp->localaddr.data, buf1, MIO_COUNTOF(buf1));

	mio_getsckaddrinfo (tcp->mio, &tcp->remoteaddr, &len, &fam);
	inet_ntop (fam, tcp->remoteaddr.data, buf2, MIO_COUNTOF(buf2));

	if (tcp->state & MIO_DEV_SCK_CONNECTED)
	{
		MIO_INFO5 (tcp->mio, "DEVICE connected to a remote server... LOCAL %hs:%d REMOTE %hs:%d SCK: %d\n", 
			buf1, mio_getsckaddrport(&tcp->localaddr), buf2, mio_getsckaddrport(&tcp->remoteaddr), tcp->sck);
	}
	else if (tcp->state & MIO_DEV_SCK_ACCEPTED)
	{
		MIO_INFO5 (tcp->mio, "DEVICE accepted client device... .LOCAL %hs:%d REMOTE %hs:%d\n",
			buf1, mio_getsckaddrport(&tcp->localaddr), buf2, mio_getsckaddrport(&tcp->remoteaddr), tcp->sck);
	}

	if (mio_dev_sck_write(tcp, "hello", 5, MIO_NULL, MIO_NULL) <= -1)
	{
		mio_dev_sck_halt (tcp);
	}
}

static int tcp_sck_on_write (mio_dev_sck_t* tcp, mio_iolen_t wrlen, void* wrctx, const mio_sckaddr_t* dstaddr)
{
	tcp_server_t* ts;
	mio_ntime_t tmout;

	if (wrlen <= -1)
	{
		MIO_INFO1 (tcp->mio, "TCP_SCK_ON_WRITE(%d) >>> SEDING TIMED OUT...........\n", (int)tcp->sck);
		mio_dev_sck_halt (tcp);
	}
	else
	{
		ts = (tcp_server_t*)(tcp + 1);
		if (wrlen == 0)
		{
			MIO_INFO1 (tcp->mio, "TCP_SCK_ON_WRITE(%d) >>> CLOSED WRITING END\n", (int)tcp->sck);
		}
		else
		{
			MIO_INFO3 (tcp->mio, "TCP_SCK_ON_WRITE(%d) >>> SENT MESSAGE %d of length %ld\n", (int)tcp->sck, ts->tally, (long int)wrlen);
		}

		ts->tally++;
	//	if (ts->tally >= 2) mio_dev_sck_halt (tcp);

		
		MIO_INIT_NTIME (&tmout, 5, 0);
		//mio_dev_sck_read (tcp, 1);

		MIO_INFO3 (tcp->mio, "TCP_SCK_ON_WRITE(%d) >>> REQUESTING to READ with timeout of %ld.%08ld\n", (int)tcp->sck, (long int)tmout.sec, (long int)tmout.nsec);
		mio_dev_sck_timedread (tcp, 1, &tmout);
	}
	return 0;
}

static int tcp_sck_on_read (mio_dev_sck_t* tcp, const void* buf, mio_iolen_t len, const mio_sckaddr_t* srcaddr)
{
	int n;

	if (len <= -1)
	{
		MIO_INFO1 (tcp->mio, "TCP_SCK_ON_READ(%d) STREAM DEVICE: TIMED OUT...\n", (int)tcp->sck);
		mio_dev_sck_halt (tcp);
		return 0;
	}
	else if (len <= 0)
	{
		MIO_INFO1 (tcp->mio, "TCP_SCK_ON_READ(%d) STREAM DEVICE: EOF RECEIVED...\n", (int)tcp->sck);
		/* no outstanding request. but EOF */
		mio_dev_sck_halt (tcp);
		return 0;
	}

	MIO_INFO2 (tcp->mio, "TCP_SCK_ON_READ(%d) - received %d bytes\n", (int)tcp->sck, (int)len);

	{
		mio_ntime_t tmout;

		static char a ='A';
		static char xxx[1000000];
		memset (xxx, a++ , MIO_SIZEOF(xxx));

		MIO_INFO2 (tcp->mio, "TCP_SCK_ON_READ(%d) >>> REQUESTING to write data of %d bytes\n", (int)tcp->sck, MIO_SIZEOF(xxx));
		//return mio_dev_sck_write  (tcp, "HELLO", 5, MIO_NULL);
		MIO_INIT_NTIME (&tmout, 5, 0);
		n = mio_dev_sck_timedwrite(tcp, xxx, MIO_SIZEOF(xxx), &tmout, MIO_NULL, MIO_NULL);

		if (n <= -1) return -1;
	}

	MIO_INFO1 (tcp->mio, "TCP_SCK_ON_READ(%d) - REQUESTING TO STOP READ\n", (int)tcp->sck);
	mio_dev_sck_read (tcp, 0);

#if 0
	MIO_INFO1 (tcp->mio, "TCP_SCK_ON_READ(%d) - REQUESTING TO CLOSE WRITING END\n", (int)tcp->sck);
	/* post the write finisher - close the writing end */
	n = mio_dev_sck_write(tcp, MIO_NULL, 0, MIO_NULL, MIO_NULL);
	if (n <= -1) return -1;
#endif

	return 0;

/* return 1; let the main loop to read more greedily without consulting the multiplexer */
}

/* ========================================================================= */

static void pro_on_close (mio_dev_pro_t* pro, mio_dev_pro_sid_t sid)
{
	mio_t* mio = pro->mio;
	if (sid == MIO_DEV_PRO_MASTER)
		MIO_INFO1 (mio, "PROCESS(%d) CLOSE MASTER\n", (int)pro->child_pid);
	else
		MIO_INFO2 (mio, "PROCESS(%d) CLOSE SLAVE[%d]\n", (int)pro->child_pid, sid);
}

static int pro_on_read (mio_dev_pro_t* pro, mio_dev_pro_sid_t sid, const void* data, mio_iolen_t dlen)
{
	mio_t* mio = pro->mio;

	if (dlen <= -1)
	{
		MIO_INFO1 (mio, "PROCESS(%d): READ TIMED OUT...\n", (int)pro->child_pid);
		mio_dev_pro_halt (pro);
		return 0;
	}
	else if (dlen <= 0)
	{
		MIO_INFO1 (mio, "PROCESS(%d): EOF RECEIVED...\n", (int)pro->child_pid);
		/* no outstanding request. but EOF */
		mio_dev_pro_halt (pro);
		return 0;
	}

	MIO_INFO5 (mio, "PROCESS(%d) READ DATA ON SLAVE[%d] len=%d [%.*hs]\n", (int)pro->child_pid, (int)sid, (int)dlen, dlen, (char*)data);
	if (sid == MIO_DEV_PRO_OUT)
	{
		mio_dev_pro_read (pro, sid, 0);
		mio_dev_pro_write (pro, "HELLO\n", 6, MIO_NULL);
	}
	return 0;
}

static int pro_on_write (mio_dev_pro_t* pro, mio_iolen_t wrlen, void* wrctx)
{
	mio_t* mio = pro->mio;
	mio_ntime_t tmout;
	if (wrlen <= -1)
	{
		MIO_INFO1 (mio, "PROCESS(%d): WRITE TIMED OUT...\n", (int)pro->child_pid);
		mio_dev_pro_halt (pro);
		return 0;
	}

	MIO_DEBUG2 (mio, "PROCESS(%d) wrote data of %d bytes\n", (int)pro->child_pid, (int)wrlen);
	/*mio_dev_pro_read (pro, MIO_DEV_PRO_OUT, 1);*/
	MIO_INIT_NTIME (&tmout, 5, 0);
	mio_dev_pro_timedread (pro, MIO_DEV_PRO_OUT, 1, &tmout);
	return 0;
}

/* ========================================================================= */

static int arp_sck_on_read (mio_dev_sck_t* dev, const void* data, mio_iolen_t dlen, const mio_sckaddr_t* srcaddr)
{
	mio_etharp_pkt_t* eap;


	if (dlen < MIO_SIZEOF(*eap)) return 0; /* drop */

	eap = (mio_etharp_pkt_t*)data;

	printf ("ARP ON IFINDEX %d OPCODE: %d", mio_getsckaddrifindex(srcaddr), ntohs(eap->arphdr.opcode));

	printf (" SHA: %02X:%02X:%02X:%02X:%02X:%02X", eap->arppld.sha[0], eap->arppld.sha[1], eap->arppld.sha[2], eap->arppld.sha[3], eap->arppld.sha[4], eap->arppld.sha[5]);
	printf (" SPA: %d.%d.%d.%d", eap->arppld.spa[0], eap->arppld.spa[1], eap->arppld.spa[2], eap->arppld.spa[3]);
	printf (" THA: %02X:%02X:%02X:%02X:%02X:%02X", eap->arppld.tha[0], eap->arppld.tha[1], eap->arppld.tha[2], eap->arppld.tha[3], eap->arppld.tha[4], eap->arppld.tha[5]);
	printf (" TPA: %d.%d.%d.%d", eap->arppld.tpa[0], eap->arppld.tpa[1], eap->arppld.tpa[2], eap->arppld.tpa[3]);
	printf ("\n");
	return 0;
}

static int arp_sck_on_write (mio_dev_sck_t* dev, mio_iolen_t wrlen, void* wrctx, const mio_sckaddr_t* dstaddr)
{
	return 0;
}

static void arp_sck_on_connect (mio_dev_sck_t* dev)
{
printf ("STARTING UP ARP SOCKET %d...\n", dev->sck);
}

static void arp_sck_on_disconnect (mio_dev_sck_t* dev)
{
printf ("SHUTTING DOWN ARP SOCKET %d...\n", dev->sck);
}

static int setup_arp_tester (mio_t* mio)
{
	mio_sckaddr_t ethdst;
	mio_etharp_pkt_t etharp;
	mio_dev_sck_make_t sck_make;
	mio_dev_sck_t* sck;

	memset (&sck_make, 0, MIO_SIZEOF(sck_make));
	sck_make.type = MIO_DEV_SCK_ARP;
	//sck_make.type = MIO_DEV_SCK_ARP_DGRAM;
	sck_make.on_write = arp_sck_on_write;
	sck_make.on_read = arp_sck_on_read;
	sck_make.on_connect = arp_sck_on_connect;
	sck_make.on_disconnect = arp_sck_on_disconnect;
	sck = mio_dev_sck_make(mio, 0, &sck_make);
	if (!sck)
	{
		printf ("Cannot make socket device\n");
		return -1;
	}

	//mio_sckaddr_initforeth (&ethdst, if_nametoindex("enp0s25.3"), (mio_ethaddr_t*)"\xFF\xFF\xFF\xFF\xFF\xFF");
	//mio_sckaddr_initforeth (&ethdst, if_nametoindex("enp0s25.3"), (mio_ethaddr_t*)"\xAA\xBB\xFF\xCC\xDD\xFF");

	mio_sckaddr_initforeth (&ethdst, if_nametoindex("wlan0"), (mio_ethaddr_t*)"\xAA\xBB\xFF\xCC\xDD\xFF");

	memset (&etharp, 0, MIO_SIZEOF(etharp));

	memcpy (etharp.ethhdr.source, "\xB8\x6B\x23\x9C\x10\x76", MIO_ETHADDR_LEN);
	//memcpy (etharp.ethhdr.dest, "\xFF\xFF\xFF\xFF\xFF\xFF", MIO_ETHADDR_LEN);
	memcpy (etharp.ethhdr.dest, "\xAA\xBB\xFF\xCC\xDD\xFF", MIO_ETHADDR_LEN);
	etharp.ethhdr.proto = MIO_CONST_HTON16(MIO_ETHHDR_PROTO_ARP);

	etharp.arphdr.htype = MIO_CONST_HTON16(MIO_ARPHDR_HTYPE_ETH);
	etharp.arphdr.ptype = MIO_CONST_HTON16(MIO_ARPHDR_PTYPE_IP4);
	etharp.arphdr.hlen = MIO_ETHADDR_LEN;
	etharp.arphdr.plen = MIO_IP4ADDR_LEN;
	etharp.arphdr.opcode = MIO_CONST_HTON16(MIO_ARPHDR_OPCODE_REQUEST);

	memcpy (etharp.arppld.sha, "\xB8\x6B\x23\x9C\x10\x76", MIO_ETHADDR_LEN);

	if (mio_dev_sck_write(sck, &etharp, MIO_SIZEOF(etharp), MIO_NULL, &ethdst) <= -1)
	//if (mio_dev_sck_write (sck, &etharp.arphdr, MIO_SIZEOF(etharp) - MIO_SIZEOF(etharp.ethhdr), MIO_NULL, &ethaddr) <= -1)
	{
		printf ("CANNOT WRITE ARP...\n");
	}


	return 0;
}

/* ========================================================================= */

struct icmpxtn_t
{
	mio_uint16_t icmp_seq;
	mio_tmridx_t tmout_jobidx;
	int reply_received;
};

typedef struct icmpxtn_t icmpxtn_t;

static int schedule_icmp_wait (mio_dev_sck_t* dev);

static void send_icmp (mio_dev_sck_t* dev, mio_uint16_t seq)
{
	mio_sckaddr_t dstaddr;
	mio_ip4addr_t ia;
	mio_icmphdr_t* icmphdr;
	mio_uint8_t buf[512];

	inet_pton (AF_INET, "192.168.1.131", &ia);
	mio_sckaddr_initforip4 (&dstaddr, 0, &ia);

	memset(buf, 0, MIO_SIZEOF(buf));
	icmphdr = (mio_icmphdr_t*)buf;
	icmphdr->type = MIO_ICMP_ECHO_REQUEST;
	icmphdr->u.echo.id = MIO_CONST_HTON16(100);
	icmphdr->u.echo.seq = mio_hton16(seq);

	memset (&buf[MIO_SIZEOF(*icmphdr)], 'A', MIO_SIZEOF(buf) - MIO_SIZEOF(*icmphdr));
	icmphdr->checksum = mio_checksumip (icmphdr, MIO_SIZEOF(buf));

	if (mio_dev_sck_write(dev, buf, MIO_SIZEOF(buf), MIO_NULL, &dstaddr) <= -1)
	{
		printf ("CANNOT WRITE ICMP...\n");
		mio_dev_sck_halt (dev);
	}

	if (schedule_icmp_wait (dev) <= -1)
	{
		printf ("CANNOT SCHEDULE ICMP WAIT...\n");
		mio_dev_sck_halt (dev);
	}
}

static void on_icmp_due (mio_t* mio, const mio_ntime_t* now, mio_tmrjob_t* tmrjob)
{
	mio_dev_sck_t* dev;
	icmpxtn_t* icmpxtn;

	dev = tmrjob->ctx;
	icmpxtn = (icmpxtn_t*)(dev + 1);

	if (icmpxtn->reply_received)
		icmpxtn->reply_received = 0;
	else
		printf ("NO ICMP REPLY RECEIVED....\n");

	send_icmp (dev, ++icmpxtn->icmp_seq);
}

static int schedule_icmp_wait (mio_dev_sck_t* dev)
{
	mio_t* mio = dev->mio;
	icmpxtn_t* icmpxtn;
	mio_tmrjob_t tmrjob;
	mio_ntime_t fire_after;

	icmpxtn = (icmpxtn_t*)(dev + 1);
	MIO_INIT_NTIME (&fire_after, 2, 0);

	memset (&tmrjob, 0, MIO_SIZEOF(tmrjob));
	tmrjob.ctx = dev;
	mio_gettime (mio, &tmrjob.when);
	MIO_ADD_NTIME (&tmrjob.when, &tmrjob.when, &fire_after);
	tmrjob.handler = on_icmp_due;
	tmrjob.idxptr = &icmpxtn->tmout_jobidx;

	assert (icmpxtn->tmout_jobidx == MIO_TMRIDX_INVALID);

	return (mio_instmrjob(dev->mio, &tmrjob) == MIO_TMRIDX_INVALID)? -1: 0;
}

static int icmp_sck_on_read (mio_dev_sck_t* dev, const void* data, mio_iolen_t dlen, const mio_sckaddr_t* srcaddr)
{
	icmpxtn_t* icmpxtn;
	mio_iphdr_t* iphdr;
	mio_icmphdr_t* icmphdr;

	/* when received, the data contains the IP header.. */
	icmpxtn = (icmpxtn_t*)(dev + 1);

	if (dlen < MIO_SIZEOF(*iphdr) + MIO_SIZEOF(*icmphdr))
	{
		printf ("INVALID ICMP PACKET.. TOO SHORT...%d\n", (int)dlen);
	}
	else
	{
		/* TODO: consider IP options... */
		iphdr = (mio_iphdr_t*)data;

		if (iphdr->ihl * 4 + MIO_SIZEOF(*icmphdr) > dlen)
		{
			printf ("INVALID ICMP PACKET.. WRONG IHL...%d\n", (int)iphdr->ihl * 4);
		}
		else
		{
			icmphdr = (mio_icmphdr_t*)((mio_uint8_t*)data + (iphdr->ihl * 4));

			/* TODO: check srcaddr against target */

			if (icmphdr->type == MIO_ICMP_ECHO_REPLY && 
			    mio_ntoh16(icmphdr->u.echo.seq) == icmpxtn->icmp_seq) /* TODO: more check.. echo.id.. */
			{
				icmpxtn->reply_received = 1;
				printf ("ICMP REPLY RECEIVED...ID %d SEQ %d\n", (int)mio_ntoh16(icmphdr->u.echo.id), (int)mio_ntoh16(icmphdr->u.echo.seq));
			}
			else
			{
				printf ("GARBAGE ICMP PACKET...LEN %d SEQ %d,%d\n", (int)dlen, (int)icmpxtn->icmp_seq, (int)mio_ntoh16(icmphdr->u.echo.seq));
			}
		}
	}
	return 0;
}


static int icmp_sck_on_write (mio_dev_sck_t* dev, mio_iolen_t wrlen, void* wrctx, const mio_sckaddr_t* dstaddr)
{
	/*icmpxtn_t* icmpxtn;

	icmpxtn = (icmpxtn_t*)(dev + 1); */

	return 0;
}

static void icmp_sck_on_disconnect (mio_dev_sck_t* dev)
{
	icmpxtn_t* icmpxtn;

	icmpxtn = (icmpxtn_t*)(dev + 1);

printf ("SHUTTING DOWN ICMP SOCKET %d...\n", dev->sck);
	if (icmpxtn->tmout_jobidx != MIO_TMRIDX_INVALID)
	{

		mio_deltmrjob (dev->mio, icmpxtn->tmout_jobidx);
		icmpxtn->tmout_jobidx = MIO_TMRIDX_INVALID;
	}
}

static int setup_ping4_tester (mio_t* mio)
{
	mio_dev_sck_make_t sck_make;
	mio_dev_sck_t* sck;
	icmpxtn_t* icmpxtn;

	memset (&sck_make, 0, MIO_SIZEOF(sck_make));
	sck_make.type = MIO_DEV_SCK_ICMP4;
	sck_make.on_write = icmp_sck_on_write;
	sck_make.on_read = icmp_sck_on_read;
	sck_make.on_disconnect = icmp_sck_on_disconnect;

	sck = mio_dev_sck_make (mio, MIO_SIZEOF(icmpxtn_t), &sck_make);
	if (!sck)
	{
		printf ("Cannot make ICMP4 socket device\n");
		return -1;
	}

	icmpxtn = (icmpxtn_t*)(sck + 1);
	icmpxtn->tmout_jobidx = MIO_TMRIDX_INVALID;
	icmpxtn->icmp_seq = 0;

	/*TODO: mio_dev_sck_setbroadcast (sck, 1);*/

	send_icmp (sck, ++icmpxtn->icmp_seq);

	return 0;
}


/* ========================================================================= */

#if 1
static mio_t* g_mio;

static void handle_signal (int sig)
{
	if (g_mio) mio_stop (g_mio, MIO_STOPREQ_TERMINATION);
}

int main (int argc, char* argv[])
{
	int i;

	mio_t* mio;
	mio_dev_sck_t* tcp[3];

	struct sigaction sigact;
	mio_dev_sck_connect_t tcp_conn;
	mio_dev_sck_listen_t tcp_lstn;
	mio_dev_sck_bind_t tcp_bind;
	mio_dev_sck_make_t tcp_make;

	tcp_server_t* ts;

#if defined(USE_SSL)
	SSL_load_error_strings ();
	SSL_library_init ();
#endif

	mio = mio_open(&mmgr, 0, MIO_NULL, 512, MIO_NULL);
	if (!mio)
	{
		printf ("Cannot open mio\n");
		return -1;
	}

	g_mio = mio;

	memset (&sigact, 0, MIO_SIZEOF(sigact));
	sigact.sa_flags = SA_RESTART;
	sigact.sa_handler = handle_signal;
	sigaction (SIGINT, &sigact, MIO_NULL);

	memset (&sigact, 0, MIO_SIZEOF(sigact));
	sigact.sa_handler = SIG_IGN;
	sigaction (SIGPIPE, &sigact, MIO_NULL);

/*
	memset (&sigact, 0, MIO_SIZEOF(sigact));
	sigact.sa_handler = SIG_IGN;
	sigaction (SIGCHLD, &sigact, MIO_NULL);
*/

	/*memset (&sin, 0, MIO_SIZEOF(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(1234); */
/*
	udp = (mio_dev_udp_t*)mio_dev_make(mio, MIO_SIZEOF(*udp), &udp_mth, &udp_evcb, &sin);
	if (!udp)
	{
		printf ("Cannot make udp\n");
		goto oops;
	}
*/

	memset (&tcp_make, 0, MIO_SIZEOF(&tcp_make));
	tcp_make.type = MIO_DEV_SCK_TCP4;
	tcp_make.on_write = tcp_sck_on_write;
	tcp_make.on_read = tcp_sck_on_read;
	tcp_make.on_connect = tcp_sck_on_connect;
	tcp_make.on_disconnect = tcp_sck_on_disconnect;
	tcp[0] = mio_dev_sck_make(mio, MIO_SIZEOF(tcp_server_t), &tcp_make);
	if (!tcp[0])
	{
		printf ("Cannot make tcp\n");
		goto oops;
	}

	ts = (tcp_server_t*)(tcp[0] + 1);
	ts->tally = 0;

	memset (&tcp_conn, 0, MIO_SIZEOF(tcp_conn));
{
	/* openssl s_server -accept 9999 -key localhost.key  -cert localhost.crt */
	in_addr_t ia = inet_addr("127.0.0.1");
	mio_sckaddr_initforip4 (&tcp_conn.remoteaddr, 9999, (mio_ip4addr_t*)&ia);
}

	MIO_INIT_NTIME (&tcp_conn.connect_tmout, 5, 0);
	tcp_conn.options = MIO_DEV_SCK_CONNECT_SSL;
	if (mio_dev_sck_connect(tcp[0], &tcp_conn) <= -1)
	{
		MIO_INFO0 (mio, "tcp[0] mio_dev_sck_connect() failed....\n");
		/* carry on regardless of failure */
	}

	/* -------------------------------------------------------------- */
	memset (&tcp_make, 0, MIO_SIZEOF(&tcp_make));
	tcp_make.type = MIO_DEV_SCK_TCP4;
	tcp_make.on_write = tcp_sck_on_write;
	tcp_make.on_read = tcp_sck_on_read;
	tcp_make.on_connect = tcp_sck_on_connect;
	tcp_make.on_disconnect = tcp_sck_on_disconnect;

	tcp[1] = mio_dev_sck_make(mio, MIO_SIZEOF(tcp_server_t), &tcp_make);
	if (!tcp[1])
	{
		MIO_INFO0 (mio, "cannot make tcp[1]....\n");
		goto oops;
	}
	ts = (tcp_server_t*)(tcp[1] + 1);
	ts->tally = 0;

	memset (&tcp_bind, 0, MIO_SIZEOF(tcp_bind));
	mio_sckaddr_initforip4 (&tcp_bind.localaddr, 1234, MIO_NULL);
	tcp_bind.options = MIO_DEV_SCK_BIND_REUSEADDR;

	if (mio_dev_sck_bind(tcp[1],&tcp_bind) <= -1)
	{
		MIO_INFO0 (mio, "tcp[1] mio_dev_sck_bind() failed....\n");
		goto oops;
	}


	tcp_lstn.backlogs = 100;
	if (mio_dev_sck_listen(tcp[1], &tcp_lstn) <= -1)
	{
		MIO_INFO0 (mio, "tcp[1] mio_dev_sck_listen() failed....\n");
		goto oops;
	}

	/* -------------------------------------------------------------- */
	memset (&tcp_make, 0, MIO_SIZEOF(&tcp_make));
	tcp_make.type = MIO_DEV_SCK_TCP4;
	tcp_make.on_write = tcp_sck_on_write;
	tcp_make.on_read = tcp_sck_on_read;
	tcp_make.on_connect = tcp_sck_on_connect;
	tcp_make.on_disconnect = tcp_sck_on_disconnect;

	tcp[2] = mio_dev_sck_make(mio, MIO_SIZEOF(tcp_server_t), &tcp_make);
	if (!tcp[2])
	{
		printf ("Cannot make tcp\n");
		goto oops;
	}
	ts = (tcp_server_t*)(tcp[2] + 1);
	ts->tally = 0;

	memset (&tcp_bind, 0, MIO_SIZEOF(tcp_bind));
	mio_sckaddr_initforip4 (&tcp_bind.localaddr, 1235, MIO_NULL);
	tcp_bind.options = MIO_DEV_SCK_BIND_REUSEADDR | /*MIO_DEV_SCK_BIND_REUSEPORT |*/ MIO_DEV_SCK_BIND_SSL; 
	tcp_bind.ssl_certfile = "localhost.crt";
	tcp_bind.ssl_keyfile = "localhost.key";
	MIO_INIT_NTIME (&tcp_bind.accept_tmout, 5, 1);

	if (mio_dev_sck_bind(tcp[2], &tcp_bind) <= -1)
	{
		MIO_INFO1 (mio, "tcp[2] mio_dev_sck_bind() failed - %js\n", mio_geterrmsg(mio));
		goto oops;
	}

	tcp_lstn.backlogs = 100;
	if (mio_dev_sck_listen(tcp[2], &tcp_lstn) <= -1)
	{
		MIO_INFO1 (mio, "tcp[2] mio_dev_sck_listen() failed - %js\n", mio_geterrmsg(mio));
		goto oops;
	}

	//mio_dev_sck_sendfile (tcp[2], fd, offset, count);

	if (setup_arp_tester(mio) <= -1) goto oops;
	if (setup_ping4_tester(mio) <= -1) goto oops;

#if 1
for (i = 0; i < 5; i++)
{
	mio_dev_pro_t* pro;
	mio_dev_pro_make_t pro_make;

	memset (&pro_make, 0, MIO_SIZEOF(pro_make));
	pro_make.flags = MIO_DEV_PRO_READOUT | MIO_DEV_PRO_READERR | MIO_DEV_PRO_WRITEIN /*| MIO_DEV_PRO_FORGET_CHILD*/;
	//pro_make.cmd = "/bin/ls -laF /usr/bin";
	//pro_make.cmd = "/bin/ls -laF";
	pro_make.cmd = "./a";
	pro_make.on_read = pro_on_read;
	pro_make.on_write = pro_on_write;
	pro_make.on_close = pro_on_close;

	pro = mio_dev_pro_make(mio, 0, &pro_make);
	if (!pro)
	{
		MIO_INFO1 (mio, "CANNOT CREATE PROCESS PIPE - %js\n", mio_geterrmsg(mio));
		goto oops;
	}
	mio_dev_pro_read (pro, MIO_DEV_PRO_OUT, 0);
	//mio_dev_pro_read (pro, MIO_DEV_PRO_ERR, 0);

	mio_dev_pro_write (pro, "MY MIO LIBRARY\n", 16, MIO_NULL);
//mio_dev_pro_killchild (pro); 
//mio_dev_pro_close (pro, MIO_DEV_PRO_IN); 
//mio_dev_pro_close (pro, MIO_DEV_PRO_OUT); 
//mio_dev_pro_close (pro, MIO_DEV_PRO_ERR); 
}
#endif

{
	mio_dnsc_t* dnsc;
	dnsc = mio_dnsc_start (mio/*, "8.8.8.8:53,1.1.1.1:53"*/); /* option - send to all, send one by one */
	{
		mio_dns_bqr_t qrs[] = 
		{
			{ "code.miflux.com",  MIO_DNS_QTYPE_A,    MIO_DNS_QCLASS_IN },
			{ "code.miflux.com",  MIO_DNS_QTYPE_AAAA, MIO_DNS_QCLASS_IN },
			{ "code.abiyo.net",   MIO_DNS_QTYPE_A,    MIO_DNS_QCLASS_IN },
			{ "code6.abiyo.net",  MIO_DNS_QTYPE_AAAA, MIO_DNS_QCLASS_IN },
			{ "abiyo.net",        MIO_DNS_QTYPE_MX,   MIO_DNS_QCLASS_IN }
		};
		mio_dns_brr_t rrs[] = 
		{
			{ MIO_DNS_RR_PART_ANSWER,    "code.miflux.com",  MIO_DNS_QTYPE_A,    MIO_DNS_QCLASS_IN, 86400, 0, MIO_NULL },
			{ MIO_DNS_RR_PART_ANSWER,    "code.miflux.com",  MIO_DNS_QTYPE_AAAA, MIO_DNS_QCLASS_IN, 86400, 0, MIO_NULL },
			{ MIO_DNS_RR_PART_AUTHORITY, "dns.miflux.com",   MIO_DNS_QTYPE_NS,   MIO_DNS_QCLASS_IN, 86400, 0, MIO_NULL }
		};

		mio_dns_beopt_t beopt[] =
		{
			{ MIO_DNS_EOPT_COOKIE, 8, "\x01\x02\x03\x04\0x05\x06\0x07\0x08" },
			{ MIO_DNS_EOPT_NSID,   0, MIO_NULL                              }
		};

		mio_dns_bedns_t qedns =
		{
			4096, /* uplen */

			0,    /* edns version */
			0,    /* dnssec ok */

			MIO_COUNTOF(beopt),    /* number of edns options */
			beopt
		};

		mio_dns_bdns_t qhdr =
		{
			-1,              /* id */
			0,                  /* qr */
			MIO_DNS_OPCODE_QUERY, /* opcode */
			0, /* aa */
			0, /* tc */
			1, /* rd */
			0, /* ra */
			0, /* ad */
			0, /* cd */
			MIO_DNS_RCODE_NOERROR /* rcode */
		};

		mio_dns_bdns_t rhdr =
		{
			0x1234,               /* id */
			1,                    /* qr */
			MIO_DNS_OPCODE_QUERY, /* opcode */

			0, /* aa */
			0, /* tc */
			0, /* rd */
			1, /* ra */
			0, /* ad */
			0, /* cd */
			MIO_DNS_RCODE_BADCOOKIE /* rcode */
		}; 

		mio_dnsc_sendreq (dnsc, &qhdr, qrs, MIO_COUNTOF(qrs), &qedns);
		mio_dnsc_sendmsg (dnsc, &rhdr, qrs, MIO_COUNTOF(qrs), rrs, MIO_COUNTOF(rrs), &qedns);
	}

	mio_loop (mio);

	/* TODO: let mio close it ... dnsc is svc. sck is dev. */
	mio_dnsc_stop (dnsc);
}

	g_mio = MIO_NULL;
	mio_close (mio);
#if defined(USE_SSL)
	cleanup_openssl ();
#endif

	return 0;

oops:
	g_mio = MIO_NULL;
	mio_close (mio);
#if defined(USE_SSL)
	cleanup_openssl ();
#endif
	return -1;
}

#else
int main (int argc, char* argv[])
{
	mio_t* mio = MIO_NULL;
	mio_dev_sck_t* tcpsvr;
	mio_dev_sck_make_t tcp_make;
	mio_dev_sck_connect_t tcp_conn;
	tcp_server_t* ts;
	mio_dnsc_t dnsc = MIO_NULL;

	mio = mio_open(&mmgr, 0, MIO_NULL, 512, MIO_NULL);
	if (!mio)
	{
		printf ("Cannot open mio\n");
		goto oops;
	}

	memset (&tcp_make, 0, MIO_SIZEOF(&tcp_make));
	tcp_make.type = MIO_DEV_SCK_TCP4;
	tcp_make.on_write = tcp_sck_on_write;
	tcp_make.on_read = tcp_sck_on_read;
	tcp_make.on_connect = tcp_sck_on_connect;
	tcp_make.on_disconnect = tcp_sck_on_disconnect;
	tcpsvr = mio_dev_sck_make(mio, MIO_SIZEOF(tcp_server_t), &tcp_make);
	if (!tcpsvr)
	{
		printf ("Cannot make a tcp server\n");
		goto oops;
	}

	ts = (tcp_server_t*)(tcpsvr + 1);
	ts->tally = 0;

	memset (&tcp_conn, 0, MIO_SIZEOF(tcp_conn));
{
	in_addr_t ia = inet_addr("127.0.0.1");
	mio_sckaddr_initforip4 (&tcp_conn.remoteaddr, 9999, (mio_ip4addr_t*)&ia);
}
	MIO_INIT_NTIME (&tcp_conn.connect_tmout, 5, 0);
	tcp_conn.options = 0;
	if (mio_dev_sck_connect(tcpsvr, &tcp_conn) <= -1)
	{
	}

#if 0
	while (1)
	{
		mio_exec (mio);
	}
#endif
	mio_loop (mio);

oops:
	if (mio) mio_close (mio);
	return 0;
}

#endif
