/*
 * $Id$
 *
    Copyright (c) 2016-2018 Chung, Hyung-Hwan. All rights reserved.

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

#include <mio-json.h>
#include <mio-chr.h>
#include <mio-fmt.h>
#include "mio-prv.h"

#define MIO_JSON_TOKEN_NAME_ALIGN 64

/* this must not overlap with MIO_JSON_INST_XXXX enumerators in mio-json.h */
#define __INST_WORD_STRING  9999

/* ========================================================================= */

static void clear_token (mio_json_t* json)
{
	json->tok.len = 0;
	if (json->tok_capa > 0) json->tok.ptr[json->tok.len] = '\0';
}

static int add_char_to_token (mio_json_t* json, mio_ooch_t ch, int handle_surrogate_pair)
{
	if (json->tok.len >= json->tok_capa)
	{
		mio_ooch_t* tmp;
		mio_oow_t newcapa;

		newcapa = MIO_ALIGN_POW2(json->tok.len + 2, MIO_JSON_TOKEN_NAME_ALIGN);  /* +2 here because of -1 when setting newcapa */
		tmp = (mio_ooch_t*)mio_reallocmem(json->mio, json->tok.ptr, newcapa * MIO_SIZEOF(*tmp));
		if (MIO_UNLIKELY(!tmp)) return -1;

		json->tok_capa = newcapa - 1; /* -1 to secure space for terminating null */
		json->tok.ptr = tmp;
	}

#if (MIO_SIZEOF_OOCH_T >= 4) 
	if (handle_surrogate_pair && ch >= 0xDC00 && ch <= 0xDFFF && json->tok.len > 0)
	{
		/* RFC7159
			To escape an extended character that is not in the Basic Multilingual
			Plane, the character is represented as a 12-character sequence,
			encoding the UTF-16 surrogate pair.  So, for example, a string
			containing only the G clef character (U+1D11E) may be represented as
			"\uD834\uDD1E".
		*/
		mio_ooch_t pch = json->tok.ptr[json->tok.len - 1];
		if (pch >= 0xD800 && pch <= 0xDBFF)
		{
			/* X = (character outside BMP) - 0x10000;
			 * W1 = high ten bits of X + 0xD800
			 * W2 = low ten bits of X + 0xDC00 */
			json->tok.ptr[json->tok.len - 1] = (((pch - 0xD800) << 10) | (ch - 0xDC00)) + 0x10000;
			return 0;
		}
	}
#endif

	json->tok.ptr[json->tok.len++] = ch;
	json->tok.ptr[json->tok.len] = '\0';
	return 0;
}

static int add_chars_to_token (mio_json_t* json, const mio_ooch_t* ptr, mio_oow_t len)
{
	mio_oow_t i;
	
	if (json->tok_capa - json->tok.len > len)
	{
		mio_ooch_t* tmp;
		mio_oow_t newcapa;

		newcapa = MIO_ALIGN_POW2(json->tok.len + len + 1, MIO_JSON_TOKEN_NAME_ALIGN);
		tmp = (mio_ooch_t*)mio_reallocmem(json->mio, json->tok.ptr, newcapa * MIO_SIZEOF(*tmp));
		if (MIO_UNLIKELY(!tmp)) return -1;

		json->tok_capa = newcapa - 1;
		json->tok.ptr = tmp;
	}

	for (i = 0; i < len; i++) json->tok.ptr[json->tok.len++] = ptr[i];
	json->tok.ptr[json->tok.len] = '\0';
	return 0;
}

static MIO_INLINE mio_ooch_t unescape (mio_ooch_t c)
{
	switch (c)
	{
		case 'a': return '\a';
		case 'b': return '\b';
		case 'f': return '\f';
		case 'n': return '\n';
		case 'r': return '\r';
		case 't': return '\t';
		case 'v': return '\v';
		default: return c;
	}
}

/* ========================================================================= */

static int push_read_state (mio_json_t* json, mio_json_state_t state)
{
	mio_json_state_node_t* ss;

	ss = (mio_json_state_node_t*)mio_callocmem(json->mio, MIO_SIZEOF(*ss));
	if (MIO_UNLIKELY(!ss)) return -1;

	ss->state = state;
	ss->level = json->state_stack->level; /* copy from the parent */
	ss->index  = 0;
	ss->next = json->state_stack;
	
	json->state_stack = ss;
	return 0;
}

static void pop_read_state (mio_json_t* json)
{
	mio_json_state_node_t* ss;

	ss = json->state_stack;
	MIO_ASSERT (json->mio, ss != MIO_NULL && ss != &json->state_top);
	json->state_stack = ss->next;

	if (json->state_stack->state == MIO_JSON_STATE_IN_ARRAY)
	{
		json->state_stack->u.ia.got_value = 1;
	}
	else if (json->state_stack->state == MIO_JSON_STATE_IN_OBJECT)
	{
		json->state_stack->u.io.state++;
	}

/* TODO: don't free this. move it to the free list? */
	mio_freemem (json->mio, ss);
}

static void pop_all_read_states (mio_json_t* json)
{
	while (json->state_stack != &json->state_top) pop_read_state (json);
}

/* ========================================================================= */

