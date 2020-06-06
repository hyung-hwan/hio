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
    IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WAfRRANTIES
    OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
    IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
    INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
    NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
    THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#include <mio.h>
#include <mio-json.h>
#include <stdio.h>
#include <string.h>

static int on_json_inst (mio_json_t* json, mio_json_inst_t inst, mio_oow_t level, mio_oow_t index, mio_json_state_t container_state, const mio_oocs_t* str, void* ctx)
{
	mio_t* mio = mio_json_getmio(json);
	mio_oow_t i;


	switch (inst)
	{
		case MIO_JSON_INST_START_ARRAY:
			if (level > 0)
			{
				if (index > 0) mio_logbfmt (mio, MIO_LOG_STDOUT, ",\n");
				for (i = 0; i < level; i++) mio_logbfmt (mio, MIO_LOG_STDOUT, "\t"); 
			}
			mio_logbfmt (mio, MIO_LOG_STDOUT, "[\n"); 
			break;

		case MIO_JSON_INST_END_ARRAY:
			mio_logbfmt (mio, MIO_LOG_STDOUT, "\n"); 
			for (i = 0; i < level; i++) mio_logbfmt (mio, MIO_LOG_STDOUT, "\t"); 
			mio_logbfmt (mio, MIO_LOG_STDOUT, "]"); 
			break;

		case MIO_JSON_INST_START_DIC:
			if (level > 0)
			{
				if (index > 0) mio_logbfmt (mio, MIO_LOG_STDOUT, ",\n");
				for (i = 0; i < level; i++) mio_logbfmt (mio, MIO_LOG_STDOUT, "\t"); 
			}
			mio_logbfmt (mio, MIO_LOG_STDOUT, "{\n"); 
			break;

		case MIO_JSON_INST_END_DIC:
			mio_logbfmt (mio, MIO_LOG_STDOUT, "\n"); 
			for (i = 0; i < level; i++) mio_logbfmt (mio, MIO_LOG_STDOUT, "\t");
			mio_logbfmt (mio, MIO_LOG_STDOUT, "}"); 
			break;

		case MIO_JSON_INST_KEY:
			if (index > 0) mio_logbfmt (mio, MIO_LOG_STDOUT, ",\n");
			for (i = 0; i < level; i++) mio_logbfmt (mio, MIO_LOG_STDOUT, "\t"); 
			mio_logbfmt (mio, MIO_LOG_STDOUT, "%.*js: ", str->len, str->ptr);
			break;

		case MIO_JSON_INST_NIL:
			if (level > 0)
			{
				if (index > 0) mio_logbfmt (mio, MIO_LOG_STDOUT, ",\n");
				for (i = 0; i < level; i++) mio_logbfmt (mio, MIO_LOG_STDOUT, "\t"); 
			}
			mio_logbfmt (mio, MIO_LOG_STDOUT, "null");
			break;

		case MIO_JSON_INST_TRUE:
			if (level > 0)
			{
				if (index > 0) mio_logbfmt (mio, MIO_LOG_STDOUT, ",\n");
				for (i = 0; i < level; i++) mio_logbfmt (mio, MIO_LOG_STDOUT, "\t"); 
			}
			mio_logbfmt (mio, MIO_LOG_STDOUT, "true");
			break;

		case MIO_JSON_INST_FALSE:
			if (level > 0)
			{
				if (index > 0) mio_logbfmt (mio, MIO_LOG_STDOUT, ",\n");
				for (i = 0; i < level; i++) mio_logbfmt (mio, MIO_LOG_STDOUT, "\t"); 
			}
			mio_logbfmt (mio, MIO_LOG_STDOUT, "false");
			break;

		case MIO_JSON_INST_NUMBER:
			if (level > 0)
			{
				if (index > 0) mio_logbfmt (mio, MIO_LOG_STDOUT, ",\n");
				for (i = 0; i < level; i++) mio_logbfmt (mio, MIO_LOG_STDOUT, "\t"); 
			}
			mio_logbfmt (mio, MIO_LOG_STDOUT, "%.*js", str->len, str->ptr); 
			break;

		case MIO_JSON_INST_STRING:
			if (level > 0)
			{
				if (index > 0) mio_logbfmt (mio, MIO_LOG_STDOUT, ",\n");
				for (i = 0; i < level; i++) mio_logbfmt (mio, MIO_LOG_STDOUT, "\t"); 
			}
			mio_logbfmt (mio, MIO_LOG_STDOUT, "\"%.*js\"", str->len, str->ptr); /* TODO: escaping */
			break;

		default:
			mio_logbfmt (mio, MIO_LOG_STDOUT, "*****UNKNOWN*****\n", str->len, str->ptr); 
			return -1;
	}
	
	return 0;
}

