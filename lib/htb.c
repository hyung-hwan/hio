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

#include <hio-htb.h>
#include "hio-prv.h"

#define pair_t          hio_htb_pair_t
#define copier_t        hio_htb_copier_t
#define freeer_t        hio_htb_freeer_t
#define hasher_t        hio_htb_hasher_t
#define comper_t        hio_htb_comper_t
#define keeper_t        hio_htb_keeper_t
#define sizer_t         hio_htb_sizer_t
#define walker_t        hio_htb_walker_t
#define cbserter_t      hio_htb_cbserter_t
#define style_t         hio_htb_style_t
#define style_kind_t    hio_htb_style_kind_t

#define KPTR(p)  HIO_HTB_KPTR(p)
#define KLEN(p)  HIO_HTB_KLEN(p)
#define VPTR(p)  HIO_HTB_VPTR(p)
#define VLEN(p)  HIO_HTB_VLEN(p)
#define NEXT(p)  HIO_HTB_NEXT(p)

#define KTOB(htb,len) ((len)*(htb)->scale[HIO_HTB_KEY])
#define VTOB(htb,len) ((len)*(htb)->scale[HIO_HTB_VAL])

HIO_INLINE pair_t* hio_htb_allocpair (hio_htb_t* htb, void* kptr, hio_oow_t klen, void* vptr, hio_oow_t vlen)
{
	pair_t* n;
	copier_t kcop, vcop;
	hio_oow_t as;

	kcop = htb->style->copier[HIO_HTB_KEY];
	vcop = htb->style->copier[HIO_HTB_VAL];

	as = HIO_SIZEOF(pair_t);
	if (kcop == HIO_HTB_COPIER_INLINE) as += HIO_ALIGN_POW2(KTOB(htb,klen), HIO_SIZEOF_VOID_P);
	if (vcop == HIO_HTB_COPIER_INLINE) as += VTOB(htb,vlen);

	n = (pair_t*) hio_allocmem(htb->hio, as);
	if (HIO_UNLIKELY(!n)) return HIO_NULL;

	NEXT(n) = HIO_NULL;

	KLEN(n) = klen;
	if (kcop == HIO_HTB_COPIER_SIMPLE)
	{
		KPTR(n) = kptr;
	}
	else if (kcop == HIO_HTB_COPIER_INLINE)
	{
		KPTR(n) = n + 1;
		/* if kptr is HIO_NULL, the inline copier does not fill
		 * the actual key area */
		if (kptr) HIO_MEMCPY (KPTR(n), kptr, KTOB(htb,klen));
	}
	else
	{
		KPTR(n) = kcop(htb, kptr, klen);
		if (KPTR(n) == HIO_NULL)
		{
			hio_freemem (htb->hio, n);
			return HIO_NULL;
		}
	}

	VLEN(n) = vlen;
	if (vcop == HIO_HTB_COPIER_SIMPLE)
	{
		VPTR(n) = vptr;
	}
	else if (vcop == HIO_HTB_COPIER_INLINE)
	{
		VPTR(n) = n + 1;
		if (kcop == HIO_HTB_COPIER_INLINE)
			VPTR(n) = (hio_uint8_t*)VPTR(n) + HIO_ALIGN_POW2(KTOB(htb,klen), HIO_SIZEOF_VOID_P);
		/* if vptr is HIO_NULL, the inline copier does not fill
		 * the actual value area */
		if (vptr) HIO_MEMCPY (VPTR(n), vptr, VTOB(htb,vlen));
	}
	else
	{
		VPTR(n) = vcop (htb, vptr, vlen);
		if (VPTR(n) != HIO_NULL)
		{
			if (htb->style->freeer[HIO_HTB_KEY] != HIO_NULL)
				htb->style->freeer[HIO_HTB_KEY] (htb, KPTR(n), KLEN(n));
			hio_freemem (htb->hio, n);
			return HIO_NULL;
		}
	}

	return n;
}

HIO_INLINE void hio_htb_freepair (hio_htb_t* htb, pair_t* pair)
{
	if (htb->style->freeer[HIO_HTB_KEY] != HIO_NULL)
		htb->style->freeer[HIO_HTB_KEY] (htb, KPTR(pair), KLEN(pair));
	if (htb->style->freeer[HIO_HTB_VAL] != HIO_NULL)
		htb->style->freeer[HIO_HTB_VAL] (htb, VPTR(pair), VLEN(pair));
	hio_freemem (htb->hio, pair);
}

