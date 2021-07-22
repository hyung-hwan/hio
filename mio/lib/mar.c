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

#include <hio-mar.h>
#include "hio-prv.h"

#if 0
#include <mariadb/mysql.h>
#include <mariadb/errmsg.h>
#else
#include <mysql.h>
#include <errmsg.h>
#endif

#include <sys/socket.h>

/* ========================================================================= */

static int dev_mar_make (hio_dev_t* dev, void* ctx)
{
	hio_t* hio = dev->hio;
	hio_dev_mar_t* rdev = (hio_dev_mar_t*)dev;
	hio_dev_mar_make_t* mi = (hio_dev_mar_make_t*)ctx;

	rdev->hnd = mysql_init(HIO_NULL);
	if (HIO_UNLIKELY(!rdev->hnd)) 
	{
		hio_seterrnum (hio, HIO_ESYSMEM);
		return -1;
	}

	if (mysql_options(rdev->hnd, MYSQL_OPT_NONBLOCK, 0) != 0)
	{
		hio_seterrbfmt (hio, HIO_ESYSERR, "%hs", mysql_error(rdev->hnd));
		mysql_close (rdev->hnd);
		rdev->hnd = HIO_NULL;
		return -1;
	}

	{
		my_bool x = 0; /* no auto-reconnect */
		mysql_options(rdev->hnd, MYSQL_OPT_RECONNECT, &x);
	}

#if 0
/* TOOD: timeout not implemented...
 * timeout can't be implemented using the mysql timeout options in the nonblocking mode.
 * i must create a timeer jobs for these */
	if (mi->flags & HIO_DEV_MAR_USE_TMOUT)
	{
		unsigned int tmout;

		tmout = mi->tmout.c.sec; /* mysql supports the granularity of seconds only */
		if (tmout >= 0) mysql_options(rdev->hnd, MYSQL_OPT_CONNECT_TIMEOUT, &tmout);

		tmout = mi->tmout.r.sec;
		if (tmout >= 0) mysql_options(rdev->hnd, MYSQL_OPT_READ_TIMEOUT, &tmout);

		tmout = mi->tmout.w.sec;
		if (tmout >= 0) mysql_options(rdev->hnd, MYSQL_OPT_WRITE_TIMEOUT, &tmout);
	}
#endif

	rdev->dev_cap = HIO_DEV_CAP_IN | HIO_DEV_CAP_OUT | HIO_DEV_CAP_VIRTUAL; /* mysql_init() doesn't create a socket. so no IO is possible at this point */
	rdev->on_read = mi->on_read;
	rdev->on_write = mi->on_write;
	rdev->on_connect = mi->on_connect;
	rdev->on_disconnect = mi->on_disconnect;
	rdev->on_query_started = mi->on_query_started;
	rdev->on_row_fetched = mi->on_row_fetched;

	rdev->progress = HIO_DEV_MAR_INITIAL;

	return 0;
}

static int dev_mar_kill (hio_dev_t* dev, int force)
{
	/*hio_t* hio = dev->hio;*/
	hio_dev_mar_t* rdev = (hio_dev_mar_t*)dev;

	/* if rdev->connected is 0 at this point, 
	 * the underlying socket of this device is down */
	if (HIO_LIKELY(rdev->on_disconnect)) rdev->on_disconnect (rdev);

	/* hack */
	if (!rdev->broken) 
	{
		/* mysql_free_result() blocks if not all rows have been read.
		 * mysql_close() also blocks to transmit COM_QUIT,
		 * in this context, it is not appropriate to call 
		 * mysql_free_result_start()/mysql_free_result_cont() and
		 * mysql_close_start()/mysql_close_cont().
		 * let me just call shutdown on the underlying socket to work around this issue.
		 * as a result, mysql_close() will be unable to send COM_QUIT but will return fast
		 */
		shutdown (mysql_get_socket(rdev->hnd), SHUT_RDWR);
	}

	if (rdev->res)
	{
		mysql_free_result (rdev->res);
		rdev->res = HIO_NULL;
	}

	if (rdev->hnd)
	{
		mysql_close (rdev->hnd); 
		rdev->hnd = HIO_NULL;
	}

	rdev->connected = 0;
	rdev->broken = 0;

	return 0;
}

