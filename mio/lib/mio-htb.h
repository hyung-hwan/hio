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

#ifndef _MIO_HTB_H_
#define _MIO_HTB_H_

#include <mio.h>

/**@file
 * This file provides a hash table encapsulated in the #mio_htb_t type that 
 * maintains buckets for key/value pairs with the same key hash chained under
 * the same bucket. Its interface is very close to #mio_rbt_t.
 *
 * This sample code adds a series of keys and values and print them
 * in the randome order.
 * @code
 * #include <mio-htb.h>
 * 
 * static mio_htb_walk_t walk (mio_htb_t* htb, mio_htb_pair_t* pair, void* ctx)
 * {
 *   mio_printf (MIO_T("key = %d, value = %d\n"),
 *     *(int*)MIO_HTB_KPTR(pair), *(int*)MIO_HTB_VPTR(pair));
 *   return MIO_HTB_WALK_FORWARD;
 * }
 * 
 * int main ()
 * {
 *   mio_htb_t* s1;
 *   int i;
 * 
 *   mio_open_stdsios ();
 *   s1 = mio_htb_open (MIO_MMGR_GETDFL(), 0, 30, 75, 1, 1); // error handling skipped
 *   mio_htb_setstyle (s1, mio_get_htb_style(MIO_HTB_STYLE_INLINE_COPIERS));
 * 
 *   for (i = 0; i < 20; i++)
 *   {
 *     int x = i * 20;
 *     mio_htb_insert (s1, &i, MIO_SIZEOF(i), &x, MIO_SIZEOF(x)); // eror handling skipped
 *   }
 * 
 *   mio_htb_walk (s1, walk, MIO_NULL);
 * 
 *   mio_htb_close (s1);
 *   mio_close_stdsios ();
 *   return 0;
 * }
 * @endcode
 */

typedef struct mio_htb_t mio_htb_t;
typedef struct mio_htb_pair_t mio_htb_pair_t;

/** 
 * The mio_htb_walk_t type defines values that the callback function can
 * return to control mio_htb_walk().
 */
enum mio_htb_walk_t
{
        MIO_HTB_WALK_STOP    = 0,
        MIO_HTB_WALK_FORWARD = 1
};
typedef enum mio_htb_walk_t mio_htb_walk_t;

/**
 * The mio_htb_id_t type defines IDs to indicate a key or a value in various
 * functions.
 */
enum mio_htb_id_t
{
	MIO_HTB_KEY = 0,
	MIO_HTB_VAL = 1
};
typedef enum mio_htb_id_t mio_htb_id_t;

/**
 * The mio_htb_copier_t type defines a pair contruction callback.
 * A special copier #MIO_HTB_COPIER_INLINE is provided. This copier enables
 * you to copy the data inline to the internal node. No freeer is invoked
 * when the node is freeed.
 */
typedef void* (*mio_htb_copier_t) (
	mio_htb_t* htb  /* hash table */,
	void*      dptr /* pointer to a key or a value */, 
	mio_oow_t dlen /* length of a key or a value */
);

/**
 * The mio_htb_freeer_t defines a key/value destruction callback
 * The freeer is called when a node containing the element is destroyed.
 */
typedef void (*mio_htb_freeer_t) (
	mio_htb_t* htb,  /**< hash table */
	void*      dptr, /**< pointer to a key or a value */
	mio_oow_t dlen  /**< length of a key or a value */
);


/**
 * The mio_htb_comper_t type defines a key comparator that is called when
 * the htb needs to compare keys. A hash table is created with a default
 * comparator which performs bitwise comparison of two keys.
 * The comparator should return 0 if the keys are the same and a non-zero
 * integer otherwise.
 */
typedef int (*mio_htb_comper_t) (
	const mio_htb_t* htb,    /**< hash table */ 
	const void*      kptr1,  /**< key pointer */
	mio_oow_t       klen1,  /**< key length */ 
	const void*      kptr2,  /**< key pointer */ 
	mio_oow_t       klen2   /**< key length */
);

/**
 * The mio_htb_keeper_t type defines a value keeper that is called when 
 * a value is retained in the context that it should be destroyed because
 * it is identical to a new value. Two values are identical if their 
 * pointers and lengths are equal.
 */
