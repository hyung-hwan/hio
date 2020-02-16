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

#ifndef _MIO_NWIF_H_
#define _MIO_NWIF_H_

#include <mio.h>
#include <mio-skad.h>


typedef struct mio_ifcfg_t mio_ifcfg_t;

enum mio_ifcfg_flag_t
{
	MIO_IFCFG_UP       = (1 << 0),
	MIO_IFCFG_RUNNING  = (1 << 1),
	MIO_IFCFG_BCAST    = (1 << 2),
	MIO_IFCFG_PTOP     = (1 << 3), /* peer to peer */
	MIO_IFCFG_LINKUP   = (1 << 4),
	MIO_IFCFG_LINKDOWN = (1 << 5)
};

enum mio_ifcfg_type_t
{
	MIO_IFCFG_IN4 = MIO_AF_INET,
	MIO_IFCFG_IN6 = MIO_AF_INET6
};

typedef enum mio_ifcfg_type_t mio_ifcfg_type_t;
struct mio_ifcfg_t
{
	mio_ifcfg_type_t type;     /* in */
	mio_ooch_t       name[64]; /* in/out */
	unsigned int      index;    /* in/out */

	/* ---------------- */
	int               flags;    /* out */
	int               mtu;      /* out */

	mio_skad_t       addr;     /* out */
	mio_skad_t       mask;     /* out */
	mio_skad_t       ptop;     /* out */
	mio_skad_t       bcast;    /* out */

	/* ---------------- */

	/* TODO: add hwaddr?? */	
	/* i support ethernet only currently */
	mio_uint8_t      ethw[6];  /* out */
};


#if defined(__cplusplus)
extern "C" {
#endif

MIO_EXPORT int mio_bcstrtoifindex (
	mio_t*            mio,
	const mio_bch_t*  ptr,
	unsigned int*     index
);

MIO_EXPORT int mio_bcharstoifindex (
	mio_t*            mio,
	const mio_bch_t*  ptr,
	mio_oow_t         len,
	unsigned int*     index
);

MIO_EXPORT int mio_ucstrtoifindex (
	mio_t*            mio,
	const mio_uch_t*  ptr,
	unsigned int*     index
);

MIO_EXPORT int mio_ucharstoifindex (
	mio_t*            mio,
	const mio_uch_t*  ptr,
	mio_oow_t         len,
	unsigned int*     index
);

MIO_EXPORT int mio_ifindextobcstr (
	mio_t*            mio,
	unsigned int      index,
	mio_bch_t*        buf,
	mio_oow_t         len
);

MIO_EXPORT int mio_ifindextoucstr (
	mio_t*            mio,
	unsigned int      index,
	mio_uch_t*        buf,
	mio_oow_t         len
);

#if defined(MIO_OOCH_IS_UCH)
#	define mio_oocstrtoifindex mio_ucstrtoifindex
#	define mio_oocharstoifindex mio_ucharstoifindex
#	define mio_ifindextooocstr mio_ifindextoucstr
#else
#	define mio_oocstrtoifindex mio_bcstrtoifindex
#	define mio_oocharstoifindex mio_bcharstoifindex
#	define mio_ifindextooocstr mio_ifindextobcstr
#endif

MIO_EXPORT int mio_getifcfg (
	mio_t*         mio,
	mio_ifcfg_t*   cfg
);

#if defined(__cplusplus)
}
#endif

#endif
