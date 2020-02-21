#
# hawk -f generr.awk mio.h
#

@pragma striprecspc on

BEGIN {
	##STRIPRECSPC=1 
	FS = "[[:space:]]+";
	capture = 0;
	msgcount = 0;
}

capture == 1 {
	if ($0 == "};") 
	{
		capture = 0;
	}
	else if ($1 ~ /^MIO_E[[:alnum:]]+,*/)
	{
		msg = "";
		for (i = 3; i < NF; i++)
		{
			if (i > 3) msg = msg OFS;
			msg = msg $i;
		}

		printf ("static mio_ooch_t errstr_%d[] = {", msgcount);
		len = length(msg);
		for (i = 1; i <= len; i++)
		{
			printf ("'%c', ", substr(msg, i, 1));
		}
		printf ("'\\0' };\n");
		msgcount++;
	}
}
/^enum mio_errnum_t$/ {
	getline x; # consume the next line
	capture = 1;
}


END {
	printf ("static mio_ooch_t* errstr[] =\n");
	printf ("{\n\t");
	for (i = 0; i < msgcount; i++)
	{
		if (i > 0)
		{
			if (i % 5 == 0) printf (",\n\t");
			else printf (", ");
		}
		printf ("errstr_%d", i);
	}
	if ((i - 1) % 5 != 0) printf ("\n");
	printf ("};\n");
}
