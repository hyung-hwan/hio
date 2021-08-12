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

#ifndef _HIO_HTB_H_
#define _HIO_HTB_H_

#include <hio.h>

/**@file
 * This file provides a hash table encapsulated in the #hio_htb_t type that 
 * maintains buckets for key/value pairs with the same key hash chained under
 * the same bucket. Its interface is very close to #hio_rbt_t.
 *
 * This sample code adds a series of keys and values and print them
 * in the randome order.
 * @code
 * #include <hio-htb.h>
 * 
 * static hio_htb_walk_t walk (hio_htb_t* htb, hio_htb_pair_t* pair, void* ctx)
 * {
 *   hio_printf (HIO_T("key = %d, value = %d\n"),
 *     *(int*)HIO_HTB_KPTR(pair), *(int*)HIO_HTB_VPTR(pair));
 *   return HIO_HTB_WALK_FORWARD;
 * }
 * 
 * int main ()
 * {
 *   hio_htb_t* s1;
 *   int i;
 * 
 *   hio_open_stdsios ();
 *   s1 = hio_htb_open (HIO_MMGR_GETDFL(), 0, 30, 75, 1, 1); // error handling skipped
 *   hio_htb_setstyle (s1, hio_get_htb_style(HIO_HTB_STYLE_INLINE_COPIERS));
 * 
 *   for (i = 0; i < 20; i++)
 *   {
 *     int x = i * 20;
 *     hio_htb_insert (s1, &i, HIO_SIZEOF(i), &x, HIO_SIZEOF(x)); // eror handling skipped
 *   }
 * 
 *   hio_htb_walk (s1, walk, HIO_NULL);
 * 
 *   hio_htb_close (s1);
 *   hio_close_stdsios ();
 *   return 0;
 * }
 * @endcode
 */

typedef struct hio_htb_t hio_htb_t;
typedef struct hio_htb_pair_t hio_htb_pair_t;

/** 
 * The hio_htb_walk_t type defines values that the callback function can
 * return to control hio_htb_walk().
 */
enum hio_htb_walk_t
{
        HIO_HTB_WALK_STOP    = 0,
        HIO_HTB_WALK_FORWARD = 1
};
typedef enum hio_htb_walk_t hio_htb_walk_t;

/**
 * The hio_htb_id_t type defines IDs to indicate a key or a value in various
 * functions.
 */
enum hio_htb_id_t
{
	HIO_HTB_KEY = 0,
	HIO_HTB_VAL = 1
};
typedef enum hio_htb_id_t hio_htb_id_t;

/**
 * The hio_htb_copier_t type defines a pair contruction callback.
 * A special copier #HIO_HTB_COPIER_INLINE is provided. This copier enables
 * you to copy the data inline to the internal node. No freeer is invoked
 * when the node is freeed.
 */
typedef void* (*hio_htb_copier_t) (
	hio_htb_t* htb  /* hash table */,
	void*      dptr /* pointer to a key or a value */, 
	hio_oow_t dlen /* length of a key or a value */
);

/**
 * The hio_htb_freeer_t defines a key/value destruction callback
 * The freeer is called when a node containing the element is destroyed.
 */
typedef void (*hio_htb_freeer_t) (
	hio_htb_t* htb,  /**< hash table */
	void*      dptr, /**< pointer to a key or a value */
	hio_oow_t dlen  /**< length of a key or a value */
);


/**
 * The hio_htb_comper_t type defines a key comparator that is called when
 * the htb needs to compare keys. A hash table is created with a default
 * comparator which performs bitwise comparison of two keys.
 * The comparator should return 0 if the keys are the same and a non-zero
 * integer otherwise.
 */
typedef int (*hio_htb_comper_t) (
	const hio_htb_t* htb,    /**< hash table */ 
	const void*      kptr1,  /**< key pointer */
	hio_oow_t       klen1,  /**< key length */ 
	const void*      kptr2,  /**< key pointer */ 
	hio_oow_t       klen2   /**< key length */
);

/**
 * The hio_htb_keeper_t type defines a value keeper that is called when 
 * a value is retained in the context that it should be destroyed because
 * it is identical to a new value. Two values are identical if their 
 * pointers and lengths are equal.
 */
