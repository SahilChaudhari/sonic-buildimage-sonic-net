// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "../ipc_internal.h"
#include "../hardware/rstsigs.h"

#include "datalink.h"
#include "rstfsm.h"

#ifdef RTOS
#include "log_helper/log_helper.h"

LOG_HELPER_DECLARE(tawk, CONFIG_TAWK_IPC_LOG_LEVEL);
#else
LOG_MODULE_DECLARE(tawk, CONFIG_TAWK_IPC_LOG_LEVEL);
#endif

/* Help readability later  */
#define RST_OUT_REQ_IS_ASSERTED(sigs_) (ipc_hw_rst_get_out_req(sigs_))
#define RST_OUT_REQ_ASSERT(sigs_) ipc_hw_rst_set_out_req(sigs_, true)
#define RST_OUT_REQ_DEASSERT(sigs_) ipc_hw_rst_set_out_req(sigs_, false)
#define RST_IN_ACK_IS_ASSERTED(sigs_) (ipc_hw_rst_get_in_ack(sigs_))
#define RST_IN_ACK_IS_DEASSERTED(sigs_) (!ipc_hw_rst_get_in_ack(sigs_))

#define RST_IN_REQ_IS_ASSERTED(sigs_) (ipc_hw_rst_get_in_req(sigs_))
#define RST_IN_REQ_IS_DEASSERTED(sigs_) (!ipc_hw_rst_get_in_req(sigs_))
#define RST_OUT_ACK_IS_ASSERTED(sigs_) (ipc_hw_rst_get_out_ack(sigs_))
#define RST_OUT_ACK_ASSERT(sigs_) ipc_hw_rst_set_out_ack(sigs_, true)
#define RST_OUT_ACK_DEASSERT(sigs_) ipc_hw_rst_set_out_ack(sigs_, false)


static ipc_datalink_ctx_t *ipc_dl_rst_datalink(ipc_datalink_reset_ctx_t *rst)
{
  /* This upcasting is ugly, but I'm not sure how this bit
   * of the code should look, so it feels fine for now. */
  return CONTAINER_OF(rst, ipc_datalink_ctx_t, rst);
}


static void ipc_dl_rst_rrq_work_wrap(struct k_work *w);

