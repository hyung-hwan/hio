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

#ifndef _HIO_RAD_H_
#define _HIO_RAD_H_

#include <hio.h>
#include <hio-skad.h>

/* ----------------------------------------------------------- 
 * RARIUS MESSAGE DEFINITIONS
 * ----------------------------------------------------------- */

#define HIO_RAD_PACKET_MAX (65535)

/* radius code */
enum hio_rad_code_t
{
	HIO_RAD_ACCESS_REQUEST      = 1,
	HIO_RAD_ACCESS_ACCEPT       = 2,
	HIO_RAD_ACCESS_REJECT       = 3,
	HIO_RAD_ACCOUNTING_REQUEST  = 4,
	HIO_RAD_ACCOUNTING_RESPONSE = 5,
	HIO_RAD_ACCESS_CHALLENGE    = 6,
	HIO_RAD_DISCONNECT_REQUEST  = 40,
	HIO_RAD_DISCONNECT_ACK      = 41,
	HIO_RAD_DISCONNECT_NAK      = 42,
	HIO_RAD_COA_REQUEST         = 43,
	HIO_RAD_COA_ACK             = 44,
	HIO_RAD_COA_NAK             = 45,

	HIO_RAD_ACCOUNTING_ERROR    = 255 /* this is not a real radius code */
};
typedef enum hio_rad_code_t hio_rad_code_t;

#define HIO_RAD_AUTHENTICATOR_LEN (16)
#define HIO_RAD_USER_PASSWORD_BLKSIZE (16) /* size of a single block */
#define HIO_RAD_USER_PASSWORD_TOTSIZE(pwlen) ((pwlen) <= 0? HIO_RAD_USER_PASSWORD_BLKSIZE: HIO_ALIGN(pwlen,HIO_RAD_USER_PASSWORD_BLKSIZE))

#define HIO_RAD_MAX_ATTR_VALUE_LEN  (HIO_TYPE_MAX(hio_uint8_t) - HIO_SIZEOF(hio_rad_attr_hdr_t))
#define HIO_RAD_MAX_XATTR_VALUE_LEN  (HIO_TYPE_MAX(hio_uint8_t) - HIO_SIZEOF(hio_rad_xattr_hdr_t))
#define HIO_RAD_MAX_LXATTR_VALUE_LEN  (HIO_TYPE_MAX(hio_uint8_t) - HIO_SIZEOF(hio_rad_lxattr_hdr_t))
#define HIO_RAD_MAX_VSATTR_VALUE_LEN (HIO_TYPE_MAX(hio_uint8_t) - HIO_SIZEOF(hio_rad_vsattr_hdr_t))
#define HIO_RAD_MAX_XVSATTR_VALUE_LEN (HIO_TYPE_MAX(hio_uint8_t) - HIO_SIZEOF(hio_rad_xvsattr_hdr_t))
#define HIO_RAD_MAX_LXVSATTR_VALUE_LEN (HIO_TYPE_MAX(hio_uint8_t) - HIO_SIZEOF(hio_rad_lxvsattr_hdr_t))



typedef struct hio_rad_hdr_t hio_rad_hdr_t;
typedef struct hio_rad_attr_hdr_t hio_rad_attr_hdr_t;
typedef struct hio_rad_xattr_hdr_t hio_rad_xattr_hdr_t;
typedef struct hio_rad_lxattr_hdr_t hio_rad_lxattr_hdr_t;
typedef struct hio_rad_vsattr_hdr_t hio_rad_vsattr_hdr_t;
typedef struct hio_rad_xvsattr_hdr_t hio_rad_xvsattr_hdr_t;
typedef struct hio_rad_lxvsattr_hdr_t hio_rad_lxvsattr_hdr_t; /* evs */

typedef struct hio_rad_attr_uint32_t hio_rad_attr_uint32_t;
#if (HIO_SIZEOF_UINT64_T > 0)
typedef struct hio_rad_attr_uint64_t hio_rad_attr_uint64_t;
#endif

#include <hio-pac1.h>
struct hio_rad_hdr_t 
{
	hio_uint8_t   code; /* hio_rad_code_t */
	hio_uint8_t   id;
	hio_uint16_t  length;
	hio_uint8_t   authenticator[HIO_RAD_AUTHENTICATOR_LEN]; /* authenticator */
};

