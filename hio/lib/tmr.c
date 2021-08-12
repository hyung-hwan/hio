/*
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


#include "hio-prv.h"

#define HEAP_PARENT(x) (((x) - 1) / 2)
#define HEAP_LEFT(x)   ((x) * 2 + 1)
#define HEAP_RIGHT(x)  ((x) * 2 + 2)

#define YOUNGER_THAN(x,y) (HIO_CMP_NTIME(&(x)->when, &(y)->when) < 0)

void hio_cleartmrjobs (hio_t* hio)
{
	while (hio->tmr.size > 0) hio_deltmrjob (hio, 0);
}

static hio_tmridx_t sift_up (hio_t* hio, hio_tmridx_t index)
{
	hio_tmridx_t parent;

	parent = HEAP_PARENT(index);
	if (index > 0 && YOUNGER_THAN(&hio->tmr.jobs[index], &hio->tmr.jobs[parent]))
	{
		hio_tmrjob_t item;

		item = hio->tmr.jobs[index]; 

		do
		{
			/* move down the parent to my current position */
			hio->tmr.jobs[index] = hio->tmr.jobs[parent];
			if (hio->tmr.jobs[index].idxptr) *hio->tmr.jobs[index].idxptr = index;

			/* traverse up */
			index = parent;
			parent = HEAP_PARENT(parent);
		}
		while (index > 0 && YOUNGER_THAN(&item, &hio->tmr.jobs[parent]));

		hio->tmr.jobs[index] = item;
		if (hio->tmr.jobs[index].idxptr) *hio->tmr.jobs[index].idxptr = index;
	}

	return index;
}

static hio_tmridx_t sift_down (hio_t* hio, hio_tmridx_t index)
{
	hio_oow_t base = hio->tmr.size / 2;

	if (index < base) /* at least 1 child is under the 'index' position */
	{
		hio_tmrjob_t item;

		item = hio->tmr.jobs[index];

		do
		{
			hio_tmridx_t left, right, younger;

			left = HEAP_LEFT(index);
			right = HEAP_RIGHT(index);

			if (right < hio->tmr.size && YOUNGER_THAN(&hio->tmr.jobs[right], &hio->tmr.jobs[left]))
			{
				younger = right;
			}
			else
			{
				younger = left;
			}

			if (YOUNGER_THAN(&item, &hio->tmr.jobs[younger])) break;

			hio->tmr.jobs[index] = hio->tmr.jobs[younger];
			if (hio->tmr.jobs[index].idxptr) *hio->tmr.jobs[index].idxptr = index;

			index = younger;
		}
		while (index < base);
		
		hio->tmr.jobs[index] = item;
		if (hio->tmr.jobs[index].idxptr) *hio->tmr.jobs[index].idxptr = index;
	}

	return index;
}

void hio_deltmrjob (hio_t* hio, hio_tmridx_t index)
{
	hio_tmrjob_t item;

	HIO_ASSERT (hio, index < hio->tmr.size);

	item = hio->tmr.jobs[index];
	if (hio->tmr.jobs[index].idxptr) *hio->tmr.jobs[index].idxptr = HIO_TMRIDX_INVALID;

	hio->tmr.size = hio->tmr.size - 1;
	if (hio->tmr.size > 0 && index != hio->tmr.size)
	{
		hio->tmr.jobs[index] = hio->tmr.jobs[hio->tmr.size];
		if (hio->tmr.jobs[index].idxptr) *hio->tmr.jobs[index].idxptr = index;
		YOUNGER_THAN(&hio->tmr.jobs[index], &item)? sift_up(hio, index): sift_down(hio, index);
	}
}

hio_tmridx_t hio_instmrjob (hio_t* hio, const hio_tmrjob_t* job)
{
	hio_tmridx_t index = hio->tmr.size;

	if (index >= hio->tmr.capa)
	{
		hio_tmrjob_t* tmp;
		hio_oow_t new_capa;

		HIO_ASSERT (hio, hio->tmr.capa >= 1);
		new_capa = hio->tmr.capa * 2;
		tmp = (hio_tmrjob_t*)hio_reallocmem(hio, hio->tmr.jobs, new_capa * HIO_SIZEOF(*tmp));
		if (!tmp) return HIO_TMRIDX_INVALID;

		hio->tmr.jobs = tmp;
		hio->tmr.capa = new_capa;
	}

	hio->tmr.size = hio->tmr.size + 1;
	hio->tmr.jobs[index] = *job;
	if (hio->tmr.jobs[index].idxptr) *hio->tmr.jobs[index].idxptr = index;
	return sift_up(hio, index);
}

