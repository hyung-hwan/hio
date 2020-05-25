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
#include "http-prv.h"
#include <mio-thr.h>
#include <mio-fmt.h>
#include <mio-chr.h>

#include <pthread.h>

struct thr_state_t
{
	MIO_SVC_HTTS_RSRC_HEADER;
	pthread_t thr;
	mio_svc_htts_thr_func_t func;
	mio_dev_t* p1; /* change it to pipe */
};

typedef struct thr_state_t thr_state_t;

static void thr_state_on_kill (thr_state_t* thr_state)
{
}

static void* thr_state_run_func (void* arg)
{
	thr_state_t* thr_state = (thr_state_t*)arg;

//	thr_state->func (thr_state->XXX, thr_state->p1->fd); /* pass the pointer to data that can be accessed safely inside a thread. mostly these must be duplicates of core data */
	return MIO_NULL;
}

int mio_svc_htts_dothr (mio_svc_htts_t* htts, mio_dev_sck_t* csck, mio_htre_t* req, mio_svc_htts_thr_func_t func)
{
	/*mio_t* mio = htts->mio;*/
//	mio_svc_htts_cli_t* cli = mio_dev_sck_getxtn(csck);
	thr_state_t* thr_state = MIO_NULL;

	thr_state = (thr_state_t*)mio_svc_htts_rsrc_make(htts, MIO_SIZEOF(*thr_state), thr_state_on_kill);
	if (MIO_UNLIKELY(!thr_state)) goto oops;

	thr_state->func = func;

#if 0
	thr_state->p1 = mio_dev_pipe_make(mio);
	if (MIO_UNLIKELY(!thr_state->p1)) goto oops;
#endif

	if (pthread_create(&thr_state->thr, MIO_NULL, thr_state_run_func, thr_state) != 0)
	{
	}

	return 0;

oops:
	//if (thr_state) thr_state_halt_particiapting_devices (thr_state);
	return -1;
}

