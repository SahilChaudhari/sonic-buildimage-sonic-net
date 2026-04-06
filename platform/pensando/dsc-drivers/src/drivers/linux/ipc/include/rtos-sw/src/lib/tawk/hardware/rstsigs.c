// SPDX-License-Identifier: MIT or GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "../ipc_internal.h"
#include "hardware.h"
#include "rstsigs.h"

#define REQ_MASK (1U)
#define ACK_MASK (2U)

#ifdef RTOS
#include "log_helper/log_helper.h"

LOG_HELPER_DECLARE(tawk, CONFIG_TAWK_IPC_LOG_LEVEL);
#else
LOG_MODULE_DECLARE(tawk, CONFIG_TAWK_IPC_LOG_LEVEL);
#endif

#define PRI_FMT "%s%s%s%s (%x)"
#define PRI_ARGS(x_)                                                      \
  (((x_) & REQ_MASK) ? "RST" : ""),                                       \
  ((((x_) &( REQ_MASK | ACK_MASK)) == (REQ_MASK | ACK_MASK)) ? "|" : ""), \
  ((x_) & ACK_MASK) ? "ACK" : "",                                         \
  ((((x_) & (REQ_MASK | ACK_MASK)) == 0) ? "0" : ""),                     \
  (x_)

static void ipc_hw_rst_work(struct k_work *t);


ipc_hardware_ctx_t *ipc_hw_rst_hardware(ipc_hardware_rstsigs_ctx_t *sigs)
{
  /* This upcasting is ugly, but I'm not sure how this bit
   * of the code should look, so it feels fine for now. */
  return CONTAINER_OF(sigs, ipc_hardware_ctx_t, signals);
}


int ipc_hw_rst_init(ipc_hardware_rstsigs_ctx_t *sigs,
                    bool own_mem,
                    mm_reg_t out_addr,
                    mm_reg_t in_addr)
{
  k_work_init(&sigs->work, ipc_hw_rst_work);

  sigs->own_mem = own_mem;
  sigs->out_addr = out_addr;
  sigs->in_addr = in_addr;

  if (sigs->own_mem) {
    sys_write32(sigs->out, sigs->out_addr);
    sigs->in = 0x51950000; /* Where does this value come from? */
    sys_write32(sigs->in, sigs->in_addr);
  } else {
    sigs->out = sys_read32(sigs->out_addr);
    sigs->in = sys_read32(sigs->in_addr);
  }

  return 0;
}

static void ipc_hw_rst_deinit(ipc_hardware_rstsigs_ctx_t *sigs)
{
  k_work_cancel_sync(&sigs->work, &sigs->work_sync);
}

void ipc_hw_rst_fini(ipc_hardware_rstsigs_ctx_t *sigs)
{
  ipc_hw_rst_deinit(sigs);
}

void ipc_hw_rst_quarantine(ipc_hardware_rstsigs_ctx_t *sigs)
{
  ipc_hw_rst_deinit(sigs);

  memset(sigs->quarantine, 0xff, sizeof(sigs->quarantine));

  sigs->out_addr = (mm_reg_t)&sigs->quarantine[TAWK_IPC_QUARANTINE_RST_SIGS_OUT_REG];
  sigs->in_addr = (mm_reg_t)&sigs->quarantine[TAWK_IPC_QUARANTINE_RST_SIGS_IN_REG];
}

bool ipc_hw_rst_get_in_req(ipc_hardware_rstsigs_ctx_t *sigs)
{
  ipc_hardware_ctx_t *hw = ipc_hw_rst_hardware(sigs);
  ipc_context_t *ipc = ipc_hw_ipc_context(hw);
  uint32_t val;

  ARG_UNUSED(ipc); /*if TAWK_IPC_VERY_VERBOSE_LOG expands to nothing this is unused */

  val = sys_read32(sigs->in_addr);

  if (val != sigs->in) {
    TAWK_IPC_VERY_VERBOSE_LOG(ipc, "%s(%p/%u): IN == " PRI_FMT, __func__, sigs, ipc_local_peer_id(ipc), PRI_ARGS(val));
  }

  sigs->in = val;

  return !!(val & REQ_MASK);
}

bool ipc_hw_rst_get_in_ack(ipc_hardware_rstsigs_ctx_t *sigs)
{
  ipc_hardware_ctx_t *hw = ipc_hw_rst_hardware(sigs);
  ipc_context_t *ipc = ipc_hw_ipc_context(hw);
  uint32_t val;

  ARG_UNUSED(ipc); /* if TAWK_IPC_VERY_VERBOSE_LOG expands to nothing this is unused */

  val = sys_read32(sigs->in_addr);

  if (val != sigs->in) {
    TAWK_IPC_VERY_VERBOSE_LOG(ipc, "%s(%p/%u): IN == " PRI_FMT, __func__, sigs, ipc_local_peer_id(ipc), PRI_ARGS(val));
  }

  sigs->in = val;

  return !!(val & ACK_MASK);
}

