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

#include <hio-nwif.h>
#include "hio-prv.h"

#if defined(_WIN32)
	/* TODO: */
#elif defined(__OS2__)
	/* TODO: */
#elif defined(__DOS__)
	/* TODO: */
#else
#	include <unistd.h>
#	include <errno.h>
#	include <sys/socket.h>
#	if defined(HAVE_SYS_IOCTL_H)
#		include <sys/ioctl.h>
#	endif
#	if defined(HAVE_NET_IF_H)
#		include <net/if.h>
#	endif
#	if defined(HAVE_SYS_SOCKIO_H)
#		include <sys/sockio.h>
#	endif
#	if !defined(IF_NAMESIZE)
#		define IF_NAMESIZE 63
#	endif
#endif

#if defined(SIOCGIFCONF) && (defined(SIOCGIFANUM) || defined(SIOCGIFNUM))
static int get_sco_ifconf (hio_t* hio, struct ifconf* ifc)
{
	/* SCO doesn't have have any IFINDEX thing.
	 * i emultate it using IFCONF */
	int h, num;
	struct ifreq* ifr;

	h = socket(AF_INET, SOCK_DGRAM, 0); 
	if (h <= -1) 
	{
		hio_seterrwithsyserr (hio, 0, errno);
		return -1;
	}

	ifc->ifc_len = 0;
	ifc->ifc_buf = HIO_NULL;

	#if defined(SIOCGIFANUM)
	if (ioctl(h, SIOCGIFANUM, &num) <= -1)
	{
		hio_seterrwithsyserr (hio, 0, errno);
		goto oops;
	}
	#else
	if (ioctl(h, SIOCGIFNUM, &num) <= -1)
	{
		hio_seterrwithsyserr (hio, 0, errno);
		goto oops;
	}
	#endif

	/* sco needs reboot when you add an network interface.
	 * it should be safe not to consider the case when the interface
	 * is added after SIOCGIFANUM above. 
	 * another thing to note is that SIOCGIFCONF ends with segfault
	 * if the buffer is not large enough unlike some other OSes
	 * like opensolaris which truncates the configuration. */

	ifc->ifc_len = num * HIO_SIZEOF(*ifr);
	ifc->ifc_buf = hio_allocmem(hio, ifc->ifc_len);
	if (ifc->ifc_buf == HIO_NULL) goto oops;

	if (ioctl(h, SIOCGIFCONF, ifc) <= -1) 
	{
		hio_seterrwithsyserr (hio, 0, errno);
		goto oops;
	}
	close (h); h = -1;

	return 0;

oops:
	if (ifc->ifc_buf) hio_freemem (hio, ifc->ifc_buf);
	if (h >= 0) close (h);
	return -1;
}

static HIO_INLINE void free_sco_ifconf (hio_t* hio, struct ifconf* ifc)
{
	hio_freemem (hio, ifc->ifc_buf);
}
#endif


