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
#include "mio-prv.h"

#include <string.h>
#include <errno.h>

#define MIO_JSON_TOKEN_NAME_ALIGN 64

/* ========================================================================= */

static void clear_token (mio_json_t* json)
{
	json->tok.len = 0;
	if (json->tok_capa > 0) json->tok.ptr[json->tok.len] = '\0';
}

static int add_char_to_token (mio_json_t* json, mio_ooch_t ch)
{
	if (json->tok.len >= json->tok_capa)
	{
		mio_ooch_t* tmp;
		mio_oow_t newcapa;

		newcapa = MIO_ALIGN_POW2(json->tok.len + 2, MIO_JSON_TOKEN_NAME_ALIGN);  /* +2 here because of -1 when setting newcapa */
		tmp = (mio_ooch_t*)mio_reallocmem(json->mio, json->tok.ptr, newcapa * MIO_SIZEOF(*tmp));
		if (!tmp) return -1;

		json->tok_capa = newcapa - 1; /* -1 to secure space for terminating null */
		json->tok.ptr = tmp;
	}

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
		if (!tmp) return -1;

		json->tok_capa = newcapa - 1;
		json->tok.ptr = tmp;
	}

	for (i = 0; i < len; i++)  
		json->tok.ptr[json->tok.len++] = ptr[i];
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

static int push_state (mio_json_t* json, mio_json_state_t state)
{
	mio_json_state_node_t* ss;

	ss = (mio_json_state_node_t*)mio_callocmem(json->mio, MIO_SIZEOF(*ss));
	if (MIO_UNLIKELY(!ss)) return -1;

	ss->state = state;
	ss->level = json->state_stack->level; /* copy from the parent */
	ss->next = json->state_stack;
	
	json->state_stack = ss;
	return 0;
}

static void pop_state (mio_json_t* json)
{
	mio_json_state_node_t* ss;

	ss = json->state_stack;
	MIO_ASSERT (json->mio, ss != MIO_NULL && ss != &json->state_top);
	json->state_stack = ss->next;

	if (json->state_stack->state == MIO_JSON_STATE_IN_ARRAY)
	{
		json->state_stack->u.ia.got_value = 1;
	}
	else if (json->state_stack->state == MIO_JSON_STATE_IN_DIC)
	{
		json->state_stack->u.id.state++;
	}

/* TODO: don't free this. move it to the free list? */
	mio_freemem (json->mio, ss);
}

static void pop_all_states (mio_json_t* json)
{
	while (json->state_stack != &json->state_top) pop_state (json);
}

/* ========================================================================= */

static int invoke_data_inst (mio_json_t* json, mio_json_inst_t inst)
{
	if (json->state_stack->state == MIO_JSON_STATE_IN_DIC && json->state_stack->u.id.state == 1) 
	{
		if (inst != MIO_JSON_INST_STRING)
		{
			mio_seterrbfmt (json->mio, MIO_EINVAL, "dictionary key not a string - %.*js", json->tok.len, json->tok.ptr);
			return -1;
		}

		inst = MIO_JSON_INST_KEY;
	}

	return json->instcb(json, inst, json->state_stack->level, &json->tok);
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
			if (add_char_to_token(json, json->state_stack->u.sv.acc) <= -1) return -1;
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
			/* TOOD: handle UTF-16 surrogate pair  U+1D11E ->  \uD834\uDD1E*/
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
			if (add_char_to_token(json, unescape(c)) <= -1) return -1;
		}
	}
	else if (c == '\\')
	{
		json->state_stack->u.sv.escaped = 1;
	}
	else if (c == '\"')
	{
		pop_state (json);
		if (invoke_data_inst(json, MIO_JSON_INST_STRING) <= -1) return -1;
	}
	else
	{
		if (add_char_to_token(json, c) <= -1) return -1;
	}

	return ret;
}

