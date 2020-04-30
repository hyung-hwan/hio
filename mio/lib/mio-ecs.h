/*
 * $Id$
 *
    Copyright (c) 2006-2020 Chung, Hyung-Hwan. All rights reserved.

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

#ifndef _MIO_ECS_H_
#define _MIO_ECS_H_

#include <mio.h>
#include <stdarg.h>

/** string pointer and length as a aggregate */
#define MIO_BECS_BCS(s)      (&((s)->val))  
/** string length */
#define MIO_BECS_LEN(s)      ((s)->val.len)
/** string pointer */
#define MIO_BECS_PTR(s)      ((s)->val.ptr)
/** pointer to a particular position */
#define MIO_BECS_CPTR(s,idx) (&(s)->val.ptr[idx])
/** string capacity */
#define MIO_BECS_CAPA(s)     ((s)->capa)
/** character at the given position */
#define MIO_BECS_CHAR(s,idx) ((s)->val.ptr[idx]) 
/**< last character. unsafe if length <= 0 */
#define MIO_BECS_LASTCHAR(s) ((s)->val.ptr[(s)->val.len-1])

/** string pointer and length as a aggregate */
#define MIO_UECS_UCS(s)      (&((s)->val))  
/** string length */
#define MIO_UECS_LEN(s)      ((s)->val.len)
/** string pointer */
#define MIO_UECS_PTR(s)      ((s)->val.ptr)
/** pointer to a particular position */
#define MIO_UECS_CPTR(s,idx) (&(s)->val.ptr[idx])
/** string capacity */
#define MIO_UECS_CAPA(s)     ((s)->capa)
/** character at the given position */
#define MIO_UECS_CHAR(s,idx) ((s)->val.ptr[idx]) 
/**< last character. unsafe if length <= 0 */
#define MIO_UECS_LASTCHAR(s) ((s)->val.ptr[(s)->val.len-1])

typedef struct mio_becs_t mio_becs_t;
typedef struct mio_uecs_t mio_uecs_t;

typedef mio_oow_t (*mio_becs_sizer_t) (
	mio_becs_t* data,
	mio_oow_t   hint
);

typedef mio_oow_t (*mio_uecs_sizer_t) (
	mio_uecs_t* data,
	mio_oow_t   hint
);

#if defined(MIO_OOCH_IS_UCH)
#	define MIO_OOECS_OOCS(s)     MIO_UECS_UCS(s)
#	define MIO_OOECS_LEN(s)      MIO_UECS_LEN(s)
#	define MIO_OOECS_PTR(s)      MIO_UECS_PTR(s)
#	define MIO_OOECS_CPTR(s,idx) MIO_UECS_CPTR(s,idx)
#	define MIO_OOECS_CAPA(s)     MIO_UECS_CAPA(s)
#	define MIO_OOECS_CHAR(s,idx) MIO_UECS_CHAR(s,idx)
#	define MIO_OOECS_LASTCHAR(s) MIO_UECS_LASTCHAR(s)
#	define mio_ooecs_t           mio_uecs_t
#	define mio_ooecs_sizer_t     mio_uecs_sizer_t
#else
#	define MIO_OOECS_OOCS(s)     MIO_BECS_BCS(s)
#	define MIO_OOECS_LEN(s)      MIO_BECS_LEN(s)
#	define MIO_OOECS_PTR(s)      MIO_BECS_PTR(s)
#	define MIO_OOECS_CPTR(s,idx) MIO_BECS_CPTR(s,idx)
#	define MIO_OOECS_CAPA(s)     MIO_BECS_CAPA(s)
#	define MIO_OOECS_CHAR(s,idx) MIO_BECS_CHAR(s,idx)
#	define MIO_OOECS_LASTCHAR(s) MIO_BECS_LASTCHAR(s)
#	define mio_ooecs_t           mio_becs_t
#	define mio_ooecs_sizer_t     mio_becs_sizer_t
#endif


/**
 * The mio_becs_t type defines a dynamically resizable multi-byte string.
 */
struct mio_becs_t
{
	mio_t*       gem;
	mio_becs_sizer_t sizer; /**< buffer resizer function */
	mio_bcs_t        val;   /**< buffer/string pointer and lengh */
	mio_oow_t        capa;  /**< buffer capacity */
};

/**
 * The mio_uecs_t type defines a dynamically resizable wide-character string.
 */
struct mio_uecs_t
{
	mio_t*       gem;
	mio_uecs_sizer_t sizer; /**< buffer resizer function */
	mio_ucs_t        val;   /**< buffer/string pointer and lengh */
	mio_oow_t        capa;  /**< buffer capacity */
};


