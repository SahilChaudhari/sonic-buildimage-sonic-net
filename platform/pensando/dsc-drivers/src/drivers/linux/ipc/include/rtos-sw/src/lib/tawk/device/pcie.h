// SPDX-License-Identifier: MIT or GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#pragma once

typedef struct ipc_device_pcie_ctx_s {
#if defined(RTOS)
  ipc_device_pcie_config_t cfg;

  struct k_timer bar_hdr_poll_timer;

  mm_reg_t bar_hdr_rd_base_addr;
  mm_reg_t bar_hdr_wr_base_addr;

#if defined(CONFIG_TAWK_IPC_WITH_INTR)
  uint32_t rst_db_data;
  uint32_t mbox_db_data;

  bool interrupt_en;
  uint32_t rst_intr;
  uint32_t mbox_intr;

  uint32_t intrb;
  uint32_t intrc;
#endif
#elif defined(IPC_BUILD_KERNEL_MODULE)
  int dummy;
#else
#error "Who am I"
#endif
} ipc_device_pcie_ctx_t;
