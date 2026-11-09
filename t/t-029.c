/*
 * the tar extractor.
 *
 * the archive is fed as octets rather than read from a file, which is how the
 * UNTAR route feeds it from a request body, so a crafted header reaches the
 * extractor exactly as it would over a connection.
 *
 * the cases here are the ones that only appear on the second run or on an
 * entry a tar(1) of its own would never write: a directory that already
 * exists, a path that strips down to nothing, and a file of no length - which
 * is followed by no content blocks at all, so the block after its header is
 * the next header.
 */

#include <hio-tar.h>
#include <hio-utl.h>
#include "tap.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

#define BLK HIO_TAR_BLKSIZE

static hio_t* g_hio;
static hio_bch_t g_root[256];

/* ------------------------------------------------------------------ */

static void put_field (char* f, int n, const char* s)
{
	hio_oow_t len = hio_count_bcstr(s);
	memset(f, 0, n);
	memcpy(f, s, len < (hio_oow_t)n? len: (hio_oow_t)n);
}

/* one header block, with the checksum tar(1) would have written */
static void make_hdr (hio_uint8_t* blk, const char* name, char typeflag, const char* octsize)
{
	hio_tar_hdr_t* h = (hio_tar_hdr_t*)blk;
	unsigned int sum = 0;
	int i;
	char cs[8];

	memset(blk, 0, BLK);
	put_field (h->name, sizeof(h->name), name);
	put_field (h->mode, sizeof(h->mode), "0000755");
	put_field (h->uid, sizeof(h->uid), "0000000");
	put_field (h->gid, sizeof(h->gid), "0000000");
	put_field (h->size, sizeof(h->size), octsize);
	put_field (h->mtime, sizeof(h->mtime), "00000000000");
	h->typeflag = typeflag;
	memcpy(h->magic, HIO_TAR_MAGIC, HIO_TAR_MAGLEN);
	memcpy(h->version, HIO_TAR_VERSION, HIO_TAR_VERSLEN);

	memset(h->chksum, ' ', sizeof(h->chksum));
	for (i = 0; i < BLK; i++) sum += blk[i];
	snprintf (cs, HIO_COUNTOF(cs), "%06o", sum);
	memcpy(h->chksum, cs, 7);
}

/* feed a whole archive in one go and report whether it was accepted */
static int extract (const hio_uint8_t* ar, hio_oow_t len)
{
	hio_tar_t* tar;
	int ok;

	tar = hio_tar_open(g_hio, 0);
	if (!tar) return -1;

	hio_tar_setxrootwithbcstr (tar, g_root);
	ok = (hio_tar_xfeed(tar, ar, len) == 0 && hio_tar_endxfeed(tar) == 0);
	hio_tar_close (tar);

	return ok? 0: -1;
}

