/*
    Copyright (c) 2016-2020 Chung, Hyung-Hwan. All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions
    are met:
    1. Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.

    THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR
    IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
    OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
    IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
    INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
    NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
    THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _HIO_FIO_H_
#define _HIO_FIO_H_

#include <hio-cmn.h>

enum hio_fio_flag_t
{
	/* (1 << 0) to (1 << 7) reserved for hio_sio_flag_t. 
	 * see <hio/cmn/sio.h>. nerver use this value. */
	HIO_FIO_RESERVED      = 0xFF,

	/** treat the file name pointer as a handle pointer */
	HIO_FIO_HANDLE        = (1 << 8),

	/** treat the file name pointer as a pointer to file name
	 *  template to use when making a temporary file name */
	HIO_FIO_TEMPORARY     = (1 << 9),

	/** don't close an I/O handle in hio_fio_fini() and hio_fio_close() */
	HIO_FIO_NOCLOSE       = (1 << 10),

	/** treat the path name as a multi-byte string */
	HIO_FIO_BCSTRPATH    = (1 << 11), 

	/* normal open flags */
	HIO_FIO_READ          = (1 << 14),
	HIO_FIO_WRITE         = (1 << 15),
	HIO_FIO_APPEND        = (1 << 16),

	HIO_FIO_CREATE        = (1 << 17),
	HIO_FIO_TRUNCATE      = (1 << 18),
	HIO_FIO_EXCLUSIVE     = (1 << 19),
	HIO_FIO_SYNC          = (1 << 20),
	
	/* do not follow a symbolic link, only on a supported platform */
	HIO_FIO_NOFOLLOW      = (1 << 23),

	/* for WIN32 only. harmless(no effect) when used on other platforms */
	HIO_FIO_NOSHREAD      = (1 << 24),
	HIO_FIO_NOSHWRITE     = (1 << 25),
	HIO_FIO_NOSHDELETE    = (1 << 26),

	/* hints to OS. harmless(no effect) when used on unsupported platforms */
	HIO_FIO_RANDOM        = (1 << 27), /* hint that access be random */
	HIO_FIO_SEQUENTIAL    = (1 << 28)  /* hint that access is sequential */
};

enum hio_fio_std_t
{
	HIO_FIO_STDIN  = 0,
	HIO_FIO_STDOUT = 1,
	HIO_FIO_STDERR = 2
};
typedef enum hio_fio_std_t hio_fio_std_t;

/* seek origin */
enum hio_fio_ori_t
{
	HIO_FIO_BEGIN   = 0,
	HIO_FIO_CURRENT = 1,
	HIO_FIO_END     = 2
};
/* file origin for seek */
typedef enum hio_fio_ori_t hio_fio_ori_t;

enum hio_fio_mode_t
{
	HIO_FIO_SUID = 04000, /* set UID */
	HIO_FIO_SGID = 02000, /* set GID */
	HIO_FIO_SVTX = 01000, /* sticky bit */
	HIO_FIO_RUSR = 00400, /* can be read by owner */
	HIO_FIO_WUSR = 00200, /* can be written by owner */
	HIO_FIO_XUSR = 00100, /* can be executed by owner */
	HIO_FIO_RGRP = 00040, /* can be read by group */
	HIO_FIO_WGRP = 00020, /* can be written by group */
	HIO_FIO_XGRP = 00010, /* can be executed by group */
	HIO_FIO_ROTH = 00004, /* can be read by others */
	HIO_FIO_WOTH = 00002, /* can be written by others */
	HIO_FIO_XOTH = 00001  /* can be executed by others */
};

#if defined(_WIN32)
	/* <winnt.h> => typedef PVOID HANDLE; */
	typedef void* hio_fio_hnd_t;
#elif defined(__OS2__)
	/* <os2def.h> => typedef LHANDLE HFILE;
	                 typedef unsigned long LHANDLE; */
	typedef unsigned long hio_fio_hnd_t;
#elif defined(__DOS__)
	typedef int hio_fio_hnd_t;
#elif defined(vms) || defined(__vms)
	typedef void* hio_fio_hnd_t; /* struct FAB*, struct RAB* */
#else
	typedef int hio_fio_hnd_t;
#endif

/* file offset */
typedef hio_foff_t hio_fio_off_t;

typedef struct hio_fio_t hio_fio_t;
typedef struct hio_fio_lck_t hio_fio_lck_t;

struct hio_fio_t
{
	hio_t*           gem;
	hio_fio_hnd_t    handle;
	int              status; 
};