int ipc_dl_rst_init(ipc_datalink_reset_ctx_t *rst)
{
  ipc_datalink_ctx_t *dl = ipc_dl_rst_datalink(rst);
  ipc_context_t *ipc = ipc_dl_ipc_context(dl);
  ipc_hardware_rstsigs_ctx_t *sigs = &ipc->hw.signals;

  k_work_init(&rst->rrq_work, ipc_dl_rst_rrq_work_wrap);

  /* We want to start in a DOWN state and guarantee that the next
   * transition will be into a RESET state.  This ensures that the
   * FIFO will get into a clean state before upper layers can attempt
   * to send messages.
   *
   * Our basic strategy is to ensure that:
   * - RRQ starts in a DOWN_REQ state
   * - RRP starts in DOWN_REQ, UP_REQ or UP_ACK state
   *
   * As explained later, this isn't always achievable, and some
   * special case logic is required.
   *
   *
   * Achieving this needs a little care.  The basic hanshake works for
   * an almost arbitry hardware implemention connecting REQ/ACK
   * between peers, allowing for an implemenation that can a) have
   * multiple edges in flight (think transmission line) and b) can
   * allow consective edges to cancel ought (think bad transmission
   * line).  However, to provide any guarantees, we do have to assume
   * that we've inherited a sane environment and thus we assume that
   * we've inherited out environment from a sanely behaving
   * predecessor.
   *
   * This means that, for the RRQ state machine:
   *
   *  - if the (inherited) OUT_REQ and (current) IN_ACK match then
   *    all the the following are true:
   *     - there's no edge in flight between our OUT_REQ and the
   *       remote peer's IN_REQ
   *     - the remote peer's IN_REQ and OUT_ACK match (and thus
   *       its RRP state machine is quiescent/waiting on us)
   *     - there's no edge in flight between the remote peer's OUT_ACK
   *       and our IN_ACK
   *
   * - if the (inherited) OUT_REQ and (current) IN_ACK do not match
   *   then exactly one of the following is true:
   *     - there's one edge in flight between our OUT_REQ and the
   *       remote peer's IN_REQ
   *     - the remote peer's IN_REQ and OUT_ACK do not match (and thus
   *       its RRP state machine is not quiescent)
   *     - there's one edge in flight between the remote peer's OUT_ACK
   *       and our IN_ACK
   *
   * For the RRP state machine:
   *
   *  - if the (current) IN_REQ and (inherited) OUT_ACK match then
   *    at most of of the following is true:
   *     - there's one edge in flight between our OUT_ACK and the
   *       remote peer's IN_ACK
   *     - there's one edge in flight between the remote peer's OUT_REQ
   *       and our IN_REQ
   *
   * - if the (inherited) IN_REQ and (current) OUT_ACK do not match
   *   then all of the following are true:
   *     - there's no edge in flight between our OUT_ACK and the
   *       remote peer's IN_ACK
   *     - the remote peer's IN_REQ and OUT_ACK do match (and thus
   *       its RRQ state machine is quiescent/waiting on us)
   *     - there's no edge in flight between the remote peer's OUT_REQ
   *       and our IN_REQ
   *
   */

  /* RRQ:
   *  OUT_REQ + !IN_ACK  =>  DOWN_REQ
   *  OUT_REQ +  IN_ACK  =>  DOWN_ACK
   * !OUT_REQ +  IN_ACK  =>  UP_REQ
   * !OUT_REQ + !IN_ACK  =>  UP_ACK
   */
  if (RST_OUT_REQ_IS_ASSERTED(sigs)) {
    /* If IN_ACK in asserted then we're in DOWN_ACK.  However, its
     * assertion would have been the last edge.  We can just pretend
     * that we haven't seen it yet and that we're still in DOWN_REQ.
     * When ipc_dl_rst_rrq_work sees IN_ACK asserted * it'll move us
     * into DOWN_ACK.  This, we start in DOWN_REQ and advance to
     * DOWN_ACK, meaning that the exposed link state starts in
     * DOWN and advances to RESET as required.
     */
    rst->rrq = RRQ_DOWN_REQ;
    rst->rrq_down_req_pending = false;
  } else {
    if (RST_IN_ACK_IS_DEASSERTED(sigs)) {
      /* If ACK_IN is deasserted then we're in UP_ACK.  We can assert
       * OUT_REQ and move into a DOWN_REQ state thence DOWN_ACK.  As
       * above, this means that the exposed link state starts in
       * DOWN and advanced to RESET.
       */
      RST_OUT_REQ_ASSERT(sigs);
      rst->rrq = RRQ_DOWN_REQ;
      rst->rrq_down_req_pending = false;
    } else {
      /* If IN_ACK is assered, we're in UP_REQ.  This is problematic as
       * we're waiting on the remote peer to transition to the next
       * state, which will be UP_ACK.  We deal with this by a)
       * settings the down_req_pending flag whilst causes us to advance
       * from UP_ACK directtly to DOWN_REQ, and b) adding logic in
       * ipc_dl_rst_rrq_work that ensures the UP_ACK state is never
       * exposed in this case.  Thus, the states reported to upper
       * layers are again DOWN -> RESET.
       */
      rst->rrq = RRQ_UP_REQ;
      rst->rrq_down_req_pending = true;
    }
  }

  /* RRP:
   *  IN_REQ + !IN_ACK  =>  DOWN_REQ
   *  IN_REQ +  IN_ACK  =>  DOWN_ACK
   * !OUT_REQ +  IN_ACK  =>  UP_REQ
   * !OUT_REQ + !IN_ACK  =>  UP_ACK
   */
  if (RST_OUT_ACK_IS_ASSERTED(sigs)) {
    /*
     * If IN_REQ is deasserted, we're in UP_REQ
     * If IN_REQ is asserted, we're in DOWN_ACK
     *
    */
    if (RST_IN_REQ_IS_ASSERTED(sigs)) {
      /* If IN_REQ is deasserted then we're in DOWN_ACK.  This is a
       * problem because we've committed to the last state transition
       * by asserting OUT_ACK and the next state transition is
       * conditional on the remote peer deasserting REQ.  However, we
       * don't want to be in DOWN_ACK because that prevents us from
       * exposing a DOWN state to upper layers ensuring that they
       * see a transition to RESET.
       *
       * We therefore use a special state that behaves as a hidden
       * DOWN_ACK. */
      rst->rrp = RRP_DOWN_ACK_INIT;
    } else {
      /* If IN_REQ is deasserted then we're in UP_REQ.  We Can just
       * deassert OUT_ACK and move into UP_ACK.  As the RRQ state
       * machine is in DOWN_REQ, the exposed link state remains in
       * LINK_DPOWN as we advance from UP_REQ to UP_ACK, and thus we
       * don't need to advertise the change */
      RST_OUT_ACK_DEASSERT(sigs);
      rst->rrp = RRP_UP_ACK;
    }

  } else {
    /* If IN_REQ in asserted then we're in DOWN_REQ.  However, its
     * assertion would have been the last edge.  We can just pretend
     * that we haven't seen it yet and that we're still in UP_ACK.
     * When ipc_dl_rst_rrp_work sees IN_REQ asserted * it'll move us
     * into DOWN_REQ.  Thus, we start in a state other that DOWN_ACK
     * and this the exposed link state starts in LINK down as
     * required.  The RRQ state machine guarantees that it'll advance
     * to RESET first.
     */
    rst->rrp = RRP_UP_ACK;
  }

  rst->reset_depth = ((rst->rrq == RRQ_DOWN_ACK) +
                      (rst->rrp == RRP_DOWN_ACK));
  rst->down_depth = ((rst->rrq != RRQ_UP_ACK) +
                     (rst->rrp != RRP_UP_ACK));

  TAWK_IPC_ASSERT(ipc, !ipc_dl_rst_in_reset(rst), "Expected to bootstrap into DOWN state, not RESET");
  TAWK_IPC_ASSERT(ipc, !ipc_dl_rst_in_up(rst), "Expected to bootstrap into DOWN state, not UP");

  /* As explained in the above init code, there are several cases where:
   *
   * - The RST_OUT_REQ and RST_IN_ACK mean that RRQ is in a *_ACK state,
   *   but we pretend that we're actually in the previous *_REQ state so
   *   that we can transition into the *_ACK state later and perform the
   *   actions needed on entry into that state.  This is because, in an
   *   *_ACK state, progress is waiting on us to make progress, and the
   *   actions performed on entry into that *_ACK state are needed
   *   to make progress.
   *
   * - The RST_IN_REQ and RST_OUT_ACK states mean that RRP is in a *_REQ
   *   state, but we pretend that we're actually in the previous *_ACK
   *   state for the same reasons as above.
   *
   * We're therefore reliant of a future calls being made to
   * ipc_dl_rst_rrq_work and ipc_dl_rst_rrq_work.  In polling mode, this
   * will happen automatically.  However, if using interrupts, this is not
   * guaranteed to happen.  The interrupt notifying us of the change that
   * we're pretending not to have seen may has likely already been sent
   * and ignored.  We therefore need to arrange to call ipc_dl_rst_rrq_work
   * and ipc_dl_rst_rrq_work ourselves.
   *
   * We can't just call them directly from here because they may invoke
   * callbacks into higher layers of the stack that haven't yet been
   * initialised.  We therefore instead call ipc_hw_rst_notify, which
   * arranges for worker thread to call ipc_hw_rst_alert thence the two
   * necessary functions.  Because it goes through a work queue, the call
   * to ipc_hw_rst_alert cannot happen until all the init functions have
   * run and returned, and whatever is instantiating the IPC stack has
   * released the lock.
   */
  ipc_hw_rst_notify(sigs);

  return 0;
}

