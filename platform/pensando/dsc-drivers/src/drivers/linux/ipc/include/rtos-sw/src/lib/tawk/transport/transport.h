// SPDX-License-Identifier: MIT or GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#pragma once

#include <zephyr/kernel.h>

#include <tawk/ipc.h>

#include "../protocol.h"
#include "../datalink/datalink.h"

#include "cfgfsm.h"


#define IPC_TP_MAX_INI_REQUESTS (16) /* should be config options */
#define IPC_TP_MAX_TGT_REQUESTS (16) /* should be config options */

#define IPC_TP_TIMEOUT K_MSEC(CONFIG_TAWK_IPC_TRANSPORT_TIMEOUT)


typedef enum ipc_buffer_state_e {
  IPC_BUFFER_FREE,

  IPC_BUFFER_ALLOC,

  /* with local stack to populate request */
  IPC_BUFFER_INI_REQ_FILL,

  /* waiting for mailbox to send to remote */
  IPC_BUFFER_INI_REQ_SEND,

  /* with remote */
  IPC_BUFFER_INI_REMOTE,

  /* with local stack to empty response */
  IPC_BUFFER_INI_RSP_EMPTY,

  /* with local stack to handle transport error */
  IPC_BUFFER_INI_RSP_TPERR,

  /* with local stack to empty request */
  IPC_BUFFER_TGT_REQ_EMPTY,

  /* with local stack to populate response */
  IPC_BUFFER_TGT_RSP_FILL,

  /* waiting for mailbox to send to remote */
  IPC_BUFFER_TGT_RSP_SEND,
} ipc_buffer_state_t;

#define IPC_TP_MAX_PAYLOAD_SIZE (1024)

struct ipc_buffer_s {
  sys_dnode_t q;
  sys_dnode_t user_q;
  ipc_buffer_state_t state;
  ipc_hdr_t hdr;
  uint8_t pld[IPC_TP_MAX_PAYLOAD_SIZE];

  /* only valid for ini, but the overhead is better than
   * a load of type complexity and upcasting. */
  k_timepoint_t timeout_exp;
  ipc_buffer_pool_t *pool;
  ipc_buffer_cb *cb;
  void *cb_ctx;
};

typedef struct ipc_transport_ctx_s {
  ipc_transport_cfg_ctx_t cfg;

  enum {
    IPC_TP_LINK_DOWN_ACK,
    IPC_TP_LINK_UP_REQ,
    IPC_TP_LINK_UP_ACK_WAIT_RDY,
    IPC_TP_LINK_UP_ACK,
    IPC_TP_LINK_DOWN_REQ,
  } link_state;
  bool down_req_pending;

  sys_dlist_t link_event_handlers;
  unsigned link_event_ack_outstanding;

  sys_dlist_t request_handlers;

  /* Common recvq for req and rsp,
   * we want to retain ordering. */
  sys_dlist_t sendq, recvq;

  struct {
    sys_dlist_t unused, avail, req, remote, rsp;
    sys_dlist_t pools;
    ipc_buffer_t buffers[IPC_TP_MAX_INI_REQUESTS];
    ipc_buffer_pool_t common;

    struct k_work timeout_work;
    struct k_work_sync timeout_work_sync;
    struct k_timer timeout_timer;
  } ini;

  struct {
    sys_dlist_t free, local;
    ipc_buffer_t buffers[IPC_TP_MAX_TGT_REQUESTS];
  } tgt;

  /* total across ini.req, ini.remote, ini.rsp
   * and tgt.local */
  unsigned buffers_outstanding;

  /* A timeout for transition IPC_TP_LINK_DOWN_REQ -> IPC_TP_LINK_DOWN_ACK. */
  struct k_work link_down_req_work;
  struct k_work_sync link_down_req_work_sync;
  struct k_timer link_down_req_timer;
} ipc_transport_ctx_t;


extern ipc_context_t *ipc_tp_ipc_context(ipc_transport_ctx_t *tp);
extern ipc_datalink_ctx_t *ipc_tp_dl_context(ipc_transport_ctx_t *tp);

extern void ipc_tp_dump(ipc_transport_ctx_t *tp);

extern int ipc_tp_init(ipc_transport_ctx_t *tp);
extern void ipc_tp_fini(ipc_transport_ctx_t *tp);