static int invoke_data_inst (mio_json_t* json, mio_json_inst_t inst)
{
	mio_json_state_node_t* ss;
	int is_obj_val = 0;

	ss = json->state_stack;

	if (ss->state == MIO_JSON_STATE_IN_OBJECT)
	{
		if (ss->u.io.state == 1) /* got colon */
		{
			/* this is called after the reader has seen a colon. 
			 * the data item must be used as a key */

			if (inst != MIO_JSON_INST_STRING && inst != __INST_WORD_STRING)
			{
				mio_seterrbfmt (json->mio, MIO_EINVAL, "object key not a string - %.*js", json->tok.len, json->tok.ptr);
				return -1;
			}

			inst = MIO_JSON_INST_KEY;
		}
		else
		{
			/* if this variable is non-zero, level is set to 0 regardless of actual level */
			is_obj_val = 1;
		}
	}

	if (inst == __INST_WORD_STRING) 
	{
		mio_seterrbfmt (json->mio, MIO_EINVAL, "invalid word value - %.*js", json->tok.len, json->tok.ptr);
		return -1;
	}

	switch (inst)
	{
		case MIO_JSON_INST_START_ARRAY:
			if (push_read_state(json, MIO_JSON_STATE_IN_ARRAY) <= -1) return -1;
			json->state_stack->u.ia.got_value = 0;
			json->state_stack->level++;
			if (ss->state != MIO_JSON_STATE_IN_OBJECT || ss->u.io.state == 1) ss->index++;
			return json->instcb(json, inst, (is_obj_val? 0: json->state_stack->level - 1), ss->index - 1, ss->state, MIO_NULL, json->rctx);

		case MIO_JSON_INST_START_OBJECT:
			if (push_read_state(json, MIO_JSON_STATE_IN_OBJECT) <= -1) return -1;
			json->state_stack->u.io.state = 0;
			json->state_stack->level++;
			if (ss->state != MIO_JSON_STATE_IN_OBJECT || ss->u.io.state == 1) ss->index++;
			return json->instcb(json, inst, (is_obj_val? 0: json->state_stack->level - 1), ss->index - 1, ss->state, MIO_NULL, json->rctx);

		default:
			if (ss->state != MIO_JSON_STATE_IN_OBJECT || ss->u.io.state == 1) ss->index++;
			return json->instcb(json, inst, (is_obj_val? 0: json->state_stack->level), ss->index - 1, ss->state, &json->tok, json->rctx);
	}
}

static int handle_string_value_char (mio_json_t* json, mio_ooci_t c)
{
	int ret = 1;

	if (json->state_stack->u.sv.escaped == 3)
	{
		if (c >= '0' && c <= '7')
		{
			json->state_stack->u.sv.acc = json->state_stack->u.sv.acc * 8 + c - '0';
			json->state_stack->u.sv.digit_count++;
			if (json->state_stack->u.sv.digit_count >= json->state_stack->u.sv.escaped) goto add_sv_acc;
		}
		else
		{
			ret = 0;
			goto add_sv_acc;
		}
	}
	else if (json->state_stack->u.sv.escaped >= 2)
	{
		if (c >= '0' && c <= '9')
		{
			json->state_stack->u.sv.acc = json->state_stack->u.sv.acc * 16 + c - '0';
			json->state_stack->u.sv.digit_count++;
			if (json->state_stack->u.sv.digit_count >= json->state_stack->u.sv.escaped) goto add_sv_acc;
		}
		else if (c >= 'a' && c <= 'f')
		{
			json->state_stack->u.sv.acc = json->state_stack->u.sv.acc * 16 + c - 'a' + 10;
			json->state_stack->u.sv.digit_count++;
			if (json->state_stack->u.sv.digit_count >= json->state_stack->u.sv.escaped) goto add_sv_acc;
		}
		else if (c >= 'A' && c <= 'F')
		{
			json->state_stack->u.sv.acc = json->state_stack->u.sv.acc * 16 + c - 'A' + 10;
			json->state_stack->u.sv.digit_count++;
			if (json->state_stack->u.sv.digit_count >= json->state_stack->u.sv.escaped) goto add_sv_acc;
		}
		else
		{
			ret = 0;
		add_sv_acc:
		#if defined(MIO_OOCH_IS_UCH)
			if (add_char_to_token(json, json->state_stack->u.sv.acc, json->state_stack->u.sv.escaped == 4) <= -1) return -1;
		#else
			/* convert the character to utf8 */
			{
				mio_bch_t bcsbuf[MIO_BCSIZE_MAX];
				mio_oow_t n;

				n = json->mio->_cmgr->uctobc(json->state_stack->u.sv.acc, bcsbuf, MIO_COUNTOF(bcsbuf));
				if (n == 0 || n > MIO_COUNTOF(bcsbuf))
				{
					/* illegal character or buffer to small */
					mio_seterrbfmt (json->mio, MIO_EECERR, "unable to convert %jc", json->state_stack->u.sv.acc);
					return -1;
				}

				if (add_chars_to_token(json, bcsbuf, n) <= -1) return -1;
			}
		#endif
			json->state_stack->u.sv.escaped = 0;
		}
	}
	else if (json->state_stack->u.sv.escaped == 1)
	{
		if (c >= '0' && c <= '8') 
		{
			json->state_stack->u.sv.escaped = 3;
			json->state_stack->u.sv.digit_count = 0;
			json->state_stack->u.sv.acc = c - '0';
		}
		else if (c == 'x')
		{
			json->state_stack->u.sv.escaped = 2;
			json->state_stack->u.sv.digit_count = 0;
			json->state_stack->u.sv.acc = 0;
		}
		else if (c == 'u')
		{
			json->state_stack->u.sv.escaped = 4;
			json->state_stack->u.sv.digit_count = 0;
			json->state_stack->u.sv.acc = 0;
		}
		else if (c == 'U')
		{
			json->state_stack->u.sv.escaped = 8;
			json->state_stack->u.sv.digit_count = 0;
			json->state_stack->u.sv.acc = 0;
		}
		else
		{
			json->state_stack->u.sv.escaped = 0;
			if (add_char_to_token(json, unescape(c), 0) <= -1) return -1;
		}
	}
	else if (c == '\\')
	{
		json->state_stack->u.sv.escaped = 1;
	}
	else if (c == '\"')
	{
		pop_read_state (json);
		if (invoke_data_inst(json, MIO_JSON_INST_STRING) <= -1) return -1;
	}
	else
	{
		if (add_char_to_token(json, c, 0) <= -1) return -1;
	}

	return ret;
}

