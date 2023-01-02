#include <hio-http.h>
#include <string.h>
#include "tap.h"

static int test_perenc(void)
{
	hio_bch_t tmp[100];	
	hio_oow_t len;

	len = hio_perenc_http_bchars("<>abc<>", 7, tmp, 3, 0);
	OK (len == 15, "hio_perenc_http_bchars() with small buffer 1");

	len = hio_perenc_http_bchars("<>abc<>", 7, tmp, 14, 0);
	OK (len == 15, "hio_perenc_http_bchars() with small buffer 2");

	memset (tmp, '1', sizeof(tmp));
	len = hio_perenc_http_bchars("<>abc<>", 7, tmp, 15, 0);
	OK (len == 15, "hio_perenc_http_bchars() with fit buffer");
	OK (hio_comp_bchars_bcstr(tmp, len, "%3C%3Eabc%3C%3E", 0) == 0, "hio_perenc_http_bchars() with fit buffer");

	len = hio_perenc_http_bcstr("<>abc<>", tmp, 3, 0);
	OK (len == 16, "hio_perenc_http_bcstr() with small buffer 1");

	len = hio_perenc_http_bcstr("<>abc<>", tmp, 15, 0);
	OK (len == 16, "hio_perenc_http_bcstr() with small buffer 2");

	memset (tmp, '1', sizeof(tmp));
	len = hio_perenc_http_bcstr("<>abc<>", tmp, 16, 0);
	OK (len == 15, "hio_perenc_http_bcstr() with fit buffer");
	OK (hio_comp_bchars_bcstr(tmp, len, "%3C%3Eabc%3C%3E", 0) == 0, "hio_perenc_http_bcstr() with fit buffer");
	OK (tmp[len] == '\0', "hio_perenc_http_bcstr() with fiter buffer - null termination");

	return 0;

oops:
	return -1;
}

static int test_escape_html(void)
{
	hio_bch_t tmp[100];	
	hio_oow_t len;

	len = hio_escape_html_bchars("<>abc<>", 7, tmp, 3);
	OK (len == 19, "hio_escape_html_bchars() with small buffer 1");

	len = hio_escape_html_bchars("<>abc<>", 7, tmp, 18);
	OK (len == 19, "hio_escape_html_bchars() with small buffer 2");

	memset (tmp, '1', sizeof(tmp));
	len = hio_escape_html_bchars("<>abc<>", 7, tmp, 19);
	OK (len == 19, "hio_escape_html_bchars() with fit buffer");
	OK (hio_comp_bchars_bcstr(tmp, len, "&lt;&gt;abc&lt;&gt;", 0) == 0, "hio_escape_html_bchars() with fit buffer");

	len = hio_escape_html_bcstr("<>abc<>", tmp, 3);
	OK (len == 20, "hio_escape_html_bcstr() with small buffer 1");

	len = hio_escape_html_bcstr("<>abc<>", tmp, 19);
	OK (len == 20, "hio_escape_html_bcstr() with small buffer 2");

	memset (tmp, '1', sizeof(tmp));
	len = hio_escape_html_bcstr("<>abc<>", tmp, 20);
	OK (len == 19, "hio_escape_html_bcstr() with fit buffer");
	OK (hio_comp_bchars_bcstr(tmp, len, "&lt;&gt;abc&lt;&gt;", 0) == 0, "hio_escape_html_bcstr() with fit buffer");
	OK (tmp[len] == '\0', "hio_escape_html_bcstr() with fiter buffer - null termination");

	return 0;

oops:
	return -1;
}

int main()
{
	no_plan ();
	if (test_perenc() <= -1) return -1;
	if (test_escape_html() <= -1) return -1;
	return exit_status();
}
