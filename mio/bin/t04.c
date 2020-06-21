
#include <mio.h>
#include <mio-mar.h>
#include <stdio.h>
#include <string.h>

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
			break;
		}

		case MIO_SVC_MARC_RCODE_DONE:
printf ("[%lu] NO DATA..\n", sid);
			break;

		case MIO_SVC_MARC_RCODE_ERROR:
			printf ("QUERY ERROR - %s\n", data); /* TODO: how to get both error code and error message? */
			break;
	}
}

int main (int argc, char* argv[])
{

	mio_t* mio = MIO_NULL;
	mio_svc_marc_t* marc;
	mio_svc_marc_connect_t ci;

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

	marc = mio_svc_marc_start(mio, &ci);
	if (!marc)
	{
		printf ("Cannot start a mariadb client service\n");
		goto oops;
	}

	mio_svc_mar_querywithbchars (marc, 0, MIO_SVC_MARC_QTYPE_SELECT, "SHOW STATUS", 11, on_result, MIO_NULL);
	mio_svc_mar_querywithbchars (marc, 0, MIO_SVC_MARC_QTYPE_ACTION, "DELETE FROM", 11, on_result, MIO_NULL);
//	mio_svc_mar_querywithbchars (marc, 0, MIO_SVC_MARC_QTYPE_SELECT, "SHOW STATUS", 11, on_result, MIO_NULL);
	mio_svc_mar_querywithbchars (marc, 0, MIO_SVC_MARC_QTYPE_ACTION, "DELETE FROM XXX", 14, on_result, MIO_NULL);

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

	mio_loop (mio);

oops:
	if (mio) mio_close (mio);
	return 0;
}