static hio_syshnd_t dev_mar_getsyshnd (hio_dev_t* dev)
{
	hio_dev_mar_t* rdev = (hio_dev_mar_t*)dev;
	if (rdev->broken) return rdev->broken_syshnd; /* hack!! */
	return (hio_syshnd_t)mysql_get_socket(rdev->hnd);
}

static int events_to_mysql_wstatus (int events)
{
	int wstatus = 0;
	if (events & HIO_DEV_EVENT_IN) wstatus |= MYSQL_WAIT_READ;
	if (events & HIO_DEV_EVENT_OUT) wstatus |= MYSQL_WAIT_WRITE;
	if (events & HIO_DEV_EVENT_PRI) wstatus |= MYSQL_WAIT_EXCEPT;
	return wstatus;
}

static int mysql_wstatus_to_events (int wstatus)
{
	int events = 0;
	if (wstatus & MYSQL_WAIT_READ) events |= HIO_DEV_EVENT_IN;
	if (wstatus & MYSQL_WAIT_WRITE) events |= HIO_DEV_EVENT_OUT;
	if (wstatus & MYSQL_WAIT_EXCEPT) events |= HIO_DEV_EVENT_PRI;
/* TODO: wstatus& MYSQL_WAIT_TIMEOUT? */
	return events;
}

static HIO_INLINE void watch_mysql (hio_dev_mar_t* rdev, int wstatus)
{
	if (hio_dev_watch((hio_dev_t*)rdev, HIO_DEV_WATCH_UPDATE, mysql_wstatus_to_events(wstatus)) <= -1)
	{
		/* watcher update failure. it's critical */
		hio_stop (rdev->hio, HIO_STOPREQ_WATCHER_ERROR);
	}
}

static void start_fetch_row (hio_dev_mar_t* rdev)
{
	MYSQL_ROW row;
	int status;

	status = mysql_fetch_row_start(&row, rdev->res);

	HIO_DEV_MAR_SET_PROGRESS (rdev, HIO_DEV_MAR_ROW_FETCHING);
	if (status)
	{
		/* row not fetched */
		rdev->row_fetched_deferred = 0;
		watch_mysql (rdev, status);
	}
	else
	{
		/* row fetched - don't handle it immediately here */
		rdev->row_fetched_deferred = 1;
		rdev->row_wstatus = status;
		rdev->row = row;
		watch_mysql (rdev, MYSQL_WAIT_READ | MYSQL_WAIT_WRITE);
	}
}

