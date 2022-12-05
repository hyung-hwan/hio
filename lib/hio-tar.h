#ifndef _HIO_TAR_H_
#define _HIO_TAR_H_

#include <hio.h>
#include <hio-ecs.h>


/* tar Header Block, from POSIX 1003.1-1990.  */
struct hio_tar_hdr_t
{                              /* byte offset */
	char name[100];               /*   0 */
	char mode[8];                 /* 100 */
	char uid[8];                  /* 108 */
	char gid[8];                  /* 116 */
	char size[12];                /* 124 - file size in an ascii octal string */
	char mtime[12];               /* 136 */
	char chksum[8];               /* 148 */
	char typeflag;                /* 156 */
	char linkname[100];           /* 157 */

	char magic[6];                /* 257 - ustar indicator */
	char version[2];              /* 263 - ustar version */
	char uname[32];               /* 265 */
	char gname[32];               /* 297 */
	char devmajor[8];             /* 329 */
	char devminor[8];             /* 337 */
	char prefix[155];             /* 345 */
	char padding[12];             /* 500 */
#if 0
	char *gnu_longname;
	char *gnu_longlink;
#endif
};

typedef struct hio_tar_hdr_t hio_tar_hdr_t;

#define HIO_TAR_MAGIC   "ustar"        /* ustar and a null */
#define HIO_TAR_MAGLEN  6
#define HIO_TAR_VERSION "00"           /* 00 and no null */
#define HIO_TAR_VERSLEN 2

/* Values used in typeflag field.  */
#define HIO_TAR_REGTYPE  '0'            /* regular file */
#define HIO_TAR_AREGTYPE '\0'           /* regular file */
#define HIO_TAR_LNKTYPE  '1'            /* link */
#define HIO_TAR_SYMTYPE  '2'            /* reserved */
#define HIO_TAR_CHRTYPE  '3'            /* character special */
#define HIO_TAR_BLKTYPE  '4'            /* block special */
#define HIO_TAR_DIRTYPE  '5'            /* directory */
#define HIO_TAR_FIFOTYPE '6'            /* FIFO special */
#define HIO_TAR_CONTTYPE '7'            /* reserved */

#define HIO_TAR_XHDTYPE  'x'            /* Extended header referring to the next file in the archive */
#define HIO_TAR_XGLTYPE  'g'            /* Global extended header */

/* Bits used in the mode field, values in octal.  */
#define HIO_TAR_SUID    04000          /* set UID on execution */
#define HIO_TAR_SGID    02000          /* set GID on execution */
#define HIO_TAR_SVTX    01000          /* reserved */

/* file permissions */
#define HIO_TAR_UREAD   00400          /* read by owner */
#define HIO_TAR_UWRITE  00200          /* write by owner */
#define HIO_TAR_UEXEC   00100          /* execute/search by owner */
#define HIO_TAR_GREAD   00040          /* read by group */
#define HIO_TAR_GWRITE  00020          /* write by group */
#define HIO_TAR_GEXEC   00010          /* execute/search by group */
#define HIO_TAR_OREAD   00004          /* read by other */
#define HIO_TAR_OWRITE  00002          /* write by other */
#define HIO_TAR_OEXEC   00001          /* execute/search by other */

#define HIO_TAR_BLKSIZE (512)

enum hio_tar_state_t
{
	HIO_TAR_STATE_START,
	HIO_TAR_STATE_FILE,
	HIO_TAR_STATE_END_1,
	HIO_TAR_STATE_END_2
};
typedef enum hio_tar_state_t hio_tar_state_t;

struct hio_tar_t
{
	hio_t* hio;

	hio_tar_state_t state;
	struct
	{
		hio_uint8_t buf[HIO_TAR_BLKSIZE];
		hio_oow_t len;
	} blk;

	struct
	{
		hio_uintmax_t filesize;
		hio_uintmax_t filemode;
		hio_becs_t filename;
		void* fp;
	} hi;
};
typedef struct hio_tar_t hio_tar_t;

#if defined(__cplusplus)
extern "C" {
#endif

HIO_EXPORT hio_tar_t* hio_tar_open (
	hio_t*    hio,
	hio_oow_t xtnsize
);

HIO_EXPORT void hio_tar_close (
	hio_tar_t* tar
);

HIO_EXPORT int hio_tar_init (
	hio_tar_t* tar,
	hio_t*     hio
);

HIO_EXPORT void hio_tar_fini (
	hio_tar_t* tar
);

#define hio_tar_endfeed(tar) hio_tar_feed(tar, HIO_NULL, 0)

HIO_EXPORT int hio_tar_feed (
	hio_tar_t*  tar,
	const void* ptr,
	hio_oow_t   len
);

#if defined(__cplusplus)
}
#endif

#endif
