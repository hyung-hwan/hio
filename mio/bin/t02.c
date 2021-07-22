

#include <mio.h>
#include <mio-utl.h>
#include <mio-sck.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct mmgr_stat_t
{
	mio_oow_t total_count;
};

typedef struct mmgr_stat_t mmgr_stat_t;

static mmgr_stat_t mmgr_stat;

static void* mmgr_alloc (mio_mmgr_t* mmgr, mio_oow_t size)
{
	void* x;

	if (((mmgr_stat_t*)mmgr->ctx)->total_count > 3000)
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


struct tcp_xtn_t
{
	int tally;
};
typedef struct tcp_xtn_t tcp_xtn_t;


static void tcp_sck_on_disconnect (mio_dev_sck_t* tcp)
{
	switch (MIO_DEV_SCK_GET_PROGRESS(tcp))
	{
		case MIO_DEV_SCK_CONNECTING:
			MIO_INFO1 (tcp->mio, "OUTGOING SESSION DISCONNECTED - FAILED TO CONNECT (%d) TO REMOTE SERVER\n", (int)tcp->hnd);
			break;

		case MIO_DEV_SCK_CONNECTING_SSL:
			MIO_INFO1 (tcp->mio, "OUTGOING SESSION DISCONNECTED - FAILED TO SSL-CONNECT (%d) TO REMOTE SERVER\n", (int)tcp->hnd);
			break;

		case MIO_DEV_SCK_LISTENING:
			MIO_INFO1 (tcp->mio, "SHUTTING DOWN THE SERVER SOCKET(%d)...\n", (int)tcp->hnd);
			break;

		case MIO_DEV_SCK_CONNECTED:
			MIO_INFO1 (tcp->mio, "OUTGOING CLIENT CONNECTION GOT TORN DOWN(%d).......\n", (int)tcp->hnd);
			break;

		case MIO_DEV_SCK_ACCEPTING_SSL:
			MIO_INFO1 (tcp->mio, "INCOMING SSL-ACCEPT GOT DISCONNECTED(%d) ....\n", (int)tcp->hnd);
			break;

		case MIO_DEV_SCK_ACCEPTED:
			MIO_INFO1 (tcp->mio, "INCOMING CLIENT BEING SERVED GOT DISCONNECTED(%d).......\n", (int)tcp->hnd);
			break;

		default:
			MIO_INFO2 (tcp->mio, "SOCKET DEVICE DISCONNECTED (%d - %x)\n", (int)tcp->hnd, (unsigned int)tcp->state);
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
		MIO_INFO3 (tcp->mio, "DEVICE connected to a remote server... LOCAL %hs REMOTE %hs SCK: %d\n", buf1, buf2, tcp->hnd);
	}
	else if (tcp->state & MIO_DEV_SCK_ACCEPTED)
	{
		MIO_INFO3 (tcp->mio, "DEVICE accepted client device... .LOCAL %hs REMOTE %hs SCK: %d\n", buf1, buf2, tcp->hnd);
	}

	if (mio_dev_sck_write(tcp, "hello", 5, MIO_NULL, MIO_NULL) <= -1)
	{
		mio_dev_sck_halt (tcp);
	}
}

static int tcp_sck_on_write (mio_dev_sck_t* tcp, mio_iolen_t wrlen, void* wrctx, const mio_skad_t* dstaddr)
{
	tcp_xtn_t* ts;
	mio_ntime_t tmout;

	if (wrlen <= -1)
	{
		MIO_INFO1 (tcp->mio, "TCP_SCK_ON_WRITE(%d) >>> SEDING TIMED OUT...........\n", (int)tcp->hnd);
		mio_dev_sck_halt (tcp);
	}
	else
	{
		ts = (tcp_xtn_t*)(tcp + 1);
		if (wrlen == 0)
		{
			MIO_INFO1 (tcp->mio, "TCP_SCK_ON_WRITE(%d) >>> CLOSED WRITING END\n", (int)tcp->hnd);
		}
		else
		{
			MIO_INFO3 (tcp->mio, "TCP_SCK_ON_WRITE(%d) >>> SENT MESSAGE %d of length %ld\n", (int)tcp->hnd, ts->tally, (long int)wrlen);
		}

		ts->tally++;
	//	if (ts->tally >= 2) mio_dev_sck_halt (tcp);

		
		MIO_INIT_NTIME (&tmout, 5, 0);
		//mio_dev_sck_read (tcp, 1);

		MIO_INFO3 (tcp->mio, "TCP_SCK_ON_WRITE(%d) >>> REQUESTING to READ with timeout of %ld.%08ld\n", (int)tcp->hnd, (long int)tmout.sec, (long int)tmout.nsec);
		mio_dev_sck_timedread (tcp, 1, &tmout);
	}
	return 0;
}

static int tcp_sck_on_read (mio_dev_sck_t* tcp, const void* buf, mio_iolen_t len, const mio_skad_t* srcaddr)
{
	int n;

	if (len <= -1)
	{
		MIO_INFO1 (tcp->mio, "TCP_SCK_ON_READ(%d) STREAM DEVICE: TIMED OUT...\n", (int)tcp->hnd);
		mio_dev_sck_halt (tcp);
		return 0;
	}
	else if (len <= 0)
	{
		MIO_INFO1 (tcp->mio, "TCP_SCK_ON_READ(%d) STREAM DEVICE: EOF RECEIVED...\n", (int)tcp->hnd);
		/* no outstanding request. but EOF */
		mio_dev_sck_halt (tcp);
		return 0;
	}

	MIO_INFO2 (tcp->mio, "TCP_SCK_ON_READ(%d) - received %d bytes\n", (int)tcp->hnd, (int)len);

	{
		mio_ntime_t tmout;

		static char a ='A';
		static char xxx[1000000];
		memset (xxx, a++ , MIO_SIZEOF(xxx));

		MIO_INFO2 (tcp->mio, "TCP_SCK_ON_READ(%d) >>> REQUESTING to write data of %d bytes\n", (int)tcp->hnd, MIO_SIZEOF(xxx));
		//return mio_dev_sck_write  (tcp, "HELLO", 5, MIO_NULL);
		MIO_INIT_NTIME (&tmout, 5, 0);
		n = mio_dev_sck_timedwrite(tcp, xxx, MIO_SIZEOF(xxx), &tmout, MIO_NULL, MIO_NULL);

		if (n <= -1) return -1;
	}

	MIO_INFO1 (tcp->mio, "TCP_SCK_ON_READ(%d) - REQUESTING TO STOP READ\n", (int)tcp->hnd);
	mio_dev_sck_read (tcp, 0);

#if 0
	MIO_INFO1 (tcp->mio, "TCP_SCK_ON_READ(%d) - REQUESTING TO CLOSE WRITING END\n", (int)tcp->hnd);
	/* post the write finisher - close the writing end */
	n = mio_dev_sck_write(tcp, MIO_NULL, 0, MIO_NULL, MIO_NULL);
	if (n <= -1) return -1;
#endif

	return 0;

/* return 1; let the main loop to read more greedily without consulting the multiplexer */
}


int main (int argc, char* argv[])
{

	mio_t* mio = MIO_NULL;
	mio_dev_sck_t* tcpsvr;
	mio_dev_sck_make_t tcp_make;
	mio_dev_sck_connect_t tcp_conn;
	tcp_xtn_t* ts;

	if (argc != 2)
	{
		fprintf (stderr, "Usage: %s ipaddr:port\n", argv[0]);
		return -1;
	}
	mio = mio_open(&mmgr, 0, MIO_NULL, MIO_FEATURE_ALL, 512, MIO_NULL);
	if (!mio)
	{
		printf ("Cannot open mio\n");
		goto oops;
	}

	memset (&tcp_conn, 0, MIO_SIZEOF(tcp_conn));
	mio_bcstrtoskad(mio, argv[1], &tcp_conn.remoteaddr);
	MIO_INIT_NTIME (&tcp_conn.connect_tmout, 5, 0);
	tcp_conn.options = 0;

	memset (&tcp_make, 0, MIO_SIZEOF(tcp_make));
	tcp_make.type = mio_skad_family(&tcp_conn.remoteaddr) == MIO_AF_INET? MIO_DEV_SCK_TCP4: MIO_DEV_SCK_TCP6;
	tcp_make.on_write = tcp_sck_on_write;
	tcp_make.on_read = tcp_sck_on_read;
	tcp_make.on_connect = tcp_sck_on_connect;
	tcp_make.on_disconnect = tcp_sck_on_disconnect;
	tcpsvr = mio_dev_sck_make(mio, MIO_SIZEOF(tcp_xtn_t), &tcp_make);
	if (!tcpsvr)
	{
		printf ("Cannot make a tcp server\n");
		goto oops;
	}

	ts = (tcp_xtn_t*)(tcpsvr + 1);
	ts->tally = 0;


	if (mio_dev_sck_connect(tcpsvr, &tcp_conn) <= -1)
	{
	}

	mio_loop (mio);

oops:
	if (mio) mio_close (mio);
	return 0;

	return 0;
}

