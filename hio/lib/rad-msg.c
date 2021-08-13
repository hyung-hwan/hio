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

#include <hio-rad.h>
#include <hio-md5.h>
#include "hio-prv.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdlib.h>

/* TODO: tlv and various value encoding.
         radius dictionary support
	    change hio_rad_walk_attributes() to hio_rad_walk_attrs() with enhancement 
	    finish long attribute insertion in hio_rad_insert_attr... break long data to multiple attrs 
*/

void hio_rad_initialize (hio_rad_hdr_t* hdr, hio_rad_code_t code, hio_uint8_t id)
{
	HIO_MEMSET (hdr, 0, HIO_SIZEOF(*hdr));
	hdr->code = code;
	hdr->id = id;
	hdr->length = hio_hton16(HIO_SIZEOF(*hdr));
}

static HIO_INLINE void xor (void* p, void* q, int length)
{
	int i;
	hio_uint8_t* pp = (hio_uint8_t*)p;
	hio_uint8_t* qq = (hio_uint8_t*)q;
	for (i = 0; i < length; i++) *(pp++) ^= *(qq++);
}

static void fill_authenticator_randomly (void* authenticator, int length)
{
	hio_uint8_t* v = (hio_uint8_t*)authenticator;
	int total = 0;
	int fd;

	fd = open("/dev/urandom", O_RDONLY, 0);
	if (fd >= 0) 
	{
		while (total < length) 
		{
			int bytes = read(fd, &v[total], length - total);
			if (bytes <= 0) break;
			total += bytes;
		}
		close (fd);
	}

	if (total < length) 
	{
		struct timeval now;
		unsigned int seed;

		gettimeofday (&now, HIO_NULL);
		seed = getpid() + now.tv_sec + now.tv_usec;
		srandom (seed);

		while (total < length) 
		{
			seed = random();
			v[total] = seed % HIO_TYPE_MAX(hio_uint8_t);
			total++;
		}
	}
}

int hio_rad_walk_attributes (const hio_rad_hdr_t* hdr, hio_rad_attr_walker_t walker, void* ctx)
{
	int totlen, rem;
	hio_rad_attr_hdr_t* attr;

	totlen = hio_ntoh16(hdr->length);
	if (totlen < HIO_SIZEOF(*hdr)) return -1;

	rem = totlen - HIO_SIZEOF(*hdr);
	attr = (hio_rad_attr_hdr_t*)(hdr + 1);
	while (rem >= HIO_SIZEOF(*attr))
	{
		/* sanity checks */
		if (rem < attr->length) return -1;
		if (attr->length < HIO_SIZEOF(*attr)) 
		{
			/* attribute length cannot be less than the header size.
			 * the packet could be corrupted... */
			return -1;
		}

		rem -= attr->length;

		if (attr->type == HIO_RAD_ATTR_VENDOR_SPECIFIC)
		{
			hio_rad_vsattr_hdr_t* vsattr;
			int val_len;

			if (attr->length < HIO_SIZEOF(*vsattr)) return -1;
			vsattr = (hio_rad_vsattr_hdr_t*)attr;

			val_len = (int)vsattr->length - HIO_SIZEOF(*vsattr);
			if ((int)vsattr->vs.length != val_len + HIO_SIZEOF(vsattr->vs)) return -1;

			/* if this vendor happens to be 0, walker can't tell
			 * if it is vendor specific or not because 0 is passed in
			 * for non-VSAs. but i don't care. in reality, 
			 * 0 is reserved in IANA enterpirse number assignments.
			 * (http://www.iana.org/assignments/enterprise-numbers) */
			if (walker(hdr, hio_ntoh32(vsattr->vendor), &vsattr->vs, ctx) <= -1) return -1;
		}
		else
		{
			if (walker(hdr, 0, attr, ctx) <= -1) return -1;
		}

		attr = (hio_rad_attr_hdr_t*)((hio_uint8_t*) attr + attr->length);
	}

	return 0;
}

/* ---------------------------------------------------------------- */

static hio_rad_attr_hdr_t* find_attribute (hio_rad_attr_hdr_t* attr, int* len, hio_uint8_t attrtype)
{
	int rem = *len;

	while (rem >= HIO_SIZEOF(*attr))
	{
		/* sanity checks */
		if (rem < attr->length) return HIO_NULL;
		if (attr->length < HIO_SIZEOF(*attr)) 
		{
			/* attribute length cannot be less than the header size.
			 * the packet could be corrupted... */
			return HIO_NULL; 
		}

		rem -= attr->length;
		if (attr->type == attrtype) 
		{
			*len = rem; /* remaining length */
			return attr;
		}

		attr = (hio_rad_attr_hdr_t*)((hio_uint8_t*)attr + attr->length);
	}

	return HIO_NULL;
}

