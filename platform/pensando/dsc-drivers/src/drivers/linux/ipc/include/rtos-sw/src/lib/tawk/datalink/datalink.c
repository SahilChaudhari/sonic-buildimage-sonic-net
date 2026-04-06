// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "../ipc_internal.h"

#include "datalink.h"

#ifdef RTOS
#include "log_helper/log_helper.h"

LOG_HELPER_DECLARE(tawk, CONFIG_TAWK_IPC_LOG_LEVEL);
#else
LOG_MODULE_DECLARE(tawk, CONFIG_TAWK_IPC_LOG_LEVEL);
#endif

ipc_context_t *ipc_dl_ipc_context(ipc_datalink_ctx_t *dl)
{
  /* This upcasting is ugly, but I'm not sure how this bit
   * of the code should look, so it feels fine for now. */
  return CONTAINER_OF(dl, ipc_context_t, dl);
}

static void ipc_dl_timeout_work(struct k_work *w);
static void ipc_dl_timeout_timer(struct k_timer *t);

int ipc_dl_init(ipc_datalink_ctx_t *dl)
{
  int rc;


  rc = ipc_dl_fifo_init(&dl->fifo);
  if (rc != 0)
    goto fail0;

  rc = ipc_dl_rst_init(&dl->rst);
  if (rc != 0)
    goto fail1;

  k_work_init(&dl->timeout_work, ipc_dl_timeout_work);
  k_timer_init(&dl->timeout_timer, ipc_dl_timeout_timer, NULL);

  return 0;

 fail1:
  ipc_dl_fifo_fini(&dl->fifo);

 fail0:
  return rc;
}

void ipc_dl_fini(ipc_datalink_ctx_t *dl)
{
  k_timer_stop(&dl->timeout_timer);
  k_timer_status_sync(&dl->timeout_timer);

  k_work_cancel_sync(&dl->timeout_work, &dl->timeout_work_sync);

  ipc_dl_rst_fini(&dl->rst);
  ipc_dl_fifo_fini(&dl->fifo);
}


size_t ipc_dl_pld_size(ipc_datalink_ctx_t *dl)
{
  return ipc_dl_fifo_pld_size(&dl->fifo);
}


void ipc_dl_rst_enter_reset(ipc_datalink_reset_ctx_t *rst)
{
  ipc_datalink_ctx_t *dl = CONTAINER_OF(rst, ipc_datalink_ctx_t, rst);

  TAWK_IPC_LOG_DBG(ipc_dl_ipc_context(dl), "%s(%p)", __func__, dl);
  /* Put the FIFO into reset */
  ipc_dl_fifo_enter_reset(&dl->fifo);
}

/* As explained in datalink_rst.h, we get synchronous/posted type
 * notifications that we have to handle before returning, and we
 * expose the same interface up to the transport layer.  This makes
 * the following glue code trivial.  Should we change to asychronous
 * notifications, this code will become less trivial as it'll need to
 * sequence the req/ack handshake first through the fifo then through
 * the upper layers for down, and vice-versa for up. */
void ipc_dl_rst_exit_reset(ipc_datalink_reset_ctx_t *rst)
{
  ipc_datalink_ctx_t *dl = CONTAINER_OF(rst, ipc_datalink_ctx_t, rst);

  TAWK_IPC_LOG_DBG(ipc_dl_ipc_context(dl), "%s(%p)", __func__, dl);

  /* Take the FIFO out of reset */
  ipc_dl_fifo_exit_reset(&dl->fifo);
}

void ipc_dl_rst_down(ipc_datalink_reset_ctx_t *rst)
{
  ipc_datalink_ctx_t *dl = CONTAINER_OF(rst, ipc_datalink_ctx_t, rst);

  TAWK_IPC_LOG_DBG(ipc_dl_ipc_context(dl), "%s(%p)", __func__, dl);

  /* Signal upper layers. */
  ipc_dl_link_down(dl);

  /* Take the FIFO down.  Do it after
   * we've signalled upper layers. */
  k_timer_stop(&dl->timeout_timer);
  k_timer_status_sync(&dl->timeout_timer);
  k_work_cancel_sync(&dl->timeout_work, &dl->timeout_work_sync);
  ipc_dl_fifo_down(&dl->fifo);
}