typedef void (*hio_htb_keeper_t) (
	hio_htb_t* htb,    /**< hash table */
	void*      vptr,   /**< value pointer */
	hio_oow_t vlen    /**< value length */
);

/**
 * The hio_htb_sizer_t type defines a bucket size claculator that is called
 * when hash table should resize the bucket. The current bucket size + 1 is 
 * passed as the hint.
 */
typedef hio_oow_t (*hio_htb_sizer_t) (
	hio_htb_t* htb,  /**< htb */
	hio_oow_t  hint  /**< sizing hint */
);

/**
 * The hio_htb_hasher_t type defines a key hash function
 */
typedef hio_oow_t (*hio_htb_hasher_t) (
	const hio_htb_t*  htb,   /**< hash table */
	const void*       kptr,  /**< key pointer */
	hio_oow_t        klen   /**< key length in bytes */
);

/**
 * The hio_htb_walker_t defines a pair visitor.
 */
typedef hio_htb_walk_t (*hio_htb_walker_t) (
	hio_htb_t*      htb,   /**< htb */
	hio_htb_pair_t* pair,  /**< pointer to a key/value pair */
	void*           ctx    /**< pointer to user-defined data */
);

/**
 * The hio_htb_cbserter_t type defines a callback function for hio_htb_cbsert().
 * The hio_htb_cbserter() function calls it to allocate a new pair for the 
 * key pointed to by @a kptr of the length @a klen and the callback context
 * @a ctx. The second parameter @a pair is passed the pointer to the existing
 * pair for the key or #HIO_NULL in case of no existing key. The callback
 * must return a pointer to a new or a reallocated pair. When reallocating the
 * existing pair, this callback must destroy the existing pair and return the 
 * newly reallocated pair. It must return #HIO_NULL for failure.
 */
typedef hio_htb_pair_t* (*hio_htb_cbserter_t) (
	hio_htb_t*      htb,    /**< hash table */
	hio_htb_pair_t* pair,   /**< pair pointer */
	void*            kptr,   /**< key pointer */
	hio_oow_t       klen,   /**< key length */
	void*            ctx     /**< callback context */
);


/**
 * The hio_htb_pair_t type defines hash table pair. A pair is composed of a key
 * and a value. It maintains pointers to the beginning of a key and a value
 * plus their length. The length is scaled down with the scale factor 
 * specified in an owning hash table. 
 */
struct hio_htb_pair_t
{
	hio_ptl_t key;
	hio_ptl_t val;

	/* management information below */
	hio_htb_pair_t* next; 
};

typedef struct hio_htb_style_t hio_htb_style_t;

struct hio_htb_style_t
{
	hio_htb_copier_t copier[2];
	hio_htb_freeer_t freeer[2];
	hio_htb_comper_t comper;   /**< key comparator */
	hio_htb_keeper_t keeper;   /**< value keeper */
	hio_htb_sizer_t  sizer;    /**< bucket capacity recalculator */
	hio_htb_hasher_t hasher;   /**< key hasher */
};

/**
 * The hio_htb_style_kind_t type defines the type of predefined
 * callback set for pair manipulation.
 */
enum hio_htb_style_kind_t
{
	/** store the key and the value pointer */
	HIO_HTB_STYLE_DEFAULT,
	/** copy both key and value into the pair */
	HIO_HTB_STYLE_INLINE_COPIERS,
	/** copy the key into the pair but store the value pointer */
	HIO_HTB_STYLE_INLINE_KEY_COPIER,
	/** copy the value into the pair but store the key pointer */
	HIO_HTB_STYLE_INLINE_VALUE_COPIER
};

typedef enum hio_htb_style_kind_t  hio_htb_style_kind_t;

/**
 * The hio_htb_t type defines a hash table.
 */
struct hio_htb_t
{
	hio_t* hio;

	const hio_htb_style_t* style;

	hio_uint8_t     scale[2]; /**< length scale */
	hio_uint8_t     factor;   /**< load factor in percentage */

	hio_oow_t       size;
	hio_oow_t       capa;
	hio_oow_t       threshold;

	hio_htb_pair_t** bucket;
};

