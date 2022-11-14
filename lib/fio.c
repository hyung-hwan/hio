/*
    Copyright (c) 2010-2020 Chung, Hyung-Hwan. All rights reserved.

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

#include <hio-fio.h>
#include "hio-prv.h"
 
#if defined(_WIN32)
#	include <windows.h>
/*#	include <psapi.h>*/ /* for GetMappedFileName(). but dynamically loaded */
#	include <tchar.h>
#	if !defined(INVALID_SET_FILE_POINTER)
#		define INVALID_SET_FILE_POINTER ((DWORD)-1)
#	endif
#elif defined(__OS2__)
#	define INCL_DOSFILEMGR
#	define INCL_DOSMODULEMGR
#	define INCL_DOSPROCESS
#	define INCL_DOSERRORS
#	include <os2.h>
#elif defined(__DOS__)
#	include <io.h>
#	include <fcntl.h>
#	include <errno.h>
#elif defined(vms) || defined(__vms)
#	define __NEW_STARLET 1
#	include <starlet.h>
#	include <rms.h>
#else
#	include <unistd.h>
#   include <fcntl.h>
#   include <errno.h>
#endif

/* internal status codes */
enum
{
	STATUS_APPEND      = (1 << 0),
	STATUS_NOCLOSE     = (1 << 1),
	STATUS_WIN32_STDIN = (1 << 2)
};

#if defined(_WIN32)

typedef DWORD WINAPI (*getmappedfilename_t) (
	HANDLE hProcess,
	LPVOID lpv,
	LPTSTR lpFilename,
	DWORD nSize
);

#elif defined(__OS2__)

#if defined(__WATCOMC__) && (__WATCOMC__ < 1200) && !defined(LONGLONG_INCLUDED)
typedef struct _LONGLONG {
	ULONG ulLo;
	LONG  ulHi;
} LONGLONG, *PLONGLONG;

typedef struct _ULONGLONG {
	ULONG ulLo;
	ULONG ulHi;
} ULONGLONG, *PULONGLONG;
#endif

typedef APIRET APIENTRY (*dosopenl_t) (
	PSZ pszFileName,
	PHFILE pHf,
	PULONG pulAction,
	LONGLONG cbFile,
	ULONG ulAttribute,
	ULONG fsOpenFlags,
	ULONG fsOpenMode,
	PEAOP2 peaop2
);

typedef APIRET APIENTRY (*dossetfileptrl_t) (
	HFILE hFile,
	LONGLONG ib,
	ULONG method,
	PLONGLONG ibActual
);

typedef APIRET APIENTRY (*dossetfilesizel_t) (
	HFILE hFile,
	LONGLONG cbSize
);

static int dos_set = 0;
static dosopenl_t dos_open_l = HIO_NULL;
static dossetfileptrl_t dos_set_file_ptr_l = HIO_NULL;
static dossetfilesizel_t dos_set_file_size_l = HIO_NULL;

#endif

hio_fio_t* hio_fio_open (hio_gem_t* gem, hio_oow_t xtnsize, const hio_ooch_t* path, int flags, int mode)
{
	hio_fio_t* fio;

	fio = (hio_fio_t*)hio_gem_allocmem(gem, HIO_SIZEOF(hio_fio_t) + xtnsize);
	if (fio)
	{
		if (hio_fio_init(fio, gem, path, flags, mode) <= -1)
		{
			hio_gem_freemem (gem, fio);
			return HIO_NULL;
		}
		else HIO_MEMSET (fio + 1, 0, xtnsize);
	}
	return fio;
}

void hio_fio_close (hio_fio_t* fio)
{
	hio_fio_fini (fio);
	hio_gem_freemem (fio->gem, fio);
}