static HIO_INLINE pair_t* change_pair_val (hio_htb_t* htb, pair_t* pair, void* vptr, hio_oow_t vlen)
{
	if (VPTR(pair) == vptr && VLEN(pair) == vlen)
	{
		/* if the old value and the new value are the same,
		 * it just calls the handler for this condition.
		 * No value replacement occurs. */
		if (htb->style->keeper != HIO_NULL)
		{
			htb->style->keeper (htb, vptr, vlen);
		}
	}
	else
	{
		copier_t vcop = htb->style->copier[HIO_HTB_VAL];
		void* ovptr = VPTR(pair);
		hio_oow_t ovlen = VLEN(pair);

		/* place the new value according to the copier */
		if (vcop == HIO_HTB_COPIER_SIMPLE)
		{
			VPTR(pair) = vptr;
			VLEN(pair) = vlen;
		}
		else if (vcop == HIO_HTB_COPIER_INLINE)
		{
			if (ovlen == vlen)
			{
				if (vptr) HIO_MEMCPY (VPTR(pair), vptr, VTOB(htb,vlen));
			}
			else
			{
				/* need to reconstruct the pair */
				pair_t* p = hio_htb_allocpair(htb, KPTR(pair), KLEN(pair), vptr, vlen);
				if (HIO_UNLIKELY(!p)) return HIO_NULL;
				hio_htb_freepair (htb, pair);
				return p;
			}
		}
		else
		{
			void* nvptr = vcop(htb, vptr, vlen);
			if (HIO_UNLIKELY(!nvptr)) return HIO_NULL;
			VPTR(pair) = nvptr;
			VLEN(pair) = vlen;
		}

		/* free up the old value */
		if (htb->style->freeer[HIO_HTB_VAL] != HIO_NULL)
		{
			htb->style->freeer[HIO_HTB_VAL] (htb, ovptr, ovlen);
		}
	}

	return pair;
}

static style_t style[] =
{
    	/* == HIO_HTB_STYLE_DEFAULT == */
	{
		{
			HIO_HTB_COPIER_DEFAULT,
			HIO_HTB_COPIER_DEFAULT
		},
		{
			HIO_HTB_FREEER_DEFAULT,
			HIO_HTB_FREEER_DEFAULT
		},
		HIO_HTB_COMPER_DEFAULT,
		HIO_HTB_KEEPER_DEFAULT,
		HIO_HTB_SIZER_DEFAULT,
		HIO_HTB_HASHER_DEFAULT
	},

	/* == HIO_HTB_STYLE_INLINE_COPIERS == */
	{
		{
			HIO_HTB_COPIER_INLINE,
			HIO_HTB_COPIER_INLINE
		},
		{
			HIO_HTB_FREEER_DEFAULT,
			HIO_HTB_FREEER_DEFAULT
		},
		HIO_HTB_COMPER_DEFAULT,
		HIO_HTB_KEEPER_DEFAULT,
		HIO_HTB_SIZER_DEFAULT,
		HIO_HTB_HASHER_DEFAULT
	},

	/* == HIO_HTB_STYLE_INLINE_KEY_COPIER == */
	{
		{
			HIO_HTB_COPIER_INLINE,
			HIO_HTB_COPIER_DEFAULT
		},
		{
			HIO_HTB_FREEER_DEFAULT,
			HIO_HTB_FREEER_DEFAULT
		},
		HIO_HTB_COMPER_DEFAULT,
		HIO_HTB_KEEPER_DEFAULT,
		HIO_HTB_SIZER_DEFAULT,
		HIO_HTB_HASHER_DEFAULT
	},

	/* == HIO_HTB_STYLE_INLINE_VALUE_COPIER == */
	{
		{
			HIO_HTB_COPIER_DEFAULT,
			HIO_HTB_COPIER_INLINE
		},
		{
			HIO_HTB_FREEER_DEFAULT,
			HIO_HTB_FREEER_DEFAULT
		},
		HIO_HTB_COMPER_DEFAULT,
		HIO_HTB_KEEPER_DEFAULT,
		HIO_HTB_SIZER_DEFAULT,
		HIO_HTB_HASHER_DEFAULT
	}
};

const style_t* hio_get_htb_style (style_kind_t kind)
{
	return &style[kind];
}

hio_htb_t* hio_htb_open (hio_t* hio, hio_oow_t xtnsize, hio_oow_t capa, int factor, int kscale, int vscale)
{
	hio_htb_t* htb;

	htb = (hio_htb_t*)hio_allocmem(hio, HIO_SIZEOF(hio_htb_t) + xtnsize);
	if (HIO_UNLIKELY(!htb)) return HIO_NULL;

	if (hio_htb_init(htb, hio, capa, factor, kscale, vscale) <= -1)
	{
		hio_freemem (hio, htb);
		return HIO_NULL;
	}

	HIO_MEMSET (htb + 1, 0, xtnsize);
	return htb;
}

