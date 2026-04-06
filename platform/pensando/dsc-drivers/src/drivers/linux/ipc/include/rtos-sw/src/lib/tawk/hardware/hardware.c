// SPDX-License-Identifier: MIT or GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "../ipc_internal.h"
#include "hardware.h"

#ifdef RTOS
#include "log_helper/log_helper.h"

LOG_HELPER_DECLARE(tawk, CONFIG_TAWK_IPC_LOG_LEVEL);
#else
LOG_MODULE_DECLARE(tawk, CONFIG_TAWK_IPC_LOG_LEVEL);
#endif

ipc_context_t *ipc_hw_ipc_context(ipc_hardware_ctx_t *hw)
{
  /* This upcasting is ugly, but I'm not sure how this bit
   * of the code should look, so it feels fine for now. */
  return CONTAINER_OF(hw, ipc_context_t, hw);
}

int ipc_hw_init(ipc_hardware_ctx_t *hw,
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

  rc = ipc_hw_rst_init(&hw->signals,
                       own_mem,
                       rst_sigs_out_addr,
                       rst_sigs_in_addr);
  if (rc != 0)
    goto fail0;

  rc = ipc_hw_mbox_init(&hw->mbox,
                        own_mem,
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
    goto fail1;

  return 0;

 fail1:
  ipc_hw_rst_fini(&hw->signals);

 fail0:
  return rc;
}

void ipc_hw_quarantine(ipc_hardware_ctx_t *hw)
{
  ipc_hw_mbox_quarantine(&hw->mbox);
  ipc_hw_rst_quarantine(&hw->signals);
}

void ipc_hw_fini(ipc_hardware_ctx_t *hw)
{
  ipc_hw_mbox_fini(&hw->mbox);
  ipc_hw_rst_fini(&hw->signals);
}

void ipc_hw_dump(ipc_hardware_ctx_t *hw)
{
  TAWK_IPC_DUMP(ipc_hw_ipc_context(hw), "****** HW layer dump ******");
  ipc_hw_mbox_dump(&hw->mbox);
  ipc_hw_rst_dump(&hw->signals);
}
