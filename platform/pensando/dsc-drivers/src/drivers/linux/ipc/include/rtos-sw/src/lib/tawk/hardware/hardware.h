// SPDX-License-Identifier: MIT or GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#pragma once

#include <zephyr/kernel.h>

#include <tawk/ipc.h>

#include "common.h"
#include "mailbox.h"
#include "rstsigs.h"


struct ipc_hardware_ctx_s {
  ipc_hardware_rstsigs_ctx_t signals;
  ipc_hardware_mailbox_ctx_t mbox;
};


extern ipc_context_t *ipc_hw_ipc_context(ipc_hardware_ctx_t *hw);

extern int ipc_hw_init(ipc_hardware_ctx_t *hw,
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
extern void ipc_hw_quarantine(ipc_hardware_ctx_t *hw);
extern void ipc_hw_fini(ipc_hardware_ctx_t *hw);

extern void ipc_hw_dump(ipc_hardware_ctx_t *hw);
