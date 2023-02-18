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

#define HIO_FCGI_VERSION (1)

#define HIO_FCGI_PADDING_SIZE 255
#define HIO_FCGI_RECORD_SIZE HIO_SIZEOF(hio_fcgi_record_header_t) + FCGI_CONTENT_SIZE + FCGI_PADDING_SIZE)

enum hio_fcgi_req_type_t
{
	HIO_FCGI_BEGIN_REQUEST      = 1,
	HIO_FCGI_ABORT_REQUEST      = 2,
	HIO_FCGI_END_REQUEST        = 3,
	HIO_FCGI_PARAMS             = 4,
	HIO_FCGI_STDIN              = 5,
	HIO_FCGI_STDOUT             = 6,
	HIO_FCGI_STDERR             = 7,
	HIO_FCGI_DATA               = 8,
	HIO_FCGI_GET_VALUES         = 9,
	HIO_FCGI_GET_VALUES_RESULT  = 10,
	HIO_FCGI_UNKNOWN_TYPE       = 11,
	HIO_FCGI_MAXTYPE            = (HIO_FCGI_UNKNOWN_TYPE)
};
typedef enum hio_fcgi_req_type_t hio_fcgi_req_type_t;

/* role in fcgi_begin_request_body */
enum hio_fcgi_role_t
{
	HIO_FCGI_ROLE_RESPONDER  = 1,
	HIO_FCGI_ROLE_AUTHORIZER = 2,
	HIO_FCGI_ROLE_FILTER     = 3,
};
typedef enum hio_fcgi_role_t hio_fcgi_role_t;


/* flag in fcgi_begin_request_body */
#define HIO_FCGI_KEEP_CONN  1

/* proto in fcgi_end_request_body */
#define HIO_FCGI_REQUEST_COMPLETE 0
#define HIO_FCGI_CANT_MPX_CONN    1
#define HIO_FCGI_OVERLOADED       2
#define HIO_FCGI_UNKNOWN_ROLE     3

#include "hio-pac1.h"
struct hio_fcgi_record_header_t
{
	hio_uint8_t   version;
	hio_uint8_t   type;
	hio_uint16_t  id;
	hio_uint16_t  content_len;
	hio_uint8_t   padding_len;
	hio_uint8_t   reserved;
	/* content data of the record 'type'*/
	/* padding data ... */
};
typedef struct hio_fcgi_record_header_t hio_fcgi_record_header_t;

struct hio_fcgi_begin_request_body_t
{
	hio_uint16_t  role;
	hio_uint8_t   flags;
	hio_uint8_t   reserved[5];
};
typedef struct hio_fcgi_begin_request_body_t hio_fcgi_begin_request_body_t;

struct hio_fcgi_end_request_body_t
{
	hio_uint32_t app_status;
	hio_uint8_t proto_status;
	hio_uint8_t reserved[3];
};
typedef struct hio_fcgi_end_request_body_t hio_fcgi_end_request_body_t;
#include "hio-upac.h"

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
typedef struct hio_svc_fcgic_conn_t hio_svc_fcgic_conn_t;

typedef int (*hio_svc_fcgic_on_read_t) (
	hio_svc_fcgic_sess_t* sess,
	const void*           data,
	hio_iolen_t           dlen,
	void*                 ctx
);

typedef void (*hio_svc_fcgic_on_untie_t) (
	hio_svc_fcgic_sess_t* sess,
	void*                 ctx;
);

struct hio_svc_fcgic_sess_t
{
	int active;
	hio_oow_t sid;
	hio_svc_fcgic_conn_t* conn;
	hio_svc_fcgic_on_read_t on_read;
	hio_svc_fcgic_on_untie_t on_untie;
	void* ctx;

	hio_svc_fcgic_sess_t* next;
};


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

HIO_EXPORT hio_svc_fcgic_sess_t* hio_svc_fcgic_tie (
	hio_svc_fcgic_t*         fcgic,
	const hio_skad_t*        fcgis_addr,
	hio_svc_fcgic_on_read_t  on_read,
	hio_svc_fcgic_on_untie_t on_untie,
	void*                    ctx
);

HIO_EXPORT void hio_svc_fcgic_untie (
	hio_svc_fcgic_sess_t* sess
);

HIO_EXPORT int hio_svc_fcgic_beginrequest (
   hio_svc_fcgic_sess_t* sess
);

HIO_EXPORT int hio_svc_fcgic_writeparam (
   hio_svc_fcgic_sess_t* sess,
   const void*           key,
   hio_iolen_t           ksz,
   const void*           val,
   hio_iolen_t           vsz
);

HIO_EXPORT int hio_svc_fcgic_writestdin (
   hio_svc_fcgic_sess_t* sess,
   const void*           data,
   hio_iolen_t           size
);

#if defined(__cplusplus)
}
#endif

#endif