static int dev_mar_ioctl (hio_dev_t* dev, int cmd, void* arg)
{
	hio_t* hio = dev->hio;
	hio_dev_mar_t* rdev = (hio_dev_mar_t*)dev;

	switch (cmd)
	{
		case HIO_DEV_MAR_CONNECT:
		{
			hio_dev_mar_connect_t* ci = (hio_dev_mar_connect_t*)arg;
			MYSQL* tmp;
			int status;

			if (HIO_DEV_MAR_GET_PROGRESS(rdev) != HIO_DEV_MAR_INITIAL)
			{
				/* can't connect again */
				hio_seterrbfmt (hio, HIO_EPERM, "operation in progress. disallowed to connect again");
				return -1;
			}

			HIO_ASSERT (hio, rdev->connected_deferred == 0);

			status = mysql_real_connect_start(&tmp, rdev->hnd, ci->host, ci->username, ci->password, ci->dbname, ci->port, HIO_NULL, 0);
			rdev->dev_cap &= ~HIO_DEV_CAP_VIRTUAL; /* a socket is created in mysql_real_connect_start() */
			if (status)
			{
				/* not connected */
				HIO_DEV_MAR_SET_PROGRESS (rdev, HIO_DEV_MAR_CONNECTING);
				watch_mysql (rdev, status);
			}
			else
			{
				if (HIO_UNLIKELY(!tmp)) /* connection attempt failed immediately */
				{
					/* immediate failure doesn't invoke on_discoonect(). 
					 * the caller must check the return code of this function.  */
					hio_seterrbfmt (hio, HIO_ESYSERR, "%hs", mysql_error(rdev->hnd));
					return -1;
				}

				/* connected_deferred immediately. postpone actual handling to the ready() callback */
				HIO_DEV_MAR_SET_PROGRESS (rdev, HIO_DEV_MAR_CONNECTING);
				rdev->connected_deferred = 1; /* to let the ready() handler to trigger on_connect() */
				/* regiter it in the multiplexer so that the ready() handler is
				 * invoked to call the on_connect() callback */
				watch_mysql (rdev, MYSQL_WAIT_READ | MYSQL_WAIT_WRITE); /* TODO: verify this */
			}
			return 0;
		}

		case HIO_DEV_MAR_QUERY_WITH_BCS:
		{
			const hio_bcs_t* qstr = (const hio_bcs_t*)arg;
			int err, status;
			hio_syshnd_t syshnd;

			if (!rdev->connected)
			{
				hio_seterrbfmt (hio, HIO_EPERM, "not connected. disallowed to query");
				return -1;
			}

			if (rdev->res) /* TODO: more accurate check */
			{
				hio_seterrbfmt (hio, HIO_EPERM, "operation in progress. disallowed to query again");
				return -1;
			}


			syshnd = mysql_get_socket(rdev->hnd);
			status = mysql_real_query_start(&err, rdev->hnd, qstr->ptr, qstr->len);
			HIO_DEV_MAR_SET_PROGRESS (rdev, HIO_DEV_MAR_QUERY_STARTING);
			if (status)
			{
				/* not done */
				watch_mysql (rdev, status);
			}
			else
			{
				/* query sent immediately */
				if (err) 
				{
					/* but there is an error */
					if (err == 1) err = mysql_errno(rdev->hnd);

					hio_seterrbfmt (hio, HIO_ESYSERR, "%hs", mysql_error(rdev->hnd));
					if (err == CR_SERVER_LOST || err == CR_SERVER_GONE_ERROR)
					{
						/* the underlying socket must have gotten closed by mysql_real_query_start() */
						const hio_ooch_t* prev_errmsg;
						prev_errmsg = hio_backuperrmsg(hio);
						rdev->broken = 1;
						rdev->broken_syshnd = syshnd;
						watch_mysql (rdev, 0);
						hio_dev_mar_halt (rdev); /* i can't keep this device alive regardless of the caller's post-action */
						hio_seterrbfmt (hio, HIO_ESYSERR, "%js", prev_errmsg);
					}
					return -1;
				}

				/* sent without an error */
				rdev->query_started_deferred = 1;
				watch_mysql (rdev, MYSQL_WAIT_READ | MYSQL_WAIT_WRITE);
			}
			return 0;
		}

		case HIO_DEV_MAR_FETCH_ROW:
		{
			if (!rdev->res)
			{
				rdev->res = mysql_use_result(rdev->hnd);
				if (HIO_UNLIKELY(!rdev->res))
				{
					hio_seterrbfmt (hio, HIO_ESYSERR, "%hs", mysql_error(rdev->hnd));
					return -1;
				}
			}

			start_fetch_row (rdev);
			return 0;
		}

		default:
			hio_seterrnum (hio, HIO_EINVAL);
			return -1;
	}
}

static hio_dev_mth_t dev_mar_methods = 
{
	dev_mar_make,
	dev_mar_kill,
	HIO_NULL,
	dev_mar_getsyshnd,

	HIO_NULL,
	HIO_NULL,
	HIO_NULL,
	HIO_NULL, /* sendfile */
	dev_mar_ioctl
};

/* ========================================================================= */


