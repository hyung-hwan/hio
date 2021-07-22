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


#include <hio.h>
#include <hio-utl.h>
#include <hio-sck.h>
#include <hio-pro.h>
#include <hio-pipe.h>
#include <hio-thr.h>
#include <hio-dns.h>
#include <hio-nwif.h>
#include <hio-http.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

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
	hio_oow_t total_count;
};

typedef struct mmgr_stat_t mmgr_stat_t;

static mmgr_stat_t mmgr_stat;

static void* mmgr_alloc (hio_mmgr_t* mmgr, hio_oow_t size)
{
	void* x;

#if 0
	if (((mmgr_stat_t*)mmgr->ctx)->total_count > 3000)
	{
printf ("CRITICAL ERROR ---> too many heap chunks...\n");
		return HIO_NULL;
	}
#endif

	x = malloc (size);
	if (x)
	{
		((mmgr_stat_t*)mmgr->ctx)->total_count++;
		/*printf ("MMGR total_count INCed to %d => %p\n", ((mmgr_stat_t*)mmgr->ctx)->total_count, x);*/
	}
	return x;
}

static void* mmgr_realloc (hio_mmgr_t* mmgr, void* ptr, hio_oow_t size)
{
	void* x;

	x = realloc (ptr, size);
	if (x && !ptr) 
	{
		((mmgr_stat_t*)mmgr->ctx)->total_count++;
		/*printf ("MMGR total_count INCed to %d => %p\n", ((mmgr_stat_t*)mmgr->ctx)->total_count, x);*/
	}
	return x;
}

static void mmgr_free (hio_mmgr_t* mmgr, void* ptr)
{
	((mmgr_stat_t*)mmgr->ctx)->total_count--;
	/*printf ("MMGR total_count DECed to %d => %p\n", ((mmgr_stat_t*)mmgr->ctx)->total_count, ptr);*/
	return free (ptr);
}


static hio_mmgr_t mmgr = 
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

static void tcp_sck_on_disconnect (hio_dev_sck_t* tcp)
{
	switch (HIO_DEV_SCK_GET_PROGRESS(tcp))
	{
		case HIO_DEV_SCK_CONNECTING:
			HIO_INFO1 (tcp->hio, "OUTGOING SESSION DISCONNECTED - FAILED TO CONNECT (%d) TO REMOTE SERVER\n", (int)tcp->hnd);
			break;

		case HIO_DEV_SCK_CONNECTING_SSL:
			HIO_INFO1 (tcp->hio, "OUTGOING SESSION DISCONNECTED - FAILED TO SSL-CONNECT (%d) TO REMOTE SERVER\n", (int)tcp->hnd);
			break;

		case HIO_DEV_SCK_LISTENING:
			HIO_INFO1 (tcp->hio, "SHUTTING DOWN THE SERVER SOCKET(%d)...\n", (int)tcp->hnd);
			break;

		case HIO_DEV_SCK_CONNECTED:
			HIO_INFO1 (tcp->hio, "OUTGOING CLIENT CONNECTION GOT TORN DOWN(%d).......\n", (int)tcp->hnd);
			break;

		case HIO_DEV_SCK_ACCEPTING_SSL:
			HIO_INFO1 (tcp->hio, "INCOMING SSL-ACCEPT GOT DISCONNECTED(%d) ....\n", (int)tcp->hnd);
			break;

		case HIO_DEV_SCK_ACCEPTED:
			HIO_INFO1 (tcp->hio, "INCOMING CLIENT BEING SERVED GOT DISCONNECTED(%d).......\n", (int)tcp->hnd);
			break;

		default:
			HIO_INFO2 (tcp->hio, "SOCKET DEVICE DISCONNECTED (%d - %x)\n", (int)tcp->hnd, (unsigned int)tcp->state);
			break;
	}
}

static void tcp_sck_on_connect (hio_dev_sck_t* tcp)
{
	hio_bch_t buf1[128], buf2[128];
	hio_bch_t k[50000];
	hio_iovec_t iov[] =
	{
		{ "hello", 5 },
		{ "world", 5 },
		{ k, HIO_COUNTOF(k) },
		{ "hio test", 8 },
		{ k, HIO_COUNTOF(k) }
	};
	int i;

	
	hio_skadtobcstr (tcp->hio, &tcp->localaddr, buf1, HIO_COUNTOF(buf1), HIO_SKAD_TO_BCSTR_ADDR | HIO_SKAD_TO_BCSTR_PORT);
	hio_skadtobcstr (tcp->hio, &tcp->remoteaddr, buf2, HIO_COUNTOF(buf2), HIO_SKAD_TO_BCSTR_ADDR | HIO_SKAD_TO_BCSTR_PORT);

	if (tcp->state & HIO_DEV_SCK_CONNECTED)
	{
		HIO_INFO3 (tcp->hio, "DEVICE connected to a remote server... LOCAL %hs REMOTE %hs SCK: %d\n", buf1, buf2, tcp->hnd);
	}
	else if (tcp->state & HIO_DEV_SCK_ACCEPTED)
	{
		HIO_INFO3 (tcp->hio, "DEVICE accepted client device... .LOCAL %hs REMOTE %hs  SCK: %d\n", buf1, buf2, tcp->hnd);
	}

	for (i = 0; i < HIO_COUNTOF(k); i++) k[i]  = 'A' + (i % 26);

/*
	{
	int sndbuf = 2000;
	hio_dev_sck_setsockopt(tcp, SOL_SOCKET, SO_SNDBUF, &sndbuf, HIO_SIZEOF(sndbuf));
	}
*/

	if (hio_dev_sck_writev(tcp, iov, HIO_COUNTOF(iov), HIO_NULL, HIO_NULL) <= -1)
	{
		hio_dev_sck_halt (tcp);
	}
}

static int tcp_sck_on_write (hio_dev_sck_t* tcp, hio_iolen_t wrlen, void* wrctx, const hio_skad_t* dstaddr)
{
	tcp_server_t* ts;
	hio_ntime_t tmout;

	if (wrlen <= -1)
	{
		HIO_INFO1 (tcp->hio, "TCP_SCK_ON_WRITE(%d) >>> SEDING TIMED OUT...........\n", (int)tcp->hnd);
		hio_dev_sck_halt (tcp);
	}
	else
	{
		ts = (tcp_server_t*)(tcp + 1);
		if (wrlen == 0)
		{
			HIO_INFO1 (tcp->hio, "TCP_SCK_ON_WRITE(%d) >>> CLOSED WRITING END\n", (int)tcp->hnd);
		}
		else
		{
			HIO_INFO3 (tcp->hio, "TCP_SCK_ON_WRITE(%d) >>> SENT MESSAGE %d of length %ld\n", (int)tcp->hnd, ts->tally, (long int)wrlen);
		}

		ts->tally++;
	//	if (ts->tally >= 2) hio_dev_sck_halt (tcp);

		
		HIO_INIT_NTIME (&tmout, 5, 0);
		//hio_dev_sck_read (tcp, 1);

		HIO_INFO3 (tcp->hio, "TCP_SCK_ON_WRITE(%d) >>> REQUESTING to READ with timeout of %ld.%08ld\n", (int)tcp->hnd, (long int)tmout.sec, (long int)tmout.nsec);
		hio_dev_sck_timedread (tcp, 1, &tmout);
	}
	return 0;
}

