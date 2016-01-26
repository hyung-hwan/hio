#ifndef _STIO_PRV_H_
#define _STIO_PRV_H_

#include "stio.h"
#include <sys/epoll.h>

struct stio_t
{
	stio_mmgr_t* mmgr;
	stio_errnum_t errnum;
	int stopreq;  /* stop request to abort stio_loop() */

	struct
	{
		stio_dev_t* head;
		stio_dev_t* tail;
	} dev;

	stio_uint8_t bigbuf[65535]; /* make this dynamic depending on devices added. device may indicate a buffer size required??? */


#if defined(_WIN32)
	HANDLE iocp;
#else
	int mux;
	struct epoll_event revs[100];
#endif
};


#ifdef __cplusplus
extern "C" {
#endif

stio_sckhnd_t stio_openasyncsck (int domain, int type);
void stio_closeasyncsck (stio_sckhnd_t sck);
int stio_makesckasync (stio_sckhnd_t sck);

#ifdef __cplusplus
}
#endif


#endif
