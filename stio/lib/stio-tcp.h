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

#ifndef _STIO_TCP_H_
#define _STIO_TCP_H_

#include <stio.h>
#include <stio-sck.h>

enum stio_dev_tcp_ioctl_cmd_t
{
	STIO_DEV_TCP_BIND, 
	STIO_DEV_TCP_CONNECT,
	STIO_DEV_TCP_LISTEN
};
typedef enum stio_dev_tcp_ioctl_cmd_t stio_dev_tcp_ioctl_cmd_t;

enum stio_dev_tcp_state_t
{
	STIO_DEV_TCP_CONNECTING = (1 << 0),
	STIO_DEV_TCP_CONNECTED  = (1 << 1),
	STIO_DEV_TCP_LISTENING  = (1 << 2),
	STIO_DEV_TCP_ACCEPTED   = (1 << 3)
};
typedef enum stio_dev_tcp_state_t stio_dev_tcp_state_t;

typedef struct stio_dev_tcp_t stio_dev_tcp_t;

typedef int (*stio_dev_tcp_on_connect_t) (stio_dev_tcp_t* dev);
typedef void (*stio_dev_tcp_on_accepted_t) (stio_dev_tcp_t* dev, stio_dev_tcp_t* clidev);
typedef void (*stio_dev_tcp_on_disconnect_t) (stio_dev_tcp_t* dev);

typedef int (*stio_dev_tcp_on_read_t) (stio_dev_tcp_t* dev, const void* data, stio_len_t len);
typedef int (*stio_dev_tcp_on_write_t) (stio_dev_tcp_t* dev, void* wrctx);

struct stio_dev_tcp_t
{
	STIO_DEV_HEADERS;

	stio_sckhnd_t sck;

	/* bitwised-ORed of #stio_dev_tcp_state_t enumerators */
	int state;

	/* peer address - valid if one of the followings is set:
	 *  STIO_DEV_TCP_ACCEPTED
	 *  STIO_DEV_TCP_CONNECTED
	 *  STIO_DEV_TCP_CONNECTING */
	stio_sckadr_t peer;

	/** return 0 on succes, -1 on failure/
	 *  called on a new tcp device for an accepted client or
	 *         on a tcp device conntected to a remote server */
	stio_dev_tcp_on_connect_t on_connect;

	stio_dev_tcp_on_disconnect_t on_disconnect;
	stio_dev_tcp_on_read_t on_read;
	stio_dev_tcp_on_write_t on_write;

	stio_tmridx_t tmridx_connect;
};

typedef struct stio_dev_tcp_make_t stio_dev_tcp_make_t;
struct stio_dev_tcp_make_t
{
	/* TODO: options: REUSEADDR for binding?? */
	stio_sckadr_t addr; /* binding address. */
	stio_dev_tcp_on_write_t on_write;
	stio_dev_tcp_on_read_t on_read;
};

typedef struct stio_dev_tcp_bind_t stio_dev_tcp_bind_t;
struct stio_dev_tcp_bind_t
{
	int opts; /* TODO: REUSEADDR , TRANSPARENT, etc  or someting?? */
	stio_sckadr_t addr;
	/* TODO: add device name for BIND_TO_DEVICE */
};

typedef struct stio_dev_tcp_connect_t stio_dev_tcp_connect_t;
struct stio_dev_tcp_connect_t
{
	stio_sckadr_t addr;
	stio_ntime_t timeout; /* connect timeout */
	stio_dev_tcp_on_connect_t on_connect;
	stio_dev_tcp_on_disconnect_t on_disconnect;
};

typedef struct stio_dev_tcp_listen_t stio_dev_tcp_listen_t;
struct stio_dev_tcp_listen_t
{
	int backlogs;
	stio_dev_tcp_on_connect_t on_connect; /* optional, but new connections are dropped immediately without this */
	stio_dev_tcp_on_disconnect_t on_disconnect; /* should on_discconneted be part of on_accept_t??? */
};

typedef struct stio_dev_tcp_accept_t stio_dev_tcp_accept_t;
struct stio_dev_tcp_accept_t
{
	stio_syshnd_t   sck;
/* TODO: add timeout */
	stio_sckadr_t   peer;
};

#ifdef __cplusplus
extern "C" {
#endif

STIO_EXPORT  stio_dev_tcp_t* stio_dev_tcp_make (
	stio_t*                    stio,
	stio_size_t                xtnsize,
	const stio_dev_tcp_make_t* data
);

STIO_EXPORT int stio_dev_tcp_bind (
	stio_dev_tcp_t*         tcp,
	stio_dev_tcp_bind_t*    bind
);

STIO_EXPORT int stio_dev_tcp_connect (
	stio_dev_tcp_t*         tcp,
	stio_dev_tcp_connect_t* conn);

STIO_EXPORT int stio_dev_tcp_listen (
	stio_dev_tcp_t*         tcp,
	stio_dev_tcp_listen_t*  lstn
);

STIO_EXPORT int stio_dev_tcp_write (
	stio_dev_tcp_t*  tcp,
	const void*      data,
	stio_len_t       len,
	void*            wrctx
);

STIO_EXPORT int stio_dev_tcp_halt (
	stio_dev_tcp_t* tcp
);

#ifdef __cplusplus
}
#endif

#endif
