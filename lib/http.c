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

#include <hio-http.h>
#include <hio-chr.h>
#include <hio-utl.h>
#include "hio-prv.h"
#include <time.h>

int hio_comp_http_versions (const hio_http_version_t* v1, const hio_http_version_t* v2)
{
	if (v1->major == v2->major) return v1->minor - v2->minor;
	return v1->major - v2->major;
}

int hio_comp_http_version_numbers (const hio_http_version_t* v1, int v2_major, int v2_minor)
{
	if (v1->major == v2_major) return v1->minor - v2_minor;
	return v1->major - v2_major;
}

const hio_bch_t* hio_http_status_to_bcstr (int code)
{
	const hio_bch_t* msg;

	switch (code)
	{
		case HIO_HTTP_STATUS_CONTINUE:              msg = "Continue"; break;
		case HIO_HTTP_STATUS_SWITCH_PROTOCOL:       msg = "Switching Protocols"; break;

		case HIO_HTTP_STATUS_OK:                    msg = "OK"; break;
		case HIO_HTTP_STATUS_CREATED:               msg = "Created"; break;
		case HIO_HTTP_STATUS_ACCEPTED:              msg = "Accepted"; break;
		case HIO_HTTP_STATUS_NON_AUTHORITATIVE:     msg = "Non-Authoritative Information"; break;
		case HIO_HTTP_STATUS_NO_CONTENT:            msg = "No Content"; break;
		case HIO_HTTP_STATUS_RESET_CONTENT:         msg = "Reset Content"; break;
		case HIO_HTTP_STATUS_PARTIAL_CONTENT:       msg = "Partial Content"; break;
		
		case 300: msg = "Multiple Choices"; break;
		case HIO_HTTP_STATUS_MOVED_PERMANENTLY:     msg = "Moved Permanently"; break;
		case 302: msg = "Found"; break;
		case 303: msg = "See Other"; break;
		case HIO_HTTP_STATUS_NOT_MODIFIED:          msg = "Not Modified"; break;
		case 305: msg = "Use Proxy"; break;
		case 307: msg = "Temporary Redirect"; break;
		case 308: msg = "Permanent Redirect"; break;

		case HIO_HTTP_STATUS_BAD_REQUEST:           msg = "Bad Request"; break;
		case 401: msg = "Unauthorized"; break;
		case 402: msg = "Payment Required"; break;
		case HIO_HTTP_STATUS_FORBIDDEN:             msg = "Forbidden"; break;
		case HIO_HTTP_STATUS_NOT_FOUND:             msg = "Not Found"; break;
		case HIO_HTTP_STATUS_METHOD_NOT_ALLOWED:   msg = "Method Not Allowed"; break;
		case 406: msg = "Not Acceptable"; break;
		case 407: msg = "Proxy Authentication Required"; break;
		case 408: msg = "Request Timeout"; break;
		case 409: msg = "Conflict"; break;
		case 410: msg = "Gone"; break;
		case HIO_HTTP_STATUS_LENGTH_REQUIRED:       msg = "Length Required"; break;
		case 412: msg = "Precondition Failed"; break;
		case 413: msg = "Request Entity Too Large"; break;
		case 414: msg = "Request-URI Too Long"; break;
		case 415: msg = "Unsupported Media Type"; break;
		case HIO_HTTP_STATUS_RANGE_NOT_SATISFIABLE: msg = "Requested Range Not Satisfiable"; break;
		case HIO_HTTP_STATUS_EXPECTATION_FAILED:    msg = "Expectation Failed"; break;
		case 426: msg = "Upgrade Required"; break;
		case 428: msg = "Precondition Required"; break;
		case 429: msg = "Too Many Requests"; break;
		case 431: msg = "Request Header Fields Too Large"; break;

		case HIO_HTTP_STATUS_INTERNAL_SERVER_ERROR: msg = "Internal Server Error"; break;
		case 501: msg = "Not Implemented"; break;
		case 502: msg = "Bad Gateway"; break;
		case 503: msg = "Service Unavailable"; break;
		case 504: msg = "Gateway Timeout"; break;
		case 505: msg = "HTTP Version Not Supported"; break;

		default: msg = "Unknown Error"; break;
	}

	return msg;
}

