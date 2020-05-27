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

#include <mio-http.h>
#include <mio-chr.h>
#include <mio-utl.h>
#include "mio-prv.h"
#include <time.h>

int mio_comp_http_versions (const mio_http_version_t* v1, const mio_http_version_t* v2)
{
	if (v1->major == v2->major) return v1->minor - v2->minor;
	return v1->major - v2->major;
}

int mio_comp_http_version_numbers (const mio_http_version_t* v1, int v2_major, int v2_minor)
{
	if (v1->major == v2_major) return v1->minor - v2_minor;
	return v1->major - v2_major;
}

const mio_bch_t* mio_http_status_to_bcstr (int code)
{
	const mio_bch_t* msg;

	switch (code)
	{
		case 100: msg = "Continue"; break;
		case 101: msg = "Switching Protocols"; break;

		case 200: msg = "OK"; break;
		case 201: msg = "Created"; break;
		case 202: msg = "Accepted"; break;
		case 203: msg = "Non-Authoritative Information"; break;
		case 204: msg = "No Content"; break;
		case 205: msg = "Reset Content"; break;
		case 206: msg = "Partial Content"; break;
		
		case 300: msg = "Multiple Choices"; break;
		case 301: msg = "Moved Permanently"; break;
		case 302: msg = "Found"; break;
		case 303: msg = "See Other"; break;
		case 304: msg = "Not Modified"; break;
		case 305: msg = "Use Proxy"; break;
		case 307: msg = "Temporary Redirect"; break;
		case 308: msg = "Permanent Redirect"; break;

		case 400: msg = "Bad Request"; break;
		case 401: msg = "Unauthorized"; break;
		case 402: msg = "Payment Required"; break;
		case 403: msg = "Forbidden"; break;
		case 404: msg = "Not Found"; break;
		case 405: msg = "Method Not Allowed"; break;
		case 406: msg = "Not Acceptable"; break;
		case 407: msg = "Proxy Authentication Required"; break;
		case 408: msg = "Request Timeout"; break;
		case 409: msg = "Conflict"; break;
		case 410: msg = "Gone"; break;
		case 411: msg = "Length Required"; break;
		case 412: msg = "Precondition Failed"; break;
		case 413: msg = "Request Entity Too Large"; break;
		case 414: msg = "Request-URI Too Long"; break;
		case 415: msg = "Unsupported Media Type"; break;
		case 416: msg = "Requested Range Not Satisfiable"; break;
		case 417: msg = "Expectation Failed"; break;
		case 426: msg = "Upgrade Required"; break;
		case 428: msg = "Precondition Required"; break;
		case 429: msg = "Too Many Requests"; break;
		case 431: msg = "Request Header Fields Too Large"; break;

		case 500: msg = "Internal Server Error"; break;
		case 501: msg = "Not Implemented"; break;
		case 502: msg = "Bad Gateway"; break;
		case 503: msg = "Service Unavailable"; break;
		case 504: msg = "Gateway Timeout"; break;
		case 505: msg = "HTTP Version Not Supported"; break;

		default: msg = "Unknown Error"; break;
	}

	return msg;
}

const mio_bch_t* mio_http_method_to_bcstr (mio_http_method_t type)
{
	/* keep this table in the same order as mio_httpd_method_t enumerators */
	static mio_bch_t* names[]  =
	{
		"OTHER",

		"HEAD",
		"GET",
		"POST",
		"PUT",
		"DELETE",
		"OPTIONS",
		"TRACE",
		"CONNECT"
	}; 

	return (type < 0 || type >= MIO_COUNTOF(names))? MIO_NULL: names[type];
}

struct mtab_t
{
	const mio_bch_t* name;
	mio_http_method_t type;
};

static struct mtab_t mtab[] =
{
	/* keep this table sorted by name for binary search */
	{ "CONNECT", MIO_HTTP_CONNECT },
	{ "DELETE",  MIO_HTTP_DELETE },
	{ "GET",     MIO_HTTP_GET },
	{ "HEAD",    MIO_HTTP_HEAD },
	{ "OPTIONS", MIO_HTTP_OPTIONS },
	{ "POST",    MIO_HTTP_POST },
	{ "PUT",     MIO_HTTP_PUT },
	{ "TRACE",   MIO_HTTP_TRACE }
};

mio_http_method_t mio_bcstr_to_http_method (const mio_bch_t* name)
{
	/* perform binary search */

	/* declaring left, right, mid to be of int is ok
	 * because we know mtab is small enough. */
	int left = 0, right = MIO_COUNTOF(mtab) - 1, mid;

	while (left <= right)
	{
		int n;
		struct mtab_t* entry;

		/*mid = (left + right) / 2;*/
		mid = left + (right - left) / 2;
		entry = &mtab[mid];

		n = mio_comp_bcstr(name, entry->name, 1);
		if (n < 0) 
		{
			/* if left, right, mid were of mio_oow_t,
			 * you would need the following line. 
			if (mid == 0) break;
			 */
			right = mid - 1;
		}
		else if (n > 0) left = mid + 1;
		else return entry->type;
	}

	return MIO_HTTP_OTHER;
}

