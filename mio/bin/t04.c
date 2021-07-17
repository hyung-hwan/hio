#if defined(MIO_ENABLE_MARIADB)

#include <mio.h>
#include <mio-mar.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>

#include <mariadb/mysql.h>

#if 0
static void mar_on_disconnect (mio_dev_mar_t* dev)
{
}

static void mar_on_connect (mio_dev_mar_t* dev)
{
printf ("CONNECTED...\n");
	if (mio_dev_mar_querywithbchars(dev, "SHOW STATUS", 11) <= -1)
	{
		mio_dev_mar_halt (dev);
	}
}

static void mar_on_query_started (mio_dev_mar_t* dev, int mar_ret, const mio_bch_t* mar_errmsg)
{
	if (mar_ret != 0)
	{
printf ("QUERY NOT SENT PROPERLY..%s\n", mysql_error(dev->hnd));
	}
	else
	{
printf ("QUERY SENT..\n");
		if (mio_dev_mar_fetchrows(dev) <= -1)
		{
printf ("FETCH ROW FAILURE - %s\n", errmsg);
			mio_dev_mar_halt (dev);
		}
	}
}

static void mar_on_row_fetched (mio_dev_mar_t* dev, void* data)
{
	MYSQL_ROW row = (MYSQL_ROW)data;
	static int x = 0;
	if (!row) 
	{
		printf ("NO MORE ROW..\n");
		if (x == 0 && mio_dev_mar_querywithbchars(dev, "SELECT * FROM pdns.records", 26) <= -1) mio_dev_mar_halt (dev);
		x++;
	}
	else
	{
		if (x == 0)
			printf ("%s %s\n", row[0], row[1]);
		else if (x == 1)
			printf ("%s %s %s %s %s\n", row[0], row[1], row[2], row[3], row[4]);
		//printf ("GOT ROW\n");
	}
}

int main (int argc, char* argv[])
{

	mio_t* mio = MIO_NULL;
	mio_dev_mar_t* mar;
	mio_dev_mar_make_t mi;
	mio_dev_mar_connect_t ci;

	if (argc != 6)
	{
		fprintf (stderr, "Usage: %s ipaddr port username password dbname\n", argv[0]);
		return -1;
	}

	mio = mio_open(MIO_NULL, 0, MIO_NULL, MIO_FEATURE_ALL, 512, MIO_NULL);
	if (!mio)
	{
		printf ("Cannot open mio\n");
		goto oops;
	}

	memset (&ci, 0, MIO_SIZEOF(ci));
	ci.host = argv[1];
	ci.port = 3306; /* TODO: argv[2]; */
	ci.username = argv[3];
	ci.password = argv[4];
	ci.dbname = argv[5];

	memset (&mi, 0, MIO_SIZEOF(mi));
	/*mi.on_write = mar_on_write;
	mi.on_read = mar_on_read;*/
	mi.on_connect = mar_on_connect;
	mi.on_disconnect = mar_on_disconnect;
	mi.on_query_started = mar_on_query_started;
	mi.on_row_fetched = mar_on_row_fetched;

	mar = mio_dev_mar_make(mio, 0, &mi);
	if (!mar)
	{
		printf ("Cannot make a mar db client device\n");
		goto oops;
	}

	if (mio_dev_mar_connect(mar, &ci) <= -1)
	{
		printf ("Cannot connect to mar db server\n");
		goto oops;
	}

	mio_loop (mio);

oops:
	if (mio) mio_close (mio);
	return 0;
}
#endif

static void on_result (mio_svc_marc_t* svc, mio_oow_t sid, mio_svc_marc_rcode_t rcode, void* data, void* qctx)
{
static int x = 0;
	switch (rcode)
	{
		case MIO_SVC_MARC_RCODE_ROW:
		{
			MYSQL_ROW row = (MYSQL_ROW)data;
//		if (x == 0)
			printf ("[%lu] %s %s\n", sid, row[0], row[1]);
//		else if (x == 1)
//			printf ("%s %s %s %s %s\n", row[0], row[1], row[2], row[3], row[4]);
		//printf ("GOT ROW\n");
#if 0
x++;
if (x == 1)
{
printf ("BLOCKING PACKET...........................\n");
system ("/sbin/iptables -I OUTPUT -p tcp --dport 3306 -j REJECT");
system ("/sbin/iptables -I INPUT -p tcp --sport 3306 -j REJECT");
}
#endif
			break;
		}

		case MIO_SVC_MARC_RCODE_DONE:
printf ("[%lu] NO DATA..\n", sid);
			break;

		case MIO_SVC_MARC_RCODE_ERROR:
		{
			mio_svc_marc_dev_error_t* err = (mio_svc_marc_dev_error_t*)data;
			printf ("QUERY ERROR - [%d] %s\n", err->mar_errcode, err->mar_errmsg); 
			break;
		}
	}
}

static mio_t* g_mio = MIO_NULL;

static void handle_signal (int sig)
{
	mio_stop (g_mio, MIO_STOPREQ_TERMINATION);
}

