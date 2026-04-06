// SPDX-License-Identifier: MIT or GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#pragma once

#ifdef __KERNEL__
#include <libc_compat.h>
#else
#include <stdbool.h>
#include <stdint.h>
#endif

#include "common.h"
#include <zephyr/kernel.h>


struct ipc_hardware_rstsigs_ctx_s {
  struct k_work work;
  struct k_work_sync work_sync;

  /* True if the local peer is owns the the memory backing the in/out
   * registers and buffers and is this responsible for initialisation
   * on init. */
  bool own_mem;

  mm_reg_t out_addr, in_addr;
  /* It may be expensive to read out_addr, so we cache our current
   * output value.  This also deals with cases where out_addr is
   * writeable by the remote peer; it guarantees to the local stack
   * the return value from get_out_* can only change as a result of
   * a call to set_out_*. Caching in_addr is helpful to help with
   * debugging at a small cost. */
  uint32_t out, in;

  uint8_t quarantine[TAWK_IPC_QUARANTINE_RST_SIGS_SZ];
};


extern ipc_hardware_ctx_t *ipc_hw_rst_hardware(ipc_hardware_rstsigs_ctx_t *sigs);

extern int ipc_hw_rst_init(ipc_hardware_rstsigs_ctx_t *sigs,
                           bool own_mem,
                           mm_reg_t out_addr,
                           mm_reg_t in_addr);
extern void ipc_hw_rst_quarantine(ipc_hardware_rstsigs_ctx_t *sigs);
extern void ipc_hw_rst_fini(ipc_hardware_rstsigs_ctx_t *sigs);

extern void ipc_hw_rst_notify(ipc_hardware_rstsigs_ctx_t *sigs);

extern void ipc_hw_rst_alert(ipc_hardware_rstsigs_ctx_t *sigs); /* CALLBACK */

extern void ipc_hw_rst_notify_peer(ipc_hardware_rstsigs_ctx_t *mb); /* CALLBACK */

extern bool ipc_hw_rst_get_in_req(ipc_hardware_rstsigs_ctx_t *sigs);
extern bool ipc_hw_rst_get_in_ack(ipc_hardware_rstsigs_ctx_t *sigs);

extern bool ipc_hw_rst_get_out_req(ipc_hardware_rstsigs_ctx_t *sigs);
extern void ipc_hw_rst_set_out_req(ipc_hardware_rstsigs_ctx_t *sigs, bool asserted);

extern bool ipc_hw_rst_get_out_ack(ipc_hardware_rstsigs_ctx_t *sigs);
extern void ipc_hw_rst_set_out_ack(ipc_hardware_rstsigs_ctx_t *sigs, bool asserted);

extern void ipc_hw_rst_dump(ipc_hardware_rstsigs_ctx_t *sigs);
