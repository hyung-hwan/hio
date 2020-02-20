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


#include <mio.h>
#include <mio-utl.h>
#include <mio-sck.h>
#include <mio-pro.h>

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

#if 0
	if (((mmgr_stat_t*)mmgr->ctx)->total_count > 300)
	{
printf ("CRITICAL ERROR ---> too many heap chunks...\n");
		return MIO_NULL;
	}
#endif

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
	mio_bch_t buf1[128], buf2[128];

	mio_skadtobcstr (tcp->mio, &tcp->localaddr, buf1, MIO_COUNTOF(buf1), MIO_SKAD_TO_BCSTR_ADDR | MIO_SKAD_TO_BCSTR_PORT);
	mio_skadtobcstr (tcp->mio, &tcp->remoteaddr, buf2, MIO_COUNTOF(buf2), MIO_SKAD_TO_BCSTR_ADDR | MIO_SKAD_TO_BCSTR_PORT);

	if (tcp->state & MIO_DEV_SCK_CONNECTED)
	{
		MIO_INFO3 (tcp->mio, "DEVICE connected to a remote server... LOCAL %hs REMOTE %hs SCK: %d\n", buf1, buf2, tcp->sck);
	}
	else if (tcp->state & MIO_DEV_SCK_ACCEPTED)
	{
		MIO_INFO3 (tcp->mio, "DEVICE accepted client device... .LOCAL %hs REMOTE %hs SCK: %d\n", buf1, buf2, tcp->sck);
	}

	if (mio_dev_sck_write(tcp, "hello", 5, MIO_NULL, MIO_NULL) <= -1)
	{
		mio_dev_sck_halt (tcp);
	}
}

static int tcp_sck_on_write (mio_dev_sck_t* tcp, mio_iolen_t wrlen, void* wrctx, const mio_skad_t* dstaddr)
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

static int tcp_sck_on_read (mio_dev_sck_t* tcp, const void* buf, mio_iolen_t len, const mio_skad_t* srcaddr)
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

static mio_t* g_mio;

static void handle_signal (int sig)
{
	if (g_mio) mio_stop (g_mio, MIO_STOPREQ_TERMINATION);
}

int main (int argc, char* argv[])
{
	mio_t* mio = MIO_NULL;
	mio_dev_sck_t* tcpsvr;
	mio_dev_sck_make_t tcp_make;
	mio_dev_sck_connect_t tcp_conn;
	tcp_server_t* ts;

	mio = mio_open(&mmgr, 0, MIO_NULL, 512, MIO_NULL);
	if (!mio)
	{
		printf ("Cannot open mio\n");
		goto oops;
	}

	memset (&tcp_make, 0, MIO_SIZEOF(tcp_make));
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
	mio_bcstrtoskad(mio, "127.0.0.1:9999", &tcp_conn.remoteaddr);
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
