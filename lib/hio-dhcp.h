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

#ifndef _HIO_DHCP_H_
#define _HIO_DHCP_H_

#include <hio.h>
#include <hio-sck.h>

/* ---------------------------------------------------------------- */

#define HIO_DHCP4_SERVER_PORT   (67)
#define HIO_DHCP4_CLIENT_PORT   (68)
#define HIO_DHCP4_MAGIC_COOKIE  (0x63825363)

/* operation code */
enum hio_dhcp4_op_t
{
	HIO_DHCP4_OP_BOOTREQUEST = 1,
	HIO_DHCP4_OP_BOOTREPLY   = 2
};

enum hio_dhcp4_htype_t
{
	HIO_DHCP4_HTYPE_ETHERNET   = 1,
	HIO_DHCP4_HTYPE_IEEE802    = 6,
	HIO_DHCP4_HTYPE_ARCNET     = 7,
	HIO_DHCP4_HTYPE_APPLETALK  = 8,
	HIO_DHCP4_HTYPE_HDLC       = 17,
	HIO_DHCP4_HTYPE_ATM        = 19,
	HIO_DHCP4_HTYPE_INFINIBAND = 32
};

/* option codes (partial) */
/** The only flag RFC 2131 defines in the bootp flags field. A client sets it
 *  to say it cannot receive a unicast reply before it has an address, so a
 *  reply to such a request has to be broadcast. The field is on the wire in
 *  network order, so test it as hio_ntoh16(hdr->flags) & this. */
#define HIO_DHCP4_FLAG_BROADCAST (0x8000)

enum hio_dhcp4_opt_t
{
	HIO_DHCP4_OPT_PADDING          = 0x00,
	HIO_DHCP4_OPT_SUBNET           = 0x01,
	HIO_DHCP4_OPT_TIME_OFFSET      = 0x02,
	HIO_DHCP4_OPT_ROUTER           = 0x03,
	HIO_DHCP4_OPT_TIME_SERVER      = 0x04,
	HIO_DHCP4_OPT_NAME_SERVER      = 0x05,
	HIO_DHCP4_OPT_DNS_SERVER       = 0x06,
	HIO_DHCP4_OPT_LOG_SERVER       = 0x07,
	HIO_DHCP4_OPT_COOKIE_SERVER    = 0x08,
	HIO_DHCP4_OPT_LPR_SERVER       = 0x09,
	HIO_DHCP4_OPT_HOST_NAME        = 0x0c,
	HIO_DHCP4_OPT_BOOT_SIZE        = 0x0d,
	HIO_DHCP4_OPT_DOMAIN_NAME      = 0x0f,
	HIO_DHCP4_OPT_SWAP_SERVER      = 0x10,
	HIO_DHCP4_OPT_ROOT_PATH        = 0x11,
	HIO_DHCP4_OPT_IP_TTL           = 0x17,
	HIO_DHCP4_OPT_MTU              = 0x1a,
	HIO_DHCP4_OPT_BROADCAST        = 0x1c,
	HIO_DHCP4_OPT_NTP_SERVER       = 0x2a,
	HIO_DHCP4_OPT_VENDOR_INFO      = 0x2b, /* 43 - vendor specific information */
	HIO_DHCP4_OPT_WINS_SERVER      = 0x2c,
	HIO_DHCP4_OPT_REQUESTED_IPADDR = 0x32,
	HIO_DHCP4_OPT_LEASE_TIME       = 0x33,
	HIO_DHCP4_OPT_OVERLOAD         = 0x34, /* overload sname or file */
	HIO_DHCP4_OPT_MESSAGE_TYPE     = 0x35,
	HIO_DHCP4_OPT_SERVER_ID        = 0x36,
	HIO_DHCP4_OPT_PARAM_REQ        = 0x37,
	HIO_DHCP4_OPT_MESSAGE          = 0x38,
	HIO_DHCP4_OPT_MAX_SIZE         = 0x39,
	HIO_DHCP4_OPT_T1               = 0x3a,
	HIO_DHCP4_OPT_T2               = 0x3b,
	/* [NOTE] this was HIO_DHCP4_OPT_VENDOR, which read as option 43's meaning
	 * while carrying option 60's value. RFC 2132 calls 60 the vendor class
	 * identifier; vendor specific information is 43, above. */
	HIO_DHCP4_OPT_CLASS_ID         = 0x3c, /* 60 - vendor class identifier */
	HIO_DHCP4_OPT_CLIENT_ID        = 0x3d,
	HIO_DHCP4_OPT_RELAY            = 0x52,
	HIO_DHCP4_OPT_SUBNET_SELECTION = 0x76,
	HIO_DHCP4_OPT_VENDOR_VC        = 0x7c, /* 124 - vendor-identifying vendor class */
	HIO_DHCP4_OPT_VENDOR_VSI       = 0x7d, /* 125 - vendor-identifying vendor-specific information */
	HIO_DHCP4_OPT_PRIVATE_SRCADDR  = 0xFE, /* site-local. used by dhcpa and dhcpb */
	HIO_DHCP4_OPT_END              = 0xFF
};

