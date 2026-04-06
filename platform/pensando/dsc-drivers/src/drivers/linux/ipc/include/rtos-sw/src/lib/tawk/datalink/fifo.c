// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "../ipc_internal.h"
#include "../hardware/mailbox.h"
#include "../protocol.h"

#include "datalink.h"
#include "fifo.h"

#ifdef RTOS
#include "log_helper/log_helper.h"

LOG_HELPER_DECLARE(tawk, CONFIG_TAWK_IPC_LOG_LEVEL);
#else
LOG_MODULE_DECLARE(tawk, CONFIG_TAWK_IPC_LOG_LEVEL);
#endif

static ipc_datalink_ctx_t *ipc_dl_fifo_datalink(const ipc_datalink_fifo_ctx_t *fifo)
{
  /* This upcasting is ugly, but I'm not sure how this bit
   * of the code should look, so it feels fine for now. */
  return CONTAINER_OF(fifo, ipc_datalink_ctx_t, fifo);
}


int ipc_dl_fifo_init(ipc_datalink_fifo_ctx_t *ff)
{
  ff->state = IPC_DL_FF_STATE_DOWN_INDET;
  /* Setting both rptr and wptr makes the fifo look full, and thus
   * our first call to ipc_dl_fifo_enter_reset will cause us to
   * flush every mailbox. */
  ff->rptr = ff->wptr = 0;
  ff->got_rptr = false;
  ff->enable_notify_can_push = false;
  ff->enable_notify_can_pop = false;

  return 0;
}

void ipc_dl_fifo_fini(ipc_datalink_fifo_ctx_t *ff)
{
  ;
}


static unsigned ipc_dl_fifo_mb_count(const ipc_datalink_fifo_ctx_t *ff)
{
  ipc_datalink_ctx_t *dl = ipc_dl_fifo_datalink((/*-const*/ ipc_datalink_fifo_ctx_t *)ff);
  ipc_context_t *ipc = ipc_dl_ipc_context(dl);
  ipc_hardware_mailbox_ctx_t *mb = &ipc->hw.mbox;
  return ipc_hw_mbox_count(mb);
}


size_t ipc_dl_fifo_pld_size(ipc_datalink_fifo_ctx_t *ff)
{
  ipc_datalink_ctx_t *dl = ipc_dl_fifo_datalink(ff);
  ipc_context_t *ipc = ipc_dl_ipc_context(dl);
  ipc_hardware_mailbox_ctx_t *mb = &ipc->hw.mbox;

  return ipc_hw_mbox_buffer_size(mb);
}


void ipc_dl_fifo_down(ipc_datalink_fifo_ctx_t *ff)
{
  ipc_datalink_ctx_t *dl = ipc_dl_fifo_datalink(ff);
  ipc_context_t *ipc = ipc_dl_ipc_context(dl);
  TAWK_IPC_ASSERT_NO_MSG(ipc, ff->state == IPC_DL_FF_STATE_UP);

  TAWK_IPC_VERY_VERBOSE_LOG(ipc, "%s(%p)", __func__, ff);

  /* No special action on UP -> DOWN.  We do everything in
   * DOWN -> RESET so that we do it during boot too. */

  ff->state = IPC_DL_FF_STATE_DOWN_INDET;

  /////////// TODO: mask box alert
}

