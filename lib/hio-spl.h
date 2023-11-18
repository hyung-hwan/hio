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

#ifndef _HIO_SPL_H_
#define _HIO_SPL_H_

#include <hio-cmn.h>

#define HIO_SUPPORT_SPL

typedef volatile hio_uint32_t hio_spl_t;

#define HIO_SPL_INIT (0)

#if defined(HIO_HAVE_INLINE)
static HIO_INLINE void hio_spl_init (hio_spl_t* spl) { *spl = HIO_SPL_INIT; }
#else
#define hio_spl_init(spl) ((*(spl)) = HIO_SPL_INIT)
#endif

#if defined(HIO_HAVE_SYNC_LOCK_TEST_AND_SET) && defined(HIO_HAVE_SYNC_LOCK_RELEASE)
/* =======================================================================
 * MODERN COMPILERS WITH BUILTIN ATOMICS
 * ======================================================================= */

#if defined(HIO_HAVE_INLINE)
static HIO_INLINE int hio_spl_trylock (hio_spl_t* spl) { return !__sync_lock_test_and_set(spl, 1); }
static HIO_INLINE void hio_spl_lock (hio_spl_t* spl) { while(__sync_lock_test_and_set(spl, 1)); }
static HIO_INLINE void hio_spl_unlock (hio_spl_t* spl) { __sync_lock_release(spl); }
#else
#	define hio_spl_trylock(spl) (!__sync_lock_test_and_set(spl, 1))
#	define hio_spl_lock(spl) do {} while(__sync_lock_test_and_set(spl, 1))
#	define hio_spl_unlock(spl) (__sync_lock_release(spl))
#endif

#elif defined(_SCO_DS)
/* =======================================================================
 * SCO DEVELOPEMENT SYSTEM
 *
 *  NOTE: when the asm macros were indented, the compiler/linker ended up
 *        with undefined symbols. never indent hio_spl_xxx macros.
 * ======================================================================= */
asm int hio_spl_trylock (hio_spl_t* spl)
{
%reg spl
	movl   $1, %eax
	xchgl  (spl), %eax
	xorl   $1, %eax     / return zero on failure, non-zero on success

%mem spl
	movl  spl,  %ecx
	movl  $1,     %eax
	xchgl (%ecx), %eax
	xorl  $1,     %eax  / return zero on failure, non-zero on success
}

#if 0
/* i can't figure out how to make jump labels unique when there are
 * multiple occurrences of hio_spl_lock(). so let me just use the while loop
 * instead. */
asm void hio_spl_lock (hio_spl_t* spl)
{
%reg spl
.lock_set_loop:
	movl  $1, %eax
	xchgl (spl), %eax
	testl %eax, %eax      / set ZF to 1 if eax is zero, 0 if eax is non-zero
	jne    .lock_set_loop / if ZF is 0(eax is non-zero), loop around

%mem spl
.lock_set_loop:
	movl  spl,  %ecx
	movl  $1,     %eax
	xchgl (%ecx), %eax
	testl %eax, %eax      / set ZF to 1 if eax is zero, 0 if eax is non-zero
	jne   .lock_set_loop  / if ZF is 0(eax is non-zero), loop around
}
#else
#define hio_spl_lock(x) do {} while(!spl_trylock(x))
#endif

#if 0
asm void hio_spl_unlock (moo_uint8_t* spl)
{
%reg spl
	movl  $0,      %eax
	xchgl (spl), %eax

%mem spl
	movl  spl,  %ecx
	movl  $0,     %eax
	xchgl (%ecx), %eax
}
#else
asm void hio_spl_unlock (hio_spl_t* spl)
{
	/* don't need xchg as movl on an aligned data is atomic */
	/* mfence is 0F AE F0 */
%reg spl
	.byte 0x0F
	.byte 0xAE
	.byte 0xF0
	movl $0, (spl)

%mem spl
	.byte 0x0F
	.byte 0xAE
	.byte 0xF0
	movl spl, %ecx
	movl $0, (%ecx)
}
#endif

