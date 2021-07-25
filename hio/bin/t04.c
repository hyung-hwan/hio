#include <hio.h>

#if defined(HIO_ENABLE_MARIADB)

#include <hio-mar.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>

#if 0
#include <mariadb/mysql.h>
#else
#include <mysql.h>
#endif

#if 0
static void mar_on_disconnect (hio_dev_mar_t* dev)
{
}

static void mar_on_connect (hio_dev_mar_t* dev)
{
printf ("CONNECTED...\n");
	if (hio_dev_mar_querywithbchars(dev, "SHOW STATUS", 11) <= -1)
	{
		hio_dev_mar_halt (dev);
	}
}

static void mar_on_query_started (hio_dev_mar_t* dev, int mar_ret, const hio_bch_t* mar_errmsg)
{
	if (mar_ret != 0)
	{
printf ("QUERY NOT SENT PROPERLY..%s\n", mysql_error(dev->hnd));
	}
	else
	{
printf ("QUERY SENT..\n");
		if (hio_dev_mar_fetchrows(dev) <= -1)
		{
printf ("FETCH ROW FAILURE - %s\n", errmsg);
			hio_dev_mar_halt (dev);
		}
	}
}

static void mar_on_row_fetched (hio_dev_mar_t* dev, void* data)
{
	MYSQL_ROW row = (MYSQL_ROW)data;
	static int x = 0;
	if (!row) 
	{
		printf ("NO MORE ROW..\n");
		if (x == 0 && hio_dev_mar_querywithbchars(dev, "SELECT * FROM pdns.records", 26) <= -1) hio_dev_mar_halt (dev);
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

	hio_t* hio = HIO_NULL;
	hio_dev_mar_t* mar;
	hio_dev_mar_make_t mi;
	hio_dev_mar_connect_t ci;

	if (argc != 6)
	{
		fprintf (stderr, "Usage: %s ipaddr port username password dbname\n", argv[0]);
		return -1;
	}

	hio = hio_open(HIO_NULL, 0, HIO_NULL, HIO_FEATURE_ALL, 512, HIO_NULL);
	if (!hio)
	{
		printf ("Cannot open hio\n");
		goto oops;
	}

	memset (&ci, 0, HIO_SIZEOF(ci));
	ci.host = argv[1];
	ci.port = 3306; /* TODO: argv[2]; */
	ci.username = argv[3];
	ci.password = argv[4];
	ci.dbname = argv[5];

	memset (&mi, 0, HIO_SIZEOF(mi));
	/*mi.on_write = mar_on_write;
	mi.on_read = mar_on_read;*/
	mi.on_connect = mar_on_connect;
	mi.on_disconnect = mar_on_disconnect;
	mi.on_query_started = mar_on_query_started;
	mi.on_row_fetched = mar_on_row_fetched;

	mar = hio_dev_mar_make(hio, 0, &mi);
	if (!mar)
	{
		printf ("Cannot make a mar db client device\n");
		goto oops;
	}

	if (hio_dev_mar_connect(mar, &ci) <= -1)
	{
		printf ("Cannot connect to mar db server\n");
		goto oops;
	}

	hio_loop (hio);

oops:
	if (hio) hio_close (hio);
	return 0;
}
#endif

static void on_result (hio_svc_marc_t* svc, hio_oow_t sid, hio_svc_marc_rcode_t rcode, void* data, void* qctx)
{
static int x = 0;
	switch (rcode)
	{
		case HIO_SVC_MARC_RCODE_ROW:
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

		case HIO_SVC_MARC_RCODE_DONE:
printf ("[%lu] NO DATA..\n", sid);
			break;

		case HIO_SVC_MARC_RCODE_ERROR:
		{
			hio_svc_marc_dev_error_t* err = (hio_svc_marc_dev_error_t*)data;
			printf ("QUERY ERROR - [%d] %s\n", err->mar_errcode, err->mar_errmsg); 
			break;
		}
	}
}

static hio_t* g_hio = HIO_NULL;

static void handle_signal (int sig)
{
	hio_stop (g_hio, HIO_STOPREQ_TERMINATION);
}

static void send_test_query (hio_t* hio, const hio_ntime_t* now, hio_tmrjob_t* job)
{
	hio_svc_marc_t* marc = (hio_svc_marc_t*)job->ctx;
	hio_bch_t buf[256];
	hio_bch_t tmp[256];
	int len;

	if (hio_svc_marc_querywithbchars(marc, 0, HIO_SVC_MARC_QTYPE_SELECT, "SHOW STATUS", 11, on_result, HIO_NULL) <= -1)
	{
		HIO_INFO1 (hio, "FAILED TO SEND QUERY - %js\n", hio_geterrmsg(hio));
	}

	hio_svc_marc_escapebchars (marc, "wild", 4, tmp);
	len = snprintf(buf, HIO_COUNTOF(buf), "SELECT name, content FROM records WHERE name like '%%%s%%'", tmp);
	if (hio_svc_marc_querywithbchars(marc, 1, HIO_SVC_MARC_QTYPE_SELECT, buf, len, on_result, HIO_NULL) <= -1)
	{
		HIO_INFO1 (hio, "FAILED TO SEND QUERY - %js\n", hio_geterrmsg(hio));
	}
}

static int schedule_timer_job_after (hio_t* hio, const hio_ntime_t* fire_after, hio_tmrjob_handler_t handler, void* ctx)
{
	hio_tmrjob_t tmrjob;

	memset (&tmrjob, 0, HIO_SIZEOF(tmrjob));
	tmrjob.ctx = ctx;

	hio_gettime (hio, &tmrjob.when);
	HIO_ADD_NTIME (&tmrjob.when, &tmrjob.when, fire_after);

	tmrjob.handler = handler;
	tmrjob.idxptr = HIO_NULL;

	return hio_instmrjob(hio, &tmrjob);
}


int main (int argc, char* argv[])
{

	hio_t* hio = HIO_NULL;
	hio_svc_marc_t* marc;
	hio_svc_marc_connect_t ci;
/*	hio_svc_marc_tmout_t tmout;*/

	if (argc != 6)
	{
		fprintf (stderr, "Usage: %s ipaddr port username password dbname\n", argv[0]);
		return -1;
	}

	hio = hio_open(HIO_NULL, 0, HIO_NULL, HIO_FEATURE_ALL, 512, HIO_NULL);
	if (!hio)
	{
		printf ("Cannot open hio\n");
		goto oops;
	}

	memset (&ci, 0, HIO_SIZEOF(ci));
	ci.host = argv[1];
	ci.port = 3306; /* TODO: argv[2]; */
	ci.username = argv[3];
	ci.password = argv[4];
	ci.dbname = argv[5];

/* timeout not implemented  yet in the mardiab device and services 
	HIO_INIT_NTIME (&tmout.c, 2,  0);
	HIO_INIT_NTIME (&tmout.r, -1,  0);
	HIO_INIT_NTIME (&tmout.w, -1,  0);
*/

	marc = hio_svc_marc_start(hio, &ci, HIO_NULL);
	if (!marc)
	{
		printf ("Cannot start a mariadb client service\n");
		goto oops;
	}

	hio_svc_marc_querywithbchars (marc, 0, HIO_SVC_MARC_QTYPE_SELECT, "SHOW STATUS", 11, on_result, HIO_NULL);
	hio_svc_marc_querywithbchars (marc, 0, HIO_SVC_MARC_QTYPE_ACTION, "DELETE FROM", 11, on_result, HIO_NULL);
//	hio_svc_marc_querywithbchars (marc, 0, HIO_SVC_MARC_QTYPE_SELECT, "SHOW STATUS", 11, on_result, HIO_NULL);
	hio_svc_marc_querywithbchars (marc, 0, HIO_SVC_MARC_QTYPE_ACTION, "DELETE FROM XXX", 14, on_result, HIO_NULL);

#if 0
	memset (&mi, 0, HIO_SIZEOF(mi));
	/*mi.on_write = mar_on_write;
	mi.on_read = mar_on_read;*/
	mi.on_connect = mar_on_connect;
	mi.on_disconnect = mar_on_disconnect;
	mi.on_query_started = mar_on_query_started;
	mi.on_row_fetched = mar_on_row_fetched;

	mar = hio_dev_mar_make(hio, 0, &mi);
	if (!mar)
	{
		printf ("Cannot make a mar db client device\n");
		goto oops;
	}

	if (hio_dev_mar_connect(mar, &ci) <= -1)
	{
		printf ("Cannot connect to mar db server\n");
		goto oops;
	}
#endif

	g_hio = hio;
	signal (SIGINT, handle_signal);

	/* ---------------------------------------- */
	{
		hio_ntime_t x;
		HIO_INIT_NTIME (&x, 32, 0);
		schedule_timer_job_after (hio, &x, send_test_query, marc);
		hio_loop (hio);
	}
	/* ---------------------------------------- */

	signal (SIGINT, SIG_IGN);
	g_hio = HIO_NULL;

oops:
printf ("about to close hio...\n");
	if (hio) hio_close (hio);
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
