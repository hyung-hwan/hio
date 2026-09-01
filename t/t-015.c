/*
 * the json reader and writer.
 *
 * this is what was worth keeping from bin/t03.c, which fed a document in and
 * printed the parse for a human to read. the parser is pure - no sockets, no
 * privilege, no timing - so it is the one part of the library that can be
 * tested exactly: feed a document, record the instructions it produces, and
 * compare against what the grammar says they should be.
 *
 * the instruction stream is recorded as a compact string, one letter per
 * instruction, so an expectation reads as a single literal instead of a page of
 * per-callback assertions:
 *
 *   [ ] { }   array and object boundaries      k   an object key
 *   s n       string and number values         t f -   true, false, null
 */

#include <hio-json.h>
#include <hio-prv.h>
#include "tap.h"

#include <string.h>
#include <stdio.h>

static hio_t* g_hio = HIO_NULL;

/* the recorded instruction stream, plus the text of every value seen, so a
 * test can check what was parsed and not merely its shape */
static hio_bch_t g_insts[256];
static hio_oow_t g_ninsts;
static hio_bch_t g_values[512];
static hio_oow_t g_nvalues;
static hio_oow_t g_maxlevel;

static void quiet_logging (hio_t* hio)
{
	hio_bitmask_t mask = HIO_LOG_FATAL | HIO_LOG_ALL_TYPES;
	hio_setoption (hio, HIO_LOG_MASK, &mask);
}

static void reset_record (void)
{
	g_ninsts = g_nvalues = g_maxlevel = 0;
	g_insts[0] = '\0';
	g_values[0] = '\0';
}

static void record_value (const hio_oocs_t* str)
{
	hio_oow_t i;
	if (!str) return;
	/* '|' separates values so a test can tell "ab","c" from "a","bc" */
	if (g_nvalues + str->len + 2 >= HIO_SIZEOF(g_values)) return;
	for (i = 0; i < str->len; i++) g_values[g_nvalues++] = str->ptr[i];
	g_values[g_nvalues++] = '|';
	g_values[g_nvalues] = '\0';
}

static int on_inst (hio_json_t* json, hio_json_inst_t inst, hio_oow_t level,
                    hio_oow_t index, hio_json_state_t container_state,
                    const hio_oocs_t* str, void* ctx)
{
	hio_bch_t c;

	if (level > g_maxlevel) g_maxlevel = level;

	switch (inst)
	{
		case HIO_JSON_INST_START_ARRAY:  c = '['; break;
		case HIO_JSON_INST_END_ARRAY:    c = ']'; break;
		case HIO_JSON_INST_START_OBJECT: c = '{'; break;
		case HIO_JSON_INST_END_OBJECT:   c = '}'; break;
		case HIO_JSON_INST_KEY:          c = 'k'; record_value(str); break;
		case HIO_JSON_INST_STRING:       c = 's'; record_value(str); break;
		case HIO_JSON_INST_NUMBER:       c = 'n'; record_value(str); break;
		case HIO_JSON_INST_NIL:          c = '-'; break;
		case HIO_JSON_INST_TRUE:         c = 't'; break;
		case HIO_JSON_INST_FALSE:        c = 'f'; break;
		default:                         c = '?'; break;
	}

	if (g_ninsts + 1 < HIO_SIZEOF(g_insts))
	{
		g_insts[g_ninsts++] = c;
		g_insts[g_ninsts] = '\0';
	}
	return 0;
}

/* feed a whole document in one go. returns what hio_json_feed() returned. */
static int parse (const hio_bch_t* doc, hio_bitmask_t opts, hio_oow_t* rem)
{
	hio_json_t* json;
	int x;
	hio_oow_t r = 0;

	reset_record ();
	json = hio_json_open(g_hio, 0);
	if (!json) return -2;
	if (opts) hio_json_setoption (json, opts);
	hio_json_setinstcb (json, on_inst, HIO_NULL);

	x = hio_json_feed(json, doc, hio_count_bcstr(doc), &r, 0);
	if (rem) *rem = r;
	hio_json_close (json);
	return x;
}

/* ------------------------------------------------------------------ */