static int dev_evcb_mar_ready (hio_dev_t* dev, int events)
{
	hio_t* hio = dev->hio;
	hio_dev_mar_t* rdev = (hio_dev_mar_t*)dev;

#if 0
	if (events & HIO_DEV_EVENT_ERR)
	{
		int errcode;
		hio_scklen_t len;

		len = HIO_SIZEOF(errcode);
		if (getsockopt(mysql_get_socket(rdev->hnd), SOL_SOCKET, SO_ERROR, (char*)&errcode, &len) == -1)
		{
			/* the error number is set to the socket error code.
			 * errno resulting from getsockopt() doesn't reflect the actual
			 * socket error. so errno is not used to set the error number.
			 * instead, the generic device error HIO_EDEVERRR is used */
			hio_seterrbfmt (hio, HIO_EDEVERR, "device error - unable to get SO_ERROR");
		}
		else
		{
			hio_seterrwithsyserr (hio, 0, errcode);
		}

		return -1;
	}
#endif

	switch (HIO_DEV_MAR_GET_PROGRESS(rdev))
	{
		case HIO_DEV_MAR_CONNECTING:
			if (rdev->connected_deferred)
			{
				/* connection esablished dev_mar_ioctl() but postponed to this function */
				rdev->connected_deferred = 0;
				rdev->connected = 1; /* really connected */
				HIO_DEV_MAR_SET_PROGRESS (rdev, HIO_DEV_MAR_CONNECTED);
				if (rdev->on_connect) rdev->on_connect (rdev);
			}
			else
			{
				int status;
				MYSQL* tmp;
				hio_syshnd_t syshnd;

				syshnd = mysql_get_socket(rdev->hnd); /* ugly hack for handling a socket closed b y mysql_real_connect_cont() */
				status = mysql_real_connect_cont(&tmp, rdev->hnd, events_to_mysql_wstatus(events));

				if (status)
				{
					/* connection in progress */
					watch_mysql (rdev, status);
				}
				else
				{
					/* connection completed. */
					if (tmp)
					{
						/* established ok */
						watch_mysql (rdev, status);

						rdev->connected = 1; /* really connected */
						HIO_DEV_MAR_SET_PROGRESS (rdev, HIO_DEV_MAR_CONNECTED);
						if (rdev->on_connect) rdev->on_connect (rdev);
					}
					else
					{
						/* connection attempt failed */

						rdev->broken = 1; /* trick dev_mar_getsyshnd() to return rdev->broken_syshnd. */
						rdev->broken_syshnd = syshnd; /* mysql_get_socket() over a failed mariadb handle ends up with segfault */

						/* this attempts to trigger the low-level multiplxer to delete 'syshnd' closed by mysql_real_connect_cont().
						 * the underlying low-level operation may fail. but i don't care. the best is not to open 
						 * new file descriptor between mysql_real_connect_cont() and watch_mysql(rdev, 0).
						 * 
						 * close(6); <- mysql_real_connect_cont();
						 * epoll_ctl(4, EPOLL_CTL_DEL, 6, 0x7ffc785e7154) = -1 EBADF (Bad file descriptor) <- by hio_dev_watch() in watch_mysql
						 */
						watch_mysql (rdev, 0);

						/* on_disconnect() will be called without on_connect(). 
						 * you may assume that the initial connectinon attempt failed. 
						 * reconnectin doesn't apply in this context. */
						hio_dev_mar_halt (rdev); 
					}
				}
			}
			break;

		case HIO_DEV_MAR_QUERY_STARTING:
			if (rdev->query_started_deferred)
			{
				rdev->query_started_deferred = 0;
				HIO_DEV_MAR_SET_PROGRESS (rdev, HIO_DEV_MAR_QUERY_STARTED);
				if (rdev->on_query_started) rdev->on_query_started (rdev, 0, HIO_NULL);
			}
			else
			{
				int status, err;
				hio_syshnd_t syshnd;

				syshnd = mysql_get_socket(rdev->hnd);
				status = mysql_real_query_cont(&err, rdev->hnd, events_to_mysql_wstatus(events));

				if (status)
				{
					watch_mysql (rdev, status);
				}
				else
				{
					if (err)
					{
						/* query send failure */
						if (err == 1) err = mysql_errno(rdev->hnd); /* err is set to 1 by mariadb-connector-c 3.1 as of this writing. let me work around it by fetching the error code */

						if (err == CR_SERVER_LOST || err == CR_SERVER_GONE_ERROR)
						{
							rdev->broken = 1;
							rdev->broken_syshnd = syshnd;
							watch_mysql (rdev, 0);
							hio_dev_mar_halt (rdev); /* i can't keep this device alive regardless of the caller's post-action */
							/* don't invoke on_query_started(). in this case, on_disconnect() will be called later */
						}
						else
						{
							/* query not sent for other reasons. probably nothing to watch? */
							watch_mysql (rdev, 0); /* TODO: use status instead of 0? is status reliable in this context? */
							if (rdev->on_query_started) rdev->on_query_started (rdev, err, mysql_error(rdev->hnd));
						}
					}
					else
					{
						/* query really sent */
						HIO_DEV_MAR_SET_PROGRESS (rdev, HIO_DEV_MAR_QUERY_STARTED);
						if (rdev->on_query_started) rdev->on_query_started (rdev, 0, HIO_NULL);
					}
				}
			}

			break;

		case HIO_DEV_MAR_ROW_FETCHING:
		{
			int status;
			MYSQL_ROW row;

			if (rdev->row_fetched_deferred)
			{
				row = (MYSQL_ROW)rdev->row;
				rdev->row_fetched_deferred = 0;

				if (!row)
				{
					HIO_ASSERT (hio, rdev->res != HIO_NULL);
					mysql_free_result (rdev->res); /* this doesn't block after the last row */
					rdev->res = HIO_NULL;

					watch_mysql (rdev, rdev->row_wstatus);
				}

				HIO_DEV_MAR_SET_PROGRESS (rdev, HIO_DEV_MAR_ROW_FETCHED);
				if (HIO_LIKELY(rdev->on_row_fetched)) rdev->on_row_fetched (rdev, row);

				if (row) start_fetch_row (rdev);
			}
			else
			{
				/* TODO: if rdev->res is HIO_NULL, error.. */
				status = mysql_fetch_row_cont(&row, rdev->res, events_to_mysql_wstatus(events));

				if (!status)
				{
					/* row is available */
					if (!row) 
					{
						/* the last row has been received - cleanup before invoking the callback */
						watch_mysql (rdev, status);

						HIO_ASSERT (hio, rdev->res != HIO_NULL);
						mysql_free_result (rdev->res); /* this doesn't block after the last row */
						rdev->res = HIO_NULL;
					}

					HIO_DEV_MAR_SET_PROGRESS (rdev, HIO_DEV_MAR_ROW_FETCHED);
					if (HIO_LIKELY(rdev->on_row_fetched)) rdev->on_row_fetched (rdev, row);

					if (row) start_fetch_row (rdev); /* arrange to fetch the next row */
				}
				else
				{
					/* no row is available */
					watch_mysql (rdev, status);
				}
			}

			break;
		}

		default:
			hio_seterrbfmt (hio, HIO_EINTERN, "invalid progress value in mar");
			return -1;
	}

	return 0; /* success. but skip core event handling */
}

