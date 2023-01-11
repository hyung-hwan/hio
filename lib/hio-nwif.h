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

#ifndef _HIO_NWIF_H_
#define _HIO_NWIF_H_

#include <hio.h>
#include <hio-skad.h>


typedef struct hio_ifcfg_t hio_ifcfg_t;

enum hio_ifcfg_flag_t
{
	HIO_IFCFG_UP       = (1 << 0),
	HIO_IFCFG_RUNNING  = (1 << 1),
	HIO_IFCFG_BCAST    = (1 << 2),
	HIO_IFCFG_PTOP     = (1 << 3), /* peer to peer */
	HIO_IFCFG_LINKUP   = (1 << 4),
	HIO_IFCFG_LINKDOWN = (1 << 5)
};

enum hio_ifcfg_type_t
{
	HIO_IFCFG_IN4 = HIO_AF_INET,
	HIO_IFCFG_IN6 = HIO_AF_INET6
};

typedef enum hio_ifcfg_type_t hio_ifcfg_type_t;
struct hio_ifcfg_t
{
	hio_ifcfg_type_t type;     /* in */
	hio_ooch_t       name[64]; /* in/out */
	unsigned int      index;    /* in/out */

	/* ---------------- */
	int               flags;    /* out */
	int               mtu;      /* out */

	hio_skad_t       addr;     /* out */
	hio_skad_t       mask;     /* out */
	hio_skad_t       ptop;     /* out */
	hio_skad_t       bcast;    /* out */

	/* ---------------- */

	/* TODO: add hwaddr?? */
	/* i support ethernet only currently */
	hio_uint8_t      ethw[6];  /* out */
};


#if defined(__cplusplus)
extern "C" {
#endif

HIO_EXPORT int hio_bcstrtoifindex (
	hio_t*            hio,
	const hio_bch_t*  ptr,
	unsigned int*     index
);

HIO_EXPORT int hio_bcharstoifindex (
	hio_t*            hio,
	const hio_bch_t*  ptr,
	hio_oow_t         len,
	unsigned int*     index
);

HIO_EXPORT int hio_ucstrtoifindex (
	hio_t*            hio,
	const hio_uch_t*  ptr,
	unsigned int*     index
);

HIO_EXPORT int hio_ucharstoifindex (
	hio_t*            hio,
	const hio_uch_t*  ptr,
	hio_oow_t         len,
	unsigned int*     index
);

HIO_EXPORT int hio_ifindextobcstr (
	hio_t*            hio,
	unsigned int      index,
	hio_bch_t*        buf,
	hio_oow_t         len
);

HIO_EXPORT int hio_ifindextoucstr (
	hio_t*            hio,
	unsigned int      index,
	hio_uch_t*        buf,
	hio_oow_t         len
);

#if defined(HIO_OOCH_IS_UCH)
#	define hio_oocstrtoifindex hio_ucstrtoifindex
#	define hio_oocharstoifindex hio_ucharstoifindex
#	define hio_ifindextooocstr hio_ifindextoucstr
#else
#	define hio_oocstrtoifindex hio_bcstrtoifindex
#	define hio_oocharstoifindex hio_bcharstoifindex
#	define hio_ifindextooocstr hio_ifindextobcstr
#endif

HIO_EXPORT int hio_getifcfg (
	hio_t*         hio,
	hio_ifcfg_t*   cfg
);

#if defined(__cplusplus)
}
#endif

#endif
