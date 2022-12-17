
#include <hio-utl.h>
#include <stdio.h>
#include "tap.h"

int main ()
{
	no_plan ();

	{
		int is_sober;
		const hio_bch_t* endptr;
		hio_intmax_t v;

		v = hio_bchars_to_intmax("10 ", 3, HIO_BCHARS_TO_INTMAX_MAKE_OPTION(0,0,0,10), &endptr, &is_sober);
		OK (v == 10 && *endptr == ' ' && is_sober == 1, "space after digits without rtrim");

		v = hio_bchars_to_intmax("10 ", 3, HIO_BCHARS_TO_INTMAX_MAKE_OPTION(0,0,1,10), &endptr, &is_sober);
		OK (v == 10 && *endptr == '\0' && is_sober == 1, "space after digits with rtrim");

		v = hio_bchars_to_intmax("10E", 3, HIO_BCHARS_TO_INTMAX_MAKE_OPTION(0,0,1,10), &endptr, &is_sober);
		OK (v == 10 && *endptr == 'E' && is_sober == 1, "number ending with E without the E option ");

		v = hio_bchars_to_intmax("10E", 3, HIO_BCHARS_TO_INTMAX_MAKE_OPTION(1,0,1,10), &endptr, &is_sober);
		OK (v == 10 && *endptr == '\0' && is_sober == 1, "integer in E notation");

		v = hio_bchars_to_intmax("10E+0", 5, HIO_BCHARS_TO_INTMAX_MAKE_OPTION(1,0,1,10), &endptr, &is_sober);
		OK (v == 10 && *endptr == '\0' && is_sober == 1, "integer in E notation");

		v = hio_bchars_to_intmax("10E+1", 5, HIO_BCHARS_TO_INTMAX_MAKE_OPTION(1,0,1,10), &endptr, &is_sober);
		OK (v == 100 && *endptr == '\0' && is_sober == 1, "integer in E notation");


		v = hio_bchars_to_intmax("10E+2", 5, HIO_BCHARS_TO_INTMAX_MAKE_OPTION(1,0,1,10), &endptr, &is_sober);
		OK (v == 1000 && *endptr == '\0' && is_sober == 1, "integer in E notation");

		v = hio_bchars_to_intmax("10E3", 4, HIO_BCHARS_TO_INTMAX_MAKE_OPTION(1,0,1,10), &endptr, &is_sober);
		OK (v == 10000 && *endptr == '\0' && is_sober == 1, "integer in E notation");

		v = hio_bchars_to_intmax("10E-", 4, HIO_BCHARS_TO_INTMAX_MAKE_OPTION(1,0,1,10), &endptr, &is_sober);
		OK (v == 10 && *endptr == '\0' && is_sober == 1, "integer in E notation");


		v = hio_bchars_to_intmax("10E-0", 5, HIO_BCHARS_TO_INTMAX_MAKE_OPTION(1,0,1,10), &endptr, &is_sober);
		OK (v == 10 && *endptr == '\0' && is_sober == 1, "integer in E notation");


		v = hio_bchars_to_intmax("10E-1", 5, HIO_BCHARS_TO_INTMAX_MAKE_OPTION(1,0,1,10), &endptr, &is_sober);
		OK (v == 1 && *endptr == '\0' && is_sober == 1, "integer in E notation");

		v = hio_bchars_to_intmax("10E-2", 5, HIO_BCHARS_TO_INTMAX_MAKE_OPTION(1,0,1,10), &endptr, &is_sober);
		OK (v == 0 && *endptr == '\0' && is_sober == 1, "integer in E notation");
	}

	{
		hio_bch_t tmp[10];
		hio_oow_t x;
		hio_uch_t uc;

		x = hio_uc_to_utf8(0x2665, tmp, HIO_COUNTOF(tmp));
		OK (x == 3 && (hio_uint8_t)tmp[0] == 0xE2 && (hio_uint8_t)tmp[1] == 0x99 && (hio_uint8_t)tmp[2] == 0xA5, "unicode to utf8 conversion");

		x = hio_utf8_to_uc(tmp, x, &uc);
		OK (x == 3 && uc == 0x2665, "utf8 to unicode conversion");

	#if (HIO_SIZEOF_UCH_T > 2)
		x = hio_uc_to_utf8(0x1F3E9, tmp, HIO_COUNTOF(tmp));
		OK (x == 4 && (hio_uint8_t)tmp[0] == 0xF0 && (hio_uint8_t)tmp[1] == 0x9F && (hio_uint8_t)tmp[2] == 0x8F && (hio_uint8_t)tmp[3] == 0xA9, "unicode to utf8 conversion");

		x = hio_utf8_to_uc(tmp, x, &uc);
		OK (x == 4 && uc == 0x1F3E9, "utf8 to unicode conversion");
	#endif
	}

	return exit_status();
}