hio_tmridx_t hio_updtmrjob (hio_t* hio, hio_tmridx_t index, const hio_tmrjob_t* job)
{
	hio_tmrjob_t item;
	item = hio->tmr.jobs[index];
	hio->tmr.jobs[index] = *job;
	if (hio->tmr.jobs[index].idxptr) *hio->tmr.jobs[index].idxptr = index;
	return YOUNGER_THAN(job, &item)? sift_up(hio, index): sift_down(hio, index);
}

void hio_firetmrjobs (hio_t* hio, const hio_ntime_t* tm, hio_oow_t* firecnt)
{
	hio_ntime_t now;
	hio_tmrjob_t tmrjob;
	hio_oow_t count = 0;

	/* if the current time is not specified, get it from the system */
	if (tm) now = *tm;
	else hio_gettime (hio, &now);

	while (hio->tmr.size > 0)
	{
		if (HIO_CMP_NTIME(&hio->tmr.jobs[0].when, &now) > 0) break;

		tmrjob = hio->tmr.jobs[0]; /* copy the scheduled job */
		hio_deltmrjob (hio, 0); /* deschedule the job */

		count++;
		tmrjob.handler (hio, &now, &tmrjob); /* then fire the job */
	}

	if (firecnt) *firecnt = count;
}

int hio_gettmrtmout (hio_t* hio, const hio_ntime_t* tm, hio_ntime_t* tmout)
{
	hio_ntime_t now;

	/* time-out can't be calculated when there's no job scheduled */
	if (hio->tmr.size <= 0) return 0; /* no scheduled job */

	/* if the current time is not specified, get it from the system */
	if (tm) now = *tm;
	else hio_gettime (hio, &now);

	HIO_SUB_NTIME (tmout, &hio->tmr.jobs[0].when, &now);
	if (tmout->sec < 0) HIO_CLEAR_NTIME (tmout);
	return 1; /* tmout is set */
}

hio_tmrjob_t* hio_gettmrjob (hio_t* hio, hio_tmridx_t index)
{
	if (index < 0 || index >= hio->tmr.size)
	{
		hio_seterrbfmt (hio, HIO_ENOENT, "unable to get timer job as the given index is out of range");
		return HIO_NULL;
	}

	return &hio->tmr.jobs[index];
}

int hio_gettmrjobdeadline (hio_t* hio, hio_tmridx_t index, hio_ntime_t* deadline)
{
	if (index < 0 || index >= hio->tmr.size)
	{
		hio_seterrbfmt (hio, HIO_ENOENT, "unable to get timer job deadline as the given index is out of range");
		return -1;
	}

	*deadline = hio->tmr.jobs[index].when;
	return 0;
}

int hio_schedtmrjobat (hio_t* hio, const hio_ntime_t* fire_at, hio_tmrjob_handler_t handler, hio_tmridx_t* tmridx, void* ctx)
{
	hio_tmrjob_t tmrjob;

	HIO_MEMSET (&tmrjob, 0, HIO_SIZEOF(tmrjob));
	tmrjob.ctx = ctx;
	if (fire_at) tmrjob.when = *fire_at;

	tmrjob.handler = handler;
	tmrjob.idxptr = tmridx;

	return hio_instmrjob(hio, &tmrjob) == HIO_TMRIDX_INVALID? -1: 0;
}

int hio_schedtmrjobafter (hio_t* hio, const hio_ntime_t* fire_after, hio_tmrjob_handler_t handler, hio_tmridx_t* tmridx, void* ctx)
{
	hio_ntime_t fire_at;

	HIO_ASSERT (hio, HIO_IS_POS_NTIME(fire_after));

	hio_gettime (hio, &fire_at);
	HIO_ADD_NTIME (&fire_at, &fire_at, fire_after);

	return hio_schedtmrjobat(hio, &fire_at, handler, tmridx, ctx);
}