static hio_rad_attr_hdr_t* find_extended_attribute (hio_rad_attr_hdr_t* attr, int* len, hio_uint8_t xtype, hio_uint8_t attrtype)
{
	int rem = *len;

	/* xtype must be one of the followings:
	 *   HIO_RAD_ATTR_EXTENDED_1
	 *   HIO_RAD_ATTR_EXTENDED_2
	 *   HIO_RAD_ATTR_EXTENDED_3
	 *   HIO_RAD_ATTR_EXTENDED_4
	 *   HIO_RAD_ATTR_EXTENDED_5
	 *   HIO_RAD_ATTR_EXTENDED_6
	 */

	while (rem >= HIO_SIZEOF(*attr))
	{
		/* sanity checks */
		if (rem < attr->length) return HIO_NULL;

		/* attribute length cannot be less than the header size.
		 * the packet could be corrupted... */
		if (attr->length < HIO_SIZEOF(*attr)) goto oops;

		rem -= attr->length;
		if (attr->type == xtype) 
		{
			hio_uint8_t xattrtype;

			if (HIO_RAD_ATTR_IS_LONG_EXTENDED(xtype))
			{
				hio_rad_lxattr_hdr_t* lxattr;
				lxattr = (hio_rad_lxattr_hdr_t*)attr;
				if (lxattr->length < HIO_SIZEOF(*lxattr)) goto oops;
				xattrtype = lxattr->xtype;
			}
			else
			{
				hio_rad_xattr_hdr_t* xattr;
				xattr = (hio_rad_xattr_hdr_t*)attr;
				if (xattr->length < HIO_SIZEOF(*xattr)) goto oops;
				xattrtype = xattr->xtype;
			}

			if (xattrtype == attrtype)
			{
				*len = rem;
				return attr;
			}
		}

		attr = (hio_rad_attr_hdr_t*)((hio_uint8_t*)attr + attr->length);
	}

oops:
	return HIO_NULL;
}

hio_rad_attr_hdr_t* hio_rad_find_attribute (hio_rad_hdr_t* hdr, hio_uint8_t attrtype, int index)
{
	hio_rad_attr_hdr_t *attr = (hio_rad_attr_hdr_t*)(hdr+1);

	if (hio_ntoh16(hdr->length) >= HIO_SIZEOF(*hdr))
	{
		int len = hio_ntoh16(hdr->length) - HIO_SIZEOF(*hdr);
		attr = find_attribute(attr, &len, attrtype);
		while (attr)
		{
			if (index <= 0) return attr;
			index--;
			attr = find_attribute((hio_rad_attr_hdr_t*)((hio_uint8_t*)attr+attr->length), &len, attrtype);
		}
	}

	return HIO_NULL;
}

hio_rad_xattr_hdr_t* hio_rad_find_extended_attribute (hio_rad_hdr_t* hdr, hio_uint8_t xtype, hio_uint8_t attrtype, int index)
{
	hio_rad_attr_hdr_t *attr = (hio_rad_attr_hdr_t*)(hdr + 1);

	if (HIO_RAD_ATTR_IS_EXTENDED(xtype) && hio_ntoh16(hdr->length) >= HIO_SIZEOF(*hdr))
	{
		int len = hio_ntoh16(hdr->length) - HIO_SIZEOF(*hdr);
		attr = find_extended_attribute(attr, &len, xtype, attrtype);
		while (attr)
		{
			if (index <= 0) return (hio_rad_xattr_hdr_t*)attr;
			index--;
			attr = find_extended_attribute((hio_rad_attr_hdr_t*)((hio_uint8_t*)attr + attr->length), &len, xtype, attrtype);
		}
	}

	return HIO_NULL;
}

hio_rad_vsattr_hdr_t* hio_rad_find_vendor_specific_attribute (hio_rad_hdr_t* hdr, hio_uint32_t vendor, hio_uint8_t attrtype, int index)
{
	hio_rad_attr_hdr_t *attr = (hio_rad_attr_hdr_t*)(hdr+1);

	if (hio_ntoh16(hdr->length) >= HIO_SIZEOF(*hdr))
	{
		int len = hio_ntoh16(hdr->length) - HIO_SIZEOF(*hdr);

		attr = find_attribute(attr, &len, HIO_RAD_ATTR_VENDOR_SPECIFIC);
		while (attr)
		{
			hio_rad_vsattr_hdr_t* vsattr;
	
			if (attr->length >= HIO_SIZEOF(*vsattr)) /* sanity check */
			{
				vsattr = (hio_rad_vsattr_hdr_t*)attr;
	
				if (hio_ntoh32(vsattr->vendor) == vendor && vsattr->vs.type == attrtype)
				{
					int val_len;
	
					val_len = (int)vsattr->length - HIO_SIZEOF(*vsattr);
	
					if ((int)vsattr->vs.length == val_len + HIO_SIZEOF(vsattr->vs)) 
					{
						if (index <= 0) return vsattr;
						index--;
					}
				}
			}
	
			attr = find_attribute((hio_rad_attr_hdr_t*)((hio_uint8_t*)attr + attr->length), &len, HIO_RAD_ATTR_VENDOR_SPECIFIC);
		}
	}

	return HIO_NULL;
}