int hio_fio_init (hio_fio_t* fio, hio_gem_t* gem, const hio_ooch_t* path, int flags, int mode)
{
	hio_fio_hnd_t handle;

	hio_uint32_t temp_no;
	hio_ooch_t* temp_ptr;
	hio_bch_t* temp_ptr_b;
	hio_oow_t temp_tries;

#if defined(_WIN32)
	int fellback = 0;
#endif

#if defined(__OS2__)
	if (!dos_set)
	{
		DosEnterCritSec ();
		if (!dos_set)
		{
			HMODULE mod;
			if (DosLoadModule(NULL, 0, "DOSCALL1", &mod) == NO_ERROR)
			{
				/* look up routines by ordinal */
				DosQueryProcAddr (mod, 981, NULL, (PFN*)&dos_open_l);
				DosQueryProcAddr (mod, 988, NULL, (PFN*)&dos_set_file_ptr_l);
				DosQueryProcAddr (mod, 989, NULL, (PFN*)&dos_set_file_size_l);
			}

			dos_set = 1;
		}
		DosExitCritSec ();
	}
#endif

	HIO_MEMSET (fio, 0, HIO_SIZEOF(*fio));
	fio->gem = gem;

	if (!(flags & (HIO_FIO_READ | HIO_FIO_WRITE | HIO_FIO_APPEND | HIO_FIO_HANDLE)))
	{
		/* one of HIO_FIO_READ, HIO_FIO_WRITE, HIO_FIO_APPEND, 
		 * and HIO_FIO_HANDLE must be specified */
		hio_gem_seterrnum (fio->gem, HIO_NULL, HIO_EINVAL);
		return -1;
	}

	/* Store some flags for later use */
	if (flags & HIO_FIO_NOCLOSE) 
		fio->status |= STATUS_NOCLOSE;

	if (flags & HIO_FIO_TEMPORARY)
	{
		hio_ntime_t now;

		/*if (flags & (HIO_FIO_HANDLE | HIO_FIO_BCSTRPATH))*/
		if (flags & HIO_FIO_HANDLE)
		{
			/* HIO_FIO_TEMPORARY and HIO_FIO_HANDLE/HIO_FIO_BCSTRPATH 
			 * are mutually exclusive */
			hio_gem_seterrnum (fio->gem, HIO_NULL, HIO_EINVAL);
			return -1;
		}

		temp_no = 0;

		if (flags & HIO_FIO_BCSTRPATH)
		{

			for (temp_ptr_b = (hio_bch_t*)path; *temp_ptr_b; temp_ptr_b++) 
				temp_no += *temp_ptr_b;

			if (temp_ptr_b - (hio_bch_t*)path < 4)
			{
				hio_gem_seterrnum (fio->gem, HIO_NULL, HIO_EINVAL);
				return -1; 
			}

			temp_ptr_b -= 4;
		}
		else
		{
			/* if HIO_FIO_TEMPORARY is used, the path name must be writable. */
			for (temp_ptr = (hio_ooch_t*)path; *temp_ptr; temp_ptr++) 
				temp_no += *temp_ptr;

			/* The path name template must be at least 4 characters long
			 * excluding the terminating null. this function fails if not */
			if (temp_ptr - path < 4) 
			{
				hio_gem_seterrnum (fio->gem, HIO_NULL, HIO_EINVAL);
				return -1; 
			}

			temp_ptr -= 4;
		}

		hio_get_ntime (&now);
		temp_no += (now.sec & 0xFFFFFFFFlu);

		temp_tries = 0;

	retry_temporary:
		temp_tries++;

		/* Fails after 5000 tries. 5000 randomly chosen */
		if (temp_tries > 5000) 
		{
			hio_gem_seterrnum (fio->gem, HIO_NULL, HIO_EINVAL);
			return -1; 
		}

		/* Generate the next random number to use to make a 
		 * new path name */
		temp_no = hio_rand31(temp_no);

		/* 
		 * You must not pass a constant string for a path name
		 * when HIO_FIO_TEMPORARY is set, because it changes
		 * the path name with a random number generated
		 */
		if (flags & HIO_FIO_BCSTRPATH)
		{
			hio_fmt_uintmax_to_bcstr (
				temp_ptr_b,
				4, 
				temp_no % 0x10000, 
				16 | HIO_FMT_UINTMAX_NOTRUNC | HIO_FMT_UINTMAX_NONULL,
				4,
				'\0',
				HIO_NULL
			);
		}
		else
		{
			hio_fmt_uintmax_to_oocstr (
				temp_ptr,
				4, 
				temp_no % 0x10000, 
				16 | HIO_FMT_UINTMAX_NOTRUNC | HIO_FMT_UINTMAX_NONULL,
				4,
				HIO_T('\0'),
				HIO_NULL
			);
		}
	}
	
#if defined(_WIN32)
	if (flags & HIO_FIO_HANDLE)
	{
		handle = *(hio_fio_hnd_t*)path;
		/* do not specify an invalid handle value */
		/*HIO_ASSERT (hio, handle != INVALID_HANDLE_VALUE);*/

		if (handle == GetStdHandle (STD_INPUT_HANDLE))
			fio->status |= STATUS_WIN32_STDIN;
	}
	else
	{
		DWORD desired_access = 0;
		DWORD share_mode = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
		DWORD creation_disposition = 0;
		DWORD flag_and_attr = FILE_ATTRIBUTE_NORMAL;

		if (fellback) share_mode &= ~FILE_SHARE_DELETE;

		if (flags & HIO_FIO_APPEND)
		{
			if (fellback)
			{
				desired_access |= GENERIC_WRITE;
			}
			else
			{
				/* this is not officially documented for CreateFile.
				 * ZwCreateFile (kernel) seems to document it */
				fio->status &= ~STATUS_APPEND;
				desired_access |= FILE_APPEND_DATA;
			}
		}
		else if (flags & HIO_FIO_WRITE)
		{
			/* In WIN32, FILE_APPEND_DATA and GENERIC_WRITE can't
			 * be used together */
			desired_access |= GENERIC_WRITE;
		}
		if (flags & HIO_FIO_READ) desired_access |= GENERIC_READ;

		if (flags & HIO_FIO_CREATE)
		{
			creation_disposition =
				(flags & HIO_FIO_EXCLUSIVE)? CREATE_NEW:
				(flags & HIO_FIO_TRUNCATE)? CREATE_ALWAYS: OPEN_ALWAYS;
		}
		else if (flags & HIO_FIO_TRUNCATE)
		{
			creation_disposition = TRUNCATE_EXISTING;
		}
		else creation_disposition = OPEN_EXISTING;

		if (flags & HIO_FIO_NOSHREAD)
			share_mode &= ~FILE_SHARE_READ;
		if (flags & HIO_FIO_NOSHWRITE)
			share_mode &= ~FILE_SHARE_WRITE;
		if (flags & HIO_FIO_NOSHDELETE)
			share_mode &= ~FILE_SHARE_DELETE;

		if (!(mode & HIO_FIO_WUSR)) 
			flag_and_attr = FILE_ATTRIBUTE_READONLY;
		if (flags & HIO_FIO_SYNC) 
			flag_and_attr |= FILE_FLAG_WRITE_THROUGH;

	#if defined(FILE_FLAG_OPEN_REPARSE_POINT)
		if (flags & HIO_FIO_NOFOLLOW)
			flag_and_attr |= FILE_FLAG_OPEN_REPARSE_POINT;
	#endif

		/* these two are just hints to OS */
		if (flags & HIO_FIO_RANDOM) 
			flag_and_attr |= FILE_FLAG_RANDOM_ACCESS;
		if (flags & HIO_FIO_SEQUENTIAL) 
			flag_and_attr |= FILE_FLAG_SEQUENTIAL_SCAN;

		if (flags & HIO_FIO_BCSTRPATH)
		{
			handle = CreateFileA(
				(const hio_bch_t*)path, desired_access, share_mode, 
				HIO_NULL, /* set noinherit by setting no secattr */
				creation_disposition, flag_and_attr, 0
			);
		}
		else
		{
			handle = CreateFile(
				path, desired_access, share_mode, 
				HIO_NULL, /* set noinherit by setting no secattr */
				creation_disposition, flag_and_attr, 0
			);
		}
		if (handle == INVALID_HANDLE_VALUE) 
		{
			DWORD e = GetLastError();
			if (!fellback && e == ERROR_INVALID_PARAMETER && 
			    ((share_mode & FILE_SHARE_DELETE) || (flags & HIO_FIO_APPEND)))
			{
				/* old windows fails with ERROR_INVALID_PARAMETER
				 * when some advanced flags are used. so try again
				 * with fallback flags */
				fellback = 1;

				share_mode &= ~FILE_SHARE_DELETE;
				if (flags & HIO_FIO_APPEND)
				{
					fio->status |= STATUS_APPEND;
					desired_access &= ~FILE_APPEND_DATA;
					desired_access |= GENERIC_WRITE;
				}
			
				if (flags & HIO_FIO_BCSTRPATH)
				{
					handle = CreateFileA(
						(const hio_bch_t*)path, desired_access, share_mode, 
						HIO_NULL, /* set noinherit by setting no secattr */
						creation_disposition, flag_and_attr, 0
					);
				}
				else
				{
					handle = CreateFile(
						path, desired_access, share_mode, 
						HIO_NULL, /* set noinherit by setting no secattr */
						creation_disposition, flag_and_attr, 0
					);
				}
				if (handle == INVALID_HANDLE_VALUE) 
				{
				i	if (flags & HIO_FIO_TEMPORARY) goto retry_temporary;
					hio_gem_seterrnum (fio->gem, HIO_NULL, hio_syserr_to_errnum(GetLastError()));
					return -1;
				}
			}
			else
			{
				if (flags & HIO_FIO_TEMPORARY) goto retry_temporary;
				hio_gem_seterrnum (fio->gem, HIO_NULL, hio_syserr_to_errnum(e));
				return -1;
			}
		}
	}

	/* some special check */
#if 0
	if (GetFileType(handle) == FILE_TYPE_UNKNOWN)
	{
		hio_gem_seterrnum (fio->gem, HIO_NULL, hio_syserr_to_errnum(GetLastError()));
		CloseHandle (handle);
		return -1;
	}
#endif

	/* TODO: support more features on WIN32 - TEMPORARY, DELETE_ON_CLOSE */

#elif defined(__OS2__)

	if (flags & HIO_FIO_HANDLE)
	{
		handle = *(hio_fio_hnd_t*)path;
	}
	else
	{
		APIRET ret;
		ULONG action_taken = 0;
		ULONG open_action, open_mode, open_attr;

	#if defined(HIO_OOCH_IS_BCH)
		const hio_bch_t* path_mb = path;
	#else
		hio_bch_t path_mb_buf[CCHMAXPATH];
		hio_bch_t* path_mb;
		hio_oow_t wl, ml;
		int px;

		if (flags & HIO_FIO_BCSTRPATH)
		{
			path_mb = (hio_bch_t*)path;
		}
		else
		{
			path_mb = path_mb_buf;
			ml = HIO_COUNTOF(path_mb_buf);
			px = hio_gem_convutobcstr(fio->gem, path, &wl, path_mb, &ml);
			if (px == -2)
			{
				/* the static buffer is too small.
				 * dynamically allocate a buffer */
				path_mb = hio_gem_duputobcstr(fio->gem, path, HIO_NUL);
				if (path_mb == HIO_NULL) return -1;
			}
			else if (px <= -1) 
			{
				return -1;
			}
		}
	#endif

		if (flags & HIO_FIO_APPEND) fio->status |= STATUS_APPEND;

		if (flags & HIO_FIO_CREATE)
		{
			if (flags & HIO_FIO_EXCLUSIVE)
			{
				open_action = OPEN_ACTION_FAIL_IF_EXISTS | OPEN_ACTION_CREATE_IF_NEW;
			}
			else if (flags & HIO_FIO_TRUNCATE)
			{
				open_action = OPEN_ACTION_REPLACE_IF_EXISTS | OPEN_ACTION_CREATE_IF_NEW;
			}
			else
			{
				open_action = OPEN_ACTION_CREATE_IF_NEW | OPEN_ACTION_OPEN_IF_EXISTS;
			}
		}
		else if (flags & HIO_FIO_TRUNCATE)
		{
			open_action = OPEN_ACTION_REPLACE_IF_EXISTS | OPEN_ACTION_FAIL_IF_NEW;
		}
		else 
		{
			open_action = OPEN_ACTION_OPEN_IF_EXISTS | OPEN_ACTION_FAIL_IF_NEW;
		}

		open_mode = OPEN_FLAGS_NOINHERIT;

		if (flags & HIO_FIO_SYNC) open_mode |= OPEN_FLAGS_WRITE_THROUGH;

		if ((flags & HIO_FIO_NOSHREAD) && (flags & HIO_FIO_NOSHWRITE)) open_mode |= OPEN_SHARE_DENYREADWRITE;
		else if (flags & HIO_FIO_NOSHREAD) open_mode |= OPEN_SHARE_DENYREAD;
		else if (flags & HIO_FIO_NOSHWRITE) open_mode |= OPEN_SHARE_DENYWRITE;
		else open_mode |= OPEN_SHARE_DENYNONE;

		if ((flags & HIO_FIO_READ) && (flags & HIO_FIO_WRITE)) open_mode |= OPEN_ACCESS_READWRITE;
		else if (flags & HIO_FIO_READ) open_mode |= OPEN_ACCESS_READONLY;
		else if (flags & HIO_FIO_WRITE) open_mode |= OPEN_ACCESS_WRITEONLY;

		open_attr = (mode & HIO_FIO_WUSR)? FILE_NORMAL: FILE_READONLY;
		
	#if defined(FIL_STANDARDL)
		if (dos_open_l)
		{
			LONGLONG zero;

			zero.ulLo = 0;
			zero.ulHi = 0;
			ret = dos_open_l (
				path_mb,       /* file name */
				&handle,       /* file handle */
				&action_taken, /* store action taken */
				zero,          /* size */
				open_attr,     /* attribute */
				open_action,   /* action if it exists */
				open_mode,     /* open mode */
				0L                            
			);
		}
		else
		{
	#endif
			ret = DosOpen (
				path_mb,       /* file name */
				&handle,       /* file handle */
				&action_taken, /* store action taken */
				0,             /* size */
				open_attr,     /* attribute */
				open_action,   /* action if it exists */
				open_mode,     /* open mode */
				0L                            
			);
	#if defined(FIL_STANDARDL)
		}
	#endif

	#if defined(HIO_OOCH_IS_BCH)
		/* nothing to do */
	#else
		if (path_mb != path_mb_buf) hio_gem_freemem (fio->gem, path_mb);
	#endif

		if (ret != NO_ERROR) 
		{
			if (flags & HIO_FIO_TEMPORARY) goto retry_temporary;
			hio_gem_seterrnum (fio->gem, HIO_NULL, hio_syserr_to_errnum(ret));
			return -1;
		}
	}

#elif defined(__DOS__)

	if (flags & HIO_FIO_HANDLE)
	{
		handle = *(hio_fio_hnd_t*)path;
		/* do not specify an invalid handle value */
		/*HIO_ASSERT (hio, handle >= 0);*/
	}
	else
	{
		int oflags = 0;
		int permission = 0;

	#if defined(HIO_OOCH_IS_BCH)
		const hio_bch_t* path_mb = path;
	#else
		hio_bch_t path_mb_buf[_MAX_PATH];
		hio_bch_t* path_mb;
		hio_oow_t wl, ml;
		int px;

		if (flags & HIO_FIO_BCSTRPATH)
		{
			path_mb = (hio_bch_t*)path;
		}
		else
		{
			path_mb = path_mb_buf;
			ml = HIO_COUNTOF(path_mb_buf);
			px = hio_gem_convutobcstr(fio->gem, path, &wl, path_mb, &ml);
			if (px == -2)
			{
				/* static buffer size not enough. 
				 * switch to dynamic allocation */
				path_mb = hio_gem_duputobcstr(fio->gem, path, HIO_NULL);
				if (path_mb == HIO_NULL)  return -1;
			}
			else if (px <= -1) 
			{
				return -1;
			}
		}
	#endif

		if (flags & HIO_FIO_APPEND)
		{
			if ((flags & HIO_FIO_READ)) oflags |= O_RDWR;
			else oflags |= O_WRONLY;
			oflags |= O_APPEND;
		}
		else
		{
			if ((flags & HIO_FIO_READ) &&
			    (flags & HIO_FIO_WRITE)) oflags |= O_RDWR;
			else if (flags & HIO_FIO_READ) oflags |= O_RDONLY;
			else if (flags & HIO_FIO_WRITE) oflags |= O_WRONLY;
		}

		if (flags & HIO_FIO_CREATE) oflags |= O_CREAT;
		if (flags & HIO_FIO_TRUNCATE) oflags |= O_TRUNC;
		if (flags & HIO_FIO_EXCLUSIVE) oflags |= O_EXCL;

		oflags |= O_BINARY | O_NOINHERIT;
		
		if (mode & HIO_FIO_RUSR) permission |= S_IREAD;
		if (mode & HIO_FIO_WUSR) permission |= S_IWRITE;

		handle = open (
			path_mb,
			oflags,
			permission
		);

	#if defined(HIO_OOCH_IS_BCH)
		/* nothing to do */
	#else
		if (path_mb != path_mb_buf) hio_gem_freemem (fio->gem, path_mb);
	#endif

		if (handle <= -1) 
		{
			if (flags & HIO_FIO_TEMPORARY) goto retry_temporary;
			hio_gem_seterrnum (fio->gem, HIO_NULL, hio_syserr_to_errnum(errno));
			return -1;
		}
	}

#elif defined(vms) || defined(__vms)

	if (flags & HIO_FIO_HANDLE)
	{
		/* TODO: implement this */
		hio_gem_seterrnum (fio->gem, HIO_NULL, HIO_ENOIMPL);
		return -1;
	}
	else
	{
		struct FAB* fab;
		struct RAB* rab;
		unsigned long r0;
		
	#if defined(HIO_OOCH_IS_BCH)
		const hio_bch_t* path_mb = path;
	#else
		hio_bch_t path_mb_buf[1024];
		hio_bch_t* path_mb;
		hio_oow_t wl, ml;
		int px;

		if (flags & HIO_FIO_BCSTRPATH)
		{
			path_mb = (hio_bch_t*)path;
		}
		else
		{
			path_mb = path_mb_buf;
			ml = HIO_COUNTOF(path_mb_buf);
			px = hio_convutobcstr(fio->gem, path, &wl, path_mb, &ml);
			if (px == -2)
			{
				/* the static buffer is too small.
				 * allocate a buffer */
				path_mb = hio_duputobcstr(fio->gem, path, mmgr);
				if (path_mb == HIO_NULL) return -1;
			}
			else if (px <= -1) 
			{
				return -1;
			}
		}
	#endif

		rab = (struct RAB*)hio_gem_allocmem(fio->gem, HIO_SIZEOF(*rab) + HIO_SIZEOF(*fab));
		if (rab == HIO_NULL)
		{
	#if defined(HIO_OOCH_IS_BCH)
			/* nothing to do */
	#else
			if (path_mb != path_mb_buf) hio_gem_freemem (fio->gem, path_mb);
	#endif
			return -1;
		}

		fab = (struct FAB*)(rab + 1);
		*rab = cc$rms_rab;
		rab->rab$l_fab = fab;

		*fab = cc$rms_fab;
		fab->fab$l_fna = path_mb;
		fab->fab$b_fns = strlen(path_mb);
		fab->fab$b_org = FAB$C_SEQ;
		fab->fab$b_rfm = FAB$C_VAR; /* FAB$C_STM, FAB$C_STMLF, FAB$C_VAR, etc... */
		fab->fab$b_fac = FAB$M_GET | FAB$M_PUT;

		fab->fab$b_fac = FAB$M_NIL;
		if (flags & HIO_FIO_READ) fab->fab$b_fac |= FAB$M_GET;
		if (flags & (HIO_FIO_WRITE | HIO_FIO_APPEND)) fab->fab$b_fac |= FAB$M_PUT | FAB$M_TRN; /* put, truncate */
		
		fab->fab$b_shr |= FAB$M_SHRPUT | FAB$M_SHRGET; /* FAB$M_NIL */
		if (flags & HIO_FIO_NOSHREAD) fab->fab$b_shr &= ~FAB$M_SHRGET;
		if (flags & HIO_FIO_NOSHWRITE) fab->fab$b_shr &= ~FAB$M_SHRPUT;

		if (flags & HIO_FIO_APPEND) rab->rab$l_rop |= RAB$M_EOF;

		if (flags & HIO_FIO_CREATE) 
		{
			if (flags & HIO_FIO_EXCLUSIVE) 
				fab->fab$l_fop &= ~FAB$M_CIF;
			else
				fab->fab$l_fop |= FAB$M_CIF;

			r0 = sys$create (&fab, 0, 0);
		}
		else
		{
			r0 = sys$open (&fab, 0, 0);
		}

		if (r0 != RMS$_NORMAL && r0 != RMS$_CREATED)
		{
	#if defined(HIO_OOCH_IS_BCH)
			/* nothing to do */
	#else
			if (path_mb != path_mb_buf) hio_gem_freemem (fio->gem, path_mb);
	#endif
			hio_gem_seterrnum (fio->gem, HIO_NULL, hio_syserr_to_errnum(r0));
			return -1;
		}


		r0 = sys$connect (&rab, 0, 0);
		if (r0 != RMS$_NORMAL)
		{
	#if defined(HIO_OOCH_IS_BCH)
			/* nothing to do */
	#else
			if (path_mb != path_mb_buf) hio_gem_freemem (fio->gem, path_mb);
	#endif
			hio_gem_seterrnum (fio->gem, HIO_NULL, hio_syserr_to_errnum(r0));
			return -1;
		}

	#if defined(HIO_OOCH_IS_BCH)
		/* nothing to do */
	#else
		if (path_mb != path_mb_buf) hio_gem_freemem (fio->gem, path_mb);
	#endif

		handle = rab;
	}

#else

	if (flags & HIO_FIO_HANDLE)
	{
		handle = *(hio_fio_hnd_t*)path;
		/* do not specify an invalid handle value */
		/*HIO_ASSERT (hio, handle >= 0);*/
	}
	else
	{
		int desired_access = 0;

	#if defined(HIO_OOCH_IS_BCH)
		const hio_bch_t* path_mb = path;
	#else
		hio_bch_t path_mb_buf[1024];  /* PATH_MAX instead? */
		hio_bch_t* path_mb;
		hio_oow_t wl, ml;
		int px;

		if (flags & HIO_FIO_BCSTRPATH)
		{
			path_mb = (hio_bch_t*)path;
		}
		else
		{
			path_mb = path_mb_buf;
			ml = HIO_COUNTOF(path_mb_buf);
			px = hio_conv_ucstr_to_bcstr_with_cmgr(path, &wl, path_mb, &ml, fio->gem->cmgr);
			if (px == -2)
			{
				/* the static buffer is too small.
				 * allocate a buffer */
				path_mb = hio_gem_duputobcstr(fio->gem, path, HIO_NULL);
				if (path_mb == HIO_NULL) return -1;
			}
			else if (px <= -1) 
			{
				hio_gem_seterrnum (fio->gem, HIO_NULL, HIO_EINVAL);
				return -1;
			}
		}
	#endif
		/*
		 * rwa -> RDWR   | APPEND
		 * ra  -> RDWR   | APPEND
		 * wa  -> WRONLY | APPEND
		 * a   -> WRONLY | APPEND
		 */
		if (flags & HIO_FIO_APPEND)
		{
			if ((flags & HIO_FIO_READ)) desired_access |= O_RDWR;
			else desired_access |= O_WRONLY;
			desired_access |= O_APPEND;
		}
		else
		{
			if ((flags & HIO_FIO_READ) &&
			    (flags & HIO_FIO_WRITE)) desired_access |= O_RDWR;
			else if (flags & HIO_FIO_READ) desired_access |= O_RDONLY;
			else if (flags & HIO_FIO_WRITE) desired_access |= O_WRONLY;
		}

		if (flags & HIO_FIO_CREATE) desired_access |= O_CREAT;
		if (flags & HIO_FIO_TRUNCATE) desired_access |= O_TRUNC;
		if (flags & HIO_FIO_EXCLUSIVE) desired_access |= O_EXCL;
	#if defined(O_SYNC)
		if (flags & HIO_FIO_SYNC) desired_access |= O_SYNC;
	#endif

	#if defined(O_NOFOLLOW)
		if (flags & HIO_FIO_NOFOLLOW) desired_access |= O_NOFOLLOW;
	#endif

	#if defined(O_LARGEFILE)
		desired_access |= O_LARGEFILE;
	#endif
	#if defined(O_CLOEXEC)
		desired_access |= O_CLOEXEC; /* no inherit */
	#endif

		handle = HIO_OPEN(path_mb, desired_access, mode);

	#if defined(HIO_OOCH_IS_BCH)
		/* nothing to do */
	#else
		if (path_mb != path_mb_buf && path_mb != (hio_bch_t*)path) 
		{
			hio_gem_freemem (fio->gem, path_mb);
		}
	#endif
		if (handle == -1) 
		{
			if (flags & HIO_FIO_TEMPORARY) goto retry_temporary;
			hio_gem_seterrnum (fio->gem, HIO_NULL, hio_syserr_to_errnum(errno));
			return -1;
		}
		else
		{
		#if !defined(O_CLOEXEC) && defined(FD_CLOEXEC)
			int flag = fcntl(handle, F_GETFD);
			if (flag >= 0) fcntl (handle, F_SETFD, flag | FD_CLOEXEC);
		#endif
		}

		/* set some file access hints */
	#if defined(POSIX_FADV_RANDOM)
		if (flags & HIO_FIO_RANDOM) 
			posix_fadvise (handle, 0, 0, POSIX_FADV_RANDOM);
	#endif
	#if defined(POSIX_FADV_SEQUENTIAL)
		if (flags & HIO_FIO_SEQUENTIAL) 
			posix_fadvise (handle, 0, 0, POSIX_FADV_SEQUENTIAL);
	#endif
	}
#endif

	fio->handle = handle;
	return 0;
}