struct hio_htb_itr_t
{
	hio_htb_pair_t* pair;
	hio_oow_t       buckno;
};

typedef struct hio_htb_itr_t hio_htb_itr_t;

/**
 * The HIO_HTB_COPIER_SIMPLE macros defines a copier that remembers the
 * pointer and length of data in a pair.
 **/
#define HIO_HTB_COPIER_SIMPLE ((hio_htb_copier_t)1)

/**
 * The HIO_HTB_COPIER_INLINE macros defines a copier that copies data into
 * a pair.
 **/
#define HIO_HTB_COPIER_INLINE ((hio_htb_copier_t)2)

#define HIO_HTB_COPIER_DEFAULT (HIO_HTB_COPIER_SIMPLE)
#define HIO_HTB_FREEER_DEFAULT (HIO_NULL)
#define HIO_HTB_COMPER_DEFAULT (hio_htb_dflcomp)
#define HIO_HTB_KEEPER_DEFAULT (HIO_NULL)
#define HIO_HTB_SIZER_DEFAULT  (HIO_NULL)
#define HIO_HTB_HASHER_DEFAULT (hio_htb_dflhash)

/**
 * The HIO_HTB_SIZE() macro returns the number of pairs in a hash table.
 */
#define HIO_HTB_SIZE(m) (*(const hio_oow_t*)&(m)->size)

/**
 * The HIO_HTB_CAPA() macro returns the maximum number of pairs that can be
 * stored in a hash table without further reorganization.
 */
#define HIO_HTB_CAPA(m) (*(const hio_oow_t*)&(m)->capa)

#define HIO_HTB_FACTOR(m) (*(const int*)&(m)->factor)
#define HIO_HTB_KSCALE(m) (*(const int*)&(m)->scale[HIO_HTB_KEY])
#define HIO_HTB_VSCALE(m) (*(const int*)&(m)->scale[HIO_HTB_VAL])

#define HIO_HTB_KPTL(p) (&(p)->key)
#define HIO_HTB_VPTL(p) (&(p)->val)

#define HIO_HTB_KPTR(p) ((p)->key.ptr)
#define HIO_HTB_KLEN(p) ((p)->key.len)
#define HIO_HTB_VPTR(p) ((p)->val.ptr)
#define HIO_HTB_VLEN(p) ((p)->val.len)

#define HIO_HTB_NEXT(p) ((p)->next)

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * The hio_get_htb_style() functions returns a predefined callback set for
 * pair manipulation.
 */
HIO_EXPORT const hio_htb_style_t* hio_get_htb_style (
	hio_htb_style_kind_t kind
);

/**
 * The hio_htb_open() function creates a hash table with a dynamic array 
 * bucket and a list of values chained. The initial capacity should be larger
 * than 0. The load factor should be between 0 and 100 inclusive and the load
 * factor of 0 disables bucket resizing. If you need extra space associated
 * with hash table, you may pass a non-zero value for @a xtnsize.
 * The HIO_HTB_XTN() macro and the hio_htb_getxtn() function return the 
 * pointer to the beginning of the extension.
 * The @a kscale and @a vscale parameters specify the unit of the key and 
 * value size. 
 * @return #hio_htb_t pointer on success, #HIO_NULL on failure.
 */
HIO_EXPORT hio_htb_t* hio_htb_open (
	hio_t*      hio,
	hio_oow_t   xtnsize, /**< extension size in bytes */
	hio_oow_t   capa,    /**< initial capacity */
	int         factor,  /**< load factor */
	int         kscale,  /**< key scale - 1 to 255 */
	int         vscale   /**< value scale - 1 to 255 */
);


/**
 * The hio_htb_close() function destroys a hash table.
 */
HIO_EXPORT void hio_htb_close (
	hio_htb_t* htb /**< hash table */
);

/**
 * The hio_htb_init() function initializes a hash table
 */
HIO_EXPORT int hio_htb_init (
	hio_htb_t*  htb,    /**< hash table */
	hio_t*      hio,
	hio_oow_t   capa,    /**< initial capacity */
	int         factor,  /**< load factor */
	int         kscale,  /**< key scale */
	int         vscale   /**< value scale */
);

