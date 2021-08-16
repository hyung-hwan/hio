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

#include <hio-dhcp.h>
#include <hio-sck.h>
#include "hio-prv.h"

struct hio_svc_dhcs_t
{
	HIO_SVC_HEADER;

	hio_dev_sck_t* sck;
};


/*                         
                   binding address         link address for relay.
lo                 ip address of lo        unspec
lo/::1,            [::1]:547               
lo/[::1]:544       [::1]:544                           
lo/[::1]:544/xxx   [::1]:544               xxx

join multicast group??
*/

struct hio_svc_dhcs_cfg_t
{
	hio_skad_t bind_addr; /* if this is link local? */
	hio_skad_t link_addr; /* want to use a different link address for relay? << if the peer address is link local, need to set this*/
	const hio_bch_t* ifce_id; /* want to set Interface-ID? */
};

typedef struct hio_svc_dhcs_cfg_t hio_svc_dhcs_cfg_t;

static hio_dev_sck_t* open_socket (hio_t* hio, hio_svc_dhcs_cfg_t* cfg)
{
	hio_dev_sck_make_t m;
	hio_dev_sck_t* sck = HIO_NULL;
	int f;

	f = hio_skad_family(&cfg->bind_addr);
	if (f != HIO_AF_INET || f != HIO_AF_INET6) 
	{
		hio_seterrbfmt (hio, HIO_EINVAL, "invalid bind address family");
		goto oops;
	}

	HIO_MEMSET (&m, 0, HIO_SIZEOF(m));
	m.type = HIO_DEV_SCK_UDP6;
	m.options = HIO_DEV_SCK_BIND_REUSEADDR | HIO_DEV_SCK_BIND_REUSEPORT | HIO_DEV_SCK_BIND_IGNERR;
	//m.on_write = 
	//m.on_read = ...
	//m.on_connect = ...
	//m.on_disconnect = ...
	sck = hio_dev_sck_make(hio, 0, &m);
	if (HIO_UNLIKELY(!sck)) goto oops;


#if 0
	if (hio_dev_sck_joingroup(sck, mcast_addr, ifindex) <= -1) goto oops;
#endif


	return sck;

oops:
	if (sck) hio_dev_sck_kill (sck);
	return HIO_NULL;
}

hio_svc_dhcs_t* hio_svc_dhcs_start (hio_t* hio, const hio_skad_t* local_binds, hio_oow_t local_nbinds)
{
	hio_svc_dhcs_t* dhcs;
	union
	{
		hio_dev_sck_make_t m;
		hio_dev_sck_listen_t l;
	} info;
	hio_oow_t i;

	dhcs = (hio_svc_dhcs_t*)hio_callocmem(hio, HIO_SIZEOF(*dhcs));
	if (HIO_UNLIKELY(!dhcs)) goto oops;

	dhcs->hio = hio;


	for (i = 0; i < local_nbinds; i++)
	{
		HIO_MEMSET (&info, 0, HIO_SIZEOF(info));
		info.m.type = HIO_DEV_SCK_UDP6;
		info.m.options = HIO_DEV_SCK_BIND_REUSEADDR | HIO_DEV_SCK_BIND_REUSEPORT | HIO_DEV_SCK_BIND_IGNERR;
		//info.m.on_write = 
		//info.m.on_read = ...
		//info.m.on_connect = ...
		//info.m.on_disconnect = ...
		dhcs->sck = hio_dev_sck_make(hio, 0, &info.m);
		if (HIO_UNLIKELY(!dhcs->sck)) goto oops;

	#if defined(IPV6_RECVPKTINFO)
		hio_dev_sck_setsockopt (dhcs->sck, IPPROTO_IPV6, IPV6_RECVPKTINFO, &v);
	#elif defined(IPV6_PKTINFO)
		hio_dev_sck_setsockopt (dhcs->sck, IPPROTO_IPV6, IPV6_PKTINFO, &v);
	#else
	//#	error no ipv6 pktinfo
	#endif

		hio_dev_sck_bind(dhcs->sck, &local_binds[i]);
	}

	HIO_SVCL_APPEND_SVC (&hio->actsvc, (hio_svc_t*)dhcs);
	return dhcs;

oops:
	if (dhcs)
	{
		if (dhcs->sck) hio_dev_sck_kill (dhcs->sck);
		hio_freemem (hio, dhcs);
	}
	return HIO_NULL;
}