const hio_bch_t* hio_http_method_to_bcstr (hio_http_method_t type)
{
	/* keep this table in the same order as hio_httpd_method_t enumerators */
	static hio_bch_t* names[]  =
	{
		"OTHER",

		"HEAD",
		"GET",
		"POST",
		"PUT",
		"DELETE",
		"PATCH",
		"OPTIONS",
		"TRACE",
		"CONNECT"
	}; 

	return (type < 0 || type >= HIO_COUNTOF(names))? HIO_NULL: names[type];
}

struct mtab_t
{
	const hio_bch_t* name;
	hio_http_method_t type;
};

static struct mtab_t mtab[] =
{
	/* keep this table sorted by name for binary search */
	{ "CONNECT", HIO_HTTP_CONNECT },
	{ "DELETE",  HIO_HTTP_DELETE },
	{ "GET",     HIO_HTTP_GET },
	{ "HEAD",    HIO_HTTP_HEAD },
	{ "OPTIONS", HIO_HTTP_OPTIONS },
	{ "PATCH",   HIO_HTTP_PATCH },
	{ "POST",    HIO_HTTP_POST },
	{ "PUT",     HIO_HTTP_PUT },
	{ "TRACE",   HIO_HTTP_TRACE }
};

hio_http_method_t hio_bcstr_to_http_method (const hio_bch_t* name)
{
	/* perform binary search */

	/* declaring left, right, mid to be of int is ok
	 * because we know mtab is small enough. */
	int left = 0, right = HIO_COUNTOF(mtab) - 1, mid;

	while (left <= right)
	{
		int n;
		struct mtab_t* entry;

		/*mid = (left + right) / 2;*/
		mid = left + (right - left) / 2;
		entry = &mtab[mid];

		n = hio_comp_bcstr(name, entry->name, 1);
		if (n < 0) 
		{
			/* if left, right, mid were of hio_oow_t,
			 * you would need the following line. 
			if (mid == 0) break;
			 */
			right = mid - 1;
		}
		else if (n > 0) left = mid + 1;
		else return entry->type;
	}

	return HIO_HTTP_OTHER;
}

hio_http_method_t hio_bchars_to_http_method (const hio_bch_t* nameptr, hio_oow_t namelen)
{
	/* perform binary search */

	/* declaring left, right, mid to be of int is ok
	 * because we know mtab is small enough. */
	int left = 0, right = HIO_COUNTOF(mtab) - 1, mid;

	while (left <= right)
	{
		int n;
		struct mtab_t* entry;

		/*mid = (left + right) / 2;*/
		mid = left + (right - left) / 2;
		entry = &mtab[mid];

		n = hio_comp_bchars_bcstr(nameptr, namelen, entry->name, 1);
		if (n < 0) 
		{
			/* if left, right, mid were of hio_oow_t,
			 * you would need the following line. 
			if (mid == 0) break;
			 */
			right = mid - 1;
		}
		else if (n > 0) left = mid + 1;
		else return entry->type;
	}

	return HIO_HTTP_OTHER;
}

int hio_parse_http_range_bcstr (const hio_bch_t* str, hio_http_range_t* range)
{
	/* NOTE: this function does not support a range set 
	 *       like bytes=1-20,30-50 */

	hio_foff_t from, to;
	int type = HIO_HTTP_RANGE_PROPER;

	if (str[0] != 'b' || str[1] != 'y' || str[2] != 't' || str[3] != 'e' || str[4] != 's' || str[5] != '=') return -1;
	str += 6;

	from = to = 0;
	if (hio_is_bch_digit(*str))
	{
		do
		{
			from = from * 10 + (*str - '0');
			str++;
		}
		while (hio_is_bch_digit(*str));
	}
	else type = HIO_HTTP_RANGE_SUFFIX;

	if (*str != '-') return -1;
	str++;

	if (hio_is_bch_digit(*str))
	{
		to = 0;
		do
		{
			to = to * 10 + (*str - '0');
			str++;
		}
		while (hio_is_bch_digit(*str));

		if (from > to) return -1;
	}
	else type = HIO_HTTP_RANGE_PREFIX;

	while (hio_is_bch_space(*str)) str++;
	if (*str != '\0') return -1;

	range->type = type;
	range->from = from;
	range->to = to;
	return 0;
}