/* flags for HIO_DHCP4_OPT_OVERLOAD */
enum hio_dhcp4_opt_overload_t
{
	HIO_DHCP4_OPT_OVERLOAD_FILE  = (1 << 0),
	HIO_DHCP4_OPT_OVERLOAD_SNAME = (1 << 1)
};

/* flags for HIO_DHCP4_OPT_OVERLOAD */
enum hio_dhcp4_opt_relay_t
{
	HIO_DHCP4_OPT_RELAY_CIRCUIT_ID  = 1,
	HIO_DHCP4_OPT_RELAY_REMOTE_ID   = 2
};

/* message type */
enum hio_dhcp4_msg_t
{
	HIO_DHCP4_MSG_DISCOVER         = 1,
	HIO_DHCP4_MSG_OFFER            = 2,
	HIO_DHCP4_MSG_REQUEST          = 3,
	HIO_DHCP4_MSG_DECLINE          = 4,
	HIO_DHCP4_MSG_ACK              = 5,
	HIO_DHCP4_MSG_NAK              = 6,
	HIO_DHCP4_MSG_RELEASE          = 7,
	HIO_DHCP4_MSG_INFORM           = 8,

	HIO_DHCP4_MSG_FORCE_RENEW      = 9,

	HIO_DHCP4_MSG_LEASE_QUERY      = 10,
	HIO_DHCP4_MSG_LEASE_UNASSIGNED = 11,
	HIO_DHCP4_MSG_LEASE_UNKNOWN    = 12,
	HIO_DHCP4_MSG_LEASE_ACTIVE     = 13,

	HIO_DHCP4_MSG_BULK_LEASE_QUERY = 14,
	HIO_DHCP4_MSG_LEASE_QUERY_DONE = 15,

	HIO_DHCP4_MSG_ACTIVE_LEASE_QUERY = 16,
	HIO_DHCP4_MSG_LEASE_QUERY_STATUS = 17,
	HIO_DHCP4_MSG_TLS                = 18
};

/* --------------------------------------------------- */
#include <hio-pac1.h>
/* --------------------------------------------------- */

struct hio_dhcp4_pkt_hdr_t
{
	hio_uint8_t  op;
	hio_uint8_t  htype;
	hio_uint8_t  hlen;
	hio_uint8_t  hops;
	hio_uint32_t xid;        /* transaction id */
	hio_uint16_t secs;       /* seconds elapsed */
	hio_uint16_t flags;      /* bootp flags. see #HIO_DHCP4_FLAG_BROADCAST */
	hio_uint32_t ciaddr;     /* client ip */
	hio_uint32_t yiaddr;     /* your ip */
	hio_uint32_t siaddr;     /* next server ip */
	hio_uint32_t giaddr;     /* relay agent ip */
	hio_uint8_t  chaddr[16]; /* client mac */