struct hio_rad_attr_hdr_t
{
	hio_uint8_t type; /* hio_rad_attr_type_t */
	hio_uint8_t length;
};

struct hio_rad_xattr_hdr_t
{
	hio_uint8_t type; /* hio_rad_attr_type_t - one of 241-244 */
	hio_uint8_t length;
	hio_uint8_t xtype; /* extended type */
};

struct hio_rad_lxattr_hdr_t
{
	hio_uint8_t type; /* hio_rad_attr_type_t - 245 or 256*/
	hio_uint8_t length;

	hio_uint8_t xtype; /* extended type */
	hio_uint8_t xflags; /* bit 7: continuation, bit 6-0: reserved. */
};

struct hio_rad_vsattr_hdr_t
{
	hio_uint8_t  type; /* type - 26 */
	hio_uint8_t  length; /* length */
	hio_uint32_t vendor; /* in network-byte order */

	/* followed by a standard attribute */
	hio_rad_attr_hdr_t vs;
};

struct hio_rad_xvsattr_hdr_t
{
	hio_uint8_t type; /* one of 241-244 */
	hio_uint8_t length;
	hio_uint8_t xtype;  /* extended type. 26 for evs(extended vendor specific) attribute */
	hio_uint32_t vendor; /* in network-byte order */

	/* followed by a standard attribute  */
	hio_rad_attr_hdr_t xvs;
};

struct hio_rad_lxvsattr_hdr_t
{
	hio_uint8_t type; /* 245, 246*/
	hio_uint8_t length;
	hio_uint8_t xtype;  /* extended type. 26 for evs(extended vendor specific) attribute */
	hio_uint32_t vendor; /* in network-byte order */

	/* followed by an extended attribute  */
	struct
	{
		hio_uint8_t type;
		hio_uint8_t flags;  /* bit 7: continuation, bit 6-0: reserved.  */
		hio_uint8_t length;
	} lxvs;
};

struct hio_rad_attr_uint32_t
{
	hio_rad_attr_hdr_t hdr;
	hio_uint32_t val;
};

#if (HIO_SIZEOF_UINT64_T > 0)
struct hio_rad_attr_uint64_t
{
	hio_rad_attr_hdr_t hdr;
	hio_uint64_t val;
};
#endif

#include <hio-upac.h>


typedef int (*hio_rad_attr_walker_t) (
	const hio_rad_hdr_t*      hdr, 
	hio_uint32_t              vendor, /* in host-byte order */
	const hio_rad_attr_hdr_t* attr, 
	void*                     ctx
);

#define HIO_RAD_ATTR_IS_SHORT_EXTENDED(attrtype) ((attrtype) >= HIO_RAD_ATTR_EXTENDED_1 && (attrtype) <= HIO_RAD_ATTR_EXTENDED_4)
#define HIO_RAD_ATTR_IS_LONG_EXTENDED(attrtype) ((attrtype) >= HIO_RAD_ATTR_EXTENDED_5 && (attrtype) <= HIO_RAD_ATTR_EXTENDED_6)
#define HIO_RAD_ATTR_IS_EXTENDED(attrtype) ((attrtype) >= HIO_RAD_ATTR_EXTENDED_1 && (attrtype) <= HIO_RAD_ATTR_EXTENDED_6)

/* The attribute code is an attribute type encoded in 2 byte integer. */
#define HIO_RAD_ATTR_CODE_MAKE(hi,lo) ((hio_uint16_t)((hi) & 0xFF) << 8 | ((lo) & 0xFF)) 
#define HIO_RAD_ATTR_CODE_HI(attrtype) (((attrtype) >> 8) & 0xFF)
#define HIO_RAD_ATTR_CODE_LO(attrtype) ((attrtype) & 0xFF)