typedef struct mname_t mname_t;
struct mname_t
{
	const hio_bch_t* s;
	const hio_bch_t* l;
};
	
static mname_t wday_name[] =
{
	{ "Sun", "Sunday" },
	{ "Mon", "Monday" },
	{ "Tue", "Tuesday" },
	{ "Wed", "Wednesday" },
	{ "Thu", "Thursday" },
	{ "Fri", "Friday" },
	{ "Sat", "Saturday" }
};

static mname_t mon_name[] =
{
	{ "Jan", "January" },
	{ "Feb", "February" },
	{ "Mar", "March" },
	{ "Apr", "April" },
	{ "May", "May" },
	{ "Jun", "June" },
	{ "Jul", "July" },
	{ "Aug", "August" },
	{ "Sep", "September" },
	{ "Oct", "October" },
	{ "Nov", "November" },
	{ "Dec", "December" }
};

int hio_parse_http_time_bcstr (const hio_bch_t* str, hio_ntime_t* nt)
{
	struct tm bt;
	const hio_bch_t* word;
	hio_oow_t wlen, i;

	/* TODO: support more formats */

	HIO_MEMSET (&bt, 0, HIO_SIZEOF(bt));

	/* weekday */
	while (hio_is_bch_space(*str)) str++;
	for (word = str; hio_is_bch_alpha(*str); str++);
	wlen = str - word;
	for (i = 0; i < HIO_COUNTOF(wday_name); i++)
	{
		if (hio_comp_bchars_bcstr(word, wlen, wday_name[i].s, 1) == 0)
		{
			bt.tm_wday = i;
			break;
		}
	}
	if (i >= HIO_COUNTOF(wday_name)) return -1;

	/* comma - i'm just loose as i don't care if it doesn't exist */
	while (hio_is_bch_space(*str)) str++;
	if (*str == ',') str++;

	/* day */
	while (hio_is_bch_space(*str)) str++;
	if (!hio_is_bch_digit(*str)) return -1;
	do bt.tm_mday = bt.tm_mday * 10 + *str++ - '0'; while (hio_is_bch_digit(*str));

	/* month */
	while (hio_is_bch_space(*str)) str++;
	for (word = str; hio_is_bch_alpha(*str); str++);
	wlen = str - word;
	for (i = 0; i < HIO_COUNTOF(mon_name); i++)
	{
		if (hio_comp_bchars_bcstr(word, wlen, mon_name[i].s, 1) == 0)
		{
			bt.tm_mon = i;
			break;
		}
	}
	if (i >= HIO_COUNTOF(mon_name)) return -1;

	/* year */
	while (hio_is_bch_space(*str)) str++;
	if (!hio_is_bch_digit(*str)) return -1;
	do bt.tm_year = bt.tm_year * 10 + *str++ - '0'; while (hio_is_bch_digit(*str));
	bt.tm_year -= 1900;

	/* hour */
	while (hio_is_bch_space(*str)) str++;
	if (!hio_is_bch_digit(*str)) return -1;
	do bt.tm_hour = bt.tm_hour * 10 + *str++ - '0'; while (hio_is_bch_digit(*str));
	if (*str != ':')  return -1;
	str++;

	/* min */
	while (hio_is_bch_space(*str)) str++;
	if (!hio_is_bch_digit(*str)) return -1;
	do bt.tm_min = bt.tm_min * 10 + *str++ - '0'; while (hio_is_bch_digit(*str));
	if (*str != ':')  return -1;
	str++;

	/* sec */
	while (hio_is_bch_space(*str)) str++;
	if (!hio_is_bch_digit(*str)) return -1;
	do bt.tm_sec = bt.tm_sec * 10 + *str++ - '0'; while (hio_is_bch_digit(*str));

	/* GMT */
	while (hio_is_bch_space(*str)) str++;
	for (word = str; hio_is_bch_alpha(*str); str++);
	wlen = str - word;
	if (hio_comp_bchars_bcstr(word, wlen, "GMT", 1) != 0) return -1;

	while (hio_is_bch_space(*str)) str++;
	if (*str != '\0') return -1;

	nt->sec = timegm(&bt);
	nt->nsec = 0;

	return 0;
}

