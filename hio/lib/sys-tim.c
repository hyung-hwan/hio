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

int hio_sys_inittime (hio_t* hio)
{
	/*hio_sys_time_t* tim = &hio->sysdep->time;*/
	/* nothing to do */
	return 0;
}

void hio_sys_finitime (hio_t* hio)
{
	/*hio_sys_time_t* tim = &hio->sysdep->tim;*/
	/* nothing to do */
}

void hio_sys_gettime (hio_t* hio, hio_ntime_t* now)
{
#if defined(_WIN32)

	#if defined(_WIN64) || (defined(_WIN32_WINNT) && (_WIN32_WINNT >= 0x0600))
	hio_uint64_t bigsec, bigmsec;
	bigmsec = GetTickCount64();
	#else
	hio_sys_time_t* tim = &hio->sysdep->tim;
	hio_uint64_t bigsec, bigmsec;
	DWORD msec;

	msec = GetTickCount(); /* this can sustain for 49.7 days */
	if (msec < xtn->tc_last)
	{
		/* i assume the difference is never bigger than 49.7 days */
		/*diff = (HIO_TYPE_MAX(DWORD) - xtn->tc_last) + 1 + msec;*/
		xtn->tc_overflow++;
		bigmsec = ((hio_uint64_t)HIO_TYPE_MAX(DWORD) * xtn->tc_overflow) + msec;
	}
	else bigmsec = msec;
	xtn->tc_last = msec;
	#endif

	bigsec = HIO_MSEC_TO_SEC(bigmsec);
	bigmsec -= HIO_SEC_TO_MSEC(bigsec);
	HIO_INIT_NTIME(now, bigsec, HIO_MSEC_TO_NSEC(bigmsec));

#elif defined(__OS2__)
	hio_sys_time_t* tim = &hio->sysdep->tim;
	ULONG msec, elapsed;
	hio_ntime_t et;

/* TODO: use DosTmrQueryTime() and DosTmrQueryFreq()? */
	DosQuerySysInfo (QSV_MS_COUNT, QSV_MS_COUNT, &msec, HIO_SIZEOF(msec)); /* milliseconds */

	elapsed = (msec < xtn->tc_last)? (HIO_TYPE_MAX(ULONG) - xtn->tc_last + msec + 1): (msec - xtn->tc_last);
	xtn->tc_last = msec;

	et.sec = HIO_MSEC_TO_SEC(elapsed);
	msec = elapsed - HIO_SEC_TO_MSEC(et.sec);
	et.nsec = HIO_MSEC_TO_NSEC(msec);

	HIO_ADD_NTIME (&xtn->tc_last_ret , &xtn->tc_last_ret, &et);
	*now = xtn->tc_last_ret;

#elif defined(__DOS__) && (defined(_INTELC32_) || defined(__WATCOMC__))
	hio_sys_time_t* tim = &hio->sysdep->tim;
	clock_t c, elapsed;
	hio_ntime_t et;

	c = clock();
	elapsed = (c < xtn->tc_last)? (HIO_TYPE_MAX(clock_t) - xtn->tc_last + c + 1): (c - xtn->tc_last);
	xtn->tc_last = c;

	et.sec = elapsed / CLOCKS_PER_SEC;
	#if (CLOCKS_PER_SEC == 100)
		et.nsec = HIO_MSEC_TO_NSEC((elapsed % CLOCKS_PER_SEC) * 10);
	#elif (CLOCKS_PER_SEC == 1000)
		et.nsec = HIO_MSEC_TO_NSEC(elapsed % CLOCKS_PER_SEC);
	#elif (CLOCKS_PER_SEC == 1000000L)
		et.nsec = HIO_USEC_TO_NSEC(elapsed % CLOCKS_PER_SEC);
	#elif (CLOCKS_PER_SEC == 1000000000L)
		et.nsec = (elapsed % CLOCKS_PER_SEC);
	#else
	#	error UNSUPPORTED CLOCKS_PER_SEC
	#endif

	HIO_ADD_NTIME (&xtn->tc_last_ret , &xtn->tc_last_ret, &et);
	*now = xtn->tc_last_ret;

#elif defined(macintosh)
	UnsignedWide tick;
	hio_uint64_t tick64;
	Microseconds (&tick);
	tick64 = *(hio_uint64_t*)&tick;
	HIO_INIT_NTIME (now, HIO_USEC_TO_SEC(tick64), HIO_USEC_TO_NSEC(tick64));
#elif defined(HAVE_CLOCK_GETTIME) && defined(CLOCK_MONOTONIC)
	struct timespec ts;
	clock_gettime (CLOCK_MONOTONIC, &ts);
	HIO_INIT_NTIME(now, ts.tv_sec, ts.tv_nsec);
#elif defined(HAVE_CLOCK_GETTIME) && defined(CLOCK_REALTIME)
	struct timespec ts;
	clock_gettime (CLOCK_REALTIME, &ts);
	HIO_INIT_NTIME(now, ts.tv_sec, ts.tv_nsec);
#else
	struct timeval tv;
	gettimeofday (&tv, HIO_NULL);
	HIO_INIT_NTIME(now, tv.tv_sec, HIO_USEC_TO_NSEC(tv.tv_usec));
#endif
}


void hio_sys_getrealtime (hio_t* hio, hio_ntime_t* now)
{
#if defined(HAVE_CLOCK_GETTIME) && defined(CLOCK_REALTIME)
	struct timespec ts;
	clock_gettime (CLOCK_REALTIME, &ts);
	HIO_INIT_NTIME(now, ts.tv_sec, ts.tv_nsec);
#else
	struct timeval tv;
	gettimeofday (&tv, HIO_NULL);
	HIO_INIT_NTIME(now, tv.tv_sec, HIO_USEC_TO_NSEC(tv.tv_usec));
#endif
}
