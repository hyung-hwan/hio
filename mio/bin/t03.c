
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

		case MIO_JSON_INST_START_OBJECT:
			if (level > 0)
			{
				if (index > 0) mio_logbfmt (mio, MIO_LOG_STDOUT, ",\n");
				for (i = 0; i < level; i++) mio_logbfmt (mio, MIO_LOG_STDOUT, "\t"); 
			}
			mio_logbfmt (mio, MIO_LOG_STDOUT, "{\n"); 
			break;

		case MIO_JSON_INST_END_OBJECT:
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
	fwrite (dptr, 1, dlen, stdout);
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
		size_t size;

		json = mio_json_open(mio, 0);

		mio_json_setinstcb (json, on_json_inst, MIO_NULL);

		rem = 0;
		while (!feof(stdin) || rem > 0)
		{
			int x;

			if (!feof(stdin))
			{
				size = fread(&buf[rem], 1, sizeof(buf) - rem, stdin);
				if (size <= 0) break;
			}
			else
			{
				size = rem;
				rem = 0;
			}

			if ((x = mio_json_feed(json, buf, size + rem, &rem, 1)) <= -1) 
			{
				mio_logbfmt (mio, MIO_LOG_STDOUT, "**** ERROR - %js ****\n", mio_geterrmsg(mio));
				break;
			}

			if (x > 0)
			{
				/* document completed */
				mio_logbfmt (mio, MIO_LOG_STDOUT, "\n-----------------------------------\n");
			}

			/*printf ("--> x %d input %d left-over %d => [%.*s]\n", (int)x, (int)size, (int)rem, (int)rem, &buf[size - rem]);*/
			if  (rem > 0) memcpy (buf, &buf[size - rem], rem);
		}

mio_logbfmt (mio, MIO_LOG_STDOUT, "\n");
		mio_json_close (json);
	}


	{
		mio_jsonwr_t* jsonwr = MIO_NULL;
		mio_uch_t ddd[4] = { 'D', '\0', 'R', 'Q' };
		mio_uch_t ddv[5] = { L'밝', L'혀', L'졌', L'는', L'데' };
	
		jsonwr = mio_jsonwr_open (mio, 0, MIO_JSONWR_FLAG_PRETTY);

		mio_jsonwr_setwritecb (jsonwr, write_json_element, MIO_NULL);

		mio_jsonwr_startarray (jsonwr);
		
			mio_jsonwr_writestringwithbchars (jsonwr, "hello", 5);
			mio_jsonwr_writestringwithbchars (jsonwr, "world", 5);

			mio_jsonwr_startobject (jsonwr);
				mio_jsonwr_writekeywithbchars (jsonwr, "abc", 3);
				mio_jsonwr_writestringwithbchars (jsonwr, "computer", 8);
				mio_jsonwr_writekeywithbchars (jsonwr, "k", 1);
				mio_jsonwr_writestringwithbchars (jsonwr, "play nice", 9);
				mio_jsonwr_writekeywithuchars (jsonwr, ddd, 4);
				mio_jsonwr_writestringwithuchars (jsonwr, ddv, 5);
			mio_jsonwr_endobject (jsonwr);

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