/**
 * The hio_htb_fini() funtion finalizes a hash table
 */
HIO_EXPORT void hio_htb_fini (
	hio_htb_t* htb
);

#if defined(HIO_HAVE_INLINE)
static HIO_INLINE void* hio_htb_getxtn (hio_htb_t* htb) { return (void*)(htb + 1); }
#else
#define hio_htb_getxtn(htb) ((void*)((hio_htb_t*)(htb) + 1))
#endif

/**
 * The hio_htb_getstyle() function gets manipulation callback function set.
 */
HIO_EXPORT const hio_htb_style_t* hio_htb_getstyle (
	const hio_htb_t* htb /**< hash table */
);

/**
 * The hio_htb_setstyle() function sets internal manipulation callback 
 * functions for data construction, destruction, resizing, hashing, etc.
 * The callback structure pointed to by \a style must outlive the hash
 * table pointed to by \a htb as the hash table doesn't copy the contents
 * of the structure.
 */
HIO_EXPORT void hio_htb_setstyle (
	hio_htb_t*              htb,  /**< hash table */
	const hio_htb_style_t*  style /**< callback function set */
);

/**
 * The hio_htb_getsize() function gets the number of pairs in hash table.
 */
HIO_EXPORT hio_oow_t hio_htb_getsize (
	const hio_htb_t* htb
);

/**
 * The hio_htb_getcapa() function gets the number of slots allocated 
 * in a hash bucket.
 */
HIO_EXPORT hio_oow_t hio_htb_getcapa (
	const hio_htb_t* htb /**< hash table */
);

/**
 * The hio_htb_search() function searches a hash table to find a pair with a 
 * matching key. It returns the pointer to the pair found. If it fails
 * to find one, it returns HIO_NULL.
 * @return pointer to the pair with a maching key, 
 *         or #HIO_NULL if no match is found.
 */
HIO_EXPORT hio_htb_pair_t* hio_htb_search (
	const hio_htb_t* htb,   /**< hash table */
	const void*      kptr,  /**< key pointer */
	hio_oow_t       klen   /**< key length */
);

/**
 * The hio_htb_upsert() function searches a hash table for the pair with a 
 * matching key. If one is found, it updates the pair. Otherwise, it inserts
 * a new pair with the key and value given. It returns the pointer to the 
 * pair updated or inserted.
 * @return pointer to the updated or inserted pair on success, 
 *         #HIO_NULL on failure. 
 */
HIO_EXPORT hio_htb_pair_t* hio_htb_upsert (
	hio_htb_t* htb,   /**< hash table */
	void*      kptr,  /**< key pointer */
	hio_oow_t klen,  /**< key length */
	void*      vptr,  /**< value pointer */
	hio_oow_t vlen   /**< value length */
);

/**
 * The hio_htb_ensert() function inserts a new pair with the key and the value
 * given. If there exists a pair with the key given, the function returns 
 * the pair containing the key.
 * @return pointer to a pair on success, #HIO_NULL on failure. 
 */
HIO_EXPORT hio_htb_pair_t* hio_htb_ensert (
	hio_htb_t* htb,   /**< hash table */
	void*      kptr,  /**< key pointer */
	hio_oow_t klen,  /**< key length */
	void*      vptr,  /**< value pointer */
	hio_oow_t vlen   /**< value length */
);

/**
 * The hio_htb_insert() function inserts a new pair with the key and the value
 * given. If there exists a pair with the key given, the function returns 
 * #HIO_NULL without channging the value.
 * @return pointer to the pair created on success, #HIO_NULL on failure. 
 */
HIO_EXPORT hio_htb_pair_t* hio_htb_insert (
	hio_htb_t* htb,   /**< hash table */
	void*      kptr,  /**< key pointer */
	hio_oow_t klen,  /**< key length */
	void*      vptr,  /**< value pointer */
	hio_oow_t vlen   /**< value length */
);

/**
 * The hio_htb_update() function updates the value of an existing pair
 * with a matching key.
 * @return pointer to the pair on success, #HIO_NULL on no matching pair
 */
