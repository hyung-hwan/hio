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

#include "hio-prv.h"

void hio_rco_init (hio_rco_t* obj, hio_t* hio, hio_rco_fini_t fini)
{
	obj->rco_hio = hio;
	obj->rco_refcnt = 0;
	obj->rco_fini = fini;
}

void hio_rco_ref (hio_rco_t* obj)
{
	obj->rco_refcnt++;
}

void hio_rco_unref (hio_rco_t* obj)
{
	hio_t* hio = obj->rco_hio;

	HIO_ASSERT(hio, obj->rco_refcnt > 0);

	if (--obj->rco_refcnt == 0)
	{
		/* cache hio above - the block is gone by the time we free it */
		if (obj->rco_fini) obj->rco_fini(obj);
		hio_freemem (hio, obj);
	}
}
