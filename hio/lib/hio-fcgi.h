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

#ifndef _HIO_FCGI_H_
#define _HIO_FCGI_H_

#include <hio.h>
#include <hio-skad.h>
/* ---------------------------------------------------------------- */

typedef struct hio_svc_fcgis_t hio_svc_fcgis_t; /* server service */
typedef struct hio_svc_fcgic_t hio_svc_fcgic_t; /* client service */

typedef struct hio_svc_fcgic_tmout_t hio_svc_fcgic_tmout_t;
struct hio_svc_fcgic_tmout_t
{
	hio_ntime_t c;
	hio_ntime_t r;
	hio_ntime_t w;
};

/* ---------------------------------------------------------------- */

typedef struct hio_svc_fcgic_sess_t hio_svc_fcgic_sess_t;

/* ---------------------------------------------------------------- */

#if defined(__cplusplus)
extern "C" {
#endif

HIO_EXPORT hio_svc_fcgic_t* hio_svc_fcgic_start (
	hio_t* hio,
	const hio_svc_fcgic_tmout_t* tmout
);

HIO_EXPORT void hio_svc_fcgic_stop (
	hio_svc_fcgic_t* fcgic
);

#if defined(HIO_HAVE_INLINE)
static HIO_INLINE hio_t* hio_svc_fcgis_gethio(hio_svc_fcgis_t* svc) { return hio_svc_gethio((hio_svc_t*)svc); }
static HIO_INLINE hio_t* hio_svc_fcgic_gethio(hio_svc_fcgic_t* svc) { return hio_svc_gethio((hio_svc_t*)svc); }
#else
#	define hio_svc_fcgis_gethio(svc) hio_svc_gethio(svc)
#	define hio_svc_fcgic_gethio(svc) hio_svc_gethio(svc)
#endif

#if defined(__cplusplus)
}
#endif

#endif