static int handle_numeric_value_char (mio_json_t* json, mio_ooci_t c)
{
	switch (json->state_stack->u.nv.progress)
	{
		case 0: /* integer part */
			if (mio_is_ooch_digit(c) || (json->tok.len == 0 && (c == '+' || c == '-')))
			{
				if (add_char_to_token(json, c, 0) <= -1) return -1;
				return 1;
			}
			else if ((c == '.' || c == 'e' || c == 'E') && json->tok.len > 0 && mio_is_ooch_digit(json->tok.ptr[json->tok.len - 1]))
			{
				if (add_char_to_token(json, c, 0) <= -1) return -1;
				json->state_stack->u.nv.progress = (c == '.'? 1: 2);
				return 1;
			}
			break;

		case 1: /* decimal part */
			if (mio_is_ooch_digit(c))
			{
				if (add_char_to_token(json, c, 0) <= -1) return -1;
				return 1;
			}
			else if (c == 'e' || c == 'E')
			{
				if (add_char_to_token(json, c, 0) <= -1) return -1;
				json->state_stack->u.nv.progress = 2;
				return 1;
			}
			break;

		case 2: /* exponent part (ok to have a sign) */
			if (c == '+' || c == '-')
			{
				if (add_char_to_token(json, c, 0) <= -1) return -1;
				json->state_stack->u.nv.progress = 3;
				return 1;
			}
			/* fall thru */
		case 3: /* exponent part (no sign expected) */
			if (mio_is_ooch_digit(c))
			{
				if (add_char_to_token(json, c, 0) <= -1) return -1;
				return 1;
			}
			break;
	}

	pop_read_state (json);

	MIO_ASSERT (json->mio, json->tok.len > 0);
	if (!mio_is_ooch_digit(json->tok.ptr[json->tok.len - 1]))
	{
		mio_seterrbfmt (json->mio, MIO_EINVAL, "invalid numeric value - %.*js", json->tok.len, json->tok.ptr);
		return -1;
	}
	if (invoke_data_inst(json, MIO_JSON_INST_NUMBER) <= -1) return -1;
	return 0; /* start over */
}

static int handle_word_value_char (mio_json_t* json, mio_ooci_t c)
{
	mio_json_inst_t inst;
	int ok;

	ok = (json->option & MIO_JSON_PERMITWORDKEY)?
		(mio_is_ooch_alpha(c) || mio_is_ooch_digit(c) || c == '_'):
		mio_is_ooch_alpha(c);
	if (ok)
	{
		if (add_char_to_token(json, c, 0) <= -1) return -1;
		return 1;
	}

	pop_read_state (json);

	if (mio_comp_oochars_bcstr(json->tok.ptr, json->tok.len, "null", 0) == 0) inst = MIO_JSON_INST_NIL;
	else if (mio_comp_oochars_bcstr(json->tok.ptr, json->tok.len, "true", 0) == 0) inst = MIO_JSON_INST_TRUE;
	else if (mio_comp_oochars_bcstr(json->tok.ptr, json->tok.len, "false", 0) == 0) inst = MIO_JSON_INST_FALSE;
	else if (json->option & MIO_JSON_PERMITWORDKEY) inst = __INST_WORD_STRING; /* internal only */
	else
	{
		mio_seterrbfmt (json->mio, MIO_EINVAL, "invalid word value - %.*js", json->tok.len, json->tok.ptr);
		return -1;
	}

	if (invoke_data_inst(json, inst) <= -1) return -1;
	return 0; /* start over */
}

/* ========================================================================= */

static int handle_start_char (mio_json_t* json, mio_ooci_t c)
{
	if (mio_is_ooch_space(c))
	{
		/* do nothing */
		return 1;
	}
	else if (c == '\"')
	{
		if (push_read_state(json, MIO_JSON_STATE_IN_STRING_VALUE) <= -1) return -1;
		clear_token (json);
		return 1; /* the quote dosn't form a string. so no start-over */
	}
	/* TOOD: else if (c == '#') MIO radixed number
	 */
	else if (mio_is_ooch_digit(c) || c == '+' || c == '-')
	{
		if (push_read_state(json, MIO_JSON_STATE_IN_NUMERIC_VALUE) <= -1) return -1;
		clear_token (json);
		json->state_stack->u.nv.progress = 0;
		return 0; /* start over to process c under the new state */
	}
	else if (mio_is_ooch_alpha(c))
	{
		if (push_read_state(json, MIO_JSON_STATE_IN_WORD_VALUE) <= -1) return -1;
		clear_token (json);
		return 0; /* start over to process c under the new state */
	}
	else if (c == '[')
	{
		if (invoke_data_inst(json, MIO_JSON_INST_START_ARRAY) <= -1) return -1;
		return 1;
	}
	else if (c == '{')
	{
		if (invoke_data_inst(json, MIO_JSON_INST_START_OBJECT) <= -1) return -1;
		return 1;
	}
	else
	{
		mio_seterrbfmt (json->mio, MIO_EINVAL, "not starting with an allowed initial character - %jc", (mio_ooch_t)c);
		return -1;
	}
}

