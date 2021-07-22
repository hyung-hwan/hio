/*
 * $Id$
 *
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

#include <mio-htb.h>
#include "mio-prv.h"

#define pair_t          mio_htb_pair_t
#define copier_t        mio_htb_copier_t
#define freeer_t        mio_htb_freeer_t
#define hasher_t        mio_htb_hasher_t
#define comper_t        mio_htb_comper_t
#define keeper_t        mio_htb_keeper_t
#define sizer_t         mio_htb_sizer_t
#define walker_t        mio_htb_walker_t
#define cbserter_t      mio_htb_cbserter_t
#define style_t         mio_htb_style_t
#define style_kind_t    mio_htb_style_kind_t

#define KPTR(p)  MIO_HTB_KPTR(p)
#define KLEN(p)  MIO_HTB_KLEN(p)
#define VPTR(p)  MIO_HTB_VPTR(p)
#define VLEN(p)  MIO_HTB_VLEN(p)
#define NEXT(p)  MIO_HTB_NEXT(p)

#define KTOB(htb,len) ((len)*(htb)->scale[MIO_HTB_KEY])
#define VTOB(htb,len) ((len)*(htb)->scale[MIO_HTB_VAL])

MIO_INLINE pair_t* mio_htb_allocpair (mio_htb_t* htb, void* kptr, mio_oow_t klen, void* vptr, mio_oow_t vlen)
{
	pair_t* n;
	copier_t kcop, vcop;
	mio_oow_t as;

	kcop = htb->style->copier[MIO_HTB_KEY];
	vcop = htb->style->copier[MIO_HTB_VAL];

	as = MIO_SIZEOF(pair_t);
	if (kcop == MIO_HTB_COPIER_INLINE) as += MIO_ALIGN_POW2(KTOB(htb,klen), MIO_SIZEOF_VOID_P);
	if (vcop == MIO_HTB_COPIER_INLINE) as += VTOB(htb,vlen);

	n = (pair_t*) mio_allocmem(htb->mio, as);
	if (MIO_UNLIKELY(!n)) return MIO_NULL;

	NEXT(n) = MIO_NULL;

	KLEN(n) = klen;
	if (kcop == MIO_HTB_COPIER_SIMPLE)
	{
		KPTR(n) = kptr;
	}
	else if (kcop == MIO_HTB_COPIER_INLINE)
	{
		KPTR(n) = n + 1;
		/* if kptr is MIO_NULL, the inline copier does not fill
		 * the actual key area */
		if (kptr) MIO_MEMCPY (KPTR(n), kptr, KTOB(htb,klen));
	}
	else 
	{
		KPTR(n) = kcop(htb, kptr, klen);
		if (KPTR(n) == MIO_NULL)
		{
			mio_freemem (htb->mio, n);
			return MIO_NULL;
		}
	}

	VLEN(n) = vlen;
	if (vcop == MIO_HTB_COPIER_SIMPLE)
	{
		VPTR(n) = vptr;
	}
	else if (vcop == MIO_HTB_COPIER_INLINE)
	{
		VPTR(n) = n + 1;
		if (kcop == MIO_HTB_COPIER_INLINE) 
			VPTR(n) = (mio_uint8_t*)VPTR(n) + MIO_ALIGN_POW2(KTOB(htb,klen), MIO_SIZEOF_VOID_P);
		/* if vptr is MIO_NULL, the inline copier does not fill
		 * the actual value area */
		if (vptr) MIO_MEMCPY (VPTR(n), vptr, VTOB(htb,vlen));
	}
	else 
	{
		VPTR(n) = vcop (htb, vptr, vlen);
		if (VPTR(n) != MIO_NULL)
		{
			if (htb->style->freeer[MIO_HTB_KEY] != MIO_NULL)
				htb->style->freeer[MIO_HTB_KEY] (htb, KPTR(n), KLEN(n));
			mio_freemem (htb->mio, n);
			return MIO_NULL;
		}
	}

	return n;
}