hio_rad_xvsattr_hdr_t* hio_rad_find_extended_vendor_specific_attribute (hio_rad_hdr_t* hdr, hio_uint32_t vendor, hio_uint8_t xtype, hio_uint8_t attrtype, int index)
{
	hio_rad_attr_hdr_t *attr = (hio_rad_attr_hdr_t*)(hdr+1);

	if (HIO_RAD_ATTR_IS_EXTENDED(xtype) && hio_ntoh16(hdr->length) >= HIO_SIZEOF(*hdr))
	{
		int len = hio_ntoh16(hdr->length) - HIO_SIZEOF(*hdr);

		attr = find_extended_attribute(attr, &len, xtype, HIO_RAD_ATTR_VENDOR_SPECIFIC);
		while (attr)
		{
			if (HIO_RAD_ATTR_IS_LONG_EXTENDED(xtype))
			{
				hio_rad_lxvsattr_hdr_t* lxvsattr;
				if (attr->length >= HIO_SIZEOF(*lxvsattr)) /* sanity check */
				{
					lxvsattr = (hio_rad_lxvsattr_hdr_t*)attr;
	
					if (hio_ntoh32(lxvsattr->vendor) == vendor && lxvsattr->lxvs.type == attrtype)
					{
						int val_len;
	
						val_len = (int)lxvsattr->length - HIO_SIZEOF(*lxvsattr);
		
						if ((int)lxvsattr->lxvs.length == val_len + HIO_SIZEOF(lxvsattr->lxvs)) 
						{
							/* the caller must check if the extended type is long. 
							 * if long, it must cast back to hio_rad_lxvsattr_hdr_t* */
							if (index <= 0) return (hio_rad_xvsattr_hdr_t*)lxvsattr;
							index--;
						}
					}
				}
			}
			else
			{	
				hio_rad_xvsattr_hdr_t* xvsattr;
				if (attr->length >= HIO_SIZEOF(*xvsattr)) /* sanity check */
				{
					xvsattr = (hio_rad_xvsattr_hdr_t*)attr;
	
					if (hio_ntoh32(xvsattr->vendor) == vendor && xvsattr->xvs.type == attrtype)
					{
						int val_len;
	
						val_len = (int)xvsattr->length - HIO_SIZEOF(*xvsattr);
		
						if ((int)xvsattr->xvs.length == val_len + HIO_SIZEOF(xvsattr->xvs)) 
						{
							if (index <= 0) return xvsattr;
							index--;
						}
					}
				}
			}
	
			attr = find_extended_attribute((hio_rad_attr_hdr_t*)((hio_uint8_t*)attr + attr->length), &len, xtype, HIO_RAD_ATTR_VENDOR_SPECIFIC);
		}
	}

	return HIO_NULL;
}

/* ---------------------------------------------------------------- */

static int delete_attribute (hio_rad_hdr_t* auth, hio_rad_attr_hdr_t* attr)
{
	hio_uint16_t auth_len;
	hio_uint16_t tmp_len;

	auth_len = hio_ntoh16(auth->length);
	tmp_len = ((hio_uint8_t*)attr - (hio_uint8_t*)auth) + attr->length;
	if (tmp_len > auth_len) return -1; /* can this happen? */

	HIO_MEMCPY (attr, (hio_uint8_t*)attr + attr->length, auth_len - tmp_len);

	auth_len -= attr->length;
	auth->length = hio_hton16(auth_len);
	return 0;
}

int hio_rad_delete_attribute (hio_rad_hdr_t* auth, hio_uint8_t attrtype, int index)
{
	hio_rad_attr_hdr_t* attr;

	attr = hio_rad_find_attribute(auth, attrtype, index);
	if (!attr) return 0; /* not found */
	return (delete_attribute(auth, attr) <= -1)? -1: 1;
}

int hio_rad_delete_extended_attribute (hio_rad_hdr_t* auth, hio_uint8_t xtype, hio_uint8_t attrtype, int index)
{
	hio_rad_xattr_hdr_t* attr;

	attr = hio_rad_find_extended_attribute(auth, xtype, attrtype, index);
	if (!attr) return 0; /* not found */
	return (delete_attribute(auth, (hio_rad_attr_hdr_t*)attr) <= -1)? -1: 1;
}

int hio_rad_delete_vendor_specific_attribute (
	hio_rad_hdr_t* auth, hio_uint32_t vendor, hio_uint8_t attrtype, int index)
{
	hio_rad_vsattr_hdr_t* vsattr;

	vsattr = hio_rad_find_vendor_specific_attribute(auth, vendor, attrtype, 0);
	if (!vsattr) return 0; /* not found */
	return (delete_attribute(auth, (hio_rad_attr_hdr_t*)vsattr) <= -1)? -1: 1;
}

int hio_rad_delete_extended_vendor_specific_attribute (
        hio_rad_hdr_t*  auth, hio_uint32_t vendor, hio_uint8_t xtype, hio_uint8_t attrtype, int index)
{
	hio_rad_xvsattr_hdr_t* xvsattr;

	xvsattr = hio_rad_find_extended_vendor_specific_attribute(auth, vendor, xtype, attrtype, 0);
	if (!xvsattr) return 0; /* not found */

	return (delete_attribute(auth, (hio_rad_attr_hdr_t*)xvsattr) <= -1)? -1: 1;
}

/* ---------------------------------------------------------------- */

