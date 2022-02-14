#include <hio-sck.h>
#include <hio-http.h>
#include <hio-pty.h>
#include <hio-utl.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <assert.h>
#include <stdlib.h>

static hio_t* g_hio = HIO_NULL;

static void handle_sigint (int sig)
{
	if (g_hio) hio_stop (g_hio, HIO_STOPREQ_TERMINATION);
}

int pty_on_read (hio_dev_pty_t* dev, const void* data, hio_iolen_t len)
{
	hio_iolen_t i;
	for (i = 0; i < len; i++) fputc(*((char*)data + i), stdout);
	return 0;
}

int pty_on_write (hio_dev_pty_t* dev, hio_iolen_t wrlen, void*  wrctx)
{
	return 0;
}

void pty_on_close (hio_dev_pty_t* dev)
{
	printf (">> pty closed....\n");
}


int main (int argc, char* argv[])
{
	hio_t* hio = HIO_NULL;
	hio_dev_pty_t* pty;
	hio_dev_pty_make_t pi;
	struct sigaction sigact;
	int xret = -1;

	memset (&sigact, 0, HIO_SIZEOF(sigact));
	sigact.sa_handler = SIG_IGN;
	sigaction (SIGPIPE, &sigact, HIO_NULL);

	memset (&sigact, 0, HIO_SIZEOF(sigact));
	sigact.sa_handler = handle_sigint;
	sigaction (SIGINT, &sigact, HIO_NULL);
	
	hio = hio_open(HIO_NULL, 0, HIO_NULL, HIO_FEATURE_ALL, 512, HIO_NULL);
	if (!hio)
	{
		printf ("Cannot open hio\n");
		goto oops;
	}

	hio_setoption (hio, HIO_LOG_TARGET_BCSTR, "/dev/stderr");

	memset (&pi, 0, HIO_SIZEOF(pi));
	pi.on_write = pty_on_write;
	pi.on_read = pty_on_read;
	pi.on_close = pty_on_close; 
	pty = hio_dev_pty_make(hio, 0, &pi);
	if (!pty) goto oops;

	g_hio = hio;

	hio_loop (hio);

	xret = 0;

oops:

	memset (&sigact, 0, HIO_SIZEOF(sigact));
	sigact.sa_handler = SIG_IGN;
	sigaction (SIGINT, &sigact, HIO_NULL);

	if (hio) hio_close (hio);
	return xret;
}

