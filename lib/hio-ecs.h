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

#ifndef _HIO_ECS_H_
#define _HIO_ECS_H_

#include <hio.h>
#include <stdarg.h>

/** string pointer and length as a aggregate */
#define HIO_BECS_BCS(s)      (&((s)->val))
#define HIO_BECS_CS(s)       (&((s)->val))
/** string length */
#define HIO_BECS_LEN(s)      ((s)->val.len)
/** string pointer */
#define HIO_BECS_PTR(s)      ((s)->val.ptr)
/** pointer to a particular position */
#define HIO_BECS_CPTR(s,idx) (&(s)->val.ptr[idx])
/** string capacity */
#define HIO_BECS_CAPA(s)     ((s)->capa)
/** character at the given position */
#define HIO_BECS_CHAR(s,idx) ((s)->val.ptr[idx])
/**< last character. unsafe if length <= 0 */
#define HIO_BECS_LASTCHAR(s) ((s)->val.ptr[(s)->val.len-1])

/** string pointer and length as a aggregate */
#define HIO_UECS_UCS(s)      (&((s)->val))
#define HIO_UECS_CS(s )      (&((s)->val))
/** string length */
#define HIO_UECS_LEN(s)      ((s)->val.len)
/** string pointer */
#define HIO_UECS_PTR(s)      ((s)->val.ptr)
/** pointer to a particular position */
#define HIO_UECS_CPTR(s,idx) (&(s)->val.ptr[idx])
/** string capacity */
#define HIO_UECS_CAPA(s)     ((s)->capa)
/** character at the given position */
#define HIO_UECS_CHAR(s,idx) ((s)->val.ptr[idx])
/**< last character. unsafe if length <= 0 */
#define HIO_UECS_LASTCHAR(s) ((s)->val.ptr[(s)->val.len-1])

/*
 * defined in hio-cmn.h
typedef struct hio_becs_t hio_becs_t;
typedef struct hio_uecs_t hio_uecs_t;
*/

typedef hio_oow_t (*hio_becs_sizer_t) (
	hio_becs_t* data,
	hio_oow_t   hint
);

typedef hio_oow_t (*hio_uecs_sizer_t) (
	hio_uecs_t* data,
	hio_oow_t   hint
);

#if defined(HIO_OOCH_IS_UCH)
#	define HIO_OOECS_OOCS(s)     HIO_UECS_UCS(s)
#	define HIO_OOECS_LEN(s)      HIO_UECS_LEN(s)
#	define HIO_OOECS_PTR(s)      HIO_UECS_PTR(s)
#	define HIO_OOECS_CPTR(s,idx) HIO_UECS_CPTR(s,idx)
#	define HIO_OOECS_CAPA(s)     HIO_UECS_CAPA(s)
#	define HIO_OOECS_CHAR(s,idx) HIO_UECS_CHAR(s,idx)
#	define HIO_OOECS_LASTCHAR(s) HIO_UECS_LASTCHAR(s)
#	define hio_ooecs_t           hio_uecs_t
#	define hio_ooecs_sizer_t     hio_uecs_sizer_t
#else
#	define HIO_OOECS_OOCS(s)     HIO_BECS_BCS(s)
#	define HIO_OOECS_LEN(s)      HIO_BECS_LEN(s)
#	define HIO_OOECS_PTR(s)      HIO_BECS_PTR(s)
#	define HIO_OOECS_CPTR(s,idx) HIO_BECS_CPTR(s,idx)
#	define HIO_OOECS_CAPA(s)     HIO_BECS_CAPA(s)
#	define HIO_OOECS_CHAR(s,idx) HIO_BECS_CHAR(s,idx)
#	define HIO_OOECS_LASTCHAR(s) HIO_BECS_LASTCHAR(s)
#	define hio_ooecs_t           hio_becs_t
#	define hio_ooecs_sizer_t     hio_becs_sizer_t
#endif


/**
 * The hio_becs_t type defines a dynamically resizable multi-byte string.
 */
struct hio_becs_t
{
	hio_t*           hio;
	hio_becs_sizer_t sizer; /**< buffer resizer function */
	hio_bcs_t        val;   /**< buffer/string pointer and lengh */
	hio_oow_t        capa;  /**< buffer capacity */
};

/**
 * The hio_uecs_t type defines a dynamically resizable wide-character string.
 */
struct hio_uecs_t
{
	hio_t*           hio;
	hio_uecs_sizer_t sizer; /**< buffer resizer function */
	hio_ucs_t        val;   /**< buffer/string pointer and lengh */
	hio_oow_t        capa;  /**< buffer capacity */
};