hio_rad_attr_hdr_t* hio_rad_insert_attribute (
	hio_rad_hdr_t* auth, int max,
	hio_uint8_t attrtype, const void* ptr, hio_uint8_t len)
{
	hio_rad_attr_hdr_t* attr;
	int auth_len = hio_ntoh16(auth->length);
	int new_auth_len;

	/*if (len > HIO_RAD_MAX_ATTR_VALUE_LEN) return HIO_NULL;*/
	if (len > HIO_RAD_MAX_ATTR_VALUE_LEN) len = HIO_RAD_MAX_ATTR_VALUE_LEN;
	new_auth_len = auth_len + HIO_SIZEOF(*attr) + len;

	if (new_auth_len > max) return HIO_NULL;

	attr = (hio_rad_attr_hdr_t*)((hio_uint8_t*)auth + auth_len);
	attr->type = attrtype;
	attr->length = new_auth_len - auth_len;
	HIO_MEMCPY (attr + 1, ptr, len);
	auth->length = hio_hton16(new_auth_len);

	return attr;
}

hio_rad_xattr_hdr_t* hio_rad_insert_extended_attribute (
	hio_rad_hdr_t* auth, int max, hio_uint8_t xtype,
	hio_uint8_t attrtype, const void* ptr, hio_uint8_t len, hio_uint8_t lxflags)
{
	hio_rad_xattr_hdr_t* xattr;
	int auth_len = hio_ntoh16(auth->length);
	int new_auth_len, maxvallen, hdrlen;

	if (HIO_RAD_ATTR_IS_SHORT_EXTENDED(xtype)) 
	{
		maxvallen = HIO_RAD_MAX_XATTR_VALUE_LEN;
		hdrlen = HIO_SIZEOF(hio_rad_xattr_hdr_t);
	}
	else if (HIO_RAD_ATTR_IS_LONG_EXTENDED(xtype)) 
	{
		maxvallen = HIO_RAD_MAX_LXATTR_VALUE_LEN;
		hdrlen = HIO_SIZEOF(hio_rad_lxattr_hdr_t);
	}
	else return HIO_NULL;

	/*if (len > maxvallen) return HIO_NULL;*/
	if (len > maxvallen) len = maxvallen;
	new_auth_len = auth_len + hdrlen + len;

	if (new_auth_len > max) return HIO_NULL;

	xattr = (hio_rad_xattr_hdr_t*)((hio_uint8_t*)auth + auth_len);
	xattr->type = xtype;
	xattr->length = new_auth_len - auth_len;
	if (HIO_RAD_ATTR_IS_LONG_EXTENDED(xtype)) 
	{
		hio_rad_lxattr_hdr_t* lxattr;
		lxattr = (hio_rad_lxattr_hdr_t*)xattr;
		lxattr->xtype = attrtype;
		lxattr->xflags = lxflags;
		HIO_MEMCPY (lxattr + 1, ptr, len);
	}
	else
	{
		xattr->xtype = attrtype;
		HIO_MEMCPY (xattr + 1, ptr, len);
	}
	auth->length = hio_hton16(new_auth_len);

	return xattr;
}

hio_rad_vsattr_hdr_t* hio_rad_insert_vendor_specific_attribute (
	hio_rad_hdr_t* auth, int max,
	hio_uint32_t vendor, hio_uint8_t attrtype, const void* ptr, hio_uint8_t len)
{
	hio_rad_vsattr_hdr_t* vsattr;
	int auth_len = hio_ntoh16(auth->length);
	int new_auth_len;

	/*if (len > HIO_RAD_MAX_VSATTR_VALUE_LEN) return HIO_NULL;*/
	if (len > HIO_RAD_MAX_VSATTR_VALUE_LEN) len = HIO_RAD_MAX_VSATTR_VALUE_LEN;
	new_auth_len = auth_len + HIO_SIZEOF(*vsattr) + len;

	if (new_auth_len > max) return HIO_NULL;

	vsattr = (hio_rad_vsattr_hdr_t*)((hio_uint8_t*)auth + auth_len);
	vsattr->type = HIO_RAD_ATTR_VENDOR_SPECIFIC;
	vsattr->length = new_auth_len - auth_len;
	vsattr->vendor = hio_hton32(vendor);

	vsattr->vs.type = attrtype;
	vsattr->vs.length = HIO_SIZEOF(vsattr->vs) + len;
	HIO_MEMCPY (vsattr + 1, ptr, len);

	auth->length = hio_hton16(new_auth_len);
	return vsattr;
}