typedef void (*mio_htb_keeper_t) (
	mio_htb_t* htb,    /**< hash table */
	void*      vptr,   /**< value pointer */
	mio_oow_t vlen    /**< value length */
);

/**
 * The mio_htb_sizer_t type defines a bucket size claculator that is called
 * when hash table should resize the bucket. The current bucket size + 1 is 
 * passed as the hint.
 */
typedef mio_oow_t (*mio_htb_sizer_t) (
	mio_htb_t* htb,  /**< htb */
	mio_oow_t  hint  /**< sizing hint */
);

/**
 * The mio_htb_hasher_t type defines a key hash function
 */
typedef mio_oow_t (*mio_htb_hasher_t) (
	const mio_htb_t*  htb,   /**< hash table */
	const void*       kptr,  /**< key pointer */
	mio_oow_t        klen   /**< key length in bytes */
);

/**
 * The mio_htb_walker_t defines a pair visitor.
 */
typedef mio_htb_walk_t (*mio_htb_walker_t) (
	mio_htb_t*      htb,   /**< htb */
	mio_htb_pair_t* pair,  /**< pointer to a key/value pair */
	void*           ctx    /**< pointer to user-defined data */
);

/**
 * The mio_htb_cbserter_t type defines a callback function for mio_htb_cbsert().
 * The mio_htb_cbserter() function calls it to allocate a new pair for the 
 * key pointed to by @a kptr of the length @a klen and the callback context
 * @a ctx. The second parameter @a pair is passed the pointer to the existing
 * pair for the key or #MIO_NULL in case of no existing key. The callback
 * must return a pointer to a new or a reallocated pair. When reallocating the
 * existing pair, this callback must destroy the existing pair and return the 
 * newly reallocated pair. It must return #MIO_NULL for failure.
 */
typedef mio_htb_pair_t* (*mio_htb_cbserter_t) (
	mio_htb_t*      htb,    /**< hash table */
	mio_htb_pair_t* pair,   /**< pair pointer */
	void*            kptr,   /**< key pointer */
	mio_oow_t       klen,   /**< key length */
	void*            ctx     /**< callback context */
);


/**
 * The mio_htb_pair_t type defines hash table pair. A pair is composed of a key
 * and a value. It maintains pointers to the beginning of a key and a value
 * plus their length. The length is scaled down with the scale factor 
 * specified in an owning hash table. 
 */
struct mio_htb_pair_t
{
	mio_ptl_t key;
	mio_ptl_t val;

	/* management information below */
	mio_htb_pair_t* next; 
};

typedef struct mio_htb_style_t mio_htb_style_t;

struct mio_htb_style_t
{
	mio_htb_copier_t copier[2];
	mio_htb_freeer_t freeer[2];
	mio_htb_comper_t comper;   /**< key comparator */
	mio_htb_keeper_t keeper;   /**< value keeper */
	mio_htb_sizer_t  sizer;    /**< bucket capacity recalculator */
	mio_htb_hasher_t hasher;   /**< key hasher */
};

/**
 * The mio_htb_style_kind_t type defines the type of predefined
 * callback set for pair manipulation.
 */
enum mio_htb_style_kind_t
{
	/** store the key and the value pointer */
	MIO_HTB_STYLE_DEFAULT,
	/** copy both key and value into the pair */
	MIO_HTB_STYLE_INLINE_COPIERS,
	/** copy the key into the pair but store the value pointer */
	MIO_HTB_STYLE_INLINE_KEY_COPIER,
	/** copy the value into the pair but store the key pointer */
	MIO_HTB_STYLE_INLINE_VALUE_COPIER
};

typedef enum mio_htb_style_kind_t  mio_htb_style_kind_t;

/**
 * The mio_htb_t type defines a hash table.
 */
struct mio_htb_t
{
	mio_t* mio;

	const mio_htb_style_t* style;

	mio_uint8_t     scale[2]; /**< length scale */
	mio_uint8_t     factor;   /**< load factor in percentage */

	mio_oow_t       size;
	mio_oow_t       capa;
	mio_oow_t       threshold;

	mio_htb_pair_t** bucket;
};

