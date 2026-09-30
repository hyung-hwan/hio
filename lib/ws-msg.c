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


#include <hio-ws.h>
#include "hio-prv.h"

/*
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-------+-+-------------+-------------------------------+
|F|R|R|R| opcode|M| Payload len |    Extended payload length    |
|I|S|S|S|  (4)  |A|     (7)     |             (16/64)           |
|N|V|V|V|       |S|             |   (if payload len==126/127)   |
| |1|2|3|       |K|             |                               |
+-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
|     Extended payload length continued, if payload len == 127  |
+-------------------------------+-------------------------------+
...
|                     Masking-key, if MASK set to 1             |
+---------------------------------------------------------------+
...
|                         Payload Data                          |
+---------------------------------------------------------------+

- Payload Length (7 bits, plus optional extensions):
- If 0–125, that is the payload length.
- If 126, the next 16 bits contain the extended length.
- If 127, the next 64 bits contain the extended length (RFC6455 - the most significant bit MUST be 0).
-
- In principal, masking is mandatory for client-to-server packets

- Some sample sequence of frames:
  - Short text message frame
    - OPCODE 0x1 FIN 1
  - Three frame long text message
    - OPCODE 0x1 FIN 0
    - OPCODE 0x0 FIN 0
    - OPCODE 0x0 FIN 1

- A close frame is sent to signal connection close.
- The close frame receiver must send a close frame to the sender.
  - the status code must be copied if it's in the incoming close frame
  - don't copy the payload.
- In case one side is not responsive, some timeout based solution must be implemented
  proper clean up.
 */

int hio_ws_parse_frame_hdr (const void* ptr, hio_oow_t len, hio_ws_frame_hdr_t* hdr)
{
	const hio_uint8_t* p = (const hio_uint8_t*)ptr;
	hio_oow_t hlen, plen;
	int lenform;

	/* most of the ways a frame can be wrong are simply protocol errors. the
	 * one that is not overwrites this. */
	hdr->status = HIO_WS_STATUS_PROTOCOL_ERROR;

	if (len < 2) return 0;

	hdr->fin = (p[0] >> 7) & 0x01;
	hdr->rsv = (p[0] >> 4) & 0x07;
	hdr->opcode = p[0] & 0x0F;
	/*hdr->masked = (p[1] & 0x80) >> 7;*/
	hdr->masked = (p[1] >> 7) & 0x01;
	lenform = p[1] & 0x7F;

	/* an opcode the protocol has not defined cannot be skipped over, because
	 * nothing says what the frame carrying it means */
	switch (hdr->opcode)
	{
		case HIO_WS_OPCODE_CONT:
		case HIO_WS_OPCODE_TEXT:
		case HIO_WS_OPCODE_BIN:
		case HIO_WS_OPCODE_CLOSE:
		case HIO_WS_OPCODE_PING:
		case HIO_WS_OPCODE_PONG:
			break;
		default:
			return -1;
	}

	if (lenform < 126)
	{
		plen = (hio_oow_t)lenform;
		hlen = 2;
	}
	else if (lenform == 126)
	{
		if (len < 4) return 0;
		plen = ((hio_oow_t)p[2] << 8) | (hio_oow_t)p[3];
		hlen = 4;

		/* the shortest form that fits must be used, so a length this small
		 * arriving in the long form is not the same frame written another
		 * way - it is a frame no conforming peer produces */
		if (plen < 126) return -1;
	}
	else
	{
		hio_uint32_t hi, lo;

		if (len < 10) return 0;
		hi = ((hio_uint32_t)p[2] << 24) | ((hio_uint32_t)p[3] << 16) | ((hio_uint32_t)p[4] << 8) | (hio_uint32_t)p[5];
		lo = ((hio_uint32_t)p[6] << 24) | ((hio_uint32_t)p[7] << 16) | ((hio_uint32_t)p[8] << 8) | (hio_uint32_t)p[9];
		hlen = 10;

		/* the length is a 63-bit quantity - the top bit is required to be
		 * clear, so a frame claiming to set it is malformed rather than huge */
		if (hi & (hio_uint32_t)0x80000000u) return -1;

	#if (HIO_SIZEOF_OOW_T >= 8)
		plen = ((hio_oow_t)hi << 32) | (hio_oow_t)lo;
	#else
		/* the length is expressible on the wire but not in a length this
		 * machine can address, which is a different complaint from a
		 * malformed frame and gets a different close code */
		if (hi != 0)
		{
			hdr->status = HIO_WS_STATUS_MESSAGE_TOO_BIG;
			return -1;
		}
		plen = (hio_oow_t)lo;
	#endif

		if (plen <= 0xFFFF) return -1; /* the shorter form would have held it */
	}

	if (hdr->masked)
	{
		if (len < hlen + 4) return 0;
		HIO_MEMCPY(hdr->mask, &p[hlen], 4);
		hlen += 4;
	}
	else
	{
		HIO_MEMSET(hdr->mask, 0, HIO_SIZEOF(hdr->mask));
	}

	/* a control frame may arrive between the fragments of a message, so it
	 * has to be readable on its own: it cannot be fragmented, and it has to
	 * be small enough that waiting for the rest of it cannot stall anything */
	if (HIO_WS_OPCODE_IS_CONTROL(hdr->opcode))
	{
		if (!hdr->fin || plen > HIO_WS_CONTROL_MAX_PLEN) return -1;
	}

	hdr->plen = plen;
	hdr->hlen = hlen;
	hdr->status = 0;
	return 1;
}

