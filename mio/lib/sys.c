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

#include "mio-sys.h"

int mio_sys_init (mio_t* mio)
{
	int log_inited = 0;
	int mux_inited = 0;

	mio->sysdep = (mio_sys_t*)mio_callocmem(mio, MIO_SIZEOF(*mio->sysdep));
	if (!mio->sysdep) return -1;

	if (mio_sys_initlog(mio) <= -1) goto oops;
	log_inited = 1;

	if (mio_sys_initmux(mio) <= -1) goto oops;
	mux_inited = 1;

	return 0;

oops:
	if (mux_inited) mio_sys_finimux (mio);
	if (log_inited) mio_sys_finilog (mio);
	if (mio->sysdep) 
	{
		mio_freemem (mio, mio->sysdep);
		mio->sysdep = MIO_NULL;
	}
	return -1;
}

void mio_sys_fini (mio_t* mio)
{
	mio_sys_finimux (mio);
	mio_sys_finilog (mio);

	mio_freemem (mio, mio->sysdep);
	mio->sysdep = MIO_NULL;
}