void hio_fio_fini (hio_fio_t* fio)
{
	if (!(fio->status & STATUS_NOCLOSE))
	{
#if defined(_WIN32)
		CloseHandle (fio->handle);

#elif defined(__OS2__)
		DosClose (fio->handle);

#elif defined(__DOS__)
		close (fio->handle);

#elif defined(vms) || defined(__vms)
		struct RAB* rab = (struct RAB*)fio->handle;
		sys$disconnect (rab, 0, 0);
		sys$close ((struct FAB*)(rab + 1), 0, 0);
		hio_gem_freemem (fio->gem, fio->handle);
#else
		close (fio->handle);
#endif
	}
}

hio_fio_hnd_t hio_fio_gethnd (const hio_fio_t* fio)
{
	return fio->handle;
}

hio_fio_off_t hio_fio_seek (hio_fio_t* fio, hio_fio_off_t offset, hio_fio_ori_t origin)
{
#if defined(_WIN32)
	static int seek_map[] =
	{
		FILE_BEGIN,
		FILE_CURRENT,
		FILE_END
	};
	LARGE_INTEGER x;
	#if defined(_WIN64)
	LARGE_INTEGER y;
	#endif

	/* HIO_ASSERT (fio->hio, HIO_SIZEOF(offset) <= HIO_SIZEOF(x.QuadPart));*/

	#if defined(_WIN64)
	x.QuadPart = offset;
	if (SetFilePointerEx (fio->handle, x, &y, seek_map[origin]) == FALSE)
	{
		return (hio_fio_off_t)-1;
	}
	return (hio_fio_off_t)y.QuadPart;
	#else

	/* SetFilePointerEx is not available on Windows NT 4.
	 * So let's use SetFilePointer */
	x.QuadPart = offset;
	x.LowPart = SetFilePointer (
		fio->handle, x.LowPart, &x.HighPart, seek_map[origin]);
	if (x.LowPart == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR)
	{
		hio_gem_seterrnum (fio->gem, HIO_NULL, hio_syserr_to_errnum(GetLastError()));
		return (hio_fio_off_t)-1;
	}
	return (hio_fio_off_t)x.QuadPart;
	#endif

#elif defined(__OS2__)
	static int seek_map[] =
	{
		FILE_BEGIN,
		FILE_CURRENT,
		FILE_END
	};

	#if defined(FIL_STANDARDL)
	if (dos_set_file_ptr_l)
	{
		LONGLONG pos, newpos;
		APIRET ret;

		/*HIO_ASSERT (fio->hio, HIO_SIZEOF(offset) >= HIO_SIZEOF(pos));*/

		pos.ulLo = (ULONG)(offset&0xFFFFFFFFlu);
		pos.ulHi = (ULONG)(offset>>32);

		ret = dos_set_file_ptr_l (fio->handle, pos, seek_map[origin], &newpos);
		if (ret != NO_ERROR) 
		{
			hio_gem_seterrnum (fio->gem, HIO_NULL, hio_syserr_to_errnum(ret));
			return (hio_fio_off_t)-1;
		}

		return ((hio_fio_off_t)newpos.ulHi << 32) | newpos.ulLo;
	}
	else
	{
	#endif
		ULONG newpos;
		APIRET ret;

		ret = DosSetFilePtr (fio->handle, offset, seek_map[origin], &newpos);
		if (ret != NO_ERROR) 
		{
			hio_gem_seterrnum (fio->gem, HIO_NULL, hio_syserr_to_errnum(ret));
			return (hio_fio_off_t)-1;
		}

		return newpos;
	#if defined(FIL_STANDARDL)
	}
	#endif

#elif defined(__DOS__)
	static int seek_map[] =
	{
		SEEK_SET,                    
		SEEK_CUR,
		SEEK_END
	};

	return lseek (fio->handle, offset, seek_map[origin]);
#elif defined(vms) || defined(__vms)

	/* TODO: */
	hio_gem_seterrnum (fio->gem, HIO_NULL, HIO_ENOIMPL);
	return (hio_fio_off_t)-1;
#else
	static int seek_map[] =
	{
		SEEK_SET,                    
		SEEK_CUR,
		SEEK_END
	};

#if defined(HIO_LLSEEK)
	loff_t tmp;

	if (HIO_LLSEEK(fio->handle,
		(unsigned long)(offset>>32),
		(unsigned long)(offset&0xFFFFFFFFlu),
		&tmp,
		seek_map[origin]) == -1)
	{
		hio_gem_seterrnum (fio->gem, HIO_NULL, hio_syserr_to_errnum(errno));
		return (hio_fio_off_t)-1;
	}

	return (hio_fio_off_t)tmp;
#else
	return lseek(fio->handle, offset, seek_map[origin]);
#endif

#endif
}