int hio_bcstrtoifindex (hio_t* hio, const hio_bch_t* ptr, unsigned int* index)
{
#if defined(_WIN32)
	/* TODO: */
	hio_seterrnum (hio, HIO_ENOIMPL);
	return -1;
#elif defined(__OS2__)
	/* TODO: */
	hio_seterrnum (hio, HIO_ENOIMPL);
	return -1;
#elif defined(__DOS__)
	/* TODO: */
	hio_seterrnum (hio, HIO_ENOIMPL);
	return -1;

#elif defined(SIOCGIFINDEX)
	int h, x;
	hio_oow_t len;
	struct ifreq ifr;

	h = socket(AF_INET, SOCK_DGRAM, 0); 
	if (h <= -1)
	{
		hio_seterrwithsyserr (hio, 0, errno);
		return -1;
	}

	HIO_MEMSET (&ifr, 0, HIO_SIZEOF(ifr));
	len = hio_copy_bcstr(ifr.ifr_name, HIO_COUNTOF(ifr.ifr_name), ptr);
	if (ptr[len] != '\0') return -1; /* name too long */

	x = ioctl(h, SIOCGIFINDEX, &ifr);
	close (h);

	if (x >= 0)
	{
	#if defined(HAVE_STRUCT_IFREQ_IFR_IFINDEX)
		*index = ifr.ifr_ifindex;
	#else
		*index = ifr.ifr_index;
	#endif
	}

	return x;

#elif defined(HAVE_IF_NAMETOINDEX)
	hio_bch_t tmp[IF_NAMESIZE + 1];
	hio_oow_t len;
	unsigned int tmpidx;

	len = hio_copy_bcstr(tmp, HIO_COUNTOF(tmp), ptr);
	if (ptr[len] != '\0') return -1; /* name too long */

	tmpidx = if_nametoindex(tmp);
	if (tmpidx == 0)
	{
		hio_seterrwithsyserr (hio, 0, errno);
		return -1;
	}
	*index = tmpidx;
	return 0;

#elif defined(SIOCGIFCONF) && (defined(SIOCGIFANUM) || defined(SIOCGIFNUM))

	struct ifconf ifc;
	int num, i;

	if (get_sco_ifconf(hio, &ifc) <= -1) return -1;

	num = ifc.ifc_len / HIO_SIZEOF(struct ifreq);
	for (i = 0; i < num; i++)
	{
		if (hio_comp_bcstr(ptr, ifc.ifc_req[i].ifr_name, 0) == 0) 
		{
			free_sco_ifconf (hio, &ifc);
			*index = i + 1;
			return 0;
		}
	}

	free_sco_ifconf (hio, &ifc);
	return -1;

#else
	return -1;
#endif
}

int hio_bcharstoifindex (hio_t* hio, const hio_bch_t* ptr, hio_oow_t len, unsigned int* index)
{
#if defined(_WIN32)
	/* TODO: */
	hio_seterrnum (hio, HIO_ENOIMPL);
	return -1;
#elif defined(__OS2__)
	/* TODO: */
	hio_seterrnum (hio, HIO_ENOIMPL);
	return -1;
#elif defined(__DOS__)
	/* TODO: */
	hio_seterrnum (hio, HIO_ENOIMPL);
	return -1;

#elif defined(SIOCGIFINDEX)
	int h, x;
	struct ifreq ifr;

	h = socket(AF_INET, SOCK_DGRAM, 0); 
	if (h <= -1) 
	{
		hio_seterrwithsyserr (hio, 0, errno);
		return -1;
	}

	HIO_MEMSET (&ifr, 0, HIO_SIZEOF(ifr));
	if (hio_copy_bchars_to_bcstr(ifr.ifr_name, HIO_COUNTOF(ifr.ifr_name), ptr, len) < len) return -1; /* name too long */

	x = ioctl(h, SIOCGIFINDEX, &ifr);
	close (h);

	if (x >= 0)
	{
	#if defined(HAVE_STRUCT_IFREQ_IFR_IFINDEX)
		*index = ifr.ifr_ifindex;
	#else
		*index = ifr.ifr_index;
	#endif
	}

	return x;

#elif defined(HAVE_IF_NAMETOINDEX)
	hio_bch_t tmp[IF_NAMESIZE + 1];
	unsigned int tmpidx;

	if (hio_copy_bchars_to_bcstr(tmp, HIO_COUNTOF(tmp), ptr, len) < len) return -1;

	tmpidx = if_nametoindex(tmp);
	if (tmpidx == 0)
	{
		hio_seterrwithsyserr (hio, 0, errno);
		return -1;
	}
	*index = tmpidx;
	return 0;

#elif defined(SIOCGIFCONF) && (defined(SIOCGIFANUM) || defined(SIOCGIFNUM))
	struct ifconf ifc;
	int num, i;

	if (get_sco_ifconf(hio, &ifc) <= -1) return -1;

	num = ifc.ifc_len / HIO_SIZEOF(struct ifreq);
	for (i = 0; i < num; i++)
	{
		if (hio_comp_bchars_bcstr(ptr, len, ifc.ifc_req[i].ifr_name) == 0) 
		{
			free_sco_ifconf (hio, &ifc);
			*index = i + 1;
			return 0;
		}
	}

	free_sco_ifconf (hio, &ifc);
	return -1;

#else
	return -1;
#endif
}

