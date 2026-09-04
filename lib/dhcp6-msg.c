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


/*
 * DHCPv6 message and option manipulation.
 *
 * A relayed DHCPv6 message does not carry a flag - the entire inner message is
 * the value of a RELAY-MSG option inside the outer one, so the options a caller
 * wants may be several messages deep. Every function here therefore takes a
 * relay level, and editing an option at a level means fixing up the length of
 * every RELAY-MSG option that encloses it.
 */

#include <hio-dhcp.h>
#include "hio-prv.h"

/* one descriptor per operation, so the recursion that walks in through the
 * relay chain carries a single argument rather than seven */

struct find_t
{
	int max_level;
	const hio_dhcp6_opt_hdr_t* pos;
	int code;
	int any_code; /* walk every option rather than matching 'code' */
};
typedef struct find_t find_t;

struct insert_t
{
	hio_dhcp6_pktbuf_t* pb;
	int max_level;
	hio_dhcp6_opt_hdr_t* pos;
	int safe;
	int code;
	const void* data;
	hio_oow_t dlen;
};
typedef struct insert_t insert_t;

struct delete_t
{
	hio_dhcp6_pktbuf_t* pb;
	int max_level;
	hio_dhcp6_opt_hdr_t* pos;
	int safe;
	hio_oow_t dlen; /* size of the option being removed, header included */
};
typedef struct delete_t delete_t;

struct replace_t
{
	hio_dhcp6_pktbuf_t* pb;
	int max_level;
	hio_dhcp6_opt_hdr_t* pos;
	int safe;
	int code;
	const void* data;
	hio_oow_t dlen;
	hio_oow_t old_len; /* size of the option being replaced, header included */
	hio_ooi_t rlen; /* how much the packet grows or shrinks */
};
typedef struct replace_t replace_t;

/* ------------------------------------------------------------------------- */
/* relay chain traversal                                                     */
/* ------------------------------------------------------------------------- */

static HIO_INLINE int is_relay_msgtype (hio_uint8_t t)
{
	return t == HIO_DHCP6_MSG_RELAYFORW || t == HIO_DHCP6_MSG_RELAYREPL;
}

/* the inner message carried by a RELAY-MSG option: where it starts and how
 * long it is. */
static HIO_INLINE hio_oow_t relay_inner_len (const hio_dhcp6_opt_hdr_t* opt)
{
	return hio_ntoh16(opt->len);
}

/* step to the option after this one, or HIO_NULL if it does not fit in 'rem'.
 * every traversal goes through here so that one bounds check covers them all. */
static const hio_dhcp6_opt_hdr_t* next_option (const hio_dhcp6_opt_hdr_t* opt, hio_oow_t* rem)
{
	hio_oow_t opt_len;

	if (*rem < HIO_SIZEOF(*opt)) return HIO_NULL;

	opt_len = HIO_SIZEOF(*opt) + hio_ntoh16(opt->len);
	if (*rem < opt_len) return HIO_NULL; /* the length field runs past the end */

	*rem -= opt_len;
	return (const hio_dhcp6_opt_hdr_t*)((const hio_uint8_t*)(opt + 1) + hio_ntoh16(opt->len));
}

/* where the options of a message begin, and how many octets of them there are.
 * a relay message has a longer fixed header than an ordinary one. */
static int option_area (const hio_dhcp6_pkt_hdr_t* phdr, hio_oow_t plen, const hio_dhcp6_opt_hdr_t** opt, hio_oow_t* rem)
{
	if (plen < HIO_SIZEOF(hio_dhcp6_pkt_hdr_t)) return -1;

	if (is_relay_msgtype(phdr->msgtype))
	{
		if (plen < HIO_SIZEOF(hio_dhcp6_relay_hdr_t)) return -1;
		*rem = plen - HIO_SIZEOF(hio_dhcp6_relay_hdr_t);
		*opt = (const hio_dhcp6_opt_hdr_t*)(((const hio_dhcp6_relay_hdr_t*)phdr) + 1);
	}
	else
	{
		*rem = plen - HIO_SIZEOF(hio_dhcp6_pkt_hdr_t);
		*opt = (const hio_dhcp6_opt_hdr_t*)(phdr + 1);
	}

	return 0;
}

