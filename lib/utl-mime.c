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

#include <hio-utl.h>

/* Keep this table sorted for binary search */
static hio_mime_type_t mime_type_tab[] =
{
	{"3gp", "video/3gpp"},
	{"3g2", "video/3gpp2"},
	{"7z", "application/x-7z-compressed"},
	{"aac", "audio/aac"},
	{"abw", "application/x-abiword"},
	{"arc", "application/x-freearc"},
	{"avif", "image/avif"},
	{"avi", "video/x-msvideo"},
	{"azw", "application/vnd.amazon.ebook"},
	{"bin", "application/octet-stream"},
	{"bmp", "image/bmp"},
	{"bz", "application/x-bzip"},
	{"bz2", "application/x-bzip2"},
	{"cda", "application/x-cdf"},
	{"csh", "application/x-csh"},
	{"css", "text/css"},
	{"csv", "text/csv"},
	{"doc", "application/msword"},
	{"docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
	{"eot", "application/vnd.ms-fontobject"},
	{"epub", "application/epub+zip"},
	{"gz", "application/gzip"},
	{"gif", "image/gif"},
	{"htm", "text/html"},
	{"html", "text/html"},
	{"ico", "image/vnd.microsoft.icon"},
	{"ics", "text/calendar"},
	{"jar", "application/java-archive"},
	{"jpeg", "image/jpeg"},
	{"jpg", "image/jpeg"},
	{"js", "text/javascript"},
	{"json", "application/json"},
	{"jsonld", "application/ld+json"},
	{"mid", "audio/midi"},
	{"midi", "audio/midi"},
	{"mjs", "text/javascript"},
	{"mp3", "audio/mpeg"},
	{"mp4", "video/mp4"},
	{"mpeg", "video/mpeg"},
	{"mpkg", "application/vnd.apple.installer+xml"},
	{"odp", "application/vnd.oasis.opendocument.presentation"},
	{"ods", "application/vnd.oasis.opendocument.spreadsheet"},
	{"odt", "application/vnd.oasis.opendocument.text"},
	{"oga", "audio/ogg"},
	{"ogv", "video/ogg"},
	{"ogx", "application/ogg"},
	{"opus", "audio/opus"},
	{"otf", "font/otf"},
	{"png", "image/png"},
	{"pdf", "application/pdf"},
	{"php", "application/x-httpd-php"},
	{"ppt", "application/vnd.ms-powerpoint"},
	{"pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation"},
	{"rar", "application/vnd.rar"},
	{"rtf", "application/rtf"},
	{"sh", "application/x-sh"},
	{"svg", "image/svg+xml"},
	{"tar", "application/x-tar"},
	{"tif", "image/tiff"},
	{"tiff", "image/tiff"},
	{"ts", "video/mp2t"},
	{"ttf", "font/ttf"},
	{"txt", "text/plain"},
	{"vsd", "application/vnd.visio"},
	{"wav", "audio/wav"},
	{"weba", "audio/webm"},
	{"webm", "video/webm"},
	{"webp", "image/webp"},
	{"woff", "font/woff"},
	{"woff2", "font/woff2"},
	{"xhtml", "application/xhtml+xml"},
	{"xls", "application/vnd.ms-excel"},
	{"xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
	{"xml", "application/xml"},
	{"xul", "application/vnd.mozilla.xul+xml"},
	{"yaml", "application/x-yaml"},
	{"yml", "application/x-yaml"},
	{"zip", "application/zip"}
};

const hio_bch_t* hio_get_mime_type_by_ext(const hio_bch_t* ext)
{
	/* perform binary search */

	/* declaring left, right, mid to be of int is ok
	 * because we know mtab is small enough. */
	int left = 0, right = HIO_COUNTOF(mime_type_tab) - 1, mid;

	while (left <= right)
	{
		int n;
		hio_mime_type_t* entry;

		/*mid = (left + right) / 2;*/
		mid = left + (right - left) / 2;
		entry = &mime_type_tab[mid];

		n = hio_comp_bcstr(ext, entry->ext, 1);
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

	return HIO_NULL;
}
