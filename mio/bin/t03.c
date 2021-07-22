
#include <hio.h>
#include <hio-json.h>
#include <stdio.h>
#include <string.h>

#define DEBUG

static int on_json_inst (hio_json_t* json, hio_json_inst_t inst, hio_oow_t level, hio_oow_t index, hio_json_state_t container_state, const hio_oocs_t* str, void* ctx)
{
	hio_t* hio = hio_json_gethio(json);
	hio_oow_t i;

	switch (inst)
	{
		case HIO_JSON_INST_START_ARRAY:
			if (level > 0)
			{
				if (index > 0) hio_logbfmt (hio, HIO_LOG_STDOUT, ",\n");
				for (i = 0; i < level; i++) hio_logbfmt (hio, HIO_LOG_STDOUT, "\t"); 
			}
		#if defined(DEBUG)
			hio_logbfmt (hio, HIO_LOG_STDOUT, "<%lu>[\n", (unsigned long)index); 
		#else
			hio_logbfmt (hio, HIO_LOG_STDOUT, "[\n"); 
		#endif
			break;

		case HIO_JSON_INST_END_ARRAY:
			hio_logbfmt (hio, HIO_LOG_STDOUT, "\n"); 
			for (i = 0; i < level; i++) hio_logbfmt (hio, HIO_LOG_STDOUT, "\t"); 
		#if defined(DEBUG)
			hio_logbfmt (hio, HIO_LOG_STDOUT, "<%lu>]", (unsigned long)index); 
		#else
			hio_logbfmt (hio, HIO_LOG_STDOUT, "]"); 
		#endif
			break;

		case HIO_JSON_INST_START_OBJECT:
			if (level > 0)
			{
				if (index > 0) hio_logbfmt (hio, HIO_LOG_STDOUT, ",\n");
				for (i = 0; i < level; i++) hio_logbfmt (hio, HIO_LOG_STDOUT, "\t"); 
			}
		#if defined(DEBUG)
			hio_logbfmt (hio, HIO_LOG_STDOUT, "<%lu>{\n", (unsigned long)index); 
		#else
			hio_logbfmt (hio, HIO_LOG_STDOUT, "{\n"); 
		#endif
			break;

		case HIO_JSON_INST_END_OBJECT:
			hio_logbfmt (hio, HIO_LOG_STDOUT, "\n"); 
			for (i = 0; i < level; i++) hio_logbfmt (hio, HIO_LOG_STDOUT, "\t");
		#if defined(DEBUG)
			hio_logbfmt (hio, HIO_LOG_STDOUT, "<%lu>}", (unsigned long)index); 
		#else
			hio_logbfmt (hio, HIO_LOG_STDOUT, "}"); 
		#endif
			break;

		case HIO_JSON_INST_KEY:
			if (index > 0) hio_logbfmt (hio, HIO_LOG_STDOUT, ",\n");
			for (i = 0; i < level; i++) hio_logbfmt (hio, HIO_LOG_STDOUT, "\t"); 
		#if defined(DEBUG)
			hio_logbfmt (hio, HIO_LOG_STDOUT, "<%lu>%.*js: ", (unsigned long)index, str->len, str->ptr);
		#else
			hio_logbfmt (hio, HIO_LOG_STDOUT, "%.*js: ", str->len, str->ptr);
		#endif
			break;

		case HIO_JSON_INST_NIL:
			if (level > 0)
			{
				if (index > 0) hio_logbfmt (hio, HIO_LOG_STDOUT, ",\n");
				for (i = 0; i < level; i++) hio_logbfmt (hio, HIO_LOG_STDOUT, "\t"); 
			}
		#if defined(DEBUG)
			hio_logbfmt (hio, HIO_LOG_STDOUT, "<%lu>null", (unsigned long)index);
		#else
			hio_logbfmt (hio, HIO_LOG_STDOUT, "null");
		#endif
			break;

		case HIO_JSON_INST_TRUE:
			if (level > 0)
			{
				if (index > 0) hio_logbfmt (hio, HIO_LOG_STDOUT, ",\n");
				for (i = 0; i < level; i++) hio_logbfmt (hio, HIO_LOG_STDOUT, "\t"); 
			}
		#if defined(DEBUG)
			hio_logbfmt (hio, HIO_LOG_STDOUT, "<%lu>true", (unsigned long)index);
		#else
			hio_logbfmt (hio, HIO_LOG_STDOUT, "true");
		#endif
			break;

		case HIO_JSON_INST_FALSE:
			if (level > 0)
			{
				if (index > 0) hio_logbfmt (hio, HIO_LOG_STDOUT, ",\n");
				for (i = 0; i < level; i++) hio_logbfmt (hio, HIO_LOG_STDOUT, "\t"); 
			}
		#if defined(DEBUG)
			hio_logbfmt (hio, HIO_LOG_STDOUT, "<%lu>false", (unsigned long)index);
		#else
			hio_logbfmt (hio, HIO_LOG_STDOUT, "false");
		#endif
			break;

		case HIO_JSON_INST_NUMBER:
			if (level > 0)
			{
				if (index > 0) hio_logbfmt (hio, HIO_LOG_STDOUT, ",\n");
				for (i = 0; i < level; i++) hio_logbfmt (hio, HIO_LOG_STDOUT, "\t"); 
			}
		#if defined(DEBUG)
			hio_logbfmt (hio, HIO_LOG_STDOUT, "<%lu>%.*js", (unsigned long)index, str->len, str->ptr); 
		#else
			hio_logbfmt (hio, HIO_LOG_STDOUT, "%.*js", str->len, str->ptr); 
		#endif
			break;

		case HIO_JSON_INST_STRING:
			if (level > 0)
			{
				if (index > 0) hio_logbfmt (hio, HIO_LOG_STDOUT, ",\n");
				for (i = 0; i < level; i++) hio_logbfmt (hio, HIO_LOG_STDOUT, "\t"); 
			}
		#if defined(DEBUG)
			hio_logbfmt (hio, HIO_LOG_STDOUT, "<%lu>\"%.*js\"", (unsigned long)index, str->len, str->ptr); /* TODO: escaping */
		#else
			hio_logbfmt (hio, HIO_LOG_STDOUT, "\"%.*js\"", str->len, str->ptr); /* TODO: escaping */
		#endif
			break;

		default:
			hio_logbfmt (hio, HIO_LOG_STDOUT, "*****UNKNOWN*****\n", str->len, str->ptr); 
			return -1;
	}
	
	return 0;
}

