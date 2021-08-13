
#include <hio-rad.h>
#include <stdio.h>
#include "t.h"


#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int main ()
{
	{	
		hio_uint8_t buf[10240];
		hio_rad_hdr_t* hdr = buf;
	
		
		int i, j, n, exptotlen;

		struct
		{
			int index; /* index among same attributes */
			int vendor;
			int attrcode;
			void* ptr;
			int len;
		} data[] = {
			{ 0, 0,     HIO_RAD_ATTR_REPLY_MESSAGE,              "reply message",        13 },
			{ 1, 0,     HIO_RAD_ATTR_REPLY_MESSAGE,              "reply message 2",      15 },
			{ 0, 10415, 1,                                       "470010171566423",      15 },
			{ 0, 10415, 8,                                       "imsi-mcc-mnc",         12 },
			{ 0, 4329,  5,                                       "ssid",                 4 },
			{ 0, 0,     HIO_RAD_ATTR_NAS_IDENTIFIER,             "nas identifier",       14 },
			{ 0, 0,     HIO_RAD_ATTR_USER_PASSWORD,              "password",             8  },
			{ 0, 0,     HIO_RAD_ATTR_USER_NAME,                  "username",             8  },
			{ 1, 10415, 8,                                       "imsi-mcc-mnc-2",       14 },

			/* Frag-Status 241.1 integer. */
			{ 0, 0,     HIO_RAD_ATTR_CODE_EXTENDED_1(1),         "\x02",                 1  }, 
			{ 1, 0,     HIO_RAD_ATTR_CODE_EXTENDED_1(1),         "\x01",                 1  }, 
			{ 2, 0,     HIO_RAD_ATTR_CODE_EXTENDED_1(1),         "\x00",                 1  }, 
			/* Proxy-State-Length 241.2 integer. */
			{ 0, 0,     HIO_RAD_ATTR_CODE_EXTENDED_1(2),         "\xFF",                 1  }, 

			{ 3, 0,     HIO_RAD_ATTR_CODE_EXTENDED_1(1),         "\x03",                 1  }, 
			{ 1, 0,     HIO_RAD_ATTR_CODE_EXTENDED_1(2),         "\xDD",                 1  }, 


			{ 0, 6527,  HIO_RAD_ATTR_CODE_EXTENDED_1(2),         "evs attribute",        13 },
			{ 1, 6527,  HIO_RAD_ATTR_CODE_EXTENDED_1(2),         "evs attribute 2",      15 },
			{ 0, 8888,  HIO_RAD_ATTR_CODE_EXTENDED_4(444),       "evs attribute",        13 },

			{ 2, 0,     HIO_RAD_ATTR_REPLY_MESSAGE,              "reply message 3",      15 },
		};

		hio_rad_initialize (buf, HIO_RAD_ACCESS_REQUEST, 255);
		T_ASSERT1 (hdr->code == HIO_RAD_ACCESS_REQUEST, "hdr->code not ok");
		T_ASSERT1 (hdr->id == 255, "hdr->id not ok");
		exptotlen = HIO_SIZEOF(*hdr);
		T_ASSERT1 (hdr->length == HIO_CONST_HTON16(HIO_SIZEOF(*hdr)), "hdr->length not ok");

		for (i = 0; i < HIO_COUNTOF(data); i++)
		{
			if (data[i].vendor)
			{
				hio_rad_vsattr_hdr_t* vsattr;
				hio_rad_xvsattr_hdr_t* xvsattr;
				hio_rad_lxvsattr_hdr_t* lxvsattr;

				vsattr = hio_rad_insert_vsattr(hdr, HIO_SIZEOF(buf), data[i].vendor, data[i].attrcode, data[i].ptr, data[i].len);
				T_ASSERT1 (vsattr != HIO_NULL, "attribute insertion failure");
				T_ASSERT1 (hio_ntoh16(hdr->length) == exptotlen + vsattr->length, "hdr->length not ok after attribute insertion");

				if (HIO_RAD_ATTR_IS_LONG_EXTENDED(vsattr->type))
				{
					exptotlen += HIO_SIZEOF(*lxvsattr);
					lxvsattr = vsattr;
					T_ASSERT1 (lxvsattr->length == HIO_SIZEOF(*lxvsattr) + data[i].len, "wrong attribute length");
				}
				else if (HIO_RAD_ATTR_IS_SHORT_EXTENDED(vsattr->type))
				{
					exptotlen += HIO_SIZEOF(*xvsattr);
					xvsattr = vsattr;
					T_ASSERT1 (xvsattr->length == HIO_SIZEOF(*xvsattr) + data[i].len, "wrong attribute length");
				}
				else
				{
					exptotlen += HIO_SIZEOF(*vsattr);
					T_ASSERT1 (vsattr->length == HIO_SIZEOF(*vsattr) + data[i].len, "wrong attribute length");
				}
			}
			else
			{
				hio_rad_attr_hdr_t* attr;
				hio_rad_xattr_hdr_t* xattr;
				hio_rad_lxattr_hdr_t* lxattr;

				attr = hio_rad_insert_attr(hdr, HIO_SIZEOF(buf), data[i].attrcode, data[i].ptr, data[i].len);
				T_ASSERT1 (attr != HIO_NULL, "attribute insertion failure");
				T_ASSERT1 (hio_ntoh16(hdr->length) == exptotlen + attr->length, "hdr->length not ok after attribute insertion");

				if (HIO_RAD_ATTR_IS_LONG_EXTENDED(attr->type))
				{
					exptotlen += HIO_SIZEOF(*lxattr);
					lxattr = attr;
					T_ASSERT1 (lxattr->length == HIO_SIZEOF(*lxattr) + data[i].len, "wrong attribute length");
				}
				else if (HIO_RAD_ATTR_IS_SHORT_EXTENDED(attr->type))
				{
					exptotlen += HIO_SIZEOF(*xattr);
					xattr = attr;
					T_ASSERT1 (xattr->length == HIO_SIZEOF(*xattr) + data[i].len, "wrong attribute length");
				}
				else
				{
					exptotlen += HIO_SIZEOF(*attr);
					T_ASSERT1 (attr->length == HIO_SIZEOF(*attr) + data[i].len, "wrong attribute length");
				}
			}
			T_ASSERT1 (hio_comp_bchars((hio_uint8_t*)hdr + hio_ntoh16(hdr->length) - data[i].len, data[i].len, data[i].ptr, data[i].len, 0) == 0, "wrong attribute value");
			exptotlen += data[i].len;
			T_ASSERT1 (hio_ntoh16(hdr->length) == exptotlen, "hdr->length not ok after attribute insertion");

			for (j = 0; j < HIO_COUNTOF(data); j++)
			{
				if (data[j].vendor)
				{
					hio_rad_vsattr_hdr_t* vsattr;
			
					vsattr = hio_rad_find_vsattr(hdr, data[j].vendor, data[j].attrcode, data[j].index);
					if (j <= i)
					{
						void* val_ptr;
						int val_len;

						T_ASSERT1 (vsattr != HIO_NULL, "find failure");

						if (HIO_RAD_ATTR_IS_SHORT_EXTENDED(vsattr->type))
						{
							hio_rad_xvsattr_hdr_t* xvsattr = (hio_rad_xvsattr_hdr_t*)vsattr;
							T_ASSERT1 (hio_ntoh32(xvsattr->vendor) == data[j].vendor, "wrong vendor code");
							T_ASSERT1 (xvsattr->type == HIO_RAD_ATTR_CODE_HI(data[j].attrcode), "wrong attribute base");
							T_ASSERT1 (xvsattr->length == HIO_SIZEOF(*xvsattr) + data[j].len, "wrong attribute length");
							T_ASSERT1 (xvsattr->xvs.type == HIO_RAD_ATTR_CODE_LO(data[j].attrcode), "wrong vendor-specific attribute type");
							T_ASSERT1 (xvsattr->xvs.length == HIO_SIZEOF(xvsattr->xvs) + data[j].len, "wrong attribute length");
							val_ptr = xvsattr + 1;
							val_len = xvsattr->xvs.length - HIO_SIZEOF(xvsattr->xvs);
						}
						else if (HIO_RAD_ATTR_IS_LONG_EXTENDED(vsattr->type))
						{
							hio_rad_lxvsattr_hdr_t* lxvsattr = (hio_rad_lxvsattr_hdr_t*)vsattr;
							T_ASSERT1 (hio_ntoh32(lxvsattr->vendor) == data[j].vendor, "wrong vendor code");
							T_ASSERT1 (lxvsattr->type == HIO_RAD_ATTR_CODE_HI(data[j].attrcode), "wrong attribute base");
							T_ASSERT1 (lxvsattr->length == HIO_SIZEOF(*lxvsattr) + data[j].len, "wrong attribute length");
							T_ASSERT1 (lxvsattr->lxvs.type == HIO_RAD_ATTR_CODE_LO(data[j].attrcode), "wrong vendor-specific attribute type");
							T_ASSERT1 (lxvsattr->lxvs.length == HIO_SIZEOF(lxvsattr->lxvs) + data[j].len, "wrong attribute length");
							val_ptr = lxvsattr + 1;
							val_len = lxvsattr->lxvs.length - HIO_SIZEOF(lxvsattr->lxvs);
						}
						else
						{
							T_ASSERT1 (hio_ntoh32(vsattr->vendor) == data[j].vendor, "wrong vendor code");
							T_ASSERT1 (vsattr->type == HIO_RAD_ATTR_VENDOR_SPECIFIC, "wrong attribute type");
							T_ASSERT1 (vsattr->length == HIO_SIZEOF(*vsattr) + data[j].len, "wrong attribute length");
							T_ASSERT1 (vsattr->vs.type == data[j].attrcode, "wrong vendor-specific attribute type");
							T_ASSERT1 (vsattr->vs.length == HIO_SIZEOF(vsattr->vs) + data[j].len, "wrong attribute length");
							val_ptr = vsattr + 1;
							val_len = vsattr->vs.length - HIO_SIZEOF(vsattr->vs);
						}

						T_ASSERT1 (hio_comp_bchars(val_ptr, val_len, data[j].ptr, data[j].len, 0) == 0, "wrong attribute value");
					}
					else
					{
						T_ASSERT1 (vsattr == HIO_NULL, "find failure");
					}
				}
				else
				{
					hio_rad_attr_hdr_t* attr;
			
					attr = hio_rad_find_attr(hdr, data[j].attrcode, data[j].index);
					if (j <= i)
					{
						void* val_ptr;
						int val_len;
						T_ASSERT1 (attr != HIO_NULL, "find failure");

						if (HIO_RAD_ATTR_IS_SHORT_EXTENDED(attr->type))
						{
							hio_rad_xattr_hdr_t* xattr = (hio_rad_xattr_hdr_t*)attr;
							T_ASSERT1 (HIO_RAD_ATTR_CODE_HI(data[j].attrcode) == xattr->type, "wrong extended attribute base");
							T_ASSERT1 (HIO_RAD_ATTR_CODE_LO(data[j].attrcode) == xattr->xtype, "wrong extended attribute type");
							T_ASSERT1 (xattr->length == HIO_SIZEOF(*xattr) + data[j].len, "wrong attribute length");
							val_ptr = xattr + 1;
							val_len = xattr->length - HIO_SIZEOF(*xattr);
						}
						else if (HIO_RAD_ATTR_IS_LONG_EXTENDED(attr->type))
						{
							hio_rad_lxattr_hdr_t* lxattr = (hio_rad_lxattr_hdr_t*)attr;
							T_ASSERT1 (HIO_RAD_ATTR_CODE_HI(data[j].attrcode) == lxattr->type, "wrong long extended attribute base");
							T_ASSERT1 (HIO_RAD_ATTR_CODE_LO(data[j].attrcode) == lxattr->xtype, "wrong long extended attribute type");
							T_ASSERT1 (lxattr->length == HIO_SIZEOF(*lxattr) + data[j].len, "wrong attribute length");
							val_ptr = lxattr + 1;
							val_len = lxattr->length - HIO_SIZEOF(*lxattr);
						}
						else
						{
							T_ASSERT1 (attr->type == data[j].attrcode, "wrong attribute type");
							T_ASSERT1 (attr->length == HIO_SIZEOF(*attr) + data[j].len, "wrong attribute length");
							val_ptr = attr + 1;
							val_len = attr->length - HIO_SIZEOF(*attr);
						}

						T_ASSERT1 (hio_comp_bchars(val_ptr, val_len, data[j].ptr, data[j].len, 0) == 0, "wrong attribute value");
					}
					else
					{
						T_ASSERT1 (attr == HIO_NULL, "find failure");
					}
				}
			}
		}

		hio_rad_fill_authenticator (hdr);
		hio_rad_set_user_password (hdr, HIO_SIZEOF(buf), "real_password", "testing123");

#if 1
		{
			int s = socket(AF_INET, SOCK_DGRAM, 0);
			struct sockaddr_in sin;
			memset (&sin, 0, sizeof(sin));
			sin.sin_family = AF_INET;
			sin.sin_addr.s_addr = inet_addr("192.168.1.1");
			sin.sin_port = HIO_CONST_HTON16(1812);
			sendto (s, hdr, hio_ntoh16(hdr->length), 0,  &sin, sizeof(sin));
		}
#endif
	}

	return 0;

oops:
	return -1;
}
