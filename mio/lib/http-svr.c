#include "mio-http.h"
#include "mio-htrd.h"
#include "mio-prv.h"

struct mio_svc_htts_t
{
	MIO_SVC_HEADER;

	mio_dev_sck_t* lsck;
};

struct mio_svc_httc_t
{
	MIO_SVC_HEADER;
};

struct sck_xtn_t
{
	mio_svc_htts_t* htts;
	mio_htrd_t* htrd; /* used by clients only */
};
typedef struct sck_xtn_t sck_xtn_t;

struct htrd_xtn_t
{
	mio_dev_sck_t* sck;
};
typedef struct htrd_xtn_t htrd_xtn_t;
/* ------------------------------------------------------------------------ */

static int client_on_read (mio_dev_sck_t* sck, const void* buf, mio_iolen_t len, const mio_skad_t* srcaddr)
{
	sck_xtn_t* sckxtn = mio_dev_sck_getxtn(sck);
printf ("** HTTS - client read   %p  %d -> htts:%p\n", sck, (int)len, sckxtn->htts);
	if (len <= 0)
	{
		mio_dev_sck_halt (sck);
	}
	else if (mio_htrd_feed(sckxtn->htrd, buf, len) <= -1)
	{
		mio_dev_sck_halt (sck);
	}

	return 0;
}

static int client_on_write (mio_dev_sck_t* sck, mio_iolen_t wrlen, void* wrctx, const mio_skad_t* dstaddr)
{
	return 0;
}

static int client_on_disconnect (mio_dev_sck_t* sck)
{
printf ("** HTTS - client disconnect  %p\n", sck);
}

/* ------------------------------------------------------------------------ */
static int listener_on_read (mio_dev_sck_t* sck, const void* buf, mio_iolen_t len, const mio_skad_t* srcaddr)
{
	return 0;
}

static int listener_on_write (mio_dev_sck_t* sck, mio_iolen_t wrlen, void* wrctx, const mio_skad_t* dstaddr)
{
	return 0;
}

static void listener_on_connect (mio_dev_sck_t* sck)
{
	sck_xtn_t* sckxtn = mio_dev_sck_getxtn(sck); /* the contents came from the listening socket */

	if (sck->state & MIO_DEV_SCK_ACCEPTED)
	{
		/* accepted a new client */
		htrd_xtn_t* htrdxtn;

		printf ("** HTTS(%p) - accepted... %p %d \n", sckxtn->htts, sck, sck->sck);

		/* the accepted socket inherits various event callbacks. switch some of them to avoid sharing */
		sck->on_read = client_on_read;
		sck->on_write = client_on_write;
		sck->on_disconnect = client_on_disconnect;

		sckxtn->htrd = mio_htrd_open(sck->mio, MIO_SIZEOF(*htrdxtn));
		if (MIO_UNLIKELY(!sckxtn->htrd)) 
		{
			MIO_INFO2 (sck->mio, "UNABLE TO OPEN HTTP READER FOR ACCEPTED SOCKET %p %d\n", sck, (int)sck->sck);
			mio_dev_sck_halt (sck);
		}
		else
		{
			htrdxtn = mio_htrd_getxtn(sckxtn->htrd);
			htrdxtn->sck = sck;
		}
	}
	else if (sck->state & MIO_DEV_SCK_CONNECTED)
	{
		/* this will never be triggered as the listing socket never call mio_dev_sck_connect() */
		printf ("** HTTS(%p) - connected... %p %d \n", sckxtn->htts, sck, sck->sck);
	}

	/* MIO_DEV_SCK_CONNECTED must not be seen here as this is only for the listener socket */
}

