#include <hio-tar.h>
#include <hio-utl.h>
#include <stdio.h>


int main (int argc, char* argv[])
{
	hio_t* hio;
	hio_tar_t* tar;
	FILE* fp;

	if (argc != 2)
	{
		fprintf (stderr, "Usage: %s <filename>\n", hio_get_base_name_bcstr(argv[0]));
		return -1;
	}

	hio = hio_open(HIO_NULL, 0, HIO_NULL, HIO_FEATURE_ALL, 512, HIO_NULL);
	if (!hio)
	{
		fprintf (stderr, "Error: unable to open hio\n");
		return -1;
	}

	tar = hio_tar_open(hio, 0);
	if (!tar)
	{
		fprintf (stderr, "Error: unable to open tar\n");
		hio_close (hio);
		return -1;
	}

	fp = fopen(argv[1], "r");
	if (!fp)
	{
		fprintf (stderr, "Error: unable to open file %s\n", argv[1]);
		hio_tar_close (tar);
		hio_close (hio);
		return -1;
	}

	while (!feof(fp) && !ferror(fp))
	{
		int n;
		char buf[4096]; /* TODO: use a different buffer size???*/
		n = fread(buf, 1, sizeof(buf), fp);
		if (n > 0) 
		{
			if (hio_tar_xfeed(tar, buf, n) <= -1)
			{
				fprintf (stderr, "Error: tar error - %s\n", hio_geterrbmsg(hio));
				break;
			}
		}
	}

	hio_tar_endxfeed (tar); /* indicate the end of input */

	fclose (fp);
	hio_tar_close (tar);
	hio_close (hio);
	return 0;
}

