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

#ifndef _HIO_JSON_H_
#define _HIO_JSON_H_

#include <hio.h>

/** 
 * The hio_json_t type defines a simple json parser.
 */
typedef struct hio_json_t hio_json_t;

/* ========================================================================= */

enum hio_json_state_t
{
	HIO_JSON_STATE_START,
	HIO_JSON_STATE_IN_ARRAY,
	HIO_JSON_STATE_IN_OBJECT,

	HIO_JSON_STATE_IN_WORD_VALUE,
	HIO_JSON_STATE_IN_NUMERIC_VALUE,
	HIO_JSON_STATE_IN_STRING_VALUE
};
typedef enum hio_json_state_t hio_json_state_t;

/* ========================================================================= */
enum hio_json_inst_t
{
	HIO_JSON_INST_START_ARRAY,
	HIO_JSON_INST_END_ARRAY,
	HIO_JSON_INST_START_OBJECT,
	HIO_JSON_INST_END_OBJECT,

	HIO_JSON_INST_KEY,

	HIO_JSON_INST_STRING,
	HIO_JSON_INST_NUMBER,
	HIO_JSON_INST_NIL,
	HIO_JSON_INST_TRUE,
	HIO_JSON_INST_FALSE,
};
typedef enum hio_json_inst_t hio_json_inst_t;

typedef int (*hio_json_instcb_t) (
	hio_json_t*           json,
	hio_json_inst_t       inst,
	hio_oow_t             level,
	hio_oow_t             index,
	hio_json_state_t      container_state,
	const hio_oocs_t*     str,
	void*                 ctx
);


typedef struct hio_json_state_node_t hio_json_state_node_t;
struct hio_json_state_node_t
{
	hio_json_state_t state;
	hio_oow_t level;
	hio_oow_t index;
	int in_comment;
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
		} io; /* in object */
		struct
		{
			int escaped;
			int digit_count;
			/* acc is always of unicode type to handle \u and \U. 
			 * in the bch mode, it will get converted to a utf8 stream. */
			hio_uch_t acc;
		} sv;
		struct
		{
			int escaped;
			int digit_count;
			/* for a character, no way to support the unicode character
			 * in the bch mode */
			hio_ooch_t acc; 
		} cv;
		struct
		{
			int progress;
		} nv;
	} u;
	hio_json_state_node_t* next;
};

enum hio_json_option_t
{
	/* allow an unquoted word as an object key */
	HIO_JSON_PERMIT_WORD_KEY  = ((hio_bitmask_t)1 << 0), 

	/* a comma as a separator is not mandatory */
	HIO_JSON_OPTIONAL_COMMA   = ((hio_bitmask_t)1 << 1),

	/* support the line comment. the text beginning with # is a comment to the end of the line */
	HIO_JSON_LINE_COMMENT   = ((hio_bitmask_t)1 << 2)
};

typedef enum hio_json_option_t hio_json_option_t;

struct hio_json_t
{
	hio_t* hio;
	hio_json_instcb_t instcb;
	void* rctx;
	hio_bitmask_t option;

	hio_json_state_node_t state_top;
	hio_json_state_node_t* state_stack;

	hio_oocs_t tok;
	hio_oow_t tok_capa;
	hio_oow_t tok_line;
	hio_oow_t tok_col;

	hio_oow_t c_line;
	hio_oow_t c_col;
};

/* ========================================================================= */

typedef struct hio_jsonwr_t hio_jsonwr_t;

typedef int (*hio_jsonwr_writecb_t) (
	hio_jsonwr_t*         jsonwr,
	const hio_bch_t*      dptr,
	hio_oow_t             dlen,
	void*                 ctx
);

typedef struct hio_jsonwr_state_node_t hio_jsonwr_state_node_t;
struct hio_jsonwr_state_node_t
{
	hio_json_state_t state;
	hio_oow_t level;
	hio_oow_t index;
	int obj_awaiting_val;
	hio_jsonwr_state_node_t* next;
};

enum hio_jsonwr_flag_t
{
	HIO_JSONWR_FLAG_PRETTY = (1 << 0)
};
typedef enum hio_jsonwr_flag_t hio_jsonwr_flag_t;

struct hio_jsonwr_t
{
	hio_t* hio;
	hio_jsonwr_writecb_t writecb;
	hio_jsonwr_state_node_t state_top;
	hio_jsonwr_state_node_t* state_stack;
	int flags;

	void* wctx;
	hio_bch_t wbuf[8192];
	hio_oow_t wbuf_len;
};

