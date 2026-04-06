// SPDX-License-Identifier: MIT or GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#pragma once

#ifdef __KERNEL__
#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/smp.h>
#include <linux/workqueue.h>
#include <linux/wait.h>
#include <libc_compat.h>
#else
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#endif

#include "sys/dlist.h"


#define BUILD_ASSERT(...) _Static_assert(__VA_ARGS__)

#ifndef __KERNEL__
#define panic(x) assert(0)
#endif

#define __ASSERT(e, ...)                        \
  do {                                          \
    if (e)                                      \
      ;                                         \
    else {                                      \
      printk(__VA_ARGS__);                      \
      panic("OOPS");                            \
    }                                           \
  } while (0)

#define __ASSERT_NO_MSG(e)                      \
  __ASSERT(e, #e)


#define ARG_UNUSED(x) (void)(x)


#define CONTAINER_OF(ptr, type, member) ({                      \
      const typeof( ((type *)0)->member ) *__mptr = (ptr);      \
      (type *)((char *)__mptr - offsetof(type,member));})

#ifndef __KERNEL__
#define ARRAY_SIZE(a) ((sizeof (a)) / (sizeof (a)[0]))
#endif

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))


#ifndef __KERNEL__
#define BITS_PER_LONG_LONG (64)
#define GENMASK(h, l)                                                   \
  (((~0UL) - (1UL << (l)) + 1) & (~0UL >> (BITS_PER_LONG - 1 - (h))))
#endif
#define GENMASK64(h, l) \
  (((~0ULL) - (1ULL << (l)) + 1) & (~0ULL >> (BITS_PER_LONG_LONG - 1 - (h))))
#define LSB_GET(value) ((value) & -(value))
#ifdef FIELD_PREP
#undef FIELD_PREP
#endif
#ifdef FIELD_GET
#undef FIELD_GET
#endif
#define FIELD_GET(mask, value)  (((value) & (mask)) / LSB_GET(mask))
#define FIELD_PREP(mask, value) (((value) * LSB_GET(mask)) & (mask))


#ifndef __KERNEL__
#define printk printf
#endif


typedef uintptr_t mm_reg_t;
typedef uintptr_t mem_addr_t;

#if defined(__KERNEL__) && defined(TAWK_DRV_DEBUG)
#include <tawk_drv_debug/debug.h>

static inline uint32_t sys_read32(mm_reg_t a)
{
  return ipc_debug_sys_read32(a);
}

static inline void sys_write32(uint32_t v, mm_reg_t a)
{
  ipc_debug_sys_write32(v, a);
}

static inline uint64_t sys_read64(mm_reg_t a)
{
  return ipc_debug_sys_read64(a);
}

static inline void sys_write64(uint64_t v, mm_reg_t a)
{
  ipc_debug_sys_write64(v, a);
}
#else
static inline uint32_t sys_read32(mm_reg_t a)
{
  return *(volatile uint32_t *)(a);
}

static inline void sys_write32(uint32_t v, mm_reg_t a)
{
  *(volatile uint32_t *)(a) = v;
}

static inline uint64_t sys_read64(mm_reg_t a)
{
  return *(volatile uint64_t *)(a);
}

static inline void sys_write64(uint64_t v, mm_reg_t a)
{
  *(volatile uint64_t *)(a) = v;
}
#endif

struct k_work_q {
#ifdef __KERNEL__
  struct workqueue_struct *workq;
#else
  sys_dlist_t q;
  bool stop;
  pthread_mutex_t m;
  pthread_cond_t c;
  pthread_t t;
#endif
};

/** @brief A structure holding optional configuration items for a work
 * queue.
 *
 * This structure, and values it references, are not retained by
 * k_work_queue_start().
 */
struct k_work_queue_config {
        /** The name to be given to the work queue thread.
         *
         * If left null the thread will not have a name.
         */
        const char *name;

