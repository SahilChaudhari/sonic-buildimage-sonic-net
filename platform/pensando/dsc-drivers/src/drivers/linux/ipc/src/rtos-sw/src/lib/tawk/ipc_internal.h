// SPDX-License-Identifier: MIT or GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#pragma once

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <tawk/ipc.h>


#if defined(__KERNEL__) && defined(TAWK_DRV_DEBUG)
#include <tawk_drv_debug/debug.h>
#endif

#include "device/device.h"
#include "hardware/hardware.h"
#include "datalink/datalink.h"
#include "transport/transport.h"


struct ipc_context_s {
  ipc_device_ctx_t dev;

  struct k_mutex mtx;
  struct k_work_q workq;
  K_KERNEL_STACK_MEMBER(workq_stack, CONFIG_TAWK_IPC_STACK_SIZE);
  unsigned unlock_prohibit;

  unsigned peer_id;

  ipc_hardware_ctx_t hw;
  ipc_datalink_ctx_t dl;
  ipc_transport_ctx_t tp;

  bool inited;
};


extern int ipc_init(ipc_context_t *ipc,
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
                    size_t mbox_buffer_size);
extern void ipc_quarantine(ipc_context_t *ipc);
extern void ipc_fini(ipc_context_t *ipc);

extern void ipc_lock(ipc_context_t *ipc);
extern void ipc_unlock(ipc_context_t *ipc);
extern void ipc_assert_locked(ipc_context_t *ipc);

/* A common pattern in the IPC code for a work item to:
 *  - acquire the IPC lock
 *  - do some work
 *  - release the IPC lock
 *
 * This is problematic for code that wishes to ensure that the work
 * item won't run again, and that can't drop the IPC lock.  Simply
 * calling k_work_cancel to cancel the work item is insufficient.  The
 * work item could have been prempted before it attempted to acquire
 * the IPC lock, or could have blocked waiting in the lock. Instead we
 * need to call k_work_cancel_sync.  However, doing so without
 * dropping the IPC lock results in deadlock.
 *
 * The solution here is to add inteligence to the code that attemps to
 * acquire the IPC lock within the work item.  It simultaneously
 * attempts to acquire the lock and checks for cancellation.  If the
 * lock is acquired, it allows the work item to proceed as normal.
 * Otherrwise, if the work item has been cancelled, it abandons trying
 * to acquire the lock and returns from the work item early.  This allows
 * code to call k_work_cancel_sync and obtain the correct behaviour.
 */
extern bool ipc_lock_for_work(ipc_context_t *ipc, struct k_work *work);


/* There are restrictions on when this code is allowed to block.  We can't
 * check that directly, but we can instead check for dropping the lock as a
 * proxy for blocking (it's not possible to drop and reacquire the lock without
 * blocking, even thought the reverse is not true).
 *
 * Attempts to drop the lock at a point where blocking is not
 * permitted will cause UB.  These functions nest.
 */
extern void ipc_block_forbid(ipc_context_t *ipc);
extern void ipc_block_permit(ipc_context_t *ipc);


static inline unsigned ipc_local_peer_id(ipc_context_t *ipc)
{
  return ipc->peer_id;
}

static inline unsigned ipc_remote_peer_id(ipc_context_t *ipc)
{
  return 1 - ipc->peer_id;
}

#ifdef IPC_BUILD_KERNEL_MODULE
#include <linux/device.h>
#define TAWK_IPC_LOG_ERR(_ipc, _fmt, ...) dev_err(_ipc->dev.dev, _fmt, ##__VA_ARGS__)
#define TAWK_IPC_LOG_WRN(_ipc, _fmt, ...) dev_warn(_ipc->dev.dev, _fmt, ##__VA_ARGS__)
#define TAWK_IPC_LOG_INF(_ipc, _fmt, ...) dev_info(_ipc->dev.dev, _fmt, ##__VA_ARGS__)
#define TAWK_IPC_LOG_DBG(_ipc, _fmt, ...) dev_dbg(_ipc->dev.dev, _fmt, ##__VA_ARGS__)

void ipc_dump_hexdump(const char *level, const struct device *dev,
                      const void *buf, size_t len, const char *prefix_str);

#define TAWK_IPC_LOG_HEXDUMP_ERR(_ipc, _data, _length, _str) \
  ipc_dump_hexdump(KERN_ERR, _ipc->dev.dev, _data, _length, _str)

#define TAWK_IPC_LOG_HEXDUMP_WRN(_ipc, _data, _length, _str) \
  ipc_dump_hexdump(KERN_WARNING, _ipc->dev.dev, _data, _length, _str)

#define TAWK_IPC_LOG_HEXDUMP_INF(_ipc, _data, _length, _str) \
  ipc_dump_hexdump(KERN_INFO, _ipc->dev.dev, _data, _length, _str)

#define TAWK_IPC_LOG_HEXDUMP_DBG(_ipc, _data, _length, _str) \
  ipc_dump_hexdump(KERN_DEBUG, _ipc->dev.dev, _data, _length, _str)

