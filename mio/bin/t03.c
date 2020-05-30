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


static int on_json_inst (mio_json_t* json, mio_json_inst_t inst,	mio_oow_t level, const mio_oocs_t* str)
{
	mio_t* mio = mio_json_getmio(json);
	mio_oow_t i;

	switch (inst)
	{
		case MIO_JSON_INST_START_ARRAY:
			/* for (i = 0; i < level; i++) mio_logbfmt (mio, MIO_LOG_STDOUT, "\t"); */
			mio_logbfmt (mio, MIO_LOG_STDOUT, "[\n"); 
			break;

		case MIO_JSON_INST_END_ARRAY:
			for (i = 0; i < level; i++) mio_logbfmt (mio, MIO_LOG_STDOUT, "\t"); 
			mio_logbfmt (mio, MIO_LOG_STDOUT, "]\n"); 
			break;

		case MIO_JSON_INST_START_DIC:
			/*for (i = 0; i < level; i++) mio_logbfmt (mio, MIO_LOG_STDOUT, "\t"); */
			mio_logbfmt (mio, MIO_LOG_STDOUT, "{\n"); 
			break;

		case MIO_JSON_INST_END_DIC:
			for (i = 0; i < level; i++) mio_logbfmt (mio, MIO_LOG_STDOUT, "\t");
			mio_logbfmt (mio, MIO_LOG_STDOUT, "}\n"); 
			break;

		case MIO_JSON_INST_KEY:
			for (i = 0; i < level; i++) mio_logbfmt (mio, MIO_LOG_STDOUT, "\t");
			mio_logbfmt (mio, MIO_LOG_STDOUT, "%.*js: ", str->len, str->ptr);
			break;

		case MIO_JSON_INST_NIL:
			mio_logbfmt (mio, MIO_LOG_STDOUT, "null\n");
			break;

		case MIO_JSON_INST_TRUE:
			mio_logbfmt (mio, MIO_LOG_STDOUT, "true\n");
			break;

		case MIO_JSON_INST_FALSE:
			mio_logbfmt (mio, MIO_LOG_STDOUT, "false\n");
			break;

		case MIO_JSON_INST_NUMBER:
		case MIO_JSON_INST_CHARACTER:
			mio_logbfmt (mio, MIO_LOG_STDOUT, "%.*js\n", str->len, str->ptr); 
			break;

		case MIO_JSON_INST_STRING:
			mio_logbfmt (mio, MIO_LOG_STDOUT, "\"%.*js\"\n", str->len, str->ptr); /* TODO: escaping */
			break;

		default:
			mio_logbfmt (mio, MIO_LOG_STDOUT, "*****UNKNOWN*****\n", str->len, str->ptr); 
			return -1;
	}
	
	return 0;
}

int main (int argc, char* argv[])
{
	mio_t* mio = MIO_NULL;
	mio_json_t* json = MIO_NULL;
	char buf[128];
	mio_oow_t rem;


	mio = mio_open(MIO_NULL, 0, MIO_NULL, 512, MIO_NULL);
	if (!mio)
	{
		printf ("Cannot open mio\n");
		return -1;
	}

	json = mio_json_open(mio, 0);

	mio_json_setinstcb (json, on_json_inst);


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

	mio_json_close (json);
	mio_close (mio);

	return 0;

}