static int handle_char_in_array (mio_json_t* json, mio_ooci_t c)
{
	if (mio_is_ooch_space(c))
	{
		/* do nothing */
		return 1;
	}
	else if (c == ']')
	{
		pop_read_state (json);
		/* START_ARRAY incremented index by 1. so subtract 1 from index before invoking instcb for END_ARRAY. */
		if (json->instcb(json, MIO_JSON_INST_END_ARRAY, json->state_stack->level, json->state_stack->index - 1, json->state_stack->state, MIO_NULL, json->rctx) <= -1) return -1;
		return 1;
	}
	else if (c == ',')
	{
		if (!json->state_stack->u.ia.got_value)
		{
			mio_seterrbfmt (json->mio, MIO_EINVAL, "redundant comma in array - %jc", (mio_ooch_t)c);
			return -1;
		}
		json->state_stack->u.ia.got_value = 0;
		return 1;
	}
	else
	{
		if (json->state_stack->u.ia.got_value)
		{
			mio_seterrbfmt (json->mio, MIO_EINVAL, "comma required in array - %jc", (mio_ooch_t)c);
			return -1;
		}

		if (c == '\"')
		{
			if (push_read_state(json, MIO_JSON_STATE_IN_STRING_VALUE) <= -1) return -1;
			clear_token (json);
			return 1;
		}
		/* TOOD: else if (c == '#') MIO radixed number
		 */
		else if (mio_is_ooch_digit(c) || c == '+' || c == '-')
		{
			if (push_read_state(json, MIO_JSON_STATE_IN_NUMERIC_VALUE) <= -1) return -1;
			clear_token (json);
			json->state_stack->u.nv.progress = 0;
			return 0; /* start over */
		}
		else if (mio_is_ooch_alpha(c))
		{
			if (push_read_state(json, MIO_JSON_STATE_IN_WORD_VALUE) <= -1) return -1;
			clear_token (json);
			return 0; /* start over */
		}
		else if (c == '[')
		{
			if (invoke_data_inst(json, MIO_JSON_INST_START_ARRAY) <= -1) return -1;
			return 1;
		}
		else if (c == '{')
		{
			if (invoke_data_inst(json, MIO_JSON_INST_START_OBJECT) <= -1) return -1;
			return 1;
		}
		else
		{
			mio_seterrbfmt (json->mio, MIO_EINVAL, "wrong character inside array - %jc[%d]", (mio_ooch_t)c, (int)c);
			return -1;
		}
	}
}

static int handle_char_in_object (mio_json_t* json, mio_ooci_t c)
{
	if (mio_is_ooch_space(c))
	{
		/* do nothing */
		return 1;
	}
	else if (c == '}')
	{
		pop_read_state (json);
		/* START_OBJECT incremented index by 1. so subtract 1 from index before invoking instcb for END_OBJECT. */
		if (json->instcb(json, MIO_JSON_INST_END_OBJECT, json->state_stack->level, json->state_stack->index - 1, json->state_stack->state, MIO_NULL, json->rctx) <= -1) return -1;
		return 1;
	}
	else if (c == ':')
	{
		if (json->state_stack->u.io.state != 1)
		{
			mio_seterrbfmt (json->mio, MIO_EINVAL, "redundant colon in object - %jc", (mio_ooch_t)c);
			return -1;
		}
		json->state_stack->u.io.state++;
		return 1;
	}
	else if (c == ',')
	{
		if (json->state_stack->u.io.state != 3)
		{
			mio_seterrbfmt (json->mio, MIO_EINVAL, "redundant comma in object - %jc", (mio_ooch_t)c);
			return -1;
		}
		json->state_stack->u.io.state = 0;
		return 1;
	}
	else
	{
		if (json->state_stack->u.io.state == 1)
		{
			mio_seterrbfmt (json->mio, MIO_EINVAL, "colon required in object - %jc", (mio_ooch_t)c);
			return -1;
		}
		else if (json->state_stack->u.io.state == 3)
		{
			mio_seterrbfmt (json->mio, MIO_EINVAL, "comma required in object - %jc", (mio_ooch_t)c);
			return -1;
		}

		if (c == '\"')
		{
			if (push_read_state(json, MIO_JSON_STATE_IN_STRING_VALUE) <= -1) return -1;
			clear_token (json);
			return 1;
		}
		/* TOOD: else if (c == '#') MIO radixed number
		 */
		else if (mio_is_ooch_digit(c) || c == '+' || c == '-')
		{
			if (push_read_state(json, MIO_JSON_STATE_IN_NUMERIC_VALUE) <= -1) return -1;
			clear_token (json);
			json->state_stack->u.nv.progress = 0;
			return 0; /* start over */
		}
		else if (mio_is_ooch_alpha(c))
		{
			if (push_read_state(json, MIO_JSON_STATE_IN_WORD_VALUE) <= -1) return -1;
			clear_token (json);
			return 0; /* start over */
		}
		else if (c == '[')
		{
			if (invoke_data_inst(json, MIO_JSON_INST_START_ARRAY) <= -1) return -1;
			return 1;
		}
		else if (c == '{')
		{
			if (invoke_data_inst(json, MIO_JSON_INST_START_OBJECT) <= -1) return -1;
			return 1;
		}
		else
		{
			mio_seterrbfmt (json->mio, MIO_EINVAL, "wrong character inside object - %jc[%d]", (mio_ooch_t)c, (int)c);
			return -1;
		}
	}
}

/* ========================================================================= */

static int handle_char (mio_json_t* json, mio_ooci_t c)
{
	int x;

start_over:
	if (c == MIO_OOCI_EOF)
	{
		if (json->state_stack->state == MIO_JSON_STATE_START)
		{
			/* no input data */
			return 0;
		}
		else
		{
			mio_seterrbfmt (json->mio, MIO_EBADRE, "unexpected end of data");
			return -1;
		}
	}

	switch (json->state_stack->state)
	{
		case MIO_JSON_STATE_START:
			x = handle_start_char(json, c);
			break;

		case MIO_JSON_STATE_IN_ARRAY:
			x = handle_char_in_array(json, c);
			break;

		case MIO_JSON_STATE_IN_OBJECT:
			x = handle_char_in_object(json, c);
			break;

		case MIO_JSON_STATE_IN_WORD_VALUE:
			x = handle_word_value_char(json, c);
			break;

		case MIO_JSON_STATE_IN_STRING_VALUE:
			x = handle_string_value_char(json, c);
			break;

		case MIO_JSON_STATE_IN_NUMERIC_VALUE:
			x = handle_numeric_value_char(json, c);
			break;

		default:
			mio_seterrbfmt (json->mio, MIO_EINTERN, "internal error - must not be called for state %d", (int)json->state_stack->state);
			return -1;
	}

	if (x <= -1) return -1;
	if (x == 0) goto start_over;

	return 0;
}

/* ========================================================================= */