/* the RELAY-MSG option in this message's option area, or HIO_NULL */
static const hio_dhcp6_opt_hdr_t* find_relay_msg (const hio_dhcp6_opt_hdr_t* opt, hio_oow_t rem)
{
	while (rem >= HIO_SIZEOF(*opt))
	{
		if (hio_ntoh16(opt->code) == HIO_DHCP6_OPT_RELAY_MESSAGE)
		{
			/* and its value must actually be inside the packet */
			if (rem < HIO_SIZEOF(*opt) + (hio_oow_t)hio_ntoh16(opt->len)) return HIO_NULL;
			return opt;
		}
		opt = next_option(opt, &rem);
		if (!opt) break;
	}

	return HIO_NULL;
}

int hio_dhcp6_get_relay_level (const hio_dhcp6_pktinf_t* pkt)
{
	const hio_dhcp6_pkt_hdr_t* phdr = pkt->hdr;
	hio_oow_t plen = pkt->len;
	int depth = 0;

	while (depth < HIO_DHCP6_MAX_RELAY_LEVEL)
	{
		const hio_dhcp6_opt_hdr_t* opt;
		const hio_dhcp6_opt_hdr_t* rm;
		hio_oow_t rem;

		if (plen < HIO_SIZEOF(hio_dhcp6_relay_hdr_t) || !is_relay_msgtype(phdr->msgtype)) break;
		if (option_area(phdr, plen, &opt, &rem) <= -1) break;

		rm = find_relay_msg(opt, rem);
		if (!rm) break;

		plen = relay_inner_len(rm);
		phdr = (const hio_dhcp6_pkt_hdr_t*)(rm + 1);
		depth++;
	}

	return depth;
}

hio_dhcp6_pkt_hdr_t* hio_dhcp6_get_pkt_hdr (const hio_dhcp6_pktinf_t* pkt, int level)
{
	const hio_dhcp6_pkt_hdr_t* phdr = pkt->hdr;
	hio_oow_t plen = pkt->len;
	int depth = 0;

	if (plen < HIO_SIZEOF(hio_dhcp6_pkt_hdr_t)) return HIO_NULL;

	while (depth < HIO_DHCP6_MAX_RELAY_LEVEL)
	{
		const hio_dhcp6_opt_hdr_t* opt;
		const hio_dhcp6_opt_hdr_t* rm;
		hio_oow_t rem;

		if (plen < HIO_SIZEOF(hio_dhcp6_relay_hdr_t) || !is_relay_msgtype(phdr->msgtype)) break;
		if (level >= 0 && depth >= level) break;
		if (option_area(phdr, plen, &opt, &rem) <= -1) break;

		rm = find_relay_msg(opt, rem);
		if (!rm) break;

		plen = relay_inner_len(rm);
		if (plen < HIO_SIZEOF(hio_dhcp6_pkt_hdr_t)) return HIO_NULL; /* the inner message is truncated */
		phdr = (const hio_dhcp6_pkt_hdr_t*)(rm + 1);
		depth++;
	}

	/* a level the packet does not nest to is an error, not the innermost
	 * message. returning the wrong header would have the caller read one
	 * message believing it was another. */
	if (level >= 0 && depth < level) return HIO_NULL;

	return (hio_dhcp6_pkt_hdr_t*)phdr;
}

/* ------------------------------------------------------------------------- */
/* finding options                                                           */
/* ------------------------------------------------------------------------- */

/* scan one option area for the option after 'pos'. with any_code set, any
 * option matches; otherwise only 'code' does. */
static hio_dhcp6_opt_hdr_t* scan_options (const hio_dhcp6_opt_hdr_t* opt, hio_oow_t rem, const find_t* a)
{
	while (rem >= HIO_SIZEOF(*opt))
	{
		/* the length must fit before the option is offered to the caller,
		 * which would otherwise read past the packet through it */
		if (rem < HIO_SIZEOF(*opt) + (hio_oow_t)hio_ntoh16(opt->len)) break;

		if ((!a->pos || opt > a->pos) &&
		    (a->any_code || hio_ntoh16(opt->code) == a->code)) return (hio_dhcp6_opt_hdr_t*)opt;

		opt = next_option(opt, &rem);
		if (!opt) break;
	}

	return HIO_NULL;
}

