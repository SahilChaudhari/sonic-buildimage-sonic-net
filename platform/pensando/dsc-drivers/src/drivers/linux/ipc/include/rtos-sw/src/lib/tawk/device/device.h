// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#pragma once

#include <zephyr/kernel.h>

#include <tawk/ipc.h>
#include <tawk/device.h>
#include <tawk/platform.h>

#include "../hardware/hardware.h"

#ifndef CONFIG_BOARD_SIM
#include "mm.h"
#include "pcie.h"
#else
#include "sim.h"
#endif


#if IPC_WITH_POLLING
#define IPC_DEVICE_RST_POLL_INTERVAL K_USEC(200)
#define IPC_DEVICE_MBOX_POLL_INTERVAL K_USEC(200)
#endif


typedef struct ipc_device_notify_peer_spec_s {
#if defined(RTOS)
#if defined(CONFIG_TAWK_IPC_WITH_INTR)
  /* We use atomics to avoid having to take the IPC
   * lock when called from the PCIe manager thread */
  atomic_t intr;
#else
  int dummy;
#endif
#elif defined(IPC_BUILD_KERNEL_MODULE)
  mm_reg_t addr;
  uint32_t val;
#else
#error "Who am I"
#endif
} ipc_device_notify_peer_spec_t;

#if defined(RTOS)

#if defined(CONFIG_TAWK_IPC_WITH_INTR)

/* Zephyr doesn't say whether atomic_t is guaranteed to be signed */
#define IPC_DEVICE_INTR_NONE ((atomic_val_t)(-1))

#define IPC_DEVICE_NOTIFY_PEER_SPEC_IS_NONE(spec_)      \
  (atomic_get(&(spec_).intr) != IPC_DEVICE_INTR_NONE)

#define IPC_DEVICE_NOTIFY_PEER_SPEC_NONE                \
  ((ipc_device_notify_peer_spec_t)                      \
   { .intr = ATOMIC_INIT(IPC_DEVICE_INTR_NONE) })

#define IPC_DEVICE_NOTIFY_PEER_SPEC_INTR(intr_)         \
  ((ipc_device_notify_peer_spec_t)                      \
  { .intr = ATOMIC_INIT(intr_) })

#else

#define IPC_DEVICE_NOTIFY_PEER_SPEC_IS_NONE(spec_)      \
  true

#define IPC_DEVICE_NOTIFY_PEER_SPEC_NONE                \
  ((ipc_device_notify_peer_spec_t)                      \
   { .dummy = 0xb050 })

#endif

#elif defined(IPC_BUILD_KERNEL_MODULE)

#define IPC_DEVICE_NOTIFY_PEER_SPEC_IS_NONE(spec_)      \
  ((spec_).addr == 0)

#define IPC_DEVICE_NOTIFY_PEER_SPEC_NONE        \
  ((ipc_device_notify_peer_spec_t)              \
   { .addr = 0 })

#define IPC_DEVICE_NOTIFY_PEER_SPEC_DB(addr_, val_)     \
  ((ipc_device_notify_peer_spec_t)                      \
   { .addr = (addr_), .val = (val_) })

#endif

#if defined(RTOS)
extern int ipc_device_db_via_intr(unsigned intr, unsigned *irq, uint64_t *dbaddr, uint32_t *dbdata);
#endif


typedef struct ipc_device_ctx_s {
  ipc_device_notify_peer_spec_t rst_notify_peer, mbox_notify_peer;
#if IPC_WITH_POLLING
  struct k_timer rstsigs_timer;
  struct k_timer mbox_timer;
#endif
#if defined(RTOS)
  unsigned rstsigs_db_irq;
  unsigned mbox_db_irq;
#endif
#if defined(IPC_BUILD_KERNEL_MODULE)
  ipc_device_fini_interrupts_fn *fini_interrupts_fn;
  void *fini_interrupts_ctx;

  struct device *dev;
#endif

  union {
#ifndef CONFIG_BOARD_SIM
    ipc_device_mm_ctx_t mm;
    ipc_device_pcie_ctx_t pcie;
#else
    ipc_device_sim_ctx_t sim;
#endif
  } u;
} ipc_device_ctx_t;


extern ipc_context_t *ipc_dev_ipc_context(ipc_device_ctx_t *dev);


extern int ipc_device_init(ipc_context_t *ipc,
                           int peer_id,
                           bool own_mem,
                           mm_reg_t rst_sigs_out_addr,
                           mm_reg_t rst_sigs_in_addr,
#if IPC_WITH_POLLING
                           k_timeout_t rst_poll_interval,
#endif
#if defined(RTOS)
                           unsigned rst_db_irq,
#endif
                           ipc_device_notify_peer_spec_t rst_notify_peer,
                           unsigned mbox_count,
                           mm_reg_t mbox_control_base,
                           unsigned mbox_control_stride_log2,
#ifdef CONFIG_TAWK_IPC_CACHE_MANAGEMENT
                           bool mbox_buffer_cached,
#endif
                           mm_reg_t mbox_buffer_base,
                           unsigned mbox_buffer_stride_log2,
                           size_t mbox_buffer_size,
#if IPC_WITH_POLLING
                           k_timeout_t mbox_poll_interval,
#endif
#if defined(RTOS)
                           unsigned mbox_db_irq,
#endif
#if defined(IPC_BUILD_KERNEL_MODULE)
                           /* The destructor ipc_device_fini() will call fn
                            * unconditionally when it is non-NULL even if
                            * *_db_irq have the *_NONE values.
                            */
                           ipc_device_fini_interrupts_fn *fini_interrupts_fn,
                           void *fini_interrupts_ctx,
                           struct device *dev,
#endif
                           ipc_device_notify_peer_spec_t mbox_notify_peer);

extern void ipc_device_quarantine(ipc_context_t *ipc);
extern void ipc_device_fini(ipc_context_t *ipc);