void ipc_dl_rst_up(ipc_datalink_reset_ctx_t *rst)
{
  ipc_datalink_ctx_t *dl = CONTAINER_OF(rst, ipc_datalink_ctx_t, rst);

  TAWK_IPC_LOG_DBG(ipc_dl_ipc_context(dl), "%s(%p)", __func__, dl);

  /* Bring the FIFO up.  Do it before
   * we've signalled upper layers. */
  dl->timeout_exp = sys_timepoint_calc(IPC_DL_TIMEOUT);
  dl->keepalive_exp = sys_timepoint_calc(IPC_DL_KEEPALIVE_INTERVAL);
  ipc_dl_fifo_up(&dl->fifo);
  k_timer_start(&dl->timeout_timer, IPC_DL_TIMEOUT, IPC_DL_TIMEOUT);

  /* Signal upper layers. */
  ipc_dl_link_up(dl);
}

bool ipc_dl_link_is_up(ipc_datalink_ctx_t *dl)
{
  return ipc_dl_rst_in_up(&dl->rst);
}

void ipc_dl_request_reset(ipc_datalink_ctx_t *dl)
{
  ipc_dl_rst_request_reset(&dl->rst);
}

bool ipc_dl_can_send(ipc_datalink_ctx_t *dl)
{
  return ipc_dl_fifo_can_push(&dl->fifo);
}

void ipc_dl_send(ipc_datalink_ctx_t *dl, ipc_hdr_t hdr, const uint8_t *payload, size_t payload_len)
{
  TAWK_IPC_VERBOSE_LOG(ipc_dl_ipc_context(dl), "%s(%p): %016llx", __func__, dl, (unsigned long long)hdr);
  TAWK_IPC_VERY_VERBOSE_HEXDUMP(ipc_dl_ipc_context(dl), payload, payload_len < 16 ? payload_len : 16, "payload start: ");
  ipc_dl_fifo_push(&dl->fifo, hdr, payload, payload_len);
  dl->timeout_exp = sys_timepoint_calc(IPC_DL_TIMEOUT);
  dl->keepalive_exp = sys_timepoint_calc(IPC_DL_KEEPALIVE_INTERVAL);
}

void ipc_dl_enable_notify_can_send(ipc_datalink_ctx_t *dl)
{
  TAWK_IPC_VERY_VERBOSE_LOG(ipc_dl_ipc_context(dl), "%s(%p)", __func__, dl);
  ipc_dl_fifo_enable_notify_can_push(&dl->fifo);
}

void ipc_dl_disable_notify_can_send(ipc_datalink_ctx_t *dl)
{
  TAWK_IPC_VERY_VERBOSE_LOG(ipc_dl_ipc_context(dl), "%s(%p)", __func__, dl);
  ipc_dl_fifo_disable_notify_can_push(&dl->fifo);
}

bool ipc_dl_can_recv(ipc_datalink_ctx_t *dl)
{
  return ipc_dl_fifo_can_pop(&dl->fifo);
}

void ipc_dl_recv(ipc_datalink_ctx_t *dl, ipc_hdr_t *hdr, const volatile uint8_t **payload)
{
  TAWK_IPC_VERBOSE_LOG(ipc_dl_ipc_context(dl), "%s(%p): %016llx", __func__, dl, (unsigned long long)*hdr);
  ipc_dl_fifo_pop(&dl->fifo, hdr, payload);
  TAWK_IPC_VERY_VERBOSE_HEXDUMP(ipc_dl_ipc_context(dl), (const uint8_t *)*payload, 16, "payload start: ");
}