/* ========================================================================= */

#if defined(__cplusplus)
extern "C" {
#endif

HIO_EXPORT hio_json_t* hio_json_open (
	hio_t*             hio,
	hio_oow_t          xtnsize
);

HIO_EXPORT void hio_json_close (
	hio_json_t* json
);

HIO_EXPORT int hio_json_init (
	hio_json_t* json,
	hio_t*      hio
);

HIO_EXPORT void hio_json_fini (
	hio_json_t* json
);

#if defined(HIO_HAVE_INLINE)
static HIO_INLINE hio_t* hio_json_gethio (hio_json_t* json) { return json->hio; }
#else
#	define hio_json_gethio(json) (((hio_json_t*)(json))->hio)
#endif

#if defined(HIO_HAVE_INLINE)
static HIO_INLINE void* hio_json_getxtn (hio_json_t* json) { return (void*)(json + 1); }
#else
#define hio_json_getxtn(json) ((void*)((hio_json_t*)(json) + 1))
#endif

HIO_EXPORT hio_bitmask_t hio_json_getoption (
	hio_json_t*       json
);

HIO_EXPORT void hio_json_setoption (
	hio_json_t*       json,
	hio_bitmask_t     mask
);

HIO_EXPORT void hio_json_setinstcb (
	hio_json_t*       json,
	hio_json_instcb_t instcb,
	void*             ctx
);

HIO_EXPORT hio_json_state_t hio_json_getstate (
	hio_json_t*   json
);

HIO_EXPORT void hio_json_resetstates (
	hio_json_t*   json
);

HIO_EXPORT void hio_json_resetfeedloc (
	hio_json_t*   json
);

/**
 * The hio_json_feed() function processes the raw data.
 *
 * If stop_if_ever_complted is 0, it returns 0 on success. If the value pointed to by
 * rem is greater 0 after the call, processing is not complete and more feeding is 
 * required. Incomplete feeding may be caused by incomplete byte sequences or incomplete
 * json object.
 *
 * If stop_if_ever_completed is non-zero, it returns 0 upon incomplet byte sequence or
 * incomplete json object. It returns 1 if it sees the first complete json object. It stores
 * the size of remaning raw data in the memory pointed to by rem.
 * 
 * The function returns -1 upon failure.
 */
HIO_EXPORT int hio_json_feed (
	hio_json_t*   json,
	const void*   ptr,
	hio_oow_t     len,
	hio_oow_t*    rem,
	int stop_if_ever_completed
);

/* ========================================================================= */

HIO_EXPORT hio_jsonwr_t* hio_jsonwr_open (
	hio_t*             hio,
	hio_oow_t          xtnsize,
	int                flags
);

HIO_EXPORT void hio_jsonwr_close (
	hio_jsonwr_t* jsonwr
);

HIO_EXPORT int hio_jsonwr_init (
	hio_jsonwr_t* jsonwr,
	hio_t*        hio,
	int           flags
);

HIO_EXPORT void hio_jsonwr_fini (
	hio_jsonwr_t* jsonwr
);

#if defined(HIO_HAVE_INLINE)
static HIO_INLINE hio_t* hio_jsonwr_gethio (hio_jsonwr_t* jsonwr) { return jsonwr->hio; }
#else
#	define hio_jsonwr_gethio(jsonwr) (((hio_jsonwr_t*)(jsonwr))->hio)
#endif

#if defined(HIO_HAVE_INLINE)
static HIO_INLINE void* hio_jsonwr_getxtn (hio_jsonwr_t* jsonwr) { return (void*)(jsonwr + 1); }
#else
#define hio_jsonwr_getxtn(jsonwr) ((void*)((hio_jsonwr_t*)(jsonwr) + 1))
#endif

HIO_EXPORT void hio_jsonwr_setwritecb (
	hio_jsonwr_t*        jsonwr,
	hio_jsonwr_writecb_t writecb,
	void*                ctx
);

HIO_EXPORT int hio_jsonwr_write (
	hio_jsonwr_t*   jsonwr,
	hio_json_inst_t inst,
	int             is_uchars,
	const void*     dptr,
	hio_oow_t       dlen
);



#define hio_jsonwr_startarray(jsonwr) hio_jsonwr_write(jsonwr, HIO_JSON_INST_START_ARRAY, 0, HIO_NULL, 0)
#define hio_jsonwr_endarray(jsonwr) hio_jsonwr_write(jsonwr, HIO_JSON_INST_END_ARRAY, 0, HIO_NULL, 0)

#define hio_jsonwr_startobject(jsonwr) hio_jsonwr_write(jsonwr, HIO_JSON_INST_START_OBJECT, 0, HIO_NULL, 0)
#define hio_jsonwr_endobject(jsonwr) hio_jsonwr_write(jsonwr, HIO_JSON_INST_END_OBJECT, 0, HIO_NULL, 0)

#define hio_jsonwr_writenil(jsonwr) hio_jsonwr_write(jsonwr, HIO_JSON_INST_NIL, 0, HIO_NULL, 0)
#define hio_jsonwr_writetrue(jsonwr) hio_jsonwr_write(jsonwr, HIO_JSON_INST_TRUE, 0, HIO_NULL, 0)
#define hio_jsonwr_writefalse(jsonwr) hio_jsonwr_write(jsonwr, HIO_JSON_INST_FALSE, 0, HIO_NULL, 0)

#define hio_jsonwr_writekeywithuchars(jsonwr,dptr,dlen) hio_jsonwr_write(jsonwr, HIO_JSON_INST_KEY, 1, dptr, dlen)
#define hio_jsonwr_writekeywithbchars(jsonwr,dptr,dlen) hio_jsonwr_write(jsonwr, HIO_JSON_INST_KEY, 0, dptr, dlen)

#define hio_jsonwr_writekeywithucstr(jsonwr,dptr) hio_jsonwr_write(jsonwr, HIO_JSON_INST_KEY, 1, dptr, hio_count_ucstr(dptr))
#define hio_jsonwr_writekeywithbcstr(jsonwr,dptr) hio_jsonwr_write(jsonwr, HIO_JSON_INST_KEY, 0, dptr, hio_count_bcstr(dptr))

#define hio_jsonwr_writenumberwithuchars(jsonwr,dptr,dlen) hio_jsonwr_write(jsonwr, HIO_JSON_INST_NUMBER, 1, dptr, dlen)
#define hio_jsonwr_writenumberwithbchars(jsonwr,dptr,dlen) hio_jsonwr_write(jsonwr, HIO_JSON_INST_NUMBER, 0, dptr, dlen)

#define hio_jsonwr_writenumberwithucstr(jsonwr,dptr) hio_jsonwr_write(jsonwr, HIO_JSON_INST_NUMBER, 1, dptr, hio_count_ucstr(dptr))
#define hio_jsonwr_writenumberwithbcstr(jsonwr,dptr) hio_jsonwr_write(jsonwr, HIO_JSON_INST_NUMBER, 0, dptr, hio_count_bcstr(dptr))

#define hio_jsonwr_writestringwithuchars(jsonwr,dptr,dlen) hio_jsonwr_write(jsonwr, HIO_JSON_INST_STRING, 1, dptr, dlen)
#define hio_jsonwr_writestringwithbchars(jsonwr,dptr,dlen) hio_jsonwr_write(jsonwr, HIO_JSON_INST_STRING, 0, dptr, dlen)

#define hio_jsonwr_writestringwithucstr(jsonwr,dptr) hio_jsonwr_write(jsonwr, HIO_JSON_INST_STRING, 1, dptr, hio_count_ucstr(dptr))
#define hio_jsonwr_writestringwithbcstr(jsonwr,dptr) hio_jsonwr_write(jsonwr, HIO_JSON_INST_STRING, 0, dptr, hio_count_bcstr(dptr))

HIO_EXPORT int hio_jsonwr_writeintmax (
	hio_jsonwr_t*     jsonwr,
	hio_intmax_t      v
);

HIO_EXPORT int hio_jsonwr_writeuintmax (
	hio_jsonwr_t*     jsonwr,
	hio_uintmax_t     v
);

HIO_EXPORT int hio_jsonwr_writerawuchars (
	hio_jsonwr_t*     jsonwr,
	const hio_uch_t*  dptr,
	hio_oow_t         dlen
);

HIO_EXPORT int hio_jsonwr_writerawucstr (
	hio_jsonwr_t*     jsonwr,
	const hio_uch_t*  dptr
);

HIO_EXPORT int hio_jsonwr_writerawbchars (
	hio_jsonwr_t*     jsonwr,
	const hio_bch_t*  dptr,
	hio_oow_t         dlen
);

HIO_EXPORT int hio_jsonwr_writerawbcstr (
	hio_jsonwr_t*     jsonwr,
	const hio_bch_t*  dptr
);

#if defined(__cplusplus)
}
#endif

#endif