static hio_dhcp6_opt_hdr_t* dhcp6_find (const find_t* a, const hio_dhcp6_pkt_hdr_t* phdr, hio_oow_t plen, int level)
{
	const hio_dhcp6_opt_hdr_t* opt;
	hio_oow_t rem;

	if (level > HIO_DHCP6_MAX_RELAY_LEVEL) return HIO_NULL;

	if (option_area(phdr, plen, &opt, &rem) <= -1) return HIO_NULL;

	if (is_relay_msgtype(phdr->msgtype) && !(a->max_level >= 0 && level >= a->max_level))
	{
		/* not yet at the level asked for, so go in one more */
		const hio_dhcp6_opt_hdr_t* rm;

		rm = find_relay_msg(opt, rem);
		if (rm) return dhcp6_find(a, (const hio_dhcp6_pkt_hdr_t*)(rm + 1), relay_inner_len(rm), level + 1);

		/* a relay message with nothing relayed. a caller naming a definite
		 * level does not get this one instead; a caller asking for the
		 * innermost gets the deepest that does exist, which is this - the
		 * same answer hio_dhcp6_get_pkt_hdr() gives for such a packet, so a
		 * malformed relay's own options stay readable. */
		if (a->max_level >= 0) return HIO_NULL;
	}

	/* the nesting stopped short of the level asked for */
	if (a->max_level >= 0 && a->max_level != level) return HIO_NULL;

	return scan_options(opt, rem, a);
}

hio_dhcp6_opt_hdr_t* hio_dhcp6_find_option_after (const hio_dhcp6_pktinf_t* pkt, int level, const hio_dhcp6_opt_hdr_t* pos, int code)
{
	find_t a;

	HIO_MEMSET(&a, 0, HIO_SIZEOF(a));
	a.max_level = level;
	a.pos = pos;
	a.code = code;

	return dhcp6_find(&a, pkt->hdr, pkt->len, 0);
}

hio_dhcp6_opt_hdr_t* hio_dhcp6_find_option (const hio_dhcp6_pktinf_t* pkt, int level, int code)
{
	return hio_dhcp6_find_option_after(pkt, level, HIO_NULL, code);
}

hio_dhcp6_opt_hdr_t* hio_dhcp6_get_option_after (const hio_dhcp6_pktinf_t* pkt, int level, const hio_dhcp6_opt_hdr_t* pos)
{
	find_t a;

	HIO_MEMSET(&a, 0, HIO_SIZEOF(a));
	a.max_level = level;
	a.pos = pos;
	a.any_code = 1;

	return dhcp6_find(&a, pkt->hdr, pkt->len, 0);
}

/* ------------------------------------------------------------------------- */

int hio_dhcp6_init_pktbuf (hio_dhcp6_pktbuf_t* pkt, void* buf, hio_oow_t capa)
{
	if (capa < HIO_SIZEOF(*pkt->hdr)) return -1;
	pkt->hdr = (hio_dhcp6_pkt_hdr_t*)buf;
	pkt->len = HIO_SIZEOF(*pkt->hdr);
	pkt->capa = capa;
	HIO_MEMSET(pkt->hdr, 0, HIO_SIZEOF(*pkt->hdr));
	return 0;
}

/* ------------------------------------------------------------------------- */
/* editing options                                                           */
/* ------------------------------------------------------------------------- */

/* walk from the first option to 'pos', checking that it really is where an
 * option header starts. 'past_end_ok' allows the position just after the last
 * option, which is where an append lands. */
static int verify_pos (const hio_dhcp6_opt_hdr_t* opt, hio_oow_t rem, const hio_dhcp6_opt_hdr_t* pos, int past_end_ok)
{
	while (1)
	{
		/* rem drops to zero at the end of the area, where no option starts.
		 * that position is the append point, and only an append may name it. */
		if (opt == pos) return (rem <= 0 && !past_end_ok)? -1: 0;
		if ((const hio_uint8_t*)opt > (const hio_uint8_t*)pos) return -1; /* not at an option boundary */

		if (rem < HIO_SIZEOF(*opt)) return -1; /* pos lies past this level's options */

		opt = next_option(opt, &rem);
		if (!opt) return -1;
	}
}