#else
#define TAWK_IPC_LOG_ERR(_ipc, _fmt, ...) \
  do {                                    \
    ARG_UNUSED(_ipc);                     \
    LOG_ERR(_fmt, ##__VA_ARGS__);         \
  } while (0)

#define TAWK_IPC_LOG_WRN(_ipc, _fmt, ...) \
  do {                                    \
    ARG_UNUSED(_ipc);                     \
    LOG_WRN(_fmt, ##__VA_ARGS__);         \
  } while (0)

#define TAWK_IPC_LOG_INF(_ipc, _fmt, ...) \
  do {                                    \
    ARG_UNUSED(_ipc);                     \
    LOG_INF(_fmt, ##__VA_ARGS__);         \
  } while (0)

#define TAWK_IPC_LOG_DBG(_ipc, _fmt, ...) \
  do {                                    \
    ARG_UNUSED(_ipc);                     \
    LOG_DBG(_fmt, ##__VA_ARGS__);         \
  } while (0)


#define TAWK_IPC_LOG_HEXDUMP_ERR(_ipc, _data, _length, _str) \
  do {                                                       \
    ARG_UNUSED(_ipc);                                        \
    LOG_HEXDUMP_ERR(_data, _length, _str);                   \
  } while (0)

#define TAWK_IPC_LOG_HEXDUMP_WRN(_ipc, _data, _length, _str) \
  do {                                                       \
    ARG_UNUSED(_ipc);                                        \
    LOG_HEXDUMP_WRN(_data, _length, _str);                   \
  } while (0)

#define TAWK_IPC_LOG_HEXDUMP_INF(_ipc, _data, _length, _str) \
  do {                                                       \
    ARG_UNUSED(_ipc);                                        \
    LOG_HEXDUMP_INF(_data, _length, _str);                   \
  } while (0)

#define TAWK_IPC_LOG_HEXDUMP_DBG(_ipc, _data, _length, _str) \
  do {                                                       \
    ARG_UNUSED(_ipc);                                        \
    LOG_HEXDUMP_DBG(_data, _length, _str);                   \
  } while (0)

#endif

#if CONFIG_TAWK_IPC_VERBOSE_LOG == 0
#define TAWK_IPC_VERBOSE_LOG(...)
#define TAWK_IPC_VERBOSE_HEXDUMP(_ipc, _data, _length, _str)
#define TAWK_IPC_VERY_VERBOSE_LOG(...)
#define TAWK_IPC_VERY_VERBOSE_HEXDUMP(_ipc, _data, _length, _str)
#elif CONFIG_TAWK_IPC_VERBOSE_LOG == 1
#define TAWK_IPC_VERBOSE_LOG(...) TAWK_IPC_LOG_DBG(__VA_ARGS__)
#define TAWK_IPC_VERBOSE_HEXDUMP(_ipc, _data, _length, _str) TAWK_IPC_LOG_HEXDUMP_DBG(_ipc, _data, _length, _str)
#define TAWK_IPC_VERY_VERBOSE_LOG(...)
#define TAWK_IPC_VERY_VERBOSE_HEXDUMP(_ipc, _data, _length, _str)
#elif CONFIG_TAWK_IPC_VERBOSE_LOG == 2
#define TAWK_IPC_VERBOSE_LOG(...) TAWK_IPC_LOG_DBG(__VA_ARGS__)
#define TAWK_IPC_VERBOSE_HEXDUMP(_ipc, _data, _length, _str) TAWK_IPC_LOG_HEXDUMP_DBG(_ipc, _data, _length, _str)
#define TAWK_IPC_VERY_VERBOSE_LOG(...) TAWK_IPC_LOG_DBG(__VA_ARGS__)
#define TAWK_IPC_VERY_VERBOSE_HEXDUMP(_ipc, _data, _length, _str) TAWK_IPC_LOG_HEXDUMP_DBG(_ipc, _data, _length, _str)
#else
#error "Unexpected CONFIG_TAWK_IPC_VERBOSE_LOG value"
#endif

#define TAWK_IPC_DUMP(_ipc, ...) TAWK_IPC_LOG_ERR(_ipc, ##__VA_ARGS__)
#define TAWK_IPC_HEXDUMP(_ipc, _data, _length, _str) TAWK_IPC_LOG_HEXDUMP_ERR(_ipc, _data, _length, _str)

#define TAWK_IPC_ASSERT(ipc_, test_, fmt, ...)            \
  do {                                                    \
    if (test_) {                                          \
      ;                                                   \
    } else {                                              \
      if (IS_ENABLED(CONFIG_TAWK_IPC_DUMP_ON_PANIC)) {    \
        ipc_dump(ipc_);                                   \
      }                                                   \
      __ASSERT(test_, fmt, ##__VA_ARGS__);                \
    }                                                     \
  } while (0)

#define TAWK_IPC_ASSERT_NO_MSG(ipc_, test_)               \
  do {                                                    \
    if (test_) {                                          \
      ;                                                   \
    } else {                                              \
      if (IS_ENABLED(CONFIG_TAWK_IPC_DUMP_ON_PANIC)) {    \
        ipc_dump(ipc_);                                   \
      }                                                   \
      __ASSERT_NO_MSG(test_);                             \
    }                                                     \
  } while (0)

#ifdef CONFIG_ASSERT
#define IPC_USED_ONLY_IN_ASSERT(x_)
#else
#define IPC_USED_ONLY_IN_ASSERT(x_) \
  ARG_UNUSED(x_)
#endif
