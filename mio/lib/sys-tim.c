/*
 * $Id$
 *
    Copyright (c) 2015-2016 Chung, Hyung-Hwan. All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions
    are met:
    1. Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.

    THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR
    IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
    OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
    IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
    INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
    NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
    THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "mio-prv.h"

#if defined(_WIN32)
#	include <windows.h>
#	include <time.h>
#elif defined(__OS2__)
#	define INCL_DOSDATETIME
#	define INCL_DOSERRORS
#	include <os2.h>
#	include <time.h>
#elif defined(__DOS__)
#	include <dos.h>
#	include <time.h>
#else
#	if defined(HAVE_SYS_TIME_H)
#		include <sys/time.h>
#	endif
#	if defined(HAVE_TIME_H)
#		include <time.h>
#	endif
#	include <errno.h>
#endif

#define MIO_EPOCH_YEAR  (1970)
#define MIO_EPOCH_MON   (1)
#define MIO_EPOCH_DAY   (1)
#define MIO_EPOCH_WDAY  (4)

/* windows specific epoch time */
#define MIO_EPOCH_YEAR_WIN   (1601)
#define MIO_EPOCH_MON_WIN    (1)
#define MIO_EPOCH_DAY_WIN    (1)

#if defined(_WIN32)
	#define EPOCH_DIFF_YEARS (MIO_EPOCH_YEAR-MIO_EPOCH_YEAR_WIN)
	#define EPOCH_DIFF_DAYS  ((mio_intptr_t)EPOCH_DIFF_YEARS*365+EPOCH_DIFF_YEARS/4-3)
	#define EPOCH_DIFF_SECS  ((mio_intptr_t)EPOCH_DIFF_DAYS*24*60*60)
#endif

#if 0
void mio_sys_gettime (mio_ntime_t* now)
{
#if defined(_WIN32)
	SYSTEMTIME st;
	FILETIME ft;
	ULARGE_INTEGER li;

	/* 
	 * MSDN: The FILETIME structure is a 64-bit value representing the 
	 *       number of 100-nanosecond intervals since January 1, 1601 (UTC).
	 */

	GetSystemTime (&st);
	SystemTimeToFileTime (&st, &ft); /* this must not fail */

	li.LowPart = ft.dwLowDateTime;
	li.HighPart = ft.dwHighDateTime;

     /* li.QuadPart is in the 100-nanosecond intervals */
	now->sec = (li.QuadPart / (MIO_NSECS_PER_SEC / 100)) - EPOCH_DIFF_SECS;
	now->nsec = (li.QuadPart % (MIO_NSECS_PER_SEC / 100)) * 100;

#elif defined(__OS2__)

	DATETIME dt;
	mio_btime_t bt;

	/* Can I use DosQuerySysInfo(QSV_TIME_LOW) and 
	 * DosQuerySysInfo(QSV_TIME_HIGH) for this instead? 
	 * Maybe, resolution too low as it returns values 
	 * in seconds. */

	DosGetDateTime (&dt);
	/* DosGetDateTime() never fails. it always returns NO_ERROR */

	bt.year = dt.year - MIO_BTIME_YEAR_BASE;
	bt.mon = dt.month - 1;
	bt.mday = dt.day;
	bt.hour = dt.hours;
	bt.min = dt.minutes;
	bt.sec = dt.seconds;
	/*bt.msec = dt.hundredths * 10;*/
	bt.isdst = -1; /* determine dst for me */

	if (mio_timelocal(&bt, t) <= -1) 
	{
		now->sec = time (MIO_NULL);
		now->nsec = 0;
	}
	else
	{
		now->nsec = MIO_MSEC_TO_NSEC(dt.hundredths * 10);
	}
	return 0;

#elif defined(__DOS__)

	struct dostime_t dt;
	struct dosdate_t dd;
	mio_btime_t bt;

	_dos_gettime (&dt);
	_dos_getdate (&dd);

	bt.year = dd.year - MIO_BTIME_YEAR_BASE;
	bt.mon = dd.month - 1;
	bt.mday = dd.day;
	bt.hour = dt.hour;
	bt.min = dt.minute;
	bt.sec = dt.second;
	/*bt.msec = dt.hsecond * 10; */
	bt.isdst = -1; /* determine dst for me */

	if (mio_timelocal(&bt, t) <= -1) 
	{
		now->sec = time (MIO_NULL);
		now->nsec = 0;
	}
	else
	{
		now->nsec = MIO_MSEC_TO_NSEC(dt.hsecond * 10);
	}

#elif defined(macintosh)
	unsigned long tv;

	GetDateTime (&tv);
	now->sec = tv;
	tv->nsec = 0;

#elif defined(HAVE_CLOCK_GETTIME) && defined(CLOCK_REALTIME)
	struct timespec ts;

	if (clock_gettime(CLOCK_REALTIME, &ts) == -1 && errno == EINVAL)
	{
	#if defined(HAVE_GETTIMEOFDAY)
		struct timeval tv;
		gettimeofday (&tv, MIO_NULL);
		now->sec = tv.tv_sec;
		now->nsec = MIO_USEC_TO_NSEC(tv.tv_usec);
	#else
		now->sec = time (MIO_NULL);
		now->nsec = 0;
	#endif
	}

	now->sec = ts.tv_sec;
	now->nsec = ts.tv_nsec;

#elif defined(HAVE_GETTIMEOFDAY)
	struct timeval tv;
	gettimeofday (&tv, MIO_NULL);
	now->sec = tv.tv_sec;
	now->nsec = MIO_USEC_TO_NSEC(tv.tv_usec);

#else
	now->sec = time(MIO_NULL);
	now->nsec = 0;
#endif
}