int hio_ws_build_frame_hdr (void* ptr, hio_oow_t capa, int fin, int opcode, const hio_uint8_t mask[4], hio_oow_t plen)
{
	hio_uint8_t* p = (hio_uint8_t*)ptr;
	hio_oow_t hlen;
	hio_oow_t i;
	int lenform;

	if (plen < 126)
	{
		lenform = (int)plen;
		hlen = 2;
	}
	else if (plen <= 0xFFFF)
	{
		lenform = 126;
		hlen = 4;
	}
	else
	{
		lenform = 127;
		hlen = 10;
	}
	if (mask) hlen += 4;

	if (capa < hlen) return -1;

	p[0] = (hio_uint8_t)((fin? 0x80: 0x00) | (opcode & 0x0F));
	p[1] = (hio_uint8_t)((mask? 0x80: 0x00) | lenform);

	if (lenform == 126)
	{
		p[2] = (hio_uint8_t)(plen >> 8);
		p[3] = (hio_uint8_t)plen;
		i = 4;
	}
	else if (lenform == 127)
	{
		/* eight octets, most significant first. the top four are zero on a
		 * machine whose lengths are four octets wide, which is the only way
		 * such a length could have been arrived at there. */
	#if (HIO_SIZEOF_OOW_T >= 8)
		p[2] = (hio_uint8_t)(plen >> 56);
		p[3] = (hio_uint8_t)(plen >> 48);
		p[4] = (hio_uint8_t)(plen >> 40);
		p[5] = (hio_uint8_t)(plen >> 32);
	#else
		p[2] = 0; p[3] = 0; p[4] = 0; p[5] = 0;
	#endif
		p[6] = (hio_uint8_t)(plen >> 24);
		p[7] = (hio_uint8_t)(plen >> 16);
		p[8] = (hio_uint8_t)(plen >> 8);
		p[9] = (hio_uint8_t)plen;
		i = 10;
	}
	else
	{
		i = 2;
	}

	if (mask)
	{
		HIO_MEMCPY(&p[i], mask, 4);
		i += 4;
	}

	return (int)hlen;
}

hio_oow_t hio_ws_mask (void* ptr, hio_oow_t len, const hio_uint8_t mask[4], hio_oow_t offset)
{
	hio_uint8_t* p = (hio_uint8_t*)ptr;
	hio_oow_t i;

	/* the key repeats from the start of the payload, so where this piece of
	 * it begins is what decides which octet of the key each one meets */
	for (i = 0; i < len; i++) p[i] ^= mask[(offset + i) & 3];

	return offset + len;
}

int hio_ws_status_is_sendable (int status)
{
	switch (status)
	{
		/* the codes that stand for the absence of a code, or for something
		 * that happened below this protocol, describe a local observation.
		 * there is no one to send them to who did not already know. */
		case HIO_WS_STATUS_NO_STATUS:
		case HIO_WS_STATUS_ABNORMAL:
		case HIO_WS_STATUS_TLS_FAILURE:
			return 0;
	}

	/* 1004 was set aside and never defined, and everything from 1016 to 2999
	 * is reserved for codes this protocol has not assigned */
	if (status >= 1000 && status <= 1014 && status != 1004) return 1;

	/* registered by others, and reserved for private use */
	if (status >= 3000 && status <= 4999) return 1;

	return 0;
}

int hio_ws_parse_close_payload (const void* ptr, hio_oow_t len, int* code, const hio_bch_t** reason, hio_oow_t* reason_len)
{
	const hio_uint8_t* p = (const hio_uint8_t*)ptr;
	int c;

	if (len == 0)
	{
		/* closing without a code is allowed and says nothing more than that
		 * the peer is done */
		if (code) *code = HIO_WS_STATUS_NO_STATUS;
		if (reason) *reason = HIO_NULL;
		if (reason_len) *reason_len = 0;
		return 0;
	}

	/* the code is two octets or it is absent. one octet is neither. */
	if (len < 2) return -1;

	c = ((int)p[0] << 8) | (int)p[1];
	if (!hio_ws_status_is_sendable(c)) return -1;

	if (code) *code = c;
	if (reason) *reason = (const hio_bch_t*)&p[2];
	if (reason_len) *reason_len = len - 2;
	return 0;
}

int hio_ws_build_close_payload (void* ptr, hio_oow_t capa, int code, const hio_bch_t* reason)
{
	hio_uint8_t* p = (hio_uint8_t*)ptr;
	hio_oow_t rlen = 0;

	if (!hio_ws_status_is_sendable(code)) return -1;
	if (reason) rlen = hio_count_bcstr(reason);

	/* the whole body travels in a control frame, so it is bounded by what one
	 * of those may carry - two octets of it are the code */
	if (rlen > HIO_WS_CONTROL_MAX_PLEN - 2) return -1;
	if (capa < rlen + 2) return -1;

	p[0] = (hio_uint8_t)(code >> 8);
	p[1] = (hio_uint8_t)code;
	if (rlen > 0) HIO_MEMCPY(&p[2], reason, rlen);

	return (int)(rlen + 2);
}