/* grow or shrink the RELAY-MSG option that encloses a level, by 'delta'. */
static int check_relay_len (const hio_dhcp6_opt_hdr_t* rm, hio_ooi_t delta)
{
	hio_ooi_t n = (hio_ooi_t)hio_ntoh16(rm->len) + delta;
	return (n < 0 || n > HIO_DHCP6_MAX_OPT_DLEN)? -1: 0;
}

static int adjust_relay_len (hio_dhcp6_opt_hdr_t* rm, hio_ooi_t delta)
{
	hio_ooi_t n = (hio_ooi_t)hio_ntoh16(rm->len) + delta;

	if (n < 0 || n > HIO_DHCP6_MAX_OPT_DLEN) return -1;
	rm->len = hio_hton16((hio_uint16_t)n);
	return 0;
}

static hio_dhcp6_opt_hdr_t* insert_here (insert_t* a, const hio_dhcp6_opt_hdr_t* first, hio_oow_t rem)
{
	hio_dhcp6_opt_hdr_t opthdr;
	hio_dhcp6_opt_hdr_t* ipos;
	hio_oow_t move_len;
	hio_uint8_t* pkt_end;

	if (a->safe && a->pos && verify_pos(first, rem, a->pos, 1) <= -1) return HIO_NULL;

	ipos = a->pos;
	/* no position given means append, which is the end of this level's option
	 * area - not the end of the packet, which for a nested level is further on */
	if (!ipos) ipos = (hio_dhcp6_opt_hdr_t*)((const hio_uint8_t*)first + rem);

	pkt_end = (hio_uint8_t*)a->pb->hdr + a->pb->len;
	if ((hio_uint8_t*)ipos < (hio_uint8_t*)a->pb->hdr || (hio_uint8_t*)ipos > pkt_end) return HIO_NULL;

	opthdr.code = hio_hton16((hio_uint16_t)a->code);
	opthdr.len = hio_hton16((hio_uint16_t)a->dlen);

	/* everything from the insertion point to the end of the packet shifts up,
	 * whatever level it belongs to */
	move_len = (hio_oow_t)(pkt_end - (hio_uint8_t*)ipos);
	HIO_MEMMOVE((hio_uint8_t*)ipos + HIO_SIZEOF(opthdr) + a->dlen, ipos, move_len);
	HIO_MEMCPY(ipos, &opthdr, HIO_SIZEOF(opthdr));
	if (a->dlen > 0) HIO_MEMCPY((hio_uint8_t*)ipos + HIO_SIZEOF(opthdr), a->data, a->dlen);

	return ipos;
}

static hio_dhcp6_opt_hdr_t* dhcp6_insert (insert_t* a, hio_dhcp6_pkt_hdr_t* phdr, hio_oow_t plen, int level)
{
	const hio_dhcp6_opt_hdr_t* opt;
	hio_oow_t rem;

	if (level > HIO_DHCP6_MAX_RELAY_LEVEL) return HIO_NULL;
	if (option_area(phdr, plen, &opt, &rem) <= -1) return HIO_NULL;

	if (is_relay_msgtype(phdr->msgtype) && !(a->max_level >= 0 && level >= a->max_level))
	{
		hio_dhcp6_opt_hdr_t* rm;
		hio_dhcp6_opt_hdr_t* ipos;

		rm = (hio_dhcp6_opt_hdr_t*)find_relay_msg(opt, rem);
		if (!rm) goto here; /* see dhcp6_find() */

		/* the enclosing option must be able to describe the result before
		 * anything is moved, or the insert would succeed and leave a length
		 * that cannot express it */
		if (check_relay_len(rm, (hio_ooi_t)(HIO_SIZEOF(*rm) + a->dlen)) <= -1) return HIO_NULL;

		ipos = dhcp6_insert(a, (hio_dhcp6_pkt_hdr_t*)(rm + 1), relay_inner_len(rm), level + 1);
		if (!ipos) return HIO_NULL;

		if (adjust_relay_len(rm, (hio_ooi_t)(HIO_SIZEOF(*rm) + a->dlen)) <= -1) return HIO_NULL;
		return ipos;
	}

here:
	if (a->max_level >= 0 && a->max_level != level) return HIO_NULL;

	return insert_here(a, opt, rem);
}