#elif defined(__GNUC__) && (defined(__x86_64) || defined(__amd64) || defined(__i386) || defined(i386))

/* =======================================================================
 * OLD GNU COMPILER FOR x86 and x86_64
 * ======================================================================= */

static HIO_INLINE int hio_spl_trylock (hio_spl_t* spl)
{
	register int x = 1;
	__asm__ volatile (
		"xchgl %0, (%2)\n"
		: "=r"(x)
		: "0"(x), "r"(spl)
		: "memory"
	);
	return !x;
}

static HIO_INLINE void hio_spl_lock (hio_spl_t* spl)
{
	register int x = 1;
	do
	{
		__asm__ volatile (
			"xchgl %0, (%2)\n"
			: "=r"(x)
			: "0"(x), "r"(spl)
			: "memory"
		);
	}
	while (x);
}

static HIO_INLINE void hio_spl_unlock (hio_spl_t* spl)
{
#if defined(__x86_64) || defined(__amd64)
	__asm__ volatile (
		"mfence\n\t"
		"movl $0, (%0)\n"
		:
		:"r"(spl)
		:"memory"
	);
#else
	__asm__ volatile (
		"movl $0, (%0)\n"
		:
		:"r"(spl)
		:"memory"
	);
#endif
}

#elif defined(__GNUC__) && (defined(__POWERPC__) || defined(__powerpc) || defined(__powerpc__) || defined(__ppc))

/* =======================================================================
 * OLD GNU COMPILER FOR ppc
 * ======================================================================= */

static HIO_INLINE int hio_spl_trylock (hio_spl_t* spl)
{

	/* lwarx	RT, RA, RB
	 *  RT Specifies target general-purpose register where result of operation is stored.
	 *  RA Specifies source general-purpose register for EA calculation.
	 *  RB Specifies source general-purpose register for EA calculation.
	 *
	 * If general-purpose register (GPR) RA = 0, the effective address (EA) is the
	 * content of GPR RB. Otherwise, the EA is the sum of the content of GPR RA
	 * plus the content of GPR RB.

	 * The lwarx instruction loads the word from the location in storage specified
	 * by the EA into the target GPR RT. In addition, a reservation on the memory
	 * location is created for use by a subsequent stwcx. instruction.

	 * The lwarx instruction has one syntax form and does not affect the
	 * Fixed-Point Exception Register. If the EA is not a multiple of 4,
	 * the results are boundedly undefined.
	 */

	unsigned int rc;

	__asm__ volatile (
		"1:\n"
		"lwarx        %0,0,%1\n"  /* load and reserve. rc(%0) = *spl(%1) */
		"cmpwi        cr0,%0,0\n" /* cr0 = (rc compare-with 0) */
		"li           %0,0\n"     /* rc = 0(failure) */
		"bne          cr0,2f\n"   /* if cr0 != 0, goto 2; */
		"li           %0,1\n"     /* rc = 1(success) */
		"stwcx.       %0,0,%1\n"  /* *spl(%1) = 1(value in rc) if reserved */
		"bne          cr0,1b\n"   /* if reservation is lost, goto 1 */
	#if 1
		"lwsync\n"
	#else
		"isync\n"
	#endif
		"2:\n"
		: "=&r"(rc)
		: "r"(spl)
		: "cr0", "memory"
	);

	return rc;
}

static HIO_INLINE void hio_spl_lock (hio_spl_t* spl)
{
	while (!hio_spl_trylock(spl)) /* nothing */;
}

static HIO_INLINE void hio_spl_unlock (hio_spl_t* spl)
{
	__asm__ volatile (
	#if 1
		"lwsync\n"
	#elif 0
		"sync\n"
	#else
		"eieio\n"
	#endif
		:
		:
		: "memory"
	);
	*spl = 0;
}

#elif defined(HIO_SPL_NO_UNSUPPORTED_ERROR)
	/* don't raise the compile time error */
	#undef HIO_SUPPORT_SPL
#else
	#undef HIO_SUPPORT_SPL
#	error UNSUPPORTED
#endif


#endif