static int tcp_sck_on_read (hio_dev_sck_t* tcp, const void* buf, hio_iolen_t len, const hio_skad_t* srcaddr)
{
	int n;

	if (len <= -1)
	{
		HIO_INFO1 (tcp->hio, "TCP_SCK_ON_READ(%d) STREAM DEVICE: TIMED OUT...\n", (int)tcp->hnd);
		hio_dev_sck_halt (tcp);
		return 0;
	}
	else if (len <= 0)
	{
		HIO_INFO1 (tcp->hio, "TCP_SCK_ON_READ(%d) STREAM DEVICE: EOF RECEIVED...\n", (int)tcp->hnd);
		/* no outstanding request. but EOF */
		hio_dev_sck_halt (tcp);
		return 0;
	}

	HIO_INFO2 (tcp->hio, "TCP_SCK_ON_READ(%d) - received %d bytes\n", (int)tcp->hnd, (int)len);

	{
		hio_ntime_t tmout;

		static char a ='A';
		static char xxx[1000000];
		memset (xxx, a++ , HIO_SIZEOF(xxx));

		HIO_INFO2 (tcp->hio, "TCP_SCK_ON_READ(%d) >>> REQUESTING to write data of %d bytes\n", (int)tcp->hnd, HIO_SIZEOF(xxx));
		//return hio_dev_sck_write  (tcp, "HELLO", 5, HIO_NULL);
		HIO_INIT_NTIME (&tmout, 5, 0);
		n = hio_dev_sck_timedwrite(tcp, xxx, HIO_SIZEOF(xxx), &tmout, HIO_NULL, HIO_NULL);

		if (n <= -1) return -1;
	}

	HIO_INFO1 (tcp->hio, "TCP_SCK_ON_READ(%d) - REQUESTING TO STOP READ\n", (int)tcp->hnd);
	hio_dev_sck_read (tcp, 0);

#if 0
	HIO_INFO1 (tcp->hio, "TCP_SCK_ON_READ(%d) - REQUESTING TO CLOSE WRITING END\n", (int)tcp->hnd);
	/* post the write finisher - close the writing end */
	n = hio_dev_sck_write(tcp, HIO_NULL, 0, HIO_NULL, HIO_NULL);
	if (n <= -1) return -1;
#endif

	return 0;

/* return 1; let the main loop to read more greedily without consulting the multiplexer */
}

/* ========================================================================= */

static void pro_on_close (hio_dev_pro_t* pro, hio_dev_pro_sid_t sid)
{
	hio_t* hio = pro->hio;
	if (sid == HIO_DEV_PRO_MASTER)
		HIO_INFO1 (hio, "PROCESS(%d) CLOSE MASTER\n", (int)pro->child_pid);
	else
		HIO_INFO2 (hio, "PROCESS(%d) CLOSE SLAVE[%d]\n", (int)pro->child_pid, sid);
}

static int pro_on_read (hio_dev_pro_t* pro, hio_dev_pro_sid_t sid, const void* data, hio_iolen_t dlen)
{
	hio_t* hio = pro->hio;

	if (dlen <= -1)
	{
		HIO_INFO1 (hio, "PROCESS(%d): READ TIMED OUT...\n", (int)pro->child_pid);
		hio_dev_pro_halt (pro);
		return 0;
	}
	else if (dlen <= 0)
	{
		HIO_INFO1 (hio, "PROCESS(%d): EOF RECEIVED...\n", (int)pro->child_pid);
		/* no outstanding request. but EOF */
		hio_dev_pro_halt (pro);
		return 0;
	}

	HIO_INFO5 (hio, "PROCESS(%d) READ DATA ON SLAVE[%d] len=%d [%.*hs]\n", (int)pro->child_pid, (int)sid, (int)dlen, dlen, (char*)data);
	if (sid == HIO_DEV_PRO_OUT)
	{
		hio_dev_pro_read (pro, sid, 0);
		hio_dev_pro_write (pro, "HELLO\n", 6, HIO_NULL);
	}
	return 0;
}

static int pro_on_write (hio_dev_pro_t* pro, hio_iolen_t wrlen, void* wrctx)
{
	hio_t* hio = pro->hio;
	hio_ntime_t tmout;
	if (wrlen <= -1)
	{
		HIO_INFO1 (hio, "PROCESS(%d): WRITE TIMED OUT...\n", (int)pro->child_pid);
		hio_dev_pro_halt (pro);
		return 0;
	}

	HIO_DEBUG2 (hio, "PROCESS(%d) wrote data of %d bytes\n", (int)pro->child_pid, (int)wrlen);
	/*hio_dev_pro_read (pro, HIO_DEV_PRO_OUT, 1);*/
	HIO_INIT_NTIME (&tmout, 5, 0);
	hio_dev_pro_timedread (pro, HIO_DEV_PRO_OUT, 1, &tmout);
	return 0;
}

/* ========================================================================= */

static int arp_sck_on_read (hio_dev_sck_t* dev, const void* data, hio_iolen_t dlen, const hio_skad_t* srcaddr)
{
	hio_etharp_pkt_t* eap;


	if (dlen < HIO_SIZEOF(*eap)) return 0; /* drop */

	eap = (hio_etharp_pkt_t*)data;

	printf ("ARP ON IFINDEX %d OPCODE: %d", hio_skad_ifindex(srcaddr), ntohs(eap->arphdr.opcode));

	printf (" SHA: %02X:%02X:%02X:%02X:%02X:%02X", eap->arppld.sha[0], eap->arppld.sha[1], eap->arppld.sha[2], eap->arppld.sha[3], eap->arppld.sha[4], eap->arppld.sha[5]);
	printf (" SPA: %d.%d.%d.%d", eap->arppld.spa[0], eap->arppld.spa[1], eap->arppld.spa[2], eap->arppld.spa[3]);
	printf (" THA: %02X:%02X:%02X:%02X:%02X:%02X", eap->arppld.tha[0], eap->arppld.tha[1], eap->arppld.tha[2], eap->arppld.tha[3], eap->arppld.tha[4], eap->arppld.tha[5]);
	printf (" TPA: %d.%d.%d.%d", eap->arppld.tpa[0], eap->arppld.tpa[1], eap->arppld.tpa[2], eap->arppld.tpa[3]);
	printf ("\n");
	return 0;
}

static int arp_sck_on_write (hio_dev_sck_t* dev, hio_iolen_t wrlen, void* wrctx, const hio_skad_t* dstaddr)
{
	return 0;
}

static void arp_sck_on_connect (hio_dev_sck_t* dev)
{
printf ("STARTING UP ARP SOCKET %d...\n", dev->hnd);
}

static void arp_sck_on_disconnect (hio_dev_sck_t* dev)
{
printf ("SHUTTING DOWN ARP SOCKET %d...\n", dev->hnd);
}