static int feed_json_data (mio_json_t* json, const mio_bch_t* data, mio_oow_t len, mio_oow_t* xlen, int stop_if_ever_completed)
{
	const mio_bch_t* ptr;
	const mio_bch_t* end;
	int ever_completed = 0;

	ptr = data;
	end = ptr + len;

	while (ptr < end)
	{
		mio_ooci_t c;
		const mio_bch_t* optr;

	#if defined(MIO_OOCH_IS_UCH)
		mio_ooch_t uc;
		mio_oow_t bcslen;
		mio_oow_t n;

		optr = ptr;
		bcslen = end - ptr;
		n = json->mio->_cmgr->bctouc(ptr, bcslen, &uc);
		if (n == 0)
		{
			/* invalid sequence */
			uc = *ptr;
			n = 1;
		}
		else if (n > bcslen)
		{
			/* incomplete sequence */
			*xlen = ptr - data; 
			return 0; /* feed more for incomplete sequence */
		}

		ptr += n;
		c = uc;
	#else
		optr = ptr;
		c = *ptr++;
	#endif

		if (json->state_stack->state == MIO_JSON_STATE_START && mio_is_ooch_space(c)) continue; /* skip white space */

		if (stop_if_ever_completed && ever_completed) 
		{
			*xlen = optr - data;
			return 2;
		}

		/* handle a signle character */
		if (handle_char(json, c) <= -1) goto oops;
		if (json->state_stack->state == MIO_JSON_STATE_START) ever_completed = 1;
	}

	*xlen = ptr - data;
	return (stop_if_ever_completed && ever_completed)? 2: 1;

oops:
	/* TODO: compute the number of processed bytes so far and return it via a parameter??? */
/*printf ("feed oops....\n");*/
	return -1;
}


/* ========================================================================= */

mio_json_t* mio_json_open (mio_t* mio, mio_oow_t xtnsize)
{
	mio_json_t* json;

	json = (mio_json_t*)mio_allocmem(mio, MIO_SIZEOF(*json) + xtnsize);
	if (MIO_LIKELY(json))
	{
		if (mio_json_init(json, mio) <= -1)
		{
			mio_freemem (mio, json);
			return MIO_NULL;
		}
		else
		{
			MIO_MEMSET (json + 1,  0, xtnsize);
		}
	}

	return json;
}

void mio_json_close (mio_json_t* json)
{
	mio_json_fini (json);
	mio_freemem (json->mio, json);
}

static int do_nothing_on_inst  (mio_json_t* json, mio_json_inst_t inst, mio_oow_t level, mio_oow_t index, mio_json_state_t container_state, const mio_oocs_t* str, void* ctx)
{
	return 0;
}

int mio_json_init (mio_json_t* json, mio_t* mio)
{
	MIO_MEMSET (json, 0, MIO_SIZEOF(*json));

	json->mio = mio;
	json->instcb = do_nothing_on_inst;
	json->state_top.state = MIO_JSON_STATE_START;
	json->state_top.next = MIO_NULL;
	json->state_stack = &json->state_top;

	return 0;
}

void mio_json_fini (mio_json_t* json)
{
	pop_all_read_states (json);
	if (json->tok.ptr) 
	{
		mio_freemem (json->mio, json->tok.ptr);
		json->tok.ptr = MIO_NULL;
	}
}
/* ========================================================================= */

mio_bitmask_t mio_json_getoption (mio_json_t* json)
{
	return json->option;
}

void mio_json_setoption (mio_json_t* json, mio_bitmask_t mask)
{
	json->option = mask;
}

void mio_json_setinstcb (mio_json_t* json, mio_json_instcb_t instcb, void* ctx)
{
	json->instcb = instcb;
	json->rctx = ctx;
}

mio_json_state_t mio_json_getstate (mio_json_t* json)
{
	return json->state_stack->state;
}

void mio_json_resetstates (mio_json_t* json)
{
	pop_all_read_states (json);
	MIO_ASSERT (json->mio, json->state_stack == &json->state_top);
	json->state_stack->state = MIO_JSON_STATE_START;
}

int mio_json_feed (mio_json_t* json, const void* ptr, mio_oow_t len, mio_oow_t* rem, int stop_if_ever_completed)
{
	int x;
	mio_oow_t total, ylen;
	const mio_bch_t* buf;

	buf = (const mio_bch_t*)ptr;
	total = 0;
	while (total < len)
	{
		x = feed_json_data(json, &buf[total], len - total, &ylen, stop_if_ever_completed);
		if (x <= -1) return -1;

		total += ylen;
		if (x == 0) break; /* incomplete sequence encountered */

		if (stop_if_ever_completed && x >= 2) 
		{
			*rem = len - total;
			return 1;
		}
	}

	*rem = len - total;
	return 0;
}


/* ========================================================================= */

static int push_write_state (mio_jsonwr_t* jsonwr, mio_json_state_t state)
{
	mio_jsonwr_state_node_t* ss;

	ss = (mio_jsonwr_state_node_t*)mio_callocmem(jsonwr->mio, MIO_SIZEOF(*ss));
	if (MIO_UNLIKELY(!ss)) return -1;

	ss->state = state;
	ss->level = jsonwr->state_stack->level; /* copy from the parent */
	ss->next = jsonwr->state_stack;

	jsonwr->state_stack = ss;
	return 0;
}

static void pop_write_state (mio_jsonwr_t* jsonwr)
{
	mio_jsonwr_state_node_t* ss;

	ss = jsonwr->state_stack;
	MIO_ASSERT (jsonwr->mio, ss != MIO_NULL && ss != &jsonwr->state_top);
	jsonwr->state_stack = ss->next;

/* TODO: don't free this. move it to the free list? */
	mio_freemem (jsonwr->mio, ss);
}

static void pop_all_write_states (mio_jsonwr_t* jsonwr)
{
	while (jsonwr->state_stack != &jsonwr->state_top) pop_write_state (jsonwr);
}