hio_rad_xvsattr_hdr_t* hio_rad_insert_extended_vendor_specific_attribute (
	hio_rad_hdr_t* auth, int max, hio_uint32_t vendor, hio_uint8_t xtype,
	hio_uint8_t attrtype, const void* ptr, hio_uint8_t len, hio_uint8_t lxflags)
{
	/* RFC6929 */
	hio_rad_xvsattr_hdr_t* xvsattr;
	int auth_len = hio_ntoh16(auth->length);
	int new_auth_len, maxvallen, hdrlen;

	if (HIO_RAD_ATTR_IS_SHORT_EXTENDED(xtype)) 
	{
		maxvallen = HIO_RAD_MAX_XVSATTR_VALUE_LEN;
		hdrlen = HIO_SIZEOF(hio_rad_xvsattr_hdr_t);
	}
	else if (HIO_RAD_ATTR_IS_LONG_EXTENDED(xtype)) 
	{
		maxvallen = HIO_RAD_MAX_LXVSATTR_VALUE_LEN;
		hdrlen = HIO_SIZEOF(hio_rad_lxvsattr_hdr_t);
	}
	else return HIO_NULL;

	/*if (len > maxvallen) return HIO_NULL;*/
	if (len > maxvallen) len = HIO_RAD_MAX_XVSATTR_VALUE_LEN;
	new_auth_len = auth_len + hdrlen + len;

	if (new_auth_len > max) return HIO_NULL;

	xvsattr = (hio_rad_xvsattr_hdr_t*)((hio_uint8_t*)auth + auth_len);
	xvsattr->type = xtype;
	xvsattr->length = new_auth_len - auth_len;
	xvsattr->xtype = HIO_RAD_ATTR_VENDOR_SPECIFIC;
	xvsattr->vendor = hio_hton32(vendor);

	if (HIO_RAD_ATTR_IS_LONG_EXTENDED(xtype)) 
	{
		/* this function is still low-level. it doesn't handle continuation of big data */
		hio_rad_lxvsattr_hdr_t* lxvsattr;
		lxvsattr = (hio_rad_lxvsattr_hdr_t*)xvsattr;
		lxvsattr->lxvs.type = attrtype;
		lxvsattr->lxvs.flags = lxflags;
		lxvsattr->lxvs.length = len + HIO_SIZEOF(lxvsattr->lxvs);
		HIO_MEMCPY (lxvsattr + 1, ptr, len);
	}
	else
	{
		xvsattr->xvs.type = attrtype;
		xvsattr->xvs.length = len + HIO_SIZEOF(xvsattr->xvs);
		HIO_MEMCPY (xvsattr + 1, ptr, len);
	}	

	auth->length = hio_hton16(new_auth_len);
	return xvsattr;
}

/* ---------------------------------------------------------------- */

hio_rad_attr_hdr_t* hio_rad_insert_attribute_with_bcstr (
	hio_rad_hdr_t* auth, int max, hio_uint32_t vendor, 
	hio_uint8_t id, const hio_bch_t* value)
{
	return (vendor == 0)?
		hio_rad_insert_attribute(auth, max, id, value, hio_count_bcstr(value)):
		(hio_rad_attr_hdr_t*)hio_rad_insert_vendor_specific_attribute(auth, max, vendor, id, value, hio_count_bcstr(value));
}

hio_rad_attr_hdr_t* hio_rad_insert_attribute_with_ucstr (
	hio_rad_hdr_t* auth, int max, hio_uint32_t vendor, 
	hio_uint8_t id, const hio_uch_t* value)
{
	hio_oow_t bcslen, ucslen;
	hio_bch_t bcsval[HIO_RAD_MAX_ATTR_VALUE_LEN + 1];

	bcslen = HIO_COUNTOF(bcsval);
	if (hio_conv_ucstr_to_utf8(value, &ucslen, bcsval, &bcslen) <= -1) return HIO_NULL;
	return (vendor == 0)?
		hio_rad_insert_attribute(auth, max, id, bcsval, bcslen):
		(hio_rad_attr_hdr_t*)hio_rad_insert_vendor_specific_attribute(auth, max, vendor, id, bcsval, bcslen);
}

hio_rad_attr_hdr_t* hio_rad_insert_attribute_with_bchars (
	hio_rad_hdr_t* auth, int max, hio_uint32_t vendor, 
	hio_uint8_t id, const hio_bch_t* value, hio_uint8_t length)
{
	return (vendor == 0)?
		hio_rad_insert_attribute(auth, max, id, value, length):
		(hio_rad_attr_hdr_t*)hio_rad_insert_vendor_specific_attribute(auth, max, vendor, id, value, length);
}

hio_rad_attr_hdr_t* hio_rad_insert_attribute_with_uchars (
	hio_rad_hdr_t* auth, int max, hio_uint32_t vendor, 
	hio_uint8_t id, const hio_uch_t* value, hio_uint8_t length)
{
	hio_oow_t bcslen, ucslen;
	hio_bch_t bcsval[HIO_RAD_MAX_ATTR_VALUE_LEN];

	ucslen = length;
	bcslen = HIO_COUNTOF(bcsval);
	if (hio_conv_uchars_to_utf8(value, &ucslen, bcsval, &bcslen) <= -1) return HIO_NULL; 

	return (vendor == 0)?
		hio_rad_insert_attribute(auth, max, id, bcsval, bcslen):
		(hio_rad_attr_hdr_t*)hio_rad_insert_vendor_specific_attribute(auth, max, vendor, id, bcsval, bcslen);
}

hio_rad_attr_hdr_t* hio_rad_insert_uint32_attribute (
	hio_rad_hdr_t* auth, int max, hio_uint32_t vendor, hio_uint8_t id, hio_uint32_t value)
{
	hio_uint32_t val = hio_hton32(value);
	return (vendor == 0)?
		hio_rad_insert_attribute(auth, max, id, &val, HIO_SIZEOF(val)):
		(hio_rad_attr_hdr_t*)hio_rad_insert_vendor_specific_attribute(auth, max, vendor, id, &val, HIO_SIZEOF(val));
}