static int handle_character_value_char (mio_json_t* json, mio_ooci_t c)
{
	/* The real JSON dones't support character literal. this is MIO's own extension. */
	int ret = 1;

	if (json->state_stack->u.cv.escaped == 3)
	{
		if (c >= '0' && c <= '7')
		{
			json->state_stack->u.cv.acc = json->state_stack->u.cv.acc * 8 + c - '0';
			json->state_stack->u.cv.digit_count++;
			if (json->state_stack->u.cv.digit_count >= json->state_stack->u.cv.escaped) goto add_cv_acc;
		}
		else
		{
			ret = 0;
			goto add_cv_acc;
		}
	}
	if (json->state_stack->u.cv.escaped >= 2)
	{
		if (c >= '0' && c <= '9')
		{
			json->state_stack->u.cv.acc = json->state_stack->u.cv.acc * 16 + c - '0';
			json->state_stack->u.cv.digit_count++;
			if (json->state_stack->u.cv.digit_count >= json->state_stack->u.cv.escaped) goto add_cv_acc;
		}
		else if (c >= 'a' && c <= 'f')
		{
			json->state_stack->u.cv.acc = json->state_stack->u.cv.acc * 16 + c - 'a' + 10;
			json->state_stack->u.cv.digit_count++;
			if (json->state_stack->u.cv.digit_count >= json->state_stack->u.cv.escaped) goto add_cv_acc;
		}
		else if (c >= 'A' && c <= 'F')
		{
			json->state_stack->u.cv.acc = json->state_stack->u.cv.acc * 16 + c - 'A' + 10;
			json->state_stack->u.cv.digit_count++;
			if (json->state_stack->u.cv.digit_count >= json->state_stack->u.cv.escaped) goto add_cv_acc;
		}
		else
		{
			ret = 0;
		add_cv_acc:
			if (add_char_to_token(json, json->state_stack->u.cv.acc) <= -1) return -1;
			json->state_stack->u.cv.escaped = 0;
		}
	}
	else if (json->state_stack->u.cv.escaped == 1)
	{
		if (c >= '0' && c <= '8') 
		{
			json->state_stack->u.cv.escaped = 3;
			json->state_stack->u.cv.digit_count = 0;
			json->state_stack->u.cv.acc = c - '0';
		}
		else if (c == 'x')
		{
			json->state_stack->u.cv.escaped = 2;
			json->state_stack->u.cv.digit_count = 0;
			json->state_stack->u.cv.acc = 0;
		}
		else if (c == 'u')
		{
			json->state_stack->u.cv.escaped = 4;
			json->state_stack->u.cv.digit_count = 0;
			json->state_stack->u.cv.acc = 0;
		}
		else if (c == 'U')
		{
			json->state_stack->u.cv.escaped = 8;
			json->state_stack->u.cv.digit_count = 0;
			json->state_stack->u.cv.acc = 0;
		}
		else
		{
			json->state_stack->u.cv.escaped = 0;
			if (add_char_to_token(json, unescape(c)) <= -1) return -1;
		}
	}
	else if (c == '\\')
	{
		json->state_stack->u.cv.escaped = 1;
	}
	else if (c == '\'')
	{
		pop_state (json);
		
		if (json->tok.len < 1)
		{
			mio_seterrbfmt (json->mio, MIO_EINVAL, "no character in a character literal");
			return -1;
		}
		if (invoke_data_inst(json, MIO_JSON_INST_CHARACTER) <= -1) return -1;
	}
	else
	{
		if (add_char_to_token(json, c) <= -1) return -1;
	}

	if (json->tok.len > 1) 
	{
		mio_seterrbfmt (json->mio, MIO_EINVAL, "too many characters in a character literal - %.*js", json->tok.len, json->tok.ptr);
		return -1;
	}

	return ret;
}