struct hio_fio_lck_t
{
	int             type;   /* READ, WRITE */
	hio_fio_off_t  offset; /* starting offset */
	hio_fio_off_t  length; /* length */
	hio_fio_ori_t  origin; /* origin */
};

#define HIO_FIO_HANDLE(fio) ((fio)->handle)

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * The hio_fio_open() function opens a file.
 * To open a file, you should set the flags with at least one of
 * HIO_FIO_READ, HIO_FIO_WRITE, HIO_FIO_APPEND.
 *
 * If the #HIO_FIO_HANDLE flag is set, the \a path parameter is interpreted
 * as a pointer to hio_fio_hnd_t.
 *
 * If the #HIO_FIO_TEMPORARY flag is set, the \a path parameter is 
 * interpreted as a path name template and an actual file name to open
 * is internally generated using the template. The \a path parameter 
 * is filled with the last actual path name attempted when the function
 * returns. So, you must not pass a constant string to the \a path 
 * parameter when #HIO_FIO_TEMPORARY is set.
 */
HIO_EXPORT hio_fio_t* hio_fio_open (
	hio_gem_t*        gem,
	hio_oow_t         xtnsize,
	const hio_ooch_t* path,
	int               flags,
	int               mode
);

/**
 * The hio_fio_close() function closes a file.
 */
HIO_EXPORT void hio_fio_close (
	hio_fio_t* fio
);

/**
 * The hio_fio_close() function opens a file into \a fio.
 */
HIO_EXPORT int hio_fio_init (
	hio_fio_t*        fio,
	hio_gem_t*        gem,
	const hio_ooch_t* path,
	int               flags,
	int               mode
);

/**
 * The hio_fio_close() function finalizes a file by closing the handle 
 * stored in @a fio.
 */
HIO_EXPORT void hio_fio_fini (
	hio_fio_t* fio
);

#if defined(HIO_HAVE_INLINE)
static HIO_INLINE void* hio_fio_getxtn (hio_fio_t* fio) { return (void*)(fio + 1); }
#else
#define hio_fio_getxtn(fio) ((void*)((hio_fio_t*)(fio) + 1))
#endif

/**
 * The hio_fio_gethnd() function returns the native file handle.
 */
HIO_EXPORT hio_fio_hnd_t hio_fio_gethnd (
	const hio_fio_t* fio
);

/**
 * The hio_fio_seek() function changes the current file position.
 */
HIO_EXPORT hio_fio_off_t hio_fio_seek (
	hio_fio_t*    fio,
	hio_fio_off_t offset,
	hio_fio_ori_t origin
);

/**
 * The hio_fio_truncate() function truncates a file to @a size.
 */
HIO_EXPORT int hio_fio_truncate (
	hio_fio_t*    fio,
	hio_fio_off_t size
);

/**
 * The hio_fio_read() function reads data.
 */
HIO_EXPORT hio_ooi_t hio_fio_read (
	hio_fio_t*  fio,
	void*       buf,
	hio_oow_t   size
);

/**
 * The hio_fio_write() function writes data.
 */
HIO_EXPORT hio_ooi_t hio_fio_write (
	hio_fio_t*  fio,
	const void* data,
	hio_oow_t   size
);

/**
 * The hio_fio_chmod() function changes the file mode.
 *
 * \note
 * On _WIN32, this function is implemented on the best-effort basis and 
 * returns an error on the following conditions:
 * - The file size is 0.
 * - The file is opened without #HIO_FIO_READ.
 */
HIO_EXPORT int hio_fio_chmod (
	hio_fio_t* fio,
	int         mode
);

/**
 * The hio_fio_sync() function synchronizes file contents into storage media
 * It is useful in determining the media error, without which hio_fio_close() 
 * may succeed despite such an error.
 */
HIO_EXPORT int hio_fio_sync (
	hio_fio_t* fio
);

HIO_EXPORT int hio_fio_lock ( 
	hio_fio_t*     fio, 
	hio_fio_lck_t* lck,
	int            flags
);

HIO_EXPORT int hio_fio_unlock (
	hio_fio_t*     fio,
	hio_fio_lck_t* lck,
	int            flags
);

/**
 * The hio_get_std_fio_handle() returns a low-level system handle to
 * commonly used I/O channels.
 */
HIO_EXPORT int hio_get_std_fio_handle (
	hio_fio_std_t  std,
	hio_fio_hnd_t* hnd
);

#if defined(__cplusplus)
}
#endif

#endif