int hio_fio_truncate (hio_fio_t* fio, hio_fio_off_t size)
{
#if defined(_WIN32)
	if (hio_fio_seek (fio, size, HIO_FIO_BEGIN) == (hio_fio_off_t)-1) return -1;
	if (SetEndOfFile(fio->handle) == FALSE) 
	{
		hio_gem_seterrnum (fio->gem, HIO_NULL, hio_syserr_to_errnum(GetLastError()));
		return -1;
	}
	return 0;
#elif defined(__OS2__)

	APIRET ret;

	#if defined(FIL_STANDARDL)
	if (dos_set_file_size_l)
	{
		LONGLONG sz;
		/* the file must have the write access for it to succeed */

		sz.ulLo = (ULONG)(size&0xFFFFFFFFlu);
		sz.ulHi = (ULONG)(size>>32);

		ret = dos_set_file_size_l (fio->handle, sz);
	}
	else
	{
	#endif
		ret = DosSetFileSize (fio->handle, size);
	#if defined(FIL_STANDARDL)
	}
	#endif

	if (ret != NO_ERROR)
	{
		hio_gem_seterrnum (fio->gem, HIO_NULL, hio_syserr_to_errnum(ret));
		return -1;
	}
	return 0;

#elif defined(__DOS__)

	int n;
	n = chsize (fio->handle, size);
	if (n <= -1) hio_gem_seterrnum (fio->gem, HIO_NULL, hio_syserr_to_errnum(errno));
	return n;

#elif defined(vms) || defined(__vms)

	unsigned long r0;
	struct RAB* rab = (struct RAB*)fio->handle;
	
	if ((r0 = sys$rewind (rab, 0, 0)) != RMS$_NORMAL ||
	    (r0 = sys$truncate (rab, 0, 0)) != RMS$_NORMAL)
	{
		hio_gem_seterrnum (fio->gem, HIO_NULL, hio_syserr_to_errnum(r0));
		return -1;
	}

	return 0;

#else

	int n;
	n = ftruncate(fio->handle, size);
	if (n <= -1) hio_gem_seterrnum (fio->gem, HIO_NULL, hio_syserr_to_errnum(errno));
	return n;

#endif
}