void ipc_dl_fifo_enter_reset(ipc_datalink_fifo_ctx_t *ff)
{
  ipc_datalink_ctx_t *dl = ipc_dl_fifo_datalink(ff);
  ipc_context_t *ipc = ipc_dl_ipc_context(dl);
  ipc_hardware_mailbox_ctx_t *mb = &ipc->hw.mbox;
  unsigned mb_count = ipc_dl_fifo_mb_count(ff);
  unsigned mb_mask = mb_count - 1;
  TAWK_IPC_ASSERT(ipc, (mb_count & mb_mask) == 0, "mb_count not power of 2");

  TAWK_IPC_VERY_VERBOSE_LOG(ipc, "%s(%p)", __func__, ff);

  TAWK_IPC_ASSERT_NO_MSG(ipc, ff->state == IPC_DL_FF_STATE_DOWN_INDET ||
                  ff->state == IPC_DL_FF_STATE_DOWN);

  /* On DOWN -> RESET ensure that any mailbox_put operations
   * have hit the hardware and won't trash a later mailbox_scrub
   * by the remote. */

  for (unsigned mbox_idx = ff->rptr - mb_count; mbox_idx != ff->wptr; mbox_idx++)
    ipc_hw_mbox_flush(mb, mbox_idx & mb_mask);

  ff->state = IPC_DL_FF_STATE_RESET;
}

void ipc_dl_fifo_exit_reset(ipc_datalink_fifo_ctx_t *ff)
{
  ipc_datalink_ctx_t *dl = ipc_dl_fifo_datalink(ff);
  ipc_context_t *ipc = ipc_dl_ipc_context(dl);
  ipc_hardware_mailbox_ctx_t *mb = &ipc->hw.mbox;
  unsigned mb_count = ipc_dl_fifo_mb_count(ff);

  TAWK_IPC_VERY_VERBOSE_LOG(ipc, "%s(%p)", __func__, ff);

  TAWK_IPC_ASSERT_NO_MSG(ipc, ff->state == IPC_DL_FF_STATE_RESET);

  /* On RESET -> DOWN, initialise the mailbox to the correct owner
   * and flush that to the hardware.  Also, flush any read caches
   * for mailboxes we don't own and re therefore polling on. */

  ff->wptr = ipc_local_peer_id(ipc) * mb_count / 2;
  ff->rptr = ff->wptr + mb_count / 2;
  for (unsigned mbox_idx = 0; mbox_idx < mb_count; mbox_idx++)
    ipc_hw_mbox_scrub(mb, mbox_idx, (mbox_idx < mb_count / 2) ?
                      ipc_remote_peer_id(ipc) : ipc_local_peer_id(ipc));

  /////////// TODO: scrub mbox alerts

  ff->state = IPC_DL_FF_STATE_DOWN;
}

void ipc_dl_fifo_up(ipc_datalink_fifo_ctx_t *ff)
{
  ipc_datalink_ctx_t *dl = ipc_dl_fifo_datalink(ff);
  ipc_context_t *ipc = ipc_dl_ipc_context(dl);
  TAWK_IPC_ASSERT_NO_MSG(ipc, ff->state == IPC_DL_FF_STATE_DOWN);

  TAWK_IPC_VERY_VERBOSE_LOG(ipc, "%s(%p)", __func__, ff);

  /* No special action on DOWN -> UP. */

  ff->state = IPC_DL_FF_STATE_UP;

  /////////// TODO: unmask box alert
}

bool ipc_dl_fifo_is_up(const ipc_datalink_fifo_ctx_t *ff)
{
  ipc_datalink_ctx_t *dl = ipc_dl_fifo_datalink(ff);
  ipc_context_t *ipc = ipc_dl_ipc_context(dl);
  unsigned mb_count = ipc_dl_fifo_mb_count(ff);

  if (ff->state != IPC_DL_FF_STATE_UP)
    return false;

  IPC_USED_ONLY_IN_ASSERT(mb_count);
  TAWK_IPC_ASSERT(ipc, ff->rptr - ff->wptr <= mb_count,
           "FF RPTR (%u) and WPTR (%u) more than a FIFO size apart", ff->rptr, ff->wptr);
  return true;
}

static bool ipc_dl_fifo_full(const ipc_datalink_fifo_ctx_t *ff)
{
  ipc_datalink_ctx_t *dl = ipc_dl_fifo_datalink(ff);
  ipc_context_t *ipc = ipc_dl_ipc_context(dl);
  TAWK_IPC_ASSERT(ipc, ipc_dl_fifo_is_up(ff), "fifo down"); /* also sanity checks pointers */
  return (ff->rptr - ff->wptr == 0);
}