MIO_INLINE void mio_htb_freepair (mio_htb_t* htb, pair_t* pair)
{
	if (htb->style->freeer[MIO_HTB_KEY] != MIO_NULL) 
		htb->style->freeer[MIO_HTB_KEY] (htb, KPTR(pair), KLEN(pair));
	if (htb->style->freeer[MIO_HTB_VAL] != MIO_NULL)
		htb->style->freeer[MIO_HTB_VAL] (htb, VPTR(pair), VLEN(pair));
	mio_freemem (htb->mio, pair);	
}

static MIO_INLINE pair_t* change_pair_val (mio_htb_t* htb, pair_t* pair, void* vptr, mio_oow_t vlen)
{
	if (VPTR(pair) == vptr && VLEN(pair) == vlen) 
	{
		/* if the old value and the new value are the same,
		 * it just calls the handler for this condition. 
		 * No value replacement occurs. */
		if (htb->style->keeper != MIO_NULL)
		{
			htb->style->keeper (htb, vptr, vlen);
		}
	}
	else
	{
		copier_t vcop = htb->style->copier[MIO_HTB_VAL];
		void* ovptr = VPTR(pair);
		mio_oow_t ovlen = VLEN(pair);

		/* place the new value according to the copier */
		if (vcop == MIO_HTB_COPIER_SIMPLE)
		{
			VPTR(pair) = vptr;
			VLEN(pair) = vlen;
		}
		else if (vcop == MIO_HTB_COPIER_INLINE)
		{
			if (ovlen == vlen)
			{
				if (vptr) MIO_MEMCPY (VPTR(pair), vptr, VTOB(htb,vlen));
			}
			else
			{
				/* need to reconstruct the pair */
				pair_t* p = mio_htb_allocpair(htb, KPTR(pair), KLEN(pair), vptr, vlen);
				if (MIO_UNLIKELY(!p)) return MIO_NULL;
				mio_htb_freepair (htb, pair);
				return p;
			}
		}
		else 
		{
			void* nvptr = vcop(htb, vptr, vlen);
			if (MIO_UNLIKELY(!nvptr)) return MIO_NULL;
			VPTR(pair) = nvptr;
			VLEN(pair) = vlen;
		}

		/* free up the old value */
		if (htb->style->freeer[MIO_HTB_VAL] != MIO_NULL) 
		{
			htb->style->freeer[MIO_HTB_VAL] (htb, ovptr, ovlen);
		}
	}

	return pair;
}

static style_t style[] =
{
    	/* == MIO_HTB_STYLE_DEFAULT == */
	{
		{
			MIO_HTB_COPIER_DEFAULT,
			MIO_HTB_COPIER_DEFAULT
		},
		{
			MIO_HTB_FREEER_DEFAULT,
			MIO_HTB_FREEER_DEFAULT
		},
		MIO_HTB_COMPER_DEFAULT,
		MIO_HTB_KEEPER_DEFAULT,
		MIO_HTB_SIZER_DEFAULT,
		MIO_HTB_HASHER_DEFAULT
	},

	/* == MIO_HTB_STYLE_INLINE_COPIERS == */
	{
		{
			MIO_HTB_COPIER_INLINE,
			MIO_HTB_COPIER_INLINE
		},
		{
			MIO_HTB_FREEER_DEFAULT,
			MIO_HTB_FREEER_DEFAULT
		},
		MIO_HTB_COMPER_DEFAULT,
		MIO_HTB_KEEPER_DEFAULT,
		MIO_HTB_SIZER_DEFAULT,
		MIO_HTB_HASHER_DEFAULT
	},

	/* == MIO_HTB_STYLE_INLINE_KEY_COPIER == */
	{
		{
			MIO_HTB_COPIER_INLINE,
			MIO_HTB_COPIER_DEFAULT
		},
		{
			MIO_HTB_FREEER_DEFAULT,
			MIO_HTB_FREEER_DEFAULT
		},
		MIO_HTB_COMPER_DEFAULT,
		MIO_HTB_KEEPER_DEFAULT,
		MIO_HTB_SIZER_DEFAULT,
		MIO_HTB_HASHER_DEFAULT
	},

	/* == MIO_HTB_STYLE_INLINE_VALUE_COPIER == */
	{
		{
			MIO_HTB_COPIER_DEFAULT,
			MIO_HTB_COPIER_INLINE
		},
		{
			MIO_HTB_FREEER_DEFAULT,
			MIO_HTB_FREEER_DEFAULT
		},
		MIO_HTB_COMPER_DEFAULT,
		MIO_HTB_KEEPER_DEFAULT,
		MIO_HTB_SIZER_DEFAULT,
		MIO_HTB_HASHER_DEFAULT
	}
};