static int path_is_dir (const char* rel)
{
	hio_bch_t p[512];
	struct stat st;
	snprintf (p, HIO_COUNTOF(p), "%s/%s", g_root, rel);
	return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

/* the size of a file under the root, or -1 if it is not there */
static long file_size (const char* rel)
{
	hio_bch_t p[512];
	struct stat st;
	snprintf (p, HIO_COUNTOF(p), "%s/%s", g_root, rel);
	if (stat(p, &st) != 0 || !S_ISREG(st.st_mode)) return -1;
	return (long)st.st_size;
}

static void rm_rf (const char* path)
{
	DIR* dp;
	struct dirent* de;
	hio_bch_t p[512];

	dp = opendir(path);
	if (dp)
	{
		while ((de = readdir(dp)))
		{
			if (hio_comp_bcstr(de->d_name, ".", 0) == 0 || hio_comp_bcstr(de->d_name, "..", 0) == 0) continue;
			snprintf (p, HIO_COUNTOF(p), "%s/%s", path, de->d_name);
			rm_rf (p);
		}
		closedir (dp);
		rmdir (path);
	}
	else unlink(path);
}

static void reset_root (void)
{
	rm_rf (g_root);
	mkdir (g_root, 0755);
}

/* ------------------------------------------------------------------ */

/* extracting the same archive twice is the ordinary case for a document root
 * that is written to more than once, and it is the one that used to walk a
 * path apart one component at a time looking for a parent to create */
static void test_existing_dir (void)
{
	hio_uint8_t ar[BLK * 5];

	memset(ar, 0, sizeof(ar));
	make_hdr (&ar[0], "sub/", HIO_TAR_DIRTYPE, "00000000000");
	make_hdr (&ar[BLK], "sub/a.txt", HIO_TAR_REGTYPE, "00000000005");
	memcpy(&ar[BLK * 2], "HELLO", 5);
	/* blocks 4 and 5 are the end-of-archive marker */

	reset_root ();

	OK (extract(ar, sizeof(ar)) == 0, "an archive with a directory extracts");
	OK (path_is_dir("sub"), "the directory is created");
	OK (file_size("sub/a.txt") == 5, "and the file inside it has its contents");

	/* again, over the top of what the first run left */
	OK (extract(ar, sizeof(ar)) == 0, "the same archive extracts a second time");
	OK (path_is_dir("sub"), "the directory is still there");
	OK (file_size("sub/a.txt") == 5, "and so are the contents");

	/* and a third, because the fault was in the retry rather than the write */
	OK (extract(ar, sizeof(ar)) == 0, "and a third time");
}

/* a directory several levels down, where the parents genuinely do have to be
 * created, must still work - the retry is what builds them */
static void test_nested_dir (void)
{
	hio_uint8_t ar[BLK * 4];

	memset(ar, 0, sizeof(ar));
	make_hdr (&ar[0], "a/b/c/", HIO_TAR_DIRTYPE, "00000000000");
	make_hdr (&ar[BLK], "a/b/c/d.txt", HIO_TAR_REGTYPE, "00000000000");

	reset_root ();

	OK (extract(ar, sizeof(ar)) == 0, "a directory whose parents do not exist extracts");
	OK (path_is_dir("a") && path_is_dir("a/b") && path_is_dir("a/b/c"),
	    "and every parent along the way is created");
}

/* an entry naming nothing leaves only the root, which has no last component
 * to strip - the case that used to index the octet before the buffer */
static void test_empty_name (void)
{
	hio_uint8_t ar[BLK * 3];

	memset(ar, 0, sizeof(ar));
	/* the name is left empty while the rest of the block is not, so this is
	 * not the all-zero block that ends an archive */
	make_hdr (&ar[0], "", HIO_TAR_DIRTYPE, "00000000000");

	reset_root ();

	/* whether this is accepted or refused is not the point - it must not
	 * reach outside the buffer either way */
	extract (ar, sizeof(ar));
	OK (1, "a directory entry with no name is handled without leaving the buffer");
}

/* a file of no length occupies no content blocks. treating the next header as
 * its content swallows that header and reads every later entry at the wrong
 * offset - which turns file content into entries of its own. */
static void test_zero_length_file (void)
{
	hio_uint8_t ar[BLK * 5];

	memset(ar, 0, sizeof(ar));
	make_hdr (&ar[0], "empty.txt", HIO_TAR_REGTYPE, "00000000000");
	make_hdr (&ar[BLK], "second.txt", HIO_TAR_REGTYPE, "00000000005");
	memcpy(&ar[BLK * 2], "HELLO", 5);
	/* blocks 4 and 5 are the end-of-archive marker */

	reset_root ();

	OK (extract(ar, sizeof(ar)) == 0, "an archive holding a zero-length file extracts");
	OK (file_size("empty.txt") == 0, "the zero-length file is created, and is empty");
	OK (file_size("second.txt") == 5, "the entry after it is read as an entry, not as its content");

	/* the content of the second file must not have been taken for a header.
	 * a file named after those octets is what that mistake leaves behind. */
	OK (file_size("HELLO") == -1, "and that entry's content is not taken for another entry");
}

/* several in a row, so that the state really does return to where a header is
 * expected each time rather than happening to line up once */
static void test_several_zero_length_files (void)
{
	hio_uint8_t ar[BLK * 6];

	memset(ar, 0, sizeof(ar));
	make_hdr (&ar[0], "z1", HIO_TAR_REGTYPE, "00000000000");
	make_hdr (&ar[BLK], "z2", HIO_TAR_REGTYPE, "00000000000");
	make_hdr (&ar[BLK * 2], "z3", HIO_TAR_REGTYPE, "00000000000");
	make_hdr (&ar[BLK * 3], "last", HIO_TAR_REGTYPE, "00000000003");
	memcpy(&ar[BLK * 4], "END", 3);

	reset_root ();

	OK (extract(ar, sizeof(ar)) == 0, "an archive of several zero-length files extracts");
	OK (file_size("z1") == 0 && file_size("z2") == 0 && file_size("z3") == 0,
	    "each of them is created");
	OK (file_size("last") == 3, "and the entry following them is still read correctly");
}

/* the octets may arrive in any sized pieces, because a request body does */
static void test_feed_in_pieces (void)
{
	hio_uint8_t ar[BLK * 5];
	hio_oow_t step;
	int ok_all = 1;

	memset(ar, 0, sizeof(ar));
	make_hdr (&ar[0], "empty.txt", HIO_TAR_REGTYPE, "00000000000");
	make_hdr (&ar[BLK], "second.txt", HIO_TAR_REGTYPE, "00000000005");
	memcpy(&ar[BLK * 2], "HELLO", 5);

	for (step = 1; step <= 1000; step += 167)
	{
		hio_tar_t* tar;
		hio_oow_t off;
		int ok;

		reset_root ();

		tar = hio_tar_open(g_hio, 0);
		if (!tar) { ok_all = 0; break; }
		hio_tar_setxrootwithbcstr (tar, g_root);

		ok = 1;
		for (off = 0; off < sizeof(ar); off += step)
		{
			hio_oow_t n = sizeof(ar) - off;
			if (n > step) n = step;
			if (hio_tar_xfeed(tar, &ar[off], n) <= -1) { ok = 0; break; }
		}
		if (ok && hio_tar_endxfeed(tar) <= -1) ok = 0;
		hio_tar_close (tar);

		if (!ok || file_size("empty.txt") != 0 || file_size("second.txt") != 5) ok_all = 0;
		if (!ok_all) break;
	}

	OK (ok_all, "the same archive extracts the same way whatever sizes it arrives in");
}

/* ------------------------------------------------------------------ */

int main (void)
{
	hio_errinf_t errinf;

	no_plan ();

	g_hio = hio_open(HIO_NULL, 0, HIO_NULL, HIO_FEATURE_ALL, 512, &errinf);
	if (!g_hio)
	{
		bail_out ("unable to open hio");
		return -1;
	}

	/* an absolute root, because that is what a document root is - and because
	 * the walk that strips a path apart only reaches the leading slash, where
	 * there is no component left to take, when there is one to reach */
	{
		hio_bch_t cwd[192];
		if (!getcwd(cwd, HIO_COUNTOF(cwd))) cwd[0] = '\0';
		snprintf (g_root, HIO_COUNTOF(g_root), "%s/t-029.root.%d", cwd, (int)getpid());
	}
	mkdir (g_root, 0755);

	test_existing_dir ();
	test_nested_dir ();
	test_empty_name ();
	test_zero_length_file ();
	test_several_zero_length_files ();
	test_feed_in_pieces ();

	rm_rf (g_root);
	hio_close (g_hio);

	return exit_status();
}