void hio_htb_close (hio_htb_t* htb)
{
	hio_htb_fini (htb);
	hio_freemem (htb->hio, htb);
}

int hio_htb_init (hio_htb_t* htb, hio_t* hio, hio_oow_t capa, int factor, int kscale, int vscale)
{
	/* The initial capacity should be greater than 0.
	 * Otherwise, it is adjusted to 1 in the release mode */
	HIO_ASSERT (hio, capa > 0);

	/* The load factor should be between 0 and 100 inclusive.
	 * In the release mode, a value out of the range is adjusted to 100 */
	HIO_ASSERT (hio, factor >= 0 && factor <= 100);

	HIO_ASSERT (hio, kscale >= 0 && kscale <= HIO_TYPE_MAX(hio_uint8_t));
	HIO_ASSERT (hio, vscale >= 0 && vscale <= HIO_TYPE_MAX(hio_uint8_t));

	/* some initial adjustment */
	if (capa <= 0) capa = 1;
	if (factor > 100) factor = 100;

	/* do not zero out the extension */
	HIO_MEMSET (htb, 0, HIO_SIZEOF(*htb));
	htb->hio = hio;

	htb->bucket = hio_allocmem(hio, capa * HIO_SIZEOF(pair_t*));
	if (HIO_UNLIKELY(!htb->bucket)) return -1;

	/*for (i = 0; i < capa; i++) htb->bucket[i] = HIO_NULL;*/
	HIO_MEMSET (htb->bucket, 0, capa * HIO_SIZEOF(pair_t*));

	htb->factor = factor;
	htb->scale[HIO_HTB_KEY] = (kscale < 1)? 1: kscale;
	htb->scale[HIO_HTB_VAL] = (vscale < 1)? 1: vscale;

	htb->size = 0;
	htb->capa = capa;
	htb->threshold = htb->capa * htb->factor / 100;
	if (htb->capa > 0 && htb->threshold <= 0) htb->threshold = 1;

	htb->style = &style[0];
	return 0;
}

void hio_htb_fini (hio_htb_t* htb)
{
	hio_htb_clear (htb);
	hio_freemem (htb->hio, htb->bucket);
}

const style_t* hio_htb_getstyle (const hio_htb_t* htb)
{
	return htb->style;
}

void hio_htb_setstyle (hio_htb_t* htb, const style_t* style)
{
	HIO_ASSERT (htb->hio, style != HIO_NULL);
	htb->style = style;
}

hio_oow_t hio_htb_getsize (const hio_htb_t* htb)
{
	return htb->size;
}

hio_oow_t hio_htb_getcapa (const hio_htb_t* htb)
{
	return htb->capa;
}

pair_t* hio_htb_search (const hio_htb_t* htb, const void* kptr, hio_oow_t klen)
{
	pair_t* pair;
	hio_oow_t hc;

	hc = htb->style->hasher(htb,kptr,klen) % htb->capa;
	pair = htb->bucket[hc];

	while (pair != HIO_NULL)
	{
		if (htb->style->comper(htb, KPTR(pair), KLEN(pair), kptr, klen) == 0)
		{
			return pair;
		}

		pair = NEXT(pair);
	}

	hio_seterrnum (htb->hio, HIO_ENOENT);
	return HIO_NULL;
}

static HIO_INLINE int reorganize (hio_htb_t* htb)
{
	hio_oow_t i, hc, new_capa;
	pair_t** new_buck;

	if (htb->style->sizer)
	{
		new_capa = htb->style->sizer (htb, htb->capa + 1);

		/* if no change in capacity, return success
		 * without reorganization */
		if (new_capa == htb->capa) return 0;

		/* adjust to 1 if the new capacity is not reasonable */
		if (new_capa <= 0) new_capa = 1;
	}
	else
	{
		/* the bucket is doubled until it grows up to 65536 slots.
		 * once it has reached it, it grows by 65536 slots */
		new_capa = (htb->capa >= 65536)? (htb->capa + 65536): (htb->capa << 1);
	}

	new_buck = (pair_t**)hio_allocmem(htb->hio, new_capa * HIO_SIZEOF(pair_t*));
	if (HIO_UNLIKELY(!new_buck))
	{
		/* reorganization is disabled once it fails */
		htb->threshold = 0;
		return -1;
	}

	/*for (i = 0; i < new_capa; i++) new_buck[i] = HIO_NULL;*/
	HIO_MEMSET (new_buck, 0, new_capa * HIO_SIZEOF(pair_t*));

	for (i = 0; i < htb->capa; i++)
	{
		pair_t* pair = htb->bucket[i];

		while (pair != HIO_NULL)
		{
			pair_t* next = NEXT(pair);

			hc = htb->style->hasher(htb, KPTR(pair), KLEN(pair)) % new_capa;

			NEXT(pair) = new_buck[hc];
			new_buck[hc] = pair;

			pair = next;
		}
	}

	hio_freemem (htb->hio, htb->bucket);
	htb->bucket = new_buck;
	htb->capa = new_capa;
	htb->threshold = htb->capa * htb->factor / 100;

	return 0;
}

