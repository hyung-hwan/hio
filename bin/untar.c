#include <hio-tar.h>
#include <hio-utl.h>
#include <stdio.h>


int main (int argc, char* argv[])
{
	hio_t* hio;

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

	hio_extract_tar(hio, argv[1]);

	hio_close (hio);
	return 0;
}