hio_rad_attr_hdr_t* hio_rad_insert_ipv6prefix_attribute (
	hio_rad_hdr_t* auth, int  max, hio_uint32_t vendor, hio_uint8_t id,
	hio_uint8_t prefix_bits, const hio_ip6ad_t* value)
{
	struct ipv6prefix_t
	{
		hio_uint8_t reserved;
		hio_uint8_t bits;
		hio_ip6ad_t value;
	} __attribute__((__packed__));

	struct ipv6prefix_t  ipv6prefix;	
	hio_uint8_t i, j;

	if (prefix_bits > 128) prefix_bits = 128;

	HIO_MEMSET (&ipv6prefix, 0, HIO_SIZEOF(ipv6prefix));
	ipv6prefix.bits = prefix_bits;

	for (i = 0, j = 0; i < prefix_bits; i += 8, j++)
	{
		hio_uint8_t bits = prefix_bits - i;
		if (bits >= 8)
		{
			ipv6prefix.value.v[j] = value->v[j];
		}
		else
		{
			/*
				1 -> 10000000
				2 -> 11000000
				3 -> 11100000
				4 -> 11110000
				5 -> 11111000
				6 -> 11111100
				7 -> 11111110
			*/
			ipv6prefix.value.v[j] = value->v[j] & (0xFF << (8 - bits));
		}
	}
	
	return (vendor == 0)?
		hio_rad_insert_attribute(auth, max, id, &ipv6prefix, j + 2):
		(hio_rad_attr_hdr_t*)hio_rad_insert_vendor_specific_attribute(auth, max, vendor, id, &ipv6prefix, j + 2);
}

#if (HIO_SIZEOF_UINT64_T > 0)
hio_rad_attr_hdr_t* hio_rad_insert_giga_attribute (
	hio_rad_hdr_t* auth, int max, hio_uint32_t  vendor, int low_id, int high_id, hio_uint64_t value)
{
	hio_rad_attr_hdr_t* hdr;
	hio_uint32_t low;
	low = value & HIO_TYPE_MAX(hio_uint32_t);
	low = hio_hton32(low);

	if (vendor == 0)
	{
		hdr = hio_rad_insert_attribute(auth, max, low_id, &low, HIO_SIZEOF(low));
		if (!hdr) return HIO_NULL;

		if (value > HIO_TYPE_MAX(hio_uint32_t))
		{
			hio_uint32_t high;
			high = value >> (HIO_SIZEOF(hio_uint32_t) * 8);
			high = hio_hton32(high);
			if (!hio_rad_insert_attribute(auth, max, high_id, &high, HIO_SIZEOF(high))) return HIO_NULL;
		}
	}
	else
	{
		hdr = (hio_rad_attr_hdr_t*)hio_rad_insert_vendor_specific_attribute(auth, max, vendor, low_id, &low, HIO_SIZEOF(low));
		if (!hdr) return HIO_NULL;

		if (value > HIO_TYPE_MAX(hio_uint32_t))
		{
			hio_uint32_t high;
			high = value >> (HIO_SIZEOF(hio_uint32_t) * 8);
			high = hio_hton32(high);
			if (!hio_rad_insert_vendor_specific_attribute(auth, max, vendor, high_id, &high, HIO_SIZEOF(high))) return HIO_NULL;
		}
	}

	return hdr;
}
#endif

/* -----------------------------------------------------------------------
 *  HIGH-LEVEL ATTRIBUTE FUNCTIONS
 * ----------------------------------------------------------------------- */

hio_rad_attr_hdr_t* hio_rad_find_attr (hio_rad_hdr_t* hdr, hio_uint16_t attrcode, int index)
{
	hio_uint8_t hi, lo;

	hi = HIO_RAD_ATTR_CODE_HI(attrcode);
	lo = HIO_RAD_ATTR_CODE_LO(attrcode);

	if (!hi)
	{
		return hio_rad_find_attribute(hdr, lo, index);
	}
	else if (HIO_RAD_ATTR_IS_EXTENDED(hi))
	{
		/* both short and long */
		return (hio_rad_attr_hdr_t*)hio_rad_find_extended_attribute(hdr, hi, lo, index);
	}

	/* attribute code out of range */
	return HIO_NULL;
}


hio_rad_vsattr_hdr_t* hio_rad_find_vsattr (hio_rad_hdr_t* hdr, hio_uint32_t vendor, hio_uint16_t attrcode, int index)
{
	hio_uint8_t hi, lo;

	hi = HIO_RAD_ATTR_CODE_HI(attrcode);
	lo = HIO_RAD_ATTR_CODE_LO(attrcode);

	if (!hi)
	{
		return hio_rad_find_vendor_specific_attribute(hdr, vendor, lo, index);
	}
	else if (HIO_RAD_ATTR_IS_EXTENDED(hi))
	{
		/* both short and long */
		return (hio_rad_vsattr_hdr_t*)hio_rad_find_extended_vendor_specific_attribute(hdr, vendor, hi, lo, index);
	}

	/* attribute code out of range */
	return HIO_NULL;
}


int hio_rad_delete_attr (hio_rad_hdr_t* hdr, hio_uint16_t attrcode, int index)
{
	hio_uint8_t hi, lo;

	hi = HIO_RAD_ATTR_CODE_HI(attrcode);
	lo = HIO_RAD_ATTR_CODE_LO(attrcode);

	if (!hi)
	{
		return hio_rad_delete_attribute(hdr, lo, index);
	}
	else if (HIO_RAD_ATTR_IS_EXTENDED(hi))
	{
		/* both short and long */
		return hio_rad_delete_extended_attribute(hdr, hi, lo, index);
	}

	/* attribute code out of range */
	return -2;
}