	char     sname[64];      /* server host name */
	char     file[128];      /* boot file name */

	/* options are placed after the header.
	 * the first four bytes of the options compose a magic cookie
	 * 0x63 0x82 0x53 0x63 */
};
typedef struct hio_dhcp4_pkt_hdr_t hio_dhcp4_pkt_hdr_t;

struct hio_dhcp4_opt_hdr_t
{
	hio_uint8_t code;
	hio_uint8_t len;
};
typedef struct hio_dhcp4_opt_hdr_t hio_dhcp4_opt_hdr_t;

/* --------------------------------------------------- */
#include <hio-upac.h>
/* --------------------------------------------------- */


typedef int (*hio_dhcp4_opt_walker_t) (hio_dhcp4_opt_hdr_t* opt);

struct hio_dhcp4_pktinf_t
{
	hio_dhcp4_pkt_hdr_t* hdr;
	hio_oow_t            len;
};
typedef struct hio_dhcp4_pktinf_t hio_dhcp4_pktinf_t;

struct hio_dhcp4_pktbuf_t
{
	hio_dhcp4_pkt_hdr_t* hdr;
	hio_oow_t            len;
	hio_oow_t            capa;
};
typedef struct hio_dhcp4_pktbuf_t hio_dhcp4_pktbuf_t;


/* ---------------------------------------------------------------- */

#define HIO_DHCP6_SERVER_PORT     (547)
#define HIO_DHCP6_CLIENT_PORT     (546)
#define HIO_DHCP6_HOP_COUNT_LIMIT (32)

enum hio_dhcp6_msg_t
{
	HIO_DHCP6_MSG_SOLICIT     = 1,
	HIO_DHCP6_MSG_ADVERTISE   = 2,
	HIO_DHCP6_MSG_REQUEST     = 3,
	HIO_DHCP6_MSG_CONFIRM     = 4,
	HIO_DHCP6_MSG_RENEW       = 5,
	HIO_DHCP6_MSG_REBIND      = 6,
	HIO_DHCP6_MSG_REPLY       = 7,
	HIO_DHCP6_MSG_RELEASE     = 8,
	HIO_DHCP6_MSG_DECLINE     = 9,
	HIO_DHCP6_MSG_RECONFIGURE = 10,
	HIO_DHCP6_MSG_INFOREQ     = 11,
	HIO_DHCP6_MSG_RELAYFORW   = 12,
	HIO_DHCP6_MSG_RELAYREPL   = 13,
};
typedef enum hio_dhcp6_msg_t hio_dhcp6_msg_t;

enum hio_dhcp6_opt_t
{
	HIO_DHCP6_OPT_CLIENTID = 1,
	HIO_DHCP6_OPT_SERVERID = 2,
	HIO_DHCP6_OPT_IA_NA = 3,
	HIO_DHCP6_OPT_IA_TA = 4,
	HIO_DHCP6_OPT_IAADDR = 5,
	HIO_DHCP6_OPT_PREFERENCE = 7,
	HIO_DHCP6_OPT_ELAPSED_TIME = 8,
	HIO_DHCP6_OPT_RELAY_MESSAGE = 9,
	HIO_DHCP6_OPT_RAPID_COMMIT = 14,
	HIO_DHCP6_OPT_USER_CLASS = 15,
	HIO_DHCP6_OPT_VENDOR_CLASS = 16,
	HIO_DHCP6_OPT_VENDOR_VSI = 17, /* vendor-specific information */
	HIO_DHCP6_OPT_INTERFACE_ID = 18,
	HIO_DHCP6_OPT_IA_PD = 25,
	HIO_DHCP6_OPT_IAPREFIX = 26
};
typedef enum hio_dhcp6_opt_t hio_dhcp6_opt_t;

/* --------------------------------------------------- */
#include <hio-pac1.h>
/* --------------------------------------------------- */