void ipc_dl_rst_fini(ipc_datalink_reset_ctx_t *rst)
{
  k_work_cancel_sync(&rst->rrq_work, &rst->rrq_work_sync);
}

static void ipc_dl_rst_enter_reset_wrap(ipc_datalink_reset_ctx_t *rst)
{
  ipc_datalink_ctx_t *dl = ipc_dl_rst_datalink(rst);
  ipc_context_t *ipc = ipc_dl_ipc_context(dl);
  if (rst->reset_depth++ == 0)
    ipc_dl_rst_enter_reset(rst);
  TAWK_IPC_ASSERT(ipc, rst->reset_depth <= 2, "%d more down_req than up_req", rst->reset_depth);
  TAWK_IPC_ASSERT(ipc, rst->reset_depth <= rst->down_depth, "%d %d", rst->reset_depth, rst->down_depth);
}

static void ipc_dl_rst_exit_reset_wrap(ipc_datalink_reset_ctx_t *rst)
{
  ipc_datalink_ctx_t *dl = ipc_dl_rst_datalink(rst);
  ipc_context_t *ipc = ipc_dl_ipc_context(dl);
  TAWK_IPC_ASSERT(ipc, rst->reset_depth > 0, "more up_req than down_req");
  if (--rst->reset_depth == 0)
    ipc_dl_rst_exit_reset(rst);
}