static int setup_arp_tester (hio_t* hio)
{
	hio_skad_t ethdst;
	hio_etharp_pkt_t etharp;
	hio_dev_sck_make_t sck_make;
	hio_dev_sck_t* sck;
	unsigned int ifindex;

	memset (&sck_make, 0, HIO_SIZEOF(sck_make));
	sck_make.type = HIO_DEV_SCK_ARP;
	//sck_make.type = HIO_DEV_SCK_ARP_DGRAM;
	sck_make.on_write = arp_sck_on_write;
	sck_make.on_read = arp_sck_on_read;
	sck_make.on_connect = arp_sck_on_connect;
	sck_make.on_disconnect = arp_sck_on_disconnect;
	sck = hio_dev_sck_make(hio, 0, &sck_make);
	if (!sck)
	{
		HIO_INFO1 (hio, "Cannot make arp socket device - %js\n", hio_geterrmsg(hio));
		return -1;
	}

	//hio_bcstrtoifindex (hio, "enp0s25.3", &ifindex);
	//hio_skad_init_for_eth (&ethdst, ifindex, (hio_ethad_t*)"\xFF\xFF\xFF\xFF\xFF\xFF");
	//hio_skad_init_for_eth (&ethdst, ifindex, (hio_ethad_t*)"\xAA\xBB\xFF\xCC\xDD\xFF");
	//hio_bcstrtoifindex (hio, "eno1", &ifindex);
	//hio_skad_init_for_eth (&ethdst, ifindex, (hio_ethad_t*)"\xAA\xBB\xFF\xCC\xDD\xFF");
	hio_bcstrtoifindex (hio, "bce0", &ifindex);
	hio_skad_init_for_eth (&ethdst, ifindex, (hio_ethad_t*)"\xAA\xBB\xFF\xCC\xDD\xFF");

	memset (&etharp, 0, HIO_SIZEOF(etharp));

	memcpy (etharp.ethhdr.source, "\xB8\x6B\x23\x9C\x10\x76", HIO_ETHAD_LEN);
	//memcpy (etharp.ethhdr.dest, "\xFF\xFF\xFF\xFF\xFF\xFF", HIO_ETHAD_LEN);
	memcpy (etharp.ethhdr.dest, "\xAA\xBB\xFF\xCC\xDD\xFF", HIO_ETHAD_LEN);
	etharp.ethhdr.proto = HIO_CONST_HTON16(HIO_ETHHDR_PROTO_ARP);

	etharp.arphdr.htype = HIO_CONST_HTON16(HIO_ARPHDR_HTYPE_ETH);
	etharp.arphdr.ptype = HIO_CONST_HTON16(HIO_ARPHDR_PTYPE_IP4);
	etharp.arphdr.hlen = HIO_ETHAD_LEN;
	etharp.arphdr.plen = HIO_IP4AD_LEN;
	etharp.arphdr.opcode = HIO_CONST_HTON16(HIO_ARPHDR_OPCODE_REQUEST);

	memcpy (etharp.arppld.sha, "\xB8\x6B\x23\x9C\x10\x76", HIO_ETHAD_LEN);

	if (hio_dev_sck_write(sck, &etharp, HIO_SIZEOF(etharp), HIO_NULL, &ethdst) <= -1)
	//if (hio_dev_sck_write (sck, &etharp.arphdr, HIO_SIZEOF(etharp) - HIO_SIZEOF(etharp.ethhdr), HIO_NULL, &ethaddr) <= -1)
	{
		HIO_INFO1 (hio, "Cannot write arp - %js\n", hio_geterrmsg(hio));
	}


	return 0;
}

/* ========================================================================= */

struct icmpxtn_t
{
	hio_uint16_t icmp_seq;
	hio_tmridx_t tmout_jobidx;
	int reply_received;
};

typedef struct icmpxtn_t icmpxtn_t;

static int schedule_icmp_wait (hio_dev_sck_t* dev);

static void send_icmp (hio_dev_sck_t* dev, hio_uint16_t seq)
{
	hio_t* hio = dev->hio;
	hio_skad_t dstaddr;
	hio_icmphdr_t* icmphdr;
	hio_uint8_t buf[512];

	//hio_bcstrtoskad (hio, "192.168.9.1", &dstaddr); 
	hio_bcstrtoskad (hio, "192.168.1.1", &dstaddr); 

	memset(buf, 0, HIO_SIZEOF(buf));
	icmphdr = (hio_icmphdr_t*)buf;
	icmphdr->type = HIO_ICMP_ECHO_REQUEST;
	icmphdr->u.echo.id = HIO_CONST_HTON16(100);
	icmphdr->u.echo.seq = hio_hton16(seq);

	memset (&buf[HIO_SIZEOF(*icmphdr)], 'A', HIO_SIZEOF(buf) - HIO_SIZEOF(*icmphdr));
	icmphdr->checksum = hio_checksum_ip(icmphdr, HIO_SIZEOF(buf));

	if (hio_dev_sck_write(dev, buf, HIO_SIZEOF(buf), HIO_NULL, &dstaddr) <= -1)
	{
		HIO_INFO1 (hio, "Cannot write icmp - %js\n", hio_geterrmsg(hio));
		hio_dev_sck_halt (dev);
	}

	if (schedule_icmp_wait(dev) <= -1)
	{
		HIO_INFO1 (hio, "Cannot schedule icmp wait - %js\n", hio_geterrmsg(hio));
		hio_dev_sck_halt (dev);
	}
}

static void on_icmp_due (hio_t* hio, const hio_ntime_t* now, hio_tmrjob_t* tmrjob)
{
	hio_dev_sck_t* dev;
	icmpxtn_t* icmpxtn;

	dev = tmrjob->ctx;
	icmpxtn = (icmpxtn_t*)(dev + 1);

	if (icmpxtn->reply_received)
		icmpxtn->reply_received = 0;
	else
		HIO_INFO0 (hio, "NO IMCP reply received in time\n");

	send_icmp (dev, ++icmpxtn->icmp_seq);
}

static int schedule_icmp_wait (hio_dev_sck_t* dev)
{
	hio_t* hio = dev->hio;
	icmpxtn_t* icmpxtn;
	hio_tmrjob_t tmrjob;
	hio_ntime_t fire_after;

	icmpxtn = (icmpxtn_t*)(dev + 1);
	HIO_INIT_NTIME (&fire_after, 2, 0);

	memset (&tmrjob, 0, HIO_SIZEOF(tmrjob));
	tmrjob.ctx = dev;
	hio_gettime (hio, &tmrjob.when);
	HIO_ADD_NTIME (&tmrjob.when, &tmrjob.when, &fire_after);
	tmrjob.handler = on_icmp_due;
	tmrjob.idxptr = &icmpxtn->tmout_jobidx;

	assert (icmpxtn->tmout_jobidx == HIO_TMRIDX_INVALID);

	return (hio_instmrjob(dev->hio, &tmrjob) == HIO_TMRIDX_INVALID)? -1: 0;
}

static int icmp_sck_on_read (hio_dev_sck_t* dev, const void* data, hio_iolen_t dlen, const hio_skad_t* srcaddr)
{
	icmpxtn_t* icmpxtn;
	hio_iphdr_t* iphdr;
	hio_icmphdr_t* icmphdr;

	/* when received, the data contains the IP header.. */
	icmpxtn = (icmpxtn_t*)(dev + 1);

	if (dlen < HIO_SIZEOF(*iphdr) + HIO_SIZEOF(*icmphdr))
	{
		printf ("INVALID ICMP PACKET.. TOO SHORT...%d\n", (int)dlen);
	}
	else
	{
		/* TODO: consider IP options... */
		iphdr = (hio_iphdr_t*)data;

		if (iphdr->ihl * 4 + HIO_SIZEOF(*icmphdr) > dlen)
		{
			printf ("INVALID ICMP PACKET.. WRONG IHL...%d\n", (int)iphdr->ihl * 4);
		}
		else
		{
			icmphdr = (hio_icmphdr_t*)((hio_uint8_t*)data + (iphdr->ihl * 4));

			/* TODO: check srcaddr against target */

			if (icmphdr->type == HIO_ICMP_ECHO_REPLY && 
			    hio_ntoh16(icmphdr->u.echo.seq) == icmpxtn->icmp_seq) /* TODO: more check.. echo.id.. */
			{
				icmpxtn->reply_received = 1;
				printf ("ICMP REPLY RECEIVED...ID %d SEQ %d\n", (int)hio_ntoh16(icmphdr->u.echo.id), (int)hio_ntoh16(icmphdr->u.echo.seq));
			}
			else
			{
				printf ("GARBAGE ICMP PACKET...LEN %d SEQ %d,%d\n", (int)dlen, (int)icmpxtn->icmp_seq, (int)hio_ntoh16(icmphdr->u.echo.seq));
			}
		}
	}
	return 0;
}


static int icmp_sck_on_write (hio_dev_sck_t* dev, hio_iolen_t wrlen, void* wrctx, const hio_skad_t* dstaddr)
{
	/*icmpxtn_t* icmpxtn;

	icmpxtn = (icmpxtn_t*)(dev + 1); */

	return 0;
}