HIO_EXPORT hio_htb_pair_t* hio_htb_update (
	hio_htb_t* htb,   /**< hash table */
	void*      kptr,  /**< key pointer */
	hio_oow_t klen,  /**< key length */
	void*      vptr,  /**< value pointer */
	hio_oow_t vlen   /**< value length */
);

/**
 * The hio_htb_cbsert() function inserts a key/value pair by delegating pair 
 * allocation to a callback function. Depending on the callback function,
 * it may behave like hio_htb_insert(), hio_htb_upsert(), hio_htb_update(),
 * hio_htb_ensert(), or totally differently. The sample code below inserts
 * a new pair if the key is not found and appends the new value to the
 * existing value delimited by a comma if the key is found.
 *
 * @code
 * #include <hio-htb.h>
 *
 * hio_htb_walk_t print_map_pair (hio_htb_t* map, hio_htb_pair_t* pair, void* ctx)
 * {
 *   hio_printf (HIO_T("%.*s[%d] => %.*s[%d]\n"),
 *     HIO_HTB_KLEN(pair), HIO_HTB_KPTR(pair), (int)HIO_HTB_KLEN(pair),
 *     HIO_HTB_VLEN(pair), HIO_HTB_VPTR(pair), (int)HIO_HTB_VLEN(pair));
 *   return HIO_HTB_WALK_FORWARD;
 * }
 * 
 * hio_htb_pair_t* cbserter (
 *   hio_htb_t* htb, hio_htb_pair_t* pair,
 *   void* kptr, hio_oow_t klen, void* ctx)
 * {
 *   hio_cstr_t* v = (hio_cstr_t*)ctx;
 *   if (pair == HIO_NULL)
 *   {
 *     // no existing key for the key 
 *     return hio_htb_allocpair (htb, kptr, klen, v->ptr, v->len);
 *   }
 *   else
 *   {
 *     // a pair with the key exists. 
 *     // in this sample, i will append the new value to the old value 
 *     // separated by a comma
 *     hio_htb_pair_t* new_pair;
 *     hio_ooch_t comma = HIO_T(',');
 *     hio_uint8_t* vptr;
 * 
 *     // allocate a new pair, but without filling the actual value. 
 *     // note vptr is given HIO_NULL for that purpose 
 *     new_pair = hio_htb_allocpair (
 *       htb, kptr, klen, HIO_NULL, HIO_HTB_VLEN(pair) + 1 + v->len); 
 *     if (new_pair == HIO_NULL) return HIO_NULL;
 * 
 *     // fill in the value space 
 *     vptr = HIO_HTB_VPTR(new_pair);
 *     hio_memcpy (vptr, HIO_HTB_VPTR(pair), HIO_HTB_VLEN(pair)*HIO_SIZEOF(hio_ooch_t));
 *     vptr += HIO_HTB_VLEN(pair)*HIO_SIZEOF(hio_ooch_t);
 *     hio_memcpy (vptr, &comma, HIO_SIZEOF(hio_ooch_t));
 *     vptr += HIO_SIZEOF(hio_ooch_t);
 *     hio_memcpy (vptr, v->ptr, v->len*HIO_SIZEOF(hio_ooch_t));
 * 
 *     // this callback requires the old pair to be destroyed 
 *     hio_htb_freepair (htb, pair);
 * 
 *     // return the new pair 
 *     return new_pair;
 *   }
 * }
 * 
 * int main ()
 * {
 *   hio_htb_t* s1;
 *   int i;
 *   hio_ooch_t* keys[] = { HIO_T("one"), HIO_T("two"), HIO_T("three") };
 *   hio_ooch_t* vals[] = { HIO_T("1"), HIO_T("2"), HIO_T("3"), HIO_T("4"), HIO_T("5") };
 * 
 *   hio_open_stdsios ();
 *   s1 = hio_htb_open (
 *     HIO_MMGR_GETDFL(), 0, 10, 70,
 *     HIO_SIZEOF(hio_ooch_t), HIO_SIZEOF(hio_ooch_t)
 *   ); // note error check is skipped 
 *   hio_htb_setstyle (s1, hio_get_htb_style(HIO_HTB_STYLE_INLINE_COPIERS));
 * 
 *   for (i = 0; i < HIO_COUNTOF(vals); i++)
 *   {
 *     hio_cstr_t ctx;
 *     ctx.ptr = vals[i]; ctx.len = hio_count_oocstr(vals[i]);
 *     hio_htb_cbsert (s1,
 *       keys[i%HIO_COUNTOF(keys)], hio_count_oocstr(keys[i%HIO_COUNTOF(keys)]),
 *       cbserter, &ctx
 *     ); // note error check is skipped
 *   }
 *   hio_htb_walk (s1, print_map_pair, HIO_NULL);
 * 
 *   hio_htb_close (s1);
 *   hio_close_stdsios ();
 *   return 0;
 * }
 * @endcode
 */