struct mio_htb_itr_t
{
	mio_htb_pair_t* pair;
	mio_oow_t       buckno;
};

typedef struct mio_htb_itr_t mio_htb_itr_t;

/**
 * The MIO_HTB_COPIER_SIMPLE macros defines a copier that remembers the
 * pointer and length of data in a pair.
 **/
#define MIO_HTB_COPIER_SIMPLE ((mio_htb_copier_t)1)

/**
 * The MIO_HTB_COPIER_INLINE macros defines a copier that copies data into
 * a pair.
 **/
#define MIO_HTB_COPIER_INLINE ((mio_htb_copier_t)2)

#define MIO_HTB_COPIER_DEFAULT (MIO_HTB_COPIER_SIMPLE)
#define MIO_HTB_FREEER_DEFAULT (MIO_NULL)
#define MIO_HTB_COMPER_DEFAULT (mio_htb_dflcomp)
#define MIO_HTB_KEEPER_DEFAULT (MIO_NULL)
#define MIO_HTB_SIZER_DEFAULT  (MIO_NULL)
#define MIO_HTB_HASHER_DEFAULT (mio_htb_dflhash)

/**
 * The MIO_HTB_SIZE() macro returns the number of pairs in a hash table.
 */
#define MIO_HTB_SIZE(m) (*(const mio_oow_t*)&(m)->size)

/**
 * The MIO_HTB_CAPA() macro returns the maximum number of pairs that can be
 * stored in a hash table without further reorganization.
 */
#define MIO_HTB_CAPA(m) (*(const mio_oow_t*)&(m)->capa)

#define MIO_HTB_FACTOR(m) (*(const int*)&(m)->factor)
#define MIO_HTB_KSCALE(m) (*(const int*)&(m)->scale[MIO_HTB_KEY])
#define MIO_HTB_VSCALE(m) (*(const int*)&(m)->scale[MIO_HTB_VAL])

#define MIO_HTB_KPTL(p) (&(p)->key)
#define MIO_HTB_VPTL(p) (&(p)->val)

#define MIO_HTB_KPTR(p) ((p)->key.ptr)
#define MIO_HTB_KLEN(p) ((p)->key.len)
#define MIO_HTB_VPTR(p) ((p)->val.ptr)
#define MIO_HTB_VLEN(p) ((p)->val.len)

#define MIO_HTB_NEXT(p) ((p)->next)

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * The mio_get_htb_style() functions returns a predefined callback set for
 * pair manipulation.
 */
MIO_EXPORT const mio_htb_style_t* mio_get_htb_style (
	mio_htb_style_kind_t kind
);

/**
 * The mio_htb_open() function creates a hash table with a dynamic array 
 * bucket and a list of values chained. The initial capacity should be larger
 * than 0. The load factor should be between 0 and 100 inclusive and the load
 * factor of 0 disables bucket resizing. If you need extra space associated
 * with hash table, you may pass a non-zero value for @a xtnsize.
 * The MIO_HTB_XTN() macro and the mio_htb_getxtn() function return the 
 * pointer to the beginning of the extension.
 * The @a kscale and @a vscale parameters specify the unit of the key and 
 * value size. 
 * @return #mio_htb_t pointer on success, #MIO_NULL on failure.
 */
MIO_EXPORT mio_htb_t* mio_htb_open (
	mio_t*      mio,
	mio_oow_t   xtnsize, /**< extension size in bytes */
	mio_oow_t   capa,    /**< initial capacity */
	int         factor,  /**< load factor */
	int         kscale,  /**< key scale - 1 to 255 */
	int         vscale   /**< value scale - 1 to 255 */
);


/**
 * The mio_htb_close() function destroys a hash table.
 */
MIO_EXPORT void mio_htb_close (
	mio_htb_t* htb /**< hash table */
);

/**
 * The mio_htb_init() function initializes a hash table
 */
MIO_EXPORT int mio_htb_init (
	mio_htb_t*  htb,    /**< hash table */
	mio_t*      mio,
	mio_oow_t   capa,    /**< initial capacity */
	int         factor,  /**< load factor */
	int         kscale,  /**< key scale */
	int         vscale   /**< value scale */
);