const style_t* mio_get_htb_style (style_kind_t kind)
{
	return &style[kind];
}

mio_htb_t* mio_htb_open (mio_t* mio, mio_oow_t xtnsize, mio_oow_t capa, int factor, int kscale, int vscale)
{
	mio_htb_t* htb;

	htb = (mio_htb_t*)mio_allocmem(mio, MIO_SIZEOF(mio_htb_t) + xtnsize);
	if (MIO_UNLIKELY(!htb)) return MIO_NULL;

	if (mio_htb_init(htb, mio, capa, factor, kscale, vscale) <= -1)
	{
		mio_freemem (mio, htb);
		return MIO_NULL;
	}

	MIO_MEMSET (htb + 1, 0, xtnsize);
	return htb;
}

void mio_htb_close (mio_htb_t* htb)
{
	mio_htb_fini (htb);
	mio_freemem (htb->mio, htb);
}

int mio_htb_init (mio_htb_t* htb, mio_t* mio, mio_oow_t capa, int factor, int kscale, int vscale)
{
	/* The initial capacity should be greater than 0. 
	 * Otherwise, it is adjusted to 1 in the release mode */
	MIO_ASSERT (mio, capa > 0);

	/* The load factor should be between 0 and 100 inclusive. 
	 * In the release mode, a value out of the range is adjusted to 100 */
	MIO_ASSERT (mio, factor >= 0 && factor <= 100);

	MIO_ASSERT (mio, kscale >= 0 && kscale <= MIO_TYPE_MAX(mio_uint8_t));
	MIO_ASSERT (mio, vscale >= 0 && vscale <= MIO_TYPE_MAX(mio_uint8_t));

	/* some initial adjustment */
	if (capa <= 0) capa = 1;
	if (factor > 100) factor = 100;

	/* do not zero out the extension */
	MIO_MEMSET (htb, 0, MIO_SIZEOF(*htb));
	htb->mio = mio;

	htb->bucket = mio_allocmem(mio, capa * MIO_SIZEOF(pair_t*));
	if (MIO_UNLIKELY(!htb->bucket)) return -1;

	/*for (i = 0; i < capa; i++) htb->bucket[i] = MIO_NULL;*/
	MIO_MEMSET (htb->bucket, 0, capa * MIO_SIZEOF(pair_t*));

	htb->factor = factor;
	htb->scale[MIO_HTB_KEY] = (kscale < 1)? 1: kscale;
	htb->scale[MIO_HTB_VAL] = (vscale < 1)? 1: vscale;

	htb->size = 0;
	htb->capa = capa;
	htb->threshold = htb->capa * htb->factor / 100;
	if (htb->capa > 0 && htb->threshold <= 0) htb->threshold = 1;

	htb->style = &style[0];
	return 0;
}

void mio_htb_fini (mio_htb_t* htb)
{
	mio_htb_clear (htb);
	mio_freemem (htb->mio, htb->bucket);
}

const style_t* mio_htb_getstyle (const mio_htb_t* htb)
{
	return htb->style;
}

void mio_htb_setstyle (mio_htb_t* htb, const style_t* style)
{
	MIO_ASSERT (htb->mio, style != MIO_NULL);
	htb->style = style;
}

mio_oow_t mio_htb_getsize (const mio_htb_t* htb)
{
	return htb->size;
}

mio_oow_t mio_htb_getcapa (const mio_htb_t* htb)
{
	return htb->capa;
}

pair_t* mio_htb_search (const mio_htb_t* htb, const void* kptr, mio_oow_t klen)
{
	pair_t* pair;
	mio_oow_t hc;

	hc = htb->style->hasher(htb,kptr,klen) % htb->capa;
	pair = htb->bucket[hc];

	while (pair != MIO_NULL) 
	{
		if (htb->style->comper(htb, KPTR(pair), KLEN(pair), kptr, klen) == 0)
		{
			return pair;
		}

		pair = NEXT(pair);
	}

	mio_seterrnum (htb->mio, MIO_ENOENT);
	return MIO_NULL;
}

