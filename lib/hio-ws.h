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


#ifndef _HIO_WS_H_
#define _HIO_WS_H_

#include <hio.h>

/* the websocket framing of rfc 6455 section 5.
 *
 * this is the wire format on its own - octets in, octets out, no sockets and
 * no connection state. what it knows is what a frame looks like and which
 * shapes the protocol forbids outright; what it does not know is anything
 * that depends on the conversation so far, which belongs with the task that
 * holds the connection.
 */

/** a frame header is two octets, plus up to eight for an extended length and
 *  four for a masking key */
#define HIO_WS_FRAME_HDR_MAX_LEN (14)

/** a control frame's payload may not exceed this, per RFC 6455 section 5.5 */
#define HIO_WS_CONTROL_MAX_PLEN (125)

enum hio_ws_opcode_t
{
	HIO_WS_OPCODE_CONT  = 0x0, /**< a continuation of the previous frame */
	HIO_WS_OPCODE_TEXT  = 0x1,
	HIO_WS_OPCODE_BIN   = 0x2,
	HIO_WS_OPCODE_CLOSE = 0x8,
	HIO_WS_OPCODE_PING  = 0x9,
	HIO_WS_OPCODE_PONG  = 0xA
};
typedef enum hio_ws_opcode_t hio_ws_opcode_t;

/** control frames are the ones with the high bit of the opcode set. they may
 *  be injected between the fragments of a message, so they are never a
 *  continuation of anything and must fit in a single frame. */
#define HIO_WS_OPCODE_IS_CONTROL(opcode) (((opcode) & 0x8) != 0)

/** close status codes - RFC 6455 section 7.4.1 */
enum hio_ws_status_t
{
	HIO_WS_STATUS_NORMAL             = 1000,
	HIO_WS_STATUS_GOING_AWAY         = 1001,
	HIO_WS_STATUS_PROTOCOL_ERROR     = 1002,
	HIO_WS_STATUS_UNACCEPTABLE_DATA  = 1003,
	HIO_WS_STATUS_NO_STATUS          = 1005, /**< never sent; means the peer gave no code */
	HIO_WS_STATUS_ABNORMAL           = 1006, /**< never sent; means the connection just broke */
	HIO_WS_STATUS_BAD_PAYLOAD        = 1007,
	HIO_WS_STATUS_POLICY_VIOLATION   = 1008,
	HIO_WS_STATUS_MESSAGE_TOO_BIG    = 1009,
	HIO_WS_STATUS_EXTENSION_REQUIRED = 1010,
	HIO_WS_STATUS_INTERNAL_ERROR     = 1011,
	HIO_WS_STATUS_TLS_FAILURE        = 1015  /**< never sent */
};
typedef enum hio_ws_status_t hio_ws_status_t;

/**
 * The hio_ws_frame_hdr_t type defines the parsed websocket header.
 */
struct hio_ws_frame_hdr_t
{
	int         fin;      /**< set if this frame ends the message */

	/**
	 * the three reserved bits as a value from 0 to 7. they mean whatever an
	 * extension says they mean, so this reports them rather than judging
	 * them: a peer that has negotiated no extension must treat anything but
	 * zero as a protocol error, and only the caller knows what was
	 * negotiated.
	 */
	int         rsv;

	int         opcode;   /**< #hio_ws_opcode_t */
	int         masked;
	hio_uint8_t mask[4];  /**< meaningful only if \b masked */

	hio_oow_t   plen;     /**< payload length, which follows the header */
	hio_oow_t   hlen;     /**< octets this header occupied */

