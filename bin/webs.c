#include <hio-http.h>
#include <hio-tar.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

struct htts_ext_t
{
	const hio_bch_t* docroot;
};
typedef struct htts_ext_t htts_ext_t;

void untar (hio_t* hio, hio_dev_thr_iopair_t* iop, hio_svc_htts_thr_func_info_t* tfi, void* ctx)
{
	FILE* wfp = HIO_NULL;
	htts_ext_t* ext;
	hio_tar_t* tar = HIO_NULL;
	hio_uint8_t buf[4096];
	ssize_t n;

	ext = hio_svc_htts_getxtn(tfi->htts);

	wfp = fdopen(iop->wfd, "w");
	if (!wfp)
	{
		write (iop->wfd, "Status: 500\r\n\r\n", 15); /* internal server error */
		goto done;
	}

	tar = hio_tar_open(hio, 0);
	if (!tar)
	{
		write (iop->wfd, "Status: 500\r\n\r\n", 15);
		goto done;
	}

	hio_tar_setxrootwithbcstr (tar, ext->docroot);

	while (1)
	{
		n = read(iop->rfd, buf, HIO_COUNTOF(buf));
		if (n <= 0) break; /* eof or error */

		if (hio_tar_xfeed(tar, buf, n) <= -1)
		{
			write (iop->wfd, "Status: 500\r\n\r\n", 20);
			goto done;
		}
	}

	hio_tar_endxfeed (tar);
	write (iop->wfd, "Status: 200\r\n\r\n", 15); 

done:
	if (tar)
	{
		hio_tar_close (tar);
	}
	if (wfp)
	{
		iop->wfd = HIO_SYSHND_INVALID; /* prevent double close by the main library since this function closes it with fclose() */
		fclose (wfp);
	}
}

static int process_http_request (hio_svc_htts_t* htts, hio_dev_sck_t* csck, hio_htre_t* req)
{
	htts_ext_t* ext = hio_svc_htts_getxtn(htts);
	hio_t* hio = hio_svc_htts_gethio(htts);
	hio_http_method_t mth;
	const hio_bch_t* qpath;

	hio_htre_perdecqpath (req);

	mth = hio_htre_getqmethodtype(req);
	qpath = hio_htre_getqpath(req);

	if (mth == HIO_HTTP_OTHER && hio_comp_bcstr(hio_htre_getqmethodname(req), "UNTAR", 1) == 0)
	{
		/* don't care about the path for now. TODO: make this secure and reasonable */
		hio_svc_htts_dothr(htts, csck, req, untar, HIO_NULL, 0);
	}
	else // if (mth == HIO_HTTP_GET || mth == HIO_HTTP_POST)
	{
		/* TODO: proper mime-type */
		/* TODO: make HIO_SVC_HTTS_FILE_DIR a cli option */
		if (hio_svc_htts_dofile(htts, csck, req, ext->docroot, qpath, HIO_NULL, HIO_SVC_HTTS_FILE_LIST_DIR, HIO_NULL) <= -1) goto oops;
	}
#if 0
	else
	{
		if (hio_svc_htts_dotxt(htts, csck, req, HIO_HTTP_STATUS_FORBIDDEN, "text/plain", hio_http_status_to_bcstr(403), 0) <= -1) goto oops;
	}
#endif
	return 0;

oops:
	hio_dev_sck_halt (csck);
	return 0;
}

int webs_start (hio_t* hio, const hio_bch_t* addrs, const hio_bch_t* docroot)
{
	const hio_bch_t* ptr, * end;
	hio_bcs_t tok;
	hio_dev_sck_bind_t bi[100];
	hio_oow_t bic;
	hio_svc_htts_t* webs;
	htts_ext_t* ext;

	bic = 0;
	ptr = addrs;
	end = ptr + hio_count_bcstr(ptr);
	while (ptr)
	{
		ptr = hio_tokenize_bchars(ptr, end - ptr, ", ", 2, &tok, 0);
		if (tok.len > 0)
		{
			if (hio_bcharstoskad(hio, tok.ptr, tok.len, &bi[bic].localaddr) <= -1)
			{
				/* TODO: logging */
				continue;
			}
			bi[bic].options = HIO_DEV_SCK_BIND_REUSEADDR | HIO_DEV_SCK_BIND_REUSEPORT | HIO_DEV_SCK_BIND_IGNERR;
			bic++;

			if (bic >= HIO_COUNTOF(bi)) break; /* TODO: make 'bi' dynamic */
		}
	}

	webs = hio_svc_htts_start(hio, HIO_SIZEOF(htts_ext_t), bi, bic, process_http_request);
	if (!webs) return -1; /* TODO: logging */

	ext = hio_svc_htts_getxtn(webs);
	ext->docroot = docroot;

	return 0;
}

static hio_t* g_hio = HIO_NULL;

static void handle_sigint (int sig)
{
	if (g_hio) hio_stop (g_hio, HIO_STOPREQ_TERMINATION);
}

int main (int argc, char* argv[])
{
	hio_t* hio = HIO_NULL;
	struct sigaction sigact;
	int xret = -1;

#if 0
	hio_oow_t i;

// TODO: use getopt() or something similar
	for (i = 1; i < argc; )
	{
		if (strcmp(argv[i], "-s") == 0)
		{
			i++;
			g_dev_type4 = HIO_DEV_SCK_SCTP4;
			g_dev_type6 = HIO_DEV_SCK_SCTP6;
		}
		else
		{
			printf ("Error: invalid argument %s\n", argv[i]);
			return -1;
		}
	}
#else
	if (argc < 3)
	{
		printf ("Error: %s listen-address doc-root\n", hio_get_base_name_bcstr(argv[0]));
		return -1;
	}
#endif

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
	{
		hio_bitmask_t logmask;
		hio_getoption (hio, HIO_LOG_MASK, &logmask);
		logmask |= HIO_LOG_GUARDED;
		hio_setoption (hio, HIO_LOG_MASK, &logmask);
	}
	
	g_hio = hio;

	if (webs_start(hio, argv[1], argv[2]) <= -1) goto oops;

	hio_loop (hio);

	g_hio = HIO_NULL;
	xret = 0;

oops:

	memset (&sigact, 0, HIO_SIZEOF(sigact));
	sigact.sa_handler = SIG_IGN;
	sigaction (SIGINT, &sigact, HIO_NULL);

	if (hio) hio_close (hio);
	return xret;
}