static int write_json_element (hio_jsonwr_t* jsonwr, const hio_bch_t* dptr, hio_oow_t dlen, void* ctx)
{
	fwrite (dptr, 1, dlen, stdout);
	return 0;
}

int main (int argc, char* argv[])
{
	hio_t* hio = HIO_NULL;
	hio_bitmask_t o = 0;
	int i;

	for (i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "--permit-word-key") == 0) o |= HIO_JSON_PERMIT_WORD_KEY;
		if (strcmp(argv[i], "--optional-comma") == 0) o |= HIO_JSON_OPTIONAL_COMMA;
		if (strcmp(argv[i], "--line-comment") == 0) o |= HIO_JSON_LINE_COMMENT;
	}

	hio = hio_open(HIO_NULL, 0, HIO_NULL, HIO_FEATURE_ALL, 512, HIO_NULL);
	if (!hio)
	{
		printf ("Cannot open hio\n");
		return -1;
	}

	{
		hio_json_t* json = HIO_NULL;
		char buf[128];
		hio_oow_t rem;
		size_t size;

		json = hio_json_open(hio, 0);

		hio_json_setoption (json, o);
		hio_json_setinstcb (json, on_json_inst, HIO_NULL);

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

			if ((x = hio_json_feed(json, buf, size + rem, &rem, 1)) <= -1) 
			{
				hio_logbfmt (hio, HIO_LOG_STDOUT, "**** ERROR - %js ****\n", hio_geterrmsg(hio));
				goto done;
			}

			if (x > 0)
			{
				/* document completed.
				 * if only whitespaces are given, x is still greater 0. */
				hio_logbfmt (hio, HIO_LOG_STDOUT, "\n-----------------------------------\n");
			}

			/*printf ("--> x %d input %d left-over %d => [%.*s]\n", (int)x, (int)size, (int)rem, (int)rem, &buf[size - rem]);*/
			if  (rem > 0) memcpy (buf, &buf[size - rem], rem);
		}

hio_logbfmt (hio, HIO_LOG_STDOUT, "\n");
		if (json->state_stack != &json->state_top) hio_logbfmt (hio, HIO_LOG_STDOUT, "**** ERROR - incomplete ****\n");

	done:
		hio_json_close (json);
	}

	hio_logbfmt (hio, HIO_LOG_STDOUT, "\n===================================\n");

	{
		hio_jsonwr_t* jsonwr = HIO_NULL;
		hio_uch_t ddd[4] = { 'D', '\0', 'R', 'Q' };
		hio_uch_t ddv[5] = { L'밝', L'혀', L'졌', L'는', L'데' };
	
		jsonwr = hio_jsonwr_open (hio, 0, HIO_JSONWR_FLAG_PRETTY);

		hio_jsonwr_setwritecb (jsonwr, write_json_element, HIO_NULL);

		hio_jsonwr_startarray (jsonwr);
		
			hio_jsonwr_writestringwithbchars (jsonwr, "hello", 5);
			hio_jsonwr_writestringwithbchars (jsonwr, "world", 5);

			hio_jsonwr_startobject (jsonwr);
				hio_jsonwr_writekeywithbchars (jsonwr, "abc", 3);
				hio_jsonwr_writestringwithbchars (jsonwr, "computer", 8);
				hio_jsonwr_writekeywithbchars (jsonwr, "k", 1);
				hio_jsonwr_writestringwithbchars (jsonwr, "play nice", 9);
				hio_jsonwr_writekeywithuchars (jsonwr, ddd, 4);
				hio_jsonwr_writestringwithuchars (jsonwr, ddv, 5);
			hio_jsonwr_endobject (jsonwr);

			hio_jsonwr_writestringwithbchars (jsonwr, "tyler", 5);

			hio_jsonwr_startarray (jsonwr);
				hio_jsonwr_writestringwithbchars (jsonwr, "airplain", 8);
				hio_jsonwr_writestringwithbchars (jsonwr, "gro\0wn\nup", 9);
				hio_jsonwr_writetrue (jsonwr);
			hio_jsonwr_endarray (jsonwr);

		hio_jsonwr_endarray (jsonwr);

		hio_jsonwr_close (jsonwr);
	}

	hio_close (hio);

	return 0;

}