hio_bch_t* hio_fmt_http_time_to_bcstr (const hio_ntime_t* nt, hio_bch_t* buf, hio_oow_t bufsz)
{
	time_t t;
	struct tm bt;

	t = nt->sec;
	gmtime_r (&t, &bt); /* TODO: create hio_sys_gmtime() and make it system dependent */

	hio_fmttobcstr (HIO_NULL, buf, bufsz, 
		"%hs, %d %hs %d %02d:%02d:%02d GMT",
		wday_name[bt.tm_wday].s,
		bt.tm_mday,
		mon_name[bt.tm_mon].s,
		bt.tm_year + 1900,
		bt.tm_hour, bt.tm_min, bt.tm_sec
	);

	return buf;
}

int hio_is_perenced_http_bcstr (const hio_bch_t* str)
{
	const hio_bch_t* p = str;

	while (*p != '\0')
	{
		if (*p == '%' && *(p + 1) != '\0' && *(p + 2) != '\0')
		{
			int q = HIO_XDIGIT_TO_NUM(*(p + 1));
			if (q >= 0)
			{
				/* return true if the first valid percent-encoded sequence is found */
				int w = HIO_XDIGIT_TO_NUM(*(p + 2));
				if (w >= 0) return 1; 
			}
		}

		p++;
	}

	return 1;
}

hio_oow_t hio_perdec_http_bcstr (const hio_bch_t* str, hio_bch_t* buf, hio_oow_t* ndecs)
{
	const hio_bch_t* p = str;
	hio_bch_t* out = buf;
	hio_oow_t dec_count = 0;

	while (*p != '\0')
	{
		if (*p == '%' && *(p + 1) != '\0' && *(p + 2) != '\0')
		{
			int q = HIO_XDIGIT_TO_NUM(*(p + 1));
			if (q >= 0)
			{
				int w = HIO_XDIGIT_TO_NUM(*(p + 2));
				if (w >= 0)
				{
					/* we don't care if it contains a null character */
					*out++ = ((q << 4) + w);
					p += 3;
					dec_count++;
					continue;
				}
			}
		}

		*out++ = *p++;
	}

	*out = '\0';
	if (ndecs) *ndecs = dec_count;
	return out - buf;
}

hio_oow_t hio_perdec_http_bcs (const hio_bcs_t* str, hio_bch_t* buf, hio_oow_t* ndecs)
{
	const hio_bch_t* p = str->ptr;
	const hio_bch_t* end = str->ptr + str->len;
	hio_bch_t* out = buf;
	hio_oow_t dec_count = 0;

	while (p < end)
	{
		if (*p == '%' && (p + 2) < end)
		{
			int q = HIO_XDIGIT_TO_NUM(*(p + 1));
			if (q >= 0)
			{
				int w = HIO_XDIGIT_TO_NUM(*(p + 2));
				if (w >= 0)
				{
					/* we don't care if it contains a null character */
					*out++ = ((q << 4) + w);
					p += 3;
					dec_count++;
					continue;
				}
			}
		}

		*out++ = *p++;
	}

	/* [NOTE] this function deesn't insert '\0' at the end */

	if (ndecs) *ndecs = dec_count;
	return out - buf;
}

