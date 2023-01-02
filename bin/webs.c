#include <hio-http.h>
#include <hio-tar.h>
#include <hio-opt.h>
#include <hio-prv.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

struct arg_info_t
{
	const char* laddrs;
	const char* docroot;
	int file_list_dir;
	int file_load_index_page;
};
typedef struct arg_info_t arg_info_t;

struct htts_ext_t
{
	const arg_info_t* ai;
};
typedef struct htts_ext_t htts_ext_t;

struct buff_t
{
	hio_uint8_t buf[4096];
	hio_oow_t len;
};
typedef struct buff_t buff_t;

/* ------------------------------------------------------------------------- */

static void untar (hio_t* hio, hio_dev_thr_iopair_t* iop, hio_svc_htts_thr_func_info_t* tfi, void* ctx)
{
	FILE* wfp = HIO_NULL;
	htts_ext_t* ext;
	hio_tar_t* tar = HIO_NULL;
	hio_uint8_t buf[4096];
	ssize_t n;

	ext = hio_svc_htts_getxtn(tfi->htts);

/* TODO: error handling on write() failure */
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

	hio_tar_setxrootwithbcstr (tar, ext->ai->docroot);

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

/* ------------------------------------------------------------------------- */

static hio_oow_t write_all_to_fd (int fd, const hio_uint8_t* ptr, hio_oow_t len)
{
	hio_oow_t rem = len;

	while (rem > 0)
	{
		hio_ooi_t n;
		n = write(fd, ptr, rem);
		if (n <= -1) break;
		rem -= n;
		ptr += n;
	}

	return len - rem; /* return the number of bytes written */
}

static HIO_INLINE void init_buff (buff_t* buf)
{
	buf->len = 0;
}

static int flush_buff_to_fd (int fd, buff_t* buf)
{
	if (buf->len > 0)
	{
		hio_oow_t n;
		n = write_all_to_fd(fd, buf->buf, buf->len);
		buf->len -= n;
		if (buf->len > 0) return -1;
	}
	return 0;
}

static int write_buff_to_fd (int fd, buff_t* buf, const void* ptr, hio_oow_t len)
{
	hio_oow_t rcapa;

	rcapa = HIO_COUNTOF(buf->buf) - buf->len;
	if (len >= HIO_COUNTOF(buf->buf) + rcapa)
	{
		if (flush_buff_to_fd(fd, buf) <= -1) return -1;
		if (write_all_to_fd(fd, (const hio_uint8_t*)ptr, len) != len) return -1;
	}
	else if (len >= rcapa)
	{
		HIO_MEMCPY (&buf->buf[buf->len], ptr, rcapa);
		buf->len += rcapa;
		if (flush_buff_to_fd(fd, buf) <= -1) return -1;
		HIO_MEMCPY (buf->buf, (const hio_uint8_t*)ptr + rcapa, len - rcapa);
		buf->len = len - rcapa;
	}
	else
	{
		HIO_MEMCPY (&buf->buf[buf->len], ptr, len);
		buf->len += len;
	}

	return 0;
}

/* ------------------------------------------------------------------------- */

static const hio_bch_t* file_get_mime_type (hio_svc_htts_t* htts, const hio_bch_t* qpath, const hio_bch_t* file_path, void* ctx)
{
	const hio_bch_t* mt = HIO_NULL;
	const hio_bch_t* dot;
	dot = hio_rfind_bchar_in_bcstr(file_path, '.');
	if (dot) mt = hio_get_mime_type_by_ext(dot + 1);
	return mt;
}

static int file_open_dir_list (hio_svc_htts_t* htts, const hio_bch_t* qpath, const hio_bch_t* dir_path, const hio_bch_t** res_mime_type, void* ctx)
{
	htts_ext_t* ext = hio_svc_htts_getxtn(htts);
	hio_t* hio = hio_svc_htts_gethio(htts);
	DIR* dp = HIO_NULL;
	hio_bch_t file_path[] = "/tmp/.XXXXXX";
	int fd = -1;
	struct dirent* de;
	buff_t buf;

	if (ext->ai->file_load_index_page)
	{
		hio_bch_t* index_path;

		index_path = hio_svc_htts_dupmergepaths(htts, dir_path, "index.html");
		if (HIO_UNLIKELY(!index_path)) goto oops;

		fd = open(index_path, O_RDONLY, 0644);
		if (fd >= 0)
		{
			if (res_mime_type)
			{
				const hio_bch_t* mt;
				mt = file_get_mime_type(htts, qpath, index_path, ctx);
				if (mt) *res_mime_type = mt;
			}
			hio_freemem (hio, index_path);
			return fd;
		}

		hio_freemem (hio, index_path);
	}

	if (!ext->ai->file_list_dir) goto oops;

	dp = opendir(dir_path);
	if (!dp) goto oops;

	/* TOOD: mkostemp instead and specify O_CLOEXEC and O_LARGEFILE? */
	fd = mkstemp(file_path);
	if (fd <= -1) goto oops;

	unlink (file_path);

	buf.len = 0;
	init_buff (&buf);

	if (write_buff_to_fd(fd, &buf, "<html><body>", 12) <= -1) goto oops;
	if (!(qpath[0] == '\0' || (qpath[0] == '/' && qpath[1] == '\0')) &&
	    write_buff_to_fd(fd, &buf,"<li><a href=\"..\">..</a>", 23) <= -1) goto oops;

	while ((de = readdir(dp)))
	{
		struct stat st;
		hio_bch_t* tptr, * dend;
		hio_bch_t tmp[1024]; /* this must be at least 5 characters large for html escaping */
		hio_oow_t tml;
		int n;

		if ((de->d_name[0] == '.' && de->d_name[1] == '\0') ||
			(de->d_name[0] == '.' && de->d_name[1] == '.' && de->d_name[2] == '\0')) continue;

		tptr = hio_svc_htts_dupmergepaths(htts, dir_path, de->d_name);
		if (HIO_UNLIKELY(!tptr)) continue;
		n = stat(tptr, &st);
		hio_freemem (hio, tptr);
		if (HIO_UNLIKELY(n <= -1)) continue;

		dend = de->d_name + strlen(de->d_name);

		if (write_buff_to_fd(fd, &buf, "<li><a href=\"", 13) <= -1) goto oops;

		for (tptr = de->d_name; tptr < dend; )
		{
			hio_oow_t dlen = dend - tptr;
			if (dlen > HIO_COUNTOF(tmp) / 3) dlen = HIO_COUNTOF(tmp) / 3; /* it can grow upto 3 folds */
			HIO_ASSERT (hio, dlen >= 1);
			tml = hio_perenc_http_bchars(tptr, dlen, tmp, HIO_COUNTOF(tmp), 0); /* feed a chunk that won't overflow the buffer */
			HIO_ASSERT (hio, tml <= HIO_COUNTOF(tmp)); /* the buffer 'tmp' must be large enough */
			if (write_buff_to_fd(fd, &buf, tmp, tml) <= -1) goto oops;
			tptr += dlen;
		}

		if (S_ISDIR(st.st_mode) && write_buff_to_fd(fd, &buf, "/", 1) <= -1) goto oops;
		if (write_buff_to_fd(fd, &buf, "\">", 2) <= -1) goto oops;

		dend = de->d_name + strlen(de->d_name);
		for (tptr = de->d_name; tptr < dend; )
		{
			hio_oow_t dlen = dend - tptr;
			if (dlen > HIO_COUNTOF(tmp) / 5) dlen = HIO_COUNTOF(tmp) / 5; /* it can grow upto 5 folds */
			HIO_ASSERT (hio, dlen >= 1);
			tml = hio_escape_html_bchars(tptr, dlen, tmp, HIO_COUNTOF(tmp)); /* feed a chunk that won't overflow the buffer */
			HIO_ASSERT (hio, tml <= HIO_COUNTOF(tmp)); /* the buffer 'tmp' must be large enough */
			if (write_buff_to_fd(fd, &buf, tmp, tml) <= -1) goto oops;
			tptr += dlen;
		}

		if (S_ISDIR(st.st_mode) && write_buff_to_fd(fd, &buf, "/", 1) <= -1) goto oops;
		if (write_buff_to_fd(fd, &buf, "</a>", 4) <= -1) goto oops;
	}

	if (write_buff_to_fd(fd, &buf, "</body></html>\n", 15) <= -1) goto oops;
	if (flush_buff_to_fd(fd, &buf) <= -1) goto oops;

	closedir (dp);
	lseek (fd, SEEK_SET, 0);

done:
	if (res_mime_type) *res_mime_type = "text/html";
	return fd;

oops:
	if (fd >= 0) close (fd);
	if (dp) closedir (dp);
	return -1;
}

static int process_http_request (hio_svc_htts_t* htts, hio_dev_sck_t* csck, hio_htre_t* req)
{
	htts_ext_t* ext = hio_svc_htts_getxtn(htts);
	hio_t* hio = hio_svc_htts_gethio(htts);
	hio_http_method_t mth;
	const hio_bch_t* qpath;

	static hio_svc_htts_file_cbs_t fcbs = { file_get_mime_type, file_open_dir_list, HIO_NULL };

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
		if (hio_svc_htts_dofile(htts, csck, req, ext->ai->docroot, qpath, HIO_NULL, 0, &fcbs) <= -1) goto oops;
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

int webs_start (hio_t* hio, const arg_info_t* ai)
{
	const hio_bch_t* ptr, * end;
	hio_bcs_t tok;
	hio_dev_sck_bind_t bi[100];
	hio_oow_t bic;
	hio_svc_htts_t* webs;
	htts_ext_t* ext;

	bic = 0;
	ptr = ai->laddrs;
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
	ext->ai = ai;

	return 0;
}

static hio_t* g_hio = HIO_NULL;

static void handle_sigint (int sig)
{
	if (g_hio) hio_stop (g_hio, HIO_STOPREQ_TERMINATION);
}

static int process_args (int argc, char* argv[], arg_info_t* ai)
{
	static hio_bopt_lng_t lopt[] =
	{
		{ "file-no-list-dir", '\0' },
		{ "file-no-load-index-page", '\0'},
		{ HIO_NULL, '\0'}
	};
	static hio_bopt_t opt =
	{
		"",
		lopt
	};

	hio_bci_t c;

	if (argc < 3)
	{
	print_usage:
		fprintf (stderr, "Usage: %s [options] listen-address:port docroot-dir\n", argv[0]);
		return -1;
	}

	memset (ai, 0, HIO_SIZEOF(*ai));
	ai->file_list_dir = 1;
	ai->file_load_index_page = 1;

	while ((c = hio_getbopt(argc, argv, &opt)) != HIO_BCI_EOF)
	{
		switch (c)
		{
			case '\0':
				if (strcasecmp(opt.lngopt, "file-no-list-dir") == 0)
				{
					ai->file_list_dir = 0;
					break;
				}
				else if (strcasecmp(opt.lngopt, "file-no-load-index-page") == 0)
				{
					ai->file_load_index_page = 0;
					break;
				}
				goto print_usage;

			case ':':
				if (opt.lngopt)
					fprintf (stderr, "bad argument for '%s'\n", opt.lngopt);
				else
					fprintf (stderr, "bad argument for '%c'\n", opt.opt);
				return -1;

			default:
				goto print_usage;
		}
	}

	if (argc - opt.ind != 2) goto print_usage;

	ai->laddrs = argv[opt.ind++];
	ai->docroot = argv[opt.ind++];
	return 0;
}

int main (int argc, char* argv[])
{
	hio_t* hio = HIO_NULL;
	struct sigaction sigact;
	arg_info_t ai;
	int xret = -1;

	if (process_args(argc, argv, &ai) <= -1) return -1;

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

	if (webs_start(hio, &ai) <= -1) goto oops;

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