static hio_dev_evcb_t dev_mar_event_callbacks =
{
	dev_evcb_mar_ready,
	HIO_NULL, /* no read callback */
	HIO_NULL  /* no write callback */
};


/* ========================================================================= */


hio_dev_mar_t* hio_dev_mar_make (hio_t* hio, hio_oow_t xtnsize, const hio_dev_mar_make_t* mi)
{
	return (hio_dev_mar_t*)hio_dev_make(
		hio, HIO_SIZEOF(hio_dev_mar_t) + xtnsize,
		&dev_mar_methods, &dev_mar_event_callbacks, (void*)mi);
}

int hio_dev_mar_connect (hio_dev_mar_t* dev, hio_dev_mar_connect_t* ci)
{
	return hio_dev_ioctl((hio_dev_t*)dev, HIO_DEV_MAR_CONNECT, ci);
}

int hio_dev_mar_querywithbchars (hio_dev_mar_t* dev, const hio_bch_t* qstr, hio_oow_t qlen)
{
	hio_bcs_t bcs = { (hio_bch_t*)qstr, qlen};
	return hio_dev_ioctl((hio_dev_t*)dev, HIO_DEV_MAR_QUERY_WITH_BCS, &bcs);
}

int hio_dev_mar_fetchrows (hio_dev_mar_t* dev)
{
	return hio_dev_ioctl((hio_dev_t*)dev, HIO_DEV_MAR_FETCH_ROW, HIO_NULL);
}

hio_oow_t hio_dev_mar_escapebchars (hio_dev_mar_t* dev, const hio_bch_t* qstr, hio_oow_t qlen, hio_bch_t* buf)
{
	hio_dev_mar_t* rdev = (hio_dev_mar_t*)dev;
	return mysql_real_escape_string (rdev->hnd, buf, qstr, qlen);
}
