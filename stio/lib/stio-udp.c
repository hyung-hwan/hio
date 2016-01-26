#include "stio-prv.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>


static int udp_make (stio_dev_t* dev, void* ctx)
{
/* NOTE: this can be extended to use ctx to tell between INET and INET6 or other types of sockets without creating a new dev method set. */

	stio_dev_udp_t* udp = (stio_dev_udp_t*)dev;
	struct sockaddr* saddr = (struct sockaddr*)ctx;

	udp->sck = stio_openasyncsck (AF_INET, SOCK_DGRAM);
	if (udp->sck == STIO_SCKHND_INVALID) goto oops;

	if (saddr)
	{
		stio_scklen_t len;
		if (saddr->sa_family == AF_INET) 
			len = STIO_SIZEOF(struct sockaddr_in);
		else if (saddr->sa_family == AF_INET6)
			len = STIO_SIZEOF(struct sockaddr_in6);
		else	
		{
			dev->stio->errnum = STIO_EINVAL;
			goto oops;
		}


		//setsockopt (udp->sck, SOL_SOCKET, SO_REUSEADDR, ...);
		if (bind (udp->sck, saddr, len) == -1) 
		{
			//dev->stio->errnum = STIO_EINVAL; TODO:
			goto oops;
		}
	}

	return 0;

oops:
	if (udp->sck != STIO_SCKHND_INVALID)
	{
		stio_closeasyncsck (udp->sck);
		udp->sck = STIO_SCKHND_INVALID;
	}
	return -1;
}

static void udp_kill (stio_dev_t* dev)
{
	stio_dev_udp_t* udp = (stio_dev_udp_t*)dev;
	if (udp->sck != STIO_SCKHND_INVALID) 
	{
		stio_closeasyncsck (udp->sck);
		udp->sck = STIO_SCKHND_INVALID;
	}
}

static stio_syshnd_t udp_getsyshnd (stio_dev_t* dev)
{
	stio_dev_udp_t* udp = (stio_dev_udp_t*)dev;
	return (stio_syshnd_t)udp->sck;
}

static int udp_recv (stio_dev_t* dev, void* buf, stio_len_t* len)
{
	stio_dev_udp_t* udp = (stio_dev_udp_t*)dev;
	stio_scklen_t addrlen;
	int x;

printf ("UDP RECVFROM...\n");
	addrlen = STIO_SIZEOF(udp->peer);
	x = recvfrom (udp->sck, buf, *len, 0, (struct sockaddr*)&udp->peer, &addrlen);
	if (x <= -1)
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK) return 0;  /* no data available */
		return -1;
	}

	*len = x;
	return 1;
}

static int udp_send (stio_dev_t* dev, const void* data, stio_len_t* len)
{
	stio_dev_udp_t* udp = (stio_dev_udp_t*)udp;
	ssize_t x;

#if 0
	x = sendto (udp->sck, data, *len, skad, stio_getskadlen(skad));
	if (x <= -1) 
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK) return 0;  /* no data can be written */
		return -1;
	}

/* for UDP, if the data chunk can't be written at one go, it's actually a failure */
	if (x != *len) return -1; /* TODO: can i hava an indicator for this in stio? */

	*len = x;
#endif
	return 1;
}


static int udp_ioctl (stio_dev_t* dev, int cmd, void* arg)
{
	return 0;
}


/* ------------------------------------------------------------------------ */

// -----------------------------------------------------------------

static stio_dev_mth_t udp_mth = 
{
	udp_make,
	udp_kill,
	udp_getsyshnd,

	udp_ioctl,     /* ioctl */
	udp_recv,
	udp_send
};

static int udp_ready (stio_dev_t* dev, int events)
{
	if (events & STIO_DEV_EVENT_ERR) printf ("UDP READY ERROR.....\n");
	if (events & STIO_DEV_EVENT_HUP) printf ("UDP READY HANGUP.....\n");
	if (events & STIO_DEV_EVENT_PRI) printf ("UDP READY PRI.....\n");
	if (events & STIO_DEV_EVENT_IN) printf ("UDP READY IN.....\n");
	if (events & STIO_DEV_EVENT_OUT) printf ("UDP READY OUT.....\n");

	return 0;
}

static int udp_on_recv (stio_dev_t* dev, const void* data, stio_len_t len)
{
printf ("dATA received %d bytes\n", (int)len);
	return 0;

}

static int udp_on_sent (stio_dev_t* dev, const void* data, stio_len_t len)
{
	return 0;

}

static stio_dev_evcb_t udp_evcb =
{
	udp_ready,
	udp_on_recv,
	udp_on_sent
};



stio_dev_udp_t* stio_dev_udp_make (stio_t* stio, stio_sckadr_t* addr)
{
	stio_dev_udp_t* udp;

	udp = (stio_dev_udp_t*)stio_makedev (stio, STIO_SIZEOF(*udp), &udp_mth, &udp_evcb, addr);

	return udp;
}


void stio_dev_udp_kill (stio_dev_udp_t* udp)
{
	stio_killdev (udp->stio, (stio_dev_t*)udp);
}