#define HIO_RAD_ATTR_CODE_EXTENDED_1(lo) HIO_RAD_ATTR_CODE_MAKE(HIO_RAD_ATTR_EXTENDED_1, lo)
#define HIO_RAD_ATTR_CODE_EXTENDED_2(lo) HIO_RAD_ATTR_CODE_MAKE(HIO_RAD_ATTR_EXTENDED_2, lo)
#define HIO_RAD_ATTR_CODE_EXTENDED_3(lo) HIO_RAD_ATTR_CODE_MAKE(HIO_RAD_ATTR_EXTENDED_3, lo)
#define HIO_RAD_ATTR_CODE_EXTENDED_4(lo) HIO_RAD_ATTR_CODE_MAKE(HIO_RAD_ATTR_EXTENDED_4, lo)
#define HIO_RAD_ATTR_CODE_EXTENDED_5(lo) HIO_RAD_ATTR_CODE_MAKE(HIO_RAD_ATTR_EXTENDED_5, lo)
#define HIO_RAD_ATTR_CODE_EXTENDED_6(lo) HIO_RAD_ATTR_CODE_MAKE(HIO_RAD_ATTR_EXTENDED_6, lo)

enum hio_rad_attr_code_t
{
	/* -----------------------------------------------------------
	 * 1 byte attribute types. they can be used as a code or a type 
	 * ----------------------------------------------------------- */
	HIO_RAD_ATTR_USER_NAME             = 1,  /* string */
	HIO_RAD_ATTR_USER_PASSWORD         = 2,  /* string encrypted */
	HIO_RAD_ATTR_NAS_IP_ADDRESS        = 4,  /* ipaddr */
	HIO_RAD_ATTR_NAS_PORT              = 5,  /* integer */
	HIO_RAD_ATTR_SERVICE_TYPE          = 6,  /* integer */
	HIO_RAD_ATTR_FRAMED_IP_ADDRESS     = 8,  /* ipaddr */
	HIO_RAD_ATTR_REPLY_MESSAGE         = 18, /* string */
	HIO_RAD_ATTR_CLASS                 = 25, /* octets */
	HIO_RAD_ATTR_VENDOR_SPECIFIC       = 26, /* octets */
	HIO_RAD_ATTR_SESSION_TIMEOUT       = 27, /* integer */
	HIO_RAD_ATTR_IDLE_TIMEOUT          = 28, /* integer */
	HIO_RAD_ATTR_TERMINATION_ACTION    = 29, /* integer. 0:default, 1:radius-request */
	HIO_RAD_ATTR_CALLING_STATION_ID    = 31, /* string */
	HIO_RAD_ATTR_NAS_IDENTIFIER        = 32, /* string */
	HIO_RAD_ATTR_ACCT_STATUS_TYPE      = 40, /* integer */
	HIO_RAD_ATTR_ACCT_INPUT_OCTETS     = 42, /* integer */
	HIO_RAD_ATTR_ACCT_OUTPUT_OCTETS    = 43, /* integer */
	HIO_RAD_ATTR_ACCT_SESSION_ID       = 44, /* string */
	HIO_RAD_ATTR_ACCT_SESSION_TIME     = 46, /* integer */
	HIO_RAD_ATTR_ACCT_TERMINATE_CAUSE  = 49, /* integer */
	HIO_RAD_ATTR_ACCT_INPUT_GIGAWORDS  = 52, /* integer */
	HIO_RAD_ATTR_ACCT_OUTPUT_GIGAWORDS = 53, /* integer */
	HIO_RAD_ATTR_EVENT_TIMESTAMP       = 55, /* integer */
	HIO_RAD_ATTR_NAS_PORT_TYPE         = 61, /* integer */
	HIO_RAD_ATTR_ACCT_INTERIM_INTERVAL = 85, /* integer */
	HIO_RAD_ATTR_NAS_PORT_ID           = 87, /* string */
	HIO_RAD_ATTR_FRAMED_IPV6_PREFIX    = 97, /* ipv6prefix */

	HIO_RAD_ATTR_EXTENDED_1            = 241,
	HIO_RAD_ATTR_EXTENDED_2            = 242,
	HIO_RAD_ATTR_EXTENDED_3            = 243,
	HIO_RAD_ATTR_EXTENDED_4            = 244,
	HIO_RAD_ATTR_EXTENDED_5            = 245, /* long extended */
	HIO_RAD_ATTR_EXTENDED_6            = 246, /* long extended */

