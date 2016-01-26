#ifndef _STIO_H_
#define _STIO_H_

/* TODO: remove these headers */
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>


typedef signed char stio_int8_t;
typedef unsigned char stio_uint8_t;
typedef unsigned long stio_size_t;
#define STIO_MEMSET(dst,byte,count) memset(dst,byte,count)
#define STIO_ASSERT assert


#define STIO_SIZEOF(x) sizeof(x)
#define STIO_COUNTOF(x) (sizeof(x) / sizeof(x[0]))
#define STIO_NULL ((void*)0)

typedef struct stio_mmgr_t stio_mmgr_t;

typedef void* (*stio_mmgr_alloc_t) (stio_mmgr_t* mmgr, stio_size_t size);
typedef void (*stio_mmgr_free_t) (stio_mmgr_t* mmgr, void* ptr);

struct stio_mmgr_t
{
	stio_mmgr_alloc_t alloc;	
	stio_mmgr_free_t free;
	void* ctx;
};

#define STIO_MMGR_ALLOC(mmgr,size) (mmgr)->alloc(mmgr,size)
#define STIO_MMGR_FREE(mmgr,ptr) (mmgr)->free(mmgr,ptr)

struct stio_sckadr_t
{
	int family;
	stio_uint8_t data[64]; /* TODO: use the actual sockaddr size */
};

typedef struct stio_sckadr_t stio_sckadr_t;

#if defined(_WIN32)
#	define STIO_IOCP_KEY 1

	typedef int stio_scklen_t;
	typedef SOCKET stio_sckhnd_t;
#	define STIO_SCKHND_INVALID (INVALID_SOCKET)

	typedef HANDLE stio_syshnd_t;
#else
	typedef socklen_t stio_scklen_t;
	typedef int stio_sckhnd_t;
#	define STIO_SCKHND_INVALID (-1)

	typedef int stio_syshnd_t;
#endif



/* ------------------------------------------------------------------------- */
typedef struct stio_t stio_t;
typedef struct stio_dev_t stio_dev_t;
typedef struct stio_dev_mth_t stio_dev_mth_t;
typedef struct stio_dev_evcb_t stio_dev_evcb_t;
typedef unsigned int stio_len_t; /* TODO: remove it? */


enum stio_errnum_t
{
	STIO_ENOERR,
	STIO_ENOMEM,
	STIO_EINVAL,
	STIO_ENOSUP, /* not supported */

	STIO_EDEVMAKE
};

typedef enum stio_errnum_t stio_errnum_t;


struct stio_dev_mth_t
{
	/* --------------------------------------------------------------------------------------------- */
	int           (*make)      (stio_dev_t* dev, void* ctx); /* mandatory. called in stix_makedev() */
	void          (*kill)      (stio_dev_t* dev); /* mandatory. called in stix_killdev(). called in stix_makedev() upon failure after make() success */
	stio_syshnd_t (*getsyshnd) (stio_dev_t* dev); /* mandatory. called in stix_makedev() after successful make() */


	int           (*ioctl)     (stio_dev_t* dev, int cmd, void* arg);

	/* --------------------------------------------------------------------------------------------- */
	int           (*recv)      (stio_dev_t* dev, void* data, stio_len_t* len);
	/* --------------------------------------------------------------------------------------------- */
	int           (*send)      (stio_dev_t* dev, const void* data, stio_len_t* len);
	/* --------------------------------------------------------------------------------------------- */
};

struct stio_dev_evcb_t
{
	int           (*ready)            (stio_dev_t* dev, int events);
	/*int           (*on_error)         (stio_dev_t* dev);
	int           (*on_hangup)        (stio_dev_t* dev);*/
	int           (*on_recv)          (stio_dev_t* dev, const void* data, stio_len_t len);
	int           (*on_sent)          (stio_dev_t* dev, const void* data, stio_len_t len);
	
};

#define STIO_DEV_HEADERS \
	stio_t* stio; \
	stio_dev_mth_t* mth; \
	stio_dev_evcb_t* evcb; \
	stio_dev_t* prev; \
	stio_dev_t* next 

struct stio_dev_t
{
	STIO_DEV_HEADERS;
};

enum stio_dev_event_cmd_t
{
	STIO_DEV_EVENT_ADD,
	STIO_DEV_EVENT_MOD,
	STIO_DEV_EVENT_DEL
};
typedef enum stio_dev_event_cmd_t stio_dev_event_cmd_t;

enum stio_dev_event_flag_t
{
	STIO_DEV_EVENT_IN  = (1 << 0),
	STIO_DEV_EVENT_OUT = (1 << 1),
	STIO_DEV_EVENT_PRI = (1 << 2),
	STIO_DEV_EVENT_HUP = (1 << 3),
	STIO_DEV_EVENT_ERR = (1 << 4)
	
};
typedef enum stio_dev_event_flag_t stio_dev_event_flag_t;



/* -------------------------------------------------------------------------- */


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

typedef void (*stio_dev_tcp_on_connected_t) (stio_dev_tcp_t* dev);
typedef void (*stio_dev_tcp_on_accepted_t) (stio_dev_tcp_t* dev, stio_dev_tcp_t* clidev);
typedef void (*stio_dev_tcp_on_disconnected_t) (stio_dev_tcp_t* dev);

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

	stio_dev_tcp_on_connected_t on_connected;
	stio_dev_tcp_on_disconnected_t on_disconnected;
	stio_dev_tcp_on_accepted_t on_accepted;
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
	stio_dev_tcp_on_connected_t on_connected;
	stio_dev_tcp_on_disconnected_t on_disconnected;
};

typedef struct stio_dev_tcp_listen_t stio_dev_tcp_listen_t;
struct stio_dev_tcp_listen_t
{
	int backlogs;
	stio_dev_tcp_on_accepted_t on_accepted; /* optional, but new connections are dropped immediately without this */
	stio_dev_tcp_on_disconnected_t on_disconnected;
};

typedef struct stio_dev_tcp_accept_t stio_dev_tcp_accept_t;
struct stio_dev_tcp_accept_t
{
	stio_syshnd_t sck;
	stio_dev_tcp_t* parent;
	stio_sckadr_t peer;
};

/* -------------------------------------------------------------------------- */

typedef struct stio_dev_udp_t stio_dev_udp_t;

struct stio_dev_udp_t
{
	STIO_DEV_HEADERS;
	stio_sckhnd_t sck;

	stio_sckadr_t peer;
};

/* -------------------------------------------------------------------------- */


#ifdef __cplusplus
extern "C" {
#endif

stio_t* stio_open (
	stio_mmgr_t* mmgr,
	stio_size_t xtnsize,
	stio_errnum_t* errnum
);

void stio_close (
	stio_t* stio
);

stio_dev_t* stio_makedev (
	stio_t*          stio,
	stio_size_t      dev_size,
	stio_dev_mth_t*  dev_mth,
	stio_dev_evcb_t* dev_evcb,
	void*            make_ctx
);

void stio_killdev (
	stio_t*     stio,
	stio_dev_t* dev
);


stio_dev_tcp_t* stio_dev_tcp_make (
	stio_t*        stio,
	stio_sckadr_t* addr
);

void stio_dev_tcp_kill (
	stio_dev_tcp_t* tcp
);


stio_dev_udp_t* stio_dev_udp_make (
	stio_t*        stio,
	stio_sckadr_t* addr
);

void stio_dev_udp_kill (
	stio_dev_udp_t* udp
);

#ifdef __cplusplus
}
#endif


#endif
