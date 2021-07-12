
#include <mio-utl.h>
#include <stdio.h>
#include "t.h"

int main ()
{
	{
		int is_sober;
		const mio_bch_t* endptr;
		mio_intmax_t v;

		v = mio_bchars_to_intmax("10 ", 3, MIO_BCHARS_TO_INTMAX_MAKE_OPTION(0,0,0,10), &endptr, &is_sober);
		T_ASSERT1 (v == 10 && *endptr == ' ' && is_sober == 1, "space after digits without rtrim");

		v = mio_bchars_to_intmax("10 ", 3, MIO_BCHARS_TO_INTMAX_MAKE_OPTION(0,0,1,10), &endptr, &is_sober);
		T_ASSERT1 (v == 10 && *endptr == '\0' && is_sober == 1, "space after digits with rtrim");

		v = mio_bchars_to_intmax("10E", 3, MIO_BCHARS_TO_INTMAX_MAKE_OPTION(0,0,1,10), &endptr, &is_sober);
		T_ASSERT1 (v == 10 && *endptr == 'E' && is_sober == 1, "number ending with E without the E option ");

		v = mio_bchars_to_intmax("10E", 3, MIO_BCHARS_TO_INTMAX_MAKE_OPTION(1,0,1,10), &endptr, &is_sober);
		T_ASSERT1 (v == 10 && *endptr == '\0' && is_sober == 1, "integer in E notation");

		v = mio_bchars_to_intmax("10E+0", 5, MIO_BCHARS_TO_INTMAX_MAKE_OPTION(1,0,1,10), &endptr, &is_sober);
		T_ASSERT1 (v == 10 && *endptr == '\0' && is_sober == 1, "integer in E notation");

		v = mio_bchars_to_intmax("10E+1", 5, MIO_BCHARS_TO_INTMAX_MAKE_OPTION(1,0,1,10), &endptr, &is_sober);
		T_ASSERT1 (v == 100 && *endptr == '\0' && is_sober == 1, "integer in E notation");


		v = mio_bchars_to_intmax("10E+2", 5, MIO_BCHARS_TO_INTMAX_MAKE_OPTION(1,0,1,10), &endptr, &is_sober);
		T_ASSERT1 (v == 1000 && *endptr == '\0' && is_sober == 1, "integer in E notation");

		v = mio_bchars_to_intmax("10E3", 4, MIO_BCHARS_TO_INTMAX_MAKE_OPTION(1,0,1,10), &endptr, &is_sober);
		T_ASSERT1 (v == 10000 && *endptr == '\0' && is_sober == 1, "integer in E notation");

		v = mio_bchars_to_intmax("10E-", 4, MIO_BCHARS_TO_INTMAX_MAKE_OPTION(1,0,1,10), &endptr, &is_sober);
		T_ASSERT1 (v == 10 && *endptr == '\0' && is_sober == 1, "integer in E notation");


		v = mio_bchars_to_intmax("10E-0", 5, MIO_BCHARS_TO_INTMAX_MAKE_OPTION(1,0,1,10), &endptr, &is_sober);
		T_ASSERT1 (v == 10 && *endptr == '\0' && is_sober == 1, "integer in E notation");


		v = mio_bchars_to_intmax("10E-1", 5, MIO_BCHARS_TO_INTMAX_MAKE_OPTION(1,0,1,10), &endptr, &is_sober);
		T_ASSERT1 (v == 1 && *endptr == '\0' && is_sober == 1, "integer in E notation");

		v = mio_bchars_to_intmax("10E-2", 5, MIO_BCHARS_TO_INTMAX_MAKE_OPTION(1,0,1,10), &endptr, &is_sober);
		T_ASSERT1 (v == 0 && *endptr == '\0' && is_sober == 1, "integer in E notation");
	}

	return 0;

oops:
	return -1;
}