mio_jsonwr_t* mio_jsonwr_open (mio_t* mio, mio_oow_t xtnsize, int flags)
{
	mio_jsonwr_t* jsonwr;

	jsonwr = (mio_jsonwr_t*)mio_allocmem(mio, MIO_SIZEOF(*jsonwr) + xtnsize);
	if (MIO_LIKELY(jsonwr))
	{
		if (mio_jsonwr_init(jsonwr, mio, flags) <= -1)
		{
			mio_freemem (mio, jsonwr);
			return MIO_NULL;
		}
		else
		{
			MIO_MEMSET (jsonwr + 1,  0, xtnsize);
		}
	}

	return jsonwr;
}

void mio_jsonwr_close (mio_jsonwr_t* jsonwr)
{
	mio_jsonwr_fini (jsonwr);
	mio_freemem (jsonwr->mio, jsonwr);
}

static int write_nothing (mio_jsonwr_t* jsonwr, const mio_bch_t* dptr, mio_oow_t dlen, void* ctx)
{
	return 0;
}

int mio_jsonwr_init (mio_jsonwr_t* jsonwr, mio_t* mio, int flags)
{
	MIO_MEMSET (jsonwr, 0, MIO_SIZEOF(*jsonwr));

	jsonwr->mio = mio;
	jsonwr->writecb = write_nothing;
	jsonwr->flags = flags;

	jsonwr->state_top.state = MIO_JSON_STATE_START;
	jsonwr->state_top.next = MIO_NULL;
	jsonwr->state_stack = &jsonwr->state_top;
	return 0;
}

static int flush_wbuf (mio_jsonwr_t* jsonwr)
{
	int ret = 0;

	if (jsonwr->writecb(jsonwr, jsonwr->wbuf, jsonwr->wbuf_len, jsonwr->wctx) <= -1) ret = -1;
	jsonwr->wbuf_len = 0; /* reset the buffer length regardless of writing result */

	return ret;
}

void mio_jsonwr_fini (mio_jsonwr_t* jsonwr)
{
	if (jsonwr->wbuf_len > 0) flush_wbuf (jsonwr); /* don't care about actual write failure */
	pop_all_write_states (jsonwr);
}

/* ========================================================================= */

void mio_jsonwr_setwritecb (mio_jsonwr_t* jsonwr, mio_jsonwr_writecb_t writecb, void* ctx)
{
	jsonwr->writecb = writecb;
	jsonwr->wctx = ctx;
}

static int escape_char (mio_uch_t uch, mio_uch_t* xch)
{
	int x = 1;

	switch (uch)
	{
		case '\"':
		case '\\':
			*xch = uch;
			break;

		case '\a':
			*xch = 'a';
			break;

		case '\b':
			*xch = 'b';
			break;

		case '\f':
			*xch = 'f';
			break;

		case '\n':
			*xch = 'n';
			break;

		case '\r':
			*xch = 'r';
			break;

		case '\t':
			*xch = 't';
			break;

		case '\v':
			*xch = 'v';
			break;

		default:
			x = (uch >= 0 && uch <= 0x1f)? 2: 0;
			break;
	}

	return x;
}

static int write_bytes_noesc (mio_jsonwr_t* jsonwr, const mio_bch_t* dptr, mio_oow_t dlen)
{
	mio_oow_t rem;
	do
	{
		rem = MIO_COUNTOF(jsonwr->wbuf) - jsonwr->wbuf_len;

		if (dlen <= rem) 
		{
			MIO_MEMCPY (&jsonwr->wbuf[jsonwr->wbuf_len], dptr, dlen);
			jsonwr->wbuf_len += dlen;
			if (dlen == rem && flush_wbuf(jsonwr) <= -1) return -1;
			break;
		}

		MIO_MEMCPY (&jsonwr->wbuf[jsonwr->wbuf_len], dptr, rem);
		jsonwr->wbuf_len += rem;
		dptr += rem;
		dlen -= rem;
		if (flush_wbuf(jsonwr) <= -1) return -1;
	}
	while (dlen > 0);

	return 0;
}

static int write_bytes_esc (mio_jsonwr_t* jsonwr, const mio_bch_t* dptr, mio_oow_t dlen)
{
	const mio_bch_t* dend = dptr + dlen;

	while (dptr < dend)
	{
		int e;
		mio_uch_t ec;
		e = escape_char(*dptr, &ec);
		if (e <= 0)
		{
			jsonwr->wbuf[jsonwr->wbuf_len++] = *dptr;
			if (jsonwr->wbuf_len >= MIO_COUNTOF(jsonwr->wbuf) && flush_wbuf(jsonwr) <= -1) return -1;
		}
		else if (e == 1)
		{
			jsonwr->wbuf[jsonwr->wbuf_len++] = '\\';
			if (jsonwr->wbuf_len >= MIO_COUNTOF(jsonwr->wbuf) && flush_wbuf(jsonwr) <= -1) return -1;
			jsonwr->wbuf[jsonwr->wbuf_len++] = ec;
			if (jsonwr->wbuf_len >= MIO_COUNTOF(jsonwr->wbuf) && flush_wbuf(jsonwr) <= -1) return -1;
		}
		else
		{
			mio_bch_t bcsbuf[7];
			bcsbuf[0] = '\\';
			bcsbuf[1] = 'u';
			mio_fmt_uintmax_to_bcstr(&bcsbuf[2], 5, *dptr, 10, 4, '0', MIO_NULL);
			if (write_bytes_noesc(jsonwr, bcsbuf, 6) <= -1) return -1;
		}

		dptr++;
	}

	return 0;
}


