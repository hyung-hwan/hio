/*
 * $Id$
 *
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

#ifndef _MIO_JSON_H_
#define _MIO_JSON_H_

#include <mio.h>

/** 
 * The mio_json_t type defines a simple json parser.
 */
typedef struct mio_json_t mio_json_t;

/* ========================================================================= */

enum mio_json_state_t
{
	MIO_JSON_STATE_START,
	MIO_JSON_STATE_IN_ARRAY,
	MIO_JSON_STATE_IN_DIC,

	MIO_JSON_STATE_IN_WORD_VALUE,
	MIO_JSON_STATE_IN_NUMERIC_VALUE,
	MIO_JSON_STATE_IN_STRING_VALUE,
	MIO_JSON_STATE_IN_CHARACTER_VALUE
};
typedef enum mio_json_state_t mio_json_state_t;

/* ========================================================================= */
enum mio_json_inst_t
{
	MIO_JSON_INST_START_ARRAY,
	MIO_JSON_INST_END_ARRAY,
	MIO_JSON_INST_START_DIC,
	MIO_JSON_INST_END_DIC,

	MIO_JSON_INST_KEY,

	MIO_JSON_INST_CHARACTER, /* there is no such element as character in real JSON */
	MIO_JSON_INST_STRING,
	MIO_JSON_INST_NUMBER,
	MIO_JSON_INST_NIL,
	MIO_JSON_INST_TRUE,
	MIO_JSON_INST_FALSE,
};
typedef enum mio_json_inst_t mio_json_inst_t;

typedef int (*mio_json_instcb_t) (
	mio_json_t*           json,
	mio_json_inst_t       inst,
	const mio_oocs_t*     str
);


typedef struct mio_json_state_node_t mio_json_state_node_t;
struct mio_json_state_node_t
{
	mio_json_state_t state;
	union
	{
		struct
		{
			int got_value;
		} ia; /* in array */

		struct
		{
			/* 0: ready to get key (at the beginning or got comma), 
			 * 1: got key, 2: got colon, 3: got value */
			int state; 
		} id; /* in dictionary */
		struct
		{
			int escaped;
			int digit_count;
			/* acc is always of unicode type to handle \u and \U. 
			 * in the bch mode, it will get converted to a utf8 stream. */
			mio_uch_t acc;
		} sv;
		struct
		{
			int escaped;
			int digit_count;
			/* for a character, no way to support the unicode character
			 * in the bch mode */
			mio_ooch_t acc; 
		} cv;
		struct
		{
			int dotted;
		} nv;
	} u;
	mio_json_state_node_t* next;
};

struct mio_json_t
{
	mio_t* mio;
	mio_json_instcb_t instcb;

	mio_json_state_node_t state_top;
	mio_json_state_node_t* state_stack;
	mio_oocs_t tok;
	mio_oow_t tok_capa;
};

/* ========================================================================= */

#if defined(__cplusplus)
extern "C" {
#endif

MIO_EXPORT mio_json_t* mio_json_open (
	mio_t*             mio,
	mio_oow_t          xtnsize
);

MIO_EXPORT void mio_json_close (
	mio_json_t* json
);

MIO_EXPORT int mio_json_init (
	mio_json_t* json,
	mio_t*      mio
);

MIO_EXPORT void mio_json_fini (
	mio_json_t* json
);

MIO_EXPORT void mio_json_reset (
	mio_json_t* json
);

MIO_EXPORT int mio_json_feed (
	mio_json_t*   json,
	const void*   ptr,
	mio_oow_t     len,
	mio_oow_t*    xlen
);

MIO_EXPORT mio_json_state_t mio_json_getstate (
	mio_json_t* json
);


#if defined(MIO_HAVE_INLINE)
static MIO_INLINE void* mio_json_getxtn (mio_json_t* json) { return (void*)(json + 1); }
#else
#define mio_json_getxtn(json) ((void*)((mio_json_t*)(json) + 1))
#endif

#if defined(__cplusplus)
}
#endif

#endif
