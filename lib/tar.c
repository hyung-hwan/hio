#include <hio-tar.h>
#include <hio-utl.h>
#include <hio-prv.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>

static hio_uint8_t _end_block[HIO_TAR_BLKSIZE] = { 0, };

#if 0
static int verify_checksum(const char *p)
{
	int n, u = 0;

	for (n = 0; n < 512; n++) 
	{
		if (n < 148 || n > 155)
			/* Standard tar checksum adds unsigned bytes. */
			u += ((hio_uint8_t*)p)[n];
		else
			u += 0x20;

	}
	return (u == parseoct(p + 148, 8));
}
#endif

static int create_dir (hio_bch_t *pathname, int mode)
{
	char *p;
	int n;
	hio_oow_t pathlen;

	pathlen = hio_count_bcstr(pathname);

	/* Strip trailing '/' TODO: improve ...*/ 
	if (pathname[pathlen - 1] == '/')
		pathname[pathlen - 1] = '\0';

	n = mkdir(pathname, mode);
	if (n <= -1) 
	{
		/* On failure, try creating parent directory. */
		p = hio_rfind_bchar_in_bcstr(pathname, '/');
		if (p) 
		{
			*p = '\0';
			create_dir(pathname, 0755);
			*p = '/';
			n = mkdir(pathname, mode);
		}
	}

	return n;
}

static FILE* create_file (char *pathname, int mode)
{
	FILE *fp;
	fp = fopen(pathname, "wb+");
	if (!fp) 
	{
		char* p = hio_rfind_bchar_in_bcstr(pathname, '/');
		if (p) 
		{
			*p = '\0';
			create_dir(pathname, 0755);
			*p = '/';
			fp = fopen(pathname, "wb+");
		}
	}
	return fp;
}

static int extract_tar (hio_t* hio, FILE* fp)
{
	char buf[HIO_TAR_BLKSIZE];
	FILE* f = NULL;
	int filesize;

	while (1)
	{
		int is_sober;
		const hio_bch_t* endptr;
		hio_iolen_t nbytes;
		hio_tar_hdr_t* hdr;
		hio_uint32_t mode;

		nbytes = fread(buf, 1, HIO_TAR_BLKSIZE, fp);
		if (nbytes < HIO_TAR_BLKSIZE)
		{
			hio_seterrbfmt (hio, HIO_EINVAL, "truncated trailing block");
			return -1;
		}

		/* all-zero byte block ends the archive */
		if (HIO_MEMCMP(buf, _end_block, HIO_TAR_BLKSIZE) == 0) break;

#if 0
		if (!verify_checksum(buf)) 
		{
			hio_seterrbfmt (hio, HIO_EINVAL, "invalid checksum value");
			return -1;
		}
#endif

		hdr = (hio_tar_hdr_t*)buf;
		filesize = hio_bchars_to_uintmax(hdr->size, HIO_COUNTOF(hdr->size), HIO_BCHARS_TO_UINTMAX_MAKE_OPTION(0,0,0,8), &endptr, &is_sober);
		mode = hio_bchars_to_uintmax(hdr->mode, HIO_COUNTOF(hdr->mode), HIO_BCHARS_TO_UINTMAX_MAKE_OPTION(0,0,0,8), &endptr, &is_sober);

		switch (hdr->typeflag) 
		{
			case HIO_TAR_LNKTYPE:
				printf(" Ignoring hardlink %s\n", hdr->name);
				break;
			case HIO_TAR_SYMTYPE:
				printf(" Ignoring symlink %s\n", hdr->name);
				break;
			case HIO_TAR_CHRTYPE:
				printf(" Ignoring character device %s\n", hdr->name);
				break;
			case HIO_TAR_BLKTYPE:
				printf(" Ignoring block device %s\n", hdr->name);
				break;
			case HIO_TAR_DIRTYPE:
				printf(" Extracting dir %s\n", hdr->name);
				if (filesize != 0)
				{
					/* something wrong */
				}
				create_dir(hdr->name, mode);
				break;
			case HIO_TAR_FIFOTYPE:
				printf(" Ignoring FIFO %s\n", hdr->name);
				break;

			case HIO_TAR_CONTTYPE:
				printf(" Ignoring cont %s\n", hdr->name);
				break;

			default:
				printf(" Extracting file %s\n", hdr->name);
				f = create_file(hdr->name, mode);

				while (filesize > 0) 
				{
					nbytes = fread(buf, 1, HIO_TAR_BLKSIZE, fp);
					if (nbytes < HIO_TAR_BLKSIZE) 
					{
						fprintf(stderr, "Short read - Expected 512, got %d\n", (int)nbytes);
						return -1;
					}
					if (filesize < HIO_TAR_BLKSIZE) nbytes = filesize;

					if (f) 
					{
						if (fwrite(buf, 1, nbytes, f) != nbytes)
						{
							fprintf(stderr, "Failed write\n");
							break;
						}
					}
					filesize -= nbytes;
				}

				fclose (f);
				break;
		}
	}

	return 0;
}


int hio_extract_tar (hio_t* hio, const hio_bch_t* archive_file)
{
	FILE* fp;
	int n;

	fp = fopen(archive_file, "r");
	if (!fp) return -1;

	n = extract_tar(hio, fp);

	fclose (fp);
	return n;
}




hio_tar_t* hio_tar_open (hio_t* hio, hio_oow_t xtnsize)
{
	hio_tar_t* tar;

	tar = (hio_tar_t*)hio_callocmem(hio, HIO_SIZEOF(*tar) + xtnsize);
	if (tar)
	{
		if (hio_tar_init(tar) <= -1)
		{
			hio_freemem (hio, tar);
			tar = HIO_NULL;
		}
	}

	return tar;
}

void hio_tar_close (hio_tar_t* tar)
{
	hio_tar_fini (tar);
	hio_freemem (tar->hio, tar);
}

int hio_tar_init (hio_tar_t* tar)
{
	tar->state = HIO_TAR_STATE_START;
	tar->blk.len = 0;
	return 0;
}

void hio_tar_fini (hio_tar_t* tar)
{
}


static int process_header (hio_tar_t* tar)
{
	hio_tar_hdr_t* hdr;

	HIO_ASSERT (tar->hio, tar->blk.len == HIO_TAR_BLKSIZE);
	hdr = (hio_tar_hdr_t*)tar->blk.buf;

	/* all-zero byte block ends the archive */
	if (HIO_MEMCMP(hdr, _end_block, HIO_TAR_BLKSIZE) == 0) 
	{
		tar->state = HIO_TAR_STATE_END;
		return 0;
	}

	return 0;
}

#define hio_tar_endfeed(tar) hio_tar_feed(tar, HIO_NULL, 0)

int hio_tar_feed (hio_tar_t* tar, const void* ptr, hio_oow_t len)
{
	if (!ptr)
	{
		/* EOF indicator */
		if (tar->state != HIO_TAR_STATE_END || tar->blk.len > 0)
		{
			/* ERROR - premature end of file */
			return -1;
		}
	}

	while (len > 0)
	{
		switch (tar->state)
		{
			case HIO_TAR_STATE_START:
			{
				hio_oow_t cplen;
				cplen = HIO_COUNTOF(tar->blk.buf) - tar->blk.len;
				if (len < cplen) cplen = len;
				HIO_MEMCPY (&tar->blk.buf[tar->blk.len], ptr, cplen);
				len -= cplen;
				break;
			}

			case HIO_TAR_STATE_FILE:
				break;

			case HIO_TAR_STATE_END:
				if (len > 0)
				{
					/* garbage after the final ending block */
					return -1;
				}
				break;
		}
	}

	return 0;
}