static int write_uchars (mio_jsonwr_t* jsonwr, int escape, const mio_uch_t* ptr, mio_oow_t len)
{
	mio_t* mio = mio_jsonwr_getmio(jsonwr);
	const mio_uch_t* end = ptr + len;
	mio_bch_t bcsbuf[MIO_BCSIZE_MAX + 4];
	mio_oow_t n;

	while (ptr < end)
	{
		if (escape)
		{
			int e;
			mio_uch_t ec;
			e = escape_char(*ptr, &ec);
			if (e <= 0) goto no_escape;
			else if (e == 1)
			{
				bcsbuf[0] = '\\';
				bcsbuf[1] = ec;
				n = 2;
			}
			else
			{
				bcsbuf[0] = '\\';
				bcsbuf[1] = 'u';
				mio_fmt_uintmax_to_bcstr(&bcsbuf[2], 5, *ptr, 10, 4, '0', MIO_NULL);
				n = 6;
			}
		}
		else
		{
		no_escape:
			n = mio->_cmgr->uctobc(*ptr, bcsbuf, MIO_COUNTOF(bcsbuf));
			if (n == 0) 
			{
				mio_seterrnum (mio, MIO_EECERR);
				return -1;
			}
		}

		ptr++;
		if (write_bytes_noesc(jsonwr, bcsbuf, n) <= -1) return -1;
	}

	return 0;
}


#define WRITE_BYTES_NOESC(jsonwr,dptr,dlen) do { if (write_bytes_noesc(jsonwr, dptr, dlen) <= -1) return -1; } while(0)
#define WRITE_BYTES_ESC(jsonwr,dptr,dlen) do { if (write_bytes_esc(jsonwr, dptr, dlen) <= -1) return -1; } while(0)

#define WRITE_UCHARS(jsonwr,esc,dptr,dlen) do { if (write_uchars(jsonwr, esc, dptr, dlen) <= -1) return -1; } while(0)

#define WRITE_LINE_BREAK(jsonwr) WRITE_BYTES_NOESC(jsonwr, "\n", 1)

#define WRITE_COMMA(jsonwr) do { WRITE_BYTES_NOESC(jsonwr, ",", 1); if (jsonwr->flags & MIO_JSONWR_FLAG_PRETTY) WRITE_LINE_BREAK(jsonwr); } while(0)

#define PREACTION_FOR_VLAUE(jsonwr,sn) do { \
	if (sn->state != MIO_JSON_STATE_IN_ARRAY && !(sn->state == MIO_JSON_STATE_IN_OBJECT && sn->obj_awaiting_val)) goto incompatible_inst; \
	if (sn->index > 0 && sn->state == MIO_JSON_STATE_IN_ARRAY) WRITE_COMMA (jsonwr); \
	sn->index++; \
	sn->obj_awaiting_val = 0; \
	if ((jsonwr->flags & MIO_JSONWR_FLAG_PRETTY) && sn->state == MIO_JSON_STATE_IN_ARRAY) WRITE_INDENT (jsonwr); \
} while(0)

#define WRITE_INDENT(jsonwr) do { mio_oow_t i; for (i = 0; i < jsonwr->state_stack->level; i++) WRITE_BYTES_NOESC (jsonwr, "\t", 1); } while(0)

int mio_jsonwr_write (mio_jsonwr_t* jsonwr, mio_json_inst_t inst, int is_uchars, const void* dptr, mio_oow_t dlen)
{
	mio_jsonwr_state_node_t* sn = jsonwr->state_stack;

	switch (inst)
	{
		case MIO_JSON_INST_START_ARRAY:
			if (sn->state != MIO_JSON_STATE_START && sn->state != MIO_JSON_STATE_IN_ARRAY &&
			    !(sn->state == MIO_JSON_STATE_IN_OBJECT && sn->obj_awaiting_val)) goto incompatible_inst;
			if (sn->index > 0) WRITE_COMMA (jsonwr);
			sn->index++;
			if ((jsonwr->flags & MIO_JSONWR_FLAG_PRETTY) &&
			    !(sn->state == MIO_JSON_STATE_IN_OBJECT && sn->obj_awaiting_val))
			{
					WRITE_INDENT (jsonwr);
			}
			sn->obj_awaiting_val = 0;
			WRITE_BYTES_NOESC (jsonwr, "[", 1); 
			if (jsonwr->flags & MIO_JSONWR_FLAG_PRETTY) WRITE_LINE_BREAK (jsonwr);
			if (push_write_state(jsonwr, MIO_JSON_STATE_IN_ARRAY) <= -1) return -1;
			jsonwr->state_stack->level++;
			break;

		case MIO_JSON_INST_START_OBJECT:
			if (sn->state != MIO_JSON_STATE_START && sn->state != MIO_JSON_STATE_IN_ARRAY &&
			    !(sn->state == MIO_JSON_STATE_IN_OBJECT && sn->obj_awaiting_val)) goto incompatible_inst; 
			if (sn->index > 0) WRITE_COMMA (jsonwr);
			sn->index++;
			if ((jsonwr->flags & MIO_JSONWR_FLAG_PRETTY) &&
			    !(sn->state == MIO_JSON_STATE_IN_OBJECT && sn->obj_awaiting_val))
			{
					WRITE_INDENT (jsonwr);
			}
			sn->obj_awaiting_val = 0;
			WRITE_BYTES_NOESC (jsonwr, "{", 1); 
			if (jsonwr->flags & MIO_JSONWR_FLAG_PRETTY) WRITE_LINE_BREAK (jsonwr);
			if (push_write_state (jsonwr, MIO_JSON_STATE_IN_OBJECT) <= -1) return -1;
			jsonwr->state_stack->level++;
			break;

		case MIO_JSON_INST_END_ARRAY:
			if (sn->state != MIO_JSON_STATE_IN_ARRAY) goto incompatible_inst;
			pop_write_state (jsonwr);
			if (jsonwr->flags & MIO_JSONWR_FLAG_PRETTY) 
			{
				WRITE_LINE_BREAK (jsonwr);
				WRITE_INDENT (jsonwr);
			}
			WRITE_BYTES_NOESC (jsonwr, "]", 1); 
			if (jsonwr->state_stack->state == MIO_JSON_STATE_START) 
			{
				/* end of json */
				if (jsonwr->flags & MIO_JSONWR_FLAG_PRETTY) WRITE_LINE_BREAK (jsonwr);
				if (jsonwr->wbuf_len > 0 && flush_wbuf(jsonwr) <= -1) return -1;
			}
			break;

		case MIO_JSON_INST_END_OBJECT:
			if (sn->state != MIO_JSON_STATE_IN_OBJECT || sn->obj_awaiting_val) goto incompatible_inst;
			pop_write_state (jsonwr);
			if (jsonwr->flags & MIO_JSONWR_FLAG_PRETTY) 
			{
				WRITE_LINE_BREAK (jsonwr);
				WRITE_INDENT (jsonwr);
			}
			WRITE_BYTES_NOESC (jsonwr, "}", 1); 
			if (jsonwr->state_stack->state == MIO_JSON_STATE_START) 
			{
				/* end of json */
				if (jsonwr->flags & MIO_JSONWR_FLAG_PRETTY) WRITE_LINE_BREAK (jsonwr);
				if (jsonwr->wbuf_len > 0 && flush_wbuf(jsonwr) <= -1) return -1;
			}
			break;

		case MIO_JSON_INST_KEY:
			if (sn->state != MIO_JSON_STATE_IN_OBJECT || sn->obj_awaiting_val) goto incompatible_inst;
			if (sn->index > 0) WRITE_COMMA (jsonwr);
			if (jsonwr->flags & MIO_JSONWR_FLAG_PRETTY) WRITE_INDENT (jsonwr);
			WRITE_BYTES_NOESC (jsonwr, "\"", 1);
			if (is_uchars) WRITE_UCHARS (jsonwr, 1, dptr, dlen);
			else WRITE_BYTES_ESC (jsonwr, dptr, dlen);
			WRITE_BYTES_NOESC (jsonwr, "\": ", 3);
			sn->obj_awaiting_val = 1;
			break;

		case MIO_JSON_INST_NIL:
			PREACTION_FOR_VLAUE (jsonwr, sn);
			WRITE_BYTES_NOESC (jsonwr, "nil", 3);
			break;

		case MIO_JSON_INST_TRUE:
			PREACTION_FOR_VLAUE (jsonwr, sn);
			WRITE_BYTES_NOESC (jsonwr, "true", 4);
			break;
			
		case MIO_JSON_INST_FALSE:
			PREACTION_FOR_VLAUE (jsonwr, sn);
			WRITE_BYTES_NOESC (jsonwr, "false", 5);
			break;

		case MIO_JSON_INST_NUMBER:
			PREACTION_FOR_VLAUE (jsonwr, sn);
			if (is_uchars)
				WRITE_UCHARS (jsonwr, 0, dptr, dlen);
			else
				WRITE_BYTES_NOESC (jsonwr, dptr, dlen);
			break;

		case MIO_JSON_INST_STRING:
			PREACTION_FOR_VLAUE (jsonwr, sn);
			WRITE_BYTES_NOESC (jsonwr, "\"", 1);
			if (is_uchars) WRITE_UCHARS (jsonwr, 1, dptr, dlen);
			else WRITE_BYTES_ESC (jsonwr, dptr, dlen);
			WRITE_BYTES_NOESC (jsonwr, "\"", 1);
			break;

		default:
		incompatible_inst:
			flush_wbuf (jsonwr);
			mio_seterrbfmt (jsonwr->mio, MIO_EINVAL, "incompatiable write instruction - %d", (int)inst);
			return -1;
	}

	return 0;
}


