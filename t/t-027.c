/* Interface lookup errors must survive a failure in descriptor cleanup. */
#include "hio-prv.h"
#include <hio-skad.h>
#include <hio-nwif.h>
#include "tap.h"

#if !defined(_WIN32) && !defined(__OS2__) && !defined(__DOS__) && defined(HAVE_NET_IF_H)
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>
#if defined(HAVE_SYS_IOCTL_H)
#include <sys/ioctl.h>
#endif
#include <net/if.h>
#if defined(HAVE_SYS_SOCKIO_H)
#include <sys/sockio.h>
#endif
#if defined(SIOCGIFINDEX) && defined(SIOCGIFNAME)
#define TEST_LOOKUP_IOCTLS
#endif
#endif

#if defined(TEST_LOOKUP_IOCTLS)
static int test_socket (int domain, int type, int protocol)
{
	(void)domain;
	(void)type;
	(void)protocol;
	return 123;
}

static int test_ioctl (int fd, unsigned long request, void* data)
{
	(void)fd;
	(void)request;
	(void)data;
	errno = ENOENT;
	return -1;
}

static int test_close (int fd)
{
	(void)fd;
	errno = EBADF;
	return -1;
}

#define socket test_socket
#define ioctl test_ioctl
#define close(fd) test_close(fd)
#include "../lib/nwif.c"
#undef socket
#undef ioctl
#undef close

int main (void)
{
	hio_t* hio;
	const hio_uch_t bad_if[] = { 'b', 'a', 'd', '-', 'i', 'f', 0 };
	hio_bch_t bytes[32];
	hio_uch_t wide[32];
	const hio_bch_t* labels[] =
	{
		"byte C-string lookup preserves the ioctl error across close failure",
		"byte-slice lookup preserves the ioctl error across close failure",
		"wide C-string lookup preserves the ioctl error across close failure",
		"wide-slice lookup preserves the ioctl error across close failure",
		"byte reverse lookup preserves the ioctl error across close failure",
		"wide reverse lookup preserves the ioctl error across close failure"
	};
	hio_oow_t i, j;
	unsigned int index;
	int n, good;

	no_plan();
	hio = hio_open(HIO_NULL, 0, HIO_NULL, HIO_FEATURE_LOG, 1, HIO_NULL);
	if (!hio) { bail_out("cannot open Hio"); return 1; }
	for (i = 0; i < HIO_COUNTOF(labels); i++)
	{
		index = 12345;
		for (j = 0; j < HIO_COUNTOF(bytes); j++) bytes[j] = wide[j] = 'X';
		hio_seterrnum(hio, HIO_ENOERR);
		switch (i)
		{
			case 0: n = hio_bcstrtoifindex(hio, "bad-if", &index); break;
			case 1: n = hio_bcharstoifindex(hio, "bad-if", 6, &index); break;
			case 2: n = hio_ucstrtoifindex(hio, bad_if, &index); break;
			case 3: n = hio_ucharstoifindex(hio, bad_if, 6, &index); break;
			case 4: n = hio_ifindextobcstr(hio, 99, bytes, HIO_COUNTOF(bytes)); break;
			default: n = hio_ifindextoucstr(hio, 99, wide, HIO_COUNTOF(wide)); break;
		}
		good = n <= -1 && hio_geterrnum(hio) == HIO_ENOENT && index == 12345;
		for (j = 0; j < HIO_COUNTOF(bytes); j++)
		{
			if (bytes[j] != 'X' || wide[j] != 'X') good = 0;
		}
		OK(good, labels[i]);
	}
	hio_close(hio);
	return exit_status();
}
#else
int main (void)
{
	skip_all("interface lookup ioctls are unavailable");
	return 0;
}
#endif