int hio_ucstrtoifindex (hio_t* hio, const hio_uch_t* ptr, unsigned int* index)
{
#if defined(_WIN32)
	/* TODO: */
	hio_seterrnum (hio, HIO_ENOIMPL);
	return -1;
#elif defined(__OS2__)
	/* TODO: */
	hio_seterrnum (hio, HIO_ENOIMPL);
	return -1;
#elif defined(__DOS__)
	/* TODO: */
	hio_seterrnum (hio, HIO_ENOIMPL);
	return -1;

#elif defined(SIOCGIFINDEX)
	int h, x;
	struct ifreq ifr;
	hio_oow_t wl, ml;

	h = socket(AF_INET, SOCK_DGRAM, 0); 
	if (h <= -1) 
	{
		hio_seterrwithsyserr (hio, 0, errno);
		return -1;
	}

	ml = HIO_COUNTOF(ifr.ifr_name);
	if (hio_convutobcstr(hio, ptr, &wl, ifr.ifr_name, &ml) <= -1) return -1;

	x = ioctl(h, SIOCGIFINDEX, &ifr);
	close (h);

	if (x >= 0)
	{
	#if defined(HAVE_STRUCT_IFREQ_IFR_IFINDEX)
		*index = ifr.ifr_ifindex;
	#else
		*index = ifr.ifr_index;
	#endif
	}

	return x;

#elif defined(HAVE_IF_NAMETOINDEX)
	hio_bch_t tmp[IF_NAMESIZE + 1];
	hio_oow_t wl, ml;
	unsigned int tmpidx;

	ml = HIO_COUNTOF(tmp);
	if (hio_convutobcstr(hio, ptr, &wl, tmp, &ml) <= -1) return -1;

	tmpidx = if_nametoindex(tmp);
	if (tmpidx == 0) 
	{
		hio_seterrwithsyserr (hio, 0, errno);
		return -1;
	}
	*index = tmpidx;
	return 0;

#elif defined(SIOCGIFCONF) && (defined(SIOCGIFANUM) || defined(SIOCGIFNUM))

	struct ifconf ifc;
	int num, i;
	hio_bch_t tmp[IF_NAMESIZE + 1];
	hio_oow_t wl, ml;

	ml = HIO_COUNTOF(tmp);
	if (hio_convutobcstr(hio, ptr, &wl, tmp, &ml) <= -1) return -1;

	if (get_sco_ifconf(hio, &ifc) <= -1) return -1;

	num = ifc.ifc_len / HIO_SIZEOF(struct ifreq);
	for (i = 0; i < num; i++)
	{
		if (hio_comp_bcstr(tmp, ifc.ifc_req[i].ifr_name, 0) == 0) 
		{
			free_sco_ifconf (hio, &ifc);
			*index = i + 1;
			return 0;
		}
	}

	free_sco_ifconf (hio, &ifc);
	return -1;

#else
	return -1;
#endif
}