/**
 * The mio_htb_fini() funtion finalizes a hash table
 */
MIO_EXPORT void mio_htb_fini (
	mio_htb_t* htb
);

#if defined(MIO_HAVE_INLINE)
static MIO_INLINE void* mio_htb_getxtn (mio_htb_t* htb) { return (void*)(htb + 1); }
#else
#define mio_htb_getxtn(htb) ((void*)((mio_htb_t*)(htb) + 1))
#endif

/**
 * The mio_htb_getstyle() function gets manipulation callback function set.
 */
MIO_EXPORT const mio_htb_style_t* mio_htb_getstyle (
	const mio_htb_t* htb /**< hash table */
);

/**
 * The mio_htb_setstyle() function sets internal manipulation callback 
 * functions for data construction, destruction, resizing, hashing, etc.
 * The callback structure pointed to by \a style must outlive the hash
 * table pointed to by \a htb as the hash table doesn't copy the contents
 * of the structure.
 */
MIO_EXPORT void mio_htb_setstyle (
	mio_htb_t*              htb,  /**< hash table */
	const mio_htb_style_t*  style /**< callback function set */
);

/**
 * The mio_htb_getsize() function gets the number of pairs in hash table.
 */
MIO_EXPORT mio_oow_t mio_htb_getsize (
	const mio_htb_t* htb
);

/**
 * The mio_htb_getcapa() function gets the number of slots allocated 
 * in a hash bucket.
 */
MIO_EXPORT mio_oow_t mio_htb_getcapa (
	const mio_htb_t* htb /**< hash table */
);

/**
 * The mio_htb_search() function searches a hash table to find a pair with a 
 * matching key. It returns the pointer to the pair found. If it fails
 * to find one, it returns MIO_NULL.
 * @return pointer to the pair with a maching key, 
 *         or #MIO_NULL if no match is found.
 */
MIO_EXPORT mio_htb_pair_t* mio_htb_search (
	const mio_htb_t* htb,   /**< hash table */
	const void*      kptr,  /**< key pointer */
	mio_oow_t       klen   /**< key length */
);

/**
 * The mio_htb_upsert() function searches a hash table for the pair with a 
 * matching key. If one is found, it updates the pair. Otherwise, it inserts
 * a new pair with the key and value given. It returns the pointer to the 
 * pair updated or inserted.
 * @return pointer to the updated or inserted pair on success, 
 *         #MIO_NULL on failure. 
 */
MIO_EXPORT mio_htb_pair_t* mio_htb_upsert (
	mio_htb_t* htb,   /**< hash table */
	void*      kptr,  /**< key pointer */
	mio_oow_t klen,  /**< key length */
	void*      vptr,  /**< value pointer */
	mio_oow_t vlen   /**< value length */
);

/**
 * The mio_htb_ensert() function inserts a new pair with the key and the value
 * given. If there exists a pair with the key given, the function returns 
 * the pair containing the key.
 * @return pointer to a pair on success, #MIO_NULL on failure. 
 */
MIO_EXPORT mio_htb_pair_t* mio_htb_ensert (
	mio_htb_t* htb,   /**< hash table */
	void*      kptr,  /**< key pointer */
	mio_oow_t klen,  /**< key length */
	void*      vptr,  /**< value pointer */
	mio_oow_t vlen   /**< value length */
);

/**
 * The mio_htb_insert() function inserts a new pair with the key and the value
 * given. If there exists a pair with the key given, the function returns 
 * #MIO_NULL without channging the value.
 * @return pointer to the pair created on success, #MIO_NULL on failure. 
 */
MIO_EXPORT mio_htb_pair_t* mio_htb_insert (
	mio_htb_t* htb,   /**< hash table */
	void*      kptr,  /**< key pointer */
	mio_oow_t klen,  /**< key length */
	void*      vptr,  /**< value pointer */
	mio_oow_t vlen   /**< value length */
);

/**
 * The mio_htb_update() function updates the value of an existing pair
 * with a matching key.
 * @return pointer to the pair on success, #MIO_NULL on no matching pair
 */