#if defined(__cplusplus)
extern "C" {
#endif

/**
 * The hio_becs_open() function creates a dynamically resizable multibyte string.
 */
HIO_EXPORT hio_becs_t* hio_becs_open (
	hio_t*     hio,
	hio_oow_t  xtnsize,
	hio_oow_t  capa
);

HIO_EXPORT void hio_becs_close (
	hio_becs_t* becs
);

/**
 * The hio_becs_init() function initializes a dynamically resizable string
 * If the parameter capa is 0, it doesn't allocate the internal buffer
 * in advance and always succeeds.
 * \return 0 on success, -1 on failure.
 */
HIO_EXPORT int hio_becs_init (
	hio_becs_t*  becs,
	hio_t*       hio,
	hio_oow_t    capa
);

/**
 * The hio_becs_fini() function finalizes a dynamically resizable string.
 */
HIO_EXPORT void hio_becs_fini (
	hio_becs_t* becs
);

#if defined(HIO_HAVE_INLINE)
static HIO_INLINE void* hio_becs_getxtn (hio_becs_t* becs) { return (void*)(becs + 1); }
#else
#define hio_becs_getxtn(becs) ((void*)((hio_becs_t*)(becs) + 1))
#endif

/**
 * The hio_becs_yield() function assigns the buffer to an variable of the
 * #hio_bcs_t type and recreate a new buffer of the \a new_capa capacity.
 * The function fails if it fails to allocate a new buffer.
 * \return 0 on success, and -1 on failure.
 */
HIO_EXPORT int hio_becs_yield (
	hio_becs_t*  becs,    /**< string */
	hio_bcs_t*   buf,    /**< buffer pointer */
	hio_oow_t    newcapa /**< new capacity */
);

HIO_EXPORT hio_bch_t* hio_becs_yieldptr (
	hio_becs_t*   becs,    /**< string */
	hio_oow_t     newcapa /**< new capacity */
);

/**
 * The hio_becs_getsizer() function gets the sizer.
 * \return sizer function set or HIO_NULL if no sizer is set.
 */
#if defined(HIO_HAVE_INLINE)
static HIO_INLINE hio_becs_sizer_t hio_becs_getsizer (hio_becs_t* becs) { return becs->sizer; }
#else
#	define hio_becs_getsizer(becs) ((becs)->sizer)
#endif

/**
 * The hio_becs_setsizer() function specify a new sizer for a dynamic string.
 * With no sizer specified, the dynamic string doubles the current buffer
 * when it needs to increase its size. The sizer function is passed a dynamic
 * string and the minimum capacity required to hold data after resizing.
 * The string is truncated if the sizer function returns a smaller number
 * than the hint passed.
 */
#if defined(HIO_HAVE_INLINE)
static HIO_INLINE void hio_becs_setsizer (hio_becs_t* becs, hio_becs_sizer_t sizer) { becs->sizer = sizer; }
#else
#	define hio_becs_setsizer(becs,sizerfn) ((becs)->sizer = (sizerfn))
#endif

/**
 * The hio_becs_getcapa() function returns the current capacity.
 * You may use HIO_STR_CAPA(str) macro for performance sake.
 * \return current capacity in number of characters.
 */
#if defined(HIO_HAVE_INLINE)
static HIO_INLINE hio_oow_t hio_becs_getcapa (hio_becs_t* becs) { return HIO_BECS_CAPA(becs); }
#else
#	define hio_becs_getcapa(becs) HIO_BECS_CAPA(becs)
#endif

/**
 * The hio_becs_setcapa() function sets the new capacity. If the new capacity
 * is smaller than the old, the overflowing characters are removed from
 * from the buffer.
 * \return (hio_oow_t)-1 on failure, new capacity on success
 */
HIO_EXPORT hio_oow_t hio_becs_setcapa (
	hio_becs_t* becs,
	hio_oow_t   capa
);

/**
 * The hio_becs_getlen() function return the string length.
 */
#if defined(HIO_HAVE_INLINE)
static HIO_INLINE hio_oow_t hio_becs_getlen (hio_becs_t* becs) { return HIO_BECS_LEN(becs); }
#else
#	define hio_becs_getlen(becs) HIO_BECS_LEN(becs)
#endif

/**
 * The hio_becs_setlen() function changes the string length.
 * \return (hio_oow_t)-1 on failure, new length on success
 */
HIO_EXPORT hio_oow_t hio_becs_setlen (
	hio_becs_t* becs,
	hio_oow_t   len
);

/**
 * The hio_becs_clear() funtion deletes all characters in a string and sets
 * the length to 0. It doesn't resize the internal buffer.
 */
HIO_EXPORT void hio_becs_clear (
	hio_becs_t* becs
);

/**
 * The hio_becs_swap() function exchanges the pointers to a buffer between
 * two strings. It updates the length and the capacity accordingly.
 */
HIO_EXPORT void hio_becs_swap (
	hio_becs_t* becs1,
	hio_becs_t* becs2
);

HIO_EXPORT hio_oow_t hio_becs_cpy (
	hio_becs_t*      becs,
	const hio_bch_t* s
);

HIO_EXPORT hio_oow_t hio_becs_ncpy (
	hio_becs_t*       becs,
	const hio_bch_t*  s,
	hio_oow_t         len
);

HIO_EXPORT hio_oow_t hio_becs_cat (
	hio_becs_t*      becs,
	const hio_bch_t* s
);

HIO_EXPORT hio_oow_t hio_becs_ncat (
	hio_becs_t*      becs,
	const hio_bch_t* s,
	hio_oow_t        len
);

HIO_EXPORT hio_oow_t hio_becs_nrcat (
	hio_becs_t*      becs,
	const hio_bch_t* s,
	hio_oow_t        len
);

HIO_EXPORT hio_oow_t hio_becs_ccat (
	hio_becs_t*  becs,
	hio_bch_t    c
);

HIO_EXPORT hio_oow_t hio_becs_nccat (
	hio_becs_t*  becs,
	hio_bch_t    c,
	hio_oow_t    len
);

HIO_EXPORT hio_oow_t hio_becs_del (
	hio_becs_t* becs,
	hio_oow_t   index,
	hio_oow_t   size
);

HIO_EXPORT hio_oow_t hio_becs_amend (
	hio_becs_t*      becs,
	hio_oow_t        index,
	hio_oow_t        size,
	const hio_bch_t* repl
);

HIO_EXPORT hio_oow_t hio_becs_vfcat (
	hio_becs_t*       str,
	const hio_bch_t*  fmt,
	va_list            ap
);

HIO_EXPORT hio_oow_t hio_becs_fcat (
	hio_becs_t*       str,
	const hio_bch_t*  fmt,
	...
);

HIO_EXPORT hio_oow_t hio_becs_vfmt (
	hio_becs_t*       str,
	const hio_bch_t*  fmt,
	va_list            ap
);

HIO_EXPORT hio_oow_t hio_becs_fmt (
	hio_becs_t*       str,
	const hio_bch_t*  fmt,
	...
);

/* ------------------------------------------------------------------------ */

/**
 * The hio_uecs_open() function creates a dynamically resizable multibyte string.
 */
HIO_EXPORT hio_uecs_t* hio_uecs_open (
	hio_t*     hio,
	hio_oow_t  xtnsize,
	hio_oow_t  capa
);

HIO_EXPORT void hio_uecs_close (
	hio_uecs_t* uecs
);

/**
 * The hio_uecs_init() function initializes a dynamically resizable string
 * If the parameter capa is 0, it doesn't allocate the internal buffer
 * in advance and always succeeds.
 * \return 0 on success, -1 on failure.
 */
HIO_EXPORT int hio_uecs_init (
	hio_uecs_t*  uecs,
	hio_t*       hio,
	hio_oow_t    capa
);

/**
 * The hio_uecs_fini() function finalizes a dynamically resizable string.
 */
HIO_EXPORT void hio_uecs_fini (
	hio_uecs_t* uecs
);

#if defined(HIO_HAVE_INLINE)
static HIO_INLINE void* hio_uecs_getxtn (hio_uecs_t* uecs) { return (void*)(uecs + 1); }
#else
#define hio_uecs_getxtn(uecs) ((void*)((hio_uecs_t*)(uecs) + 1))
#endif

/**
 * The hio_uecs_yield() function assigns the buffer to an variable of the
 * #hio_ucs_t type and recreate a new buffer of the \a new_capa capacity.
 * The function fails if it fails to allocate a new buffer.
 * \return 0 on success, and -1 on failure.
 */
HIO_EXPORT int hio_uecs_yield (
	hio_uecs_t*   uecs,    /**< string */
	hio_ucs_t*    buf,    /**< buffer pointer */
	hio_oow_t     newcapa /**< new capacity */
);

HIO_EXPORT hio_uch_t* hio_uecs_yieldptr (
	hio_uecs_t*   uecs,    /**< string */
	hio_oow_t     newcapa /**< new capacity */
);

/**
 * The hio_uecs_getsizer() function gets the sizer.
 * \return sizer function set or HIO_NULL if no sizer is set.
 */
#if defined(HIO_HAVE_INLINE)
static HIO_INLINE hio_uecs_sizer_t hio_uecs_getsizer (hio_uecs_t* uecs) { return uecs->sizer; }
#else
#	define hio_uecs_getsizer(uecs) ((uecs)->sizer)
#endif

/**
 * The hio_uecs_setsizer() function specify a new sizer for a dynamic string.
 * With no sizer specified, the dynamic string doubles the current buffer
 * when it needs to increase its size. The sizer function is passed a dynamic
 * string and the minimum capacity required to hold data after resizing.
 * The string is truncated if the sizer function returns a smaller number
 * than the hint passed.
 */
#if defined(HIO_HAVE_INLINE)
static HIO_INLINE void hio_uecs_setsizer (hio_uecs_t* uecs, hio_uecs_sizer_t sizer) { uecs->sizer = sizer; }
#else
#	define hio_uecs_setsizer(uecs,sizerfn) ((uecs)->sizer = (sizerfn))
#endif

/**
 * The hio_uecs_getcapa() function returns the current capacity.
 * You may use HIO_STR_CAPA(str) macro for performance sake.
 * \return current capacity in number of characters.
 */
#if defined(HIO_HAVE_INLINE)
static HIO_INLINE hio_oow_t hio_uecs_getcapa (hio_uecs_t* uecs) { return HIO_UECS_CAPA(uecs); }
#else
#	define hio_uecs_getcapa(uecs) HIO_UECS_CAPA(uecs)
#endif

/**
 * The hio_uecs_setcapa() function sets the new capacity. If the new capacity
 * is smaller than the old, the overflowing characters are removed from
 * from the buffer.
 * \return (hio_oow_t)-1 on failure, new capacity on success
 */
HIO_EXPORT hio_oow_t hio_uecs_setcapa (
	hio_uecs_t* uecs,
	hio_oow_t   capa
);

/**
 * The hio_uecs_getlen() function return the string length.
 */
#if defined(HIO_HAVE_INLINE)
static HIO_INLINE hio_oow_t hio_uecs_getlen (hio_uecs_t* uecs) { return HIO_UECS_LEN(uecs); }
#else
#	define hio_uecs_getlen(uecs) HIO_UECS_LEN(uecs)
#endif

/**
 * The hio_uecs_setlen() function changes the string length.
 * \return (hio_oow_t)-1 on failure, new length on success
 */
HIO_EXPORT hio_oow_t hio_uecs_setlen (
	hio_uecs_t* uecs,
	hio_oow_t   len
);


/**
 * The hio_uecs_clear() funtion deletes all characters in a string and sets
 * the length to 0. It doesn't resize the internal buffer.
 */
HIO_EXPORT void hio_uecs_clear (
	hio_uecs_t* uecs
);

/**
 * The hio_uecs_swap() function exchanges the pointers to a buffer between
 * two strings. It updates the length and the capacity accordingly.
 */
HIO_EXPORT void hio_uecs_swap (
	hio_uecs_t* uecs1,
	hio_uecs_t* uecs2
);

HIO_EXPORT hio_oow_t hio_uecs_cpy (
	hio_uecs_t*      uecs,
	const hio_uch_t* s
);

HIO_EXPORT hio_oow_t hio_uecs_ncpy (
	hio_uecs_t*      uecs,
	const hio_uch_t* s,
	hio_oow_t        len
);

HIO_EXPORT hio_oow_t hio_uecs_cat (
	hio_uecs_t*      uecs,
	const hio_uch_t* s
);

HIO_EXPORT hio_oow_t hio_uecs_ncat (
	hio_uecs_t*      uecs,
	const hio_uch_t* s,
	hio_oow_t        len
);

HIO_EXPORT hio_oow_t hio_uecs_nrcat (
	hio_uecs_t*      uecs,
	const hio_uch_t* s,
	hio_oow_t        len
);

HIO_EXPORT hio_oow_t hio_uecs_ccat (
	hio_uecs_t*  uecs,
	hio_uch_t    c
);

HIO_EXPORT hio_oow_t hio_uecs_nccat (
	hio_uecs_t*  uecs,
	hio_uch_t    c,
	hio_oow_t    len
);

HIO_EXPORT hio_oow_t hio_uecs_del (
	hio_uecs_t* uecs,
	hio_oow_t   index,
	hio_oow_t   size
);

HIO_EXPORT hio_oow_t hio_uecs_amend (
	hio_uecs_t*       uecs,
	hio_oow_t         index,
	hio_oow_t         size,
	const hio_uch_t*  repl
);

HIO_EXPORT hio_oow_t hio_uecs_vfcat (
	hio_uecs_t*       str,
	const hio_uch_t*  fmt,
	va_list            ap
);

HIO_EXPORT hio_oow_t hio_uecs_fcat (
	hio_uecs_t*       str,
	const hio_uch_t*  fmt,
	...
);

HIO_EXPORT hio_oow_t hio_uecs_vfmt (
	hio_uecs_t*       str,
	const hio_uch_t*  fmt,
	va_list            ap
);

HIO_EXPORT hio_oow_t hio_uecs_fmt (
	hio_uecs_t*       str,
	const hio_uch_t*  fmt,
	...
);

#if defined(HIO_OOCH_IS_UCH)
#	define hio_ooecs_open hio_uecs_open
#	define hio_ooecs_close hio_uecs_close
#	define hio_ooecs_init hio_uecs_init
#	define hio_ooecs_fini hio_uecs_fini
#	define hio_ooecs_getxtn hio_uecs_getxtn
#	define hio_ooecs_yield hio_uecs_yield
#	define hio_ooecs_yieldptr hio_uecs_yieldptr
#	define hio_ooecs_getsizer hio_uecs_getsizer
#	define hio_ooecs_setsizer hio_uecs_setsizer
#	define hio_ooecs_getcapa hio_uecs_getcapa
#	define hio_ooecs_setcapa hio_uecs_setcapa
#	define hio_ooecs_getlen hio_uecs_getlen
#	define hio_ooecs_setlen hio_uecs_setlen
#	define hio_ooecs_clear hio_uecs_clear
#	define hio_ooecs_swap hio_uecs_swap
#	define hio_ooecs_cpy hio_uecs_cpy
#	define hio_ooecs_ncpy hio_uecs_ncpy
#	define hio_ooecs_cat hio_uecs_cat
#	define hio_ooecs_ncat hio_uecs_ncat
#	define hio_ooecs_nrcat hio_uecs_nrcat
#	define hio_ooecs_ccat hio_uecs_ccat
#	define hio_ooecs_nccat hio_uecs_nccat
#	define hio_ooecs_del hio_uecs_del
#	define hio_ooecs_amend hio_uecs_amend

#	define hio_ooecs_vfcat hio_uecs_vfcat
#	define hio_ooecs_fcat hio_uecs_fcat
#	define hio_ooecs_vfmt hio_uecs_vfmt
#	define hio_ooecs_fmt hio_uecs_fmt
#else
#	define hio_ooecs_open hio_becs_open
#	define hio_ooecs_close hio_becs_close
#	define hio_ooecs_init hio_becs_init
#	define hio_ooecs_fini hio_becs_fini
#	define hio_ooecs_getxtn hio_becs_getxtn
#	define hio_ooecs_yield hio_becs_yield
#	define hio_ooecs_yieldptr hio_becs_yieldptr
#	define hio_ooecs_getsizer hio_becs_getsizer
#	define hio_ooecs_setsizer hio_becs_setsizer
#	define hio_ooecs_getcapa hio_becs_getcapa
#	define hio_ooecs_setcapa hio_becs_setcapa
#	define hio_ooecs_getlen hio_becs_getlen
#	define hio_ooecs_setlen hio_becs_setlen
#	define hio_ooecs_clear hio_becs_clear
#	define hio_ooecs_swap hio_becs_swap
#	define hio_ooecs_cpy hio_becs_cpy
#	define hio_ooecs_ncpy hio_becs_ncpy
#	define hio_ooecs_cat hio_becs_cat
#	define hio_ooecs_ncat hio_becs_ncat
#	define hio_ooecs_nrcat hio_becs_nrcat
#	define hio_ooecs_ccat hio_becs_ccat
#	define hio_ooecs_nccat hio_becs_nccat
#	define hio_ooecs_del hio_becs_del
#	define hio_ooecs_amend hio_becs_amend

#	define hio_ooecs_vfcat hio_becs_vfcat
#	define hio_ooecs_fcat hio_becs_fcat
#	define hio_ooecs_vfmt hio_becs_vfmt
#	define hio_ooecs_fmt hio_becs_fmt

#endif

HIO_EXPORT hio_oow_t hio_becs_ncatuchars (
	hio_becs_t*       str,
	const hio_uch_t*  s,
	hio_oow_t         len,
	hio_cmgr_t*       cmgr
);

HIO_EXPORT hio_oow_t hio_uecs_ncatbchars (
	hio_uecs_t*       str,
	const hio_bch_t*  s,
	hio_oow_t         len,
	hio_cmgr_t*       cmgr,
	int                all
);

#if defined(__cplusplus)
}
#endif

#endif