int hio_ucharstoifindex (hio_t* hio, const hio_uch_t* ptr, hio_oow_t len, unsigned int* index)
{
#if defined(_WIN32)
	/* TODO: */
	hio_seterrnum (hio, HIO_ENOIMPL);
	return -1;
#elif defined(__OS2__)
	/* TODO: */
	hio_seterrnum (hio, HIO_ENOIMPL);
	return -1;
#elif defined(__DOS__)
	/* TODO: */
	hio_seterrnum (hio, HIO_ENOIMPL);
	return -1;

#elif defined(SIOCGIFINDEX)
	int h, x;
	struct ifreq ifr;
	hio_oow_t wl, ml;

	h = socket(AF_INET, SOCK_DGRAM, 0); 
	if (h <= -1) 
	{
		hio_seterrwithsyserr (hio, 0, errno);
		return -1;
	}

	wl = len; ml = HIO_COUNTOF(ifr.ifr_name) - 1;
	if (hio_convutobchars(hio, ptr, &wl, ifr.ifr_name, &ml) <= -1) return -1;
	ifr.ifr_name[ml] = '\0';

	x = ioctl(h, SIOCGIFINDEX, &ifr);
	close (h);

	if (x >= 0)
	{
	#if defined(HAVE_STRUCT_IFREQ_IFR_IFINDEX)
		*index = ifr.ifr_ifindex;
	#else
		*index = ifr.ifr_index;
	#endif
	}

	return x;

#elif defined(HAVE_IF_NAMETOINDEX)
	hio_bch_t tmp[IF_NAMESIZE + 1];
	hio_oow_t wl, ml;
	unsigned int tmpidx;

	wl = len; ml = HIO_COUNTOF(tmp) - 1;
	if (hio_convutobchars(hio, ptr, &wl, tmp, &ml) <= -1) return -1;
	tmp[ml] = '\0';

	tmpidx = if_nametoindex(tmp);
	if (tmpidx == 0) 
	{
		hio_seterrwithsyserr (hio, 0, errno);
		return -1;
	}
	*index = tmpidx;
	return 0;

#elif defined(SIOCGIFCONF) && (defined(SIOCGIFANUM) || defined(SIOCGIFNUM))
	struct ifconf ifc;
	int num, i;
	hio_bch_t tmp[IF_NAMESIZE + 1];
	hio_oow_t wl, ml;

	wl = len; ml = HIO_COUNTOF(tmp) - 1;
	if (hio_convutobchars(ptr, &wl, tmp, &ml) <= -1) return -1;
	tmp[ml] = '\0';

	if (get_sco_ifconf(hio, &ifc) <= -1) return -1;

	num = ifc.ifc_len / HIO_SIZEOF(struct ifreq);
	for (i = 0; i < num; i++)
	{
		if (hio_comp_bcstr(tmp, ifc.ifc_req[i].ifr_name, 0) == 0) 
		{
			free_sco_ifconf (hio, &ifc);
			*index = i + 1;
			return 0;
		}
	}

	free_sco_ifconf (hio, &ifc);
	return -1;
#else
	return -1;
#endif
}

/* ---------------------------------------------------------- */

int hio_ifindextobcstr (hio_t* hio, unsigned int index, hio_bch_t* buf, hio_oow_t len)
{
#if defined(_WIN32)
	/* TODO: */
	hio_seterrnum (hio, HIO_ENOIMPL);
	return -1;
#elif defined(__OS2__)
	/* TODO: */
	hio_seterrnum (hio, HIO_ENOIMPL);
	return -1;
#elif defined(__DOS__)
	/* TODO: */
	hio_seterrnum (hio, HIO_ENOIMPL);
	return -1;

#elif defined(SIOCGIFNAME)

	int h, x;
	struct ifreq ifr;

	h = socket(AF_INET, SOCK_DGRAM, 0); 
	if (h <= -1) 
	{
		hio_seterrwithsyserr (hio, 0, errno);
		return -1;
	}

	HIO_MEMSET (&ifr, 0, HIO_SIZEOF(ifr));
	#if defined(HAVE_STRUCT_IFREQ_IFR_IFINDEX)
	ifr.ifr_ifindex = index;
	#else
	ifr.ifr_index = index;
	#endif
	
	x = ioctl(h, SIOCGIFNAME, &ifr);
	close (h);

	if (x <= -1)
	{
		hio_seterrwithsyserr (hio, 0, errno);
		return -1;
	}

	return hio_copy_bcstr(buf, len, ifr.ifr_name);

#elif defined(HAVE_IF_INDEXTONAME)
	hio_bch_t tmp[IF_NAMESIZE + 1];
	if (if_indextoname (index, tmp) == HIO_NULL) 
	{
		hio_seterrwithsyserr (hio, 0, errno);
		return -1;
	}
	return hio_copy_bcstr(buf, len, tmp);

#elif defined(SIOCGIFCONF) && (defined(SIOCGIFANUM) || defined(SIOCGIFNUM))

	struct ifconf ifc;
	hio_oow_t ml;
	int num;

	if (index <= 0) return -1;
	if (get_sco_ifconf(hio, &ifc) <= -1) return -1;

	num = ifc.ifc_len / HIO_SIZEOF(struct ifreq);
	if (index > num) 
	{
		hio_seterrnum (hio, HIO_ENOENT);
		free_sco_ifconf (hio, &ifc);
		return -1;
	}

	ml = hio_copy_bcstr(buf, len, ifc.ifc_req[index - 1].ifr_name);
	free_sco_ifconf (hio, &ifc);
	return ml;

#else
	return -1;
#endif
}