int mio_jsonwr_writeintmax (mio_jsonwr_t* jsonwr, mio_intmax_t v)
{
	mio_jsonwr_state_node_t* sn = jsonwr->state_stack;
	mio_bch_t tmp[((MIO_SIZEOF_UINTMAX_T * MIO_BITS_PER_BYTE) / 3) + 3]; /* there can be a sign. so +3 instead of +2 */
	mio_oow_t len;

	PREACTION_FOR_VLAUE (jsonwr, sn);
	len = mio_fmt_intmax_to_bcstr(tmp, MIO_COUNTOF(tmp), v, 10, 0, '\0', MIO_NULL);
	WRITE_BYTES_NOESC (jsonwr, tmp, len);
	return 0;

incompatible_inst:
	flush_wbuf (jsonwr);
	mio_seterrbfmt (jsonwr->mio, MIO_EINVAL, "incompatiable integer write instruction");
	return -1;
}

int mio_jsonwr_writeuintmax (mio_jsonwr_t* jsonwr, mio_uintmax_t v)
{
	mio_jsonwr_state_node_t* sn = jsonwr->state_stack;
	mio_bch_t tmp[((MIO_SIZEOF_UINTMAX_T * MIO_BITS_PER_BYTE) / 3) + 2];
	mio_oow_t len;

	PREACTION_FOR_VLAUE (jsonwr, sn);
	len = mio_fmt_uintmax_to_bcstr(tmp, MIO_COUNTOF(tmp), v, 10, 0, '\0', MIO_NULL);
	WRITE_BYTES_NOESC (jsonwr, tmp, len);
	return 0;

incompatible_inst:
	flush_wbuf (jsonwr);
	mio_seterrbfmt (jsonwr->mio, MIO_EINVAL, "incompatiable integer write instruction");
	return -1;
}

int mio_jsonwr_writerawuchars (mio_jsonwr_t* jsonwr, const mio_uch_t* dptr, mio_oow_t dlen)
{
	WRITE_UCHARS (jsonwr, 0, dptr, dlen);
	return 0;
}

int mio_jsonwr_writerawucstr (mio_jsonwr_t* jsonwr, const mio_uch_t* dptr)
{
	WRITE_UCHARS (jsonwr, 0, dptr, mio_count_ucstr(dptr));
	return 0;
}

int mio_jsonwr_writerawbchars (mio_jsonwr_t* jsonwr, const mio_bch_t* dptr, mio_oow_t dlen)
{
	WRITE_BYTES_NOESC (jsonwr, dptr, dlen);
	return 0;
}

int mio_jsonwr_writerawbcstr (mio_jsonwr_t* jsonwr, const mio_bch_t* dptr)
{
	WRITE_BYTES_NOESC (jsonwr, dptr, mio_count_bcstr(dptr));
	return 0;
}