	 /* -----------------------------------------------------------
	 * 2-byte attribute codes. represented extended attributes.
	 * ----------------------------------------------------------- */
	HIO_RAD_ATTR_CODE_FRAG_STATUS           = HIO_RAD_ATTR_CODE_EXTENDED_1(1),
	HIO_RAD_ATTR_CODE_PROXY_STATE_LENGTH    = HIO_RAD_ATTR_CODE_EXTENDED_1(2),
};


enum hio_rad_attr_acct_status_type_t
{
	HIO_RAD_ATTR_ACCT_STATUS_TYPE_START  = 1, /* accounting start */
	HIO_RAD_ATTR_ACCT_STATUS_TYPE_STOP   = 2, /* accounting stop */
	HIO_RAD_ATTR_ACCT_STATUS_TYPE_UPDATE = 3, /* interim update */
	HIO_RAD_ATTR_ACCT_STATUS_TYPE_ON     = 7, /* accounting on */
	HIO_RAD_ATTR_ACCT_STATUS_TYPE_OFF    = 8, /* accounting off */
	HIO_RAD_ATTR_ACCT_STATUS_TYPE_FAILED = 15
};

enum hio_rad_attr_acct_terminate_cause_t
{
	HIO_RAD_ATTR_ACCT_TERMINATE_CAUSE_USER_REQUEST        = 1,
	HIO_RAD_ATTR_ACCT_TERMINATE_CAUSE_LOST_CARRIER        = 2,
	HIO_RAD_ATTR_ACCT_TERMINATE_CAUSE_LOST_SERVICE        = 3,
	HIO_RAD_ATTR_ACCT_TERMINATE_CAUSE_IDLE_TIMEOUT        = 4,
	HIO_RAD_ATTR_ACCT_TERMINATE_CAUSE_SESSION_TIMEOUT     = 5,
	HIO_RAD_ATTR_ACCT_TERMINATE_CAUSE_ADMIN_RESET         = 6,
	HIO_RAD_ATTR_ACCT_TERMINATE_CAUSE_ADMIN_REBOOT        = 7,
	HIO_RAD_ATTR_ACCT_TERMINATE_CAUSE_PORT_ERROR          = 8,
	HIO_RAD_ATTR_ACCT_TERMINATE_CAUSE_NAS_ERROR           = 9,
	HIO_RAD_ATTR_ACCT_TERMINATE_CAUSE_NAS_REQUEST         = 10,
	HIO_RAD_ATTR_ACCT_TERMINATE_CAUSE_NAS_REBOOT          = 11,
	HIO_RAD_ATTR_ACCT_TERMINATE_CAUSE_PORT_UNNEEDED       = 12,
	HIO_RAD_ATTR_ACCT_TERMINATE_CAUSE_PORT_PREEMPTED      = 13,
	HIO_RAD_ATTR_ACCT_TERMINATE_CAUSE_PORT_SUSPENDED      = 14,
	HIO_RAD_ATTR_ACCT_TERMINATE_CAUSE_SERVICE_UNAVAILABLE = 15,
	HIO_RAD_ATTR_ACCT_TERMINATE_CAUSE_CALLBACK            = 16,
	HIO_RAD_ATTR_ACCT_TERMINATE_CAUSE_USER_ERROR          = 17,
	HIO_RAD_ATTR_ACCT_TERMINATE_CAUSE_HOST_REQUEST        = 18
};

enum hio_rad_attr_nas_port_type_t
{
	HIO_RAD_ATTR_NAS_PORT_TYPE_ASYNC        = 0,
	HIO_RAD_ATTR_NAS_PORT_TYPE_SYNC         = 1,
	HIO_RAD_ATTR_NAS_PORT_TYPE_ISDN         = 2,
	HIO_RAD_ATTR_NAS_PORT_TYPE_ISDN_V120    = 3,
	HIO_RAD_ATTR_NAS_PORT_TYPE_ISDN_V110    = 4,
	HIO_RAD_ATTR_NAS_PORT_TYPE_VIRTUAL      = 5
	/* TODO: more types */
};