int hio_ifindextoucstr (hio_t* hio, unsigned int index, hio_uch_t* buf, hio_oow_t len)
{
#if defined(_WIN32)
	/* TODO: */
	hio_seterrnum (hio, HIO_ENOIMPL);
	return -1;
#elif defined(__OS2__)
	/* TODO: */
	hio_seterrnum (hio, HIO_ENOIMPL);
	return -1;
#elif defined(__DOS__)
	/* TODO: */
	hio_seterrnum (hio, HIO_ENOIMPL);
	return -1;

#elif defined(SIOCGIFNAME)

	int h, x;
	struct ifreq ifr;
	hio_oow_t wl, ml;

	h = socket(AF_INET, SOCK_DGRAM, 0); 
	if (h <= -1)
	{
		hio_seterrwithsyserr (hio, 0, errno);
		return -1;
	}

	HIO_MEMSET (&ifr, 0, HIO_SIZEOF(ifr));
	#if defined(HAVE_STRUCT_IFREQ_IFR_IFINDEX)
	ifr.ifr_ifindex = index;
	#else
	ifr.ifr_index = index;
	#endif

	x = ioctl(h, SIOCGIFNAME, &ifr);
	close (h);

	if (x <= -1)
	{
		hio_seterrwithsyserr (hio, 0, errno);
		return -1;
	}

	wl = len;
	x = hio_convbtoucstr(hio, ifr.ifr_name, &ml, buf, &wl, 0);
	if (x == -2 && wl > 1) buf[wl - 1] = '\0';
	else if (x != 0) return -1;
	return wl;

#elif defined(HAVE_IF_INDEXTONAME)
	hio_bch_t tmp[IF_NAMESIZE + 1];
	hio_oow_t ml, wl;
	int x;

	if (if_indextoname(index, tmp) == HIO_NULL) 
	{
		hio_seterrwithsyserr (hio, 0, errno);
		return -1;
	}
	wl = len;
	x = hio_convbtoucstr(hio, tmp, &ml, buf, &wl, 0);
	if (x == -2 && wl > 1) buf[wl - 1] = '\0';
	else if (x != 0) return -1;
	return wl;

#elif defined(SIOCGIFCONF) && (defined(SIOCGIFANUM) || defined(SIOCGIFNUM))

	struct ifconf ifc;
	hio_oow_t wl, ml;
	int num, x;

	if (index <= 0) return -1;
	if (get_sco_ifconf(hio, &ifc) <= -1) return -1;

	num = ifc.ifc_len / HIO_SIZEOF(struct ifreq);
	if (index > num) 
	{
		free_sco_ifconf (hio, &ifc);
		return -1;
	}

	wl = len;
	x = hio_convbtoucstr(ifc.ifc_req[index - 1].ifr_name, &ml, buf, &wl, 0);
	free_sco_ifconf (hio, &ifc);

	if (x == -2 && wl > 1) buf[wl - 1] = '\0';
	else if (x != 0) return -1;

	return wl;
#else
	return -1;
#endif
}