struct hio_dhcp6_pkt_hdr_t
{
	hio_uint8_t msgtype;
	hio_uint8_t transid[3];
};
typedef struct hio_dhcp6_pkt_hdr_t hio_dhcp6_pkt_hdr_t;

struct hio_dhcp6_relay_hdr_t
{
	hio_uint8_t msgtype;  /* RELAY-FORW, RELAY-REPL */
	hio_uint8_t hopcount;
	hio_uint8_t linkaddr[16];
	hio_uint8_t peeraddr[16];
};
typedef struct hio_dhcp6_relay_hdr_t hio_dhcp6_relay_hdr_t;

struct hio_dhcp6_opt_hdr_t
{
	hio_uint16_t code;
	hio_uint16_t len; /* length of option data, excludes the option header */
};
typedef struct hio_dhcp6_opt_hdr_t hio_dhcp6_opt_hdr_t;

/* --------------------------------------------------- */
#include <hio-upac.h>
/* --------------------------------------------------- */

struct hio_dhcp6_pktinf_t
{
	hio_dhcp6_pkt_hdr_t* hdr;
	hio_oow_t            len;
};
typedef struct hio_dhcp6_pktinf_t hio_dhcp6_pktinf_t;

struct hio_dhcp6_pktbuf_t
{
	/* ---------------------------------------------------------------- */
	/* this part must match hio_dhcp6_pktinf_t: a hio_dhcp6_pktbuf_t* is */
	/* cast down to a hio_dhcp6_pktinf_t* by the functions that only     */
	/* read, so that a caller holding a buffer need not build a second   */
	/* structure to inspect it.                                         */
	hio_dhcp6_pkt_hdr_t* hdr;
	hio_oow_t            len;
	/* ---------------------------------------------------------------- */
	hio_oow_t            capa;
};
typedef struct hio_dhcp6_pktbuf_t hio_dhcp6_pktbuf_t;

/* ---------------------------------------------------------------- */

