// SPDX-License-Identifier: MIT or GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#include <linux/module.h>      /* Needed by all modules */
#include <linux/kernel.h>      /* Needed for KERN_ALERT */
#include <linux/init.h>        /* Needed for the macros */
#include <linux/delay.h>

#include <zephyr/kernel.h>

#define K_TIMER_TEST_50_MS K_MSEC(50)
#define K_TIMER_TEST_100_MS K_MSEC(100)

static int test_timer_count = 0;

static void on_test_timer(struct k_timer *t)
{
  printk("%s\n", __func__);
  ++test_timer_count;
}

static void on_test_stop(struct k_timer *t)
{
  printk("%s\n", __func__);
}

/* tawk_test_timer - A test subroutine to smoke-test Zephyr timers on Linux.
 *
 * The test body:
 * 1. Start the timer.
 * 2. Sleep and allow the timer to fire after the initial D1 and the longer D2
 *    intervals.
 * 3. Time how long it takes to stop the timer.
 * 4. Check that the timer-stop function returns "almost immediately".
 * 5. Check that the timer has fired exactly twice.
 *
 * The chosen D1 and D2 intervals are long enough to ignore possible delayed
 * scheduling.
 */
static void tawk_test_timer(struct k_timer *t)
{
  struct timespec64 start_ts, end_ts;
  s64 delta_ts;

  printk("%s\n", __func__);

  k_timer_init(t, on_test_timer, on_test_stop);

  k_timer_start(t, K_TIMER_TEST_50_MS, K_TIMER_TEST_100_MS);

  msleep(200);

  ktime_get_ts64(&start_ts);
  k_timer_stop(t);
  ktime_get_ts64(&end_ts);

  delta_ts = timespec64_to_ns(&end_ts) - timespec64_to_ns(&start_ts);
  if (delta_ts <= (50 * 1000 * 1000)) {
    printk("%s: Check k_timer_stop(): SUCCESS\n", __func__);
  } else {
    printk("%s: Check k_timer_stop(): FAIL (delta_ts=%lldns)\n", __func__,
           delta_ts);
  }

  if (test_timer_count == 2) {
    printk("%s: Check test_timer_count: SUCCESS\n", __func__);
  } else {
    printk("%s: Check test_timer_count: FAIL (test_timer_count=%d)\n",
           __func__, test_timer_count);
  }
}

static int __init tawk_init(void)
{
  struct k_timer t;

  printk("%s\n", __func__);

  tawk_test_timer(&t);

  return 0;
}


static void __exit tawk_exit(void)
{
  printk("%s\n", __func__);
}


module_init(tawk_init);
module_exit(tawk_exit);

MODULE_LICENSE("GPL");