#if defined(__cplusplus)
extern "C" {
#endif

/* ----------------------------------------------------------------------- */

HIO_EXPORT void hio_rad_initialize (
	hio_rad_hdr_t*  hdr,
	hio_rad_code_t  code,
	hio_uint8_t     id
);

HIO_EXPORT int hio_rad_walk_attributes (
	const hio_rad_hdr_t*  hdr,
	hio_rad_attr_walker_t walker, 
	void*                 ctx
);


/* ----------------------------------------------------------------------- */

HIO_EXPORT hio_rad_attr_hdr_t* hio_rad_find_attr (
	hio_rad_hdr_t*      hdr,
	hio_uint16_t        attrcode,
	int                 index
);

HIO_EXPORT hio_rad_vsattr_hdr_t* hio_rad_find_vsattr (
	hio_rad_hdr_t*      hdr,
	hio_uint32_t        vendor,
	hio_uint16_t        attrcode,
	int                 index
);

HIO_EXPORT int hio_rad_delete_attr (
	hio_rad_hdr_t*      hdr,
	hio_uint16_t        attrcode,
	int                 index
);

HIO_EXPORT int hio_rad_delete_vsattr (
	hio_rad_hdr_t*      hdr,
	hio_uint32_t        vendor,
	hio_uint16_t        attrcode,
	int                 index
);

HIO_EXPORT hio_rad_attr_hdr_t* hio_rad_insert_attr (
	hio_rad_hdr_t*      auth,
	int                 max,
	hio_uint16_t        attrcode,
	const void*         ptr,
	hio_uint16_t        len
);

HIO_EXPORT hio_rad_vsattr_hdr_t* hio_rad_insert_vsattr (
	hio_rad_hdr_t*      auth,
	int                 max,
	hio_uint32_t        vendor,
	hio_uint16_t        attrcode,
	const void*         ptr,
	hio_uint16_t        len
);

/* ----------------------------------------------------------------------- */

HIO_EXPORT hio_rad_attr_hdr_t* hio_rad_find_attribute (
	hio_rad_hdr_t*  hdr,
	hio_uint8_t     attrtype,
	int             index
);

HIO_EXPORT hio_rad_xattr_hdr_t* hio_rad_find_extended_attribute (
	hio_rad_hdr_t*  hdr,
	hio_uint8_t     xtype,
	hio_uint8_t     attrtype,
	int             index
);

HIO_EXPORT hio_rad_vsattr_hdr_t* hio_rad_find_vendor_specific_attribute (
	hio_rad_hdr_t*  hdr,
	hio_uint32_t    vendor,
	hio_uint8_t     id,
	int             index
);

HIO_EXPORT hio_rad_xvsattr_hdr_t* hio_rad_find_extended_vendor_specific_attribute (
	hio_rad_hdr_t*  hdr,
	hio_uint32_t    vendor,
	hio_uint8_t     xtype,
	hio_uint8_t     attrtype,
	int             index
);

HIO_EXPORT int hio_rad_delete_attribute (
	hio_rad_hdr_t*  auth,
	hio_uint8_t     attrtype,
	int             index
);

HIO_EXPORT int hio_rad_delete_extended_attribute (
	hio_rad_hdr_t*  auth, 
	hio_uint8_t     xtype,
	hio_uint8_t     attrtype, 
	int             index
);

HIO_EXPORT int hio_rad_delete_vendor_specific_attribute (
	hio_rad_hdr_t*  auth,
	hio_uint32_t    vendor,
	hio_uint8_t     attrtype,
	int             index
);

HIO_EXPORT int hio_rad_delete_extended_vendor_specific_attribute (
	hio_rad_hdr_t*  auth,
	hio_uint32_t    vendor,
	hio_uint8_t     xtype, /* HIO_RAD_ATTR_EXTENDED_X */
	hio_uint8_t     attrtype,
	int             index
);

HIO_EXPORT hio_rad_attr_hdr_t* hio_rad_insert_attribute (
	hio_rad_hdr_t*  auth,
	int             max,
	hio_uint8_t     id,
	const void*     ptr,
	hio_uint8_t     len
);

HIO_EXPORT hio_rad_xattr_hdr_t* hio_rad_insert_extended_attribute (
	hio_rad_hdr_t*  auth,
	int             max,
	hio_uint8_t     xtype,
	hio_uint8_t     attrtype,
	const void*     ptr,
	hio_uint8_t     len,
	hio_uint8_t     lxflags
);

HIO_EXPORT hio_rad_vsattr_hdr_t* hio_rad_insert_vendor_specific_attribute (
	hio_rad_hdr_t*  auth,
	int             max,
	hio_uint32_t    vendor,
	hio_uint8_t     attrtype, 
	const void*     ptr,
	hio_uint8_t     len
);

HIO_EXPORT hio_rad_xvsattr_hdr_t* hio_rad_insert_extended_vendor_specific_attribute (
	hio_rad_hdr_t*  auth,
	int             max,
	hio_uint32_t    vendor,
	hio_uint8_t     xtype, /* HIO_RAD_ATTR_EXTENDED_X */
	hio_uint8_t     attrtype, 
	const void*     ptr,
	hio_uint8_t     len,
	hio_uint8_t     lxflags
);


HIO_EXPORT hio_rad_attr_hdr_t* hio_rad_insert_attribute_with_bcstr (
	hio_rad_hdr_t*     auth, 
	int                max, 
	hio_uint32_t       vendor, /* in host-byte order */
	hio_uint8_t        id, 
	const hio_bch_t*   value
);

HIO_EXPORT hio_rad_attr_hdr_t* hio_rad_insert_attribute_ucstr (
	hio_rad_hdr_t*     auth, 
	int                max, 
	hio_uint32_t       vendor, /* in host-byte order */
	hio_uint8_t        id, 
	const hio_uch_t*   value
);

HIO_EXPORT hio_rad_attr_hdr_t* hio_rad_insert_attribute_with_bchars (
	hio_rad_hdr_t*     auth, 
	int                max, 
	hio_uint32_t       vendor, /* in host-byte order */
	hio_uint8_t        id, 
	const hio_bch_t*   value,
	hio_uint8_t        length
);

HIO_EXPORT hio_rad_attr_hdr_t* hio_rad_insert_attribute_with_uchars (
	hio_rad_hdr_t*     auth, 
	int                max, 
	hio_uint32_t       vendor, /* in host-byte order */
	hio_uint8_t        id, 
	const hio_uch_t*   value,
	hio_uint8_t        length
);

HIO_EXPORT hio_rad_attr_hdr_t* hio_rad_insert_uint32_attribute (
	hio_rad_hdr_t*     auth, 
	int                max,
	hio_uint32_t       vendor, /* in host-byte order */
	hio_uint8_t        id,
	hio_uint32_t       value /* in host-byte order */
);

HIO_EXPORT hio_rad_attr_hdr_t* hio_rad_insert_ipv6prefix_attribute (
	hio_rad_hdr_t*     auth, 
	int                max,
	hio_uint32_t       vendor, /* in host-byte order */
	hio_uint8_t        id,
	hio_uint8_t        prefix_bits,
	const hio_ip6ad_t* value
);

#if (HIO_SIZEOF_UINT64_T > 0)
HIO_EXPORT hio_rad_attr_hdr_t* hio_rad_insert_giga_attribute (
	hio_rad_hdr_t*     auth,
	int                max,
	hio_uint32_t       vendor,
	int                low_id,
	int                high_id,
	hio_uint64_t       value
);
#endif

/* ----------------------------------------------------------------------- */
 
HIO_EXPORT int hio_rad_set_user_password (
	hio_rad_hdr_t*     auth,
	int                max,
	const hio_bch_t*   password,
	const hio_bch_t*   secret
);

HIO_EXPORT void hio_rad_fill_authenticator (
	hio_rad_hdr_t*     auth
);

HIO_EXPORT void hio_rad_copy_authenticator (
	hio_rad_hdr_t*       dst,
	const hio_rad_hdr_t* src
);

HIO_EXPORT int hio_rad_set_authenticator (
	hio_rad_hdr_t*     req, 
	const hio_bch_t*   secret
);

/* 
 * verify an accounting request.
 * the authenticator of an access request is filled randomly.
 * so this function doesn't apply
 */
HIO_EXPORT int hio_rad_verify_request (
	hio_rad_hdr_t*      req,
	const hio_bch_t*    secret
);

HIO_EXPORT int hio_rad_verify_response (
	hio_rad_hdr_t*         res,
	const hio_rad_hdr_t*   req,
	const hio_bch_t*       secret
);

#if defined(__cplusplus)
}
#endif

#endif
