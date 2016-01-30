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

#include "stio-tim.h"
#include "stio-prv.h"

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

#if defined(_WIN32)
	#define EPOCH_DIFF_YEARS (STIO_EPOCH_YEAR-STIO_EPOCH_YEAR_WIN)
	#define EPOCH_DIFF_DAYS  ((stio_intptr_t)EPOCH_DIFF_YEARS*365+EPOCH_DIFF_YEARS/4-3)
	#define EPOCH_DIFF_SECS  ((stio_intptr_t)EPOCH_DIFF_DAYS*24*60*60)
#endif

int stio_gettime (stio_ntime_t* t)
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
	if (SystemTimeToFileTime (&st, &ft) == FALSE) return -1;

	li.LowPart = ft.dwLowDateTime;
	li.HighPart = ft.dwHighDateTime;

     /* li.QuadPart is in the 100-nanosecond intervals */
	t->sec = (li.QuadPart / (STIO_NSECS_PER_SEC / 100)) - EPOCH_DIFF_SECS;
	t->nsec = (li.QuadPart % (STIO_NSECS_PER_SEC / 100)) * 100;

	return 0;

#elif defined(__OS2__)

	APIRET rc;
	DATETIME dt;
	stio_btime_t bt;

	/* Can I use DosQuerySysInfo(QSV_TIME_LOW) and 
	 * DosQuerySysInfo(QSV_TIME_HIGH) for this instead? 
	 * Maybe, resolution too low as it returns values 
	 * in seconds. */

	rc = DosGetDateTime (&dt);
	if (rc != NO_ERROR) return -1;

	bt.year = dt.year - STIO_BTIME_YEAR_BASE;
	bt.mon = dt.month - 1;
	bt.mday = dt.day;
	bt.hour = dt.hours;
	bt.min = dt.minutes;
	bt.sec = dt.seconds;
	/*bt.msec = dt.hundredths * 10;*/
	bt.isdst = -1; /* determine dst for me */

	if (stio_timelocal (&bt, t) <= -1) return -1;
	t->nsec = STIO_MSEC_TO_NSEC(dt.hundredths * 10);
	return 0;

#elif defined(__DOS__)

	struct dostime_t dt;
	struct dosdate_t dd;
	stio_btime_t bt;

	_dos_gettime (&dt);
	_dos_getdate (&dd);

	bt.year = dd.year - STIO_BTIME_YEAR_BASE;
	bt.mon = dd.month - 1;
	bt.mday = dd.day;
	bt.hour = dt.hour;
	bt.min = dt.minute;
	bt.sec = dt.second;
	/*bt.msec = dt.hsecond * 10; */
	bt.isdst = -1; /* determine dst for me */

	if (stio_timelocal (&bt, t) <= -1) return -1;
	t->nsec = STIO_MSEC_TO_NSEC(dt.hsecond * 10);
	return 0;

#elif defined(macintosh)
	unsigned long tv;

	GetDateTime (&tv);

	t->sec = tv;
	tv->nsec = 0;
	
	return 0;

#elif defined(HAVE_GETTIMEOFDAY)
	struct timeval tv;
	int n;

	/* TODO: consider using clock_gettime() if it's avaialble.. -lrt may be needed */
	n = gettimeofday (&tv, STIO_NULL);
	if (n == -1) 
	{
/* TODO: set stio->errnum using errno... */
		return -1;
	}

	t->sec = tv.tv_sec;
	t->nsec = STIO_USEC_TO_NSEC(tv.tv_usec);
	return 0;

#else
	t->sec = STIO_TIME (STIO_NULL);
	t->nsec = 0;

	return 0;
#endif
}

void stio_addtime (const stio_ntime_t* x, const stio_ntime_t* y, stio_ntime_t* z)
{
	STIO_ASSERT (x->nsec >= 0 && x->nsec < STIO_NSECS_PER_SEC);
	STIO_ASSERT (y->nsec >= 0 && y->nsec < STIO_NSECS_PER_SEC);

	z->sec = x->sec + y->sec;
	z->nsec = x->nsec + y->nsec;

	if (z->nsec >= STIO_NSECS_PER_SEC)
	{
		z->sec = z->sec + 1;
		z->nsec = z->nsec - STIO_NSECS_PER_SEC;
	}
}

void stio_subtime (const stio_ntime_t* x, const stio_ntime_t* y, stio_ntime_t* z)
{
	STIO_ASSERT (x->nsec >= 0 && x->nsec < STIO_NSECS_PER_SEC);
	STIO_ASSERT (y->nsec >= 0 && y->nsec < STIO_NSECS_PER_SEC);

	z->sec = x->sec - y->sec;
	z->nsec = x->nsec - y->nsec;

	if (z->nsec < 0)
	{
		z->sec = z->sec - 1;
		z->nsec = z->nsec + STIO_NSECS_PER_SEC;
	}
}
