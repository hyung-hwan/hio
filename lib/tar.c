#include <hio-tar.h>
#include <hio-utl.h>
#include <hio-prv.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>

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

hio_tar_t* hio_tar_open (hio_t* hio, hio_oow_t xtnsize)
{
	hio_tar_t* tar;

	tar = (hio_tar_t*)hio_callocmem(hio, HIO_SIZEOF(*tar) + xtnsize);
	if (tar)
	{
		if (hio_tar_init(tar, hio) <= -1)
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

int hio_tar_init (hio_tar_t* tar, hio_t* hio)
{
	tar->hio = hio;
	tar->x.state = HIO_TAR_STATE_START;
	tar->x.blk.len = 0;
	hio_becs_init (&tar->x.hi.filename, tar->hio, 0); /* won't fail with the capacity of 0 */
	return 0;
}

void hio_tar_fini (hio_tar_t* tar)
{
	hio_becs_fini (&tar->x.hi.filename);
	if (tar->x.hi.fp)
	{
		/* clean up */
		fclose (tar->x.hi.fp);
		tar->x.hi.fp = HIO_NULL;
	}
}

/* extraction - the implementation is still far from complete */

void hio_tar_setxrootwithbcstr (hio_tar_t* tar, const hio_bch_t* root)
{
	hio_copy_bcstr (tar->x.root, HIO_COUNTOF(tar->x.root), root); /* TOOD: handle truncation. make tar->x.root dyanmic? */
}

void hio_tar_setxcb (hio_tar_t* tar, hio_tar_xcb_t xcb,	void* ctx)
{
	tar->x.xcb = xcb;
	tar->x.xcb_ctx = ctx;
}

static int x_process_header (hio_tar_t* tar)
{
	hio_tar_hdr_t* hdr;

	HIO_ASSERT (tar->hio, tar->x.state == HIO_TAR_STATE_START);
	HIO_ASSERT (tar->hio, tar->x.blk.len == HIO_TAR_BLKSIZE);
	hdr = (hio_tar_hdr_t*)tar->x.blk.buf;

	/* all-zero byte block ends the archive */
	if (HIO_MEMCMP(hdr, _end_block, HIO_TAR_BLKSIZE) == 0)
	{
		/* two all-zero blocks are expected as the EOF indicator */
		tar->x.state = HIO_TAR_STATE_END;
	}
	else
	{
		int is_sober;
		const hio_bch_t* endptr;
		const hio_bch_t* filename;

		tar->x.hi.filesize = hio_bchars_to_uintmax(hdr->size, HIO_COUNTOF(hdr->size), HIO_BCHARS_TO_UINTMAX_MAKE_OPTION(0,0,0,8), &endptr, &is_sober);
		tar->x.hi.filemode = hio_bchars_to_uintmax(hdr->mode, HIO_COUNTOF(hdr->mode), HIO_BCHARS_TO_UINTMAX_MAKE_OPTION(0,0,0,8), &endptr, &is_sober);
		tar->x.hi.devmajor = hio_bchars_to_uintmax(hdr->devmajor, HIO_COUNTOF(hdr->devmajor), HIO_BCHARS_TO_UINTMAX_MAKE_OPTION(0,0,0,8), &endptr, &is_sober);
		tar->x.hi.devminor = hio_bchars_to_uintmax(hdr->devminor, HIO_COUNTOF(hdr->devminor), HIO_BCHARS_TO_UINTMAX_MAKE_OPTION(0,0,0,8), &endptr, &is_sober);

		if (tar->x.hi.fp)
		{
			/* just in case */
			fclose (tar->x.hi.fp);
			tar->x.hi.fp = HIO_NULL;
		}

		hio_becs_clear (&tar->x.hi.filename);
		if (tar->x.root[0] != '\0')
		{
			if (hio_becs_cat(&tar->x.hi.filename, tar->x.root) == (hio_oow_t)-1) return -1;
			if (HIO_BECS_LASTCHAR(&tar->x.hi.filename) != '/' && hio_becs_ccat(&tar->x.hi.filename, '/') == (hio_oow_t)-1) return -1;
		}
		if (hio_becs_cat(&tar->x.hi.filename, hdr->prefix) == (hio_oow_t)-1 ||
		    hio_becs_cat(&tar->x.hi.filename, hdr->name) == (hio_oow_t)-1) return -1;

		filename = HIO_BECS_PTR(&tar->x.hi.filename);
		switch (hdr->typeflag)
		{
			case HIO_TAR_LNKTYPE:
				link (hdr->linkname, filename);
				break;
			case HIO_TAR_SYMTYPE:
				symlink (hdr->linkname, filename); /* TODO: error check */
				break;
			case HIO_TAR_CHRTYPE:
				mknod (filename, S_IFCHR | tar->x.hi.filemode,  ((tar->x.hi.devmajor << 8) | tar->x.hi.devminor));
				break;
			case HIO_TAR_BLKTYPE:
				mknod (filename, S_IFBLK | tar->x.hi.filemode,  ((tar->x.hi.devmajor << 8) | tar->x.hi.devminor));
				break;
			case HIO_TAR_DIRTYPE:
				create_dir (filename, tar->x.hi.filemode);
				break;
			case HIO_TAR_FIFOTYPE:
				mkfifo (filename, tar->x.hi.filemode);
				break;

			case HIO_TAR_CONTTYPE: /* treate it like REGTYPE for now */
			default: /* HIO_TAR_REGTYPE */
			{
				FILE* fp;

				fp = fopen(filename, "wb+");
				if (!fp)
				{
					hio_seterrwithsyserr (tar->hio, 0, errno);
					return -1;
				}

				fchmod (fileno(fp), tar->x.hi.filemode);

				tar->x.hi.fp = fp;
				tar->x.state = HIO_TAR_STATE_FILE;
				goto done;
			}
		}

		tar->x.state = HIO_TAR_STATE_START;
	}

done:
	return 0;
}

static int x_process_content (hio_tar_t* tar)
{
	hio_oow_t chunksize;

	HIO_ASSERT (tar->hio, tar->x.blk.len == HIO_TAR_BLKSIZE);
	HIO_ASSERT (tar->hio, tar->x.hi.filesize > 0);
	HIO_ASSERT (tar->hio, tar->x.hi.fp != HIO_NULL);

	chunksize = tar->x.hi.filesize < tar->x.blk.len? tar->x.hi.filesize: tar->x.blk.len;

/* TODO: error check */
	fwrite (tar->x.blk.buf, 1, chunksize, tar->x.hi.fp);

	tar->x.hi.filesize -= chunksize;
	if (tar->x.hi.filesize <= 0)
	{
		/* end of file */
		fclose (tar->x.hi.fp);
		tar->x.hi.fp = HIO_NULL;

		tar->x.state = HIO_TAR_STATE_START;
	}

	return 0;
}

int hio_tar_xfeed (hio_tar_t* tar, const void* ptr, hio_oow_t len)
{
	if (!ptr)
	{
		/* EOF indicator */
		if (tar->x.state != HIO_TAR_STATE_END || tar->x.blk.len > 0)
		{
			/* ERROR - premature end of file */
			hio_seterrbfmt (tar->hio, HIO_EINVAL, "premature end of feed");
			return -1;
		}
	}

	while (len > 0)
	{
		hio_oow_t cplen;

		cplen = HIO_COUNTOF(tar->x.blk.buf) - tar->x.blk.len; /* required length to fill a block */
		if (len < cplen) cplen = len; /* not enough to fill a block */

		HIO_MEMCPY (&tar->x.blk.buf[tar->x.blk.len], ptr, cplen);
		tar->x.blk.len += cplen;
		len -= cplen;
		ptr += cplen;

		if (tar->x.blk.len == HIO_COUNTOF(tar->x.blk.buf))
		{
			/* on a complete block */
			switch (tar->x.state)
			{
				case HIO_TAR_STATE_START:
					if (x_process_header(tar) <= -1) return -1;
					break;

				case HIO_TAR_STATE_FILE:
					if (x_process_content(tar) <= -1) return -1;
					break;

				case HIO_TAR_STATE_END:
					if (HIO_MEMCMP(tar->x.blk.buf, _end_block, HIO_TAR_BLKSIZE) != 0)
					{
						hio_seterrbfmt (tar->hio, HIO_EINVAL, "trailing garbage at the end of feed");
						return -1;
					}
					/* there may come multiple EOF marker blocks depending on the logical record size.
					 * this implementation doesn't care how much such blocks are given */
					break;
			}

			tar->x.blk.len = 0;
		}
	}

	return 0;
}

