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

#include <tawk/ipc.h>

#include "../protocol.h"

#include "fifo.h"
#include "rstfsm.h"


/*! \file
 *
 * `datalink.h` provides an abstraction over the datalink layer.
 *
 * \section locking Locking
 *
 * The datalink layer API users the IPC stack lock as exposed
 * by the transport layer.  All functions and callbacks must
 * hold the lock on entry and exit.  No functions or callbacks
 * can drop the lock or block.
 *
 *
 * \section link Datalink Link Stats
 *
 * The datalink layer maintain it's own link status.  This transitions
 * between two states, `UP` and `DOWN`.  Callbacks notify transitions
 * between these states.  This notifications as posted; there's no
 * acknowledgement.  It's only valid to call the send/recv API
 * functions whilst in the up state.
 *
 *
 * \section msg Messages
 *
 * The datalink layer provides the ability to send and receive messages.
 * All messages comprise a fixed size header and a variable sized payload.
 *
 * On send, the payload length is passed explicity.
 *
 * On receive, the payload length must be deduced from the header.
 * The received payload is volatile and can be changed arbitrarily by
 * the peer.  It should be copied into local storage.

 * \section send Send Model
 *
 * The datalink layer API uses a callback for sendind messages.  This
 * callback can be masked.  The callback will be called when either a)
 * It becomes valid to send and the callback is currently unmasked, or
 * b) It's currently valid to send and the callback is unmasked.
 *
 * It's only valid to send witin the callback.
 *
 * Users of the API should normally keen the callback masked.  When they
 * wish to send, they should unmask the callback.  Within the callback,
 * they should send until either,
 *   a) they've send all the data they wish to send, in which case they
 *      should remask the callback, or,
 *   b) it becomes invalid to send, in which case the callback will be
 *      called again.
 *
 * \code{.c}
 * {
 *   ...
 *   fifo_push(some_queue, msg);
 *   ipc_dl_enable_notify_can_send(dl);
 *   ...
 * }
 *
 * ipc_dl_notify_can_send(ipc_datalink_ctx_t *dl)
 * {
 *   assert(ipc_dl_can_send(dl));
 *   do {
 *     msg = fifo_pop(some_queue);
 *     ipc_dl_send(dl, msg, ...);
 *     if (fifo_is_empty(some_queue)) {
 *       ipc_dl_disable_notify_can_send(dl);
 *       return;
 *     }
 *   } while (ipc_dl_can_send(dl));
 * }
 *
 * \endcode
 *
 *
 * \section recv Receive Model
 *
 * The datalink API uses a callback for receiving messages.    This
 * callback can be masked.  The callback will be called when either a)
 * It becomes valid to receive and the callback is currently unmasked, or
 * b) It's currently valid to receive and the callback is unmasked.
 *
 * It's only valid to receive within the callback.
 *
 * Users of the API should normally keep the callback unmasked (i.e.
 * datalink messages should not be backpressured).  Within the callback,
 * users should receive all data available.
 *
 * \code{.c}
 * ipc_dl_notify_can_recv(ipc_datalink_ctx_t *dl)
 * {
 *   assert(ipc_dl_can_recv(dl));
 *   do {
 *     ipc_dl_recv(dl, &msg, ...);
 *     handle_msg(msg);
 *   } while (ipc_dl_can_send(dl));
 * }
 *
 * \endcode
 *
 */


#define IPC_DL_TIMEOUT K_MSEC(CONFIG_TAWK_IPC_DATALINK_TIMEOUT)
#define IPC_DL_KEEPALIVE_INTERVAL K_MSEC(CONFIG_TAWK_IPC_DATALINK_KEEPALIVE_INTERVAL)


typedef struct ipc_datalink_ctx_s {
  ipc_datalink_fifo_ctx_t fifo;
  ipc_datalink_reset_ctx_t rst;

  struct k_work timeout_work;
  struct k_work_sync timeout_work_sync;
  struct k_timer timeout_timer;
  k_timepoint_t timeout_exp;
  k_timepoint_t keepalive_exp;
} ipc_datalink_ctx_t;


extern ipc_context_t *ipc_dl_ipc_context(ipc_datalink_ctx_t *dl);

extern int ipc_dl_init(ipc_datalink_ctx_t *dl);
extern void ipc_dl_fini(ipc_datalink_ctx_t *dl);

extern size_t ipc_dl_pld_size(ipc_datalink_ctx_t *dl);

extern void ipc_dl_link_up(ipc_datalink_ctx_t *dl); /* CALLBACK */
extern void ipc_dl_link_down(ipc_datalink_ctx_t *dl); /* CALLBACK */
extern bool ipc_dl_link_is_up(ipc_datalink_ctx_t *dl);
extern void ipc_dl_request_reset(ipc_datalink_ctx_t *dl);

extern bool ipc_dl_can_send(ipc_datalink_ctx_t *dl);
extern void ipc_dl_send(ipc_datalink_ctx_t *dl, ipc_hdr_t hdr, const uint8_t *payload, size_t payload_len);
extern void ipc_dl_notify_can_send(ipc_datalink_ctx_t *dl); /* CALLBACK */
extern void ipc_dl_enable_notify_can_send(ipc_datalink_ctx_t *dl);
extern void ipc_dl_disable_notify_can_send(ipc_datalink_ctx_t *dl);

extern bool ipc_dl_can_recv(ipc_datalink_ctx_t *dl);
extern void ipc_dl_recv(ipc_datalink_ctx_t *dl, ipc_hdr_t *hdr, const volatile uint8_t **payload);
extern void ipc_dl_recv_payload(uint8_t *dst, const volatile uint8_t *payload, size_t pld_len);
extern void ipc_dl_notify_can_recv(ipc_datalink_ctx_t *dl); /* CALLBACK */
extern void ipc_dl_enable_notify_can_recv(ipc_datalink_ctx_t *dl);
extern void ipc_dl_disable_notify_can_recv(ipc_datalink_ctx_t *dl);

extern void ipc_dl_dump(ipc_datalink_ctx_t *dl);
