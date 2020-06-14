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

#include <mio-mar.h>
#include "mio-prv.h"

#include <mariadb/mysql.h>


/* ========================================================================= */

static int dev_mar_make (mio_dev_t* dev, void* ctx)
{
	mio_t* mio = dev->mio;
	mio_dev_mar_t* rdev = (mio_dev_mar_t*)dev;
	mio_dev_mar_make_t* mi = (mio_dev_mar_make_t*)ctx;

	rdev->hnd = mysql_init(MIO_NULL);
	if (MIO_UNLIKELY(!rdev->hnd)) 
	{
		mio_seterrnum (mio, MIO_ESYSMEM);
		return -1;
	}

	if (mysql_options(rdev->hnd, MYSQL_OPT_NONBLOCK, 0) != 0)
	{
		mio_seterrbfmt (mio, MIO_ESYSERR, "%s", mysql_error(rdev->hnd));
		mysql_close (rdev->hnd);
		rdev->hnd = MIO_NULL;
		return -1;
	}

	{
		my_bool x = 0;
		mysql_options(rdev->hnd, MYSQL_OPT_RECONNECT, &x);
	}

	rdev->dev_cap = MIO_DEV_CAP_IN | MIO_DEV_CAP_OUT | MIO_DEV_CAP_VIRTUAL; /* mysql_init() doesn't create a socket. so no IO is possible at this point */
	rdev->on_read = mi->on_read;
	rdev->on_write = mi->on_write;
	rdev->on_connect = mi->on_connect;
	rdev->on_disconnect = mi->on_disconnect;
	rdev->on_query_started = mi->on_query_started;
	rdev->on_row_fetched = mi->on_row_fetched;
	return 0;
}

static int dev_mar_kill (mio_dev_t* dev, int force)
{
	/*mio_t* mio = dev->mio;*/
	mio_dev_mar_t* rdev = (mio_dev_mar_t*)dev;

	if (rdev->on_disconnect) rdev->on_disconnect (rdev);

	if (rdev->res)
	{
		mysql_free_result (rdev->res);
		rdev->res = MIO_NULL;
	}
	if (rdev->hnd)
	{
		mysql_close (rdev->hnd);
		rdev->hnd = MIO_NULL;
	}

	return 0;
}

static mio_syshnd_t dev_mar_getsyshnd (mio_dev_t* dev)
{
	mio_dev_mar_t* rdev = (mio_dev_mar_t*)dev;
	return (mio_syshnd_t)mysql_get_socket(rdev->hnd);
}

static int events_to_mysql_wstatus (int events)
{
	int wstatus = 0;
	if (events & MIO_DEV_EVENT_IN) wstatus |= MYSQL_WAIT_READ;
	if (events & MIO_DEV_EVENT_OUT) wstatus |= MYSQL_WAIT_WRITE;
	if (events & MIO_DEV_EVENT_PRI) wstatus |= MYSQL_WAIT_EXCEPT;
	return wstatus;
}

static int mysql_wstatus_to_events (int wstatus)
{
	int events = 0;
	if (wstatus & MYSQL_WAIT_READ) events |= MIO_DEV_EVENT_IN;
	if (wstatus & MYSQL_WAIT_WRITE) events |= MIO_DEV_EVENT_OUT;
	if (wstatus & MYSQL_WAIT_EXCEPT) events |= MIO_DEV_EVENT_PRI;
/* TODO: wstatus& MYSQL_WAIT_TIMEOUT? */
	return events;
}

static MIO_INLINE void watch_mysql (mio_dev_mar_t* rdev, int wstatus)
{
	if (mio_dev_watch((mio_dev_t*)rdev, MIO_DEV_WATCH_UPDATE, mysql_wstatus_to_events(wstatus)) <= -1)
	{
		/* watcher update failure. it's critical */
		mio_stop (rdev->mio, MIO_STOPREQ_WATCHER_ERROR);
	}
}


static void start_fetch_row (mio_dev_mar_t* rdev)
{
	MYSQL_ROW row;
	int status;

	status = mysql_fetch_row_start(&row, rdev->res);
	MIO_DEV_MAR_SET_PROGRESS (rdev, MIO_DEV_MAR_ROW_FETCHING);
	if (status)
	{
		/* row fetched */
		rdev->row_fetched = 0;
		watch_mysql (rdev, status);
	}
	else
	{
		/* row fetched - don't handle it immediately here */
		rdev->row_fetched = 1;
		rdev->row_wstatus = status;
		rdev->row = row;
		watch_mysql (rdev, MYSQL_WAIT_READ | MYSQL_WAIT_WRITE);
	}
}