HIO_EXPORT hio_htb_pair_t* hio_htb_cbsert (
	hio_htb_t*         htb,      /**< hash table */
	void*               kptr,     /**< key pointer */
	hio_oow_t          klen,     /**< key length */
	hio_htb_cbserter_t cbserter, /**< callback function */
	void*               ctx       /**< callback context */
);

/**
 * The hio_htb_delete() function deletes a pair with a matching key 
 * @return 0 on success, -1 on failure
 */
HIO_EXPORT int hio_htb_delete (
	hio_htb_t* htb,   /**< hash table */
	const void* kptr, /**< key pointer */
	hio_oow_t klen   /**< key length */
);

/**
 * The hio_htb_clear() function empties a hash table
 */
HIO_EXPORT void hio_htb_clear (
	hio_htb_t* htb /**< hash table */
);

/**
 * The hio_htb_walk() function traverses a hash table.
 */
HIO_EXPORT void hio_htb_walk (
	hio_htb_t*       htb,    /**< hash table */
	hio_htb_walker_t walker, /**< callback function for each pair */
	void*            ctx     /**< pointer to user-specific data */
);

HIO_EXPORT void hio_init_htb_itr (
	hio_htb_itr_t* itr
);

/**
 * The hio_htb_getfirstpair() function returns the pointer to the first pair
 * in a hash table.
 */
HIO_EXPORT hio_htb_pair_t* hio_htb_getfirstpair (
	hio_htb_t*     htb,   /**< hash table */
	hio_htb_itr_t* itr    /**< iterator*/
);

/**
 * The hio_htb_getnextpair() function returns the pointer to the next pair 
 * to the current pair @a pair in a hash table.
 */
HIO_EXPORT hio_htb_pair_t* hio_htb_getnextpair (
	hio_htb_t*      htb,    /**< hash table */
	hio_htb_itr_t*  itr    /**< iterator*/
);

/**
 * The hio_htb_allocpair() function allocates a pair for a key and a value 
 * given. But it does not chain the pair allocated into the hash table @a htb.
 * Use this function at your own risk. 
 *
 * Take note of he following special behavior when the copier is 
 * #HIO_HTB_COPIER_INLINE.
 * - If @a kptr is #HIO_NULL, the key space of the size @a klen is reserved but
 *   not propagated with any data.
 * - If @a vptr is #HIO_NULL, the value space of the size @a vlen is reserved
 *   but not propagated with any data.
 */
HIO_EXPORT hio_htb_pair_t* hio_htb_allocpair (
	hio_htb_t* htb,
	void*      kptr, 
	hio_oow_t klen,	
	void*      vptr,
	hio_oow_t vlen
);

/**
 * The hio_htb_freepair() function destroys a pair. But it does not detach
 * the pair destroyed from the hash table @a htb. Use this function at your
 * own risk.
 */
HIO_EXPORT void hio_htb_freepair (
	hio_htb_t*      htb,
	hio_htb_pair_t* pair
);

/**
 * The hio_htb_dflhash() function is a default hash function.
 */
HIO_EXPORT hio_oow_t hio_htb_dflhash (
	const hio_htb_t*  htb,
	const void*       kptr,
	hio_oow_t        klen
);

/**
 * The hio_htb_dflcomp() function is default comparator.
 */
HIO_EXPORT int hio_htb_dflcomp (
	const hio_htb_t* htb,
	const void*      kptr1,
	hio_oow_t       klen1,
	const void*      kptr2,
	hio_oow_t       klen2
);

#if defined(__cplusplus)
}
#endif

#endif