int hio_rad_delete_vsattr (hio_rad_hdr_t* hdr, hio_uint32_t vendor, hio_uint16_t attrcode, int index)
{
	hio_uint8_t hi, lo;

	hi = HIO_RAD_ATTR_CODE_HI(attrcode);
	lo = HIO_RAD_ATTR_CODE_LO(attrcode);

	if (!hi)
	{
		return hio_rad_delete_vendor_specific_attribute(hdr, vendor, lo, index);
	}
	else if (HIO_RAD_ATTR_IS_EXTENDED(hi))
	{
		/* both short and long */
		return hio_rad_delete_extended_vendor_specific_attribute(hdr, vendor, hi, lo, index);
	}

	/* attribute code out of range */
	return -2;
}

hio_rad_attr_hdr_t* hio_rad_insert_attr (hio_rad_hdr_t* auth, int max, hio_uint16_t attrcode, const void* ptr, hio_uint16_t len)
{
	hio_uint8_t hi, lo;

	hi = HIO_RAD_ATTR_CODE_HI(attrcode);
	lo = HIO_RAD_ATTR_CODE_LO(attrcode);

	if (!hi)
	{
		/* classical attribute */
		return hio_rad_insert_attribute(auth, max, lo, ptr, len);
	}
	else if (HIO_RAD_ATTR_IS_SHORT_EXTENDED(hi))
	{
		return (hio_rad_attr_hdr_t*)hio_rad_insert_extended_attribute(auth, max, hi, lo, ptr, len, 0);
	}
	else if (HIO_RAD_ATTR_IS_LONG_EXTENDED(hi))
	{
		/* TODO: mutliple attributes if data is long */
		return (hio_rad_attr_hdr_t*)hio_rad_insert_extended_attribute(auth, max, hi, lo, ptr, len, 0);
	}

	/* attribute code out of range */
	return HIO_NULL;
}


hio_rad_vsattr_hdr_t* hio_rad_insert_vsattr (hio_rad_hdr_t* auth, int max, hio_uint32_t vendor, hio_uint16_t attrcode, const void* ptr, hio_uint16_t len)
{
	hio_uint8_t hi, lo;

	hi = HIO_RAD_ATTR_CODE_HI(attrcode);
	lo = HIO_RAD_ATTR_CODE_LO(attrcode);

	if (!hi)
	{
		/* classical attribute */
		return hio_rad_insert_vendor_specific_attribute(auth, max, vendor, lo, ptr, len);
	}
	else if (HIO_RAD_ATTR_IS_SHORT_EXTENDED(hi))
	{
		return (hio_rad_vsattr_hdr_t*)hio_rad_insert_extended_vendor_specific_attribute(auth, max, vendor, hi, lo, ptr, len, 0);
	}
	else if (HIO_RAD_ATTR_IS_LONG_EXTENDED(hi))
	{
	/* TODO: if len is greater than the maxm add multiple extended attributes with continuation */
		return (hio_rad_vsattr_hdr_t*)hio_rad_insert_extended_vendor_specific_attribute(auth, max, vendor, hi, lo, ptr, len, 0);
	}

	/* attribute code out of range */
	return HIO_NULL;
}

/* -----------------------------------------------------------------------
 *  UTILITY FUNCTIONS
 * ----------------------------------------------------------------------- */

#define PASS_BLKSIZE HIO_RAD_MAX_AUTHENTICATOR_LEN
#define ALIGN(x,factor) ((((x) + (factor) - 1) / (factor)) * (factor))

int hio_rad_set_user_password (hio_rad_hdr_t* auth, int max, const hio_bch_t* password, const hio_bch_t* secret)
{
	hio_md5_t md5;

	hio_uint8_t hashed[HIO_RAD_MAX_ATTR_VALUE_LEN]; /* can't be longer than this */
	hio_uint8_t tmp[PASS_BLKSIZE];

	int i, pwlen, padlen;

	/*HIO_ASSERT (HIO_SIZEOF(tmp) >= HIO_MD5_DIGEST_LEN);*/

	pwlen = hio_count_bcstr(password);

	/* calculate padlen to be the multiples of 16.
	 * 0 is forced to 16. */
	padlen = (pwlen <= 0)? PASS_BLKSIZE: ALIGN(pwlen,PASS_BLKSIZE);

	/* keep the padded length limited within the maximum attribute length */
	if (padlen > HIO_RAD_MAX_ATTR_VALUE_LEN)
	{
		padlen = HIO_RAD_MAX_ATTR_VALUE_LEN;
		padlen = ALIGN(padlen,PASS_BLKSIZE);
		if (padlen > HIO_RAD_MAX_ATTR_VALUE_LEN) padlen -= PASS_BLKSIZE;

		/* also limit the original length */
		if (pwlen > padlen) pwlen = padlen;
	}

	HIO_MEMSET (hashed, 0, padlen);
	HIO_MEMCPY (hashed, password, pwlen);

	/*
	 * c1 = p1 XOR MD5(secret + authenticator)
	 * c2 = p2 XOR MD5(secret + c1)
	 * ...
	 * cn = pn XOR MD5(secret + cn-1)
	 */
	hio_md5_initialize (&md5);
	hio_md5_update (&md5, secret, hio_count_bcstr(secret));
	hio_md5_update (&md5, auth->authenticator, HIO_SIZEOF(auth->authenticator));
	hio_md5_digest (&md5, tmp, HIO_SIZEOF(tmp));

	xor (&hashed[0], tmp, HIO_SIZEOF(tmp));

	for (i = 1; i < (padlen >> 4); i++) 
	{
		hio_md5_initialize (&md5);
		hio_md5_update (&md5, secret, hio_count_bcstr(secret));
		hio_md5_update (&md5, &hashed[(i-1) * PASS_BLKSIZE], PASS_BLKSIZE);
		hio_md5_digest (&md5, tmp, HIO_SIZEOF(tmp));
		xor (&hashed[i * PASS_BLKSIZE], tmp, HIO_SIZEOF(tmp));
	}

	/* ok if not found or deleted. but not ok if an error occurred */
	while (1)
	{
		int n;
		n = hio_rad_delete_attribute(auth, HIO_RAD_ATTR_USER_PASSWORD, 0);
		if (n <= -1) goto oops;
		if (n == 0) break; 
	}
	if (!hio_rad_insert_attribute(auth, max, HIO_RAD_ATTR_USER_PASSWORD, hashed, padlen)) goto oops;

	return 0;

oops:
	return -1;
}

