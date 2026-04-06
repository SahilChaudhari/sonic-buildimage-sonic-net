// SPDX-License-Identifier: MIT or GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#pragma once

#if defined(RTOS)
#include <limits.h>
#endif

#include "ipc.h"
#include "platform.h"

#define IPC_WITH_POLLING 1 /* temporary flag to make code that'll go when we remove support
                            * for polling within the core IPC stack (it'll become the responsbility
                            * of the environment isntantiating the stack to poll if interrupts/doorbells
                            * aren't available) */

#define IPC_PS_11703_WORKAROUND 1 /* PS-11703: device_map is broken and we have to use
                                   * physical addresses. Make sure that they're mapped
                                   * with appropriate properties. */

/*
 * For ipc_device_init_*, the IPC lock:
 * - doesn't exist when called
 * - is held on return
 *
 * For ipc_device_fini, the IPC lock:
 *  - must be held when called
 *  - doesn't exist on return
 */
#if defined(RTOS)

#define IPC_DEVICE_DB_IRQ_NONE UINT_MAX

/* The BAR should have a region that's mapped to two local memory
 * ranges, one handled for reads and one for writes.  The physical
 * address to which these regions should map can be queried with
 * ipc_device_pcie_config. */
#define IPC_PCIE_BAR_HDR_MEM_OFFS 0x0000
#define IPC_PCIE_BAR_HDR_MEM_SIZE 0x0080

/* The BAR should expose a number of doorbell registers that map to a
 * contiguous range of intr resources.  The number of doorbells, the
 * starting intr number and their physical addresses can be queried
 * with ipc_device_pcie_config. */
#define IPC_PCIE_BAR_DB_OFFS 0x0100
#define IPC_PCIE_BAR_DB_SIZE 0x4
#define IPC_PCIE_BAR_DB_STRIDE 0x4
#define IPC_PCIE_BAR_DB_IRQ_COUNT (2)

/* The BAR should expose another region that's mapped to local memory.
 * The physical address to which this region should map can be queried
 * with ipc_device_pcie_config. */
#define IPC_PCIE_BAR_MEM_OFFS 0x4000
#define IPC_PCIE_BAR_MEM_SIZE 0x4000

#define IPC_PCIE_BAR_SIZE 0x8000

typedef struct ipc_device_pcie_config_s {
  unsigned intrb;
  unsigned intrc;
  uintptr_t mem_pa;
  size_t mem_sz; /* At least IPC_PCIE_BAR_MEM_SIZE */
  uintptr_t bar_hdr_rd_pa;
  size_t bar_hdr_rd_sz; /* At least IPC_PCIE_BAR_HDR_MEM_SIZE */
  uintptr_t bar_hdr_wr_pa;
  size_t bar_hdr_wr_sz; /* At least IPC_PCIE_BAR_HDR_MEM_SIZE */
  unsigned db_intrb;
  unsigned db_intrc; /* In then range [0,IPC_PCIE_BAR_DB_IRQ_COUNT] */
} ipc_device_pcie_config_t;

extern const ipc_device_pcie_config_t *ipc_device_pcie_config(ipc_context_t *ipc);

/* A memory mapped device should have a region that's mapped read/write
 * to both peers.  The physical address of this region can be queried
 * with ipc_device_mm_config */
#define IPC_MM_MEM_SIZE 0x4000
#define IPC_MM_DB_IRQ_COUNT (2)

typedef struct ipc_device_mm_config_s {
  unsigned remote_peer_id;

  uintptr_t db_pa; /* UINTPTR_MAX if no doorbells */
  size_t db_sz;
  size_t db_rst_offs; /* SIZE_MAX if no such doorbell */
  uint32_t db_rst_val;
  size_t db_mbox_offs; /* SIZE_MAX if no such doorbell */
  uint32_t db_mbox_val;

  uintptr_t msix_pa; /* UINTPTR_MAX if no interrupts */
  size_t msix_sz;
  size_t msix_rst_offs; /* SIZE_MAX if no such interrupt */
  size_t msix_mbox_offs;/* SIZE_MAX if no such interrupt */

  uintptr_t rstsigs_pa;
  size_t rstsigs_sz;
  size_t rstsigs_out_offs;
  size_t rstsigs_in_offs;

  size_t mbox_count;

  uintptr_t mbox_control_pa;
  size_t mbox_control_offs;
  size_t mbox_control_sz;
  unsigned mbox_control_stride_log2;

  uintptr_t mbox_buffer_pa;
  size_t mbox_buffer_sz; /* size of the region */
  size_t mbox_buffer_offs;
  unsigned mbox_buffer_stride_log2;
  size_t mbox_buffer_size; /* size of one buffer */
} ipc_device_mm_config_t;


#ifdef CONFIG_BOARD_SIM
/* A memory mapped  file that can be read/write from both peers.
 * Using mm configuration as both are similar in architecture
 * shared-memory vs memory mapped file */
typedef ipc_device_mm_config_t ipc_device_sim_config_t;
#endif

#elif defined(IPC_BUILD_KERNEL_MODULE)

struct device;

/* Platform-specific interrupt initialisation and deinitialisation.
 * On error, the return code must be positive.
 */
typedef int (ipc_device_init_interrupts_fn)(void *fn_ctx);
typedef void (ipc_device_fini_interrupts_fn)(void *fn_ctx);

/* The interaction of poll and interrupts is subtle.
 *
 * On entry, *interrupts should be true if the caller wishes to use
 * device interrupts.  rst_int_vec and mbox_int_vec should be set
 * appropriately.  Additionally, the caller must provide function
 * pointers to configure interrupts in a platform-specific way to call
 * ipc_device_rst_notify_interrupt and ipc_device_mbox_notify_interrupt
 * if the device supports interrupts.  A non-zero (positive) return code
 * implies a fatal error.
 *
 * Note that a function that initialises interrupts must ensure they
 * are *masked* so that any interim interrupt requests will get queued
 * as pending but won't result in calling ISR until the caller explicitly
 * enables them.  This will ensure that ISR operates in a fully initialised
 * runtime.
 *
 * On exit, *interrupts will be true if interrupts were configured
 * successfully.  It will only be true on exit it it was true on
 * entry.
 *
 * On exit, if *interrupts is false then it is necessary to poll. */
#if IPC_WITH_POLLING
/* If poll was true on entry then then IPC stack will automatically
 * poll.  If poll was false on entry then... */
#endif
 /* The caller is responsible for setting up a polling loop that calls
 * ipc_device_rst_notify_poll and ipc_device_mbox_notify_poll.
 */
extern int ipc_device_init_pcie_host(ipc_context_t *ipc,
                                     mm_reg_t bar_base_addr /* must be mapped uncached */,
#if IPC_WITH_POLLING
                                     bool poll,
#endif
                                     bool *interrupts,
                                     int rst_int_vec,
                                     int mbox_int_vec,
                                     ipc_device_init_interrupts_fn init_interrupts_fn,
                                     ipc_device_fini_interrupts_fn fini_interrupts_fn,
                                     void *fn_ctx,
                                     struct device *dev);

/* Here, the semantics of poll are a little simpler.
 */
#if IPC_WITH_POLLING
/* If poll is true on then IPC stack will automatically poll.  If poll
 * was false on entry then... */
#endif
 /* The caller is responsible for either,
  * a) setting up a polling loop that calls ipc_device_rst_notify_poll and
  *    ipc_device_mbox_notify_poll, or,
  * b) install interrupt handlers (based upon a dts) that calls
  *    ipc_device_rst_notify_interrupt and
  *    ipc_device_mbox_notify_interrupt.
  */
extern int ipc_device_init_mm_host(ipc_context_t *ipc,
                                   int peer_id,
#if IPC_WITH_POLLING
                                   bool poll,
#endif
                                   mm_reg_t rst_sigs_out_addr,
                                   mm_reg_t rst_sigs_in_addr,
                                   bool rst_db_exists,
                                   mm_reg_t rst_db_addr,
                                   uint32_t rst_db_val,
                                   unsigned mbox_count,
                                   mm_reg_t mbox_control_base,
                                   unsigned mbox_control_stride_log2,
                                   mm_reg_t mbox_buffer_base,
                                   unsigned mbox_buffer_stride_log2,
                                   size_t mbox_buffer_size,
                                   bool mbox_db_exists,
                                   mm_reg_t mbox_db_addr,
                                   uint32_t mbox_db_val,
                                   struct device *dev);
#else

#error "Who am I?"

#endif

extern void ipc_device_quarantine(ipc_context_t *ipc);
extern void ipc_device_fini(ipc_context_t *ipc);

#if defined(RTOS)
/* Allocation is done statically through device tree */
#else
#define ipc_alloc_init(ctor_, ...)                                      \
  ({ipc_context_t *ipc__ = ipc_alloc();                                 \
    if (ipc__ != NULL) {                                                \
      int rc__ = ipc_device_init_ ## ctor_ (ipc__, ## __VA_ARGS__);     \
      if (rc__ != 0) {                                                  \
        ipc_free(ipc__);                                                \
        /* Convert TAWK +ve return code into -ve Linux kernel code */   \
        ipc__ = ERR_PTR(-rc__);                                         \
      }                                                                 \
    }                                                                   \
    ipc__;})

#define ipc_fini_free(ipc_)                     \
  do {                                          \
    ipc_device_fini(ipc_);                      \
    ipc_free(ipc_);                             \
} while (0)
#endif

#if defined(IPC_BUILD_KERNEL_MODULE)
extern void ipc_device_rst_notify_poll(ipc_context_t *ipc);
extern void ipc_device_mbox_notify_poll(ipc_context_t *ipc);
extern void ipc_device_rst_notify_interrupt(ipc_context_t *ipc);
extern void ipc_device_mbox_notify_interrupt(ipc_context_t *ipc);
#endif
