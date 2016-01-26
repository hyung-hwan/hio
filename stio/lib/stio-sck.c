#include "stio-prv.h"
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>

/* ------------------------------------------------------------------------ */
void stio_closeasyncsck (stio_sckhnd_t sck)
{
#if defined(_WIN32)
	closesocket (sck);
#else
	close (sck);
#endif
}

int stio_makesckasync (stio_sckhnd_t sck)
{
	int flags;

	if ((flags = fcntl (sck, F_GETFL)) <= -1 ||
	    (flags = fcntl (sck, F_SETFL, flags | O_NONBLOCK)) <= -1)
	{
		/* stio_seterrnum (dev->stio, STIO_ESYSERR); or translate errno to stio errnum */
		return -1;
	}

	return 0;
}

stio_sckhnd_t stio_openasyncsck (int domain, int type)
{
	stio_sckhnd_t sck;

#if defined(_WIN32)
	sck = WSASocket (domain, type, 0, NULL, 0, WSA_FLAG_OVERLAPPED /*| WSA_FLAG_NO_HANDLE_INHERIT*/);
	if (sck == STIO_SCKHND_INVALID) 
	{
		/* stio_seterrnum (dev->stio, STIO_ESYSERR); or translate errno to stio errnum */
		return STIO_SCKHND_INVALID;
	}
#else
	sck = socket (domain, type, 0); /* NO CLOEXEC or somethign */
	if (sck == STIO_SCKHND_INVALID) 
	{
		/* stio_seterrnum (dev->stio, STIO_ESYSERR); or translate errno to stio errnum */
		return STIO_SCKHND_INVALID;
	}

	if (stio_makesckasync (sck) <= -1)
	{
		close (sck);
		return STIO_SCKHND_INVALID;
	}
#endif

	return sck;
}