static void ipc_dl_rst_down_wrap(ipc_datalink_reset_ctx_t *rst)
{
  ipc_datalink_ctx_t *dl = ipc_dl_rst_datalink(rst);
  ipc_context_t *ipc = ipc_dl_ipc_context(dl);
  if (rst->down_depth++ == 0)
    ipc_dl_rst_down(rst);
  TAWK_IPC_ASSERT(ipc, rst->down_depth <= 2, "%d more down_req than up_req", rst->down_depth);
}

static void ipc_dl_rst_up_wrap(ipc_datalink_reset_ctx_t *rst)
{
  ipc_datalink_ctx_t *dl = ipc_dl_rst_datalink(rst);
  ipc_context_t *ipc = ipc_dl_ipc_context(dl);
  TAWK_IPC_ASSERT(ipc, rst->down_depth > 0, "more up_req than down_req");
  if (--rst->down_depth == 0)
    ipc_dl_rst_up(rst);
  TAWK_IPC_ASSERT(ipc, rst->reset_depth <= rst->down_depth, "%d %d", rst->reset_depth, rst->down_depth);
}


bool ipc_dl_rst_in_reset(ipc_datalink_reset_ctx_t *rst)
{
  return (rst->reset_depth > 0);
}

bool ipc_dl_rst_in_up(ipc_datalink_reset_ctx_t *rst)
{
  ipc_datalink_ctx_t *dl = ipc_dl_rst_datalink(rst);
  ipc_context_t *ipc = ipc_dl_ipc_context(dl);
  TAWK_IPC_ASSERT(ipc, rst->reset_depth <= rst->down_depth, "%d %d", rst->reset_depth, rst->down_depth);
  return (rst->down_depth == 0);
}


static void ipc_dl_rst_rrq_work(ipc_datalink_reset_ctx_t *rst)
{
  ipc_datalink_ctx_t *dl = ipc_dl_rst_datalink(rst);
  ipc_context_t *ipc = ipc_dl_ipc_context(dl);
  ipc_hardware_rstsigs_ctx_t *sigs = &ipc->hw.signals;

  while (1) {
    switch (rst->rrq) {
    case RRQ_DOWN_REQ:
      if (RST_IN_ACK_IS_DEASSERTED(sigs))
        return; /* We'll be called again when it changes */
      rst->rrq = RRQ_DOWN_ACK;

      ipc_dl_rst_enter_reset_wrap(rst);

      break;

    case RRQ_DOWN_ACK:
      ipc_dl_rst_exit_reset_wrap(rst);

      rst->rrq = RRQ_UP_REQ;
      RST_OUT_REQ_DEASSERT(sigs);

      break;

    case RRQ_UP_REQ:
      if (RST_IN_ACK_IS_ASSERTED(sigs))
        return; /* We'll be called again when it changes */
      rst->rrq = RRQ_UP_ACK;

      /* Ugly but important.  As explained in ipc_dl_rst_init, we
       * want to avoid exposing the UP_ACK state here and go direct
       * to DOWN_REQ. */
      if (rst->rrq_down_req_pending)
        goto RRQ_UP_REQ_TO_DOWN_REQ;

      ipc_dl_rst_up_wrap(rst);

      break;

    case RRQ_UP_ACK:
      if (RST_IN_ACK_IS_ASSERTED(sigs)) {
        /* Protocol violation.  Notify upper layers of the link down then
         * tailcall into a path that'll wait for ACK to go away then request
         * a reset outselves */
        rst->rrq_down_req_pending = true;
      }

      if (!rst->rrq_down_req_pending)
        return; /* We'll be called again when it changes */

      ipc_dl_rst_down_wrap(rst);
      rst->rrq_down_req_pending = false;

      RRQ_UP_REQ_TO_DOWN_REQ:

      rst->rrq = RRQ_DOWN_REQ;
      RST_OUT_REQ_ASSERT(sigs);
      break;

    default:
      TAWK_IPC_ASSERT(ipc, 0, "undefined rst->rrq: %d", rst->rrq);

    }
  }
}