/* insert options */
#define UPSERT 1
#define UPDATE 2
#define ENSERT 3
#define INSERT 4

static HIO_INLINE pair_t* insert (hio_htb_t* htb, void* kptr, hio_oow_t klen, void* vptr, hio_oow_t vlen, int opt)
{
	pair_t* pair, * p, * prev, * next;
	hio_oow_t hc;

	hc = htb->style->hasher(htb,kptr,klen) % htb->capa;
	pair = htb->bucket[hc];
	prev = HIO_NULL;

	while (pair != HIO_NULL)
	{
		next = NEXT(pair);

		if (htb->style->comper (htb, KPTR(pair), KLEN(pair), kptr, klen) == 0)
		{
			/* found a pair with a matching key */
			switch (opt)
			{
				case UPSERT:
				case UPDATE:
					p = change_pair_val (htb, pair, vptr, vlen);
					if (p == HIO_NULL)
					{
						/* error in changing the value */
						return HIO_NULL;
					}
					if (p != pair)
					{
						/* old pair destroyed. new pair reallocated.
						 * relink to include the new pair but to drop
						 * the old pair. */
						if (prev == HIO_NULL) htb->bucket[hc] = p;
						else NEXT(prev) = p;
						NEXT(p) = next;
					}
					return p;

				case ENSERT:
					/* return existing pair */
					return pair;

				case INSERT:
					/* return failure */
					hio_seterrnum (htb->hio, HIO_EEXIST);
					return HIO_NULL;
			}
		}

		prev = pair;
		pair = next;
	}

	if (opt == UPDATE)
	{
		hio_seterrnum (htb->hio, HIO_ENOENT);
		return HIO_NULL;
	}

	if (htb->threshold > 0 && htb->size >= htb->threshold)
	{
		/* ingore reorganization error as it simply means
		 * more bucket collision and performance penalty. */
		if (reorganize(htb) == 0)
		{
			hc = htb->style->hasher(htb,kptr,klen) % htb->capa;
		}
	}

	HIO_ASSERT (htb->hio, pair == HIO_NULL);

	pair = hio_htb_allocpair (htb, kptr, klen, vptr, vlen);
	if (HIO_UNLIKELY(!pair)) return HIO_NULL; /* error */

	NEXT(pair) = htb->bucket[hc];
	htb->bucket[hc] = pair;
	htb->size++;

	return pair; /* new key added */
}

pair_t* hio_htb_upsert (hio_htb_t* htb, void* kptr, hio_oow_t klen, void* vptr, hio_oow_t vlen)
{
	return insert (htb, kptr, klen, vptr, vlen, UPSERT);
}

pair_t* hio_htb_ensert (hio_htb_t* htb, void* kptr, hio_oow_t klen, void* vptr, hio_oow_t vlen)
{
	return insert (htb, kptr, klen, vptr, vlen, ENSERT);
}

pair_t* hio_htb_insert (hio_htb_t* htb, void* kptr, hio_oow_t klen, void* vptr, hio_oow_t vlen)
{
	return insert (htb, kptr, klen, vptr, vlen, INSERT);
}


pair_t* hio_htb_update (hio_htb_t* htb, void* kptr, hio_oow_t klen, void* vptr, hio_oow_t vlen)
{
	return insert (htb, kptr, klen, vptr, vlen, UPDATE);
}