static void send_test_query (mio_t* mio, const mio_ntime_t* now, mio_tmrjob_t* job)
{
	mio_svc_marc_t* marc = (mio_svc_marc_t*)job->ctx;
	mio_bch_t buf[256];
	mio_bch_t tmp[256];
	int len;

	if (mio_svc_marc_querywithbchars(marc, 0, MIO_SVC_MARC_QTYPE_SELECT, "SHOW STATUS", 11, on_result, MIO_NULL) <= -1)
	{
		MIO_INFO1 (mio, "FAILED TO SEND QUERY - %js\n", mio_geterrmsg(mio));
	}

	mio_svc_marc_escapebchars (marc, "wild", 4, tmp);
	len = snprintf(buf, MIO_COUNTOF(buf), "SELECT name, content FROM records WHERE name like '%%%s%%'", tmp);
	if (mio_svc_marc_querywithbchars(marc, 1, MIO_SVC_MARC_QTYPE_SELECT, buf, len, on_result, MIO_NULL) <= -1)
	{
		MIO_INFO1 (mio, "FAILED TO SEND QUERY - %js\n", mio_geterrmsg(mio));
	}
}

static int schedule_timer_job_after (mio_t* mio, const mio_ntime_t* fire_after, mio_tmrjob_handler_t handler, void* ctx)
{
	mio_tmrjob_t tmrjob;

	memset (&tmrjob, 0, MIO_SIZEOF(tmrjob));
	tmrjob.ctx = ctx;

	mio_gettime (mio, &tmrjob.when);
	MIO_ADD_NTIME (&tmrjob.when, &tmrjob.when, fire_after);

	tmrjob.handler = handler;
	tmrjob.idxptr = MIO_NULL;

	return mio_instmrjob(mio, &tmrjob);
}


int main (int argc, char* argv[])
{

	mio_t* mio = MIO_NULL;
	mio_svc_marc_t* marc;
	mio_svc_marc_connect_t ci;
/*	mio_svc_marc_tmout_t tmout;*/

	if (argc != 6)
	{
		fprintf (stderr, "Usage: %s ipaddr port username password dbname\n", argv[0]);
		return -1;
	}

	mio = mio_open(MIO_NULL, 0, MIO_NULL, 512, MIO_NULL);
	if (!mio)
	{
		printf ("Cannot open mio\n");
		goto oops;
	}

	memset (&ci, 0, MIO_SIZEOF(ci));
	ci.host = argv[1];
	ci.port = 3306; /* TODO: argv[2]; */
	ci.username = argv[3];
	ci.password = argv[4];
	ci.dbname = argv[5];

/* timeout not implemented  yet in the mardiab device and services 
	MIO_INIT_NTIME (&tmout.c, 2,  0);
	MIO_INIT_NTIME (&tmout.r, -1,  0);
	MIO_INIT_NTIME (&tmout.w, -1,  0);
*/

	marc = mio_svc_marc_start(mio, &ci, MIO_NULL);
	if (!marc)
	{
		printf ("Cannot start a mariadb client service\n");
		goto oops;
	}

	mio_svc_marc_querywithbchars (marc, 0, MIO_SVC_MARC_QTYPE_SELECT, "SHOW STATUS", 11, on_result, MIO_NULL);
	mio_svc_marc_querywithbchars (marc, 0, MIO_SVC_MARC_QTYPE_ACTION, "DELETE FROM", 11, on_result, MIO_NULL);
//	mio_svc_marc_querywithbchars (marc, 0, MIO_SVC_MARC_QTYPE_SELECT, "SHOW STATUS", 11, on_result, MIO_NULL);
	mio_svc_marc_querywithbchars (marc, 0, MIO_SVC_MARC_QTYPE_ACTION, "DELETE FROM XXX", 14, on_result, MIO_NULL);

#if 0
	memset (&mi, 0, MIO_SIZEOF(mi));
	/*mi.on_write = mar_on_write;
	mi.on_read = mar_on_read;*/
	mi.on_connect = mar_on_connect;
	mi.on_disconnect = mar_on_disconnect;
	mi.on_query_started = mar_on_query_started;
	mi.on_row_fetched = mar_on_row_fetched;

	mar = mio_dev_mar_make(mio, 0, &mi);
	if (!mar)
	{
		printf ("Cannot make a mar db client device\n");
		goto oops;
	}

	if (mio_dev_mar_connect(mar, &ci) <= -1)
	{
		printf ("Cannot connect to mar db server\n");
		goto oops;
	}
#endif

	g_mio = mio;
	signal (SIGINT, handle_signal);

	/* ---------------------------------------- */
	{
		mio_ntime_t x;
		MIO_INIT_NTIME (&x, 32, 0);
		schedule_timer_job_after (mio, &x, send_test_query, marc);
		mio_loop (mio);
	}
	/* ---------------------------------------- */

	signal (SIGINT, SIG_IGN);
	g_mio = MIO_NULL;

oops:
printf ("about to close mio...\n");
	if (mio) mio_close (mio);
	return 0;
}




#else

#include <stdio.h>
int main (int argc, char* argv[])
{
	printf ("mariadb not enabled\n");
	return 0;
}
#endif
