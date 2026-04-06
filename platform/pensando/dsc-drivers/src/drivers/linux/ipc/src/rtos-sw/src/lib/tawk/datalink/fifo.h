// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#pragma once

#ifdef __KERNEL__
#include <libc_compat.h>
#else
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#endif

#include "../hardware/hardware.h"
#include "../hardware/mailbox.h"
#include "../protocol.h"


typedef struct {
  enum state {
    IPC_DL_FF_STATE_RESET,
    IPC_DL_FF_STATE_DOWN_INDET,
    IPC_DL_FF_STATE_DOWN,
    IPC_DL_FF_STATE_UP,
  } state;
  unsigned rptr, wptr;

  bool got_rptr;
  ipc_hdr_t rptr_hdr;
  const volatile uint8_t *rptr_pld;

  bool enable_notify_can_push, enable_notify_can_pop;
} ipc_datalink_fifo_ctx_t;


extern int ipc_dl_fifo_init(ipc_datalink_fifo_ctx_t *ff);
extern void ipc_dl_fifo_fini(ipc_datalink_fifo_ctx_t *ff);

extern size_t ipc_dl_fifo_pld_size(ipc_datalink_fifo_ctx_t *ff);

extern void ipc_dl_fifo_down(ipc_datalink_fifo_ctx_t *ff);
extern void ipc_dl_fifo_enter_reset(ipc_datalink_fifo_ctx_t *ff);
extern void ipc_dl_fifo_exit_reset(ipc_datalink_fifo_ctx_t *ff);
extern void ipc_dl_fifo_up(ipc_datalink_fifo_ctx_t *ff);

extern bool ipc_dl_fifo_is_up(const ipc_datalink_fifo_ctx_t *ff);

/* these follow exactly the same semantics as the main datalink api */
extern bool ipc_dl_fifo_can_push(const ipc_datalink_fifo_ctx_t *ff);
extern void ipc_dl_fifo_push(ipc_datalink_fifo_ctx_t *ff, ipc_hdr_t hdr, const uint8_t *payload, size_t payload_len);
extern void ipc_dl_fifo_notify_can_push(ipc_datalink_fifo_ctx_t *ff); /* CALLBACK */
extern void ipc_dl_fifo_enable_notify_can_push(ipc_datalink_fifo_ctx_t *ff);
extern void ipc_dl_fifo_disable_notify_can_push(ipc_datalink_fifo_ctx_t *ff);

/* these follow exactly the same semantics as the main datalink api */
extern bool ipc_dl_fifo_can_pop(const ipc_datalink_fifo_ctx_t *ff);
extern void ipc_dl_fifo_pop(ipc_datalink_fifo_ctx_t *ff, ipc_hdr_t *hdr, const volatile uint8_t **payload);
extern void ipc_dl_fifo_pop_payload(uint8_t *dst, const volatile uint8_t *payload, size_t pld_len);
extern void ipc_dl_fifo_notify_can_pop(ipc_datalink_fifo_ctx_t *ff); /* CALLBACK */
extern void ipc_dl_fifo_enable_notify_can_pop(ipc_datalink_fifo_ctx_t *dl);
extern void ipc_dl_fifo_disable_notify_can_pop(ipc_datalink_fifo_ctx_t *dl);

extern void ipc_dl_fifo_dump(ipc_datalink_fifo_ctx_t *ff);