#define IS_UNRESERVED(c) \
	(((c) >= 'A' && (c) <= 'Z') || \
	 ((c) >= 'a' && (c) <= 'z') || \
	 ((c) >= '0' && (c) <= '9') || \
	 (c) == '-' || (c) == '_' || \
	 (c) == '.' || (c) == '~')

#define TO_HEX(v) ("0123456789ABCDEF"[(v) & 15])

hio_oow_t hio_perenc_http_bcstr (int opt, const hio_bch_t* str, hio_bch_t* buf, hio_oow_t* nencs)
{
	const hio_bch_t* p = str;
	hio_bch_t* out = buf;
	hio_oow_t enc_count = 0;

	/* this function doesn't accept the size of the buffer. the caller must 
	 * ensure that the buffer is large enough */

	if (opt & HIO_PERENC_HTTP_KEEP_SLASH)
	{
		while (*p != '\0')
		{
			if (IS_UNRESERVED(*p) || *p == '/') *out++ = *p;
			else
			{
				*out++ = '%';
				*out++ = TO_HEX (*p >> 4);
				*out++ = TO_HEX (*p & 15);
				enc_count++;
			}
			p++;
		}
	}
	else
	{
		while (*p != '\0')
		{
			if (IS_UNRESERVED(*p)) *out++ = *p;
			else
			{
				*out++ = '%';
				*out++ = TO_HEX (*p >> 4);
				*out++ = TO_HEX (*p & 15);
				enc_count++;
			}
			p++;
		}
	}
	*out = '\0';
	if (nencs) *nencs = enc_count;
	return out - buf;
}

#if 0
hio_bch_t* hio_perenc_http_bcstrdup (int opt, const hio_bch_t* str, hio_mmgr_t* mmgr)
{
	hio_bch_t* buf;
	hio_oow_t len = 0;
	hio_oow_t count = 0;
	
	/* count the number of characters that should be encoded */
	if (opt & HIO_PERENC_HTTP_KEEP_SLASH)
	{
		for (len = 0; str[len] != '\0'; len++)
		{
			if (!IS_UNRESERVED(str[len]) && str[len] != '/') count++;
		}
	}
	else
	{
		for (len = 0; str[len] != '\0'; len++)
		{
			if (!IS_UNRESERVED(str[len])) count++;
		}
	}

	/* if there are no characters to escape, just return the original string */
	if (count <= 0) return (hio_bch_t*)str;

	/* allocate a buffer of an optimal size for escaping, otherwise */
	buf = HIO_MMGR_ALLOC(mmgr, (len  + (count * 2) + 1)  * HIO_SIZEOF(*buf));
	if (!buf) return HIO_NULL;

	/* perform actual escaping */
	hio_perenc_http_bcstr (opt, str, buf, HIO_NULL);

	return buf;
}
#endif


int hio_scan_http_qparam (hio_bch_t* qparam, int (*qparamcb) (hio_bcs_t* key, hio_bcs_t* val, void* ctx), void* ctx)
{
	hio_bcs_t key, val;
	hio_bch_t* p, * end;

	p = qparam;
	end = p + hio_count_bcstr(qparam);

	key.ptr = p; key.len = 0;
	val.ptr = HIO_NULL; val.len = 0;

	do
	{
		if (p >= end || *p == '&' || *p == ';')
		{
			if (val.ptr)
			{
				val.len = p - val.ptr;
			}
			else
			{
				key.len = p - key.ptr;
				if (key.len == 0) goto next_octet; /* both key and value are empty. we don't need to do anything */
			}

			/* set request parameter string callback before scanning */
			if (qparamcb(&key, &val, ctx) <= -1) return -1;

		next_octet:
			if (p >= end) break;
			p++;

			key.ptr = p; key.len = 0;
			val.ptr = HIO_NULL; val.len = 0;
		}
		else if (*p == '=')
		{
			if (!val.ptr)
			{
				key.len = p - key.ptr;
				val.ptr = ++p;
				/*val.len = 0; */
			}
			else
			{
				p++;
			}
		}
		else
		{
			p++;
		}
	}
	while (1);

	return 0;
}