static int dev_mar_ioctl (mio_dev_t* dev, int cmd, void* arg)
{
	mio_t* mio = dev->mio;
	mio_dev_mar_t* rdev = (mio_dev_mar_t*)dev;

	switch (cmd)
	{
		case MIO_DEV_MAR_CONNECT:
		{
			mio_dev_mar_connect_t* ci = (mio_dev_mar_connect_t*)arg;
			MYSQL* ret;
			int status;


			if (MIO_DEV_MAR_GET_PROGRESS(rdev))
			{
				/* can't connect again */
				mio_seterrbfmt (mio, MIO_EPERM, "operation in progress. disallowed to connect again");
				return -1;
			}

			status = mysql_real_connect_start(&ret, rdev->hnd, ci->host, ci->username, ci->password, ci->dbname, ci->port, MIO_NULL, 0);
			rdev->dev_cap &= ~MIO_DEV_CAP_VIRTUAL; /* a socket is created in mysql_real_connect_start() */
			if (status)
			{
				/* not connected */
				MIO_DEV_MAR_SET_PROGRESS (rdev, MIO_DEV_MAR_CONNECTING);
				rdev->connected = 0;
				watch_mysql (rdev, status);
			}
			else
			{
				/* connected immediately */
				if (MIO_UNLIKELY(!ret))
				{
					mio_seterrbfmt (mio, MIO_ESYSERR, "%s", mysql_error(rdev->hnd));
					return -1;
				}

				/* regiter it in the multiplexer so that the ready() handler is
				 * invoked to call the on_connect() callback */
				rdev->connected = 1;
				watch_mysql (rdev, MYSQL_WAIT_READ | MYSQL_WAIT_WRITE); /* TODO: verify this */
			}
			return 0;
		}

		case MIO_DEV_MAR_QUERY_WITH_BCS:
		{
			const mio_bcs_t* qstr = (const mio_bcs_t*)arg;
			int err, status;

			if (rdev->res) /* TODO: more accurate check */
			{
				mio_seterrbfmt (mio, MIO_EPERM, "operation in progress. disallowed to query again");
				return -1;
			}

			status = mysql_real_query_start(&err, rdev->hnd, qstr->ptr, qstr->len);
			MIO_DEV_MAR_SET_PROGRESS (rdev, MIO_DEV_MAR_QUERY_STARTING);
			if (status)
			{
				/* not done */
				rdev->query_started = 0;
				watch_mysql (rdev, status);
			}
			else
			{
				/* query sent immediately */
				rdev->query_started = 1;
				rdev->query_ret = err;
				watch_mysql (rdev, MYSQL_WAIT_READ | MYSQL_WAIT_WRITE);
			}
			return 0;
		}

		case MIO_DEV_MAR_FETCH_ROW:
		{
			if (!rdev->res)
			{
				rdev->res = mysql_use_result(rdev->hnd);
				if (MIO_UNLIKELY(!rdev->res))
				{
					mio_seterrbfmt (mio, MIO_ESYSERR, "%s", mysql_error(rdev->hnd));
					return -1;
				}
			}

			start_fetch_row (rdev);
			return 0;
		}

		default:
			mio_seterrnum (mio, MIO_EINVAL);
			return -1;
	}
}

static mio_dev_mth_t dev_mar_methods = 
{
	dev_mar_make,
	dev_mar_kill,
	dev_mar_getsyshnd,

	MIO_NULL,
	MIO_NULL,
	MIO_NULL,
	dev_mar_ioctl
};

/* ========================================================================= */


