/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * BRIEF DESCRIPTION
 *
 * Some General x86_64 operations.
 * NT means Non-Temporal (Intel's terminology)
 *
 * Copyright (c) 2018 NetApp, Inc. All rights reserved.
 *
 * See module.c for LICENSE details.
 */

#ifndef __ZUS_MOVENT_H
#define __ZUS_MOVENT_H

#define CACHELINE_SHIFT	(6)
#define CACHELINE_SIZE	(1UL << CACHELINE_SHIFT)

#include <emmintrin.h>

#include "zus.h"

static inline void a_clflushopt(void *p)
{
	asm volatile(".byte 0x66; clflush %0" : "+m" (*(volatile char *)p));
}

static inline void a_clwb(void *p)
{
	asm volatile(".byte 0x66; xsaveopt %0" : "+m" (*(volatile char *)p));
}

static inline void cl_flush(void *buf, uint32_t len)
{
	uint32_t i;

	len = len + ((unsigned long)(buf) & (CACHELINE_SIZE - 1));
	for (i = 0; i < len; i += CACHELINE_SIZE)
		_mm_clflush(buf + i);
}

/*
 * clwb writes back cachelines concurrently and require a store
 * barrier (sfence) to verify completeness.
 *
 * WARNING: don't use directly, will crash old unsupported CPUs!
 */
static inline void __cl_flush_wb(void *buf, uint32_t len)
{
	uint32_t i;

	len = len + ((unsigned long)(buf) & (CACHELINE_SIZE - 1));
	for (i = 0; i < len; i += CACHELINE_SIZE)
		a_clwb(buf + i);

	_mm_sfence();
}

/*
 * clflushopt flushes cachelines concurrently and require a store
 * barrier (sfence) to verify completeness.
 *
 * WARNING: don't use directly, will crash old unsupported CPUs!
 */
static inline void __cl_flush_opt(void *buf, uint32_t len)
{
	uint32_t i;

	len = len + ((unsigned long)(buf) & (CACHELINE_SIZE - 1));
	for (i = 0; i < len; i += CACHELINE_SIZE)
		a_clflushopt(buf + i);

	_mm_sfence();
}

extern void (*cl_flush_opt)(void *buf, uint32_t len);
extern void (*cl_flush_wb)(void *buf, uint32_t len);

/* TODO use AVX-512 instructions if available PXS-245 */
static inline void _memzero_nt_cachelines(void *dst, size_t cachelines)
{
	/* must use dummy outputs so not to clobber inputs */
	ulong dummy1, dummy2;

	asm volatile (
		"xor %%rax,%%rax\n"
		"1: movnti %%rax,(%0)\n"
		"movnti %%rax,1*8(%0)\n"
		"movnti %%rax,2*8(%0)\n"
		"movnti %%rax,3*8(%0)\n"
		"movnti %%rax,4*8(%0)\n"
		"movnti %%rax,5*8(%0)\n"
		"movnti %%rax,6*8(%0)\n"
		"movnti %%rax,7*8(%0)\n"
		"leaq 64(%0),%0\n"
		"dec %1\n"
		"jnz 1b\n"
		: "=D" (dummy1), "=d" (dummy2) :
		  "D" (dst), "d" (cachelines) : "memory", "rax");
}

static inline void memzero_nt(void *dst, size_t len)
{
	size_t cachelines, prefix_len;

	/* if dst is not cacheline aligned, fill with memset */
	if (unlikely((ulong)dst & (CACHELINE_SIZE-1))) {
		prefix_len = CACHELINE_SIZE - ((ulong)dst & (CACHELINE_SIZE-1));
		if (prefix_len > len)
			prefix_len = len;
		memset(dst, 0, prefix_len);
		cl_flush(dst, prefix_len);
		len -= prefix_len;
		dst += prefix_len;
	}

	cachelines = len >> CACHELINE_SHIFT;
	if (likely(cachelines))
		_memzero_nt_cachelines(dst, cachelines);

	/* fill remaining bytes with memset */
	len -= cachelines << CACHELINE_SHIFT;
	dst += cachelines << CACHELINE_SHIFT;
	if (unlikely(len > 0)) {
		memset(dst, 0, len);
		cl_flush(dst, len);
	}
}

/* zus: nvml_movnt.c */
void *pmem_memmove_persist(void *pmemdest, const void *src, size_t len);

#define  memcpy_to_pmem pmem_memmove_persist

#endif /* ifndef __ZUS_MOVENT_H */
