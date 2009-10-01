#ifndef _URCU_QSBR_STATIC_H
#define _URCU_QSBR_STATIC_H

/*
 * urcu-qsbr-static.h
 *
 * Userspace RCU QSBR header.
 *
 * TO BE INCLUDED ONLY IN LGPL-COMPATIBLE CODE. See urcu-qsbr.h for linking
 * dynamically with the userspace rcu QSBR library.
 *
 * Copyright (c) 2009 Mathieu Desnoyers <mathieu.desnoyers@polymtl.ca>
 * Copyright (c) 2009 Paul E. McKenney, IBM Corporation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * IBM's contributions to this file may be relicensed under LGPLv2 or later.
 */

#include <stdlib.h>
#include <pthread.h>
#include <assert.h>
#include <limits.h>
#include <syscall.h>
#include <unistd.h>

#include <urcu/compiler.h>
#include <urcu/arch.h>
#include <urcu/system.h>
#include <urcu/arch_uatomic.h>
#include <urcu/list.h>

#define futex(...)		syscall(__NR_futex, __VA_ARGS__)
#define FUTEX_WAIT		0
#define FUTEX_WAKE		1

/*
 * This code section can only be included in LGPL 2.1 compatible source code.
 * See below for the function call wrappers which can be used in code meant to
 * be only linked with the Userspace RCU library. This comes with a small
 * performance degradation on the read-side due to the added function calls.
 * This is required to permit relinking with newer versions of the library.
 */

/*
 * If a reader is really non-cooperative and refuses to commit its
 * urcu_reader.ctr count to memory (there is no barrier in the reader
 * per-se), kick it after a few loops waiting for it.
 */
#define KICK_READER_LOOPS 10000

/*
 * Active attempts to check for reader Q.S. before calling futex().
 */
#define RCU_QS_ACTIVE_ATTEMPTS 100

#ifdef DEBUG_RCU
#define rcu_assert(args...)	assert(args)
#else
#define rcu_assert(args...)
#endif

#ifdef DEBUG_YIELD
#include <sched.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>

#define YIELD_READ 	(1 << 0)
#define YIELD_WRITE	(1 << 1)

/* maximum sleep delay, in us */
#define MAX_SLEEP 50

extern unsigned int yield_active;
extern unsigned int __thread rand_yield;

static inline void debug_yield_read(void)
{
	if (yield_active & YIELD_READ)
		if (rand_r(&rand_yield) & 0x1)
			usleep(rand_r(&rand_yield) % MAX_SLEEP);
}

static inline void debug_yield_write(void)
{
	if (yield_active & YIELD_WRITE)
		if (rand_r(&rand_yield) & 0x1)
			usleep(rand_r(&rand_yield) % MAX_SLEEP);
}

static inline void debug_yield_init(void)
{
	rand_yield = time(NULL) ^ pthread_self();
}
#else
static inline void debug_yield_read(void)
{
}

static inline void debug_yield_write(void)
{
}

static inline void debug_yield_init(void)
{

}
#endif

static inline void reader_barrier()
{
	smp_mb();
}

#define RCU_GP_ONLINE		(1UL << 0)
#define RCU_GP_CTR		(1UL << 1)

/*
 * Global quiescent period counter with low-order bits unused.
 * Using a int rather than a char to eliminate false register dependencies
 * causing stalls on some architectures.
 */
extern unsigned long urcu_gp_ctr;

struct urcu_reader {
	/* Data used by both reader and synchronize_rcu() */
	unsigned long ctr;
	/* Data used for registry */
	struct list_head head __attribute__((aligned(CACHE_LINE_SIZE)));
	pthread_t tid;
};

extern struct urcu_reader __thread urcu_reader;

extern int gp_futex;

/*
 * Wake-up waiting synchronize_rcu(). Called from many concurrent threads.
 */
static inline void wake_up_gp(void)
{
	if (unlikely(uatomic_read(&gp_futex) == -1)) {
		uatomic_set(&gp_futex, 0);
		futex(&gp_futex, FUTEX_WAKE, 1,
		      NULL, NULL, 0);
	}
}

#if (BITS_PER_LONG < 64)
static inline int rcu_gp_ongoing(unsigned long *value)
{
	unsigned long reader_gp;

	if (value == NULL)
		return 0;
	reader_gp = LOAD_SHARED(*value);
	return reader_gp && ((reader_gp ^ urcu_gp_ctr) & RCU_GP_CTR);
}
#else /* !(BITS_PER_LONG < 64) */
static inline int rcu_gp_ongoing(unsigned long *value)
{
	unsigned long reader_gp;

	if (value == NULL)
		return 0;
	reader_gp = LOAD_SHARED(*value);
	return reader_gp && (reader_gp - urcu_gp_ctr > ULONG_MAX / 2);
}
#endif  /* !(BITS_PER_LONG < 64) */

static inline void _rcu_read_lock(void)
{
	rcu_assert(urcu_reader.ctr);
}

static inline void _rcu_read_unlock(void)
{
}

static inline void _rcu_quiescent_state(void)
{
	smp_mb();	
	_STORE_SHARED(urcu_reader.ctr, _LOAD_SHARED(urcu_gp_ctr));
	smp_mb();	/* write urcu_reader.ctr before read futex */
	wake_up_gp();
	smp_mb();
}

static inline void _rcu_thread_offline(void)
{
	smp_mb();
	STORE_SHARED(urcu_reader.ctr, 0);
	smp_mb();	/* write urcu_reader.ctr before read futex */
	wake_up_gp();
}

static inline void _rcu_thread_online(void)
{
	_STORE_SHARED(urcu_reader.ctr, LOAD_SHARED(urcu_gp_ctr));
	smp_mb();
}

#endif /* _URCU_QSBR_STATIC_H */