static MIO_INLINE int reorganize (mio_htb_t* htb)
{
	mio_oow_t i, hc, new_capa;
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

	new_buck = (pair_t**)mio_allocmem(htb->mio, new_capa * MIO_SIZEOF(pair_t*));
	if (MIO_UNLIKELY(!new_buck)) 
	{
		/* reorganization is disabled once it fails */
		htb->threshold = 0;
		return -1;
	}

	/*for (i = 0; i < new_capa; i++) new_buck[i] = MIO_NULL;*/
	MIO_MEMSET (new_buck, 0, new_capa * MIO_SIZEOF(pair_t*));

	for (i = 0; i < htb->capa; i++)
	{
		pair_t* pair = htb->bucket[i];

		while (pair != MIO_NULL) 
		{
			pair_t* next = NEXT(pair);

			hc = htb->style->hasher(htb, KPTR(pair), KLEN(pair)) % new_capa;

			NEXT(pair) = new_buck[hc];
			new_buck[hc] = pair;

			pair = next;
		}
	}

	mio_freemem (htb->mio, htb->bucket);
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

static MIO_INLINE pair_t* insert (mio_htb_t* htb, void* kptr, mio_oow_t klen, void* vptr, mio_oow_t vlen, int opt)
{
	pair_t* pair, * p, * prev, * next;
	mio_oow_t hc;

	hc = htb->style->hasher(htb,kptr,klen) % htb->capa;
	pair = htb->bucket[hc];
	prev = MIO_NULL;

	while (pair != MIO_NULL) 
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
					if (p == MIO_NULL) 
					{
						/* error in changing the value */
						return MIO_NULL; 
					}
					if (p != pair) 
					{
						/* old pair destroyed. new pair reallocated.
						 * relink to include the new pair but to drop
						 * the old pair. */
						if (prev == MIO_NULL) htb->bucket[hc] = p;
						else NEXT(prev) = p;
						NEXT(p) = next; 
					}
					return p;

				case ENSERT:
					/* return existing pair */
					return pair; 

				case INSERT:
					/* return failure */
					mio_seterrnum (htb->mio, MIO_EEXIST);
					return MIO_NULL;
			}
		}

		prev = pair;
		pair = next;
	}

	if (opt == UPDATE) 
	{
		mio_seterrnum (htb->mio, MIO_ENOENT);
		return MIO_NULL;
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

	MIO_ASSERT (htb->mio, pair == MIO_NULL);

	pair = mio_htb_allocpair (htb, kptr, klen, vptr, vlen);
	if (MIO_UNLIKELY(!pair)) return MIO_NULL; /* error */

	NEXT(pair) = htb->bucket[hc];
	htb->bucket[hc] = pair;
	htb->size++;

	return pair; /* new key added */
}

pair_t* mio_htb_upsert (mio_htb_t* htb, void* kptr, mio_oow_t klen, void* vptr, mio_oow_t vlen)
{
	return insert (htb, kptr, klen, vptr, vlen, UPSERT);
}

pair_t* mio_htb_ensert (mio_htb_t* htb, void* kptr, mio_oow_t klen, void* vptr, mio_oow_t vlen)
{
	return insert (htb, kptr, klen, vptr, vlen, ENSERT);
}

pair_t* mio_htb_insert (mio_htb_t* htb, void* kptr, mio_oow_t klen, void* vptr, mio_oow_t vlen)
{
	return insert (htb, kptr, klen, vptr, vlen, INSERT);
}


pair_t* mio_htb_update (mio_htb_t* htb, void* kptr, mio_oow_t klen, void* vptr, mio_oow_t vlen)
{
	return insert (htb, kptr, klen, vptr, vlen, UPDATE);
}

