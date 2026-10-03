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

#include "httpc-prv.h"


static int connect_to_servers (hio_svc_httpc_t httpc, hio_svc_httpc_conn_t* conns, hio_oow_t nconns)
{
	hio_oow_t i;

	for (i = 0; i < nconns; i++)
	{
	}

	return -1;
}

hio_svc_httpc_t* hio_svc_httpc_start (hio_t* hio, hio_oow_t xtnsize, hio_svc_httpc_conn_t* conns, hio_oow_t nconns)
{
	hio_svc_httpc_t* httpc = HIO_NULL;

	if (HIO_UNLIKELY(nconns <= 0))
	{
		hio_seterrnum(hio, HIO_EINVAL);
		goto oops;
	}

	httpc = (hio_svc_httpc_t*)hio_callocmem(hio, HIO_SIZEOF(*httpc) + xtnsize);
	if (HIO_UNLIKELY(!httpc)) goto oops;

	HIO_DEBUG1(hio, "HTTPC - STARTING SERVICE %p\n", httpc);

	httpc->hio = hio;
	httpc->svc_stop = (hio_svc_stop_t)hio_svc_httpc_stop;
/*
	httpc->proc_req = proc_req;
	httpc->idle_tmridx = HIO_TMRIDX_INVALID;
	HIO_INIT_NTIME(&httpc->option.cli_idle_tmout, HIO_SVC_HTTPC_DFL_CLIENT_IDLE_TMOUT, 0);
	HIO_INIT_NTIME(&httpc->option.cli_hdr_tmout, HIO_SVC_HTTPC_DFL_CLIENT_HDR_TMOUT, 0);

	httpc->option.task_max = HIO_TYPE_MAX(hio_oow_t);
	httpc->option.task_cgi_max = HIO_TYPE_MAX(hio_oow_t);

	httpc->becbuf = hio_becs_open(hio, 0, 256);
	if (HIO_UNLIKELY(!httpc->becbuf)) goto oops;

	httpc->l.sck = (hio_dev_sck_t**)hio_callocmem(hio, HIO_SIZEOF(*httpc->l.sck) * nconns);
	if (HIO_UNLIKELY(!httpc->l.sck)) goto oops;
	httpc->l.count = nconns;
*/

	return httpc;

oops:
	return HIO_NULL;
}

void hio_svc_httpc_stop (hio_svc_httpc_t* httpc)
{
}