hio_ooi_t hio_fio_read (hio_fio_t* fio, void* buf, hio_oow_t size)
{
#if defined(_WIN32)

	DWORD count;

	if (size > (HIO_TYPE_MAX(hio_ooi_t) & HIO_TYPE_MAX(DWORD))) 
		size = HIO_TYPE_MAX(hio_ooi_t) & HIO_TYPE_MAX(DWORD);
	if (ReadFile (fio->handle, buf, (DWORD)size, &count, HIO_NULL) == FALSE)
	{
		DWORD e = GetLastError();
		/* special case when ReadFile returns failure with ERROR_BROKEN_PIPE.
		 * this happens when an anonymous pipe is a standard input for redirection.
		 * assuming that ERROR_BROKEN_PIPE doesn't occur with normal 
		 * input streams, i treat the condition as a normal EOF indicator. */
		if ((fio->status & STATUS_WIN32_STDIN) && e == ERROR_BROKEN_PIPE) return 0;
		hio_gem_seterrnum (fio->gem, HIO_NULL, hio_syserr_to_errnum(e));
		return -1;
	}
	return (hio_ooi_t)count;

#elif defined(__OS2__)

	APIRET ret;
	ULONG count;
	if (size > (HIO_TYPE_MAX(hio_ooi_t) & HIO_TYPE_MAX(ULONG))) 
		size = HIO_TYPE_MAX(hio_ooi_t) & HIO_TYPE_MAX(ULONG);
	ret = DosRead (fio->handle, buf, (ULONG)size, &count);
	if (ret != NO_ERROR) 
	{
		hio_gem_seterrnum (fio->gem, HIO_NULL, hio_syserr_to_errnum(ret));
		return -1;
	}
	return (hio_ooi_t)count;

#elif defined(__DOS__)

	int n;
	if (size > (HIO_TYPE_MAX(hio_ooi_t) & HIO_TYPE_MAX(unsigned int))) 
		size = HIO_TYPE_MAX(hio_ooi_t) & HIO_TYPE_MAX(unsigned int);
	n = read (fio->handle, buf, size);
	if (n <= -1) hio_gem_seterrnum (fio->gem, HIO_NULL, hio_syserr_to_errnum(errno));
	return n;

#elif defined(vms) || defined(__vms)

	unsigned long r0;
	struct RAB* rab = (struct RAB*)fio->handle;

	if (size > 32767) size = 32767;

	rab->rab$l_ubf = buf;
	rab->rab$w_usz = size;

	r0 = sys$get (rab, 0, 0);
	if (r0 != RMS$_NORMAL)
	{
		hio_gem_seterrnum (fio->gem, HIO_NULL, hio_syserr_to_errnum(r0));
		return -1;
	}

	return rab->rab$w_rsz;
#else

	hio_ooi_t n;
	if (size > HIO_TYPE_MAX(hio_ooi_t)) size = HIO_TYPE_MAX(hio_ooi_t);
	n = read(fio->handle, buf, size);
	if (n <= -1) hio_gem_seterrnum (fio->gem, HIO_NULL, hio_syserr_to_errnum(errno));
	return n;
#endif
}

