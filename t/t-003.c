
#include <hio-rad.h>
#include <stdio.h>
#include "tap.h"

#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int main ()
{
	no_plan ();

	{	
		hio_uint8_t buf[10240];
		hio_rad_hdr_t* hdr = (hio_rad_hdr_t*)buf;
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
			{ 1, 0,     HIO_RAD_ATTR_USER_PASSWORD,              "password",             8  },
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

		hio_rad_initialize (hdr, HIO_RAD_ACCESS_REQUEST, 255);
		OK (hdr->code == HIO_RAD_ACCESS_REQUEST, "hdr->code not ok");
		OK (hdr->id == 255, "hdr->id not ok");
		exptotlen = HIO_SIZEOF(*hdr);
		OK (hdr->length == HIO_CONST_HTON16(HIO_SIZEOF(*hdr)), "hdr->length not ok");


		for (i = 0; i < HIO_COUNTOF(data); i++)
		{
			if (data[i].vendor)
			{
				hio_rad_vsattr_hdr_t* vsattr;
				hio_rad_xvsattr_hdr_t* xvsattr;
				hio_rad_lxvsattr_hdr_t* lxvsattr;

				vsattr = hio_rad_insert_vsattr(hdr, HIO_SIZEOF(buf), data[i].vendor, data[i].attrcode, data[i].ptr, data[i].len);
				OK (vsattr != HIO_NULL, "attribute insertion failure");
				OK (hio_ntoh16(hdr->length) == exptotlen + vsattr->length, "hdr->length not ok after attribute insertion");

				if (HIO_RAD_ATTR_IS_LONG_EXTENDED(vsattr->type))
				{
					exptotlen += HIO_SIZEOF(*lxvsattr);
					lxvsattr = (hio_rad_lxvsattr_hdr_t*)vsattr;
					OK (lxvsattr->length == HIO_SIZEOF(*lxvsattr) + data[i].len, "wrong attribute length");
				}
				else if (HIO_RAD_ATTR_IS_SHORT_EXTENDED(vsattr->type))
				{
					exptotlen += HIO_SIZEOF(*xvsattr);
					xvsattr = (hio_rad_xvsattr_hdr_t*)vsattr;
					OK (xvsattr->length == HIO_SIZEOF(*xvsattr) + data[i].len, "wrong attribute length");
				}
				else
				{
					exptotlen += HIO_SIZEOF(*vsattr);
					OK (vsattr->length == HIO_SIZEOF(*vsattr) + data[i].len, "wrong attribute length");
				}
			}
			else
			{
				hio_rad_attr_hdr_t* attr;
				hio_rad_xattr_hdr_t* xattr;
				hio_rad_lxattr_hdr_t* lxattr;

				attr = hio_rad_insert_attr(hdr, HIO_SIZEOF(buf), data[i].attrcode, data[i].ptr, data[i].len);
				OK (attr != HIO_NULL, "attribute insertion failure");
				OK (hio_ntoh16(hdr->length) == exptotlen + attr->length, "hdr->length not ok after attribute insertion");

				if (HIO_RAD_ATTR_IS_LONG_EXTENDED(attr->type))
				{
					exptotlen += HIO_SIZEOF(*lxattr);
					lxattr = (hio_rad_lxattr_hdr_t*)attr;
					OK (lxattr->length == HIO_SIZEOF(*lxattr) + data[i].len, "wrong attribute length");
				}
				else if (HIO_RAD_ATTR_IS_SHORT_EXTENDED(attr->type))
				{
					exptotlen += HIO_SIZEOF(*xattr);
					xattr = (hio_rad_xattr_hdr_t*)attr;
					OK (xattr->length == HIO_SIZEOF(*xattr) + data[i].len, "wrong attribute length");
				}
				else
				{
					exptotlen += HIO_SIZEOF(*attr);
					OK (attr->length == HIO_SIZEOF(*attr) + data[i].len, "wrong attribute length");
				}
			}
			OK (hio_comp_bchars((hio_uint8_t*)hdr + hio_ntoh16(hdr->length) - data[i].len, data[i].len, data[i].ptr, data[i].len, 0) == 0, "wrong attribute value");
			exptotlen += data[i].len;
			OK (hio_ntoh16(hdr->length) == exptotlen, "hdr->length not ok after attribute insertion");

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

						OK (vsattr != HIO_NULL, "find failure");

						if (HIO_RAD_ATTR_IS_SHORT_EXTENDED(vsattr->type))
						{
							hio_rad_xvsattr_hdr_t* xvsattr = (hio_rad_xvsattr_hdr_t*)vsattr;
							OK (hio_ntoh32(xvsattr->vendor) == data[j].vendor, "wrong vendor code");
							OK (xvsattr->type == HIO_RAD_ATTR_CODE_HI(data[j].attrcode), "wrong attribute base");
							OK (xvsattr->length == HIO_SIZEOF(*xvsattr) + data[j].len, "wrong attribute length");
							OK (xvsattr->xvs.type == HIO_RAD_ATTR_CODE_LO(data[j].attrcode), "wrong vendor-specific attribute type");
							OK (xvsattr->xvs.length == HIO_SIZEOF(xvsattr->xvs) + data[j].len, "wrong attribute length");
							val_ptr = xvsattr + 1;
							val_len = xvsattr->xvs.length - HIO_SIZEOF(xvsattr->xvs);
						}
						else if (HIO_RAD_ATTR_IS_LONG_EXTENDED(vsattr->type))
						{
							hio_rad_lxvsattr_hdr_t* lxvsattr = (hio_rad_lxvsattr_hdr_t*)vsattr;
							OK (hio_ntoh32(lxvsattr->vendor) == data[j].vendor, "wrong vendor code");
							OK (lxvsattr->type == HIO_RAD_ATTR_CODE_HI(data[j].attrcode), "wrong attribute base");
							OK (lxvsattr->length == HIO_SIZEOF(*lxvsattr) + data[j].len, "wrong attribute length");
							OK (lxvsattr->lxvs.type == HIO_RAD_ATTR_CODE_LO(data[j].attrcode), "wrong vendor-specific attribute type");
							OK (lxvsattr->lxvs.length == HIO_SIZEOF(lxvsattr->lxvs) + data[j].len, "wrong attribute length");
							val_ptr = lxvsattr + 1;
							val_len = lxvsattr->lxvs.length - HIO_SIZEOF(lxvsattr->lxvs);
						}
						else
						{
							OK (hio_ntoh32(vsattr->vendor) == data[j].vendor, "wrong vendor code");
							OK (vsattr->type == HIO_RAD_ATTR_VENDOR_SPECIFIC, "wrong attribute type");
							OK (vsattr->length == HIO_SIZEOF(*vsattr) + data[j].len, "wrong attribute length");
							OK (vsattr->vs.type == data[j].attrcode, "wrong vendor-specific attribute type");
							OK (vsattr->vs.length == HIO_SIZEOF(vsattr->vs) + data[j].len, "wrong attribute length");
							val_ptr = vsattr + 1;
							val_len = vsattr->vs.length - HIO_SIZEOF(vsattr->vs);
						}

						OK (hio_comp_bchars(val_ptr, val_len, data[j].ptr, data[j].len, 0) == 0, "wrong attribute value");
					}
					else
					{
						OK (vsattr == HIO_NULL, "find failure");
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
						OK (attr != HIO_NULL, "find failure");

						if (HIO_RAD_ATTR_IS_SHORT_EXTENDED(attr->type))
						{
							hio_rad_xattr_hdr_t* xattr = (hio_rad_xattr_hdr_t*)attr;
							OK (HIO_RAD_ATTR_CODE_HI(data[j].attrcode) == xattr->type, "wrong extended attribute base");
							OK (HIO_RAD_ATTR_CODE_LO(data[j].attrcode) == xattr->xtype, "wrong extended attribute type");
							OK (xattr->length == HIO_SIZEOF(*xattr) + data[j].len, "wrong attribute length");
							val_ptr = xattr + 1;
							val_len = xattr->length - HIO_SIZEOF(*xattr);
						}
						else if (HIO_RAD_ATTR_IS_LONG_EXTENDED(attr->type))
						{
							hio_rad_lxattr_hdr_t* lxattr = (hio_rad_lxattr_hdr_t*)attr;
							OK (HIO_RAD_ATTR_CODE_HI(data[j].attrcode) == lxattr->type, "wrong long extended attribute base");
							OK (HIO_RAD_ATTR_CODE_LO(data[j].attrcode) == lxattr->xtype, "wrong long extended attribute type");
							OK (lxattr->length == HIO_SIZEOF(*lxattr) + data[j].len, "wrong attribute length");
							val_ptr = lxattr + 1;
							val_len = lxattr->length - HIO_SIZEOF(*lxattr);
						}
						else
						{
							OK (attr->type == data[j].attrcode, "wrong attribute type");
							OK (attr->length == HIO_SIZEOF(*attr) + data[j].len, "wrong attribute length");
							val_ptr = attr + 1;
							val_len = attr->length - HIO_SIZEOF(*attr);
						}

						OK (hio_comp_bchars(val_ptr, val_len, data[j].ptr, data[j].len, 0) == 0, "wrong attribute value");
					}
					else
					{
						OK (attr == HIO_NULL, "find failure");
					}
				}
			}
		}

		hio_rad_fill_authenticator (hdr);
		hio_rad_set_user_password (hdr, HIO_SIZEOF(buf), "real_real_password", "testing123");
		exptotlen -= HIO_SIZEOF(hio_rad_attr_hdr_t) + 8; /* the first User-Password in the data table */
		exptotlen -= HIO_SIZEOF(hio_rad_attr_hdr_t) + 8; /* the second User-Password in the data table */
		exptotlen += HIO_SIZEOF(hio_rad_attr_hdr_t) + HIO_RAD_USER_PASSWORD_TOTSIZE(18);
		OK (hio_ntoh16(hdr->length) == exptotlen, "hdr->length not ok");

		{
			char tmp[1024];
			hio_rad_attr_hdr_t* attr;
			hio_rad_lxattr_hdr_t* lxattr;
			
			for (i = 0; i < HIO_COUNTOF(tmp); i++) tmp[i] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"[i % 26];

			/* the following call must insert 5 attributes. it returns the pointer to the first attribute. */
			attr = hio_rad_insert_attr (hdr, HIO_SIZEOF(buf), HIO_RAD_ATTR_CODE_EXTENDED_6(10), tmp, 1024);
			OK (attr != HIO_NULL, "long extended attribue insertion failure");
			OK (attr->type == HIO_RAD_ATTR_EXTENDED_6, "wrong extended attribute base");

			lxattr = (hio_rad_lxattr_hdr_t*)attr;
			OK (lxattr->xtype == 10, "wrong extended attribute type");
			OK (lxattr->xflags == (1 << 7), "wrong long extended attribute flags");

			/* TODO: inspect 4 continuing attributes */
		}

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

	return exit_status();
}