static void test_scalars_and_shape (void)
{
	OK (parse("[1,2,3]", 0, HIO_NULL) == 0 && hio_comp_bcstr(g_insts, "[nnn]", 0) == 0,
	    "an array of numbers parses to the instructions the grammar says");
	OK (hio_comp_bcstr(g_values, "1|2|3|", 0) == 0,
	    "and the numbers arrive with their text intact");

	OK (parse("{\"a\":1}", 0, HIO_NULL) == 0 && hio_comp_bcstr(g_insts, "{kn}", 0) == 0,
	    "an object parses as a key followed by its value");
	OK (hio_comp_bcstr(g_values, "a|1|", 0) == 0,
	    "and the key is reported separately from the value");

	OK (parse("[true,false,null]", 0, HIO_NULL) == 0 && hio_comp_bcstr(g_insts, "[tf-]", 0) == 0,
	    "the three literals each get an instruction of their own");

	/* the distinction that matters: a quoted 1 is a string, not a number */
	OK (parse("[\"1\"]", 0, HIO_NULL) == 0 && hio_comp_bcstr(g_insts, "[s]", 0) == 0,
	    "a quoted number is a string rather than a number");
}

static void test_nesting (void)
{
	OK (parse("{\"a\":{\"b\":[1,{\"c\":2}]}}", 0, HIO_NULL) == 0 &&
	    hio_comp_bcstr(g_insts, "{k{k[n{kn}]}}", 0) == 0,
	    "nesting comes out in the order it was written");
	OK (g_maxlevel >= 3, "and the level reported grows with the nesting");
}

static void test_strings (void)
{
	/* escapes are the parser's job - what reaches the callback should be the
	 * text the document meant, not the text it was spelled with */
	OK (parse("[\"a\\\"b\"]", 0, HIO_NULL) == 0 && hio_comp_bcstr(g_values, "a\"b|", 0) == 0,
	    "an escaped quote is unescaped before the value is handed over");
	OK (parse("[\"a\\nb\"]", 0, HIO_NULL) == 0 &&
	    g_values[1] == '\n' && hio_comp_bcstr(g_values, "a\nb|", 0) == 0,
	    "and so is an escaped newline");
	OK (parse("[\"\"]", 0, HIO_NULL) == 0 && hio_comp_bcstr(g_insts, "[s]", 0) == 0,
	    "an empty string is still a string");
}

static void test_split_feeds (void)
{
	hio_json_t* json;
	hio_oow_t rem = 0;
	static const hio_bch_t doc[] = "{\"key\":[1,\"two\",true]}";
	hio_oow_t i, len = hio_count_bcstr(doc);
	int accepted = 1;

	/* the reader is fed by a loop that has no idea where token boundaries
	 * fall, so feeding one octet at a time must produce the same parse as
	 * feeding the lot. this is the property the whole streaming design rests
	 * on and the one a whole-document test would never notice breaking. */
	reset_record ();
	json = hio_json_open(g_hio, 0);
	if (!json) { skip ("cannot open a json reader", 2); return; }
	hio_json_setinstcb (json, on_inst, HIO_NULL);

	for (i = 0; i < len; i++)
	{
		if (hio_json_feed(json, &doc[i], 1, &rem, 0) <= -1) { accepted = 0; break; }
	}
	hio_json_close (json);

	OK (accepted, "a document fed one octet at a time is accepted");
	OK (hio_comp_bcstr(g_insts, "{k[nst]}", 0) == 0,
	    "and parses identically to the same document fed whole");
}