	/**
	 * when hio_ws_parse_frame_hdr() rejects a frame, the close code that says
	 * why - #HIO_WS_STATUS_PROTOCOL_ERROR for a malformed frame, or
	 * #HIO_WS_STATUS_MESSAGE_TOO_BIG for a length this machine cannot address.
	 */
	int         status;
};
typedef struct hio_ws_frame_hdr_t hio_ws_frame_hdr_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The hio_ws_parse_frame_hdr() function reads the frame header at \a ptr.
 *
 * A stream hands over octets in whatever sizes it likes, so an incomplete
 * header is an ordinary outcome rather than an error - the caller keeps the
 * octets and asks again when more have arrived.
 *
 * The payload itself is not touched. \b hlen says where it starts and \b plen
 * how long it is; a masked payload still has to go through hio_ws_mask().
 *
 * \return 1 if a whole header was read, 0 if more octets are needed, or -1 if
 *         the frame cannot be valid, with \b status set to the close code
 *         that describes it.
 */
HIO_EXPORT int hio_ws_parse_frame_hdr (
	const void*          ptr,
	hio_oow_t            len,
	hio_ws_frame_hdr_t*  hdr
);

/**
 * The hio_ws_build_frame_hdr() function writes the header for a frame of
 * \a plen octets, using the shortest length form that fits - a peer is
 * required to reject any other.
 *
 * Pass #HIO_NULL for \a mask to leave the payload unmasked, which is what a
 * server does; a client must mask every frame it sends. The reserved bits are
 * written as zero, which is what they must be without a negotiated extension.
 *
 * \return the number of octets written, or -1 if \a capa was too small. At
 *         most #HIO_WS_FRAME_HDR_MAX_LEN octets are ever needed.
 */
HIO_EXPORT int hio_ws_build_frame_hdr (
	void*              ptr,
	hio_oow_t          capa,
	int                fin,
	int                opcode,
	const hio_uint8_t  mask[4],
	hio_oow_t          plen
);

/**
 * The hio_ws_mask() function applies a masking key to \a len octets at \a ptr,
 * in place. Masking and unmasking are the same operation.
 *
 * The key repeats every four octets counted from the start of the payload, not
 * from the start of the buffer, so \a offset carries that position across a
 * payload that arrives in pieces. Pass 0 for the first piece and the returned
 * value for each one after it.
 *
 * \return the payload offset just past what was masked.
 */
HIO_EXPORT hio_oow_t hio_ws_mask (
	void*              ptr,
	hio_oow_t          len,
	const hio_uint8_t  mask[4],
	hio_oow_t          offset
);

/**
 * The hio_ws_status_is_sendable() function says whether \a status is a close
 * code that may appear on the wire. The codes that stand for the absence of
 * one - #HIO_WS_STATUS_NO_STATUS, #HIO_WS_STATUS_ABNORMAL and
 * #HIO_WS_STATUS_TLS_FAILURE - may not, nor may anything outside the ranges
 * RFC 6455 section 7.4 sets aside.
 */
HIO_EXPORT int hio_ws_status_is_sendable (
	int status
);

/**
 * The hio_ws_parse_close_payload() function reads the body of a close frame,
 * which is a status code and an optional reason, or nothing at all.
 *
 * An empty body is not an error: it says the peer closed without giving a
 * reason, and \a code is set to #HIO_WS_STATUS_NO_STATUS to report that. The
 * reason is not copied - \a reason points into \a ptr - and it is not checked
 * for valid UTF-8.
 *
 * \return 0 on success, or -1 if the body is one octet long or carries a
 *         status code that may not be sent.
 */
HIO_EXPORT int hio_ws_parse_close_payload (
	const void*        ptr,
	hio_oow_t          len,
	int*               code,
	const hio_bch_t**  reason,
	hio_oow_t*         reason_len
);

/**
 * The hio_ws_build_close_payload() function writes the body of a close frame.
 * Pass #HIO_NULL or an empty string for \a reason to send the code alone.
 *
 * \return the number of octets written, or -1 if \a capa was too small or
 *         \a code may not be sent.
 */
HIO_EXPORT int hio_ws_build_close_payload (
	void*              ptr,
	hio_oow_t          capa,
	int                code,
	const hio_bch_t*   reason
);

#ifdef __cplusplus
}
#endif

#endif