#if defined(__cplusplus)
extern "C" {
#endif

HIO_EXPORT int hio_dhcp4_init_pktbuf (
	hio_dhcp4_pktbuf_t* pkt,
	void*               buf,
	hio_oow_t           capa
);

HIO_EXPORT int hio_dhcp4_add_option (
	hio_dhcp4_pktbuf_t* pkt,
	int                 code,
	void*               optr, /**< option data pointer */
	hio_uint8_t         olen  /**< option data length */
);

HIO_EXPORT int hio_dhcp4_delete_option (
	hio_dhcp4_pktbuf_t* pkt,
	int                 code
);

/**
 * The hio_dhcp4_compact_options() function moves options out of the options
 * area into the \b sname and \b file fields, which a packet carrying neither
 * a server host name nor a boot file name has no other use for. The
 * #HIO_DHCP4_OPT_OVERLOAD option it adds is what tells a reader to interpret
 * them that way, and it is what hio_dhcp4_find_option() and
 * hio_dhcp4_walk_options() already honour. It buys up to 190 octets of option
 * space without growing the packet.
 *
 * A reader takes the fields after the options area, so the options that move
 * are the trailing ones and they keep their order: a walk over the compacted
 * packet reports the same options in the same sequence as before.
 *
 * Only a field that is entirely zero is taken over, so a caller that has set a
 * boot file name keeps it - RFC 2132 gives options 66 and 67 for carrying
 * those by name when the fields are wanted for options instead. The packet is
 * left alone if neither field is free, if it already carries an overload
 * option, or if the move would not pay for the three octets that option costs.
 *
 * Call this before appending #HIO_DHCP4_OPT_END. The end option terminates the
 * options area, and anything hio_dhcp4_add_option() appends past it is
 * invisible to a reader.
 *
 * \return the number of octets freed from the options area, 0 if the packet
 *         was left as it was, or -1 if the packet is malformed.
 */
HIO_EXPORT int hio_dhcp4_compact_options (
	hio_dhcp4_pktbuf_t* pkt
);

#if 0
HIO_EXPORT int hio_dhcp4_add_options (
	hio_dhcp4_pkt_hdr_t* pkt,
	hio_oow_t        len,
	hio_oow_t        max,
	int              code,
	hio_uint8_t*     optr, /* option data */
	hio_uint8_t      olen  /* option length */
);
#endif

/* ---------------------------------------------------------------- */
/* typed option access                                              */
/*                                                                  */
/* option payloads are not aligned - they sit wherever the preceding */
/* options left them - so a multi-octet value must be copied out     */
/* rather than read through a cast. these do that, and the byte      */
/* order conversion, so that no caller has to remember either.       */
/* ---------------------------------------------------------------- */

HIO_EXPORT int hio_dhcp4_add_option_uint8 (
	hio_dhcp4_pktbuf_t* pkt,
	int                 code,
	hio_uint8_t         value
);

HIO_EXPORT int hio_dhcp4_add_option_uint16 (
	hio_dhcp4_pktbuf_t* pkt,
	int                 code,
	hio_uint16_t        value  /**< in host order */
);

HIO_EXPORT int hio_dhcp4_add_option_uint32 (
	hio_dhcp4_pktbuf_t* pkt,
	int                 code,
	hio_uint32_t        value  /**< in host order */
);

/**
 * Read a fixed-width option. Returns 0 and stores the value, or -1 if the
 * option is absent or is not exactly the width asked for - a length that
 * disagrees with the option's definition is a malformed packet, not a value
 * to be salvaged.
 */
HIO_EXPORT int hio_dhcp4_get_option_uint8 (
	const hio_dhcp4_pktinf_t* pkt,
	int                       code,
	hio_uint8_t*              value
);

HIO_EXPORT int hio_dhcp4_get_option_uint16 (
	const hio_dhcp4_pktinf_t* pkt,
	int                       code,
	hio_uint16_t*             value  /**< stored in host order */
);

HIO_EXPORT int hio_dhcp4_get_option_uint32 (
	const hio_dhcp4_pktinf_t* pkt,
	int                       code,
	hio_uint32_t*             value  /**< stored in host order */
);

/**
 * Read an option's payload without copying it. Returns 0 and points 'ptr' into
 * the packet, or -1 if the option is absent. The pointer is only valid while
 * the packet is.
 */
HIO_EXPORT int hio_dhcp4_get_option_data (
	const hio_dhcp4_pktinf_t* pkt,
	int                       code,
	const hio_uint8_t**       ptr,
	hio_uint8_t*              len
);

/* ---------------------------------------------------------------- */
/* message-level helpers shared by both ends                        */
/* ---------------------------------------------------------------- */

/**
 * The DHCP message type, option 53. Returns 0 and stores it, or -1 if the
 * option is missing or malformed - which is what tells a BOOTP packet from a
 * DHCP one, so both ends check it before anything else.
 */
HIO_EXPORT int hio_dhcp4_get_msg_type (
	const hio_dhcp4_pktinf_t* pkt,
	hio_uint8_t*              mtype
);

/**
 * What identifies the client: option 61 if it sent one, otherwise the hardware
 * type and address from the header. Both ends have to derive this the same way
 * or a lease looked up by one will not be the lease recorded by the other, so
 * it lives here rather than in either of them.
 *
 * On success 'ptr' points into the packet and 'len' is its length. -1 means
 * there is nothing usable - no client id option and no hardware address.
 */
HIO_EXPORT int hio_dhcp4_get_client_id (
	const hio_dhcp4_pktinf_t* pkt,
	const hio_uint8_t**       ptr,
	hio_uint8_t*              len
);

/**
 * Check that a received buffer is shaped like a DHCP packet at all: long
 * enough for the fixed header, carrying the magic cookie, and with a hardware
 * address length that fits the field. Returns 0 or -1.
 *
 * Both ends call this on every datagram before looking at anything in it.
 */
HIO_EXPORT int hio_dhcp4_check_pkt (
	const hio_dhcp4_pktinf_t* pkt
);

/**
 * Start a reply to a request, copying over the fields a reply must echo: the
 * transaction id, the broadcast flag, the relay address, and the client's
 * hardware address. The caller sets yiaddr/siaddr and adds options.
 */
HIO_EXPORT int hio_dhcp4_init_reply_pktbuf (
	hio_dhcp4_pktbuf_t*       pkt,
	void*                     buf,
	hio_oow_t                 capa,
	const hio_dhcp4_pktinf_t* req
);

HIO_EXPORT int hio_dhcp4_walk_options (
	const hio_dhcp4_pktinf_t* pkt,
	hio_dhcp4_opt_walker_t    walker
);

HIO_EXPORT hio_dhcp4_opt_hdr_t* hio_dhcp4_find_option (
	const hio_dhcp4_pktinf_t* pkt,
	int                       code
);

/**
 * Return a pointer to an option's value, past its header, and its length.
 * #HIO_NULL if the option is absent.
 */
HIO_EXPORT hio_uint8_t* hio_dhcp4_get_option_value (
	const hio_dhcp4_pktinf_t* pkt,
	int                       code,
	hio_uint8_t*              olen
);

/**
 * Find a suboption inside the value of a relay agent information option
 * (#HIO_DHCP4_OPT_RELAY). 'ptr' points at the option's value, not its header:
 *
 *     |  82  |  N  | SUBOPT... | SUBOPT... |
 *                  ^ ptr
 *
 * Each suboption is a one-octet code, a one-octet length, then the value.
 * Returns a pointer to the value, or #HIO_NULL.
 *
 * [NOTE] this was hio_dhcp4_get_relay_suboption(). Renamed to say that what
 * comes back is the value rather than the suboption header, which is what
 * hio_dhcp4_find_option() returns for an option and is easy to confuse.
 */
HIO_EXPORT hio_uint8_t* hio_dhcp4_get_relay_suboption_value (
	const hio_uint8_t* ptr,
	hio_uint8_t        len,
	int                relay_subcode,
	hio_uint8_t*       olen
);

/**
 * Find the relay agent information option in a packet and a suboption within
 * it, in one step. Returns a pointer to the suboption's value, or #HIO_NULL if
 * either is absent.
 */
HIO_EXPORT hio_uint8_t* hio_dhcp4_find_relay_suboption_value (
	const hio_dhcp4_pktinf_t* pkt,
	int                       relay_subcode,
	hio_uint8_t*              olen
);

/**
 * Remove one suboption from the relay agent information option, and the
 * option itself if that was its last suboption.
 *
 * Returns 0, or -1 if the suboption is not there or the option's length
 * disagrees with what it contains.
 */
HIO_EXPORT int hio_dhcp4_delete_relay_suboption (
	hio_dhcp4_pktbuf_t*       pkt,
	int                       relay_subcode
);

/**
 * Append a suboption to the relay agent information option, creating that
 * option if the packet does not have one yet.
 *
 * Fails with -1 if the packet has no room, if the suboption would take the
 * option's length past the 255 an option length field can express, or if the
 * existing option lives in the overloaded sname/file area - which cannot be
 * grown. Delete it and add it again to move it into the options field.
 */
HIO_EXPORT int hio_dhcp4_add_relay_suboption (
	hio_dhcp4_pktbuf_t*       pkt,
	int                       relay_subcode,
	const hio_uint8_t*        dptr,
	hio_uint8_t               dlen
);

/* ---------------------------------------------------------------- */

/** The most a relay chain may be nested. RFC 8415 caps a relay's hop count at
 *  32, so a packet claiming more nesting than that is malformed - and without a
 *  cap the recursive traversals below would exhaust the stack on one. */
#define HIO_DHCP6_MAX_RELAY_LEVEL HIO_DHCP6_HOP_COUNT_LIMIT

/** An option's length field is 16 bits, so this is the most option data one
 *  can carry. */
#define HIO_DHCP6_MAX_OPT_DLEN (0xFFFF)

/**
 * How many relay layers wrap a packet. 0 for a message that is not relayed.
 *
 * A relayed DHCPv6 message is not a header with a flag - the whole inner
 * message is carried as the value of a RELAY-MSG option inside the outer one,
 * so "level" below means how far in to look.
 */
HIO_EXPORT int hio_dhcp6_get_relay_level (
	const hio_dhcp6_pktinf_t* pkt
);

/**
 * The message header at a given relay level. A negative level means the
 * innermost. #HIO_NULL if the packet is too short or does not nest that far.
 */
HIO_EXPORT hio_dhcp6_pkt_hdr_t* hio_dhcp6_get_pkt_hdr (
	const hio_dhcp6_pktinf_t* pkt,
	int                       level
);

/**
 * Find an option at a relay level. A negative level searches the innermost
 * message.
 */
HIO_EXPORT hio_dhcp6_opt_hdr_t* hio_dhcp6_find_option (
	const hio_dhcp6_pktinf_t* pkt,
	int                       level,
	int                       code
);

/**
 * The next option with this code after 'pos', or the first if 'pos' is
 * #HIO_NULL - for walking the several options that may share a code.
 */
HIO_EXPORT hio_dhcp6_opt_hdr_t* hio_dhcp6_find_option_after (
	const hio_dhcp6_pktinf_t*  pkt,
	int                        level,
	const hio_dhcp6_opt_hdr_t* pos,
	int                        code
);

/** The next option of any code after 'pos', for walking every option. */
HIO_EXPORT hio_dhcp6_opt_hdr_t* hio_dhcp6_get_option_after (
	const hio_dhcp6_pktinf_t*  pkt,
	int                        level,
	const hio_dhcp6_opt_hdr_t* pos
);

HIO_EXPORT int hio_dhcp6_init_pktbuf (
	hio_dhcp6_pktbuf_t* pkt,
	void*               buf,
	hio_oow_t           capa
);

/**
 * Insert an option at 'pos', or append it if 'pos' is #HIO_NULL. When the
 * level is inside a relay chain, the enclosing RELAY-MSG options grow to
 * match.
 *
 * 'safe' asks for 'pos' to be checked against the options actually present
 * first. Without it a wrong position corrupts the packet, so pass 0 only for a
 * position this same packet just returned.
 *
 * #HIO_NULL if the packet has no room, if the data is longer than an option
 * length field can express, or if growing an enclosing relay option would
 * take it past that.
 */
HIO_EXPORT hio_dhcp6_opt_hdr_t* hio_dhcp6_insert_option_at (
	hio_dhcp6_pktbuf_t*   pkt,
	int                   level,
	hio_dhcp6_opt_hdr_t*  pos,
	int                   safe,
	int                   code,
	const void*           data,
	hio_oow_t             dlen
);

/** Replace the option at 'pos'. The packet grows or shrinks to fit, and the
 *  enclosing relay options follow. See hio_dhcp6_insert_option_at() for
 *  'safe'. */
HIO_EXPORT hio_dhcp6_opt_hdr_t* hio_dhcp6_replace_option_at (
	hio_dhcp6_pktbuf_t*   pkt,
	int                   level,
	hio_dhcp6_opt_hdr_t*  pos,
	int                   safe,
	int                   code,
	const void*           data,
	hio_oow_t             dlen
);

/** Delete the option at 'pos'. See hio_dhcp6_insert_option_at() for 'safe'. */
HIO_EXPORT int hio_dhcp6_delete_option_at (
	hio_dhcp6_pktbuf_t*   pkt,
	int                   level,
	hio_dhcp6_opt_hdr_t*  pos,
	int                   safe
);

/* ---------------------------------------------------------------- */

#if defined(__cplusplus)
}
#endif

#endif