pair_t* hio_htb_cbsert (hio_htb_t* htb, void* kptr, hio_oow_t klen, cbserter_t cbserter, void* ctx)
{
	pair_t* pair, * p, * prev, * next;
	hio_oow_t hc;

	hc = htb->style->hasher(htb,kptr,klen) % htb->capa;
	pair = htb->bucket[hc];
	prev = HIO_NULL;

	while (pair != HIO_NULL)
	{
		next = NEXT(pair);

		if (htb->style->comper(htb, KPTR(pair), KLEN(pair), kptr, klen) == 0)
		{
			/* found a pair with a matching key */
			p = cbserter(htb, pair, kptr, klen, ctx);
			if (p == HIO_NULL)
			{
				/* error returned by the callback function */
				return HIO_NULL;
			}
			if (p != pair)
			{
				/* old pair destroyed. new pair reallocated.
				 * relink to include the new pair but to drop
				 * the old pair. */
				if (prev == HIO_NULL) htb->bucket[hc] = p;
				else NEXT(prev) = p;
				NEXT(p) = next;
			}
			return p;
		}

		prev = pair;
		pair = next;
	}

	if (htb->threshold > 0 && htb->size >= htb->threshold)
	{
		/* ingore reorganization error as it simply means
		 * more bucket collision and performance penalty. */
		if (reorganize(htb) == 0)
		{
			hc = htb->style->hasher(htb,kptr,klen) % htb->capa;
		}
	}

	HIO_ASSERT (htb->hio, pair == HIO_NULL);

	pair = cbserter(htb, HIO_NULL, kptr, klen, ctx);
	if (HIO_UNLIKELY(!pair)) return HIO_NULL; /* error */

	NEXT(pair) = htb->bucket[hc];
	htb->bucket[hc] = pair;
	htb->size++;

	return pair; /* new key added */
}

int hio_htb_delete (hio_htb_t* htb, const void* kptr, hio_oow_t klen)
{
	pair_t* pair, * prev;
	hio_oow_t hc;

	hc = htb->style->hasher(htb,kptr,klen) % htb->capa;
	pair = htb->bucket[hc];
	prev = HIO_NULL;

	while (pair != HIO_NULL)
	{
		if (htb->style->comper(htb, KPTR(pair), KLEN(pair), kptr, klen) == 0)
		{
			if (prev == HIO_NULL)
				htb->bucket[hc] = NEXT(pair);
			else NEXT(prev) = NEXT(pair);

			hio_htb_freepair (htb, pair);
			htb->size--;

			return 0;
		}

		prev = pair;
		pair = NEXT(pair);
	}

	hio_seterrnum (htb->hio, HIO_ENOENT);
	return -1;
}

void hio_htb_clear (hio_htb_t* htb)
{
	hio_oow_t i;
	pair_t* pair, * next;

	for (i = 0; i < htb->capa; i++)
	{
		pair = htb->bucket[i];

		while (pair != HIO_NULL)
		{
			next = NEXT(pair);
			hio_htb_freepair (htb, pair);
			htb->size--;
			pair = next;
		}

		htb->bucket[i] = HIO_NULL;
	}
}

void hio_htb_walk (hio_htb_t* htb, walker_t walker, void* ctx)
{
	hio_oow_t i;
	pair_t* pair, * next;

	for (i = 0; i < htb->capa; i++)
	{
		pair = htb->bucket[i];

		while (pair != HIO_NULL)
		{
			next = NEXT(pair);
			if (walker(htb, pair, ctx) == HIO_HTB_WALK_STOP) return;
			pair = next;
		}
	}
}


void hio_init_htb_itr (hio_htb_itr_t* itr)
{
	itr->pair = HIO_NULL;
	itr->buckno = 0;
}

pair_t* hio_htb_getfirstpair (hio_htb_t* htb, hio_htb_itr_t* itr)
{
	hio_oow_t i;
	pair_t* pair;

	for (i = 0; i < htb->capa; i++)
	{
		pair = htb->bucket[i];
		if (pair)
		{
			itr->buckno = i;
			itr->pair = pair;
			return pair;
		}
	}

	return HIO_NULL;
}

pair_t* hio_htb_getnextpair (hio_htb_t* htb, hio_htb_itr_t* itr)
{
	hio_oow_t i;
	pair_t* pair;

	pair = NEXT(itr->pair);
	if (pair)
	{
		/* no change in bucket number */
		itr->pair = pair;
		return pair;
	}

	for (i = itr->buckno + 1; i < htb->capa; i++)
	{
		pair = htb->bucket[i];
		if (pair)
		{
			itr->buckno = i;
			itr->pair = pair;
			return pair;
		}
	}

	return HIO_NULL;
}

hio_oow_t hio_htb_dflhash (const hio_htb_t* htb, const void* kptr, hio_oow_t klen)
{
	hio_oow_t h;
	HIO_HASH_BYTES (h, kptr, klen);
	return h ;
}

int hio_htb_dflcomp (const hio_htb_t* htb, const void* kptr1, hio_oow_t klen1, const void* kptr2, hio_oow_t klen2)
{
	if (klen1 == klen2) return HIO_MEMCMP (kptr1, kptr2, KTOB(htb,klen1));
	/* it just returns 1 to indicate that they are different. */
	return 1;
}