        /** Control whether the work queue thread should yield between
         * items.
         *
         * Yielding between items helps guarantee the work queue
         * thread does not starve other threads, including cooperative
         * ones released by a work item.  This is the default behavior.
         *
         * Set this to @c true to prevent the work queue thread from
         * yielding between items.  This may be appropriate when a
         * sequence of items should complete without yielding
         * control.
         */
        bool no_yield;
};
struct k_work {
  void (*fn)(struct k_work *w);
#ifdef __KERNEL__
  struct work_struct w;
  spinlock_t s;
  struct completion canceled;
  int flags;
#else
  sys_dnode_t node;
  struct k_work_q *q;
  pthread_cond_t canceled;
  int flags;
#endif
};

struct k_work_sync {
  int dummy;
};

typedef struct k_thread_stack {
  char data;
} k_thread_stack_t;

#define K_KERNEL_STACK_MEMBER(sym, size) k_thread_stack_t (sym)[1]
#define K_KERNEL_STACK_SIZEOF(sym) sizeof(sym)

extern void k_work_queue_init(struct k_work_q *q);
#ifdef __KERNEL__
extern void k_work_queue_fini(struct k_work_q *q);
#endif
extern void k_work_init(struct k_work *w, void (*fn)(struct k_work *));
void k_work_queue_start(struct k_work_q *queue, k_thread_stack_t *stack, size_t stack_size, int prio,
                        const struct k_work_queue_config *cfg);

/* k_work_submit[_to_queue] - Submit a work item to a queue.
 * Return values:
 * 0 if work was already submitted to a queue
 * 1 if work was not submitted and has been queued to queue
 * 2 if work was running and has been queued to the queue that was running it
 */
extern int k_work_submit_to_queue(struct k_work_q *q, struct k_work *w);
extern int k_work_submit(struct k_work *w);
extern int k_work_busy_get(struct k_work *w);
#define K_WORK_QUEUED 1
#define K_WORK_RUNNING 2
#define K_WORK_CANCELING 4
extern void k_work_cancel(struct k_work *w);
extern void k_work_cancel_sync(struct k_work *w, struct k_work_sync *ws);

#define K_MSEC(m) ((m) * 1000000ULL)
#define K_USEC(u) ((u) * 1000ULL)
typedef uint64_t k_timeout_t;
typedef uint64_t k_timepoint_t;
#define K_NO_WAIT 0
#define K_FOREVER 0xffffffffffffffff
#define K_TIMEOUT_EQ(a_, b_) ((a_) == (b_))
extern k_timepoint_t sys_now(void);
extern k_timepoint_t sys_timepoint_calc(k_timeout_t  timeout);
extern bool sys_timepoint_expired(k_timepoint_t timepoint);
extern int k_sleep(k_timeout_t timeout);

struct k_timer {
  void (*f_timer)(struct k_timer *t);
  void (*f_stop)(struct k_timer *t);
  k_timeout_t d1;
  k_timeout_t d2;
  bool running;
  volatile bool stop;
#ifdef __KERNEL__
  struct task_struct *kth;
  struct completion stopped;
  wait_queue_head_t wq;
#else
  pthread_t t;
#endif
};

extern void k_timer_init(struct k_timer *t,
                         void (*f_timer)(struct k_timer *),
                         void (*f_stop)(struct k_timer *));
extern void k_timer_start(struct k_timer *t, k_timeout_t d1, k_timeout_t d2);
extern void k_timer_stop(struct k_timer *t);
extern void k_timer_status_sync(struct k_timer *t);


struct k_thread;

struct k_mutex {
#ifdef __KERNEL__
  struct mutex m;
#else
  pthread_mutex_t m;
#endif
  struct k_thread *owner;
};

#ifdef __KERNEL__
#define _current (struct k_thread *)current
#else
#define _current ((struct k_thread *)(uintptr_t)pthread_self())
#endif

extern void k_mutex_init(struct k_mutex *m);
extern int k_mutex_lock(struct k_mutex *m, k_timeout_t t);
extern void k_mutex_unlock(struct k_mutex *m);


struct k_condvar {
#ifdef __KERNEL__
  struct completion c;
#else
  pthread_cond_t c;
#endif
};

extern void k_condvar_init(struct k_condvar *c);
extern int k_condvar_wait(struct k_condvar *c, struct k_mutex *m, k_timeout_t t);
extern void k_condvar_signal(struct k_condvar *c);
