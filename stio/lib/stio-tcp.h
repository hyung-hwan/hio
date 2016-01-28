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

typedef int (*stio_dev_tcp_on_connected_t) (stio_dev_tcp_t* dev);
typedef void (*stio_dev_tcp_on_accepted_t) (stio_dev_tcp_t* dev, stio_dev_tcp_t* clidev);
typedef void (*stio_dev_tcp_on_disconnected_t) (stio_dev_tcp_t* dev);

typedef int (*stio_dev_tcp_on_recv_t) (stio_dev_tcp_t* dev, const void* data, stio_len_t len);
typedef int (*stio_dev_tcp_on_sent_t) (stio_dev_tcp_t* dev, void* sendctx);

struct stio_dev_tcp_t
{
	STIO_DEV_HEADERS;

	stio_sckhnd_t sck;

	unsigned int state;

	/* peer address - valid if one of the followings is set:
	 *  STIO_DEV_TCP_ACCEPTED
	 *  STIO_DEV_TCP_CONNECTED
	 *  STIO_DEV_TCP_CONNECTING */
	stio_sckadr_t peer;

	/* parent tcp device. valid if STIO_DEV_TCP_ACCEPTED is set */
	stio_dev_tcp_t* parent;

	/** return 0 on succes, -1 on failure/
	 *  called on a new tcp device for an accepted client or
	 *         on a tcp device conntected to a remote server */
	stio_dev_tcp_on_connected_t on_connected;

	stio_dev_tcp_on_disconnected_t on_disconnected;
	stio_dev_tcp_on_recv_t on_recv;
	stio_dev_tcp_on_sent_t on_sent;
};

typedef struct stio_dev_tcp_make_t stio_dev_tcp_make_t;
struct stio_dev_tcp_make_t
{
	/* TODO: options: REUSEADDR for binding?? */
	stio_sckadr_t addr; /* binding address. */
	stio_dev_tcp_on_sent_t on_sent;
	stio_dev_tcp_on_recv_t on_recv;
};

typedef struct stio_dev_tcp_bind_t stio_dev_tcp_bind_t;
struct stio_dev_tcp_bind_t
{
	int options; /* TODO: REUSEADDR , TRANSPARENT, etc  or someting?? */
	stio_sckadr_t addr;
	/* TODO: add device name for BIND_TO_DEVICE */
};

typedef struct stio_dev_tcp_connect_t stio_dev_tcp_connect_t;
struct stio_dev_tcp_connect_t
{
	stio_sckadr_t addr;
/* TODO: add timeout */
	stio_dev_tcp_on_connected_t on_connected;
	stio_dev_tcp_on_disconnected_t on_disconnected;
	/* stio_dev_tcp_on_timeout_t on_timeout; should the timeout handler be here? what about write timeout? or accept timeout? */
};

typedef struct stio_dev_tcp_listen_t stio_dev_tcp_listen_t;
struct stio_dev_tcp_listen_t
{
	int backlogs;
	stio_dev_tcp_on_connected_t on_connected; /* optional, but new connections are dropped immediately without this */
	stio_dev_tcp_on_disconnected_t on_disconnected; /* should on_discconneted be part of on_accept_t??? */
};

typedef struct stio_dev_tcp_accept_t stio_dev_tcp_accept_t;
struct stio_dev_tcp_accept_t
{
	stio_syshnd_t   sck;
/* TODO: add timeout */
	stio_dev_tcp_t* parent; /* TODO: is this really needed? */
	stio_sckadr_t   peer;
};

#ifdef __cplusplus
extern "C" {
#endif

stio_dev_tcp_t* stio_dev_tcp_make (
	stio_t*                    stio,
	stio_size_t                xtnsize,
	const stio_dev_tcp_make_t* data
);

void stio_dev_tcp_kill (
	stio_dev_tcp_t* tcp
);

int stio_dev_tcp_bind (
	stio_dev_tcp_t*         tcp,
	stio_dev_tcp_bind_t*    bind
);

int stio_dev_tcp_connect (
	stio_dev_tcp_t*         tcp,
	stio_dev_tcp_connect_t* conn);

int stio_dev_tcp_listen (
	stio_dev_tcp_t*         tcp,
	stio_dev_tcp_listen_t*  lstn
);

int stio_dev_tcp_send (
	stio_dev_tcp_t*  tcp,
	const void*      data,
	stio_len_t       len,
	void*            sendctx
);

#ifdef __cplusplus
}
#endif

#endif
