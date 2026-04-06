// SPDX-License-Identifier: MIT or GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "../protocol.h"
#include "../datalink/datalink.h"
#include "../ipc_internal.h"

#include "transport.h"
#include "cfgfsm.h"

#ifdef RTOS
#include "log_helper/log_helper.h"

LOG_HELPER_DECLARE(tawk, CONFIG_TAWK_IPC_LOG_LEVEL);
#else
LOG_MODULE_DECLARE(tawk, CONFIG_TAWK_IPC_LOG_LEVEL);
#endif

#define IPC_TL_CFG_STATE_RECV_NEXT(state_) ((state_) + 1)
#define IPC_TL_CFG_STATE_SEND_NEXT(state_) ((state_) + 2)
#define IPC_TL_CFG_STATE_WAIT_NEXT(state_) ((state_) + 2)

#define CHECK_RECV_NEXT(cur_, nxt_)\
  BUILD_ASSERT(IPC_TL_CFG_STATE_RECV_NEXT(IPC_TL_CFG_ ## cur_) == (IPC_TL_CFG_ ## nxt_), "")

#define CHECK_SEND_NEXT(cur_, nxt_)                                     \
  BUILD_ASSERT(IPC_TL_CFG_STATE_SEND_NEXT(IPC_TL_CFG_ ## cur_) == (IPC_TL_CFG_ ## nxt_), "")

#define CHECK_WAIT_NEXT(cur_, nxt_)                                     \
  BUILD_ASSERT(IPC_TL_CFG_STATE_WAIT_NEXT(IPC_TL_CFG_ ## cur_) == (IPC_TL_CFG_ ## nxt_), "")

CHECK_RECV_NEXT(VER_SEND_RECV, VER_SEND);
CHECK_RECV_NEXT(VER_RECV, CAPS_SEND_RECV);
CHECK_RECV_NEXT(CAPS_SEND_RECV, CAPS_SEND);
CHECK_RECV_NEXT(CAPS_RECV, READY_WAIT_RECV);
CHECK_RECV_NEXT(READY_WAIT_RECV, READY_WAIT);
CHECK_RECV_NEXT(READY_SEND_RECV, READY_SEND);
CHECK_RECV_NEXT(READY_RECV, BOTH_READY);

CHECK_SEND_NEXT(VER_SEND_RECV, VER_RECV);
CHECK_SEND_NEXT(VER_SEND, CAPS_SEND_RECV);
CHECK_SEND_NEXT(CAPS_SEND_RECV, CAPS_RECV);
CHECK_SEND_NEXT(CAPS_SEND, READY_WAIT_RECV);
CHECK_SEND_NEXT(READY_SEND_RECV, READY_RECV);
CHECK_SEND_NEXT(READY_SEND, BOTH_READY);

CHECK_WAIT_NEXT(READY_WAIT_RECV, READY_SEND_RECV);
CHECK_WAIT_NEXT(READY_WAIT, READY_SEND);


static ipc_datalink_ctx_t *ipc_tp_cfg_datalink(ipc_transport_cfg_ctx_t *cfg)
{
  /* This upcasting is ugly, but I'm not sure how this bit
   * of the code should look, so it feels fine for now. */
  ipc_transport_ctx_t *tp = CONTAINER_OF(cfg, ipc_transport_ctx_t, cfg);
  ipc_datalink_ctx_t *dl = ipc_tp_dl_context(tp);
  return dl;
}

int ipc_tp_cfg_init(ipc_transport_cfg_ctx_t *cfg)
{
  cfg->state = IPC_TL_CFG_DOWN;

  return 0;
}

void ipc_tp_cfg_fini(ipc_transport_cfg_ctx_t *cfg)
{
  ipc_datalink_ctx_t *dl = ipc_tp_cfg_datalink(cfg);

  if (cfg->state != IPC_TL_CFG_BOTH_READY) {
    /* We already owned the datalink layer.  Ensure that can_send is
     * now masked. */
    ipc_dl_disable_notify_can_send(dl);
  } else {
    /* Datalink handed over.  Always done with can_recv unmasked and
     * can_send masked. */
  }
  /* So in both cases, mask can_recv */
  ipc_dl_disable_notify_can_recv(dl);
}

void ipc_tp_cfg_up(ipc_transport_cfg_ctx_t *cfg)
{
  ipc_datalink_ctx_t *dl = ipc_tp_cfg_datalink(cfg);
  ipc_context_t *ipc = ipc_dl_ipc_context(dl);

  TAWK_IPC_ASSERT_NO_MSG(ipc, cfg->state == IPC_TL_CFG_DOWN);
  cfg->state = IPC_TL_CFG_VER_SEND_RECV;
  cfg->timeout_exp = sys_timepoint_calc(IPC_TP_TIMEOUT);

  /* We own the datalink layer. */
  ipc_dl_enable_notify_can_recv(dl);

  /* We may have advanced during enable_notify_can_recv, but are still
   * in a send state */
  TAWK_IPC_ASSERT_NO_MSG(ipc, cfg->state == IPC_TL_CFG_VER_SEND_RECV ||
                  cfg->state == IPC_TL_CFG_VER_SEND);

  ipc_dl_enable_notify_can_send(dl);
}

bool ipc_tp_cfg_is_done(ipc_transport_cfg_ctx_t *cfg)
{
  switch (cfg->state) {
  case IPC_TL_CFG_DOWN:

  case IPC_TL_CFG_VER_SEND_RECV:
  case IPC_TL_CFG_VER_SEND:
  case IPC_TL_CFG_VER_RECV:

  case IPC_TL_CFG_CAPS_SEND_RECV:
  case IPC_TL_CFG_CAPS_SEND:
  case IPC_TL_CFG_CAPS_RECV:
    return false;

  case IPC_TL_CFG_READY_WAIT_RECV:
  case IPC_TL_CFG_READY_WAIT:
  case IPC_TL_CFG_READY_SEND_RECV:
  case IPC_TL_CFG_READY_SEND:
  case IPC_TL_CFG_READY_RECV:
  case IPC_TL_CFG_BOTH_READY:
    return true;

  default:
    __ASSERT(0, "undefined config state: %u\n", cfg->state);
    return false;
  }
}

void ipc_tp_cfg_local_ready(ipc_transport_cfg_ctx_t *cfg)
{
  ipc_datalink_ctx_t *dl = ipc_tp_cfg_datalink(cfg);
  ipc_context_t *ipc = ipc_dl_ipc_context(dl);

  TAWK_IPC_ASSERT_NO_MSG(ipc, cfg->state == IPC_TL_CFG_READY_WAIT_RECV ||
                  cfg->state == IPC_TL_CFG_READY_WAIT);

  cfg->state = IPC_TL_CFG_STATE_WAIT_NEXT(cfg->state);;

  TAWK_IPC_ASSERT_NO_MSG(ipc, cfg->state == IPC_TL_CFG_READY_SEND_RECV ||
                  cfg->state == IPC_TL_CFG_READY_SEND);

  ipc_dl_enable_notify_can_send(dl);
}

void ipc_tp_cfg_down(ipc_transport_cfg_ctx_t *cfg)
{
  ipc_datalink_ctx_t *dl = ipc_tp_cfg_datalink(cfg);
  ipc_context_t *ipc = ipc_dl_ipc_context(dl);

  TAWK_IPC_ASSERT_NO_MSG(ipc, cfg->state != IPC_TL_CFG_DOWN);

  if (cfg->state != IPC_TL_CFG_BOTH_READY) {
    /* We already owned the datalink layer.  Ensure that can_send is
     * now masked. */
    ipc_dl_disable_notify_can_send(dl);
  } else {
    /* Datalink handed over.  Always done with can_recv unmasked and
     * can_send masked. */
  }
  /* So in both cases, mask can_recv */
  ipc_dl_disable_notify_can_recv(dl);

  cfg->state = IPC_TL_CFG_DOWN;
}


static bool ipc_tp_cfg_do_send_ver(ipc_transport_cfg_ctx_t *cfg)
{
  ipc_datalink_ctx_t *dl = ipc_tp_cfg_datalink(cfg);
  ipc_context_t *ipc = ipc_dl_ipc_context(dl);

  ipc_hdr_t hdr = 0;

  TAWK_IPC_ASSERT_NO_MSG(ipc, cfg->state == IPC_TL_CFG_VER_SEND_RECV ||
                  cfg->state == IPC_TL_CFG_VER_SEND);

  ipc_hdr_set_type(&hdr, IPC_TRANSPORT_HDR_TYPE_TPMSG);
  ipc_hdr_set_tpmsg_code(&hdr, IPC_TRANSPORT_HDR_TPMSG_CFG_VER);

  /* We support versions [0:0] */
  ipc_hdr_set_tpmsg_cfg_ver_min(&hdr, 0);
  ipc_hdr_set_tpmsg_cfg_ver_max(&hdr, 0);

  ipc_dl_send(dl, hdr, NULL, 0);

  cfg->state = IPC_TL_CFG_STATE_SEND_NEXT(cfg->state);

  TAWK_IPC_ASSERT_NO_MSG(ipc, cfg->state == IPC_TL_CFG_VER_RECV ||
                  cfg->state == IPC_TL_CFG_CAPS_SEND_RECV);

  if (cfg->state == IPC_TL_CFG_VER_RECV) {
    ipc_dl_disable_notify_can_send(dl);
    return false;
  } else
    return true;
}

static bool ipc_tp_cfg_do_send_caps(ipc_transport_cfg_ctx_t *cfg)
{
  ipc_datalink_ctx_t *dl = ipc_tp_cfg_datalink(cfg);
  ipc_context_t *ipc = ipc_dl_ipc_context(dl);

  ipc_hdr_t hdr = 0;

  TAWK_IPC_ASSERT_NO_MSG(ipc, cfg->state == IPC_TL_CFG_CAPS_SEND_RECV ||
                  cfg->state == IPC_TL_CFG_CAPS_SEND);

  ipc_hdr_set_type(&hdr, IPC_TRANSPORT_HDR_TYPE_TPMSG);
  ipc_hdr_set_tpmsg_code(&hdr, IPC_TRANSPORT_HDR_TPMSG_CFG_CAPS);

  ipc_hdr_set_tpmsg_cfg_caps_ini_max(&hdr, IPC_TP_MAX_INI_REQUESTS);
  ipc_hdr_set_tpmsg_cfg_caps_tgt_max(&hdr, IPC_TP_MAX_TGT_REQUESTS);

  ipc_dl_send(dl, hdr, NULL, 0);

  cfg->state = IPC_TL_CFG_STATE_SEND_NEXT(cfg->state);

  TAWK_IPC_ASSERT_NO_MSG(ipc, cfg->state == IPC_TL_CFG_CAPS_RECV ||
                  cfg->state == IPC_TL_CFG_READY_WAIT_RECV);

  ipc_dl_disable_notify_can_send(dl);

  if (cfg->state == IPC_TL_CFG_READY_WAIT_RECV)
    ipc_tp_cfg_done(cfg);

  return false;
}

static bool ipc_tp_cfg_do_send_ready(ipc_transport_cfg_ctx_t *cfg)
{
  ipc_datalink_ctx_t *dl = ipc_tp_cfg_datalink(cfg);
  ipc_context_t *ipc = ipc_dl_ipc_context(dl);

  ipc_hdr_t hdr = 0;

  TAWK_IPC_ASSERT_NO_MSG(ipc, cfg->state == IPC_TL_CFG_READY_SEND_RECV ||
                  cfg->state == IPC_TL_CFG_READY_SEND);

  ipc_hdr_set_type(&hdr, IPC_TRANSPORT_HDR_TYPE_TPMSG);
  ipc_hdr_set_tpmsg_code(&hdr, IPC_TRANSPORT_HDR_TPMSG_READY);

  ipc_hdr_clr_tpmsg_ready_rsvd0(&hdr);

  ipc_dl_send(dl, hdr, NULL, 0);

  cfg->state = IPC_TL_CFG_STATE_SEND_NEXT(cfg->state);

  TAWK_IPC_ASSERT_NO_MSG(ipc, cfg->state == IPC_TL_CFG_READY_RECV ||
                  cfg->state == IPC_TL_CFG_BOTH_READY);

  ipc_dl_disable_notify_can_send(dl);

  if (cfg->state == IPC_TL_CFG_BOTH_READY) {
    /* Hand over datalink.  Always done with can_recv unmasked and
     * can_send masked. */
    ipc_tp_cfg_both_ready(cfg);
  }

  return false;
}

/* returns true if we are still in a state whereby we need to send */
static bool ipc_tp_cfg_do_send(ipc_transport_cfg_ctx_t *cfg)
{
  ipc_datalink_ctx_t *dl = ipc_tp_cfg_datalink(cfg);
  ipc_context_t *ipc = ipc_dl_ipc_context(dl);

  switch (cfg->state) {
  case IPC_TL_CFG_DOWN: /* unreachable: shouldn't get can_send alerts
                         * whilst the link is down */
  case IPC_TL_CFG_BOTH_READY: /* unreachable: transport now owns the
                               * datalink so shouldn't call into us */
    TAWK_IPC_ASSERT(ipc, 0, "send alert unexpected state: %u\n", cfg->state);
    break;

  case IPC_TL_CFG_VER_SEND_RECV:
  case IPC_TL_CFG_VER_SEND:
    return ipc_tp_cfg_do_send_ver(cfg);

  case IPC_TL_CFG_CAPS_SEND_RECV:
  case IPC_TL_CFG_CAPS_SEND:
    return ipc_tp_cfg_do_send_caps(cfg);

  case IPC_TL_CFG_READY_SEND_RECV:
  case IPC_TL_CFG_READY_SEND:
    return ipc_tp_cfg_do_send_ready(cfg);

  case IPC_TL_CFG_VER_RECV:
  case IPC_TL_CFG_CAPS_RECV:
  case IPC_TL_CFG_READY_WAIT_RECV:
  case IPC_TL_CFG_READY_WAIT:
  case IPC_TL_CFG_READY_RECV:
    /* unreachable: send alerts should have been masked for these
     * states. */
    TAWK_IPC_ASSERT(ipc, 0, "send alert unexpected state: %u\n", cfg->state);
  }

  return false;
}

static void ipc_tp_cfg_send_work(ipc_transport_cfg_ctx_t *cfg)
{
  ipc_datalink_ctx_t *dl = ipc_tp_cfg_datalink(cfg);

  do {
    bool need_send = ipc_tp_cfg_do_send(cfg);

    if (!need_send) {
      /* For now, ipc_dl_disable_notify_can_send done in
       * do_send.  That's because it needs doing before
       * the tp_cfg_both_ready callback.
       */
      break;
    }
  } while (ipc_dl_can_send(dl));
}


#define ipc_tp_cfg_fatal(cfg_, fmt_, ...)                       \
  do {                                                          \
    ipc_datalink_ctx_t *dl__ = ipc_tp_cfg_datalink(cfg_);       \
    ipc_context_t *ipc__ = ipc_dl_ipc_context(dl__);            \
    TAWK_IPC_LOG_ERR(ipc__, fmt_, ## __VA_ARGS__);              \
    ipc_dl_request_reset(dl__);                                 \
  } while (0)

static void ipc_tp_cfg_handle_recv_ver(ipc_transport_cfg_ctx_t *cfg, ipc_hdr_t hdr)
{
  ipc_datalink_ctx_t *dl = ipc_tp_cfg_datalink(cfg);
  ipc_context_t *ipc = ipc_dl_ipc_context(dl);

  TAWK_IPC_ASSERT_NO_MSG(ipc, cfg->state == IPC_TL_CFG_VER_SEND_RECV ||
                  cfg->state == IPC_TL_CFG_VER_RECV);

  if (ipc_hdr_get_tpmsg_cfg_ver_min(hdr) > 0) {
    ipc_tp_cfg_fatal(cfg, "peer doesn't support v0 (supports [%x:%x])",
                     ipc_hdr_get_tpmsg_cfg_ver_min(hdr),
                     ipc_hdr_get_tpmsg_cfg_ver_max(hdr));
    return;
  }

  cfg->state = IPC_TL_CFG_STATE_RECV_NEXT(cfg->state);

  TAWK_IPC_ASSERT_NO_MSG(ipc, cfg->state == IPC_TL_CFG_VER_SEND ||
                  cfg->state == IPC_TL_CFG_CAPS_SEND_RECV);

  /* If send was blocked waiting on receipt of version then unblock */
  if (cfg->state == IPC_TL_CFG_CAPS_SEND_RECV)
    ipc_dl_enable_notify_can_send(dl);
}

static void ipc_tp_cfg_handle_recv_caps(ipc_transport_cfg_ctx_t *cfg, ipc_hdr_t hdr)
{
  ipc_datalink_ctx_t *dl = ipc_tp_cfg_datalink(cfg);
  ipc_context_t *ipc = ipc_dl_ipc_context(dl);

  TAWK_IPC_ASSERT_NO_MSG(ipc, cfg->state == IPC_TL_CFG_CAPS_SEND_RECV ||
                  cfg->state == IPC_TL_CFG_CAPS_RECV);

  /* Yes, ini and tgt are correct here.  The maximum number of
   * requests that we can initiate is bounded by the maximum number of
   * requests that the remote peer can handle as a target. */
  cfg->max_ini_req = MIN(IPC_TP_MAX_INI_REQUESTS,
                         ipc_hdr_get_tpmsg_cfg_caps_tgt_max(hdr));
  cfg->max_tgt_req = MIN(IPC_TP_MAX_TGT_REQUESTS,
                         ipc_hdr_get_tpmsg_cfg_caps_ini_max(hdr));

  cfg->state = IPC_TL_CFG_STATE_RECV_NEXT(cfg->state);

  TAWK_IPC_ASSERT_NO_MSG(ipc, cfg->state == IPC_TL_CFG_CAPS_SEND ||
                  cfg->state == IPC_TL_CFG_READY_WAIT_RECV);

  if (cfg->state == IPC_TL_CFG_READY_WAIT_RECV)
    ipc_tp_cfg_done(cfg);
}

static void ipc_tp_cfg_handle_recv_ready(ipc_transport_cfg_ctx_t *cfg, ipc_hdr_t hdr)
{
  ipc_datalink_ctx_t *dl = ipc_tp_cfg_datalink(cfg);
  ipc_context_t *ipc = ipc_dl_ipc_context(dl);

  TAWK_IPC_ASSERT_NO_MSG(ipc, cfg->state == IPC_TL_CFG_READY_WAIT_RECV ||
                  cfg->state == IPC_TL_CFG_READY_SEND_RECV ||
                  cfg->state == IPC_TL_CFG_READY_RECV);

  cfg->state = IPC_TL_CFG_STATE_RECV_NEXT(cfg->state);

  TAWK_IPC_ASSERT_NO_MSG(ipc, cfg->state == IPC_TL_CFG_READY_WAIT ||
                  cfg->state == IPC_TL_CFG_READY_SEND ||
                  cfg->state == IPC_TL_CFG_BOTH_READY);

  if (cfg->state == IPC_TL_CFG_BOTH_READY) {
    /* Split out so that there's somewhere for this comment.  The compiler
     * should be able to optimise this given LTO.
     *
     * If we're in READY_WAIT then the alert is already masked
     * If we're in READY_SEND then we don't want the alert masked.
     * That only leaves BOTH_READY.
     */
    ipc_dl_disable_notify_can_send(dl);
  }

  if (cfg->state == IPC_TL_CFG_BOTH_READY) {
    /* Hand over datalink.  Always done with can_recv unmasked and
     * can_send masked. */
    ipc_tp_cfg_both_ready(cfg);
  }
}

void ipc_tp_cfg_handle_recv(ipc_transport_cfg_ctx_t *cfg, ipc_hdr_t hdr, const volatile uint8_t *pld)
{
  ipc_datalink_ctx_t *dl = ipc_tp_cfg_datalink(cfg);
  ipc_context_t *ipc = ipc_dl_ipc_context(dl);
  ipc_hdr_type_t hdr_type;
  ipc_hdr_tpmsg_msgcode_t msgcode;

  bool hdr_valid = ipc_hdr_validate(hdr);
  if (!hdr_valid) {
    ipc_tp_cfg_fatal(cfg, "header validate failed (%llx)",
                     (unsigned long long)hdr);
    return;
  }

  hdr_type = ipc_hdr_get_type(hdr);

  if (hdr_type != IPC_TRANSPORT_HDR_TYPE_TPMSG)
    ipc_tp_cfg_fatal(cfg, "expected transport message, got header type %u", hdr_type);

  cfg->timeout_exp = sys_timepoint_calc(IPC_TP_TIMEOUT);

  msgcode = ipc_hdr_get_tpmsg_code(hdr);

  switch (cfg->state) {
  case IPC_TL_CFG_DOWN: /* unreachable: shouldn't get can_send alerts
                         * whilst the link is down */
  case IPC_TL_CFG_BOTH_READY: /* unreachable: transport now owns the
                               * datalink so shouldn't call into us */
    TAWK_IPC_ASSERT(ipc, 0, "attempting recv in unexpected state: %u\n", cfg->state);
    break;

  case IPC_TL_CFG_VER_SEND_RECV:
  case IPC_TL_CFG_VER_RECV:
    if (msgcode != IPC_TRANSPORT_HDR_TPMSG_CFG_VER) {
      ipc_tp_cfg_fatal(cfg, "expected version transport message, got message code %u", msgcode);
      return;
    }

    ipc_tp_cfg_handle_recv_ver(cfg, hdr);
    break;

  case IPC_TL_CFG_CAPS_SEND_RECV:
  case IPC_TL_CFG_CAPS_RECV:
    if (msgcode != IPC_TRANSPORT_HDR_TPMSG_CFG_CAPS) {
      ipc_tp_cfg_fatal(cfg, "expected capabilities transport message, got message code %u", msgcode);
      return;
    }

    ipc_tp_cfg_handle_recv_caps(cfg, hdr);
    break;

  case IPC_TL_CFG_READY_WAIT_RECV:
  case IPC_TL_CFG_READY_SEND_RECV:
  case IPC_TL_CFG_READY_RECV:
    if (msgcode != IPC_TRANSPORT_HDR_TPMSG_READY) {
      ipc_tp_cfg_fatal(cfg, "expected ready transport message, got message code %u", msgcode);
      return;
    }

    ipc_tp_cfg_handle_recv_ready(cfg, hdr);
    break;

  case IPC_TL_CFG_VER_SEND:
  case IPC_TL_CFG_CAPS_SEND:
  case IPC_TL_CFG_READY_WAIT:
  case IPC_TL_CFG_READY_SEND:
    ipc_tp_cfg_fatal(cfg, "unexpected transport message");
    return;

  }
}

void ipc_tp_cfg_notify_can_send(ipc_transport_cfg_ctx_t *cfg)
{
  ipc_tp_cfg_send_work(cfg);
}


size_t ipc_tp_cfg_max_ini(ipc_transport_cfg_ctx_t *cfg)
{
  ipc_datalink_ctx_t *dl = ipc_tp_cfg_datalink(cfg);
  ipc_context_t *ipc = ipc_dl_ipc_context(dl);

  switch (cfg->state) {
  case IPC_TL_CFG_DOWN:
  case IPC_TL_CFG_VER_SEND_RECV:
  case IPC_TL_CFG_VER_SEND:
  case IPC_TL_CFG_VER_RECV:
  case IPC_TL_CFG_CAPS_SEND_RECV:
  case IPC_TL_CFG_CAPS_SEND:
  case IPC_TL_CFG_CAPS_RECV:
    TAWK_IPC_ASSERT(ipc, 0, "capabilities not yet exchanged: %u\n", cfg->state);
    break;

  case IPC_TL_CFG_READY_WAIT_RECV:
  case IPC_TL_CFG_READY_WAIT:
  case IPC_TL_CFG_READY_SEND_RECV:
  case IPC_TL_CFG_READY_SEND:
  case IPC_TL_CFG_READY_RECV:
  case IPC_TL_CFG_BOTH_READY:
    break;
  }

  return cfg->max_ini_req;
}

size_t ipc_tp_cfg_max_tgt(ipc_transport_cfg_ctx_t *cfg)
{
  ipc_datalink_ctx_t *dl = ipc_tp_cfg_datalink(cfg);
  ipc_context_t *ipc = ipc_dl_ipc_context(dl);

  switch (cfg->state) {
  case IPC_TL_CFG_DOWN:
  case IPC_TL_CFG_VER_SEND_RECV:
  case IPC_TL_CFG_VER_SEND:
  case IPC_TL_CFG_VER_RECV:
  case IPC_TL_CFG_CAPS_SEND_RECV:
  case IPC_TL_CFG_CAPS_SEND:
  case IPC_TL_CFG_CAPS_RECV:
    TAWK_IPC_ASSERT(ipc, 0, "capabilities not yet exchanged: %u\n", cfg->state);
    break;

  case IPC_TL_CFG_READY_WAIT_RECV:
  case IPC_TL_CFG_READY_WAIT:
  case IPC_TL_CFG_READY_SEND_RECV:
  case IPC_TL_CFG_READY_SEND:
  case IPC_TL_CFG_READY_RECV:
  case IPC_TL_CFG_BOTH_READY:
    break;
  }

  return cfg->max_tgt_req;
}


void ipc_tp_cfg_timeout_work(ipc_transport_cfg_ctx_t *cfg)
{
  ipc_datalink_ctx_t *dl = ipc_tp_cfg_datalink(cfg);
  ipc_context_t *ipc = ipc_dl_ipc_context(dl);

  switch (cfg->state) {
  case IPC_TL_CFG_DOWN:
    TAWK_IPC_ASSERT(ipc, 0, "unreachable, timer should be disabled: %u\n", cfg->state);
    break;

  case IPC_TL_CFG_BOTH_READY:
    TAWK_IPC_ASSERT(ipc, 0, "unreachable, transport owns datalink: %u\n", cfg->state);
    break;

  case IPC_TL_CFG_VER_SEND_RECV:
  case IPC_TL_CFG_VER_RECV:
  case IPC_TL_CFG_CAPS_SEND_RECV:
  case IPC_TL_CFG_CAPS_RECV:
  case IPC_TL_CFG_READY_WAIT_RECV:
  case IPC_TL_CFG_READY_SEND_RECV:
  case IPC_TL_CFG_READY_RECV:
    ipc_tp_cfg_fatal(cfg, "config timeout");
    break;

  case IPC_TL_CFG_VER_SEND:
  case IPC_TL_CFG_CAPS_SEND:
  case IPC_TL_CFG_READY_WAIT:
  case IPC_TL_CFG_READY_SEND:
    /* not waiting on remote peer */
    break;
  }
}