static bool ipc_dl_fifo_empty(const ipc_datalink_fifo_ctx_t *ff)
{
  ipc_datalink_ctx_t *dl = ipc_dl_fifo_datalink(ff);
  ipc_context_t *ipc = ipc_dl_ipc_context(dl);
  unsigned mb_count = ipc_dl_fifo_mb_count(ff);

  TAWK_IPC_ASSERT(ipc, ipc_dl_fifo_is_up(ff), "fifo down"); /* also sanity checks pointers */
  return (ff->rptr - ff->wptr == mb_count);
}


static void ipc_dl_fifo_lookahead(ipc_datalink_fifo_ctx_t *ff)
{
  ipc_datalink_ctx_t *dl = ipc_dl_fifo_datalink(ff);
  ipc_context_t *ipc = ipc_dl_ipc_context(dl);
  ipc_hardware_mailbox_ctx_t *mb = &ipc->hw.mbox;
  unsigned mb_count = ipc_dl_fifo_mb_count(ff);
  unsigned mb_mask = mb_count - 1;
  TAWK_IPC_ASSERT(ipc, (mb_count & mb_mask) == 0, "mb_count not power of 2");

  while (!ff->got_rptr && !ipc_dl_fifo_empty(ff)) {
    ff->got_rptr = ipc_hw_mbox_get(mb, ff->rptr & mb_mask, &ff->rptr_hdr, &ff->rptr_pld);
    if (!ff->got_rptr)
      return;
    else if (ipc_hdr_get_type(ff->rptr_hdr) == IPC_TRANSPORT_HDR_TYPE_NOOP) {
      ff->rptr++;
      ff->got_rptr = false;
    }
  }
}

bool ipc_dl_fifo_can_push(const ipc_datalink_fifo_ctx_t *ff)
{
  return !ipc_dl_fifo_full(ff);
}

void ipc_dl_fifo_push(ipc_datalink_fifo_ctx_t *ff, ipc_hdr_t hdr, const uint8_t *pld, size_t pld_len)
{
  ipc_datalink_ctx_t *dl = ipc_dl_fifo_datalink(ff);
  ipc_context_t *ipc = ipc_dl_ipc_context(dl);
  ipc_hardware_mailbox_ctx_t *mb = &ipc->hw.mbox;
  unsigned mb_count = ipc_dl_fifo_mb_count(ff);
  unsigned mb_mask = mb_count - 1;
  TAWK_IPC_ASSERT(ipc, (mb_count & mb_mask) == 0, "mb_count not power of 2");

  TAWK_IPC_ASSERT(ipc, ipc_dl_fifo_is_up(ff), "fifo down"); /* also sanity checks pointers */
  TAWK_IPC_ASSERT(ipc, !ipc_dl_fifo_full(ff), "pushing to a full fifo");

  TAWK_IPC_VERY_VERBOSE_LOG(ipc, "%s(%p) %llx @ %x", __func__, ff, (unsigned long long)hdr, ff->wptr);

  ipc_hw_mbox_put(mb, ff->wptr & mb_mask, hdr, pld, pld_len);
  ff->wptr++;
}

void ipc_dl_fifo_enable_notify_can_push(ipc_datalink_fifo_ctx_t *ff)
{
  ff->enable_notify_can_push = true;
  if (ipc_dl_fifo_is_up(ff) &&
      ipc_dl_fifo_can_push(ff))
    ipc_dl_fifo_notify_can_push(ff);
}

void ipc_dl_fifo_disable_notify_can_push(ipc_datalink_fifo_ctx_t *ff)
{
  ff->enable_notify_can_push = false;
}

bool ipc_dl_fifo_can_pop(const ipc_datalink_fifo_ctx_t *ff)
{
  return ff->got_rptr;
}

