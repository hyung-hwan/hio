

#include <hio.h>
#include <hio-utl.h>
#include <hio-sck.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct mmgr_stat_t
{
	hio_oow_t total_count;
};

typedef struct mmgr_stat_t mmgr_stat_t;

static mmgr_stat_t mmgr_stat;

static void* mmgr_alloc (hio_mmgr_t* mmgr, hio_oow_t size)
{
	void* x;

	if (((mmgr_stat_t*)mmgr->ctx)->total_count > 3000)
	{
printf ("CRITICAL ERROR ---> too many heap chunks...\n");
		return HIO_NULL;
	}

	x = malloc (size);
	if (x) ((mmgr_stat_t*)mmgr->ctx)->total_count++;
	return x;
}

static void* mmgr_realloc (hio_mmgr_t* mmgr, void* ptr, hio_oow_t size)
{
	return realloc (ptr, size);
}

static void mmgr_free (hio_mmgr_t* mmgr, void* ptr)
{
	((mmgr_stat_t*)mmgr->ctx)->total_count--;
	return free (ptr);
}


static hio_mmgr_t mmgr = 
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

	hio_skadtobcstr (tcp->hio, &tcp->localaddr, buf1, HIO_COUNTOF(buf1), HIO_SKAD_TO_BCSTR_ADDR | HIO_SKAD_TO_BCSTR_PORT);
	hio_skadtobcstr (tcp->hio, &tcp->remoteaddr, buf2, HIO_COUNTOF(buf2), HIO_SKAD_TO_BCSTR_ADDR | HIO_SKAD_TO_BCSTR_PORT);

	if (tcp->state & HIO_DEV_SCK_CONNECTED)
	{
		HIO_INFO3 (tcp->hio, "DEVICE connected to a remote server... LOCAL %hs REMOTE %hs SCK: %d\n", buf1, buf2, tcp->hnd);
	}
	else if (tcp->state & HIO_DEV_SCK_ACCEPTED)
	{
		HIO_INFO3 (tcp->hio, "DEVICE accepted client device... .LOCAL %hs REMOTE %hs SCK: %d\n", buf1, buf2, tcp->hnd);
	}

	if (hio_dev_sck_write(tcp, "hello", 5, HIO_NULL, HIO_NULL) <= -1)
	{
		hio_dev_sck_halt (tcp);
	}
}

static int tcp_sck_on_write (hio_dev_sck_t* tcp, hio_iolen_t wrlen, void* wrctx, const hio_skad_t* dstaddr)
{
	tcp_xtn_t* ts;
	hio_ntime_t tmout;

	if (wrlen <= -1)
	{
		HIO_INFO1 (tcp->hio, "TCP_SCK_ON_WRITE(%d) >>> SEDING TIMED OUT...........\n", (int)tcp->hnd);
		hio_dev_sck_halt (tcp);
	}
	else
	{
		ts = (tcp_xtn_t*)(tcp + 1);
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


int main (int argc, char* argv[])
{

	hio_t* hio = HIO_NULL;
	hio_dev_sck_t* tcpsvr;
	hio_dev_sck_make_t tcp_make;
	hio_dev_sck_connect_t tcp_conn;
	tcp_xtn_t* ts;

	if (argc != 2)
	{
		fprintf (stderr, "Usage: %s ipaddr:port\n", argv[0]);
		return -1;
	}
	hio = hio_open(&mmgr, 0, HIO_NULL, HIO_FEATURE_ALL, 512, HIO_NULL);
	if (!hio)
	{
		printf ("Cannot open hio\n");
		goto oops;
	}

	memset (&tcp_conn, 0, HIO_SIZEOF(tcp_conn));
	hio_bcstrtoskad(hio, argv[1], &tcp_conn.remoteaddr);
	HIO_INIT_NTIME (&tcp_conn.connect_tmout, 5, 0);
	tcp_conn.options = 0;

	memset (&tcp_make, 0, HIO_SIZEOF(tcp_make));
	tcp_make.type = hio_skad_get_family(&tcp_conn.remoteaddr) == HIO_AF_INET? HIO_DEV_SCK_TCP4: HIO_DEV_SCK_TCP6;
	tcp_make.on_write = tcp_sck_on_write;
	tcp_make.on_read = tcp_sck_on_read;
	tcp_make.on_connect = tcp_sck_on_connect;
	tcp_make.on_disconnect = tcp_sck_on_disconnect;
	tcpsvr = hio_dev_sck_make(hio, HIO_SIZEOF(tcp_xtn_t), &tcp_make);
	if (!tcpsvr)
	{
		printf ("Cannot make a tcp server\n");
		goto oops;
	}

	ts = (tcp_xtn_t*)(tcpsvr + 1);
	ts->tally = 0;


	if (hio_dev_sck_connect(tcpsvr, &tcp_conn) <= -1)
	{
	}

	hio_loop (hio);

oops:
	if (hio) hio_close (hio);
	return 0;

	return 0;
}

