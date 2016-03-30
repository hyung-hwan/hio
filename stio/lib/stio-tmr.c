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


#include "stio-prv.h"

#define HEAP_PARENT(x) (((x) - 1) / 2)
#define HEAP_LEFT(x)   ((x) * 2 + 1)
#define HEAP_RIGHT(x)  ((x) * 2 + 2)

#define YOUNGER_THAN(x,y) (stio_cmptime(&(x)->when, &(y)->when) < 0)

void stio_cleartmrjobs (stio_t* stio)
{
	while (stio->tmr.size > 0) stio_deltmrjob (stio, 0);
}

static stio_tmridx_t sift_up (stio_t* stio, stio_tmridx_t index, int notify)
{
	stio_tmridx_t parent;

	parent = HEAP_PARENT(index);
	if (index > 0 && YOUNGER_THAN(&stio->tmr.jobs[index], &stio->tmr.jobs[parent]))
	{
		stio_tmrjob_t item;

		item = stio->tmr.jobs[index]; 

		do
		{
			/* move down the parent to my current position */
			stio->tmr.jobs[index] = stio->tmr.jobs[parent];
			if (stio->tmr.jobs[index].idxptr) *stio->tmr.jobs[index].idxptr = index;

			/* traverse up */
			index = parent;
			parent = HEAP_PARENT(parent);
		}
		while (index > 0 && YOUNGER_THAN(&item, &stio->tmr.jobs[parent]));

		stio->tmr.jobs[index] = item;
		if (stio->tmr.jobs[index].idxptr) *stio->tmr.jobs[index].idxptr = index;
	}

	return index;
}

static stio_tmridx_t sift_down (stio_t* stio, stio_tmridx_t index, int notify)
{
	stio_size_t base = stio->tmr.size / 2;

	if (index < base) /* at least 1 child is under the 'index' position */
	{
		stio_tmrjob_t item;

		item = stio->tmr.jobs[index];

		do
		{
			stio_tmridx_t left, right, younger;

			left = HEAP_LEFT(index);
			right = HEAP_RIGHT(index);

			if (right < stio->tmr.size && YOUNGER_THAN(&stio->tmr.jobs[right], &stio->tmr.jobs[left]))
			{
				younger = right;
			}
			else
			{
				younger = left;
			}

			if (YOUNGER_THAN(&item, &stio->tmr.jobs[younger])) break;

			stio->tmr.jobs[index] = stio->tmr.jobs[younger];
			if (stio->tmr.jobs[index].idxptr) *stio->tmr.jobs[index].idxptr = index;

			index = younger;
		}
		while (index < base);
		
		stio->tmr.jobs[index] = item;
		if (stio->tmr.jobs[index].idxptr) *stio->tmr.jobs[index].idxptr = index;
	}

	return index;
}

void stio_deltmrjob (stio_t* stio, stio_tmridx_t index)
{
	stio_tmrjob_t item;

	STIO_ASSERT (index < stio->tmr.size);

	item = stio->tmr.jobs[index];
	if (stio->tmr.jobs[index].idxptr) *stio->tmr.jobs[index].idxptr = STIO_TMRIDX_INVALID;

	stio->tmr.size = stio->tmr.size - 1;
	if (stio->tmr.size > 0 && index != stio->tmr.size)
	{
		stio->tmr.jobs[index] = stio->tmr.jobs[stio->tmr.size];
		if (stio->tmr.jobs[index].idxptr) *stio->tmr.jobs[index].idxptr = index;
		YOUNGER_THAN(&stio->tmr.jobs[index], &item)? sift_up(stio, index, 1): sift_down(stio, index, 1);
	}
}

stio_tmridx_t stio_instmrjob (stio_t* stio, const stio_tmrjob_t* job)
{
	stio_tmridx_t index = stio->tmr.size;

	if (index >= stio->tmr.capa)
	{
		stio_tmrjob_t* tmp;
		stio_size_t new_capa;

		STIO_ASSERT (stio->tmr.capa >= 1);
		new_capa = stio->tmr.capa * 2;
		tmp = (stio_tmrjob_t*)STIO_MMGR_REALLOC (stio->mmgr, stio->tmr.jobs, new_capa * STIO_SIZEOF(*tmp));
		if (tmp == STIO_NULL) 
		{
			stio->errnum = STIO_ENOMEM;
			return STIO_TMRIDX_INVALID;
		}

		stio->tmr.jobs = tmp;
		stio->tmr.capa = new_capa;
	}

	stio->tmr.size = stio->tmr.size + 1;
	stio->tmr.jobs[index] = *job;
	if (stio->tmr.jobs[index].idxptr) *stio->tmr.jobs[index].idxptr = index;
	return sift_up (stio, index, 0);
}

stio_tmridx_t stio_updtmrjob (stio_t* stio, stio_tmridx_t index, const stio_tmrjob_t* job)
{
	stio_tmrjob_t item;
	item = stio->tmr.jobs[index];
	stio->tmr.jobs[index] = *job;
	if (stio->tmr.jobs[index].idxptr) *stio->tmr.jobs[index].idxptr = index;
	return YOUNGER_THAN(job, &item)? sift_up (stio, index, 0): sift_down (stio, index, 0);
}

void stio_firetmrjobs (stio_t* stio, const stio_ntime_t* tm, stio_size_t* firecnt)
{
	stio_ntime_t now;
	stio_tmrjob_t tmrjob;
	stio_size_t count = 0;

	/* if the current time is not specified, get it from the system */
	if (tm) now = *tm;
	else stio_gettime (&now);

	while (stio->tmr.size > 0)
	{
		if (stio_cmptime(&stio->tmr.jobs[0].when, &now) > 0) break;

		tmrjob = stio->tmr.jobs[0]; /* copy the scheduled job */
		stio_deltmrjob (stio, 0); /* deschedule the job */

		count++;
		tmrjob.handler (stio, &now, &tmrjob); /* then fire the job */
	}

	if (firecnt) *firecnt = count;
}

int stio_gettmrtmout (stio_t* stio, const stio_ntime_t* tm, stio_ntime_t* tmout)
{
	stio_ntime_t now;

	/* time-out can't be calculated when there's no job scheduled */
	if (stio->tmr.size <= 0) 
	{
		stio->errnum = STIO_ENOENT;
		return -1;
	}

	/* if the current time is not specified, get it from the system */
	if (tm) now = *tm;
	else stio_gettime (&now);

	stio_subtime (&stio->tmr.jobs[0].when, &now, tmout);
	if (tmout->sec < 0) stio_cleartime (tmout);

	return 0;
}

stio_tmrjob_t* stio_gettmrjob (stio_t* stio, stio_tmridx_t index)
{
	return (index < 0 || index >= stio->tmr.size)? STIO_NULL: &stio->tmr.jobs[index];
}