hio_dhcp6_opt_hdr_t* hio_dhcp6_insert_option_at (
	hio_dhcp6_pktbuf_t* pkt, int level, hio_dhcp6_opt_hdr_t* pos, int safe,
	int code, const void* data, hio_oow_t dlen)
{
	insert_t a;
	hio_dhcp6_opt_hdr_t* x;
	hio_oow_t new_len;

	if (dlen > HIO_DHCP6_MAX_OPT_DLEN) return HIO_NULL;

	new_len = pkt->len + HIO_SIZEOF(hio_dhcp6_opt_hdr_t) + dlen;
	if (new_len > pkt->capa) return HIO_NULL;

	HIO_MEMSET(&a, 0, HIO_SIZEOF(a));
	a.pb = pkt;
	a.max_level = level;
	a.pos = pos;
	a.code = code;
	a.data = data;
	a.dlen = dlen;
	a.safe = safe;

	x = dhcp6_insert(&a, pkt->hdr, pkt->len, 0);
	if (x) pkt->len = new_len;
	return x;
}

/* ------------------------------------------------------------------------- */

static int delete_here (delete_t* a, const hio_dhcp6_opt_hdr_t* first, hio_oow_t rem)
{
	hio_uint8_t* pos_end;
	hio_oow_t move_len;

	/* the position must name an option that exists at this level, not merely a
	 * boundary - there is nothing to delete just past the last one */
	if (a->safe && verify_pos(first, rem, a->pos, 0) <= -1) return -1;

	/* bounded by the caller before anything was touched */
	pos_end = (hio_uint8_t*)a->pos + a->dlen;

	move_len = (hio_oow_t)(((hio_uint8_t*)a->pb->hdr + a->pb->len) - pos_end);
	HIO_MEMMOVE(a->pos, pos_end, move_len);
	return 0;
}

static int dhcp6_delete (delete_t* a, hio_dhcp6_pkt_hdr_t* phdr, hio_oow_t plen, int level)
{
	const hio_dhcp6_opt_hdr_t* opt;
	hio_oow_t rem;

	if (level > HIO_DHCP6_MAX_RELAY_LEVEL) return -1;
	if (option_area(phdr, plen, &opt, &rem) <= -1) return -1;

	if (is_relay_msgtype(phdr->msgtype) && !(a->max_level >= 0 && level >= a->max_level))
	{
		hio_dhcp6_opt_hdr_t* rm;

		rm = (hio_dhcp6_opt_hdr_t*)find_relay_msg(opt, rem);
		if (!rm) goto here; /* see dhcp6_find() */

		if (check_relay_len(rm, -(hio_ooi_t)a->dlen) <= -1) return -1;

		if (dhcp6_delete(a, (hio_dhcp6_pkt_hdr_t*)(rm + 1), relay_inner_len(rm), level + 1) <= -1) return -1;

		adjust_relay_len (rm, -(hio_ooi_t)a->dlen);
		return 0;
	}

here:
	if (a->max_level >= 0 && a->max_level != level) return -1;

	return delete_here(a, opt, rem);
}

int hio_dhcp6_delete_option_at (hio_dhcp6_pktbuf_t* pkt, int level, hio_dhcp6_opt_hdr_t* pos, int safe)
{
	delete_t a;
	hio_uint8_t* pkt_end;

	if (!pos) return -1;

	pkt_end = (hio_uint8_t*)pkt->hdr + pkt->len;
	if ((hio_uint8_t*)pos < (hio_uint8_t*)(pkt->hdr + 1) ||
	    (hio_uint8_t*)pos + HIO_SIZEOF(*pos) > pkt_end) return -1;

	HIO_MEMSET(&a, 0, HIO_SIZEOF(a));
	a.pb = pkt;
	a.max_level = level;
	a.pos = pos;
	a.safe = safe;
	a.dlen = HIO_SIZEOF(*pos) + hio_ntoh16(pos->len);

	/* the option must fit inside the packet whether or not safe checking is
	 * asked for - the move length below depends on it */
	if ((hio_uint8_t*)pos + a.dlen > pkt_end) return -1;

	if (dhcp6_delete(&a, pkt->hdr, pkt->len, 0) <= -1) return -1;

	pkt->len -= a.dlen;
	return 0;
}

/* ------------------------------------------------------------------------- */