static void icmp_sck_on_disconnect (hio_dev_sck_t* dev)
{
	icmpxtn_t* icmpxtn;

	icmpxtn = (icmpxtn_t*)(dev + 1);

printf ("SHUTTING DOWN ICMP SOCKET %d...\n", dev->hnd);
	if (icmpxtn->tmout_jobidx != HIO_TMRIDX_INVALID)
	{

		hio_deltmrjob (dev->hio, icmpxtn->tmout_jobidx);
		icmpxtn->tmout_jobidx = HIO_TMRIDX_INVALID;
	}
}

static int setup_ping4_tester (hio_t* hio)
{
	hio_dev_sck_make_t sck_make;
	hio_dev_sck_t* sck;
	icmpxtn_t* icmpxtn;

	memset (&sck_make, 0, HIO_SIZEOF(sck_make));
	sck_make.type = HIO_DEV_SCK_ICMP4;
	sck_make.on_write = icmp_sck_on_write;
	sck_make.on_read = icmp_sck_on_read;
	sck_make.on_disconnect = icmp_sck_on_disconnect;

	sck = hio_dev_sck_make (hio, HIO_SIZEOF(icmpxtn_t), &sck_make);
	if (!sck)
	{
		HIO_INFO1 (hio, "Cannot make ICMP4 socket device - %js\n", hio_geterrmsg(hio));
		return -1;
	}

	icmpxtn = (icmpxtn_t*)(sck + 1);
	icmpxtn->tmout_jobidx = HIO_TMRIDX_INVALID;
	icmpxtn->icmp_seq = 0;

	/*TODO: hio_dev_sck_setbroadcast (sck, 1);*/

	send_icmp (sck, ++icmpxtn->icmp_seq);

	return 0;
}

/* ========================================================================= */
static int pipe_on_read (hio_dev_pipe_t* dev, const void* data, hio_iolen_t dlen)
{
	HIO_INFO3 (dev->hio, "PIPE READ %d bytes - [%.*s]\n", (int)dlen, (int)dlen, data);
	return 0;
}
static int pipe_on_write (hio_dev_pipe_t* dev, hio_iolen_t wrlen, void* wrctx)
{
	HIO_INFO1 (dev->hio, "PIPE WRITTEN %d bytes\n", (int)wrlen);
	return 0;
}

static void pipe_on_close (hio_dev_pipe_t* dev, hio_dev_pipe_sid_t sid)
{
	HIO_INFO1 (dev->hio, "PIPE[%d] CLOSED \n", (int)sid);
}


static int thr_on_read (hio_dev_thr_t* dev, const void* data, hio_iolen_t dlen)
{
	HIO_INFO3 (dev->hio, "THR READ FROM THR - %d bytes - [%.*s]\n", (int)dlen, (int)dlen, data);
	//if (dlen == 0) hio_dev_halt(dev); /* EOF on the input. treat this as end of whole thread transaction */
	return 0;
}

static int thr_on_write (hio_dev_thr_t* dev, hio_iolen_t wrlen, void* wrctx)
{
	HIO_INFO1 (dev->hio, "THR WRITTEN TO THR - %d bytes\n", (int)wrlen);
	return 0;
}

static void thr_on_close (hio_dev_thr_t* dev, hio_dev_thr_sid_t sid)
{
	if (sid == HIO_DEV_THR_OUT) hio_dev_thr_haltslave (dev, HIO_DEV_THR_IN);
	HIO_INFO1 (dev->hio, "THR[%d] CLOSED \n", (int)sid);
}

static void thr_func (hio_t* hio, hio_dev_thr_iopair_t* iop, void* ctx)
{
	hio_bch_t buf[5];
	ssize_t n;

static int x = 0;
int y;
int z = 0;

#if defined(__ATOMIC_RELAXED)
	y = __atomic_add_fetch (&x, 1, __ATOMIC_RELAXED);
#else
	// this is buggy..
	y = ++x;
#endif

	while ((n = read(iop->rfd, buf, HIO_COUNTOF(buf)))> 0) write (iop->wfd, buf, n);

	while (1)
	{
		sleep (1);
		write (iop->wfd, "THR LOOPING", 11);
		z++;
		if ((y % 2) && (z >5)) 
		{
			write (iop->wfd, 0, HIO_NULL);
			break;
		}
	}

}
/* ========================================================================= */

static void on_dnc_resolve(hio_svc_dnc_t* dnc, hio_dns_msg_t* reqmsg, hio_errnum_t status, const void* data, hio_oow_t dlen)
{
	hio_dns_pkt_info_t* pi = (hio_dns_pkt_info_t*)data;

	if (pi) // status == HIO_ENOERR
	{
		hio_uint32_t i;


		printf (">>>>>>>> RRDLEN = %d\n", (int)pi->_rrdlen);
		printf (">>>>>>>> RCODE %s(%d) EDNS exist %d uplen %d version %d dnssecok %d\n", hio_dns_rcode_to_bcstr(pi->hdr.rcode), pi->hdr.rcode, pi->edns.exist, pi->edns.uplen, pi->edns.version, pi->edns.dnssecok);
		if (pi->hdr.rcode == HIO_DNS_RCODE_BADCOOKIE)
		{
			/* TODO: must retry?? there shoudl be no RRs in the payload */
			//if (hio_svc_dnc_resolve(dnc, hio_svc_dnc_getqnamefromreqmsg(reqmsg), hio_svc_dnc_getqtypefromreqmsg(qtype), hio_svc_dnc_getresolflagsfromreqmsg(resolve_flags), on_dnc_resolve, 0) >= 0) return;
		}

		if (hio_svc_dnc_checkclientcookie(dnc, reqmsg, pi) == 0)
		{
			/* client cookie is bad.. */
			printf ("CLIENT COOKIE IS BAD>>>>>>>>>>>>>>>>>>>%d\n", hio_svc_dnc_checkclientcookie(dnc, reqmsg, pi));
		}
		else
		{
			printf ("CLIENT COOKIE IS OK>>>>>>>>>>>>>>>>>>>%d\n", hio_svc_dnc_checkclientcookie(dnc, reqmsg, pi));
		}

		//if (pi->hdr.rcode != HIO_DNS_RCODE_NOERROR) goto no_data;
		if (pi->ancount < 0) goto no_data;

		for (i = 0; i < pi->ancount; i++)
		{
			hio_dns_brr_t* brr = &pi->rr.an[i];
			switch (pi->rr.an[i].rrtype)
			{
				case HIO_DNS_RRT_A:
				{
					struct in_addr ia;
					memcpy (&ia.s_addr, brr->dptr, brr->dlen);
					printf ("^^^  GOT REPLY A........................   %d ", brr->dlen);
					printf ("[%s]", inet_ntoa(ia));
					printf ("\n");
					goto done;
				}
				case HIO_DNS_RRT_AAAA:
				{
					struct in6_addr ia;
					char buf[128];
					memcpy (&ia.s6_addr, brr->dptr, brr->dlen);
					printf ("^^^  GOT REPLY AAAA........................   %d ", brr->dlen);
					printf ("[%s]", inet_ntop(AF_INET6, &ia, buf, HIO_COUNTOF(buf)));
					printf ("\n");
					goto done;
				}
				case HIO_DNS_RRT_CNAME:
					printf ("^^^  GOT REPLY.... CNAME [%s] %d\n", brr->dptr, (int)brr->dlen);
					goto done;
				case HIO_DNS_RRT_MX:
					printf ("^^^  GOT REPLY.... MX [%s] %d\n", brr->dptr, (int)brr->dlen);
					goto done;
				case HIO_DNS_RRT_NS:
					printf ("^^^  GOT REPLY.... NS [%s] %d\n", brr->dptr, (int)brr->dlen);
					goto done;
				case HIO_DNS_RRT_PTR:
					printf ("^^^  GOT REPLY.... PTR [%s] %d\n", brr->dptr, (int)brr->dlen);
					goto done;
				default:
					goto no_data;
			}
		}
		goto no_data;
	}
	else
	{
	no_data:
		if (status == HIO_ETMOUT) printf ("XXXXXXXXXXXXXXXX TIMED OUT XXXXXXXXXXXXXXXXX\n");
		else printf ("XXXXXXXXXXXXXXXXx NO REPLY DATA XXXXXXXXXXXXXXXXXXXXXXXXX\n");
	}

done:
	/* nothing special */
	return;
}