static void ipc_dl_rst_rrp_work(ipc_datalink_reset_ctx_t *rst)
{
  ipc_datalink_ctx_t *dl = ipc_dl_rst_datalink(rst);
  ipc_context_t *ipc = ipc_dl_ipc_context(dl);
  ipc_hardware_rstsigs_ctx_t *sigs = &ipc->hw.signals;

  while (1) {
    switch (rst->rrp) {
    case RRP_DOWN_ACK:
    case RRP_DOWN_ACK_INIT:
      if (RST_IN_REQ_IS_ASSERTED(sigs))
        return; /* We'll be called again when it changes */
      /* in UP_REQ */

      if (rst->rrp != RRP_DOWN_ACK_INIT)
        ipc_dl_rst_exit_reset_wrap(rst);

      /* FALLTHROUGH UP_REQ */

      ipc_dl_rst_up_wrap(rst);

      rst->rrp = RRP_UP_ACK;
      RST_OUT_ACK_DEASSERT(sigs);

      break;

    case RRP_UP_ACK:
      if (!RST_IN_REQ_IS_ASSERTED(sigs))
        return; /* We'll be called again when it changes */
      /* in DOWN_REQ */

      ipc_dl_rst_down_wrap(rst);

      /* FALLTHROUGH DOWN_REQ */

      ipc_dl_rst_enter_reset_wrap(rst);

      rst->rrp = RRP_DOWN_ACK;
      RST_OUT_ACK_ASSERT(sigs);
      break;

    default:
      TAWK_IPC_ASSERT(ipc, 0, "undefined rst->rrp: %d", rst->rrp);
    }
  }
}

void ipc_dl_rst_request_reset(ipc_datalink_reset_ctx_t *rst)
{
  ipc_datalink_ctx_t *dl = ipc_dl_rst_datalink(rst);
  ipc_context_t *ipc = ipc_dl_ipc_context(dl);

  rst->rrq_down_req_pending = true;
  k_work_submit_to_queue(&ipc->workq, &rst->rrq_work);
}

static void ipc_dl_rst_sigs_alert(ipc_datalink_reset_ctx_t *rst)
{
  ipc_dl_rst_rrq_work(rst);
  ipc_dl_rst_rrp_work(rst);
}

void ipc_hw_rst_alert(ipc_hardware_rstsigs_ctx_t *sigs)
{
  ipc_hardware_ctx_t *hw = ipc_hw_rst_hardware(sigs);
  ipc_context_t *ipc = ipc_hw_ipc_context(hw);
  ipc_datalink_reset_ctx_t *rst = &ipc->dl.rst;

  ipc_dl_rst_sigs_alert(rst);
}

static void ipc_dl_rst_rrq_work_wrap(struct k_work *w)
{
  ipc_datalink_reset_ctx_t *rst = CONTAINER_OF(w, ipc_datalink_reset_ctx_t, rrq_work);
  ipc_datalink_ctx_t *dl = ipc_dl_rst_datalink(rst);
  ipc_context_t *ipc = ipc_dl_ipc_context(dl);

  bool got_lock = ipc_lock_for_work(ipc, w);
  if (!got_lock)
    return;

  ipc_dl_rst_rrq_work(rst);

  ipc_unlock(ipc);
}

void ipc_dl_rst_dump(ipc_datalink_reset_ctx_t *rst)
{
  ipc_datalink_ctx_t *dl = ipc_dl_rst_datalink(rst);
  ipc_context_t *ipc = ipc_dl_ipc_context(dl);

  TAWK_IPC_DUMP(ipc, "dl_rst: pending rrq_down_req = %d, rrq = %d, rrp = %d, rst_depth = %d, down_depth = %d",
                rst->rrq_down_req_pending, rst->rrq, rst->rrp, rst->reset_depth, rst->down_depth);
}