mio_http_method_t mio_bchars_to_http_method (const mio_bch_t* nameptr, mio_oow_t namelen)
{
	/* perform binary search */

	/* declaring left, right, mid to be of int is ok
	 * because we know mtab is small enough. */
	int left = 0, right = MIO_COUNTOF(mtab) - 1, mid;

	while (left <= right)
	{
		int n;
		struct mtab_t* entry;

		/*mid = (left + right) / 2;*/
		mid = left + (right - left) / 2;
		entry = &mtab[mid];

		n = mio_comp_bchars_bcstr(nameptr, namelen, entry->name, 1);
		if (n < 0) 
		{
			/* if left, right, mid were of mio_oow_t,
			 * you would need the following line. 
			if (mid == 0) break;
			 */
			right = mid - 1;
		}
		else if (n > 0) left = mid + 1;
		else return entry->type;
	}

	return MIO_HTTP_OTHER;
}

int mio_parse_http_range_bcstr (const mio_bch_t* str, mio_http_range_t* range)
{
	/* NOTE: this function does not support a range set 
	 *       like bytes=1-20,30-50 */

	mio_http_range_int_t from, to;
	int type = MIO_HTTP_RANGE_PROPER;

	if (str[0] != 'b' ||
	    str[1] != 'y' ||
	    str[2] != 't' ||
	    str[3] != 'e' ||
	    str[4] != 's' ||
	    str[5] != '=') return -1;
	
	str += 6;

	from = 0;
	if (mio_is_bch_digit(*str))
	{
		do
		{
			from = from * 10 + (*str - '0');
			str++;
		}
		while (mio_is_bch_digit(*str));
	}
	else type = MIO_HTTP_RANGE_SUFFIX;

	if (*str != '-') return -1;
	str++;

	if (mio_is_bch_digit(*str))
	{
		to = 0;
		do
		{
			to = to * 10 + (*str - '0');
			str++;
		}
		while (mio_is_bch_digit(*str));
	}
	else to = MIO_TYPE_MAX(mio_http_range_int_t); 

	if (from > to) return -1;

	range->type = type;
	range->from = from;
	range->to = to;
	return 0;
}