MIO_EXPORT mio_htb_pair_t* mio_htb_update (
	mio_htb_t* htb,   /**< hash table */
	void*      kptr,  /**< key pointer */
	mio_oow_t klen,  /**< key length */
	void*      vptr,  /**< value pointer */
	mio_oow_t vlen   /**< value length */
);

/**
 * The mio_htb_cbsert() function inserts a key/value pair by delegating pair 
 * allocation to a callback function. Depending on the callback function,
 * it may behave like mio_htb_insert(), mio_htb_upsert(), mio_htb_update(),
 * mio_htb_ensert(), or totally differently. The sample code below inserts
 * a new pair if the key is not found and appends the new value to the
 * existing value delimited by a comma if the key is found.
 *
 * @code
 * #include <mio-htb.h>
 *
 * mio_htb_walk_t print_map_pair (mio_htb_t* map, mio_htb_pair_t* pair, void* ctx)
 * {
 *   mio_printf (MIO_T("%.*s[%d] => %.*s[%d]\n"),
 *     MIO_HTB_KLEN(pair), MIO_HTB_KPTR(pair), (int)MIO_HTB_KLEN(pair),
 *     MIO_HTB_VLEN(pair), MIO_HTB_VPTR(pair), (int)MIO_HTB_VLEN(pair));
 *   return MIO_HTB_WALK_FORWARD;
 * }
 * 
 * mio_htb_pair_t* cbserter (
 *   mio_htb_t* htb, mio_htb_pair_t* pair,
 *   void* kptr, mio_oow_t klen, void* ctx)
 * {
 *   mio_cstr_t* v = (mio_cstr_t*)ctx;
 *   if (pair == MIO_NULL)
 *   {
 *     // no existing key for the key 
 *     return mio_htb_allocpair (htb, kptr, klen, v->ptr, v->len);
 *   }
 *   else
 *   {
 *     // a pair with the key exists. 
 *     // in this sample, i will append the new value to the old value 
 *     // separated by a comma
 *     mio_htb_pair_t* new_pair;
 *     mio_ooch_t comma = MIO_T(',');
 *     mio_uint8_t* vptr;
 * 
 *     // allocate a new pair, but without filling the actual value. 
 *     // note vptr is given MIO_NULL for that purpose 
 *     new_pair = mio_htb_allocpair (
 *       htb, kptr, klen, MIO_NULL, MIO_HTB_VLEN(pair) + 1 + v->len); 
 *     if (new_pair == MIO_NULL) return MIO_NULL;
 * 
 *     // fill in the value space 
 *     vptr = MIO_HTB_VPTR(new_pair);
 *     mio_memcpy (vptr, MIO_HTB_VPTR(pair), MIO_HTB_VLEN(pair)*MIO_SIZEOF(mio_ooch_t));
 *     vptr += MIO_HTB_VLEN(pair)*MIO_SIZEOF(mio_ooch_t);
 *     mio_memcpy (vptr, &comma, MIO_SIZEOF(mio_ooch_t));
 *     vptr += MIO_SIZEOF(mio_ooch_t);
 *     mio_memcpy (vptr, v->ptr, v->len*MIO_SIZEOF(mio_ooch_t));
 * 
 *     // this callback requires the old pair to be destroyed 
 *     mio_htb_freepair (htb, pair);
 * 
 *     // return the new pair 
 *     return new_pair;
 *   }
 * }
 * 
 * int main ()
 * {
 *   mio_htb_t* s1;
 *   int i;
 *   mio_ooch_t* keys[] = { MIO_T("one"), MIO_T("two"), MIO_T("three") };
 *   mio_ooch_t* vals[] = { MIO_T("1"), MIO_T("2"), MIO_T("3"), MIO_T("4"), MIO_T("5") };
 * 
 *   mio_open_stdsios ();
 *   s1 = mio_htb_open (
 *     MIO_MMGR_GETDFL(), 0, 10, 70,
 *     MIO_SIZEOF(mio_ooch_t), MIO_SIZEOF(mio_ooch_t)
 *   ); // note error check is skipped 
 *   mio_htb_setstyle (s1, mio_get_htb_style(MIO_HTB_STYLE_INLINE_COPIERS));
 * 
 *   for (i = 0; i < MIO_COUNTOF(vals); i++)
 *   {
 *     mio_cstr_t ctx;
 *     ctx.ptr = vals[i]; ctx.len = mio_count_oocstr(vals[i]);
 *     mio_htb_cbsert (s1,
 *       keys[i%MIO_COUNTOF(keys)], mio_count_oocstr(keys[i%MIO_COUNTOF(keys)]),
 *       cbserter, &ctx
 *     ); // note error check is skipped
 *   }
 *   mio_htb_walk (s1, print_map_pair, MIO_NULL);
 * 
 *   mio_htb_close (s1);
 *   mio_close_stdsios ();
 *   return 0;
 * }
 * @endcode
 */
