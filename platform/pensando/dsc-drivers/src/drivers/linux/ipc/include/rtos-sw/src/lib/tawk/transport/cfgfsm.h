// SPDX-License-Identifier: MIT or GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#pragma once

#include <zephyr/kernel.h>

#include "../protocol.h"


typedef enum ipc_tl_cfg_state_e {
  IPC_TL_CFG_DOWN,

  IPC_TL_CFG_VER_SEND_RECV,
  IPC_TL_CFG_VER_SEND,
  IPC_TL_CFG_VER_RECV,

  IPC_TL_CFG_CAPS_SEND_RECV,
  IPC_TL_CFG_CAPS_SEND,
  IPC_TL_CFG_CAPS_RECV,

  IPC_TL_CFG_READY_WAIT_RECV,
  IPC_TL_CFG_READY_WAIT,

  IPC_TL_CFG_READY_SEND_RECV,
  IPC_TL_CFG_READY_SEND,
  IPC_TL_CFG_READY_RECV,

  IPC_TL_CFG_BOTH_READY,
} ipc_tl_cfg_state_t;

typedef struct ipc_transport_cfg_ctx_s {
  ipc_tl_cfg_state_t state;

  k_timepoint_t timeout_exp;
  size_t max_ini_req;
  size_t max_tgt_req;
} ipc_transport_cfg_ctx_t;


extern int ipc_tp_cfg_init(ipc_transport_cfg_ctx_t *cfg);
extern void ipc_tp_cfg_fini(ipc_transport_cfg_ctx_t *cfg);

extern void ipc_tp_cfg_up(ipc_transport_cfg_ctx_t *cfg);
extern void ipc_tp_cfg_done(ipc_transport_cfg_ctx_t *cfg); /* CALLBACK */
extern bool ipc_tp_cfg_is_done(ipc_transport_cfg_ctx_t *cfg);
extern void ipc_tp_cfg_local_ready(ipc_transport_cfg_ctx_t *cfg);
extern void ipc_tp_cfg_both_ready(ipc_transport_cfg_ctx_t *cfg); /* CALLBACK */
extern void ipc_tp_cfg_down(ipc_transport_cfg_ctx_t *cfg);

extern void ipc_tp_cfg_notify_can_send(ipc_transport_cfg_ctx_t *cfg);
extern void ipc_tp_cfg_handle_recv(ipc_transport_cfg_ctx_t *cfg, ipc_hdr_t hdr, const volatile uint8_t *pld);

extern void ipc_tp_cfg_timeout_work(ipc_transport_cfg_ctx_t *cfg);

extern size_t ipc_tp_cfg_max_ini(ipc_transport_cfg_ctx_t *cfg);
extern size_t ipc_tp_cfg_max_tgt(ipc_transport_cfg_ctx_t *cfg);