#else

void mio_sys_gettime (mio_ntime_t* now)
{
#if defined(_WIN32)

	#if defined(_WIN64) || (defined(_WIN32_WINNT) && (_WIN32_WINNT >= 0x0600))
	mio_uint64_t bigsec, bigmsec;
	bigmsec = GetTickCount64();
	#else
	xtn_t* xtn = GET_XTN(mio);
	mio_uint64_t bigsec, bigmsec;
	DWORD msec;

	msec = GetTickCount(); /* this can sustain for 49.7 days */
	if (msec < xtn->tc_last)
	{
		/* i assume the difference is never bigger than 49.7 days */
		/*diff = (MIO_TYPE_MAX(DWORD) - xtn->tc_last) + 1 + msec;*/
		xtn->tc_overflow++;
		bigmsec = ((mio_uint64_t)MIO_TYPE_MAX(DWORD) * xtn->tc_overflow) + msec;
	}
	else bigmsec = msec;
	xtn->tc_last = msec;
	#endif

	bigsec = MIO_MSEC_TO_SEC(bigmsec);
	bigmsec -= MIO_SEC_TO_MSEC(bigsec);
	MIO_INIT_NTIME(now, bigsec, MIO_MSEC_TO_NSEC(bigmsec));

#elif defined(__OS2__)
	xtn_t* xtn = GET_XTN(mio);
	ULONG msec, elapsed;
	mio_ntime_t et;

/* TODO: use DosTmrQueryTime() and DosTmrQueryFreq()? */
	DosQuerySysInfo (QSV_MS_COUNT, QSV_MS_COUNT, &msec, MIO_SIZEOF(msec)); /* milliseconds */

	elapsed = (msec < xtn->tc_last)? (MIO_TYPE_MAX(ULONG) - xtn->tc_last + msec + 1): (msec - xtn->tc_last);
	xtn->tc_last = msec;

	et.sec = MIO_MSEC_TO_SEC(elapsed);
	msec = elapsed - MIO_SEC_TO_MSEC(et.sec);
	et.nsec = MIO_MSEC_TO_NSEC(msec);

	MIO_ADD_NTIME (&xtn->tc_last_ret , &xtn->tc_last_ret, &et);
	*now = xtn->tc_last_ret;

#elif defined(__DOS__) && (defined(_INTELC32_) || defined(__WATCOMC__))
	xtn_t* xtn = GET_XTN(mio);
	clock_t c, elapsed;
	mio_ntime_t et;

	c = clock();
	elapsed = (c < xtn->tc_last)? (MIO_TYPE_MAX(clock_t) - xtn->tc_last + c + 1): (c - xtn->tc_last);
	xtn->tc_last = c;

	et.sec = elapsed / CLOCKS_PER_SEC;
	#if (CLOCKS_PER_SEC == 100)
		et.nsec = MIO_MSEC_TO_NSEC((elapsed % CLOCKS_PER_SEC) * 10);
	#elif (CLOCKS_PER_SEC == 1000)
		et.nsec = MIO_MSEC_TO_NSEC(elapsed % CLOCKS_PER_SEC);
	#elif (CLOCKS_PER_SEC == 1000000L)
		et.nsec = MIO_USEC_TO_NSEC(elapsed % CLOCKS_PER_SEC);
	#elif (CLOCKS_PER_SEC == 1000000000L)
		et.nsec = (elapsed % CLOCKS_PER_SEC);
	#else
	#	error UNSUPPORTED CLOCKS_PER_SEC
	#endif

	MIO_ADD_NTIME (&xtn->tc_last_ret , &xtn->tc_last_ret, &et);
	*now = xtn->tc_last_ret;

#elif defined(macintosh)
	UnsignedWide tick;
	mio_uint64_t tick64;
	Microseconds (&tick);
	tick64 = *(mio_uint64_t*)&tick;
	MIO_INIT_NTIME (now, MIO_USEC_TO_SEC(tick64), MIO_USEC_TO_NSEC(tick64));
#elif defined(HAVE_CLOCK_GETTIME) && defined(CLOCK_MONOTONIC)
	struct timespec ts;
	clock_gettime (CLOCK_MONOTONIC, &ts);
	MIO_INIT_NTIME(now, ts.tv_sec, ts.tv_nsec);
#elif defined(HAVE_CLOCK_GETTIME) && defined(CLOCK_REALTIME)
	struct timespec ts;
	clock_gettime (CLOCK_REALTIME, &ts);
	MIO_INIT_NTIME(now, ts.tv_sec, ts.tv_nsec);
#else
	struct timeval tv;
	gettimeofday (&tv, MIO_NULL);
	MIO_INIT_NTIME(now, tv.tv_sec, MIO_USEC_TO_NSEC(tv.tv_usec));
#endif
}

#endif