static int handle_numeric_value_char (mio_json_t* json, mio_ooci_t c)
{
	if (mio_is_ooch_digit(c) || (json->tok.len == 0 && (c == '+' || c == '-')))
	{
		if (add_char_to_token(json, c) <= -1) return -1;
		return 1;
	}
	else if (!json->state_stack->u.nv.dotted && c == '.' &&
	         json->tok.len > 0 && mio_is_ooch_digit(json->tok.ptr[json->tok.len - 1]))
	{
		if (add_char_to_token(json, c) <= -1) return -1;
		json->state_stack->u.nv.dotted = 1;
		return 1;
	}

	pop_state (json);

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

	if (mio_is_ooch_alpha(c))
	{
		if (add_char_to_token(json, c) <= -1) return -1;
		return 1;
	}

	pop_state (json);

	if (mio_comp_oochars_bcstr(json->tok.ptr, json->tok.len, "null", 0) == 0) inst = MIO_JSON_INST_NIL;
	else if (mio_comp_oochars_bcstr(json->tok.ptr, json->tok.len, "true", 0) == 0) inst = MIO_JSON_INST_TRUE;
	else if (mio_comp_oochars_bcstr(json->tok.ptr, json->tok.len, "false", 0) == 0) inst = MIO_JSON_INST_FALSE;
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
printf ("HANDLE START CHAR [%c]\n", c);
	if (c == '[')
	{
		if (push_state(json, MIO_JSON_STATE_IN_ARRAY) <= -1) return -1;
		json->state_stack->u.ia.got_value = 0;
		if (json->instcb(json, MIO_JSON_INST_START_ARRAY, json->state_stack->level, MIO_NULL) <= -1) return -1;
		json->state_stack->level++;
		return 1;
	}
	else if (c == '{')
	{
		if (push_state(json, MIO_JSON_STATE_IN_DIC) <= -1) return -1;
		json->state_stack->u.id.state = 0;
		if (json->instcb(json, MIO_JSON_INST_START_DIC, json->state_stack->level, MIO_NULL) <= -1) return -1;
		json->state_stack->level++;
		return 1;
	}
#if 0
/* this check is not needed for screening in feed_json_data() */
	else if (mio_is_ooch_space(c)) 
	{
		/* do nothing */
		return 1;
	}
#endif
	else
	{
		mio_seterrbfmt (json->mio, MIO_EINVAL, "not starting with [ or { - %jc", (mio_ooch_t)c);
		return -1;
	}
}

static int handle_char_in_array (mio_json_t* json, mio_ooci_t c)
{
	if (c == ']')
	{
		if (json->instcb(json, MIO_JSON_INST_END_ARRAY, json->state_stack->level - 1, MIO_NULL) <= -1) return -1;
		pop_state (json);
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
	else if (mio_is_ooch_space(c))
	{
		/* do nothing */
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
			if (push_state(json, MIO_JSON_STATE_IN_STRING_VALUE) <= -1) return -1;
			clear_token (json);
			return 1;
		}
		else if (c == '\'')
		{
			if (push_state(json, MIO_JSON_STATE_IN_CHARACTER_VALUE) <= -1) return -1;
			clear_token (json);
			return 1;
		}
		/* TOOD: else if (c == '#') MIO radixed number
		 */
		else if (mio_is_ooch_digit(c) || c == '+' || c == '-')
		{
			if (push_state(json, MIO_JSON_STATE_IN_NUMERIC_VALUE) <= -1) return -1;
			clear_token (json);
			json->state_stack->u.nv.dotted = 0;
			return 0; /* start over */
		}
		else if (mio_is_ooch_alpha(c))
		{
			if (push_state(json, MIO_JSON_STATE_IN_WORD_VALUE) <= -1) return -1;
			clear_token (json);
			return 0; /* start over */
		}
		else if (c == '[')
		{
			if (push_state(json, MIO_JSON_STATE_IN_ARRAY) <= -1) return -1;
			json->state_stack->u.ia.got_value = 0;
			if (json->instcb(json, MIO_JSON_INST_START_ARRAY, json->state_stack->level, MIO_NULL) <= -1) return -1;
			json->state_stack->level++;
			return 1;
		}
		else if (c == '{')
		{
			if (push_state(json, MIO_JSON_STATE_IN_DIC) <= -1) return -1;
			json->state_stack->u.id.state = 0;
			if (json->instcb(json, MIO_JSON_INST_START_DIC, json->state_stack->level, MIO_NULL) <= -1) return -1;
			json->state_stack->level++;
			return 1;
		}
		else
		{
			mio_seterrbfmt (json->mio, MIO_EINVAL, "wrong character inside array - %jc[%d]", (mio_ooch_t)c, (int)c);
			return -1;
		}
	}
}