static void on_dnc_resolve_brief (hio_svc_dnc_t* dnc, hio_dns_msg_t* reqmsg, hio_errnum_t status, const void* data, hio_oow_t dlen)
{
	if (data) /* status must be HIO_ENOERR */
	{
		hio_dns_brr_t* brr = (hio_dns_brr_t*)data;

		if (brr->rrtype == HIO_DNS_RRT_AAAA)
		{
			struct in6_addr ia;
			char buf[128];
			memcpy (&ia.s6_addr, brr->dptr, brr->dlen);
			printf ("^^^ SIMPLE -> GOT REPLY........................   %d ", brr->dlen);
			printf ("[%s]", inet_ntop(AF_INET6, &ia, buf, HIO_COUNTOF(buf)));
			printf ("\n");
		}
		else if (brr->rrtype == HIO_DNS_RRT_A)
		{
			struct in_addr ia;
			memcpy (&ia.s_addr, brr->dptr, brr->dlen);
			printf ("^^^ SIMPLE -> GOT REPLY........................   %d ", brr->dlen);
			printf ("[%s]", inet_ntoa(ia));
			printf ("\n");
		}
		else if (brr->rrtype == HIO_DNS_RRT_CNAME)
		{
			printf ("^^^ SIMPLE -> CNAME [%s] %d\n", brr->dptr, (int)brr->dlen);
		}
		else if (brr->rrtype == HIO_DNS_RRT_NS)
		{
			printf ("^^^ SIMPLE -> NS [%s] %d\n", brr->dptr, (int)brr->dlen);
		}
		else if (brr->rrtype == HIO_DNS_RRT_PTR)
		{
			printf ("^^^ SIMPLE -> PTR [%s] %d\n", brr->dptr, (int)brr->dlen);
		}
		else if (brr->rrtype == HIO_DNS_RRT_SOA)
		{
			hio_dns_brrd_soa_t* soa = brr->dptr;
			printf ("^^^ SIMPLE -> SOA [%s] [%s] [%u %u %u %u %u] %d\n", soa->mname, soa->rname, (unsigned)soa->serial, (unsigned)soa->refresh, (unsigned)soa->retry, (unsigned)soa->expire, (unsigned)soa->minimum, (int)brr->dlen);
		}
		else
		{
			printf ("^^^ SIMPLE -> UNKNOWN DATA... [%.*s] %d\n", (int)brr->dlen, brr->dptr, (int)brr->dlen);
		}
	}
	else
	{
		if (status == HIO_ETMOUT) printf ("QQQQQQQQQQQQQQQQQQQ TIMED OUT QQQQQQQQQQQQQQQQQ\n");
		else printf ("QQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQ NO REPLY DATA QQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQq - %d\n", hio_geterrnum(hio_svc_dnc_gethio(dnc)));
	}
}

static int print_qparam (hio_bcs_t* key, hio_bcs_t* val, void* ctx)
{
	key->len = hio_perdec_http_bcs(key, key->ptr, HIO_NULL);
	val->len = hio_perdec_http_bcs(val, val->ptr, HIO_NULL);
	fprintf ((FILE*)ctx, "\t[%.*s] = [%.*s]\n", (int)key->len, key->ptr, (int)val->len, val->ptr);
	return 0;
}

static void on_htts_thr_request (hio_t* hio, hio_dev_thr_iopair_t* iop, hio_svc_htts_thr_func_info_t* tfi, void* ctx)
{
	FILE* fp;
	int i;

	if (tfi->req_method != HIO_HTTP_GET)
	{
		write (iop->wfd, "Status: 405\r\n\r\n", 15); /* method not allowed */
		return;
	}

	fp = fdopen(iop->wfd, "w");
	if (!fp)
	{
		write (iop->wfd, "Status: 500\r\n\r\n", 15); /* internal server error */
		return;
	}

	fprintf (fp, "Status: 200\r\n");
	fprintf (fp, "Content-Type: text/html\r\n\r\n");

	fprintf (fp, "request path = %s\n", tfi->req_path);
	if (tfi->req_param) 
	{
		fprintf (fp, "request params:\n");
		hio_scan_http_qparam (tfi->req_param, print_qparam, fp);
	}
	for (i = 0; i < 100; i++) fprintf (fp, "%d * %d => %d\n", i, i, i * i);

	fclose (fp);
	iop->wfd = HIO_SYSHND_INVALID;
}