static void listener_on_disconnect (mio_dev_sck_t* sck)
{
	sck_xtn_t* sckxtn = mio_dev_sck_getxtn(sck);

	switch (MIO_DEV_SCK_GET_PROGRESS(sck))
	{
		case MIO_DEV_SCK_CONNECTING:
			MIO_INFO1 (sck->mio, "OUTGOING SESSION DISCONNECTED - FAILED TO CONNECT (%d) TO REMOTE SERVER\n", (int)sck->sck);
			break;

		case MIO_DEV_SCK_CONNECTING_SSL:
			MIO_INFO1 (sck->mio, "OUTGOING SESSION DISCONNECTED - FAILED TO SSL-CONNECT (%d) TO REMOTE SERVER\n", (int)sck->sck);
			break;

		case MIO_DEV_SCK_LISTENING:
			MIO_INFO1 (sck->mio, "SHUTTING DOWN THE SERVER SOCKET(%d)...\n", (int)sck->sck);
			break;

		case MIO_DEV_SCK_CONNECTED:
			MIO_INFO1 (sck->mio, "OUTGOING CLIENT CONNECTION GOT TORN DOWN(%d).......\n", (int)sck->sck);
			break;

		case MIO_DEV_SCK_ACCEPTING_SSL:
			MIO_INFO1 (sck->mio, "INCOMING SSL-ACCEPT GOT DISCONNECTED(%d) ....\n", (int)sck->sck);
			break;

		case MIO_DEV_SCK_ACCEPTED:
			MIO_INFO1 (sck->mio, "INCOMING CLIENT BEING SERVED GOT DISCONNECTED(%d).......\n", (int)sck->sck);
			break;

		default:
			MIO_INFO1 (sck->mio, "DISCONNECTED AFTER ALL(%d).......\n", (int)sck->sck);
			break;
	}

	if (sckxtn->htrd)
	{
		mio_htrd_close (sckxtn->htrd);
		sckxtn->htrd = 0;
	}
}


/* ------------------------------------------------------------------------ */

mio_svc_htts_t* mio_svc_htts_start (mio_t* mio, const mio_skad_t* bind_addr)
{
	mio_svc_htts_t* htts = MIO_NULL;
	union
	{
		mio_dev_sck_make_t m;
		mio_dev_sck_bind_t b;
		mio_dev_sck_listen_t l;
	} info;
	sck_xtn_t* sckxtn;

	htts = (mio_svc_htts_t*)mio_callocmem(mio, MIO_SIZEOF(*htts));
	if (MIO_UNLIKELY(!htts)) goto oops;

	htts->mio = mio;
	htts->svc_stop = mio_svc_htts_stop;

	MIO_MEMSET (&info, 0, MIO_SIZEOF(info));
	switch (mio_skad_family(bind_addr))
	{
		case MIO_AF_INET:
			info.m.type = MIO_DEV_SCK_TCP4;
			break;

		case MIO_AF_INET6:
			info.m.type = MIO_DEV_SCK_TCP6;
			break;

		default:
			mio_seterrnum (mio, MIO_EINVAL);
			goto oops;
	}

	MIO_MEMSET (&info, 0, MIO_SIZEOF(info));
	info.m.on_write = listener_on_write;
	info.m.on_read = listener_on_read;
	info.m.on_connect = listener_on_connect;
	info.m.on_disconnect = listener_on_disconnect;
	htts->lsck = mio_dev_sck_make(mio, MIO_SIZEOF(*sckxtn), &info.m);
	if (!htts->lsck) goto oops;

	sckxtn = (sck_xtn_t*)mio_dev_sck_getxtn(htts->lsck);
	sckxtn->htts = htts;

	MIO_MEMSET (&info, 0, MIO_SIZEOF(info));
	info.b.localaddr = *bind_addr;
	info.b.options = MIO_DEV_SCK_BIND_REUSEADDR | MIO_DEV_SCK_BIND_REUSEPORT;
	MIO_INIT_NTIME (&info.b.accept_tmout, 5, 1);
	if (mio_dev_sck_bind(htts->lsck, &info.b) <= -1) goto oops;

	info.l.backlogs = 255;
	if (mio_dev_sck_listen(htts->lsck, &info.l) <= -1) goto oops;

printf ("** HTTS LISTENER SOCKET %p\n", htts->lsck);
	MIO_SVCL_APPEND_SVC (&mio->actsvc, (mio_svc_t*)htts);
	return htts;

oops:
	if (htts)
	{
		if (htts->lsck) mio_dev_sck_kill (htts->lsck);
		mio_freemem (mio, htts);
	}
	return MIO_NULL;
}

void mio_svc_htts_stop (mio_svc_htts_t* htts)
{
	mio_t* mio = htts->mio;

	if (htts->lsck) mio_dev_sck_kill (htts->lsck);
	/*if (dnc->lsck) mio_dev_sck_kill (dnc->lsck);
	while (dnc->pending_req) release_dns_msg (dnc, dnc->pending_req);*/

	MIO_SVCL_UNLINK_SVC (htts);
	mio_freemem (mio, htts);
}