static int handle_char_in_dic (mio_json_t* json, mio_ooci_t c)
{
	if (c == '}')
	{
		if (json->instcb(json, MIO_JSON_INST_END_DIC, json->state_stack->level - 1, MIO_NULL) <= -1) return -1;
		pop_state (json);
		return 1;
	}
	else if (c == ':')
	{
		if (json->state_stack->u.id.state != 1)
		{
			mio_seterrbfmt (json->mio, MIO_EINVAL, "redundant colon in dictionary - %jc", (mio_ooch_t)c);
			return -1;
		}
		json->state_stack->u.id.state++;
		return 1;
	}
	else if (c == ',')
	{
		if (json->state_stack->u.id.state != 3)
		{
			mio_seterrbfmt (json->mio, MIO_EINVAL, "redundant comma in dicitonary - %jc", (mio_ooch_t)c);
			return -1;
		}
		json->state_stack->u.id.state = 0;
		return 1;
	}
	else if (mio_is_ooch_space(c))
	{
		/* do nothing */
		return 1;
	}
	else
	{
		if (json->state_stack->u.id.state == 1)
		{
			mio_seterrbfmt (json->mio, MIO_EINVAL, "colon required in dicitonary - %jc", (mio_ooch_t)c);
			return -1;
		}
		else if (json->state_stack->u.id.state == 3)
		{
			mio_seterrbfmt (json->mio, MIO_EINVAL, "comma required in dicitonary - %jc", (mio_ooch_t)c);
			return -1;
		}

		if (c == '\"')
		{
			if (push_state(json, MIO_JSON_STATE_IN_STRING_VALUE) <= -1) return -1;
			clear_token (json);
			return 1;
		}
		else if (c == '\'')
		{
			if (push_state(json, MIO_JSON_STATE_IN_CHARACTER_VALUE) <= -1) return -1;
			clear_token (json);
			return 1;
		}
		/* TOOD: else if (c == '#') MIO radixed number
		 */
		else if (mio_is_ooch_digit(c) || c == '+' || c == '-')
		{
			if (push_state(json, MIO_JSON_STATE_IN_NUMERIC_VALUE) <= -1) return -1;
			clear_token (json);
			json->state_stack->u.nv.dotted = 0;
			return 0; /* start over */
		}
		else if (mio_is_ooch_alpha(c))
		{
			if (push_state(json, MIO_JSON_STATE_IN_WORD_VALUE) <= -1) return -1;
			clear_token (json);
			return 0; /* start over */
		}
		else if (c == '[')
		{
			if (push_state(json, MIO_JSON_STATE_IN_ARRAY) <= -1) return -1;
			json->state_stack->u.ia.got_value = 0;
			json->state_stack->level++;
			if (json->instcb(json, MIO_JSON_INST_START_ARRAY, json->state_stack->level, MIO_NULL) <= -1) return -1;
			return 1;
		}
		else if (c == '{')
		{
			if (push_state(json, MIO_JSON_STATE_IN_DIC) <= -1) return -1;
			json->state_stack->u.id.state = 0;
			json->state_stack->level++;
			if (json->instcb(json, MIO_JSON_INST_START_DIC, json->state_stack->level, MIO_NULL) <= -1) return -1;
			return 1;
		}
		else
		{
			mio_seterrbfmt (json->mio, MIO_EINVAL, "wrong character inside dictionary - %jc[%d]", (mio_ooch_t)c, (int)c);
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

		case MIO_JSON_STATE_IN_DIC:
			x = handle_char_in_dic(json, c);
			break;

		case MIO_JSON_STATE_IN_WORD_VALUE:
			x = handle_word_value_char(json, c);
			break;

		case MIO_JSON_STATE_IN_STRING_VALUE:
			x = handle_string_value_char(json, c);
			break;
			
		case MIO_JSON_STATE_IN_CHARACTER_VALUE:
			x = handle_character_value_char(json, c);
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

static int do_nothing_on_inst  (mio_json_t* json, mio_json_inst_t inst, mio_oow_t level, const mio_oocs_t* str)
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
	pop_all_states (json);
	if (json->tok.ptr) 
	{
		mio_freemem (json->mio, json->tok.ptr);
		json->tok.ptr = MIO_NULL;
	}
}
/* ========================================================================= */

void mio_json_setinstcb (mio_json_t* json, mio_json_instcb_t instcb)
{
	json->instcb = instcb;
}

mio_json_state_t mio_json_getstate (mio_json_t* json)
{
	return json->state_stack->state;
}

void mio_json_resetstates (mio_json_t* json)
{
	pop_all_states (json);
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