hio_ooi_t hio_fio_write (hio_fio_t* fio, const void* data, hio_oow_t size)
{
#if defined(_WIN32)

	DWORD count;

   	if (fio->status & STATUS_APPEND)
	{
/* TODO: only when FILE_APPEND_DATA failed???  how do i know this??? */
		/* i do this on a best-effort basis */
	#if defined(_WIN64)
		LARGE_INTEGER x;
		x.QuadPart = 0;
        SetFilePointerEx (fio->handle, x, HIO_NULL, FILE_END);
	#else
        SetFilePointer (fio->handle, 0, HIO_NULL, FILE_END);
	#endif
    }

	if (size > (HIO_TYPE_MAX(hio_ooi_t) & HIO_TYPE_MAX(DWORD))) 
		size = HIO_TYPE_MAX(hio_ooi_t) & HIO_TYPE_MAX(DWORD);
	if (WriteFile(fio->handle, data, (DWORD)size, &count, HIO_NULL) == FALSE) 
	{
		hio_gem_seterrnum (fio->gem, HIO_NULL, hio_syserr_to_errnum(GetLastError()));
		return -1;
	}
	return (hio_ooi_t)count;

#elif defined(__OS2__)

	APIRET ret;
	ULONG count;

   	if (fio->status & STATUS_APPEND)
	{
		/* i do this on a best-effort basis */
	#if defined(FIL_STANDARDL)
		if (dos_set_file_ptr_l)
		{
			LONGLONG pos, newpos;
			pos.ulLo = (ULONG)0;
			pos.ulHi = (ULONG)0;
            dos_set_file_ptr_l (fio->handle, pos, FILE_END, &newpos);
		}
		else
		{
	#endif
			ULONG newpos;
            DosSetFilePtr (fio->handle, 0, FILE_END, &newpos);
	#if defined(FIL_STANDARDL)
		}
	#endif
    	}

	if (size > (HIO_TYPE_MAX(hio_ooi_t) & HIO_TYPE_MAX(ULONG))) 
		size = HIO_TYPE_MAX(hio_ooi_t) & HIO_TYPE_MAX(ULONG);
	ret = DosWrite(fio->handle, (PVOID)data, (ULONG)size, &count);
	if (ret != NO_ERROR) 
	{
		hio_gem_seterrnum (fio->gem, HIO_NULL, hio_syserr_to_errnum(ret));
		return -1;
	}
	return (hio_ooi_t)count;

#elif defined(__DOS__)

	int n;
	if (size > (HIO_TYPE_MAX(hio_ooi_t) & HIO_TYPE_MAX(unsigned int))) 
		size = HIO_TYPE_MAX(hio_ooi_t) & HIO_TYPE_MAX(unsigned int);
	n = write(fio->handle, data, size);
	if (n <= -1) hio_gem_seterrnum (fio->gem, HIO_NULL, hio_syserr_to_errnum(errno));
	return n;

#elif defined(vms) || defined(__vms)

	unsigned long r0;
	struct RAB* rab = (struct RAB*)fio->handle;

	if (size > 32767) size = 32767;

	rab->rab$l_rbf = (char*)data;
	rab->rab$w_rsz = size;

	r0 = sys$put(rab, 0, 0);
	if (r0 != RMS$_NORMAL)
	{
		hio_gem_seterrnum (fio->gem, HIO_NULL, hio_syserr_to_errnum(r0));
		return -1;
	}

	return rab->rab$w_rsz;

#else

	hio_ooi_t n;
	if (size > HIO_TYPE_MAX(hio_ooi_t)) size = HIO_TYPE_MAX(hio_ooi_t);
	n = write(fio->handle, data, size);
	if (n <= -1) hio_gem_seterrnum (fio->gem, HIO_NULL, hio_syserr_to_errnum(errno));
	return n;
#endif
}