static int dev_evcb_mar_ready (mio_dev_t* dev, int events)
{
	mio_t* mio = dev->mio;
	mio_dev_mar_t* rdev = (mio_dev_mar_t*)dev;

#if 0
	if (events & MIO_DEV_EVENT_ERR)
	{
		int errcode;
		mio_scklen_t len;

		len = MIO_SIZEOF(errcode);
		if (getsockopt(mysql_get_socket(rdev->hnd), SOL_SOCKET, SO_ERROR, (char*)&errcode, &len) == -1)
		{
			/* the error number is set to the socket error code.
			 * errno resulting from getsockopt() doesn't reflect the actual
			 * socket error. so errno is not used to set the error number.
			 * instead, the generic device error MIO_EDEVERRR is used */
			mio_seterrbfmt (mio, MIO_EDEVERR, "device error - unable to get SO_ERROR");
		}
		else
		{
			mio_seterrwithsyserr (mio, 0, errcode);
		}

		return -1;
	}
#endif

	switch (MIO_DEV_MAR_GET_PROGRESS(rdev))
	{
		case MIO_DEV_MAR_CONNECTING:
		
			if (rdev->connected)
			{
				rdev->connected = 0;
				MIO_DEV_MAR_SET_PROGRESS (rdev, MIO_DEV_MAR_CONNECTED);
				if (rdev->on_connect) rdev->on_connect (rdev);
			}
			else
			{
				int status;
				MYSQL* tmp;

				status = mysql_real_connect_cont(&tmp, rdev->hnd, events_to_mysql_wstatus(events));
				watch_mysql (rdev, status);

				if (!status)
				{
					/* connected */
					MIO_DEV_MAR_SET_PROGRESS (rdev, MIO_DEV_MAR_CONNECTED);
					if (rdev->on_connect) rdev->on_connect (rdev);
				}
			}
			break;

		case MIO_DEV_MAR_QUERY_STARTING:
			if (rdev->query_started)
			{
				rdev->query_started = 0;
				MIO_DEV_MAR_SET_PROGRESS (rdev, MIO_DEV_MAR_QUERY_STARTED);
				if (rdev->on_query_started) rdev->on_query_started (rdev, rdev->query_ret);
			}
			else
			{
				int status;
				int tmp;

				status = mysql_real_query_cont(&tmp, rdev->hnd, events_to_mysql_wstatus(events));
				watch_mysql (rdev, status);

				if (!status)
				{
					/* query sent */
					MIO_DEV_MAR_SET_PROGRESS (rdev, MIO_DEV_MAR_QUERY_STARTED);
					if (rdev->on_query_started) rdev->on_query_started (rdev, tmp);
				}
			}

			break;

		case MIO_DEV_MAR_ROW_FETCHING:
		{
			int status;
			MYSQL_ROW row;

			if (rdev->row_fetched)
			{
				row = (MYSQL_ROW)rdev->row;
				rdev->row_fetched = 0;

				if (!row)
				{
					MIO_ASSERT (mio, rdev->res != MIO_NULL);
					mysql_free_result (rdev->res); /* this doesn't block after the last row */
					rdev->res = MIO_NULL;

					watch_mysql (rdev, rdev->row_wstatus);
				}

				MIO_DEV_MAR_SET_PROGRESS (rdev, MIO_DEV_MAR_ROW_FETCHED);
				if (rdev->on_row_fetched) rdev->on_row_fetched (rdev, row);

				if (row) start_fetch_row (rdev);
			}
			else
			{
				/* TODO: if rdev->res is MIO_NULL, error.. */
				status = mysql_fetch_row_cont(&row, rdev->res, events_to_mysql_wstatus(events));

				if (!status)
				{
					if (!row) 
					{
						/* the last row has been received - cleanup before invoking the callback */
						watch_mysql (rdev, status);

						MIO_ASSERT (mio, rdev->res != MIO_NULL);
						mysql_free_result (rdev->res); /* this doesn't block after the last row */
						rdev->res = MIO_NULL;
					}

					MIO_DEV_MAR_SET_PROGRESS (rdev, MIO_DEV_MAR_ROW_FETCHED);
					if (rdev->on_row_fetched) rdev->on_row_fetched (rdev, row);

					if (row) start_fetch_row (rdev); /* arrange to fetch the next row */
				}
				else
				{
					watch_mysql (rdev, status);
				}
			}

			break;
		}

		default:
			mio_seterrbfmt (mio, MIO_EINTERN, "invalid progress state in mar");
			return -1;
	}

	return 0; /* success. but skip core event handling */
}

static mio_dev_evcb_t dev_mar_event_callbacks =
{
	dev_evcb_mar_ready,
	MIO_NULL, /* no read callback */
	MIO_NULL  /* no write callback */
};


/* ========================================================================= */


mio_dev_mar_t* mio_dev_mar_make (mio_t* mio, mio_oow_t xtnsize, const mio_dev_mar_make_t* mi)
{
	return (mio_dev_mar_t*)mio_dev_make(
		mio, MIO_SIZEOF(mio_dev_mar_t) + xtnsize,
		&dev_mar_methods, &dev_mar_event_callbacks, (void*)mi);
}

int mio_dev_mar_connect (mio_dev_mar_t* dev, mio_dev_mar_connect_t* ci)
{
	return mio_dev_ioctl((mio_dev_t*)dev, MIO_DEV_MAR_CONNECT, ci);
}

int mio_dev_mar_querywithbchars (mio_dev_mar_t* dev, const mio_bch_t* qstr, mio_oow_t qlen)
{
	mio_bcs_t bcs = { (mio_bch_t*)qstr, qlen};
	return mio_dev_ioctl((mio_dev_t*)dev, MIO_DEV_MAR_QUERY_WITH_BCS, &bcs);
}

int mio_dev_mar_fetchrows (mio_dev_mar_t* dev)
{
	return mio_dev_ioctl((mio_dev_t*)dev, MIO_DEV_MAR_FETCH_ROW, MIO_NULL);
}