typedef struct mname_t mname_t;
struct mname_t
{
	const mio_bch_t* s;
	const mio_bch_t* l;
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

int mio_parse_http_time_bcstr (const mio_bch_t* str, mio_ntime_t* nt)
{
	struct tm bt;
	const mio_bch_t* word;
	mio_oow_t wlen, i;

	/* TODO: support more formats */

	MIO_MEMSET (&bt, 0, MIO_SIZEOF(bt));

	/* weekday */
	while (mio_is_bch_space(*str)) str++;
	for (word = str; mio_is_bch_alpha(*str); str++);
	wlen = str - word;
	for (i = 0; i < MIO_COUNTOF(wday_name); i++)
	{
		if (mio_comp_bchars_bcstr(word, wlen, wday_name[i].s, 1) == 0)
		{
			bt.tm_wday = i;
			break;
		}
	}
	if (i >= MIO_COUNTOF(wday_name)) return -1;

	/* comma - i'm just loose as i don't care if it doesn't exist */
	while (mio_is_bch_space(*str)) str++;
	if (*str == ',') str++;

	/* day */
	while (mio_is_bch_space(*str)) str++;
	if (!mio_is_bch_digit(*str)) return -1;
	do bt.tm_mday = bt.tm_mday * 10 + *str++ - '0'; while (mio_is_bch_digit(*str));

	/* month */
	while (mio_is_bch_space(*str)) str++;
	for (word = str; mio_is_bch_alpha(*str); str++);
	wlen = str - word;
	for (i = 0; i < MIO_COUNTOF(mon_name); i++)
	{
		if (mio_comp_bchars_bcstr(word, wlen, mon_name[i].s, 1) == 0)
		{
			bt.tm_mon = i;
			break;
		}
	}
	if (i >= MIO_COUNTOF(mon_name)) return -1;

	/* year */
	while (mio_is_bch_space(*str)) str++;
	if (!mio_is_bch_digit(*str)) return -1;
	do bt.tm_year = bt.tm_year * 10 + *str++ - '0'; while (mio_is_bch_digit(*str));
	bt.tm_year -= 1900;

	/* hour */
	while (mio_is_bch_space(*str)) str++;
	if (!mio_is_bch_digit(*str)) return -1;
	do bt.tm_hour = bt.tm_hour * 10 + *str++ - '0'; while (mio_is_bch_digit(*str));
	if (*str != ':')  return -1;
	str++;

	/* min */
	while (mio_is_bch_space(*str)) str++;
	if (!mio_is_bch_digit(*str)) return -1;
	do bt.tm_min = bt.tm_min * 10 + *str++ - '0'; while (mio_is_bch_digit(*str));
	if (*str != ':')  return -1;
	str++;

	/* sec */
	while (mio_is_bch_space(*str)) str++;
	if (!mio_is_bch_digit(*str)) return -1;
	do bt.tm_sec = bt.tm_sec * 10 + *str++ - '0'; while (mio_is_bch_digit(*str));

	/* GMT */
	while (mio_is_bch_space(*str)) str++;
	for (word = str; mio_is_bch_alpha(*str); str++);
	wlen = str - word;
	if (mio_comp_bchars_bcstr(word, wlen, "GMT", 1) != 0) return -1;

	while (mio_is_bch_space(*str)) str++;
	if (*str != '\0') return -1;

	nt->sec = timegm(&bt);
	nt->nsec = 0;

	return 0;
}

mio_bch_t* mio_fmt_http_time_to_bcstr (const mio_ntime_t* nt, mio_bch_t* buf, mio_oow_t bufsz)
{
	time_t t;
	struct tm bt;

	t = nt->sec;
	gmtime_r (&t, &bt); /* TODO: create mio_sys_gmtime() and make it system dependent */

	mio_fmttobcstr (MIO_NULL, buf, bufsz, 
		"%hs, %d %hs %d %02d:%02d:%02d GMT",
		wday_name[bt.tm_wday].s,
		bt.tm_mday,
		mon_name[bt.tm_mon].s,
		bt.tm_year + 1900,
		bt.tm_hour, bt.tm_min, bt.tm_sec
	);

	return buf;
}

int mio_is_perenced_http_bcstr (const mio_bch_t* str)
{
	const mio_bch_t* p = str;

	while (*p != '\0')
	{
		if (*p == '%' && *(p + 1) != '\0' && *(p + 2) != '\0')
		{
			int q = MIO_XDIGIT_TO_NUM(*(p + 1));
			if (q >= 0)
			{
				/* return true if the first valid percent-encoded sequence is found */
				int w = MIO_XDIGIT_TO_NUM(*(p + 2));
				if (w >= 0) return 1; 
			}
		}

		p++;
	}

	return 1;
}

mio_oow_t mio_perdec_http_bcstr (const mio_bch_t* str, mio_bch_t* buf, mio_oow_t* ndecs)
{
	const mio_bch_t* p = str;
	mio_bch_t* out = buf;
	mio_oow_t dec_count = 0;

	while (*p != '\0')
	{
		if (*p == '%' && *(p + 1) != '\0' && *(p + 2) != '\0')
		{
			int q = MIO_XDIGIT_TO_NUM(*(p + 1));
			if (q >= 0)
			{
				int w = MIO_XDIGIT_TO_NUM(*(p + 2));
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

mio_oow_t mio_perdec_http_bcs (const mio_bcs_t* str, mio_bch_t* buf, mio_oow_t* ndecs)
{
	const mio_bch_t* p = str->ptr;
	const mio_bch_t* end = str->ptr + str->len;
	mio_bch_t* out = buf;
	mio_oow_t dec_count = 0;

	while (p < end)
	{
		if (*p == '%' && (p + 2) < end)
		{
			int q = MIO_XDIGIT_TO_NUM(*(p + 1));
			if (q >= 0)
			{
				int w = MIO_XDIGIT_TO_NUM(*(p + 2));
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

mio_oow_t mio_perenc_http_bcstr (int opt, const mio_bch_t* str, mio_bch_t* buf, mio_oow_t* nencs)
{
	const mio_bch_t* p = str;
	mio_bch_t* out = buf;
	mio_oow_t enc_count = 0;

	/* this function doesn't accept the size of the buffer. the caller must 
	 * ensure that the buffer is large enough */

	if (opt & MIO_PERENC_HTTP_KEEP_SLASH)
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
mio_bch_t* mio_perenc_http_bcstrdup (int opt, const mio_bch_t* str, mio_mmgr_t* mmgr)
{
	mio_bch_t* buf;
	mio_oow_t len = 0;
	mio_oow_t count = 0;
	
	/* count the number of characters that should be encoded */
	if (opt & MIO_PERENC_HTTP_KEEP_SLASH)
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
	if (count <= 0) return (mio_bch_t*)str;

	/* allocate a buffer of an optimal size for escaping, otherwise */
	buf = MIO_MMGR_ALLOC(mmgr, (len  + (count * 2) + 1)  * MIO_SIZEOF(*buf));
	if (!buf) return MIO_NULL;

	/* perform actual escaping */
	mio_perenc_http_bcstr (opt, str, buf, MIO_NULL);

	return buf;
}
#endif


int mio_scan_http_qparam (mio_bch_t* qparam, int (*qparamcb) (mio_bcs_t* key, mio_bcs_t* val, void* ctx), void* ctx)
{
	mio_bcs_t key, val;
	mio_bch_t* p, * end;

	p = qparam;
	end = p + mio_count_bcstr(qparam);

	key.ptr = p; key.len = 0;
	val.ptr = MIO_NULL; val.len = 0;

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
			val.ptr = MIO_NULL; val.len = 0;
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