#if defined(_WIN32)

static int get_devname_from_handle (
	hio_fio_t* fio, hio_ooch_t* buf, hio_oow_t len) 
{
	HANDLE map = NULL;
	void* mem = NULL;
	DWORD olen;
	HINSTANCE psapi;
	getmappedfilename_t getmappedfilename;

	/* try to load psapi.dll dynamially for 
	 * systems without it. direct linking to the library
	 * may end up with dependency failure on such systems. 
	 * this way, the worst case is that this function simply 
	 * fails. */
	psapi = LoadLibrary (HIO_T("PSAPI.DLL"));
	if (!psapi)
	{
		hio_gem_seterrnum (fio->gem, HIO_NULL, hio_syserr_to_errnum(GetLastError()));
		return -1;
	}

	getmappedfilename = (getmappedfilename_t) 
		GetProcAddress (psapi, HIO_BT("GetMappedFileName"));
	if (!getmappedfilename)
	{
		hio_gem_seterrnum (fio->gem, HIO_NULL, hio_syserr_to_errnum(GetLastError()));
		FreeLibrary (psapi);
		return -1;
	}

	/* create a file mapping object */
	map = CreateFileMapping (
		fio->handle, 
		NULL,
		PAGE_READONLY,
		0, 
		1,
		NULL
	);
	if (map == NULL) 
	{
		hio_gem_seterrnum (fio->gem, HIO_NULL, hio_syserr_to_errnum(GetLastError()));
		FreeLibrary (psapi);
		return -1;
	}	

	/* create a file mapping to get the file name. */
	mem = MapViewOfFile (map, FILE_MAP_READ, 0, 0, 1);
	if (mem == NULL)
	{
		hio_gem_seterrnum (fio->gem, HIO_NULL, hio_syserr_to_errnum(GetLastError()));
		CloseHandle (map);
		FreeLibrary (psapi);
		return -1;
	}

	olen = getmappedfilename (GetCurrentProcess(), mem, buf, len); 
	if (olen == 0)
	{
		hio_gem_seterrnum (fio->gem, HIO_NULL, hio_syserr_to_errnum(GetLastError()));
		UnmapViewOfFile (mem);
		CloseHandle (map);
		FreeLibrary (psapi);
		return -1;
	}

	UnmapViewOfFile (mem);
	CloseHandle (map);
	FreeLibrary (psapi);
	return 0;
}

static int get_volname_from_handle (hio_fio_t* fio, hio_ooch_t* buf, hio_oow_t len) 
{
	if (get_devname_from_handle (fio, buf, len) == -1) return -1;

	if (hio_comp_oocstr_limited(buf, HIO_T("\\Device\\LanmanRedirector\\"), 25, 1) == 0)
	{
		/*buf[0] = HIO_T('\\');*/
		hio_copy_oocstr_unlimited (&buf[1], &buf[24]);
	}
	else
	{
		DWORD n;
		hio_ooch_t drives[128];

		n = GetLogicalDriveStrings(HIO_COUNTOF(drives), drives);

		if (n == 0 /* error */ || 
		    n > HIO_COUNTOF(drives) /* buffer small */) 
		{
			hio_gem_seterrnum (fio->gem, HIO_NULL, hio_syserr_to_errnum(GetLastError()));
			return -1;
		}

		while (n > 0)
		{
			hio_ooch_t drv[3];
			hio_ooch_t path[MAX_PATH];

			drv[0] = drives[--n];
			drv[1] = HIO_T(':');
			drv[2] = HIO_T('\0');
			if (QueryDosDevice (drv, path, HIO_COUNTOF(path)))
			{
				hio_oow_t pl = hio_count_oocstr(path);
				hio_oow_t bl = hio_count_oocstr(buf);
				if (bl > pl && buf[pl] == HIO_T('\\') &&
				    hio_comp_oochars(buf, pl, path, pl, 1) == 0)
				{
					buf[0] = drv[0];
					buf[1] = HIO_T(':');
					hio_copy_oocstr_unlimited (&buf[2], &buf[pl]);
					break;
				}
			}
		}
	}
	
	/* if the match is not found, the device name is returned
	 * without translation */
	return 0;
}
#endif