void ipc_dl_recv_payload(uint8_t *dst, const volatile uint8_t *payload, size_t pld_len)
{
  ipc_dl_fifo_pop_payload(dst, payload, pld_len);
}

void ipc_dl_enable_notify_can_recv(ipc_datalink_ctx_t *dl)
{
  TAWK_IPC_VERY_VERBOSE_LOG(ipc_dl_ipc_context(dl), "%s(%p)", __func__, dl);
  ipc_dl_fifo_enable_notify_can_pop(&dl->fifo);
}

void ipc_dl_disable_notify_can_recv(ipc_datalink_ctx_t *dl)
{
  TAWK_IPC_VERY_VERBOSE_LOG(ipc_dl_ipc_context(dl), "%s(%p)", __func__, dl);
  ipc_dl_fifo_disable_notify_can_pop(&dl->fifo);
}

void ipc_dl_fifo_notify_can_pop(ipc_datalink_fifo_ctx_t *ff)
{
  ipc_datalink_ctx_t *dl = CONTAINER_OF(ff, ipc_datalink_ctx_t, fifo);
  ipc_context_t *ipc = ipc_dl_ipc_context(dl);
  TAWK_IPC_ASSERT(ipc, ipc_dl_rst_in_up(&dl->rst),
           "fifo should only report work when up");

  ipc_dl_notify_can_recv(dl); /* prod the transport layer */
}

void ipc_dl_fifo_notify_can_push(ipc_datalink_fifo_ctx_t *ff)
{
  ipc_datalink_ctx_t *dl = CONTAINER_OF(ff, ipc_datalink_ctx_t, fifo);
  ipc_context_t *ipc = ipc_dl_ipc_context(dl);
  TAWK_IPC_ASSERT(ipc, ipc_dl_rst_in_up(&dl->rst),
           "fifo should only report work when up");

  ipc_dl_notify_can_send(dl); /* prod the transport layer */
}


#define ipc_dl_fatal(dl_, fmt_, ...)                    \
  do {                                                  \
    ipc_context_t *ipc__ = ipc_dl_ipc_context(dl_);     \
    TAWK_IPC_LOG_ERR(ipc__, fmt_, ## __VA_ARGS__);      \
    if (IS_ENABLED(CONFIG_TAWK_IPC_DUMP_ON_ERROR)) {    \
      ipc_dump(ipc__);                                  \
    }                                                   \
    ipc_dl_request_reset(dl_);                          \
  } while (0)

static void ipc_dl_timeout_work(struct k_work *w)
{
  ipc_datalink_ctx_t *dl = CONTAINER_OF(w, ipc_datalink_ctx_t, timeout_work);
  ipc_context_t *ipc = ipc_dl_ipc_context(dl);

  bool got_lock = ipc_lock_for_work(ipc, w);
  if (!got_lock)
    return;

  if (!ipc_dl_can_send(dl)) {
    if (sys_timepoint_expired(dl->timeout_exp))
      ipc_dl_fatal(dl, "datalink timeout");
  } else if (sys_timepoint_expired(dl->keepalive_exp)) {
    ipc_dl_send(dl, ipc_hdr_noop, NULL, 0);
    /* dl_send also bumps keepalive_exp */
  }

  ipc_unlock(ipc);
}

static void ipc_dl_timeout_timer(struct k_timer *t)
{
  ipc_datalink_ctx_t *dl = CONTAINER_OF(t, ipc_datalink_ctx_t, timeout_timer);
  ipc_context_t *ipc = ipc_dl_ipc_context(dl);

  k_work_submit_to_queue(&ipc->workq, &dl->timeout_work);
}

void ipc_dl_dump(ipc_datalink_ctx_t *dl)
{
  TAWK_IPC_DUMP(ipc_dl_ipc_context(dl), "****** DL layer dump ******");
  ipc_dl_fifo_dump(&dl->fifo);
  ipc_dl_rst_dump(&dl->rst);
}