MIO_EXPORT mio_htb_pair_t* mio_htb_cbsert (
	mio_htb_t*         htb,      /**< hash table */
	void*               kptr,     /**< key pointer */
	mio_oow_t          klen,     /**< key length */
	mio_htb_cbserter_t cbserter, /**< callback function */
	void*               ctx       /**< callback context */
);

/**
 * The mio_htb_delete() function deletes a pair with a matching key 
 * @return 0 on success, -1 on failure
 */
MIO_EXPORT int mio_htb_delete (
	mio_htb_t* htb,   /**< hash table */
	const void* kptr, /**< key pointer */
	mio_oow_t klen   /**< key length */
);

/**
 * The mio_htb_clear() function empties a hash table
 */
MIO_EXPORT void mio_htb_clear (
	mio_htb_t* htb /**< hash table */
);

/**
 * The mio_htb_walk() function traverses a hash table.
 */
MIO_EXPORT void mio_htb_walk (
	mio_htb_t*       htb,    /**< hash table */
	mio_htb_walker_t walker, /**< callback function for each pair */
	void*            ctx     /**< pointer to user-specific data */
);

MIO_EXPORT void mio_init_htb_itr (
	mio_htb_itr_t* itr
);

/**
 * The mio_htb_getfirstpair() function returns the pointer to the first pair
 * in a hash table.
 */
MIO_EXPORT mio_htb_pair_t* mio_htb_getfirstpair (
	mio_htb_t*     htb,   /**< hash table */
	mio_htb_itr_t* itr    /**< iterator*/
);

/**
 * The mio_htb_getnextpair() function returns the pointer to the next pair 
 * to the current pair @a pair in a hash table.
 */
MIO_EXPORT mio_htb_pair_t* mio_htb_getnextpair (
	mio_htb_t*      htb,    /**< hash table */
	mio_htb_itr_t*  itr    /**< iterator*/
);

/**
 * The mio_htb_allocpair() function allocates a pair for a key and a value 
 * given. But it does not chain the pair allocated into the hash table @a htb.
 * Use this function at your own risk. 
 *
 * Take note of he following special behavior when the copier is 
 * #MIO_HTB_COPIER_INLINE.
 * - If @a kptr is #MIO_NULL, the key space of the size @a klen is reserved but
 *   not propagated with any data.
 * - If @a vptr is #MIO_NULL, the value space of the size @a vlen is reserved
 *   but not propagated with any data.
 */
MIO_EXPORT mio_htb_pair_t* mio_htb_allocpair (
	mio_htb_t* htb,
	void*      kptr, 
	mio_oow_t klen,	
	void*      vptr,
	mio_oow_t vlen
);

/**
 * The mio_htb_freepair() function destroys a pair. But it does not detach
 * the pair destroyed from the hash table @a htb. Use this function at your
 * own risk.
 */
MIO_EXPORT void mio_htb_freepair (
	mio_htb_t*      htb,
	mio_htb_pair_t* pair
);

/**
 * The mio_htb_dflhash() function is a default hash function.
 */
MIO_EXPORT mio_oow_t mio_htb_dflhash (
	const mio_htb_t*  htb,
	const void*       kptr,
	mio_oow_t        klen
);

/**
 * The mio_htb_dflcomp() function is default comparator.
 */
MIO_EXPORT int mio_htb_dflcomp (
	const mio_htb_t* htb,
	const void*      kptr1,
	mio_oow_t       klen1,
	const void*      kptr2,
	mio_oow_t       klen2
);

#if defined(__cplusplus)
}
#endif

#endif