int hio_fio_chmod (hio_fio_t* fio, int mode)
{
#if defined(_WIN32)

	int flags = FILE_ATTRIBUTE_NORMAL;
	hio_ooch_t name[MAX_PATH];

	/* it is a best effort implementation. if the file size is 0,
	 * it can't even get the file name from the handle and thus fails. 
	 * if GENERIC_READ is not set in CreateFile, CreateFileMapping fails. 
	 * so if this fio is opened without HIO_FIO_READ, this function fails.
	 */
	if (get_volname_from_handle(fio, name, HIO_COUNTOF(name)) == -1) return -1;

	if (!(mode & HIO_FIO_WUSR)) flags = FILE_ATTRIBUTE_READONLY;
	if (SetFileAttributes(name, flags) == FALSE)
	{
		hio_gem_seterrnum (fio->gem, HIO_NULL, hio_syserr_to_errnum(GetLastError()));
		return -1;
	}
	return 0;

#elif defined(__OS2__)

	APIRET n;
	int flags = FILE_NORMAL;
	#if defined(FIL_STANDARDL)
	FILESTATUS3L stat;
	#else
	FILESTATUS3 stat;
	#endif
	ULONG size = HIO_SIZEOF(stat);

	#if defined(FIL_STANDARDL)
	n = DosQueryFileInfo(fio->handle, FIL_STANDARDL, &stat, size);
	#else
	n = DosQueryFileInfo(fio->handle, FIL_STANDARD, &stat, size);
	#endif
	if (n != NO_ERROR)
	{
		hio_gem_seterrnum (fio->gem, HIO_NULL, hio_syserr_to_errnum(n));
		return -1;
	}

	if (!(mode & HIO_FIO_WUSR)) flags = FILE_READONLY;
	
	stat.attrFile = flags;
	#if defined(FIL_STANDARDL)
	n = DosSetFileInfo(fio->handle, FIL_STANDARDL, &stat, size);
	#else
	n = DosSetFileInfo(fio->handle, FIL_STANDARD, &stat, size);
	#endif
	if (n != NO_ERROR)
	{
		hio_gem_seterrnum (fio->gem, HIO_NULL, hio_syserr_to_errnum(n));
		return -1;
	}

	return 0;

#elif defined(__DOS__)

	int permission = 0;

	if (mode & HIO_FIO_RUSR) permission |= S_IREAD;
	if (mode & HIO_FIO_WUSR) permission |= S_IWRITE;

	/* TODO: fchmod not available. find a way to do this
	return fchmod (fio->handle, permission); */

	hio_gem_seterrnum (fio->gem, HIO_NULL, HIO_ENOIMPL);
	return -1;

#elif defined(vms) || defined(__vms)

	/* TODO: */
	hio_gem_seterrnum (fio->gem, HIO_NULL, HIO_ENOIMPL);
	return (hio_fio_off_t)-1;

#elif defined(HAVE_FCHMOD)
	int n;
	n = fchmod(fio->handle, mode);
	if (n <= -1) hio_gem_seterrnum (fio->gem, HIO_NULL, hio_syserr_to_errnum(errno));
	return n;

#else
	hio_gem_seterrnum (fio->gem, HIO_NULL, HIO_ENOIMPL);
	return -1;

#endif
}

int hio_fio_sync (hio_fio_t* fio)
{
#if defined(_WIN32)

	if (FlushFileBuffers(fio->handle) == FALSE)
	{
		hio_gem_seterrnum (fio->gem, HIO_NULL, hio_syserr_to_errnum(GetLastError()));
		return -1;
	}
	return 0;

#elif defined(__OS2__)

	APIRET n;
	n = DosResetBuffer(fio->handle); 
	if (n != NO_ERROR)
	{
		hio_gem_seterrnum (fio->gem, HIO_NULL, hio_syserr_to_errnum(n));
		return -1;
	}
	return 0;

#elif defined(__DOS__)

	int n;
	n = fsync(fio->handle);
	if (n <= -1) hio_gem_seterrnum (fio->gem, HIO_NULL, hio_syserr_to_errnum(errno));
	return n;

#elif defined(vms) || defined(__vms)

	/* TODO: */
	hio_gem_seterrnum (fio->gem, HIO_NULL, HIO_ENOIMPL);
	return (hio_fio_off_t)-1;

#elif defined(HAVE_FSYNC)

	int n;
	n = fsync(fio->handle);
	if (n <= -1) hio_gem_seterrnum (fio->gem, HIO_NULL, hio_syserr_to_errnum(errno));
	return n;
#else
	hio_gem_seterrnum (fio->gem, HIO_NULL, HIO_ENOIMPL);
	return -1;
#endif
}

int hio_fio_lock (hio_fio_t* fio, hio_fio_lck_t* lck, int flags)
{
	/* TODO: hio_fio_lock 
	 * struct flock fl;
	 * fl.l_type = F_RDLCK, F_WRLCK;
	 * HIO_FCNTL (fio->handle, F_SETLK, &fl);
	 */
	hio_gem_seterrnum (fio->gem, HIO_NULL, HIO_ENOIMPL);
	return -1;
}

int hio_fio_unlock (hio_fio_t* fio, hio_fio_lck_t* lck, int flags)
{
	/* TODO: hio_fio_unlock 
	 * struct flock fl;
	 * fl.l_type = F_UNLCK;
	 * HIO_FCNTL (fio->handle, F_SETLK, &fl);
	 */
	hio_gem_seterrnum (fio->gem, HIO_NULL, HIO_ENOIMPL);
	return -1;
}

int hio_get_std_fio_handle (hio_fio_std_t std, hio_fio_hnd_t* hnd)
{
#if defined(_WIN32)
	static DWORD tab[] =
	{
		STD_INPUT_HANDLE,
		STD_OUTPUT_HANDLE,
		STD_ERROR_HANDLE
	};
#elif defined(vms) || defined(__vms)
	/* TODO */
	static int tab[] = { 0, 1, 2 };
#else

	static hio_fio_hnd_t tab[] =
	{
#if defined(__OS2__)
		(HFILE)0, (HFILE)1, (HFILE)2
#elif defined(__DOS__)
		0, 1, 2
#else
		0, 1, 2
#endif
	};

#endif

	if (std < 0 || std >= HIO_COUNTOF(tab)) return -1;

#if defined(_WIN32)
	{
		HANDLE tmp = GetStdHandle(tab[std]);
		if (tmp == INVALID_HANDLE_VALUE) return -1;
		*hnd = tmp;
	}
#elif defined(vms) || defined(__vms)
	/* TODO: */
	return -1;
#else
	*hnd = tab[std];
#endif
	return 0;
}
