// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#pragma once

#ifdef __KERNEL__
#include <libc_compat.h>
#else
#include <stdbool.h>
#include <stdint.h>
#endif


/*
 * === General Documentation ===
 *
 * This module managers the reset communication between peers.
 *
 * Each peer has a REQ and ACK input and output.  These form two
 * independent state machines that allow either end to to request a
 * reset.  For a given state machine, one peer is the requester (and
 * uses the REQ output and ACK input) and the other is the responded
 * (and uses the REQ input and ACK output).  The request us termed RRQ
 * and the response RRP.
 *
 * Each state machine has fource states: DOWN_ACK, UP_REQ, UP_ACK and
 * DOWN_REQ.  The requester drives transitions from ACK to REQ states
 * and the response drives transitions from ACK to REQ states:
 *
 *  - In the UP_ACK state, REQ and ACK are deasserted.
 *  - To move into the DOWN_REQ state, the requester asserts REQ
 *  - To move into the DOWN_ACK state, the responder asserts ACK
 *  - To move into the DOWN_REQ state, the requester desserts REQ
 *  - To move into the UP_ACK state, the responder deasserts ACK
 *
 * It's required that the responder act in a timtely manner (i.e.
 * immediately taken the actions necessery to advance its RRP state
 * machine from REQ states to the subsequent ACK states).
 *
 * There's no equivalent requirement on the requester.  It can request
 * entry into reset or exit from reset whenever it likes.
 *
 * However, the currently implementation tries to keep the datalink
 * up, i.e. treats DOWN_ACK as a transient state and immediately
 * transitions the requester state machine into UP_REQ.
 *
 * The RRQ and RRP state machines operate independently.  Their state
 * is combined into a single datalink reset (DL_RST) state.
 *
 * This has three possible states: UP, DOWN, RESET.
 * These map as as follows:
 *
 * |  RRQ                |  RRP                 |  DL_RST  |
 * |---------------------|----------------------|----------|
 * |  DOWN_ACK           |   *                  |  RESET   |
 * |  *                  |  DOWN_ACK            |  RESET   |
 * |  UP_REQ or DOWN_REQ |  not DOWN_ACK        |  DOWN    |
 * |  not DOWN_ACK       |  UP_REQ or DOWN_REQ  |  DOWN    |
 * |  UP_ACK             |  UP_ACK              |  UP      |
 *
 * This provides several guarantees:
 *   - It's impossible to transition directly between RESET and UP.
 *     The only valid transitions are RESET <-> DOWN and DOWN <-> UP.
 *   - A transition from UP -> DOWN will be followed by at least
 *     one DOWN -> RESET transition before the next DOWN -> UP
 *     transition.
 *   - If the local peer transitions UP -> DOWN then it will not
 *     transition DOWN -> RESET until the until the remote PEER
 *     is in the DOWN state
 *   - If the local peer transitions DOWN -> RESET then it will not
 *     transition RESET -> DOWN until the remote peer is in the RESET
 *     state.
 *   - If the local peer transitions RESET -> DOWN then it will not
 *     transition DOWN -> UP until the remote peer is in the DOWN
 *     start.
 *
 * The above guarantees allow us to build a safe way to reset a
 * FIFO-like communication channel:
 *
 *  - On UP -> DOWN, all interaction with the local end of the FIFO
 *    is quiesced.
 *  - On DOWN -> RESET and RESET -> DOWN, FIFO-specific actions can
 *    be taken with the knowledge that the a) the remote end of the
 *    FIFO is quiesced, that b) local actions during DOWN -> RESET
 *    will complete before remote actions during RESET -> DOWN
 *  - On DOWN -> UP, interaction with the FIFO starts again.
 *
 *
 * === Implementation Specifics ===
 *
 * For now, it is assumed that the transitions between the DL_RST
 * states are atomic (e.g. we don't need to request putting the FIFO
 * into reset and then do other processing until it's acknowleged that
 * it is in reset).  If this ceases to be a reasonable assumption, the
 * callbacks for the four transitions could be made async (req/ack).
 *
 * We'd need to stall the RRQ and RRP state machines as needed whilst
 * waiting for an acknowledgement to DL_RST state change.
 */

typedef struct ipc_datalink_reset_ctx_s {
  /* Requester state machine */
  enum {
    RRQ_DOWN_REQ,
    RRQ_DOWN_ACK,
    RRQ_UP_REQ,
    RRQ_UP_ACK,
  } rrq;
  bool rrq_down_req_pending;
  struct k_work rrq_work;
  struct k_work_sync rrq_work_sync;

  /* Responder state machine */
  enum {
    RRP_DOWN_ACK,
    /* No UP_REQ, we go straight to UP_ACK */
    RRP_UP_ACK,
    /* No DOWN_REQ, we go straight to DOWN_ACK */
    RRP_DOWN_ACK_INIT, /* special state used during init; see comments
                        * where this state is assigned */

  } rrp;

  /* Combining the RRQ and RRP states as above and detcting changes is
   * awkardward.  The simpler way is to keep track of a) how many
   * state machines are in DOWN_ACK (as reset_depth), and b) how many
   * state machines are in DOWN_REQ, DOWN_ACK or UP_REQ (as
   * down_depth).  If reset_depth is non-zero then we're in RESET,
   * else if down_depth is non-zero we're in DOWN else we're in UP. */
  int reset_depth;
  int down_depth;
} ipc_datalink_reset_ctx_t;



extern int ipc_dl_rst_init(ipc_datalink_reset_ctx_t *rst);
extern void ipc_dl_rst_fini(ipc_datalink_reset_ctx_t *rst);

extern void ipc_dl_rst_request_reset(ipc_datalink_reset_ctx_t *rst);

/* Are we in RESET */
extern bool ipc_dl_rst_in_reset(ipc_datalink_reset_ctx_t *rst);

/* Are we in UP */
extern bool ipc_dl_rst_in_up(ipc_datalink_reset_ctx_t *rst);

extern void ipc_dl_rst_dump(ipc_datalink_reset_ctx_t *rst);

/* LINK_DOWN -> FIFO_DOWN */
extern void ipc_dl_rst_enter_reset(ipc_datalink_reset_ctx_t *rst); /* CALLBACK */

/* RESET -> DOWN */
extern void ipc_dl_rst_exit_reset(ipc_datalink_reset_ctx_t *rst); /* CALLBACK */

/* UP -> DOWN */
extern void ipc_dl_rst_down(ipc_datalink_reset_ctx_t *rst); /* CALLBACK */

/* DOWN -> UP */
extern void ipc_dl_rst_up(ipc_datalink_reset_ctx_t *rst);  /* CALLBACK */