static void test_options (void)
{
	/* off by default, so the same document parses or does not depending on
	 * what was asked for - which is what makes these options rather than
	 * quirks */
	OK (parse("{a:1}", 0, HIO_NULL) <= -1,
	    "an unquoted key is rejected by default");
	OK (parse("{a:1}", HIO_JSON_PERMIT_WORD_KEY, HIO_NULL) == 0 &&
	    hio_comp_bcstr(g_insts, "{kn}", 0) == 0,
	    "and accepted with HIO_JSON_PERMIT_WORD_KEY");

	OK (parse("[1 2]", 0, HIO_NULL) <= -1,
	    "a missing comma is rejected by default");
	OK (parse("[1 2]", HIO_JSON_OPTIONAL_COMMA, HIO_NULL) == 0 &&
	    hio_comp_bcstr(g_insts, "[nn]", 0) == 0,
	    "and accepted with HIO_JSON_OPTIONAL_COMMA");

	OK (parse("[1] # trailing\n", HIO_JSON_LINE_COMMENT, HIO_NULL) == 0 &&
	    hio_comp_bcstr(g_insts, "[n]", 0) == 0,
	    "and a line comment is ignored with HIO_JSON_LINE_COMMENT");
}

static void test_malformed (void)
{
	/* a parser that accepts everything is not a parser. each of these is a
	 * different way to be wrong, so each is checked rather than assuming one
	 * rejection implies the rest. */
	OK (parse("[1,2", 0, HIO_NULL) == 0 && g_insts[hio_count_bcstr(g_insts) - 1] != ']',
	    "an unterminated array is left incomplete rather than closed");
	OK (parse("[1,2}", 0, HIO_NULL) <= -1, "a mismatched bracket is rejected");
	OK (parse("{1:2}", 0, HIO_NULL) <= -1, "a non-string object key is rejected");
	OK (parse("[tru]", 0, HIO_NULL) <= -1, "a misspelled literal is rejected");
}

static void test_number_forms (void)
{
	/* the ordinary forms */
	OK (parse("[1.5]", 0, HIO_NULL) == 0 && hio_comp_bcstr(g_values, "1.5|", 0) == 0,
	    "a fractional number keeps its text");
	OK (parse("[-2]", 0, HIO_NULL) == 0 && hio_comp_bcstr(g_values, "-2|", 0) == 0,
	    "and so does a negative one");
	OK (parse("[1e3]", 0, HIO_NULL) == 0 && hio_comp_bcstr(g_values, "1e3|", 0) == 0,
	    "and one in exponent form");

	/* [NOTE] and two that strict json does not allow. this reader takes a
	 * laxer view of numbers than the grammar does - asserted as it behaves
	 * rather than as it ought to, so that if it is ever tightened, this says
	 * so instead of quietly continuing to pass. */
	OK (parse("[01]", 0, HIO_NULL) == 0 && hio_comp_bcstr(g_insts, "[n]", 0) == 0,
	    "a leading zero is accepted, which strict json would not allow");
	OK (parse("[+1]", 0, HIO_NULL) == 0 && hio_comp_bcstr(g_insts, "[n]", 0) == 0,
	    "and so is a leading plus");
}

static void test_stop_when_complete (void)
{
	hio_json_t* json;
	hio_oow_t rem = 0;
	static const hio_bch_t two[] = "[1][2]";
	int x;

	/* with stop_if_ever_completed, feeding two documents back to back must
	 * stop after the first and say how much is left - which is how a stream of
	 * documents on one connection is read */
	reset_record ();
	json = hio_json_open(g_hio, 0);
	if (!json) { skip ("cannot open a json reader", 2); return; }
	hio_json_setinstcb (json, on_inst, HIO_NULL);

	x = hio_json_feed(json, two, hio_count_bcstr(two), &rem, 1);
	OK (x == 1, "feeding two documents stops at the end of the first");
	OK (rem == 3 && hio_comp_bcstr(g_insts, "[n]", 0) == 0,
	    "and reports exactly the second one as unconsumed");

	hio_json_close (json);
}

/* ------------------------------------------------------------------ */

int main (void)
{
	hio_errinf_t errinf;

	g_hio = hio_open(HIO_NULL, 0, HIO_NULL, HIO_FEATURE_ALL, 16, &errinf);
	if (!g_hio)
	{
		no_plan ();
		bail_out ("unable to open hio");
		return -1;
	}
	quiet_logging (g_hio);

	no_plan ();

	test_scalars_and_shape ();
	test_nesting ();
	test_strings ();
	test_split_feeds ();
	test_options ();
	test_malformed ();
	test_number_forms ();
	test_stop_when_complete ();

	hio_close (g_hio);
	return exit_status();
}