pair_t* mio_htb_cbsert (mio_htb_t* htb, void* kptr, mio_oow_t klen, cbserter_t cbserter, void* ctx)
{
	pair_t* pair, * p, * prev, * next;
	mio_oow_t hc;

	hc = htb->style->hasher(htb,kptr,klen) % htb->capa;
	pair = htb->bucket[hc];
	prev = MIO_NULL;

	while (pair != MIO_NULL) 
	{
		next = NEXT(pair);

		if (htb->style->comper(htb, KPTR(pair), KLEN(pair), kptr, klen) == 0) 
		{
			/* found a pair with a matching key */
			p = cbserter(htb, pair, kptr, klen, ctx);
			if (p == MIO_NULL) 
			{
				/* error returned by the callback function */
				return MIO_NULL; 
			}
			if (p != pair) 
			{
				/* old pair destroyed. new pair reallocated.
				 * relink to include the new pair but to drop
				 * the old pair. */
				if (prev == MIO_NULL) htb->bucket[hc] = p;
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

	MIO_ASSERT (htb->mio, pair == MIO_NULL);

	pair = cbserter(htb, MIO_NULL, kptr, klen, ctx);
	if (MIO_UNLIKELY(!pair)) return MIO_NULL; /* error */

	NEXT(pair) = htb->bucket[hc];
	htb->bucket[hc] = pair;
	htb->size++;

	return pair; /* new key added */
}

int mio_htb_delete (mio_htb_t* htb, const void* kptr, mio_oow_t klen)
{
	pair_t* pair, * prev;
	mio_oow_t hc;

	hc = htb->style->hasher(htb,kptr,klen) % htb->capa;
	pair = htb->bucket[hc];
	prev = MIO_NULL;

	while (pair != MIO_NULL) 
	{
		if (htb->style->comper(htb, KPTR(pair), KLEN(pair), kptr, klen) == 0) 
		{
			if (prev == MIO_NULL) 
				htb->bucket[hc] = NEXT(pair);
			else NEXT(prev) = NEXT(pair);

			mio_htb_freepair (htb, pair);
			htb->size--;

			return 0;
		}

		prev = pair;
		pair = NEXT(pair);
	}

	mio_seterrnum (htb->mio, MIO_ENOENT);
	return -1;
}

void mio_htb_clear (mio_htb_t* htb)
{
	mio_oow_t i;
	pair_t* pair, * next;

	for (i = 0; i < htb->capa; i++) 
	{
		pair = htb->bucket[i];

		while (pair != MIO_NULL) 
		{
			next = NEXT(pair);
			mio_htb_freepair (htb, pair);
			htb->size--;
			pair = next;
		}

		htb->bucket[i] = MIO_NULL;
	}
}

void mio_htb_walk (mio_htb_t* htb, walker_t walker, void* ctx)
{
	mio_oow_t i;
	pair_t* pair, * next;

	for (i = 0; i < htb->capa; i++) 
	{
		pair = htb->bucket[i];

		while (pair != MIO_NULL) 
		{
			next = NEXT(pair);
			if (walker(htb, pair, ctx) == MIO_HTB_WALK_STOP) return;
			pair = next;
		}
	}
}


void mio_init_htb_itr (mio_htb_itr_t* itr)
{
	itr->pair = MIO_NULL;
	itr->buckno = 0;
}

pair_t* mio_htb_getfirstpair (mio_htb_t* htb, mio_htb_itr_t* itr)
{
	mio_oow_t i;
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

	return MIO_NULL;
}

pair_t* mio_htb_getnextpair (mio_htb_t* htb, mio_htb_itr_t* itr)
{
	mio_oow_t i;
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

	return MIO_NULL;
}

mio_oow_t mio_htb_dflhash (const mio_htb_t* htb, const void* kptr, mio_oow_t klen)
{
	mio_oow_t h;
	MIO_HASH_BYTES (h, kptr, klen);
	return h ; 
}

int mio_htb_dflcomp (const mio_htb_t* htb, const void* kptr1, mio_oow_t klen1, const void* kptr2, mio_oow_t klen2)
{
	if (klen1 == klen2) return MIO_MEMCMP (kptr1, kptr2, KTOB(htb,klen1));
	/* it just returns 1 to indicate that they are different. */
	return 1;
}

