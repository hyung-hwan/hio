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

#include "sys-prv.h"

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

int mio_sys_inittime (mio_t* mio)
{
	/*mio_sys_time_t* tim = &mio->sysdep->time;*/
	/* nothing to do */
	return 0;
}

void mio_sys_finitime (mio_t* mio)
{
	/*mio_sys_time_t* tim = &mio->sysdep->tim;*/
	/* nothing to do */
}

void mio_sys_gettime (mio_t* mio, mio_ntime_t* now)
{
#if defined(_WIN32)

	#if defined(_WIN64) || (defined(_WIN32_WINNT) && (_WIN32_WINNT >= 0x0600))
	mio_uint64_t bigsec, bigmsec;
	bigmsec = GetTickCount64();
	#else
	mio_sys_time_t* tim = &mio->sysdep->tim;
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
	mio_sys_time_t* tim = &mio->sysdep->tim;
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
	mio_sys_time_t* tim = &mio->sysdep->tim;
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


void mio_sys_getrealtime (mio_t* mio, mio_ntime_t* now)
{
#if defined(HAVE_CLOCK_GETTIME) && defined(CLOCK_REALTIME)
	struct timespec ts;
	clock_gettime (CLOCK_REALTIME, &ts);
	MIO_INIT_NTIME(now, ts.tv_sec, ts.tv_nsec);
#else
	struct timeval tv;
	gettimeofday (&tv, MIO_NULL);
	MIO_INIT_NTIME(now, tv.tv_sec, MIO_USEC_TO_NSEC(tv.tv_usec));
#endif
}
