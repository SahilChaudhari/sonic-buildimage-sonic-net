// SPDX-License-Identifier: MIT or GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#if defined(IPC_BUILD_KERNEL_MODULE)
#include <linux/slab.h>
#endif

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <tawk/ipc.h>
#include <tawk/platform.h>

#include "ipc_internal.h"

#ifdef RTOS
#include "log_helper/log_helper.h"

LOG_HELPER_REGISTER(tawk, CONFIG_TAWK_IPC_LOG_LEVEL);
#else
LOG_MODULE_REGISTER(tawk, CONFIG_TAWK_IPC_LOG_LEVEL);
#endif

struct k_work_queue_config tawk_work_q_cfg = { .name = "tawk_workq" };

int ipc_init(ipc_context_t *ipc,
             int peer_id,
             bool own_mem,
             mm_reg_t rst_sigs_out_addr,
             mm_reg_t rst_sigs_in_addr,
             unsigned mbox_count,
             mm_reg_t mbox_control_base,
             unsigned mbox_control_stride_log2,
#ifdef CONFIG_TAWK_IPC_CACHE_MANAGEMENT
             bool mbox_buffer_cached,
#endif
             mm_reg_t mbox_buffer_base,
             unsigned mbox_buffer_stride_log2,
             size_t mbox_buffer_size)
{
  int rc;

  k_mutex_init(&ipc->mtx);
  k_work_queue_init(&ipc->workq);
  k_work_queue_start(&ipc->workq, ipc->workq_stack, K_KERNEL_STACK_SIZEOF(ipc->workq_stack),
                     CONFIG_TAWK_IPC_WORKQ_THREAD_PRIORITY, &tawk_work_q_cfg);

  ipc->unlock_prohibit = 0;
  ipc_lock(ipc);

  ipc->peer_id = peer_id;

  rc = ipc_hw_init(&ipc->hw,
                   own_mem,
                   rst_sigs_out_addr,
                   rst_sigs_in_addr,
                   mbox_count,
                   mbox_control_base,
                   mbox_control_stride_log2,
#ifdef CONFIG_TAWK_IPC_CACHE_MANAGEMENT
                   mbox_buffer_cached,
#endif
                   mbox_buffer_base,
                   mbox_buffer_stride_log2,
                   mbox_buffer_size);
  if (rc != 0)
    goto fail0;

  rc = ipc_dl_init(&ipc->dl);
  if (rc != 0)
    goto fail1;

  rc = ipc_tp_init(&ipc->tp);
  if (rc != 0)
    goto fail2;

#if defined(__KERNEL__) && defined(TAWK_DRV_DEBUG)
  rc = ipc_debug_init(ipc);
  if (rc != 0) {
    ipc_tp_fini(&ipc->tp);
    goto fail2;
  }
#endif

  ipc->inited = true;

  ipc_assert_locked(ipc);
  return 0;

 fail2:
  ipc_dl_fini(&ipc->dl);

 fail1:
  ipc_hw_fini(&ipc->hw);

 fail0:
#ifdef __KERNEL__
  /* Avoid leaking memory in kernel driver */
  k_work_queue_fini(&ipc->workq);
#endif

  return rc;
}

static void ipc_deinit(ipc_context_t *ipc, bool quarantined)
{
  ipc_assert_locked(ipc);

  if (!ipc->inited)
    return;

#if defined(__KERNEL__) && defined(TAWK_DRV_DEBUG)
  ipc_debug_fini(ipc);
#endif

  ipc_tp_fini(&ipc->tp);
  ipc_dl_fini(&ipc->dl);
  if (quarantined)
    ipc_hw_quarantine(&ipc->hw);
  else
    ipc_hw_fini(&ipc->hw);

#ifdef __KERNEL__
  /* Avoid leaking memory in kernel driver */
  k_work_queue_fini(&ipc->workq);
#endif

  ipc->inited = false;

  ipc_unlock(ipc);
}

void ipc_quarantine(ipc_context_t *ipc)
{
  ipc_deinit(ipc, /* quarantined = */ true);
}

void ipc_fini(ipc_context_t *ipc)
{
  ipc_deinit(ipc, /* quarantined = */ false);
}

static bool ipc_lock_timeout(ipc_context_t *ipc, k_timeout_t timeout)
{
  int rc = k_mutex_lock(&ipc->mtx, timeout);
  if (rc == 0) {
    __ASSERT_NO_MSG(ipc->unlock_prohibit == 0);
    return true;
  } else
    return false;
}

void ipc_lock(ipc_context_t *ipc)
{
  bool got_lock = ipc_lock_timeout(ipc, K_FOREVER);
  IPC_USED_ONLY_IN_ASSERT(got_lock);
  __ASSERT_NO_MSG(got_lock);
}

void ipc_unlock(ipc_context_t *ipc)
{
  __ASSERT(ipc->unlock_prohibit == 0,
           "attempting to drop lock whilst blocking prohibited");
  k_mutex_unlock(&ipc->mtx);
}

void ipc_assert_locked(ipc_context_t *ipc)
{
  __ASSERT(ipc->mtx.owner == _current,
           "mutex not held: owner=%p current=%p",
           ipc->mtx.owner, _current); /* UPSTREAM? */
}

bool ipc_lock_for_work(ipc_context_t *ipc, struct k_work *work)
{
  while (1) {
    int flags;
    bool got_lock;

    flags = k_work_busy_get(work);
    __ASSERT_NO_MSG(flags & K_WORK_RUNNING);

    if (flags & K_WORK_CANCELING)
      return false;

    got_lock = ipc_lock_timeout(ipc, K_MSEC(10));
    if (got_lock)
      return true;
  }
}

void ipc_condvar_wait(ipc_context_t *ipc, struct k_condvar *cv)
{
  int rc;

  ipc_assert_locked(ipc);
  __ASSERT(ipc->unlock_prohibit == 0,
           "attempting to drop lock whilst blocking prohibited");
  rc = k_condvar_wait(cv, &ipc->mtx, K_FOREVER);
  IPC_USED_ONLY_IN_ASSERT(rc);
  __ASSERT_NO_MSG(rc == 0);
}

void ipc_block_forbid(ipc_context_t *ipc)
{
  ipc->unlock_prohibit++;
  __ASSERT(ipc->unlock_prohibit > 0, "didn't overflow");
}

void ipc_block_permit(ipc_context_t *ipc)
{
  __ASSERT(ipc->unlock_prohibit > 0, "won't underflow");
  ipc->unlock_prohibit--;
}

void ipc_dump(ipc_context_t *ipc)
{
  TAWK_IPC_DUMP(ipc, "****** TAWK IPC library state dump ******");
  ipc_tp_dump(&ipc->tp);
  ipc_dl_dump(&ipc->dl);
  ipc_hw_dump(&ipc->hw);
}

#ifdef IPC_BUILD_KERNEL_MODULE

#define IPC_DUMP_HEXDUMP_LINE_WIDTH 128

void ipc_dump_hexdump(const char *level, const struct device *dev,
                      const void *buf, size_t len, const char *prefix_str)
{
  char line[IPC_DUMP_HEXDUMP_LINE_WIDTH];
  char *cur = line;
  char *end = line + sizeof(line);

  for (int b = 0; b < len; b++) {
    cur += snprintf(cur, end - cur, "%s%02x", line < cur ? " " : "",
                    ((uint8_t *)buf)[b]);
    if ((b + 1) % 16 == 0) {
      dev_printk(level, dev, "%s%s", prefix_str, line);
      cur = line;
    }
  }

  if (cur > line)
    dev_printk(level, dev, "%s%s", prefix_str, line);
}
#endif
