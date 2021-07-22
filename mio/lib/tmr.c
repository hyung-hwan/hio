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


#include "mio-prv.h"

#define HEAP_PARENT(x) (((x) - 1) / 2)
#define HEAP_LEFT(x)   ((x) * 2 + 1)
#define HEAP_RIGHT(x)  ((x) * 2 + 2)

#define YOUNGER_THAN(x,y) (MIO_CMP_NTIME(&(x)->when, &(y)->when) < 0)

void mio_cleartmrjobs (mio_t* mio)
{
	while (mio->tmr.size > 0) mio_deltmrjob (mio, 0);
}

static mio_tmridx_t sift_up (mio_t* mio, mio_tmridx_t index)
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

static mio_tmridx_t sift_down (mio_t* mio, mio_tmridx_t index)
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

	MIO_ASSERT (mio, index < mio->tmr.size);

	item = mio->tmr.jobs[index];
	if (mio->tmr.jobs[index].idxptr) *mio->tmr.jobs[index].idxptr = MIO_TMRIDX_INVALID;

	mio->tmr.size = mio->tmr.size - 1;
	if (mio->tmr.size > 0 && index != mio->tmr.size)
	{
		mio->tmr.jobs[index] = mio->tmr.jobs[mio->tmr.size];
		if (mio->tmr.jobs[index].idxptr) *mio->tmr.jobs[index].idxptr = index;
		YOUNGER_THAN(&mio->tmr.jobs[index], &item)? sift_up(mio, index): sift_down(mio, index);
	}
}

mio_tmridx_t mio_instmrjob (mio_t* mio, const mio_tmrjob_t* job)
{
	mio_tmridx_t index = mio->tmr.size;

	if (index >= mio->tmr.capa)
	{
		mio_tmrjob_t* tmp;
		mio_oow_t new_capa;

		MIO_ASSERT (mio, mio->tmr.capa >= 1);
		new_capa = mio->tmr.capa * 2;
		tmp = (mio_tmrjob_t*)mio_reallocmem(mio, mio->tmr.jobs, new_capa * MIO_SIZEOF(*tmp));
		if (!tmp) return MIO_TMRIDX_INVALID;

		mio->tmr.jobs = tmp;
		mio->tmr.capa = new_capa;
	}

	mio->tmr.size = mio->tmr.size + 1;
	mio->tmr.jobs[index] = *job;
	if (mio->tmr.jobs[index].idxptr) *mio->tmr.jobs[index].idxptr = index;
	return sift_up(mio, index);
}

mio_tmridx_t mio_updtmrjob (mio_t* mio, mio_tmridx_t index, const mio_tmrjob_t* job)
{
	mio_tmrjob_t item;
	item = mio->tmr.jobs[index];
	mio->tmr.jobs[index] = *job;
	if (mio->tmr.jobs[index].idxptr) *mio->tmr.jobs[index].idxptr = index;
	return YOUNGER_THAN(job, &item)? sift_up(mio, index): sift_down(mio, index);
}

void mio_firetmrjobs (mio_t* mio, const mio_ntime_t* tm, mio_oow_t* firecnt)
{
	mio_ntime_t now;
	mio_tmrjob_t tmrjob;
	mio_oow_t count = 0;

	/* if the current time is not specified, get it from the system */
	if (tm) now = *tm;
	else mio_gettime (mio, &now);

	while (mio->tmr.size > 0)
	{
		if (MIO_CMP_NTIME(&mio->tmr.jobs[0].when, &now) > 0) break;

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
	if (mio->tmr.size <= 0) return 0; /* no scheduled job */

	/* if the current time is not specified, get it from the system */
	if (tm) now = *tm;
	else mio_gettime (mio, &now);

	MIO_SUB_NTIME (tmout, &mio->tmr.jobs[0].when, &now);
	if (tmout->sec < 0) MIO_CLEAR_NTIME (tmout);
	return 1; /* tmout is set */
}

mio_tmrjob_t* mio_gettmrjob (mio_t* mio, mio_tmridx_t index)
{
	if (index < 0 || index >= mio->tmr.size)
	{
		mio_seterrbfmt (mio, MIO_ENOENT, "unable to get timer job as the given index is out of range");
		return MIO_NULL;
	}

	return &mio->tmr.jobs[index];
}

int mio_gettmrjobdeadline (mio_t* mio, mio_tmridx_t index, mio_ntime_t* deadline)
{
	if (index < 0 || index >= mio->tmr.size)
	{
		mio_seterrbfmt (mio, MIO_ENOENT, "unable to get timer job deadline as the given index is out of range");
		return -1;
	}

	*deadline = mio->tmr.jobs[index].when;
	return 0;
}

int mio_schedtmrjobat (mio_t* mio, const mio_ntime_t* fire_at, mio_tmrjob_handler_t handler, mio_tmridx_t* tmridx, void* ctx)
{
	mio_tmrjob_t tmrjob;

	MIO_MEMSET (&tmrjob, 0, MIO_SIZEOF(tmrjob));
	tmrjob.ctx = ctx;
	if (fire_at) tmrjob.when = *fire_at;

	tmrjob.handler = handler;
	tmrjob.idxptr = tmridx;

	return mio_instmrjob(mio, &tmrjob) == MIO_TMRIDX_INVALID? -1: 0;
}

int mio_schedtmrjobafter (mio_t* mio, const mio_ntime_t* fire_after, mio_tmrjob_handler_t handler, mio_tmridx_t* tmridx, void* ctx)
{
	mio_ntime_t fire_at;

	MIO_ASSERT (mio, MIO_IS_POS_NTIME(fire_after));

	mio_gettime (mio, &fire_at);
	MIO_ADD_NTIME (&fire_at, &fire_at, fire_after);

	return mio_schedtmrjobat(mio, &fire_at, handler, tmridx, ctx);
}
