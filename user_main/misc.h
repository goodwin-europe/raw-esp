#pragma once
#include "c_types.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(*(x)))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
// I'm not completely sure that those functions really do what they should.
static inline uint32_t irq_save()
{
	uint32_t level;
	asm volatile ("RSIL %0, 15" : "=r"(level));
	return level;
}

static inline void irq_restore(uint32_t level)
{
	uint32_t tmp, ps;
	// IRQs should be disabled, since we need to do following operations
	// atomically
	level = level & 0xf;
	asm volatile ("RSIL %0, 15\n" : "=r"(tmp));
	asm volatile ("RSR.PS %0" : "=r"(ps));
	ps = (ps & 0xfffffff0) | level;
	asm volatile ("WSR.PS %0" : : "r"(ps));
}