static int write_json_element (mio_jsonwr_t* jsonwr, const mio_bch_t* dptr, mio_oow_t dlen, void* ctx)
{
	write (1, dptr, dlen);
	return 0;
}

int main (int argc, char* argv[])
{
	mio_t* mio = MIO_NULL;

	mio = mio_open(MIO_NULL, 0, MIO_NULL, 512, MIO_NULL);
	if (!mio)
	{
		printf ("Cannot open mio\n");
		return -1;
	}

	{
		mio_json_t* json = MIO_NULL;
		char buf[128];
		mio_oow_t rem;

		json = mio_json_open(mio, 0);

		mio_json_setinstcb (json, on_json_inst, MIO_NULL);


		rem = 0;
		while (1)
		{
			int x;
			size_t size = fread(&buf[rem], 1, sizeof(buf) - rem, stdin);
			if (size <= 0) break;


			if ((x = mio_json_feed(json, buf, size + rem, &rem, 1)) <= -1) 
			{
				printf ("**** ERROR ****\n");
				break;
			}

			//printf ("--> x %d input %d left-over %d\n", (int)x, (int)size, (int)rem);
			if  (rem > 0) memcpy (buf, &buf[size - rem], rem);
		}

mio_logbfmt (mio, MIO_LOG_STDOUT, "\n");
		mio_json_close (json);
	}


	{
		mio_jsonwr_t* jsonwr = MIO_NULL;
		mio_uch_t ddd[4] = { 'D', '\0', 'R', 'Q' };
		mio_uch_t ddv[5] = { L'밝', L'혀', L'졌', L'는', L'데' };
	
		jsonwr = mio_jsonwr_open (mio, 0);

		mio_jsonwr_setwritecb (jsonwr, write_json_element, MIO_NULL);

		mio_jsonwr_startarray (jsonwr);
		
			mio_jsonwr_writestringwithbchars (jsonwr, "hello", 5);
			mio_jsonwr_writestringwithbchars (jsonwr, "world", 5);

			mio_jsonwr_startdic (jsonwr);
				mio_jsonwr_writekeywithbchars (jsonwr, "abc", 3);
				mio_jsonwr_writestringwithbchars (jsonwr, "computer", 8);
				mio_jsonwr_writekeywithbchars (jsonwr, "k", 1);
				mio_jsonwr_writestringwithbchars (jsonwr, "play nice", 9);
				mio_jsonwr_writekeywithuchars (jsonwr, ddd, 4);
				mio_jsonwr_writestringwithuchars (jsonwr, ddv, 5);
			mio_jsonwr_enddic (jsonwr);

			mio_jsonwr_writestringwithbchars (jsonwr, "tyler", 5);

			mio_jsonwr_startarray (jsonwr);
				mio_jsonwr_writestringwithbchars (jsonwr, "airplain", 8);
				mio_jsonwr_writestringwithbchars (jsonwr, "gro\0wn\nup", 9);
				mio_jsonwr_writetrue (jsonwr);
			mio_jsonwr_endarray (jsonwr);

		mio_jsonwr_endarray (jsonwr);

		mio_jsonwr_close (jsonwr);
	}

	mio_close (mio);

	return 0;

}

