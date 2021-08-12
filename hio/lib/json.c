/*
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

#include <hio-json.h>
#include <hio-chr.h>
#include <hio-fmt.h>
#include "hio-prv.h"

#define HIO_JSON_TOKEN_NAME_ALIGN 64

/* this must not overlap with HIO_JSON_INST_XXXX enumerators in hio-json.h */
#define __INST_WORD_STRING  9999

/* ========================================================================= */

static void clear_token (hio_json_t* json)
{
	json->tok.len = 0;
	json->tok_line = json->c_line;
	json->tok_col = json->c_col;
	if (json->tok_capa > 0) json->tok.ptr[json->tok.len] = '\0';
}

static int add_char_to_token (hio_json_t* json, hio_ooch_t ch, int handle_surrogate_pair)
{
	if (json->tok.len >= json->tok_capa)
	{
		hio_ooch_t* tmp;
		hio_oow_t newcapa;

		newcapa = HIO_ALIGN_POW2(json->tok.len + 2, HIO_JSON_TOKEN_NAME_ALIGN);  /* +2 here because of -1 when setting newcapa */
		tmp = (hio_ooch_t*)hio_reallocmem(json->hio, json->tok.ptr, newcapa * HIO_SIZEOF(*tmp));
		if (HIO_UNLIKELY(!tmp)) return -1;

		json->tok_capa = newcapa - 1; /* -1 to secure space for terminating null */
		json->tok.ptr = tmp;
	}

#if (HIO_SIZEOF_OOCH_T >= 4) 
	if (handle_surrogate_pair && ch >= 0xDC00 && ch <= 0xDFFF && json->tok.len > 0)
	{
		/* RFC7159
			To escape an extended character that is not in the Basic Multilingual
			Plane, the character is represented as a 12-character sequence,
			encoding the UTF-16 surrogate pair.  So, for example, a string
			containing only the G clef character (U+1D11E) may be represented as
			"\uD834\uDD1E".
		*/
		hio_ooch_t pch = json->tok.ptr[json->tok.len - 1];
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

static int add_chars_to_token (hio_json_t* json, const hio_ooch_t* ptr, hio_oow_t len)
{
	hio_oow_t i;
	
	if (json->tok_capa - json->tok.len > len)
	{
		hio_ooch_t* tmp;
		hio_oow_t newcapa;

		newcapa = HIO_ALIGN_POW2(json->tok.len + len + 1, HIO_JSON_TOKEN_NAME_ALIGN);
		tmp = (hio_ooch_t*)hio_reallocmem(json->hio, json->tok.ptr, newcapa * HIO_SIZEOF(*tmp));
		if (HIO_UNLIKELY(!tmp)) return -1;

		json->tok_capa = newcapa - 1;
		json->tok.ptr = tmp;
	}

	for (i = 0; i < len; i++) json->tok.ptr[json->tok.len++] = ptr[i];
	json->tok.ptr[json->tok.len] = '\0';
	return 0;
}

static HIO_INLINE hio_ooch_t unescape (hio_ooch_t c)
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

static int push_read_state (hio_json_t* json, hio_json_state_t state)
{
	hio_json_state_node_t* ss;

	ss = (hio_json_state_node_t*)hio_callocmem(json->hio, HIO_SIZEOF(*ss));
	if (HIO_UNLIKELY(!ss)) return -1;

	ss->state = state;
	ss->level = json->state_stack->level; /* copy from the parent */
	ss->index  = 0;
	ss->in_comment = 0;
	ss->next = json->state_stack;
	
	json->state_stack = ss;
	return 0;
}

static void pop_read_state (hio_json_t* json)
{
	hio_json_state_node_t* ss;

	ss = json->state_stack;
	HIO_ASSERT (json->hio, ss != HIO_NULL && ss != &json->state_top);
	json->state_stack = ss->next;

	if (json->state_stack->state == HIO_JSON_STATE_IN_ARRAY)
	{
		json->state_stack->u.ia.got_value = 1;
	}
	else if (json->state_stack->state == HIO_JSON_STATE_IN_OBJECT)
	{
		json->state_stack->u.io.state++;
	}

/* TODO: don't free this. move it to the free list? */
	hio_freemem (json->hio, ss);
}

static void pop_all_read_states (hio_json_t* json)
{
	while (json->state_stack != &json->state_top) pop_read_state (json);
}

/* ========================================================================= */

static int invoke_data_inst (hio_json_t* json, hio_json_inst_t inst)
{
	hio_json_state_node_t* ss;
	int is_obj_val = 0;

	ss = json->state_stack;

	if (ss->state == HIO_JSON_STATE_IN_OBJECT)
	{
		if (ss->u.io.state == 1) 
		{
			/* just got the key part. the colon has not been seen.  */

			if (inst != HIO_JSON_INST_STRING && inst != __INST_WORD_STRING)
			{
				if (inst == HIO_JSON_INST_END_ARRAY)
					hio_seterrbfmt (json->hio, HIO_EINVAL, "object key not a string - <array> at %zu:%zu", json->c_line, json->c_col);
				else if (inst == HIO_JSON_INST_END_OBJECT)
					hio_seterrbfmt (json->hio, HIO_EINVAL, "object key not a string - <object> at %zu:%zu", json->c_line, json->c_col);
				else
					hio_seterrbfmt (json->hio, HIO_EINVAL, "object key not a string - %.*js at %zu:%zu", json->tok.len, json->tok.ptr, json->tok_line, json->tok_col);
				return -1;
			}

			inst = HIO_JSON_INST_KEY;
		}
		else 
		{
			/* if this variable is non-zero, level is set to 0 regardless of actual level.
			 * this helps the callback to print the value without indentation immediately
			 * after the key */
			//is_obj_val = 1;
			is_obj_val = (ss->u.io.state >= 2);
		}
	}

	if (inst == __INST_WORD_STRING) 
	{
		hio_seterrbfmt (json->hio, HIO_EINVAL, "invalid word value - %.*js at line %zu:%zu", json->tok.len, json->tok.ptr, json->tok_line, json->tok_col);
		return -1;
	}

	switch (inst)
	{
		case HIO_JSON_INST_START_ARRAY:
		{
			hio_json_state_node_t* nss;
			if (push_read_state(json, HIO_JSON_STATE_IN_ARRAY) <= -1) return -1;
			nss = json->state_stack;
			nss->u.ia.got_value = 0;
			nss->level++;

			HIO_ASSERT (json->hio, nss->level == ss->level + 1);
			return json->instcb(json, inst, (is_obj_val? 0: ss->level), ss->index, ss->state, HIO_NULL, json->rctx);
			/* no increment on ss->index here. incremented on END */
		}

		case HIO_JSON_INST_END_ARRAY:
			if (json->instcb(json, HIO_JSON_INST_END_ARRAY, ss->level, ss->index, ss->state, HIO_NULL, json->rctx) <= -1) return -1;
			if (ss->state != HIO_JSON_STATE_IN_OBJECT || ss->u.io.state == 3) ss->index++;
			break;

		case HIO_JSON_INST_START_OBJECT:
		{
			hio_json_state_node_t* nss;

			if (push_read_state(json, HIO_JSON_STATE_IN_OBJECT) <= -1) return -1;
			nss = json->state_stack;
			nss->u.io.state = 0;
			nss->level++;

			HIO_ASSERT (json->hio, nss->level == ss->level + 1);
			return json->instcb(json, inst, (is_obj_val? 0: ss->level), ss->index, ss->state, HIO_NULL, json->rctx);
			/* no increment on ss->index here. incremented on END */
		}

		case HIO_JSON_INST_END_OBJECT:
			if (json->instcb(json, HIO_JSON_INST_END_OBJECT, ss->level, ss->index, ss->state, HIO_NULL, json->rctx) <= -1) return -1;
			if (ss->state != HIO_JSON_STATE_IN_OBJECT || ss->u.io.state == 3) ss->index++;
			break;

		default:
			if (json->instcb(json, inst, (is_obj_val? 0: ss->level), ss->index, ss->state, &json->tok, json->rctx) <= -1) return -1;
			if (ss->state != HIO_JSON_STATE_IN_OBJECT || ss->u.io.state == 3) ss->index++;
			break;
	}

	return 0;
}

static int handle_string_value_char (hio_json_t* json, hio_ooci_t c)
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
		#if defined(HIO_OOCH_IS_UCH)
			if (add_char_to_token(json, json->state_stack->u.sv.acc, json->state_stack->u.sv.escaped == 4) <= -1) return -1;
		#else
			/* convert the character to utf8 */
			{
				hio_bch_t bcsbuf[HIO_BCSIZE_MAX];
				hio_oow_t n;

				n = json->hio->_cmgr->uctobc(json->state_stack->u.sv.acc, bcsbuf, HIO_COUNTOF(bcsbuf));
				if (n == 0 || n > HIO_COUNTOF(bcsbuf))
				{
					/* illegal character or buffer to small */
					hio_seterrbfmt (json->hio, HIO_EECERR, "unable to convert %jc", json->state_stack->u.sv.acc);
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
		if (invoke_data_inst(json, HIO_JSON_INST_STRING) <= -1) return -1;
	}
	else
	{
		if (add_char_to_token(json, c, 0) <= -1) return -1;
	}

	return ret;
}

static int handle_numeric_value_char (hio_json_t* json, hio_ooci_t c)
{
	switch (json->state_stack->u.nv.progress)
	{
		case 0: /* integer part */
			if (hio_is_ooch_digit(c) || (json->tok.len == 0 && (c == '+' || c == '-')))
			{
				if (add_char_to_token(json, c, 0) <= -1) return -1;
				return 1;
			}
			else if ((c == '.' || c == 'e' || c == 'E') && json->tok.len > 0 && hio_is_ooch_digit(json->tok.ptr[json->tok.len - 1]))
			{
				if (add_char_to_token(json, c, 0) <= -1) return -1;
				json->state_stack->u.nv.progress = (c == '.'? 1: 2);
				return 1;
			}
			break;

		case 1: /* decimal part */
			if (hio_is_ooch_digit(c))
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
			if (hio_is_ooch_digit(c))
			{
				if (add_char_to_token(json, c, 0) <= -1) return -1;
				return 1;
			}
			break;
	}

	pop_read_state (json);

	HIO_ASSERT (json->hio, json->tok.len > 0);
	if (!hio_is_ooch_digit(json->tok.ptr[json->tok.len - 1]))
	{
		hio_seterrbfmt (json->hio, HIO_EINVAL, "invalid numeric value - %.*js", json->tok.len, json->tok.ptr);
		return -1;
	}
	if (invoke_data_inst(json, HIO_JSON_INST_NUMBER) <= -1) return -1;
	return 0; /* start over */
}

static int handle_word_value_char (hio_json_t* json, hio_ooci_t c)
{
	hio_json_inst_t inst;
	int ok;

	ok = (json->option & HIO_JSON_PERMIT_WORD_KEY)?
		(hio_is_ooch_alpha(c) || hio_is_ooch_digit(c) || c == '_' || c == '-'):
		hio_is_ooch_alpha(c);
	if (ok)
	{
		if (add_char_to_token(json, c, 0) <= -1) return -1;
		return 1;
	}

	pop_read_state (json);

	if (hio_comp_oochars_bcstr(json->tok.ptr, json->tok.len, "null", 0) == 0) inst = HIO_JSON_INST_NIL;
	else if (hio_comp_oochars_bcstr(json->tok.ptr, json->tok.len, "true", 0) == 0) inst = HIO_JSON_INST_TRUE;
	else if (hio_comp_oochars_bcstr(json->tok.ptr, json->tok.len, "false", 0) == 0) inst = HIO_JSON_INST_FALSE;
	else if (json->option & HIO_JSON_PERMIT_WORD_KEY) inst = __INST_WORD_STRING; /* internal only */
	else
	{
		hio_seterrbfmt (json->hio, HIO_EINVAL, "invalid word value - %.*js", json->tok.len, json->tok.ptr);
		return -1;
	}

	if (invoke_data_inst(json, inst) <= -1) return -1;
	return 0; /* start over */
}

/* ========================================================================= */

static int handle_start_char (hio_json_t* json, hio_ooci_t c)
{
	if (hio_is_ooch_space(c))
	{
		/* do nothing */
		return 1;
	}
	else if ((json->option & HIO_JSON_LINE_COMMENT) && c == '#')
	{
		/* line comment */
		json->state_stack->in_comment = 1;
		return 1;
	}
	else if (c == '\"')
	{
		if (push_read_state(json, HIO_JSON_STATE_IN_STRING_VALUE) <= -1) return -1;
		clear_token (json);
		return 1; /* the quote dosn't form a string. so no start-over */
	}
	else if (hio_is_ooch_digit(c) || c == '+' || c == '-')
	{
		if (push_read_state(json, HIO_JSON_STATE_IN_NUMERIC_VALUE) <= -1) return -1;
		clear_token (json);
		json->state_stack->u.nv.progress = 0;
		return 0; /* start over to process c under the new state */
	}
	else if (hio_is_ooch_alpha(c))
	{
		if (push_read_state(json, HIO_JSON_STATE_IN_WORD_VALUE) <= -1) return -1;
		clear_token (json);
		return 0; /* start over to process c under the new state */
	}
	else if (c == '[')
	{
		if (invoke_data_inst(json, HIO_JSON_INST_START_ARRAY) <= -1) return -1;
		return 1;
	}
	else if (c == '{')
	{
		if (invoke_data_inst(json, HIO_JSON_INST_START_OBJECT) <= -1) return -1;
		return 1;
	}
	else
	{
		hio_seterrbfmt (json->hio, HIO_EINVAL, "not starting with an allowed initial character - %jc at %zu:%zu", (hio_ooch_t)c, json->c_line,json->c_col);
		return -1;
	}
}

static int handle_char_in_array (hio_json_t* json, hio_ooci_t c)
{
	if (hio_is_ooch_space(c))
	{
		/* do nothing */
		return 1;
	}
	else if ((json->option & HIO_JSON_LINE_COMMENT) && c == '#')
	{
		/* line comment */
		json->state_stack->in_comment = 1;
		return 1;
	}
	else if (c == ']')
	{
		pop_read_state (json);
		if (invoke_data_inst(json, HIO_JSON_INST_END_ARRAY) <= -1) return -1;
		return 1;
	}
	else if (c == ',')
	{
		if (!json->state_stack->u.ia.got_value)
		{
			hio_seterrbfmt (json->hio, HIO_EINVAL, "redundant comma in array - %jc at %zu:%zu", (hio_ooch_t)c, json->c_line, json->c_col);
			return -1;
		}
		json->state_stack->u.ia.got_value = 0;
		return 1;
	}
	else
	{
		if (json->state_stack->u.ia.got_value)
		{
			if (json->option & HIO_JSON_OPTIONAL_COMMA)
			{
				json->state_stack->u.ia.got_value = 0;
			}
			else
			{
				hio_seterrbfmt (json->hio, HIO_EINVAL, "comma required in array - %jc at %zu:%zu", (hio_ooch_t)c, json->c_line, json->c_col);
				return -1;
			}
		}

		if (c == '\"')
		{
			if (push_read_state(json, HIO_JSON_STATE_IN_STRING_VALUE) <= -1) return -1;
			clear_token (json);
			return 1;
		}
		else if (hio_is_ooch_digit(c) || c == '+' || c == '-')
		{
			if (push_read_state(json, HIO_JSON_STATE_IN_NUMERIC_VALUE) <= -1) return -1;
			clear_token (json);
			json->state_stack->u.nv.progress = 0;
			return 0; /* start over */
		}
		else if (hio_is_ooch_alpha(c))
		{
			if (push_read_state(json, HIO_JSON_STATE_IN_WORD_VALUE) <= -1) return -1;
			clear_token (json);
			return 0; /* start over */
		}
		else if (c == '[')
		{
			if (invoke_data_inst(json, HIO_JSON_INST_START_ARRAY) <= -1) return -1;
			return 1;
		}
		else if (c == '{')
		{
			if (invoke_data_inst(json, HIO_JSON_INST_START_OBJECT) <= -1) return -1;
			return 1;
		}
		else
		{
			hio_seterrbfmt (json->hio, HIO_EINVAL, "wrong character inside array - %jc[%d] at %zu:%zu", (hio_ooch_t)c, (int)c, json->c_line, json->c_col);
			return -1;
		}
	}
}

static int handle_char_in_object (hio_json_t* json, hio_ooci_t c)
{
	if (hio_is_ooch_space(c))
	{
		/* do nothing */
		return 1;
	}
	else if ((json->option & HIO_JSON_LINE_COMMENT) && c == '#')
	{
		/* line comment */
		json->state_stack->in_comment = 1;
		return 1;
	}
	else if (c == '}')
	{
		/* 0 - initial, 1 - got key, 2 -> got colon, 3 -> got value, 0 -> after comma */
		if (json->state_stack->u.io.state == 1 || json->state_stack->u.io.state == 2)
		{
			hio_seterrbfmt (json->hio, HIO_EINVAL, "no value for a key in object at %zu:%zu", json->c_line, json->c_col);
			return -1;
		}

		pop_read_state (json);
		if (invoke_data_inst(json, HIO_JSON_INST_END_OBJECT) <= -1) return -1;
		return 1;
	}
	else if (c == ':')
	{
		if (json->state_stack->u.io.state != 1)
		{
			hio_seterrbfmt (json->hio, HIO_EINVAL, "redundant colon in object - %jc at %zu:%zu", (hio_ooch_t)c, json->c_line, json->c_col);
			return -1;
		}
		json->state_stack->u.io.state++;
		return 1;
	}
	else if (c == ',')
	{
		if (json->state_stack->u.io.state != 3)
		{
			hio_seterrbfmt (json->hio, HIO_EINVAL, "comma without value or redundant comma in object - %jc at %zu:%zu", (hio_ooch_t)c, json->c_line, json->c_col);
			return -1;
		}
		json->state_stack->u.io.state = 0;
		return 1;
	}
	else
	{
		if (json->state_stack->u.io.state == 1)
		{
			hio_seterrbfmt (json->hio, HIO_EINVAL, "colon required in object - %jc at %zu:%zu", (hio_ooch_t)c, json->c_line, json->c_col);
			return -1;
		}
		else if (json->state_stack->u.io.state == 3)
		{
			if (json->option & HIO_JSON_OPTIONAL_COMMA)
			{
				json->state_stack->u.io.state = 0;
			}
			else
			{
				hio_seterrbfmt (json->hio, HIO_EINVAL, "comma required in object - %jc at %zu:%zu", (hio_ooch_t)c, json->c_line, json->c_col);
			}
		}

		if (c == '\"')
		{
			if (push_read_state(json, HIO_JSON_STATE_IN_STRING_VALUE) <= -1) return -1;
			clear_token (json);
			return 1;
		}
		else if (hio_is_ooch_digit(c) || c == '+' || c == '-')
		{
			if (push_read_state(json, HIO_JSON_STATE_IN_NUMERIC_VALUE) <= -1) return -1;
			clear_token (json);
			json->state_stack->u.nv.progress = 0;
			return 0; /* start over */
		}
		else if (hio_is_ooch_alpha(c))
		{
			if (push_read_state(json, HIO_JSON_STATE_IN_WORD_VALUE) <= -1) return -1;
			clear_token (json);
			return 0; /* start over */
		}
		else if (c == '[')
		{
			if (invoke_data_inst(json, HIO_JSON_INST_START_ARRAY) <= -1) return -1;
			return 1;
		}
		else if (c == '{')
		{
			if (invoke_data_inst(json, HIO_JSON_INST_START_OBJECT) <= -1) return -1;
			return 1;
		}
		else
		{
			hio_seterrbfmt (json->hio, HIO_EINVAL, "wrong character inside object - %jc[%d] at %zu:%zu", (hio_ooch_t)c, (int)c, json->c_line, json->c_col);
			return -1;
		}
	}
}

/* ========================================================================= */

static int handle_char (hio_json_t* json, hio_ooci_t c)
{
	int x;

start_over:
	if (c == HIO_OOCI_EOF)
	{
		if (json->state_stack->state == HIO_JSON_STATE_START)
		{
			/* no input data */
			return 0;
		}
		else
		{
			hio_seterrbfmt (json->hio, HIO_EBADRE, "unexpected end of data at %zu:%zu", json->c_line, json->c_col);
			return -1;
		}
	}

	switch (json->state_stack->state)
	{
		case HIO_JSON_STATE_START:
			x = handle_start_char(json, c);
			break;

		case HIO_JSON_STATE_IN_ARRAY:
			x = handle_char_in_array(json, c);
			break;

		case HIO_JSON_STATE_IN_OBJECT:
			x = handle_char_in_object(json, c);
			break;

		case HIO_JSON_STATE_IN_WORD_VALUE:
			x = handle_word_value_char(json, c);
			break;

		case HIO_JSON_STATE_IN_STRING_VALUE:
			x = handle_string_value_char(json, c);
			break;

		case HIO_JSON_STATE_IN_NUMERIC_VALUE:
			x = handle_numeric_value_char(json, c);
			break;

		default:
			hio_seterrbfmt (json->hio, HIO_EINTERN, "internal error - must not be called for state %d", (int)json->state_stack->state);
			return -1;
	}

	if (x <= -1) return -1;
	if (x == 0) goto start_over;

	return 0;
}

/* ========================================================================= */

static int feed_json_data (hio_json_t* json, const hio_bch_t* data, hio_oow_t len, hio_oow_t* xlen, int stop_if_ever_completed)
{
	const hio_bch_t* ptr;
	const hio_bch_t* end;
	int ever_completed = 0;

	ptr = data;
	end = ptr + len;

	while (ptr < end)
	{
		hio_ooci_t c;
		const hio_bch_t* optr;

	#if defined(HIO_OOCH_IS_UCH)
		hio_ooch_t uc;
		hio_oow_t bcslen;
		hio_oow_t n;

		optr = ptr;
		bcslen = end - ptr;
		n = json->hio->_cmgr->bctouc(ptr, bcslen, &uc);
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

		if (c == HIO_EOL)
		{
			json->c_col = 0;
			json->c_line++;
		}
		else
		{
			json->c_col++;
		}

		if (json->state_stack->in_comment) 
		{
			if (c == HIO_EOL) json->state_stack->in_comment = 0;
			continue;
		}
		if (json->state_stack->state == HIO_JSON_STATE_START && hio_is_ooch_space(c)) continue; /* skip white space */

		if (stop_if_ever_completed && ever_completed) 
		{
			*xlen = optr - data;
			return 2;
		}

		/* handle a signle character */
		if (handle_char(json, c) <= -1) goto oops;
		if (json->state_stack->state == HIO_JSON_STATE_START) ever_completed = 1;
	}

	*xlen = ptr - data;
	return (stop_if_ever_completed && ever_completed)? 2: 1;

oops:
	/* TODO: compute the number of processed bytes so far and return it via a parameter??? */
/*printf ("feed oops....\n");*/
	return -1;
}


/* ========================================================================= */

hio_json_t* hio_json_open (hio_t* hio, hio_oow_t xtnsize)
{
	hio_json_t* json;

	json = (hio_json_t*)hio_allocmem(hio, HIO_SIZEOF(*json) + xtnsize);
	if (HIO_LIKELY(json))
	{
		if (hio_json_init(json, hio) <= -1)
		{
			hio_freemem (hio, json);
			return HIO_NULL;
		}
		else
		{
			HIO_MEMSET (json + 1,  0, xtnsize);
		}
	}

	return json;
}

void hio_json_close (hio_json_t* json)
{
	hio_json_fini (json);
	hio_freemem (json->hio, json);
}

static int do_nothing_on_inst  (hio_json_t* json, hio_json_inst_t inst, hio_oow_t level, hio_oow_t index, hio_json_state_t container_state, const hio_oocs_t* str, void* ctx)
{
	return 0;
}

int hio_json_init (hio_json_t* json, hio_t* hio)
{
	HIO_MEMSET (json, 0, HIO_SIZEOF(*json));

	json->hio = hio;
	json->instcb = do_nothing_on_inst;
	json->state_top.state = HIO_JSON_STATE_START;
	json->state_top.next = HIO_NULL;
	json->state_stack = &json->state_top;

	json->c_line = 1;
	json->c_col = 0;

	return 0;
}

void hio_json_fini (hio_json_t* json)
{
	pop_all_read_states (json);
	if (json->tok.ptr) 
	{
		hio_freemem (json->hio, json->tok.ptr);
		json->tok.ptr = HIO_NULL;
	}
}
/* ========================================================================= */

hio_bitmask_t hio_json_getoption (hio_json_t* json)
{
	return json->option;
}

void hio_json_setoption (hio_json_t* json, hio_bitmask_t mask)
{
	json->option = mask;
}

void hio_json_setinstcb (hio_json_t* json, hio_json_instcb_t instcb, void* ctx)
{
	json->instcb = instcb;
	json->rctx = ctx;
}

hio_json_state_t hio_json_getstate (hio_json_t* json)
{
	return json->state_stack->state;
}

void hio_json_resetstates (hio_json_t* json)
{
	pop_all_read_states (json);
	HIO_ASSERT (json->hio, json->state_stack == &json->state_top);
	json->state_stack->state = HIO_JSON_STATE_START;
}

void hio_json_resetfeedloc (hio_json_t* json)
{
	json->c_line = 1;
	json->c_col = 0;
}

int hio_json_feed (hio_json_t* json, const void* ptr, hio_oow_t len, hio_oow_t* rem, int stop_if_ever_completed)
{
	int x;
	hio_oow_t total, ylen;
	const hio_bch_t* buf;

	buf = (const hio_bch_t*)ptr;
	total = 0;
	while (total < len)
	{
		x = feed_json_data(json, &buf[total], len - total, &ylen, stop_if_ever_completed);
		if (x <= -1) return -1;

		total += ylen;
		if (x == 0) break; /* incomplete sequence encountered */

		if (stop_if_ever_completed && x >= 2) 
		{
			if (rem) *rem = len - total;
			return 1;
		}
	}

	if (rem) *rem = len - total;
	return 0;
}

/* ========================================================================= */

static int push_write_state (hio_jsonwr_t* jsonwr, hio_json_state_t state)
{
	hio_jsonwr_state_node_t* ss;

	ss = (hio_jsonwr_state_node_t*)hio_callocmem(jsonwr->hio, HIO_SIZEOF(*ss));
	if (HIO_UNLIKELY(!ss)) return -1;

	ss->state = state;
	ss->level = jsonwr->state_stack->level; /* copy from the parent */
	ss->next = jsonwr->state_stack;

	jsonwr->state_stack = ss;
	return 0;
}

static void pop_write_state (hio_jsonwr_t* jsonwr)
{
	hio_jsonwr_state_node_t* ss;

	ss = jsonwr->state_stack;
	HIO_ASSERT (jsonwr->hio, ss != HIO_NULL && ss != &jsonwr->state_top);
	jsonwr->state_stack = ss->next;

/* TODO: don't free this. move it to the free list? */
	hio_freemem (jsonwr->hio, ss);
}

static void pop_all_write_states (hio_jsonwr_t* jsonwr)
{
	while (jsonwr->state_stack != &jsonwr->state_top) pop_write_state (jsonwr);
}

hio_jsonwr_t* hio_jsonwr_open (hio_t* hio, hio_oow_t xtnsize, int flags)
{
	hio_jsonwr_t* jsonwr;

	jsonwr = (hio_jsonwr_t*)hio_allocmem(hio, HIO_SIZEOF(*jsonwr) + xtnsize);
	if (HIO_LIKELY(jsonwr))
	{
		if (hio_jsonwr_init(jsonwr, hio, flags) <= -1)
		{
			hio_freemem (hio, jsonwr);
			return HIO_NULL;
		}
		else
		{
			HIO_MEMSET (jsonwr + 1,  0, xtnsize);
		}
	}

	return jsonwr;
}

void hio_jsonwr_close (hio_jsonwr_t* jsonwr)
{
	hio_jsonwr_fini (jsonwr);
	hio_freemem (jsonwr->hio, jsonwr);
}

static int write_nothing (hio_jsonwr_t* jsonwr, const hio_bch_t* dptr, hio_oow_t dlen, void* ctx)
{
	return 0;
}

int hio_jsonwr_init (hio_jsonwr_t* jsonwr, hio_t* hio, int flags)
{
	HIO_MEMSET (jsonwr, 0, HIO_SIZEOF(*jsonwr));

	jsonwr->hio = hio;
	jsonwr->writecb = write_nothing;
	jsonwr->flags = flags;

	jsonwr->state_top.state = HIO_JSON_STATE_START;
	jsonwr->state_top.next = HIO_NULL;
	jsonwr->state_stack = &jsonwr->state_top;
	return 0;
}

static int flush_wbuf (hio_jsonwr_t* jsonwr)
{
	int ret = 0;

	if (jsonwr->writecb(jsonwr, jsonwr->wbuf, jsonwr->wbuf_len, jsonwr->wctx) <= -1) ret = -1;
	jsonwr->wbuf_len = 0; /* reset the buffer length regardless of writing result */

	return ret;
}

void hio_jsonwr_fini (hio_jsonwr_t* jsonwr)
{
	if (jsonwr->wbuf_len > 0) flush_wbuf (jsonwr); /* don't care about actual write failure */
	pop_all_write_states (jsonwr);
}

/* ========================================================================= */

void hio_jsonwr_setwritecb (hio_jsonwr_t* jsonwr, hio_jsonwr_writecb_t writecb, void* ctx)
{
	jsonwr->writecb = writecb;
	jsonwr->wctx = ctx;
}

static int escape_char (hio_uch_t uch, hio_uch_t* xch)
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

static int write_bytes_noesc (hio_jsonwr_t* jsonwr, const hio_bch_t* dptr, hio_oow_t dlen)
{
	hio_oow_t rem;
	do
	{
		rem = HIO_COUNTOF(jsonwr->wbuf) - jsonwr->wbuf_len;

		if (dlen <= rem) 
		{
			HIO_MEMCPY (&jsonwr->wbuf[jsonwr->wbuf_len], dptr, dlen);
			jsonwr->wbuf_len += dlen;
			if (dlen == rem && flush_wbuf(jsonwr) <= -1) return -1;
			break;
		}

		HIO_MEMCPY (&jsonwr->wbuf[jsonwr->wbuf_len], dptr, rem);
		jsonwr->wbuf_len += rem;
		dptr += rem;
		dlen -= rem;
		if (flush_wbuf(jsonwr) <= -1) return -1;
	}
	while (dlen > 0);

	return 0;
}

static int write_bytes_esc (hio_jsonwr_t* jsonwr, const hio_bch_t* dptr, hio_oow_t dlen)
{
	const hio_bch_t* dend = dptr + dlen;

	while (dptr < dend)
	{
		int e;
		hio_uch_t ec;
		e = escape_char(*dptr, &ec);
		if (e <= 0)
		{
			jsonwr->wbuf[jsonwr->wbuf_len++] = *dptr;
			if (jsonwr->wbuf_len >= HIO_COUNTOF(jsonwr->wbuf) && flush_wbuf(jsonwr) <= -1) return -1;
		}
		else if (e == 1)
		{
			jsonwr->wbuf[jsonwr->wbuf_len++] = '\\';
			if (jsonwr->wbuf_len >= HIO_COUNTOF(jsonwr->wbuf) && flush_wbuf(jsonwr) <= -1) return -1;
			jsonwr->wbuf[jsonwr->wbuf_len++] = ec;
			if (jsonwr->wbuf_len >= HIO_COUNTOF(jsonwr->wbuf) && flush_wbuf(jsonwr) <= -1) return -1;
		}
		else
		{
			hio_bch_t bcsbuf[7];
			bcsbuf[0] = '\\';
			bcsbuf[1] = 'u';
			hio_fmt_uintmax_to_bcstr(&bcsbuf[2], 5, *dptr, 10, 4, '0', HIO_NULL);
			if (write_bytes_noesc(jsonwr, bcsbuf, 6) <= -1) return -1;
		}

		dptr++;
	}

	return 0;
}


static int write_uchars (hio_jsonwr_t* jsonwr, int escape, const hio_uch_t* ptr, hio_oow_t len)
{
	hio_t* hio = hio_jsonwr_gethio(jsonwr);
	const hio_uch_t* end = ptr + len;
	hio_bch_t bcsbuf[HIO_BCSIZE_MAX + 4];
	hio_oow_t n;

	while (ptr < end)
	{
		if (escape)
		{
			int e;
			hio_uch_t ec;
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
				hio_fmt_uintmax_to_bcstr(&bcsbuf[2], 5, *ptr, 10, 4, '0', HIO_NULL);
				n = 6;
			}
		}
		else
		{
		no_escape:
			n = hio->_cmgr->uctobc(*ptr, bcsbuf, HIO_COUNTOF(bcsbuf));
			if (n == 0) 
			{
				hio_seterrnum (hio, HIO_EECERR);
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

#define WRITE_COMMA(jsonwr) do { WRITE_BYTES_NOESC(jsonwr, ",", 1); if (jsonwr->flags & HIO_JSONWR_FLAG_PRETTY) WRITE_LINE_BREAK(jsonwr); } while(0)

#define PREACTION_FOR_VALUE(jsonwr,sn) do { \
	if (sn->state != HIO_JSON_STATE_IN_ARRAY && !(sn->state == HIO_JSON_STATE_IN_OBJECT && sn->obj_awaiting_val)) goto incompatible_inst; \
	if (sn->index > 0 && sn->state == HIO_JSON_STATE_IN_ARRAY) WRITE_COMMA (jsonwr); \
	sn->index++; \
	sn->obj_awaiting_val = 0; \
	if ((jsonwr->flags & HIO_JSONWR_FLAG_PRETTY) && sn->state == HIO_JSON_STATE_IN_ARRAY) WRITE_INDENT (jsonwr); \
} while(0)

#define WRITE_INDENT(jsonwr) do { hio_oow_t i; for (i = 0; i < jsonwr->state_stack->level; i++) WRITE_BYTES_NOESC (jsonwr, "\t", 1); } while(0)

int hio_jsonwr_write (hio_jsonwr_t* jsonwr, hio_json_inst_t inst, int is_uchars, const void* dptr, hio_oow_t dlen)
{
	hio_jsonwr_state_node_t* sn = jsonwr->state_stack;

	switch (inst)
	{
		case HIO_JSON_INST_START_ARRAY:
			if (sn->state != HIO_JSON_STATE_START && sn->state != HIO_JSON_STATE_IN_ARRAY &&
			    !(sn->state == HIO_JSON_STATE_IN_OBJECT && sn->obj_awaiting_val)) goto incompatible_inst;
			if (sn->index > 0 && sn->state == HIO_JSON_STATE_IN_ARRAY) WRITE_COMMA (jsonwr);
			sn->index++;
			if ((jsonwr->flags & HIO_JSONWR_FLAG_PRETTY) &&
			    !(sn->state == HIO_JSON_STATE_IN_OBJECT && sn->obj_awaiting_val))
			{
				WRITE_INDENT (jsonwr);
			}
			sn->obj_awaiting_val = 0;
			WRITE_BYTES_NOESC (jsonwr, "[", 1); 
			if (jsonwr->flags & HIO_JSONWR_FLAG_PRETTY) WRITE_LINE_BREAK (jsonwr);
			if (push_write_state(jsonwr, HIO_JSON_STATE_IN_ARRAY) <= -1) return -1;
			jsonwr->state_stack->level++;
			break;

		case HIO_JSON_INST_START_OBJECT:
			if (sn->state != HIO_JSON_STATE_START && sn->state != HIO_JSON_STATE_IN_ARRAY &&
			    !(sn->state == HIO_JSON_STATE_IN_OBJECT && sn->obj_awaiting_val)) goto incompatible_inst; 
			if (sn->index > 0 && sn->state == HIO_JSON_STATE_IN_ARRAY) WRITE_COMMA (jsonwr);
			sn->index++;
			if ((jsonwr->flags & HIO_JSONWR_FLAG_PRETTY) &&
			    !(sn->state == HIO_JSON_STATE_IN_OBJECT && sn->obj_awaiting_val))
			{
					WRITE_INDENT (jsonwr);
			}
			sn->obj_awaiting_val = 0;
			WRITE_BYTES_NOESC (jsonwr, "{", 1); 
			if (jsonwr->flags & HIO_JSONWR_FLAG_PRETTY) WRITE_LINE_BREAK (jsonwr);
			if (push_write_state (jsonwr, HIO_JSON_STATE_IN_OBJECT) <= -1) return -1;
			jsonwr->state_stack->level++;
			break;

		case HIO_JSON_INST_END_ARRAY:
			if (sn->state != HIO_JSON_STATE_IN_ARRAY) goto incompatible_inst;
			pop_write_state (jsonwr);
			if (jsonwr->flags & HIO_JSONWR_FLAG_PRETTY) 
			{
				WRITE_LINE_BREAK (jsonwr);
				WRITE_INDENT (jsonwr);
			}
			WRITE_BYTES_NOESC (jsonwr, "]", 1); 
			if (jsonwr->state_stack->state == HIO_JSON_STATE_START) 
			{
				/* end of json */
				if (jsonwr->flags & HIO_JSONWR_FLAG_PRETTY) WRITE_LINE_BREAK (jsonwr);
				if (jsonwr->wbuf_len > 0 && flush_wbuf(jsonwr) <= -1) return -1;
			}
			break;

		case HIO_JSON_INST_END_OBJECT:
			if (sn->state != HIO_JSON_STATE_IN_OBJECT || sn->obj_awaiting_val) goto incompatible_inst;
			pop_write_state (jsonwr);
			if (jsonwr->flags & HIO_JSONWR_FLAG_PRETTY) 
			{
				WRITE_LINE_BREAK (jsonwr);
				WRITE_INDENT (jsonwr);
			}
			WRITE_BYTES_NOESC (jsonwr, "}", 1); 
			if (jsonwr->state_stack->state == HIO_JSON_STATE_START) 
			{
				/* end of json */
				if (jsonwr->flags & HIO_JSONWR_FLAG_PRETTY) WRITE_LINE_BREAK (jsonwr);
				if (jsonwr->wbuf_len > 0 && flush_wbuf(jsonwr) <= -1) return -1;
			}
			break;

		case HIO_JSON_INST_KEY:
			if (sn->state != HIO_JSON_STATE_IN_OBJECT || sn->obj_awaiting_val) goto incompatible_inst;
			if (sn->index > 0) WRITE_COMMA (jsonwr);
			if (jsonwr->flags & HIO_JSONWR_FLAG_PRETTY) WRITE_INDENT (jsonwr);
			WRITE_BYTES_NOESC (jsonwr, "\"", 1);
			if (is_uchars) WRITE_UCHARS (jsonwr, 1, dptr, dlen);
			else WRITE_BYTES_ESC (jsonwr, dptr, dlen);
			WRITE_BYTES_NOESC (jsonwr, "\": ", 3);
			sn->obj_awaiting_val = 1;
			break;

		case HIO_JSON_INST_NIL:
			PREACTION_FOR_VALUE (jsonwr, sn);
			WRITE_BYTES_NOESC (jsonwr, "nil", 3);
			break;

		case HIO_JSON_INST_TRUE:
			PREACTION_FOR_VALUE (jsonwr, sn);
			WRITE_BYTES_NOESC (jsonwr, "true", 4);
			break;
			
		case HIO_JSON_INST_FALSE:
			PREACTION_FOR_VALUE (jsonwr, sn);
			WRITE_BYTES_NOESC (jsonwr, "false", 5);
			break;

		case HIO_JSON_INST_NUMBER:
			PREACTION_FOR_VALUE (jsonwr, sn);
			if (is_uchars)
				WRITE_UCHARS (jsonwr, 0, dptr, dlen);
			else
				WRITE_BYTES_NOESC (jsonwr, dptr, dlen);
			break;

		case HIO_JSON_INST_STRING:
			PREACTION_FOR_VALUE (jsonwr, sn);
			WRITE_BYTES_NOESC (jsonwr, "\"", 1);
			if (is_uchars) WRITE_UCHARS (jsonwr, 1, dptr, dlen);
			else WRITE_BYTES_ESC (jsonwr, dptr, dlen);
			WRITE_BYTES_NOESC (jsonwr, "\"", 1);
			break;

		default:
		incompatible_inst:
			flush_wbuf (jsonwr);
			hio_seterrbfmt (jsonwr->hio, HIO_EINVAL, "incompatiable write instruction - %d", (int)inst);
			return -1;
	}

	return 0;
}


int hio_jsonwr_writeintmax (hio_jsonwr_t* jsonwr, hio_intmax_t v)
{
	hio_jsonwr_state_node_t* sn = jsonwr->state_stack;
	hio_bch_t tmp[((HIO_SIZEOF_UINTMAX_T * HIO_BITS_PER_BYTE) / 3) + 3]; /* there can be a sign. so +3 instead of +2 */
	hio_oow_t len;

	PREACTION_FOR_VALUE (jsonwr, sn);
	len = hio_fmt_intmax_to_bcstr(tmp, HIO_COUNTOF(tmp), v, 10, 0, '\0', HIO_NULL);
	WRITE_BYTES_NOESC (jsonwr, tmp, len);
	return 0;

incompatible_inst:
	flush_wbuf (jsonwr);
	hio_seterrbfmt (jsonwr->hio, HIO_EINVAL, "incompatiable integer write instruction");
	return -1;
}

int hio_jsonwr_writeuintmax (hio_jsonwr_t* jsonwr, hio_uintmax_t v)
{
	hio_jsonwr_state_node_t* sn = jsonwr->state_stack;
	hio_bch_t tmp[((HIO_SIZEOF_UINTMAX_T * HIO_BITS_PER_BYTE) / 3) + 2];
	hio_oow_t len;

	PREACTION_FOR_VALUE (jsonwr, sn);
	len = hio_fmt_uintmax_to_bcstr(tmp, HIO_COUNTOF(tmp), v, 10, 0, '\0', HIO_NULL);
	WRITE_BYTES_NOESC (jsonwr, tmp, len);
	return 0;

incompatible_inst:
	flush_wbuf (jsonwr);
	hio_seterrbfmt (jsonwr->hio, HIO_EINVAL, "incompatiable integer write instruction");
	return -1;
}

int hio_jsonwr_writerawuchars (hio_jsonwr_t* jsonwr, const hio_uch_t* dptr, hio_oow_t dlen)
{
	WRITE_UCHARS (jsonwr, 0, dptr, dlen);
	return 0;
}

int hio_jsonwr_writerawucstr (hio_jsonwr_t* jsonwr, const hio_uch_t* dptr)
{
	WRITE_UCHARS (jsonwr, 0, dptr, hio_count_ucstr(dptr));
	return 0;
}

int hio_jsonwr_writerawbchars (hio_jsonwr_t* jsonwr, const hio_bch_t* dptr, hio_oow_t dlen)
{
	WRITE_BYTES_NOESC (jsonwr, dptr, dlen);
	return 0;
}

int hio_jsonwr_writerawbcstr (hio_jsonwr_t* jsonwr, const hio_bch_t* dptr)
{
	WRITE_BYTES_NOESC (jsonwr, dptr, hio_count_bcstr(dptr));
	return 0;
}
