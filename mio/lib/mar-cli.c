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

#include <mio-mar.h>

#include <mariadb/mysql.h>

struct mio_svc_marc_t
{
	MIO_SVC_HEADER;
};

mio_svc_marc_t* mio_svc_marc_start (mio_t* mio)
{
	mio_svc_marc_t* marc = MIO_NULL;


	marc = (mio_svc_marc_t*)mio_callocmem(mio, MIO_SIZEOF(*marc));
	if (MIO_UNLIKELY(!marc)) goto oops;

	marc->mio = mio;	
	marc->svc_stop = mio_svc_marc_stop;

	MIO_SVCL_APPEND_SVC (&mio->actsvc, (mio_svc_t*)marc);
	return marc;

oops:
	if (marc)
	{
		mio_freemem (mio, marc);
	}
	return MIO_NULL;
}

void mio_svc_marc_stop (mio_svc_marc_t* marc)
{
	mio_t* mio = marc->mio;

	MIO_SVCL_UNLINK_SVC (marc);
	mio_freemem (mio, marc);
}