void hio_rad_fill_authenticator (hio_rad_hdr_t* auth)
{
	fill_authenticator_randomly (auth->authenticator, HIO_SIZEOF(auth->authenticator));
}

void hio_rad_copy_authenticator (hio_rad_hdr_t* dst, const hio_rad_hdr_t* src)
{
	HIO_MEMCPY (dst->authenticator, src->authenticator, HIO_SIZEOF(dst->authenticator));
}

int hio_rad_set_authenticator (hio_rad_hdr_t* req, const hio_bch_t* secret)
{
	hio_md5_t md5;

	/* this assumes that req->authentcator at this point
	 * is filled with zeros. so make sure that it contains zeros
	 * before you call this function */

	hio_md5_initialize (&md5);
	hio_md5_update (&md5, req, hio_ntoh16(req->length));
	if (*secret) hio_md5_update (&md5, secret, hio_count_bcstr(secret));
	hio_md5_digest (&md5, req->authenticator, HIO_SIZEOF(req->authenticator));

	return 0;
}

int hio_rad_verify_request (hio_rad_hdr_t* req, const hio_bch_t* secret)
{
	hio_md5_t md5;
	hio_uint8_t orgauth[HIO_RAD_MAX_AUTHENTICATOR_LEN];
	int ret;

	HIO_MEMCPY (orgauth, req->authenticator, HIO_SIZEOF(req->authenticator));
	HIO_MEMSET (req->authenticator, 0, HIO_SIZEOF(req->authenticator));

	hio_md5_initialize (&md5);
	hio_md5_update (&md5, req, hio_ntoh16(req->length));
	if (*secret) hio_md5_update (&md5, secret, hio_count_bcstr(secret));
	hio_md5_digest (&md5, req->authenticator, HIO_SIZEOF(req->authenticator));

	ret = (HIO_MEMCMP (req->authenticator, orgauth, HIO_SIZEOF(req->authenticator)) == 0)? 1: 0;
	HIO_MEMCPY (req->authenticator, orgauth, HIO_SIZEOF(req->authenticator));

	return ret;
}

int hio_rad_verify_response (hio_rad_hdr_t* res, const hio_rad_hdr_t* req, const hio_bch_t* secret)
{
	hio_md5_t md5;

	hio_uint8_t calculated[HIO_RAD_MAX_AUTHENTICATOR_LEN];
	hio_uint8_t reply[HIO_RAD_MAX_AUTHENTICATOR_LEN];

	/*HIO_ASSERT (HIO_SIZEOF(req->authenticator) == HIO_RAD_MAX_AUTHENTICATOR_LEN);
	HIO_ASSERT (HIO_SIZEOF(res->authenticator) == HIO_RAD_MAX_AUTHENTICATOR_LEN);*/

	/*
	 * We could dispense with the HIO_MEMCPY, and do MD5's of the packet
	 * + authenticator piece by piece. This is easier understand, 
	 * and maybe faster.
	 */
	HIO_MEMCPY(reply, res->authenticator, HIO_SIZEOF(res->authenticator)); /* save the reply */
	HIO_MEMCPY(res->authenticator, req->authenticator, HIO_SIZEOF(req->authenticator)); /* sent authenticator */

	/* MD5(response packet header + authenticator + response packet data + secret) */
	hio_md5_initialize (&md5);
	hio_md5_update (&md5, res, hio_ntoh16(res->length));

	/* 
	 * This next bit is necessary because of a bug in the original Livingston
	 * RADIUS server. The authentication authenticator is *supposed* to be 
	 * MD5'd with the old password (as the secret) for password changes.
	 * However, the old password isn't used. The "authentication" authenticator
	 * for the server reply packet is simply the MD5 of the reply packet.
	 * Odd, the code is 99% there, but the old password is never copied
	 * to the secret!
	 */
	if (*secret) hio_md5_update (&md5, secret, hio_count_bcstr(secret));
	hio_md5_digest (&md5, calculated, HIO_SIZEOF(calculated));

	/* Did he use the same random authenticator + shared secret? */
	return (HIO_MEMCMP(calculated, reply, HIO_SIZEOF(reply)) != 0)? 0: 1;
}
