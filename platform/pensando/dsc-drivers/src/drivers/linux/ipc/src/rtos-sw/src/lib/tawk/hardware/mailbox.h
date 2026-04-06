// SPDX-License-Identifier: MIT or GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#pragma once

#ifdef __KERNEL__
#include <libc_compat.h>
#else
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#endif

#include <zephyr/kernel.h>

#ifdef TAWK_IPC_PROFILE
#include <tawk_drv_debug/profile.h>
#endif

#include "common.h"
#include "../protocol.h"


struct ipc_hardware_mailbox_ctx_s {
  struct k_work work;
  struct k_work_sync work_sync;

  /* True if the local peer is owns the the memory backing the control
   * registers and buffers and is this responsible for initialisation
   * on flush/scrub. */
  bool own_mem;

  unsigned mailbox_count;

  mm_reg_t control_base;
  unsigned control_stride_log2;

#ifdef CONFIG_TAWK_IPC_CACHE_MANAGEMENT
  bool buffer_cached;
#endif
  mm_reg_t buffer_base;
  unsigned buffer_stride_log2;
  size_t buffer_size;

#ifdef TAWK_IPC_PROFILE
  /* Profile how long the MB work item is "active" and "idle". */
  ipc_prof_t active_prof;
  ipc_prof_t idle_prof;
  ipc_prof_t lock_prof;
#endif

  uint8_t quarantine[TAWK_IPC_QUARANTINE_MBOX_SZ];
};


extern ipc_hardware_ctx_t *ipc_hw_mbox_hardware(ipc_hardware_mailbox_ctx_t *mb);

extern int ipc_hw_mbox_init(ipc_hardware_mailbox_ctx_t *mb,
                            bool own_mem,
                            unsigned mbox_count,
                            mm_reg_t control_base,
                            unsigned control_stride_log2,
#ifdef CONFIG_TAWK_IPC_CACHE_MANAGEMENT
                            bool buffer_cached,
#endif
                            mm_reg_t buffer_base,
                            unsigned buffer_stride_log2,
                            size_t buffer_size);

extern void ipc_hw_mbox_quarantine(ipc_hardware_mailbox_ctx_t *mb);

extern void ipc_hw_mbox_fini(ipc_hardware_mailbox_ctx_t *mb);

extern void ipc_hw_mbox_notify(ipc_hardware_mailbox_ctx_t *mb);

extern void ipc_hw_mbox_alert(ipc_hardware_mailbox_ctx_t *mb); /* CALLBACK */

extern void ipc_hw_mbox_notify_peer(ipc_hardware_mailbox_ctx_t *mb); /* CALLBACK */

/* Guaranteed to be a power of 2 */
extern unsigned ipc_hw_mbox_count(ipc_hardware_mailbox_ctx_t *mb);

extern size_t ipc_hw_mbox_buffer_size(ipc_hardware_mailbox_ctx_t *mb);

/* \param owned:
 *  - True if mailbox us owned by us in quiescent state.  Next valid
 *    actions are put or scrub.
 *  - False if mailbox not owned by us in quiescent state.  Next valid actions are get and
 *    scrub. */
extern void ipc_hw_mbox_scrub(ipc_hardware_mailbox_ctx_t *mb, unsigned mbox_idx, bool own_mbox);

/* Only valid on mailboxes not yet known to be owned by us.  Returns:
 *  - True if mailbox has been transfered to us.  Next valid actions
 *    are put and scrub.
 *  - False if mailbox has not been transfered to us.  Next valid actions
 *    are get and init. */
extern bool ipc_hw_mbox_get(ipc_hardware_mailbox_ctx_t *mb, unsigned mbox_idx, ipc_hdr_t *hdr, const volatile uint8_t **payload);

extern void ipc_hw_mbox_get_payload(uint8_t *dst, const volatile uint8_t *payload, size_t pld_len);

/* Only valid on mailboxes known to be owned by us. Next valid actions are recv and init. */
extern void ipc_hw_mbox_put(ipc_hardware_mailbox_ctx_t *mb, unsigned mbox_idx, ipc_hdr_t hdr, const uint8_t *payload, size_t payload_len);

extern void ipc_hw_mbox_flush(ipc_hardware_mailbox_ctx_t *mb, unsigned mbox_idx);

extern void ipc_hw_mbox_dump(ipc_hardware_mailbox_ctx_t *mb);