/* ========================================================================= */
int process_http_request (hio_svc_htts_t* htts, hio_dev_sck_t* csck, hio_htre_t* req)
{
	hio_t* hio = hio_svc_htts_gethio(htts);
//	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(csck);
	hio_http_method_t mth;

	/* percent-decode the query path to the original buffer
	 * since i'm not going to need it in the original form
	 * any more. once it's decoded in the peek mode,
	 * the decoded query path is made available in the
	 * non-peek mode as well */

	HIO_DEBUG2 (hio, "[RAW-REQ] %s %s\n", hio_htre_getqmethodname(req), hio_htre_getqpath(req));

	hio_htre_perdecqpath(req);
	/* TODO: proper request logging */

	HIO_DEBUG2 (hio, "[REQ] %s %s\n", hio_htre_getqmethodname(req), hio_htre_getqpath(req));

#if 0
hio_printf (HIO_T("================================\n"));
hio_printf (HIO_T("[%lu] %hs REQUEST ==> [%hs] version[%d.%d %hs] method[%hs]\n"),
	(unsigned long)time(NULL),
	(peek? HIO_MT("PEEK"): HIO_MT("HANDLE")),
	hio_htre_getqpath(req),
	hio_htre_getmajorversion(req),
	hio_htre_getminorversion(req),
	hio_htre_getverstr(req),
	hio_htre_getqmethodname(req)
);
if (hio_htre_getqparam(req))
	hio_printf (HIO_T("PARAMS ==> [%hs]\n"), hio_htre_getqparam(req));

hio_htb_walk (&req->hdrtab, walk, HIO_NULL);
if (hio_htre_getcontentlen(req) > 0)
{
	hio_printf (HIO_T("CONTENT [%.*S]\n"), (int)hio_htre_getcontentlen(req), hio_htre_getcontentptr(req));
}
#endif

	mth = hio_htre_getqmethodtype(req);
	/* determine what to do once the header fields are all received.
	 * i don't want to delay this until the contents are received.
	 * if you don't like this behavior, you must implement your own
	 * callback function for request handling. */
#if 0
	/* TODO support X-HTTP-Method-Override */
	if (data.method == HIO_HTTP_POST)
	{
		tmp = hio_htre_getheaderval(req, HIO_MT("X-HTTP-Method-Override"));
		if (tmp)
		{
			/*while (tmp->next) tmp = tmp->next;*/ /* get the last value */
			data.method = hio_mbstohttpmethod (tmp->ptr);
		}
	}
#endif

#if 0
	if (mth == HIO_HTTP_CONNECT)
	{
		/* CONNECT method must not have content set. 
		 * however, arrange to discard it if so. 
		 *
		 * NOTE: CONNECT is implemented to ignore many headers like
		 *       'Expect: 100-continue' and 'Connection: keep-alive'. */
		hio_htre_discardcontent (req);
	}
	else 
	{
/* this part can be checked in actual hio_svc_htts_doXXX() functions.
 * some doXXX handlers may not require length for POST.
 * it may be able to simply accept till EOF? or  treat as if CONTENT_LENGTH is 0*/
		if (mth == HIO_HTTP_POST && !(req->flags & (HIO_HTRE_ATTR_LENGTH | HIO_HTRE_ATTR_CHUNKED)))
		{
			/* POST without Content-Length nor not chunked */
			hio_htre_discardcontent (req); 
			/* 411 Length Required - can't keep alive. Force disconnect */
			req->flags &= ~HIO_HTRE_ATTR_KEEPALIVE; /* to cause sendstatus() to close */
			if (hio_svc_htts_sendstatus(htts, csck, req, 411, HIO_NULL) <= -1) goto oops;
		}
		else

		{
#endif
			const hio_bch_t* qpath = hio_htre_getqpath(req);
			int x;
			if (hio_comp_bcstr_limited(qpath, "/thr/", 5, 1) == 0)
				x = hio_svc_htts_dothr(htts, csck, req, on_htts_thr_request, HIO_NULL);
			else if (hio_comp_bcstr_limited(qpath, "/txt/", 5, 1) == 0)
				x = hio_svc_htts_dotxt(htts, csck, req, 200, "text/plain", qpath);
			else if (hio_comp_bcstr_limited(qpath, "/cgi/", 5, 1) == 0)
				x = hio_svc_htts_docgi(htts, csck, req, "", hio_htre_getqpath(req));
			else
				x = hio_svc_htts_dofile(htts, csck, req, "", hio_htre_getqpath(req), "text/plain");
			if (x <= -1) goto oops;

	return 0;

oops:
	hio_dev_sck_halt (csck);
	return -1;
}

/* ========================================================================= */

static hio_t* g_hio;

static void handle_signal (int sig)
{
	if (g_hio) hio_stop (g_hio, HIO_STOPREQ_TERMINATION);
}

static void send_test_query (hio_t* hio, const hio_ntime_t* now, hio_tmrjob_t* job)
{
	//if (!hio_svc_dnc_resolve((hio_svc_dnc_t*)job->ctx, "www.microsoft.com", HIO_DNS_RRT_CNAME, HIO_SVC_DNC_RESOLVE_FLAG_COOKIE, on_dnc_resolve, 0))
	if (!hio_svc_dnc_resolve((hio_svc_dnc_t*)job->ctx, "mailserver.manyhost.net", HIO_DNS_RRT_A, HIO_SVC_DNC_RESOLVE_FLAG_COOKIE | HIO_SVC_DNC_RESOLVE_FLAG_DNSSEC, on_dnc_resolve, 0))
	{
		printf ("resolve attempt failure ---> mailserver.manyhost.net\n");
	}

	if (!hio_svc_dnc_resolve((hio_svc_dnc_t*)job->ctx, "ns2.switch.ch", HIO_DNS_RRT_A, HIO_SVC_DNC_RESOLVE_FLAG_COOKIE | HIO_SVC_DNC_RESOLVE_FLAG_DNSSEC, on_dnc_resolve, 0))
	{
		printf ("resolve attempt failure ---> ns2.switch.ch\n");
	}
}

int main (int argc, char* argv[])
{
	int i;

	hio_t* hio;
	hio_dev_sck_t* tcp[3];

	struct sigaction sigact;
	hio_dev_sck_connect_t tcp_conn;
	hio_dev_sck_listen_t tcp_lstn;
	hio_dev_sck_bind_t tcp_bind;
	hio_dev_sck_make_t tcp_make;

	tcp_server_t* ts;

#if defined(USE_SSL)
	SSL_load_error_strings ();
	SSL_library_init ();
#endif

	hio = hio_open(&mmgr, 0, HIO_NULL, HIO_FEATURE_ALL, 512, HIO_NULL);
	if (!hio)
	{
		printf ("Cannot open hio\n");
		return -1;
	}

	g_hio = hio;

	memset (&sigact, 0, HIO_SIZEOF(sigact));
	sigact.sa_flags = SA_RESTART;
	sigact.sa_handler = handle_signal;
	sigaction (SIGINT, &sigact, HIO_NULL);

	memset (&sigact, 0, HIO_SIZEOF(sigact));
	sigact.sa_handler = SIG_IGN;
	sigaction (SIGPIPE, &sigact, HIO_NULL);

/*
	memset (&sigact, 0, HIO_SIZEOF(sigact));
	sigact.sa_handler = SIG_IGN;
	sigaction (SIGCHLD, &sigact, HIO_NULL);
*/

	/*memset (&sin, 0, HIO_SIZEOF(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(1234); */
/*
	udp = (hio_dev_udp_t*)hio_dev_make(hio, HIO_SIZEOF(*udp), &udp_mth, &udp_evcb, &sin);
	if (!udp)
	{
		printf ("Cannot make udp\n");
		goto oops;
	}
*/

	memset (&tcp_make, 0, HIO_SIZEOF(tcp_make));
	tcp_make.type = HIO_DEV_SCK_TCP4;
	tcp_make.on_write = tcp_sck_on_write;
	tcp_make.on_read = tcp_sck_on_read;
	tcp_make.on_connect = tcp_sck_on_connect;
	tcp_make.on_disconnect = tcp_sck_on_disconnect;
	tcp[0] = hio_dev_sck_make(hio, HIO_SIZEOF(tcp_server_t), &tcp_make);
	if (!tcp[0])
	{
		HIO_INFO1 (hio, "Cannot make tcp - %js\n", hio_geterrmsg(hio));
		goto oops;
	}

	ts = (tcp_server_t*)(tcp[0] + 1);
	ts->tally = 0;

	memset (&tcp_conn, 0, HIO_SIZEOF(tcp_conn));
	/* openssl s_server -accept 9999 -key localhost.key  -cert localhost.crt */
	hio_bcstrtoskad(hio, "127.0.0.1:9999", &tcp_conn.remoteaddr);

	HIO_INIT_NTIME (&tcp_conn.connect_tmout, 5, 0);
	tcp_conn.options = HIO_DEV_SCK_CONNECT_SSL;
	if (hio_dev_sck_connect(tcp[0], &tcp_conn) <= -1)
	{
		HIO_INFO1 (hio, "tcp[0] hio_dev_sck_connect() failed - %js\n", hio_geterrmsg(hio));
		/* carry on regardless of failure */
	}

	/* -------------------------------------------------------------- */
	memset (&tcp_make, 0, HIO_SIZEOF(tcp_make));
	tcp_make.type = HIO_DEV_SCK_TCP4;
	tcp_make.on_write = tcp_sck_on_write;
	tcp_make.on_read = tcp_sck_on_read;
	tcp_make.on_connect = tcp_sck_on_connect;
	tcp_make.on_disconnect = tcp_sck_on_disconnect;

	tcp[1] = hio_dev_sck_make(hio, HIO_SIZEOF(tcp_server_t), &tcp_make);
	if (!tcp[1])
	{
		HIO_INFO1 (hio, "Cannot make tcp[1] - %js\n", hio_geterrmsg(hio));
		goto oops;
	}
	ts = (tcp_server_t*)(tcp[1] + 1);
	ts->tally = 0;

	memset (&tcp_bind, 0, HIO_SIZEOF(tcp_bind));
	hio_bcstrtoskad(hio, "0.0.0.0:1234", &tcp_bind.localaddr);
	tcp_bind.options = HIO_DEV_SCK_BIND_REUSEADDR;

	if (hio_dev_sck_bind(tcp[1],&tcp_bind) <= -1)
	{
		HIO_INFO1 (hio, "tcp[1] hio_dev_sck_bind() failed - %js\n", hio_geterrmsg(hio));
		goto oops;
	}


	memset (&tcp_lstn, 0, HIO_SIZEOF(tcp_lstn));
	tcp_lstn.backlogs = 100;
	if (hio_dev_sck_listen(tcp[1], &tcp_lstn) <= -1)
	{
		HIO_INFO1 (hio, "tcp[1] hio_dev_sck_listen() failed - %js\n", hio_geterrmsg(hio));
		goto oops;
	}


	/* -------------------------------------------------------------- */
	memset (&tcp_make, 0, HIO_SIZEOF(tcp_make));
	tcp_make.type = HIO_DEV_SCK_TCP4;
	tcp_make.on_write = tcp_sck_on_write;
	tcp_make.on_read = tcp_sck_on_read;
	tcp_make.on_connect = tcp_sck_on_connect;
	tcp_make.on_disconnect = tcp_sck_on_disconnect;

	tcp[2] = hio_dev_sck_make(hio, HIO_SIZEOF(tcp_server_t), &tcp_make);
	if (!tcp[2])
	{
		HIO_INFO1 (hio, "Cannot make tcp[2] - %js\n", hio_geterrmsg(hio));
		goto oops;
	}
	ts = (tcp_server_t*)(tcp[2] + 1);
	ts->tally = 0;


	memset (&tcp_bind, 0, HIO_SIZEOF(tcp_bind));
	hio_bcstrtoskad(hio, "0.0.0.0:1235", &tcp_bind.localaddr);
	tcp_bind.options = HIO_DEV_SCK_BIND_REUSEADDR /*| HIO_DEV_SCK_BIND_REUSEPORT |*/;
#if defined(USE_SSL)
	tcp_bind.options |= HIO_DEV_SCK_BIND_SSL; 
	tcp_bind.ssl_certfile = "localhost.crt";
	tcp_bind.ssl_keyfile = "localhost.key";
#endif

	if (hio_dev_sck_bind(tcp[2], &tcp_bind) <= -1)
	{
		HIO_INFO1 (hio, "tcp[2] hio_dev_sck_bind() failed - %js\n", hio_geterrmsg(hio));
		goto oops;
	}

	tcp_lstn.backlogs = 100;
	HIO_INIT_NTIME (&tcp_lstn.accept_tmout, 5, 1);
	if (hio_dev_sck_listen(tcp[2], &tcp_lstn) <= -1)
	{
		HIO_INFO1 (hio, "tcp[2] hio_dev_sck_listen() failed - %js\n", hio_geterrmsg(hio));
		goto oops;
	}

	//hio_dev_sck_sendfile (tcp[2], fd, offset, count);

	setup_arp_tester(hio);
	setup_ping4_tester(hio);

#if 0
for (i = 0; i < 5; i++)
{
	hio_dev_pro_t* pro;
	hio_dev_pro_make_t pro_make;

	memset (&pro_make, 0, HIO_SIZEOF(pro_make));
	pro_make.flags = HIO_DEV_PRO_READOUT | HIO_DEV_PRO_READERR | HIO_DEV_PRO_WRITEIN /*| HIO_DEV_PRO_FORGET_CHILD*/;
	//pro_make.cmd = "/bin/ls -laF /usr/bin";
	//pro_make.cmd = "/bin/ls -laF";
	pro_make.cmd = "./a";
	pro_make.on_read = pro_on_read;
	pro_make.on_write = pro_on_write;
	pro_make.on_close = pro_on_close;

	pro = hio_dev_pro_make(hio, 0, &pro_make);
	if (!pro)
	{
		HIO_INFO1 (hio, "CANNOT CREATE PROCESS PIPE - %js\n", hio_geterrmsg(hio));
		goto oops;
	}
	hio_dev_pro_read (pro, HIO_DEV_PRO_OUT, 0);
	//hio_dev_pro_read (pro, HIO_DEV_PRO_ERR, 0);

	hio_dev_pro_write (pro, "MY HIO LIBRARY\n", 16, HIO_NULL);
//hio_dev_pro_killchild (pro); 
//hio_dev_pro_close (pro, HIO_DEV_PRO_IN); 
//hio_dev_pro_close (pro, HIO_DEV_PRO_OUT); 
//hio_dev_pro_close (pro, HIO_DEV_PRO_ERR); 
}
#endif

{
	hio_svc_dnc_t* dnc;
	hio_svc_htts_t* htts;
	hio_ntime_t send_tmout, reply_tmout;
	hio_skad_t servaddr;
	hio_dev_sck_bind_t htts_bind_info;

	send_tmout.sec = 0;
	send_tmout.nsec = 0;
	reply_tmout.sec = 1;
	reply_tmout.nsec = 0;

	//hio_bcstrtoskad (hio, "8.8.8.8:53", &servaddr);
	hio_bcstrtoskad (hio, "198.41.0.4:53", &servaddr); // a.root-servers.net
	//hio_bcstrtoskad (hio, "130.59.31.29:53", &servaddr); // ns2.switch.ch
	//hio_bcstrtoskad (hio, "134.119.216.86:53", &servaddr); // ns.manyhost.net
	//hio_bcstrtoskad (hio, "[fe80::c7e2:bd6e:1209:ac1b]:1153", &servaddr);
	//hio_bcstrtoskad (hio, "[fe80::c7e2:bd6e:1209:ac1b%eno1]:1153", &servaddr);

	memset (&htts_bind_info, 0, HIO_SIZEOF(htts_bind_info));
	//hio_bcstrtoskad (hio, "[""]:9988", &htts_bind_info.localaddr);
	hio_bcstrtoskad (hio, "0.0.0.0:9988", &htts_bind_info.localaddr);
	htts_bind_info.options = HIO_DEV_SCK_BIND_REUSEADDR | HIO_DEV_SCK_BIND_REUSEPORT | HIO_DEV_SCK_BIND_IGNERR;
	//htts_bind_info.options |= HIO_DEV_SCK_BIND_SSL; 
	htts_bind_info.ssl_certfile = "localhost.crt";
	htts_bind_info.ssl_keyfile = "localhost.key";

	dnc = hio_svc_dnc_start(hio, &servaddr, HIO_NULL, &send_tmout, &reply_tmout, 2); /* option - send to all, send one by one */
	if (!dnc)
	{
		HIO_INFO1 (hio, "UNABLE TO START DNC - %js\n", hio_geterrmsg(hio));
	}

	htts = hio_svc_htts_start(hio, &htts_bind_info, process_http_request);
	if (htts) hio_svc_htts_setservernamewithbcstr (htts, "HIO-HTTP");
	else HIO_INFO1 (hio, "UNABLE TO START HTTS - %js\n", hio_geterrmsg(hio));

#if 0
	if (dnc)
	{
		hio_dns_bqr_t qrs[] = 
		{
			{ "code.miflux.com",  HIO_DNS_RRT_A,    HIO_DNS_RRC_IN },
			{ "code.miflux.com",  HIO_DNS_RRT_AAAA, HIO_DNS_RRC_IN },
			{ "code.abiyo.net",   HIO_DNS_RRT_A,    HIO_DNS_RRC_IN },
			{ "code6.abiyo.net",  HIO_DNS_RRT_AAAA, HIO_DNS_RRC_IN },
			{ "abiyo.net",        HIO_DNS_RRT_MX,   HIO_DNS_RRC_IN }
		};

		hio_ip4ad_t rrdata_a = { { 4, 3, 2, 1 } };
		hio_ip6ad_t rrdata_aaaa = { { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, }};

		hio_dns_brrd_soa_t miflux_soa_data = 
		{
			"ns9.dnszi.com", "root.dnszi.com", 2019091905, 43200, 3600, 1209600, 1100
		};

		hio_dns_brr_t rrs[] = 
		{
			{ HIO_DNS_RR_PART_ANSWER,    "code.miflux.com",  HIO_DNS_RRT_A,     HIO_DNS_RRC_IN, 86400, HIO_SIZEOF(rrdata_a),    &rrdata_a },
			{ HIO_DNS_RR_PART_ANSWER,    "code.miflux.com",  HIO_DNS_RRT_AAAA,  HIO_DNS_RRC_IN, 86400, HIO_SIZEOF(rrdata_aaaa), &rrdata_aaaa },
			{ HIO_DNS_RR_PART_ANSWER,    "miflux.com",       HIO_DNS_RRT_NS,    HIO_DNS_RRC_IN, 86400, 0,  "ns1.miflux.com" },
			{ HIO_DNS_RR_PART_ANSWER,    "miflux.com",       HIO_DNS_RRT_NS,    HIO_DNS_RRC_IN, 86400, 0,  "ns2.miflux.com" },
			{ HIO_DNS_RR_PART_AUTHORITY, "miflux.com",       HIO_DNS_RRT_SOA,    HIO_DNS_RRC_IN, 86400, HIO_SIZEOF(miflux_soa_data),  &miflux_soa_data }, //, 
			//{ HIO_DNS_RR_PART_ANSERT,    "www.miflux.com",   HIO_DNS_RRT_CNAME, HIO_DNS_RRC_IN, 60,    15, "code.miflux.com" }  
		};

		hio_dns_beopt_t beopt[] =
		{
			{ HIO_DNS_EOPT_COOKIE, 8, "\x01\x02\x03\x04\0x05\x06\0x07\0x08" },
			{ HIO_DNS_EOPT_NSID,   0, HIO_NULL                              }
		};

		hio_dns_bedns_t qedns =
		{
			4096, /* uplen */

			0,    /* edns version */
			0,    /* dnssec ok */

			HIO_COUNTOF(beopt),    /* number of edns options */
			beopt
		};

		hio_dns_bhdr_t qhdr =
		{
			-1,              /* id */
			0,                  /* qr */
			HIO_DNS_OPCODE_QUERY, /* opcode */
			0, /* aa */
			0, /* tc */
			1, /* rd */
			0, /* ra */
			0, /* ad */
			0, /* cd */
			HIO_DNS_RCODE_NOERROR /* rcode */
		};

		hio_dns_bhdr_t rhdr =
		{
			0x1234,               /* id */
			1,                    /* qr */
			HIO_DNS_OPCODE_QUERY, /* opcode */

			0, /* aa */
			0, /* tc */
			0, /* rd */
			1, /* ra */
			0, /* ad */
			0, /* cd */
			HIO_DNS_RCODE_BADCOOKIE /* rcode */
		}; 

		hio_svc_dnc_sendreq (dnc, &qhdr, &qrs[0], &qedns, 0, HIO_NULL, 0);
		hio_svc_dnc_sendmsg (dnc, &rhdr, qrs, HIO_COUNTOF(qrs), rrs, HIO_COUNTOF(rrs), &qedns, 0, HIO_NULL, 0);
	}
#endif

	if (dnc)
	{
		hio_ntime_t x;
		HIO_INIT_NTIME (&x, 5, 0);
		hio_schedtmrjobafter (hio, &x, send_test_query, HIO_NULL, dnc);

		if (!hio_svc_dnc_resolve(dnc, "b.wild.com", HIO_DNS_RRT_A, HIO_SVC_DNC_RESOLVE_FLAG_PREFER_TCP, on_dnc_resolve, 0))
		{
			printf ("resolve attempt failure ---> a.wild.com\n");
		}
		
		if (!hio_svc_dnc_resolve(dnc, "www.microsoft.com", HIO_DNS_RRT_CNAME, HIO_SVC_DNC_RESOLVE_FLAG_COOKIE, on_dnc_resolve, 0))
		{
			printf ("resolve attempt failure ---> www.microsoft.com\n");
		}

		if (!hio_svc_dnc_resolve(dnc, "ns2.switch.ch", HIO_DNS_RRT_CNAME, HIO_SVC_DNC_RESOLVE_FLAG_COOKIE, on_dnc_resolve, 0))
		{
			printf ("resolve attempt failure ---> ns2.switch.ch\n");
		}
		
		
		//if (!hio_svc_dnc_resolve(dnc, "www.microsoft.com", HIO_DNS_RRT_A, HIO_SVC_DNC_RESOLVE_FLAG_BRIEF, on_dnc_resolve_brief, 0))
		if (!hio_svc_dnc_resolve(dnc, "code.miflux.com", HIO_DNS_RRT_A, HIO_SVC_DNC_RESOLVE_FLAG_BRIEF | HIO_SVC_DNC_RESOLVE_FLAG_PREFER_TCP, on_dnc_resolve_brief, 0))
		{
			printf ("resolve attempt failure ---> code.miflux.com\n");
		}
		
		if (!hio_svc_dnc_resolve(dnc, "45.77.246.105.in-addr.arpa", HIO_DNS_RRT_PTR, HIO_SVC_DNC_RESOLVE_FLAG_BRIEF, on_dnc_resolve_brief, 0))
		{
			printf ("resolve attempt failure ---> 45.77.246.105.in-addr.arpa.\n");
		}
		
		#if 0
		if (!hio_svc_dnc_resolve(dnc, "1.1.1.1.in-addr.arpa", HIO_DNS_RRT_PTR, HIO_SVC_DNC_RESOLVE_FLAG_BRIEF, on_dnc_resolve_brief, 0))
		{
			printf ("resolve attempt failure ---> 1.1.1.1.in-addr.arpa\n");
		}
		
		//if (!hio_svc_dnc_resolve(dnc, "ipv6.google.com", HIO_DNS_RRT_AAAA, HIO_SVC_DNC_RESOLVE_FLAG_BRIEF, on_dnc_resolve_brief, 0))
		if (!hio_svc_dnc_resolve(dnc, "google.com", HIO_DNS_RRT_SOA, HIO_SVC_DNC_RESOLVE_FLAG_BRIEF, on_dnc_resolve_brief, 0))
		//if (!hio_svc_dnc_resolve(dnc, "google.com", HIO_DNS_RRT_NS, HIO_SVC_DNC_RESOLVE_FLAG_BRIEF, on_dnc_resolve_brief, 0))
		{
			printf ("resolve attempt failure ---> code.miflux.com\n");
		}
		#endif
	}

#if 0
{
	hio_dev_pipe_t* pp;
	hio_dev_pipe_make_t mi;
	mi.on_read = pipe_on_read;
	mi.on_write = pipe_on_write;
	mi.on_close = pipe_on_close;
	pp = hio_dev_pipe_make(hio, 0, &mi);
	hio_dev_pipe_write (pp, "this is good", 12, HIO_NULL);
}

for (i = 0; i < 20; i++)
{
	hio_dev_thr_t* tt;
	hio_dev_thr_make_t mi;
	mi.thr_func = thr_func;
	mi.thr_ctx = HIO_NULL;
	mi.on_read = thr_on_read;
	mi.on_write = thr_on_write;
	mi.on_close =  thr_on_close;
	tt = hio_dev_thr_make(hio, 0, &mi);
	hio_dev_thr_write (tt, "hello, world", 12, HIO_NULL);
	hio_dev_thr_write (tt, HIO_NULL, 0, HIO_NULL);
}
#endif

	hio_loop (hio);

	/* TODO: let hio close it ... dnc is svc. sck is dev. */
	if (htts) hio_svc_htts_stop (htts);
	if (dnc) hio_svc_dnc_stop (dnc);
}

	g_hio = HIO_NULL;
	hio_close (hio);
#if defined(USE_SSL)
	cleanup_openssl ();
#endif

	return 0;

oops:
	g_hio = HIO_NULL;
	hio_close (hio);
#if defined(USE_SSL)
	cleanup_openssl ();
#endif
	return -1;
}