static hio_dhcp6_opt_hdr_t* replace_here (replace_t* a, const hio_dhcp6_opt_hdr_t* first, hio_oow_t rem)
{
	hio_dhcp6_opt_hdr_t opthdr;
	hio_uint8_t* pos_end;

	if (a->safe && verify_pos(first, rem, a->pos, 0) <= -1) return HIO_NULL;

	pos_end = (hio_uint8_t*)a->pos + a->old_len; /* bounded by the caller */

	if (a->rlen != 0)
	{
		/* the tail shifts either way by the difference in the two lengths */
		hio_oow_t move_len = (hio_oow_t)(((hio_uint8_t*)a->pb->hdr + a->pb->len) - pos_end);
		HIO_MEMMOVE(pos_end + a->rlen, pos_end, move_len);
	}

	opthdr.code = hio_hton16((hio_uint16_t)a->code);
	opthdr.len = hio_hton16((hio_uint16_t)a->dlen);
	HIO_MEMCPY(a->pos, &opthdr, HIO_SIZEOF(opthdr));
	if (a->dlen > 0) HIO_MEMCPY((hio_uint8_t*)a->pos + HIO_SIZEOF(opthdr), a->data, a->dlen);

	return a->pos;
}

static hio_dhcp6_opt_hdr_t* dhcp6_replace (replace_t* a, hio_dhcp6_pkt_hdr_t* phdr, hio_oow_t plen, int level)
{
	const hio_dhcp6_opt_hdr_t* opt;
	hio_oow_t rem;

	if (level > HIO_DHCP6_MAX_RELAY_LEVEL) return HIO_NULL;
	if (option_area(phdr, plen, &opt, &rem) <= -1) return HIO_NULL;

	if (is_relay_msgtype(phdr->msgtype) && !(a->max_level >= 0 && level >= a->max_level))
	{
		hio_dhcp6_opt_hdr_t* rm;
		hio_dhcp6_opt_hdr_t* x;

		rm = (hio_dhcp6_opt_hdr_t*)find_relay_msg(opt, rem);
		if (!rm) goto here; /* see dhcp6_find() */

		if (check_relay_len(rm, a->rlen) <= -1) return HIO_NULL;

		x = dhcp6_replace(a, (hio_dhcp6_pkt_hdr_t*)(rm + 1), relay_inner_len(rm), level + 1);
		if (!x) return HIO_NULL;

		adjust_relay_len (rm, a->rlen);
		return x;
	}

here:
	if (a->max_level >= 0 && a->max_level != level) return HIO_NULL;

	return replace_here(a, opt, rem);
}

hio_dhcp6_opt_hdr_t* hio_dhcp6_replace_option_at (
	hio_dhcp6_pktbuf_t* pkt, int level, hio_dhcp6_opt_hdr_t* pos, int safe,
	int code, const void* data, hio_oow_t dlen)
{
	replace_t a;
	hio_dhcp6_opt_hdr_t* x;
	hio_uint8_t* pkt_end;

	if (!pos) return HIO_NULL;
	if (dlen > HIO_DHCP6_MAX_OPT_DLEN) return HIO_NULL;

	pkt_end = (hio_uint8_t*)pkt->hdr + pkt->len;
	if ((hio_uint8_t*)pos < (hio_uint8_t*)(pkt->hdr + 1) ||
	    (hio_uint8_t*)pos + HIO_SIZEOF(*pos) > pkt_end) return HIO_NULL;

	HIO_MEMSET(&a, 0, HIO_SIZEOF(a));
	a.pb = pkt;
	a.max_level = level;
	a.pos = pos;
	a.safe = safe;
	a.code = code;
	a.data = data;
	a.dlen = dlen;
	a.old_len = HIO_SIZEOF(*pos) + hio_ntoh16(pos->len);
	a.rlen = (hio_ooi_t)(HIO_SIZEOF(*pos) + dlen) - (hio_ooi_t)a.old_len;

	if ((hio_uint8_t*)pos + a.old_len > pkt_end) return HIO_NULL;
	if ((hio_ooi_t)pkt->len + a.rlen > (hio_ooi_t)pkt->capa) return HIO_NULL;

	x = dhcp6_replace(&a, pkt->hdr, pkt->len, 0);
	if (x) pkt->len = (hio_oow_t)((hio_ooi_t)pkt->len + a.rlen);
	return x;
}
