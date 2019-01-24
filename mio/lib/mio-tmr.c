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


#include "mio-prv.h"

#define HEAP_PARENT(x) (((x) - 1) / 2)
#define HEAP_LEFT(x)   ((x) * 2 + 1)
#define HEAP_RIGHT(x)  ((x) * 2 + 2)

#define YOUNGER_THAN(x,y) (mio_cmptime(&(x)->when, &(y)->when) < 0)

void mio_cleartmrjobs (mio_t* mio)
{
	while (mio->tmr.size > 0) mio_deltmrjob (mio, 0);
}

static mio_tmridx_t sift_up (mio_t* mio, mio_tmridx_t index, int notify)
{
	mio_tmridx_t parent;

	parent = HEAP_PARENT(index);
	if (index > 0 && YOUNGER_THAN(&mio->tmr.jobs[index], &mio->tmr.jobs[parent]))
	{
		mio_tmrjob_t item;

		item = mio->tmr.jobs[index]; 

		do
		{
			/* move down the parent to my current position */
			mio->tmr.jobs[index] = mio->tmr.jobs[parent];
			if (mio->tmr.jobs[index].idxptr) *mio->tmr.jobs[index].idxptr = index;

			/* traverse up */
			index = parent;
			parent = HEAP_PARENT(parent);
		}
		while (index > 0 && YOUNGER_THAN(&item, &mio->tmr.jobs[parent]));

		mio->tmr.jobs[index] = item;
		if (mio->tmr.jobs[index].idxptr) *mio->tmr.jobs[index].idxptr = index;
	}

	return index;
}

static mio_tmridx_t sift_down (mio_t* mio, mio_tmridx_t index, int notify)
{
	mio_oow_t base = mio->tmr.size / 2;

	if (index < base) /* at least 1 child is under the 'index' position */
	{
		mio_tmrjob_t item;

		item = mio->tmr.jobs[index];

		do
		{
			mio_tmridx_t left, right, younger;

			left = HEAP_LEFT(index);
			right = HEAP_RIGHT(index);

			if (right < mio->tmr.size && YOUNGER_THAN(&mio->tmr.jobs[right], &mio->tmr.jobs[left]))
			{
				younger = right;
			}
			else
			{
				younger = left;
			}

			if (YOUNGER_THAN(&item, &mio->tmr.jobs[younger])) break;

			mio->tmr.jobs[index] = mio->tmr.jobs[younger];
			if (mio->tmr.jobs[index].idxptr) *mio->tmr.jobs[index].idxptr = index;

			index = younger;
		}
		while (index < base);
		
		mio->tmr.jobs[index] = item;
		if (mio->tmr.jobs[index].idxptr) *mio->tmr.jobs[index].idxptr = index;
	}

	return index;
}

void mio_deltmrjob (mio_t* mio, mio_tmridx_t index)
{
	mio_tmrjob_t item;

	MIO_ASSERT (index < mio->tmr.size);

	item = mio->tmr.jobs[index];
	if (mio->tmr.jobs[index].idxptr) *mio->tmr.jobs[index].idxptr = MIO_TMRIDX_INVALID;

	mio->tmr.size = mio->tmr.size - 1;
	if (mio->tmr.size > 0 && index != mio->tmr.size)
	{
		mio->tmr.jobs[index] = mio->tmr.jobs[mio->tmr.size];
		if (mio->tmr.jobs[index].idxptr) *mio->tmr.jobs[index].idxptr = index;
		YOUNGER_THAN(&mio->tmr.jobs[index], &item)? sift_up(mio, index, 1): sift_down(mio, index, 1);
	}
}

mio_tmridx_t mio_instmrjob (mio_t* mio, const mio_tmrjob_t* job)
{
	mio_tmridx_t index = mio->tmr.size;

	if (index >= mio->tmr.capa)
	{
		mio_tmrjob_t* tmp;
		mio_oow_t new_capa;

		MIO_ASSERT (mio->tmr.capa >= 1);
		new_capa = mio->tmr.capa * 2;
		tmp = (mio_tmrjob_t*)MIO_MMGR_REALLOC (mio->mmgr, mio->tmr.jobs, new_capa * MIO_SIZEOF(*tmp));
		if (tmp == MIO_NULL) 
		{
			mio->errnum = MIO_ESYSMEM;
			return MIO_TMRIDX_INVALID;
		}

		mio->tmr.jobs = tmp;
		mio->tmr.capa = new_capa;
	}

	mio->tmr.size = mio->tmr.size + 1;
	mio->tmr.jobs[index] = *job;
	if (mio->tmr.jobs[index].idxptr) *mio->tmr.jobs[index].idxptr = index;
	return sift_up (mio, index, 0);
}

mio_tmridx_t mio_updtmrjob (mio_t* mio, mio_tmridx_t index, const mio_tmrjob_t* job)
{
	mio_tmrjob_t item;
	item = mio->tmr.jobs[index];
	mio->tmr.jobs[index] = *job;
	if (mio->tmr.jobs[index].idxptr) *mio->tmr.jobs[index].idxptr = index;
	return YOUNGER_THAN(job, &item)? sift_up (mio, index, 0): sift_down (mio, index, 0);
}

void mio_firetmrjobs (mio_t* mio, const mio_ntime_t* tm, mio_oow_t* firecnt)
{
	mio_ntime_t now;
	mio_tmrjob_t tmrjob;
	mio_oow_t count = 0;

	/* if the current time is not specified, get it from the system */
	if (tm) now = *tm;
	else mio_gettime (&now);

	while (mio->tmr.size > 0)
	{
		if (mio_cmptime(&mio->tmr.jobs[0].when, &now) > 0) break;

		tmrjob = mio->tmr.jobs[0]; /* copy the scheduled job */
		mio_deltmrjob (mio, 0); /* deschedule the job */

		count++;
		tmrjob.handler (mio, &now, &tmrjob); /* then fire the job */
	}

	if (firecnt) *firecnt = count;
}

int mio_gettmrtmout (mio_t* mio, const mio_ntime_t* tm, mio_ntime_t* tmout)
{
	mio_ntime_t now;

	/* time-out can't be calculated when there's no job scheduled */
	if (mio->tmr.size <= 0) 
	{
		mio->errnum = MIO_ENOENT;
		return -1;
	}

	/* if the current time is not specified, get it from the system */
	if (tm) now = *tm;
	else mio_gettime (&now);

	mio_subtime (&mio->tmr.jobs[0].when, &now, tmout);
	if (tmout->sec < 0) mio_cleartime (tmout);

	return 0;
}

mio_tmrjob_t* mio_gettmrjob (mio_t* mio, mio_tmridx_t index)
{
	if (index < 0 || index >= mio->tmr.size)
	{
		mio->errnum = MIO_ENOENT;
		return MIO_NULL;
	}

	return &mio->tmr.jobs[index];
}

int mio_gettmrjobdeadline (mio_t* mio, mio_tmridx_t index, mio_ntime_t* deadline)
{
	if (index < 0 || index >= mio->tmr.size)
	{
		mio->errnum = MIO_ENOENT;
		return -1;
	}

	*deadline = mio->tmr.jobs[index].when;
	return 0;
}