#if defined(__cplusplus)
extern "C" {
#endif

/**
 * The mio_becs_open() function creates a dynamically resizable multibyte string.
 */
MIO_EXPORT mio_becs_t* mio_becs_open (
	mio_t* gem,
	mio_oow_t  xtnsize,
	mio_oow_t  capa
);

MIO_EXPORT void mio_becs_close (
	mio_becs_t* becs
);

/**
 * The mio_becs_init() function initializes a dynamically resizable string
 * If the parameter capa is 0, it doesn't allocate the internal buffer 
 * in advance and always succeeds.
 * \return 0 on success, -1 on failure.
 */
MIO_EXPORT int mio_becs_init (
	mio_becs_t*  becs,
	mio_t*   gem,
	mio_oow_t    capa
);

/**
 * The mio_becs_fini() function finalizes a dynamically resizable string.
 */
MIO_EXPORT void mio_becs_fini (
	mio_becs_t* becs
);

#if defined(MIO_HAVE_INLINE)
static MIO_INLINE void* mio_becs_getxtn (mio_becs_t* becs) { return (void*)(becs + 1); }
#else
#define mio_becs_getxtn(becs) ((void*)((mio_becs_t*)(becs) + 1))
#endif

/**
 * The mio_becs_yield() function assigns the buffer to an variable of the
 * #mio_bcs_t type and recreate a new buffer of the \a new_capa capacity.
 * The function fails if it fails to allocate a new buffer.
 * \return 0 on success, and -1 on failure.
 */
MIO_EXPORT int mio_becs_yield (
	mio_becs_t*  becs,    /**< string */
	mio_bcs_t*   buf,    /**< buffer pointer */
	mio_oow_t    newcapa /**< new capacity */
);

MIO_EXPORT mio_bch_t* mio_becs_yieldptr (
	mio_becs_t*   becs,    /**< string */
	mio_oow_t     newcapa /**< new capacity */
);

/**
 * The mio_becs_getsizer() function gets the sizer.
 * \return sizer function set or MIO_NULL if no sizer is set.
 */
#if defined(MIO_HAVE_INLINE)
static MIO_INLINE mio_becs_sizer_t mio_becs_getsizer (mio_becs_t* becs) { return becs->sizer; }
#else
#	define mio_becs_getsizer(becs) ((becs)->sizer)
#endif

/**
 * The mio_becs_setsizer() function specify a new sizer for a dynamic string.
 * With no sizer specified, the dynamic string doubles the current buffer
 * when it needs to increase its size. The sizer function is passed a dynamic
 * string and the minimum capacity required to hold data after resizing.
 * The string is truncated if the sizer function returns a smaller number
 * than the hint passed.
 */
#if defined(MIO_HAVE_INLINE)
static MIO_INLINE void mio_becs_setsizer (mio_becs_t* becs, mio_becs_sizer_t sizer) { becs->sizer = sizer; }
#else
#	define mio_becs_setsizer(becs,sizerfn) ((becs)->sizer = (sizerfn))
#endif

/**
 * The mio_becs_getcapa() function returns the current capacity.
 * You may use MIO_STR_CAPA(str) macro for performance sake.
 * \return current capacity in number of characters.
 */
#if defined(MIO_HAVE_INLINE)
static MIO_INLINE mio_oow_t mio_becs_getcapa (mio_becs_t* becs) { return MIO_BECS_CAPA(becs); }
#else
#	define mio_becs_getcapa(becs) MIO_BECS_CAPA(becs)
#endif

/**
 * The mio_becs_setcapa() function sets the new capacity. If the new capacity
 * is smaller than the old, the overflowing characters are removed from
 * from the buffer.
 * \return (mio_oow_t)-1 on failure, new capacity on success 
 */
MIO_EXPORT mio_oow_t mio_becs_setcapa (
	mio_becs_t* becs,
	mio_oow_t   capa
);

/**
 * The mio_becs_getlen() function return the string length.
 */
#if defined(MIO_HAVE_INLINE)
static MIO_INLINE mio_oow_t mio_becs_getlen (mio_becs_t* becs) { return MIO_BECS_LEN(becs); }
#else
#	define mio_becs_getlen(becs) MIO_BECS_LEN(becs)
#endif

/**
 * The mio_becs_setlen() function changes the string length.
 * \return (mio_oow_t)-1 on failure, new length on success 
 */
MIO_EXPORT mio_oow_t mio_becs_setlen (
	mio_becs_t* becs,
	mio_oow_t   len
);

/**
 * The mio_becs_clear() funtion deletes all characters in a string and sets
 * the length to 0. It doesn't resize the internal buffer.
 */
MIO_EXPORT void mio_becs_clear (
	mio_becs_t* becs
);

/**
 * The mio_becs_swap() function exchanges the pointers to a buffer between
 * two strings. It updates the length and the capacity accordingly.
 */
MIO_EXPORT void mio_becs_swap (
	mio_becs_t* becs1,
	mio_becs_t* becs2
);

MIO_EXPORT mio_oow_t mio_becs_cpy (
	mio_becs_t*      becs,
	const mio_bch_t* s
);

MIO_EXPORT mio_oow_t mio_becs_ncpy (
	mio_becs_t*       becs,
	const mio_bch_t*  s,
	mio_oow_t         len
);

MIO_EXPORT mio_oow_t mio_becs_cat (
	mio_becs_t*      becs,
	const mio_bch_t* s
);

MIO_EXPORT mio_oow_t mio_becs_ncat (
	mio_becs_t*      becs,
	const mio_bch_t* s,
	mio_oow_t        len
);

MIO_EXPORT mio_oow_t mio_becs_nrcat (
	mio_becs_t*      becs,
	const mio_bch_t* s,
	mio_oow_t        len
);

MIO_EXPORT mio_oow_t mio_becs_ccat (
	mio_becs_t*  becs,
	mio_bch_t    c
);

MIO_EXPORT mio_oow_t mio_becs_nccat (
	mio_becs_t*  becs,
	mio_bch_t    c,
	mio_oow_t    len
);

MIO_EXPORT mio_oow_t mio_becs_del (
	mio_becs_t* becs,
	mio_oow_t   index,
	mio_oow_t   size
);

MIO_EXPORT mio_oow_t mio_becs_amend (
	mio_becs_t*      becs,
	mio_oow_t        index,
	mio_oow_t        size,
	const mio_bch_t* repl
);

MIO_EXPORT mio_oow_t mio_becs_vfcat (
	mio_becs_t*       str, 
	const mio_bch_t*  fmt,
	va_list            ap
);

MIO_EXPORT mio_oow_t mio_becs_fcat (
	mio_becs_t*       str, 
	const mio_bch_t*  fmt,
	...
);

MIO_EXPORT mio_oow_t mio_becs_vfmt (
	mio_becs_t*       str, 
	const mio_bch_t*  fmt,
	va_list            ap
);

MIO_EXPORT mio_oow_t mio_becs_fmt (
	mio_becs_t*       str, 
	const mio_bch_t*  fmt,
	...
);

/* ------------------------------------------------------------------------ */

/**
 * The mio_uecs_open() function creates a dynamically resizable multibyte string.
 */
MIO_EXPORT mio_uecs_t* mio_uecs_open (
	mio_t* gem,
	mio_oow_t  xtnsize,
	mio_oow_t  capa
);

MIO_EXPORT void mio_uecs_close (
	mio_uecs_t* uecs
);

/**
 * The mio_uecs_init() function initializes a dynamically resizable string
 * If the parameter capa is 0, it doesn't allocate the internal buffer 
 * in advance and always succeeds.
 * \return 0 on success, -1 on failure.
 */
MIO_EXPORT int mio_uecs_init (
	mio_uecs_t*  uecs,
	mio_t*   gem,
	mio_oow_t    capa
);

/**
 * The mio_uecs_fini() function finalizes a dynamically resizable string.
 */
MIO_EXPORT void mio_uecs_fini (
	mio_uecs_t* uecs
);

#if defined(MIO_HAVE_INLINE)
static MIO_INLINE void* mio_uecs_getxtn (mio_uecs_t* uecs) { return (void*)(uecs + 1); }
#else
#define mio_uecs_getxtn(uecs) ((void*)((mio_uecs_t*)(uecs) + 1))
#endif

/**
 * The mio_uecs_yield() function assigns the buffer to an variable of the
 * #mio_ucs_t type and recreate a new buffer of the \a new_capa capacity.
 * The function fails if it fails to allocate a new buffer.
 * \return 0 on success, and -1 on failure.
 */
MIO_EXPORT int mio_uecs_yield (
	mio_uecs_t*   uecs,    /**< string */
	mio_ucs_t*    buf,    /**< buffer pointer */
	mio_oow_t     newcapa /**< new capacity */
);

MIO_EXPORT mio_uch_t* mio_uecs_yieldptr (
	mio_uecs_t*   uecs,    /**< string */
	mio_oow_t     newcapa /**< new capacity */
);

/**
 * The mio_uecs_getsizer() function gets the sizer.
 * \return sizer function set or MIO_NULL if no sizer is set.
 */
#if defined(MIO_HAVE_INLINE)
static MIO_INLINE mio_uecs_sizer_t mio_uecs_getsizer (mio_uecs_t* uecs) { return uecs->sizer; }
#else
#	define mio_uecs_getsizer(uecs) ((uecs)->sizer)
#endif

/**
 * The mio_uecs_setsizer() function specify a new sizer for a dynamic string.
 * With no sizer specified, the dynamic string doubles the current buffer
 * when it needs to increase its size. The sizer function is passed a dynamic
 * string and the minimum capacity required to hold data after resizing.
 * The string is truncated if the sizer function returns a smaller number
 * than the hint passed.
 */
#if defined(MIO_HAVE_INLINE)
static MIO_INLINE void mio_uecs_setsizer (mio_uecs_t* uecs, mio_uecs_sizer_t sizer) { uecs->sizer = sizer; }
#else
#	define mio_uecs_setsizer(uecs,sizerfn) ((uecs)->sizer = (sizerfn))
#endif

/**
 * The mio_uecs_getcapa() function returns the current capacity.
 * You may use MIO_STR_CAPA(str) macro for performance sake.
 * \return current capacity in number of characters.
 */
#if defined(MIO_HAVE_INLINE)
static MIO_INLINE mio_oow_t mio_uecs_getcapa (mio_uecs_t* uecs) { return MIO_UECS_CAPA(uecs); }
#else
#	define mio_uecs_getcapa(uecs) MIO_UECS_CAPA(uecs)
#endif

/**
 * The mio_uecs_setcapa() function sets the new capacity. If the new capacity
 * is smaller than the old, the overflowing characters are removed from
 * from the buffer.
 * \return (mio_oow_t)-1 on failure, new capacity on success 
 */
MIO_EXPORT mio_oow_t mio_uecs_setcapa (
	mio_uecs_t* uecs,
	mio_oow_t   capa
);

/**
 * The mio_uecs_getlen() function return the string length.
 */
#if defined(MIO_HAVE_INLINE)
static MIO_INLINE mio_oow_t mio_uecs_getlen (mio_uecs_t* uecs) { return MIO_UECS_LEN(uecs); }
#else
#	define mio_uecs_getlen(uecs) MIO_UECS_LEN(uecs)
#endif

/**
 * The mio_uecs_setlen() function changes the string length.
 * \return (mio_oow_t)-1 on failure, new length on success 
 */
MIO_EXPORT mio_oow_t mio_uecs_setlen (
	mio_uecs_t* uecs,
	mio_oow_t   len
);


/**
 * The mio_uecs_clear() funtion deletes all characters in a string and sets
 * the length to 0. It doesn't resize the internal buffer.
 */
MIO_EXPORT void mio_uecs_clear (
	mio_uecs_t* uecs
);

/**
 * The mio_uecs_swap() function exchanges the pointers to a buffer between
 * two strings. It updates the length and the capacity accordingly.
 */
MIO_EXPORT void mio_uecs_swap (
	mio_uecs_t* uecs1,
	mio_uecs_t* uecs2
);

MIO_EXPORT mio_oow_t mio_uecs_cpy (
	mio_uecs_t*      uecs,
	const mio_uch_t* s
);

MIO_EXPORT mio_oow_t mio_uecs_ncpy (
	mio_uecs_t*      uecs,
	const mio_uch_t* s,
	mio_oow_t        len
);

MIO_EXPORT mio_oow_t mio_uecs_cat (
	mio_uecs_t*      uecs,
	const mio_uch_t* s
);

MIO_EXPORT mio_oow_t mio_uecs_ncat (
	mio_uecs_t*      uecs,
	const mio_uch_t* s,
	mio_oow_t        len
);

MIO_EXPORT mio_oow_t mio_uecs_nrcat (
	mio_uecs_t*      uecs,
	const mio_uch_t* s,
	mio_oow_t        len
);

MIO_EXPORT mio_oow_t mio_uecs_ccat (
	mio_uecs_t*  uecs,
	mio_uch_t    c
);

MIO_EXPORT mio_oow_t mio_uecs_nccat (
	mio_uecs_t*  uecs,
	mio_uch_t    c,
	mio_oow_t    len
);

MIO_EXPORT mio_oow_t mio_uecs_del (
	mio_uecs_t* uecs,
	mio_oow_t   index,
	mio_oow_t   size
);

MIO_EXPORT mio_oow_t mio_uecs_amend (
	mio_uecs_t*       uecs,
	mio_oow_t         index,
	mio_oow_t         size,
	const mio_uch_t*  repl
);

#if 0
MIO_EXPORT mio_oow_t mio_uecs_vfcat (
	mio_uecs_t*       str, 
	const mio_uch_t*  fmt,
	va_list            ap
);

MIO_EXPORT mio_oow_t mio_uecs_fcat (
	mio_uecs_t*       str, 
	const mio_uch_t*  fmt,
	...
);

MIO_EXPORT mio_oow_t mio_uecs_vfmt (
	mio_uecs_t*       str, 
	const mio_uch_t*  fmt,
	va_list            ap
);

MIO_EXPORT mio_oow_t mio_uecs_fmt (
	mio_uecs_t*       str, 
	const mio_uch_t*  fmt,
	...
);
#endif

#if defined(MIO_OOCH_IS_UCH)
#	define mio_ooecs_open mio_uecs_open
#	define mio_ooecs_close mio_uecs_close
#	define mio_ooecs_init mio_uecs_init
#	define mio_ooecs_fini mio_uecs_fini
#	define mio_ooecs_getxtn mio_uecs_getxtn
#	define mio_ooecs_yield mio_uecs_yield
#	define mio_ooecs_yieldptr mio_uecs_yieldptr
#	define mio_ooecs_getsizer mio_uecs_getsizer
#	define mio_ooecs_setsizer mio_uecs_setsizer
#	define mio_ooecs_getcapa mio_uecs_getcapa
#	define mio_ooecs_setcapa mio_uecs_setcapa
#	define mio_ooecs_getlen mio_uecs_getlen
#	define mio_ooecs_setlen mio_uecs_setlen
#	define mio_ooecs_clear mio_uecs_clear
#	define mio_ooecs_swap mio_uecs_swap
#	define mio_ooecs_cpy mio_uecs_cpy
#	define mio_ooecs_ncpy mio_uecs_ncpy
#	define mio_ooecs_cat mio_uecs_cat
#	define mio_ooecs_ncat mio_uecs_ncat
#	define mio_ooecs_nrcat mio_uecs_nrcat
#	define mio_ooecs_ccat mio_uecs_ccat
#	define mio_ooecs_nccat mio_uecs_nccat
#	define mio_ooecs_del mio_uecs_del
#	define mio_ooecs_amend mio_uecs_amend
#if 0
#	define mio_ooecs_vfcat mio_uecs_vfcat
#	define mio_ooecs_fcat mio_uecs_fcat
#	define mio_ooecs_vfmt mio_uecs_vfmt
#	define mio_ooecs_fmt mio_uecs_fmt
#endif
#else
#	define mio_ooecs_open mio_becs_open
#	define mio_ooecs_close mio_becs_close
#	define mio_ooecs_init mio_becs_init
#	define mio_ooecs_fini mio_becs_fini
#	define mio_ooecs_getxtn mio_becs_getxtn
#	define mio_ooecs_yield mio_becs_yield
#	define mio_ooecs_yieldptr mio_becs_yieldptr
#	define mio_ooecs_getsizer mio_becs_getsizer
#	define mio_ooecs_setsizer mio_becs_setsizer
#	define mio_ooecs_getcapa mio_becs_getcapa
#	define mio_ooecs_setcapa mio_becs_setcapa
#	define mio_ooecs_getlen mio_becs_getlen
#	define mio_ooecs_setlen mio_becs_setlen
#	define mio_ooecs_clear mio_becs_clear
#	define mio_ooecs_swap mio_becs_swap
#	define mio_ooecs_cpy mio_becs_cpy
#	define mio_ooecs_ncpy mio_becs_ncpy
#	define mio_ooecs_cat mio_becs_cat
#	define mio_ooecs_ncat mio_becs_ncat
#	define mio_ooecs_nrcat mio_becs_nrcat
#	define mio_ooecs_ccat mio_becs_ccat
#	define mio_ooecs_nccat mio_becs_nccat
#	define mio_ooecs_del mio_becs_del
#	define mio_ooecs_amend mio_becs_amend

#if 0
#	define mio_ooecs_vfcat mio_becs_vfcat
#	define mio_ooecs_fcat mio_becs_fcat
#	define mio_ooecs_vfmt mio_becs_vfmt
#	define mio_ooecs_fmt mio_becs_fmt
#endif

#endif

MIO_EXPORT mio_oow_t mio_becs_ncatuchars (
	mio_becs_t*       str,
	const mio_uch_t*  s,
	mio_oow_t         len,
	mio_cmgr_t*       cmgr
);

MIO_EXPORT mio_oow_t mio_uecs_ncatbchars (
	mio_uecs_t*       str,
	const mio_bch_t*  s,
	mio_oow_t         len,
	mio_cmgr_t*       cmgr,
	int                all
);

#if defined(__cplusplus)
}
#endif

#endif