bool ipc_hw_rst_get_out_req(ipc_hardware_rstsigs_ctx_t *sigs)
{
  ipc_hardware_ctx_t *hw = ipc_hw_rst_hardware(sigs);
  ipc_context_t *ipc = ipc_hw_ipc_context(hw);
  ARG_UNUSED(ipc); /* if TAWK_IPC_VERY_VERBOSE_LOG expands to nothing this is unused */

  TAWK_IPC_VERY_VERBOSE_LOG(ipc, "%s(%p/%u): OUT == " PRI_FMT "\n", __func__, sigs, ipc_local_peer_id(ipc),
                            PRI_ARGS(sigs->out));
  return !!(sigs->out & REQ_MASK);
}

void ipc_hw_rst_set_out_req(ipc_hardware_rstsigs_ctx_t *sigs, bool asserted)
{
  ipc_hardware_ctx_t *hw;
  ipc_context_t *ipc;
  uint32_t oval;

  if (asserted)
    sigs->out |= REQ_MASK;
  else
    sigs->out &= ~REQ_MASK;

  hw = ipc_hw_rst_hardware(sigs);
  ipc = ipc_hw_ipc_context(hw);
  oval = sigs->out;

  ARG_UNUSED(ipc); /* if TAWK_IPC_VERY_VERBOSE_LOG expands to nothing this is unused */
  ARG_UNUSED(oval); /* if TAWK_IPC_VERY_VERBOSE_LOG expands to nothing this is unused */

  TAWK_IPC_VERY_VERBOSE_LOG(ipc, "%s(%p/%u): OUT " PRI_FMT " -> " PRI_FMT, __func__, sigs, ipc_local_peer_id(ipc),
                            PRI_ARGS(oval), PRI_ARGS(sigs->out));

  sys_write32(sigs->out, sigs->out_addr);
  ipc_hw_rst_notify_peer(sigs);
}

bool ipc_hw_rst_get_out_ack(ipc_hardware_rstsigs_ctx_t *sigs)
{
  ipc_hardware_ctx_t *hw = ipc_hw_rst_hardware(sigs);
  ipc_context_t *ipc = ipc_hw_ipc_context(hw);
  ARG_UNUSED(ipc); /* if TAWK_IPC_VERY_VERBOSE_LOG expands to nothing this is unused */

  TAWK_IPC_VERY_VERBOSE_LOG(ipc, "%s(%p/%u): OUT == " PRI_FMT, __func__, sigs, ipc_local_peer_id(ipc), PRI_ARGS(sigs->out));

  return !!(sigs->out & ACK_MASK);
}

void ipc_hw_rst_set_out_ack(ipc_hardware_rstsigs_ctx_t *sigs, bool asserted)
{
  ipc_hardware_ctx_t *hw;
  ipc_context_t *ipc;
  uint32_t oval;

  if (asserted)
    sigs->out |= ACK_MASK;
  else
    sigs->out &= ~ACK_MASK;

  hw = ipc_hw_rst_hardware(sigs);
  ipc = ipc_hw_ipc_context(hw);
  oval = sigs->out;

  ARG_UNUSED(ipc); /* if TAWK_IPC_VERY_VERBOSE_LOG expands to nothing this is unused */
  ARG_UNUSED(oval); /* if TAWK_IPC_VERY_VERBOSE_LOG expands to nothing this is unused */

  TAWK_IPC_VERY_VERBOSE_LOG(ipc, "%s(%p/%u): OUT " PRI_FMT " -> " PRI_FMT, __func__, sigs, ipc_local_peer_id(ipc), PRI_ARGS(oval), PRI_ARGS(sigs->out));

  sys_write32(sigs->out, sigs->out_addr);
  ipc_hw_rst_notify_peer(sigs);
}

static void ipc_hw_rst_work(struct k_work *w)
{
  ipc_hardware_rstsigs_ctx_t *sigs = CONTAINER_OF(w, ipc_hardware_rstsigs_ctx_t, work);
  ipc_hardware_ctx_t *hw = ipc_hw_rst_hardware(sigs);
  ipc_context_t *ipc = ipc_hw_ipc_context(hw);

  bool got_lock = ipc_lock_for_work(ipc, w);
  if (!got_lock)
    return;

  ipc_hw_rst_alert(sigs);

  ipc_unlock(ipc);
}

void ipc_hw_rst_notify(ipc_hardware_rstsigs_ctx_t *sigs)
{
  ipc_hardware_ctx_t *hw = ipc_hw_rst_hardware(sigs);
  ipc_context_t *ipc = ipc_hw_ipc_context(hw);

  k_work_submit_to_queue(&ipc->workq, &sigs->work);
}

void ipc_hw_rst_dump(ipc_hardware_rstsigs_ctx_t *sigs)
{
  ipc_hardware_ctx_t *hw = ipc_hw_rst_hardware(sigs);
  ipc_context_t *ipc = ipc_hw_ipc_context(hw);

  TAWK_IPC_DUMP(ipc, "hw_rst: lib view (out %" PRIx32 ") hw view (in %" PRIx32 " out %" PRIx32 ")",
                sigs->out, sys_read32(sigs->in_addr), sys_read32(sigs->out_addr));
}