void ipc_dl_fifo_pop(ipc_datalink_fifo_ctx_t *ff, ipc_hdr_t *hdr, const volatile uint8_t **pld)
{
  ipc_datalink_ctx_t *dl = ipc_dl_fifo_datalink(ff);
  ipc_context_t *ipc = ipc_dl_ipc_context(dl);
  TAWK_IPC_ASSERT(ipc, ipc_dl_fifo_is_up(ff), "fifo down"); /* also sanity checks pointers */
  TAWK_IPC_ASSERT_NO_MSG(ipc, ipc_dl_fifo_can_pop(ff));

  *hdr = ff->rptr_hdr;
  *pld = ff->rptr_pld;

  TAWK_IPC_VERY_VERBOSE_LOG(ipc, "%s(%p) %llx @ %x", __func__, ff, (unsigned long long)*hdr, ff->rptr);

  ff->rptr++;
  ff->got_rptr = false;
  ipc_dl_fifo_lookahead(ff);
}

void ipc_dl_fifo_pop_payload(uint8_t *dst, const volatile uint8_t *payload, size_t pld_len)
{
  ipc_hw_mbox_get_payload(dst, payload, pld_len);
}

void ipc_dl_fifo_enable_notify_can_pop(ipc_datalink_fifo_ctx_t *ff)
{
  ff->enable_notify_can_pop = true;
  if (ipc_dl_fifo_is_up(ff) &&
      ipc_dl_fifo_can_pop(ff))
    ipc_dl_fifo_notify_can_pop(ff);
}

void ipc_dl_fifo_disable_notify_can_pop(ipc_datalink_fifo_ctx_t *ff)
{
  ff->enable_notify_can_pop = false;
}


static void ipc_dl_fifo_rebalance(ipc_datalink_fifo_ctx_t *ff)
{
  ipc_datalink_ctx_t *dl = ipc_dl_fifo_datalink(ff);
  ipc_context_t *ipc = ipc_dl_ipc_context(dl);
  unsigned mb_count = ipc_dl_fifo_mb_count(ff);

  TAWK_IPC_ASSERT(ipc, ipc_dl_fifo_is_up(ff), "fifo down"); /* also sanity checks pointers */
  while (ff->rptr - ff->wptr > mb_count / 2)
    ipc_dl_fifo_push(ff, ipc_hdr_noop, NULL, 0);
}

static void ipc_dl_fifo_mbox_alert(ipc_datalink_fifo_ctx_t *ff)
{
  /////////// TODO: assert up here, mask box alert

  if (ipc_dl_fifo_is_up(ff)) {
    ipc_dl_fifo_lookahead(ff);

    if (ipc_dl_fifo_can_pop(ff) &&
        ff->enable_notify_can_pop)
      ipc_dl_fifo_notify_can_pop(ff);

    /* If stuff has been popped, there
     * may be space now to push. */
    if (ipc_dl_fifo_can_push(ff) &&
        ff->enable_notify_can_push)
      ipc_dl_fifo_notify_can_push(ff);

    /* If we've popped more than we've
     * pushed then rebalance. */
    ipc_dl_fifo_rebalance(ff);
  }
}

void ipc_hw_mbox_alert(ipc_hardware_mailbox_ctx_t *mb)
{
  ipc_hardware_ctx_t *hw = ipc_hw_mbox_hardware(mb);
  ipc_context_t *ipc = ipc_hw_ipc_context(hw);
  ipc_datalink_fifo_ctx_t *ff = &ipc->dl.fifo;

  ipc_dl_fifo_mbox_alert(ff);
}

void ipc_dl_fifo_dump(ipc_datalink_fifo_ctx_t *ff)
{
  ipc_datalink_ctx_t *dl = ipc_dl_fifo_datalink(ff);
  ipc_context_t *ipc = ipc_dl_ipc_context(dl);

  TAWK_IPC_DUMP(ipc, "dl_fifo: state = %d, rptr = %u, wptr = %u, got_rptr = %d, rptr_hdr = %016" PRIx64,
                ff->state, ff->rptr, ff->wptr, ff->got_rptr, ff->rptr_hdr);
}
