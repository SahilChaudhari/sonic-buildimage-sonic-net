// SPDX-License-Identifier: MIT or GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#ifdef __KERNEL__
#include <libc_compat.h>
#else
#include <errno.h>
#include <stdio.h>
#include <string.h>
#endif

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <tawk/ipc.h>

#include "../ipc_internal.h"
#include "../protocol.h"
#include "../datalink/datalink.h"

#include "transport.h"

#ifdef RTOS
#include "log_helper/log_helper.h"

LOG_HELPER_DECLARE(tawk, CONFIG_TAWK_IPC_LOG_LEVEL);
#else
LOG_MODULE_DECLARE(tawk, CONFIG_TAWK_IPC_LOG_LEVEL);
#endif

ipc_context_t *ipc_tp_ipc_context(ipc_transport_ctx_t *tp)
{
  /* This upcasting is ugly, but I'm not sure how this bit
   * of the code should look, so it feels fine for now. */
  return CONTAINER_OF(tp, ipc_context_t, tp);
}

ipc_datalink_ctx_t *ipc_tp_dl_context(ipc_transport_ctx_t *tp)
{
  ipc_context_t *ipc = ipc_tp_ipc_context(tp);
  return &ipc->dl;
}

static unsigned ipc_tp_buffer_idx(ipc_transport_ctx_t *tp, ipc_buffer_t *buffer, bool is_ini)
{
  return is_ini ? buffer - tp->ini.buffers : buffer - tp->tgt.buffers;
}

static void ipc_tp_dump_buffer(ipc_context_t *ipc, ipc_buffer_t *buffer, bool is_ini)
{
  ipc_hdr_t hdr = buffer->hdr;
  bool peer_local = (ipc_hdr_get_owner(hdr) == ipc_local_peer_id(ipc));

  if (buffer->state != IPC_BUFFER_FREE) {
    int type = ipc_hdr_get_type(hdr);
    TAWK_IPC_DUMP(ipc, "buf: %s idx = %d state = %d, hdr(raw = %llx, owner = %s, type = %d)",
                  is_ini ? "ini" : "tgt", ipc_tp_buffer_idx(&ipc->tp, buffer, is_ini),
                  buffer->state, hdr, peer_local ? "local" : "remote", type);
  }

  /* Expand tgt buffer. */
  switch (buffer->state) {
  case IPC_BUFFER_TGT_REQ_EMPTY:
  case IPC_BUFFER_TGT_RSP_FILL:
  case IPC_BUFFER_TGT_RSP_SEND:
    switch (ipc_hdr_get_type(hdr)) {
    case IPC_TRANSPORT_HDR_TYPE_REQ: {
      ipc_hdr_req_endpoint_t ep = ipc_hdr_get_req_endpoint(hdr);
      TAWK_IPC_DUMP(ipc, "buf: ...contains request for ep %d", ep);
    } break;
    case IPC_BUFFER_INI_RSP_TPERR: {
      ipc_hdr_tperr_errcode_t err = ipc_hdr_get_tperr_errcode(hdr);
      TAWK_IPC_DUMP(ipc, "buf: ...tp error %d", err);
    } break;
    }
    break;
  default:
    break;
  }
}

#define IPC_TP_DUMP_BUFFER_LIST_SIZE 80

static void ipc_tp_dump_buffer_list(ipc_transport_ctx_t *tp, sys_dlist_t *dlist,
                                    const char *name, bool is_ini)
{
  char buf[IPC_TP_DUMP_BUFFER_LIST_SIZE];
  char *cur = buf;
  char *end = buf + sizeof(buf);
  sys_dnode_t *node;
  unsigned n = 0;

  ipc_context_t *ipc = ipc_tp_ipc_context(tp);

  memset(buf, 0, sizeof(buf));

  SYS_DLIST_FOR_EACH_NODE(dlist, node) {
    ipc_buffer_t *buffer = CONTAINER_OF(node, ipc_buffer_t, q);

    cur += snprintf(cur, end - cur, "%s%u", n++ ? ", " : "",
                    ipc_tp_buffer_idx(tp, buffer, is_ini));
  }

  TAWK_IPC_DUMP(ipc, "%s: %s = [%s]", is_ini ? "ini" : "tgt", name, buf);
}

static void ipc_tp_dump_free_buffers(ipc_transport_ctx_t *tp, bool is_ini)
{
  char buf[IPC_TP_DUMP_BUFFER_LIST_SIZE];
  char *cur = buf;
  char *end = buf + sizeof(buf);
  unsigned n = 0;
  ipc_buffer_t *arr;
  int arr_size;

  ipc_context_t *ipc = ipc_tp_ipc_context(tp);

  if (is_ini) {
    arr = tp->ini.buffers;
    arr_size = ARRAY_SIZE(tp->ini.buffers);
  } else {
    arr = tp->tgt.buffers;
    arr_size = ARRAY_SIZE(tp->tgt.buffers);
  }

  memset(buf, 0, sizeof(buf));

  for (int b = 0; b < arr_size; b++) {
    ipc_buffer_t *buffer = &arr[b];
    if (buffer->state != IPC_BUFFER_FREE)
      continue;

    cur += snprintf(cur, end - cur, "%s%u", n++ ? ", " : "",
                    ipc_tp_buffer_idx(tp, buffer, is_ini));
  }

  TAWK_IPC_DUMP(ipc, "buf: %s idx = [%s] state = %d",
                is_ini ? "ini" : "tgt", buf, IPC_BUFFER_FREE);
}

void ipc_tp_dump(ipc_transport_ctx_t *tp)
{
  ipc_context_t *ipc = ipc_tp_ipc_context(tp);
  sys_dnode_t *node;

  bool request_pools = sys_dlist_peek_tail(&tp->ini.pools) != &tp->ini.common.q;
  bool common_pool_waiters = !sys_dlist_is_empty(&tp->ini.common.wait);

  TAWK_IPC_DUMP(ipc, "****** TP layer dump ******");
  TAWK_IPC_DUMP(ipc, "tp: link state = %d, active: request pools ? %s, "
                "common pool waiters ? %s, buffers = %d, acks = %d",
                tp->link_state, request_pools ? "y" : "n",
                common_pool_waiters? "y" : "n", tp->buffers_outstanding,
                tp->link_event_ack_outstanding);

  for (int b = 0; b < ARRAY_SIZE(tp->ini.buffers); b++)
    ipc_tp_dump_buffer(ipc, &tp->ini.buffers[b], true);

  ipc_tp_dump_free_buffers(tp, true);

  for (int b = 0; b < ARRAY_SIZE(tp->tgt.buffers); b++)
    ipc_tp_dump_buffer(ipc, &tp->tgt.buffers[b], false);

  ipc_tp_dump_free_buffers(tp, false);

  ipc_tp_dump_buffer_list(tp, &tp->ini.unused, "unused", true);
  ipc_tp_dump_buffer_list(tp, &tp->ini.avail, "avail", true);
  ipc_tp_dump_buffer_list(tp, &tp->ini.req, "req", true);
  ipc_tp_dump_buffer_list(tp, &tp->ini.remote, "remote", true);
  ipc_tp_dump_buffer_list(tp, &tp->ini.rsp, "rsp", true);

  SYS_DLIST_FOR_EACH_NODE(&tp->ini.pools, node) {
    ipc_buffer_pool_t *pool = CONTAINER_OF(node, ipc_buffer_pool_t, q);

    TAWK_IPC_DUMP(ipc, "ini: %spool:", pool == &tp->ini.common ? "common " : "");
    ipc_tp_dump_buffer_list(tp, &pool->free, ".free", true);
    ipc_tp_dump_buffer_list(tp, &pool->wait, ".wait", true);
  }

  ipc_tp_dump_buffer_list(tp, &tp->tgt.free, "free", false);
  ipc_tp_dump_buffer_list(tp, &tp->tgt.local, "local", false);
}

static ipc_buffer_t *ipc_tp_dlist_get(sys_dlist_t *dlist)
{
  sys_dnode_t *node = sys_dlist_get(dlist);
  if (node == NULL)
    return NULL;
  return CONTAINER_OF(node, ipc_buffer_t, q);
}

static void ipc_tp_dlist_append(sys_dlist_t *dlist, ipc_buffer_t *buffer)
{
  sys_dlist_append(dlist, &buffer->q);
}

static void ipc_tp_dlist_remove(sys_dlist_t *dlist, ipc_buffer_t *buffer)
{
  (void)dlist;
  sys_dlist_remove(&buffer->q);
}

static void ipc_buffer_init(ipc_buffer_t *buffer)
{
  sys_dnode_init(&buffer->q);
  sys_dnode_init(&buffer->user_q);
  buffer->state = IPC_BUFFER_FREE;
}


static void ipc_tp_timeout_work_wrap(struct k_work *w);
static void ipc_tp_timeout_timer(struct k_timer *t);
static void ipc_tp_link_down_req_work_wrap(struct k_work *w);
static void ipc_tp_link_down_req_timer(struct k_timer *t);

int ipc_tp_init(ipc_transport_ctx_t *tp)
{
  ipc_datalink_ctx_t *dl = ipc_tp_dl_context(tp);
  int rc;

  rc = ipc_tp_cfg_init(&tp->cfg);
  if (rc != 0)
    goto fail0;

  /* Deeply nasty; the payload size of runtime determined in the
   * device and datalink code, but is compile time fixed within the
   * transport code.  A future commit will the change the transport
   * code to solely apply an upper limit to the payload size.  The
   * payload size to be used over the link will be the minimum of,
   *  - local peer transport upper limit
   *  - remote peer transport upper limit
   *  - value advertise by datalink layer
   */
  if (IPC_TP_MAX_PAYLOAD_SIZE != ipc_dl_pld_size(dl)) {
    rc = EINVAL;
    goto fail1;
  }

  tp->link_state = IPC_TP_LINK_DOWN_ACK;
  tp->down_req_pending = false;

  sys_dlist_init(&tp->link_event_handlers);
  sys_dlist_init(&tp->request_handlers);

  sys_dlist_init(&tp->sendq);
  sys_dlist_init(&tp->recvq);

  sys_dlist_init(&tp->ini.unused);
  sys_dlist_init(&tp->ini.avail);
  sys_dlist_init(&tp->ini.req);
  sys_dlist_init(&tp->ini.remote);
  sys_dlist_init(&tp->ini.rsp);
  sys_dlist_init(&tp->ini.pools);
  for (int b = 0; b < ARRAY_SIZE(tp->ini.buffers); b++) {
    ipc_buffer_init(&tp->ini.buffers[b]);
    ipc_hdr_set_type(&tp->ini.buffers[b].hdr, IPC_TRANSPORT_HDR_TYPE_REQ);
    ipc_hdr_set_tag(&tp->ini.buffers[b].hdr, b);
    /* Moved to the free list after config exchange when we know how many we can use. */
    ipc_tp_dlist_append(&tp->ini.unused, &tp->ini.buffers[b]);
  }

  k_work_init(&tp->ini.timeout_work, ipc_tp_timeout_work_wrap);
  k_timer_init(&tp->ini.timeout_timer, ipc_tp_timeout_timer, NULL);

  k_work_init(&tp->link_down_req_work, ipc_tp_link_down_req_work_wrap);
  k_timer_init(&tp->link_down_req_timer, ipc_tp_link_down_req_timer, NULL);

  sys_dlist_init(&tp->tgt.free);
  sys_dlist_init(&tp->tgt.local);
  for (int b = 0; b < ARRAY_SIZE(tp->tgt.buffers); b++) {
    ipc_buffer_init(&tp->tgt.buffers[b]);
    ipc_tp_dlist_append(&tp->tgt.free, &tp->tgt.buffers[b]);
  }

  tp->buffers_outstanding = 0;

  return 0;

 fail1:
  ipc_tp_cfg_fini(&tp->cfg);

 fail0:
  return rc;
}

void ipc_tp_fini(ipc_transport_ctx_t *tp)
{
  ipc_datalink_ctx_t *dl = ipc_tp_dl_context(tp);

  k_timer_stop(&tp->ini.timeout_timer);
  k_timer_status_sync(&tp->ini.timeout_timer);

  k_work_cancel_sync(&tp->ini.timeout_work, &tp->ini.timeout_work_sync);

  k_timer_stop(&tp->link_down_req_timer);
  k_timer_status_sync(&tp->link_down_req_timer);

  k_work_cancel_sync(&tp->link_down_req_work, &tp->link_down_req_work_sync);

  if (tp->link_state == IPC_TP_LINK_UP_ACK) {
    /* If we own the datalink then we hand it back on fini.  Always
     * done with can_recv unmasked and can_send masked.  Ensure that
     * can_send is now masked. */
    ipc_dl_disable_notify_can_send(dl);
  }

  ipc_tp_cfg_fini(&tp->cfg);
}


sys_dnode_t *ipc_buffer_dnode(ipc_buffer_t *buffer)
{
  return &buffer->user_q;
}

ipc_buffer_t *ipc_buffer_from_dnode(sys_dnode_t *node)
{
  return CONTAINER_OF(node, ipc_buffer_t, user_q);
}

void *ipc_buffer_pld_ptr(ipc_buffer_t *buffer)
{
  return &buffer->pld[0];
}

size_t ipc_buffer_pld_size(ipc_buffer_t *buffer)
{
  return sizeof buffer->pld;
}

size_t ipc_buffer_get_pld_len(ipc_buffer_t *buffer)
{
  return ipc_hdr_get_pld_len(buffer->hdr);
}

void ipc_buffer_set_pld_len(ipc_buffer_t *buffer, size_t len)
{
  ipc_hdr_set_pld_len(&buffer->hdr, len);
}

uint32_t ipc_buffer_get_req_endpoint(ipc_buffer_t *buffer)
{
  return ipc_hdr_get_req_endpoint(buffer->hdr);
}

void ipc_buffer_set_req_endpoint(ipc_buffer_t *buffer, uint32_t ep)
{
  ipc_hdr_set_req_endpoint(&buffer->hdr, ep);
}

uint32_t ipc_buffer_get_rsp_errno(ipc_buffer_t *buffer)
{
  return ipc_hdr_get_rsp_errno(buffer->hdr);
}

void ipc_buffer_set_rsp_errno(ipc_buffer_t *buffer, uint32_t rsp_errno)
{
  ipc_hdr_set_rsp_errno(&buffer->hdr, rsp_errno);
}

static int ipc_hdr_tperrcode_to_errno(ipc_hdr_tperr_errcode_t errcode);

int ipc_buffer_rsp_tp_err(ipc_buffer_t *buffer)
{
  ipc_hdr_type_t hdr_type = ipc_hdr_get_type(buffer->hdr);

  switch (hdr_type) {
  case IPC_TRANSPORT_HDR_TYPE_RSP:
    return 0;

  case IPC_TRANSPORT_HDR_TYPE_TPERR: {
    ipc_hdr_tperr_errcode_t errcode = ipc_hdr_get_tperr_errcode(buffer->hdr);
    return ipc_hdr_tperrcode_to_errno(errcode);
  }

  case IPC_TRANSPORT_HDR_TYPE_NOOP:
  case IPC_TRANSPORT_HDR_TYPE_TPMSG:
  default:
    /* unreachable given validation */
    __ASSERT(0, "invalid/undefined hdr_type: %u\n", hdr_type);
    return 0;
  }
}

size_t ipc_max_ini_requests(ipc_context_t *ipc)
{
  ipc_transport_ctx_t *tp = &ipc->tp;
  return ipc_tp_cfg_max_ini(&tp->cfg);
}

size_t ipc_max_tgt_requests(ipc_context_t *ipc)
{
  ipc_transport_ctx_t *tp = &ipc->tp;
  return ipc_tp_cfg_max_tgt(&tp->cfg);
}


static ipc_buffer_t *ipc_tp_buffer_alloc(ipc_transport_ctx_t *tp, sys_dlist_t *free)
{
  ipc_context_t *ipc = ipc_tp_ipc_context(tp);
  ipc_buffer_t *buffer = ipc_tp_dlist_get(free);
  if (buffer != NULL) {
    TAWK_IPC_ASSERT(ipc, buffer->state == IPC_BUFFER_FREE,
             "invalid/undefined buffer->state: %d\n", buffer->state);
    TAWK_IPC_ASSERT(ipc, !sys_dnode_is_linked(&buffer->user_q),
             "user dnode should be unlinked when handed to user code");
    tp->buffers_outstanding++;
    buffer->state = IPC_BUFFER_ALLOC;
  }
  return buffer;
}

static void ipc_tp_link_down_maybe_ack(ipc_transport_ctx_t *tp);

static void ipc_tp_buffer_free(ipc_transport_ctx_t *tp, ipc_buffer_t *buffer, sys_dlist_t *free)
{
  ipc_context_t *ipc = ipc_tp_ipc_context(tp);
  TAWK_IPC_ASSERT(ipc, buffer->state == IPC_BUFFER_ALLOC,
           "invalid/undefined buffer->state: %d\n", buffer->state);
  TAWK_IPC_ASSERT(ipc, !sys_dnode_is_linked(&buffer->user_q),
           "user dnode should be unlinked when returned by user code");
  ipc_tp_dlist_append(free, buffer);
  buffer->state = IPC_BUFFER_FREE;
  tp->buffers_outstanding--;

  if (tp->link_state == IPC_TP_LINK_DOWN_REQ) {
    /* Back in the free pool, maybe we're quiescent now */
    ipc_tp_link_down_maybe_ack(tp);
  }
}


static void ipc_tp_buffer_build_request(ipc_transport_ctx_t *tp, ipc_buffer_t *buffer)
{
  ipc_hdr_set_type(&buffer->hdr, IPC_TRANSPORT_HDR_TYPE_REQ);
  ipc_hdr_set_pld_len(&buffer->hdr, 0);
  ipc_hdr_set_req_endpoint(&buffer->hdr, 0);
  /* tag preserved */
}

static void ipc_tp_buffer_build_response(ipc_transport_ctx_t *tp, ipc_buffer_t *buffer)
{
  ipc_hdr_set_type(&buffer->hdr, IPC_TRANSPORT_HDR_TYPE_RSP);
  ipc_hdr_set_pld_len(&buffer->hdr, 0);
  ipc_hdr_set_rsp_errno(&buffer->hdr, 0);
  /* tag preserved */
}

static void ipc_tp_build_tperr_respose(ipc_transport_ctx_t *tp, ipc_buffer_t *buffer, ipc_hdr_tperr_errcode_t errcode)
{
  ipc_hdr_set_type(&buffer->hdr, IPC_TRANSPORT_HDR_TYPE_TPERR);
  ipc_hdr_set_pld_len(&buffer->hdr, 0);
  ipc_hdr_set_tperr_errcode(&buffer->hdr, errcode);
  /* tag preserved */
}


/* Take something that was in sendq, either pass to datalink or short-circuit back into recvq. */
static void ipc_tp_do_send(ipc_transport_ctx_t *tp)
{
  ipc_context_t *ipc = ipc_tp_ipc_context(tp);
  ipc_datalink_ctx_t *dl = ipc_tp_dl_context(tp);

  ipc_buffer_t *buffer = ipc_tp_dlist_get(&tp->sendq);
  TAWK_IPC_ASSERT(ipc, buffer != NULL, "ipc_to_do_send called when sendq was empty");

  switch (tp->link_state) {
  case IPC_TP_LINK_UP_ACK: {
    size_t pld_len = ipc_hdr_get_pld_len(buffer->hdr);
    ipc_dl_send(dl, buffer->hdr, buffer->pld, pld_len);
  } break;

  case IPC_TP_LINK_DOWN_REQ:
    /* Drop on the floor.  Code below synthesises a response if needed. */
    break;

  case IPC_TP_LINK_UP_REQ:
  case IPC_TP_LINK_UP_ACK_WAIT_RDY:
  case IPC_TP_LINK_DOWN_ACK:
    TAWK_IPC_ASSERT(ipc, 0, "sendq should have drained by link state %u\n", tp->link_state);
    break;

  default:
    TAWK_IPC_ASSERT(ipc, 0, "undefined link state: %u\n", tp->link_state);
  }

  switch (buffer->state) {
  case IPC_BUFFER_INI_REQ_SEND:
    if (tp->link_state == IPC_TP_LINK_UP_ACK) {
      buffer->state = IPC_BUFFER_INI_REMOTE;
      ipc_tp_dlist_append(&tp->ini.remote, buffer);
      buffer->timeout_exp = sys_timepoint_calc(IPC_TP_TIMEOUT);
    } else if (tp->link_state == IPC_TP_LINK_DOWN_REQ) {
      /* As explained where its down, we want to empty ini.remote
       * into recvq on entry into DOWN_REQ. */
      TAWK_IPC_ASSERT(ipc, sys_dlist_is_empty(&tp->ini.remote),
               "ini.remote not empty in down_req");
      ipc_tp_build_tperr_respose(tp, buffer, IPC_TRANSPORT_HDR_TPERR_NOLINK);
      buffer->state = IPC_BUFFER_INI_RSP_TPERR;
      ipc_tp_dlist_append(&tp->recvq, buffer);
    } else {
      TAWK_IPC_ASSERT_NO_MSG(ipc, 0); /* unreachable */
    }
    break;

  case IPC_BUFFER_TGT_RSP_SEND:
    buffer->state = IPC_BUFFER_ALLOC;
    ipc_tp_buffer_free(tp, buffer, &tp->tgt.free);
    break;

  default:
    TAWK_IPC_ASSERT(ipc, 0, "invalid/undefined buffer->state: %d\n", buffer->state);
  }
}

/* ipc_tp_sendq_work must only be called when sendq is not empty.
 * This is because the caller of ipc_tp_sendq_work can, in general,
 * know implicitly whether sendq is empty or not.  It has either just
 * put work on the queue itself, or is responding to a ipc_dl_can_send
 * callback that was unmasked when the sendq wasn't empty.
 *
 * Making the caller responsible for providing this guarantee helps
 * ensure that the masking/unmasking of ipc_dl_can_send remains correct.
 * If ipc_tp_sendq_work became a nop when the sendq was empty the
 * incorrect use of ipc_dl_can_send could isntead cause subtle failures.
 */
static void ipc_tp_sendq_work(ipc_transport_ctx_t *tp)
{
  ipc_context_t *ipc = ipc_tp_ipc_context(tp);
  ipc_datalink_ctx_t *dl = ipc_tp_dl_context(tp);

  switch (tp->link_state) {
  case IPC_TP_LINK_UP_ACK:
  case IPC_TP_LINK_DOWN_REQ:
    break;

  case IPC_TP_LINK_UP_REQ:
  case IPC_TP_LINK_UP_ACK_WAIT_RDY:
  case IPC_TP_LINK_DOWN_ACK:
    TAWK_IPC_ASSERT(ipc, 0, "sendq should have drained by link state %u\n", tp->link_state);
    break;

  default:
    TAWK_IPC_ASSERT(ipc, 0, "undefined link state: %u\n", tp->link_state);
  }

  do {
    ipc_tp_do_send(tp);

    if (sys_dlist_is_empty(&tp->sendq)) {
      if (tp->link_state == IPC_TP_LINK_UP_ACK)
        ipc_dl_disable_notify_can_send(dl);
      break;
    }

  } while (tp->link_state == IPC_TP_LINK_DOWN_REQ ||
           ipc_dl_can_send(dl));
}

static void ipc_tp_sendq_push(ipc_transport_ctx_t *tp, ipc_buffer_t *buffer)
{
  ipc_context_t *ipc = ipc_tp_ipc_context(tp);
  ipc_datalink_ctx_t *dl = ipc_tp_dl_context(tp);
  bool was_empty;

  switch (tp->link_state) {
  case IPC_TP_LINK_UP_ACK:
  case IPC_TP_LINK_DOWN_REQ:
    break;

  case IPC_TP_LINK_UP_REQ:
  case IPC_TP_LINK_UP_ACK_WAIT_RDY:
  case IPC_TP_LINK_DOWN_ACK:
    TAWK_IPC_ASSERT(ipc, 0, "sendq should have drained by link state %u\n", tp->link_state);
    break;

  default:
    TAWK_IPC_ASSERT(ipc, 0, "undefined link state: %u\n", tp->link_state);
  }

  was_empty = sys_dlist_is_empty(&tp->sendq);
  ipc_tp_dlist_append(&tp->sendq, buffer);

  if (tp->link_state == IPC_TP_LINK_UP_ACK) {
    if (was_empty) {
      /* If the queue was empty then notifications will have been disabled.  Enabling
       * them will cause a call into ipc_fl_notify_can_send (either immediately or
       * when we can send) which will land is in ipc_tp_sendq_work. */
      ipc_dl_enable_notify_can_send(dl);
    }
  } else
    ipc_tp_sendq_work(tp);
}


static void ipc_tp_dispatch_request(ipc_transport_ctx_t *tp, ipc_buffer_t *buffer)
{
  ipc_context_t *ipc = ipc_tp_ipc_context(tp);

  ipc_hdr_req_endpoint_t ep = ipc_hdr_get_req_endpoint(buffer->hdr);

  sys_dnode_t *node;

  TAWK_IPC_ASSERT(ipc, buffer->state == IPC_BUFFER_TGT_REQ_EMPTY,
           "invalid/undefined buffer->state: %d\n", buffer->state);

  SYS_DLIST_FOR_EACH_NODE(&tp->request_handlers, node) {
    ipc_request_cb_t *cb = CONTAINER_OF(node, ipc_request_cb_t, q);
    if (ep == cb->endpoint) {

      ipc_assert_locked(ipc);
      ipc_block_forbid(ipc);

      cb->handler(ipc, buffer, cb->cb_ctx);

      ipc_block_permit(ipc);
      ipc_assert_locked(ipc);

      return;
    }
  }

  /* not found; send a transport error response */
  ipc_tp_build_tperr_respose(tp, buffer, IPC_TRANSPORT_HDR_TPERR_NOEP);

  buffer->state = IPC_BUFFER_TGT_RSP_FILL;
  ipc_response_send(ipc, buffer);
}

static int ipc_hdr_tperrcode_to_errno(ipc_hdr_tperr_errcode_t errcode)
{
  switch (errcode) {
  case IPC_TRANSPORT_HDR_TPERR_NOLINK: return ENOLINK;
  case IPC_TRANSPORT_HDR_TPERR_NOEP: return ENOSYS;
  default: return ENOMSG;
  }
}

static void ipc_tp_dispatch_response(ipc_transport_ctx_t *tp, ipc_buffer_t *buffer)
{
  ipc_context_t *ipc = ipc_tp_ipc_context(tp);

  TAWK_IPC_ASSERT(ipc, buffer->state == IPC_BUFFER_INI_RSP_EMPTY ||
           buffer->state == IPC_BUFFER_INI_RSP_TPERR,
           "invalid/undefined buffer->state: %d\n", buffer->state);

  if (buffer->state == IPC_BUFFER_INI_RSP_EMPTY) {
    ipc_assert_locked(ipc);
    ipc_block_forbid(ipc);

    buffer->cb(ipc, 0, buffer, buffer->cb_ctx);

    ipc_block_permit(ipc);
    ipc_assert_locked(ipc);

  } else {
    ipc_hdr_tperr_errcode_t errcode = ipc_hdr_get_tperr_errcode(buffer->hdr);
    int tp_errno = ipc_hdr_tperrcode_to_errno(errcode);

    ipc_assert_locked(ipc);
    ipc_block_forbid(ipc);

    buffer->cb(ipc, tp_errno, buffer, buffer->cb_ctx);

    ipc_block_permit(ipc);
    ipc_assert_locked(ipc);
  }
}


#define ipc_tp_fatal(tp_, fmt_, ...)                    \
  do {                                                  \
    ipc_datalink_ctx_t *dl__ = ipc_tp_dl_context(tp_);  \
    ipc_context_t *ipc__ = ipc_dl_ipc_context(dl__);    \
    TAWK_IPC_LOG_ERR(ipc__, fmt_, ##__VA_ARGS__);       \
    ipc_dl_request_reset(dl__);                         \
  } while (0)

/* Take something from datalink; process and enqueue into recvq. */
static void ipc_tp_handle_recv(ipc_transport_ctx_t *tp, ipc_hdr_t hdr, const volatile uint8_t *pld)
{
  ipc_context_t *ipc = ipc_tp_ipc_context(tp);
  ipc_hdr_type_t hdr_type;
  bool hdr_valid;

  /* Should to to cfg state machiine unless in UP_ACK */
  TAWK_IPC_ASSERT(ipc, tp->link_state == IPC_TP_LINK_UP_ACK, "Should only receive data in UP_ACK");

  hdr_valid = ipc_hdr_validate(hdr);
  if (!hdr_valid) {
    ipc_tp_fatal(tp, "header validate failed (%llx)",
                 (unsigned long long)hdr);
    return;
  }

  hdr_type = ipc_hdr_get_type(hdr);

  switch (hdr_type) {
  case IPC_TRANSPORT_HDR_TYPE_NOOP:
    /* filtered by datalink */
    TAWK_IPC_ASSERT_NO_MSG(ipc, 0);
    break;

  case IPC_TRANSPORT_HDR_TYPE_TPMSG:
    ipc_tp_fatal(tp, "transport message not expected");
    break;

  case IPC_TRANSPORT_HDR_TYPE_REQ: {
    ipc_buffer_t *buffer = NULL;

    size_t pld_len = ipc_hdr_get_pld_len(hdr);
    if (pld_len > ipc_buffer_pld_size(buffer)) {
      ipc_tp_fatal(tp, "request exceeded maximum payload size %lu > %lu",
                   pld_len, ipc_buffer_pld_size(buffer));
      break;
    }

    buffer = ipc_tp_buffer_alloc(tp, &tp->tgt.free);
    if (buffer == NULL) {
      ipc_tp_fatal(tp, "exceeded maximum target requests");
      break;
    }
    TAWK_IPC_ASSERT(ipc, buffer->state == IPC_BUFFER_ALLOC,
             "invalid/undefined buffer->state: %d\n", buffer->state);

    buffer->hdr = hdr;
    ipc_dl_recv_payload(&buffer->pld[0], pld, pld_len);

    buffer->state = IPC_BUFFER_TGT_REQ_EMPTY;
    sys_dlist_append(&tp->recvq, &buffer->q);
  } break;

  case IPC_TRANSPORT_HDR_TYPE_TPERR:
  case IPC_TRANSPORT_HDR_TYPE_RSP: {
    ipc_buffer_t *buffer;
    size_t pld_len;

    ipc_hdr_tag_t tag = ipc_hdr_get_tag(hdr);
    if (tag > ARRAY_SIZE(tp->tgt.buffers)) {
      ipc_tp_fatal(tp, "invalid tag: 0x%x", tag);
      break;
    }

    buffer = &tp->ini.buffers[tag];
    if (buffer->state != IPC_BUFFER_INI_REMOTE) {
      ipc_tp_fatal(tp, "unexpected response");
      break;
    }

    pld_len = ipc_hdr_get_pld_len(hdr);
    if (pld_len > ipc_buffer_pld_size(buffer)) {
      ipc_tp_fatal(tp, "response exceeded maximum payload size %lu > %lu",
                   pld_len, ipc_buffer_pld_size(buffer));
      break;
    }

    ipc_tp_dlist_remove(&tp->ini.remote, buffer);

    buffer->hdr = hdr;
    if (hdr_type == IPC_TRANSPORT_HDR_TYPE_TPERR) {
      /* no payload */
      buffer->state = IPC_BUFFER_INI_RSP_TPERR;
      ipc_dl_recv_payload(NULL, pld, 0);
    } else {
      ipc_dl_recv_payload(&buffer->pld[0], pld, pld_len);

      buffer->state = IPC_BUFFER_INI_RSP_EMPTY;
    }
    ipc_tp_dlist_append(&tp->recvq, buffer);
  } break;

  default:
    /* unreachable given validation */
    TAWK_IPC_ASSERT(ipc, 0, "invalid/undefined hdr_type: %u\n", hdr_type);
  }
}

static void ipc_tp_recv_work(ipc_transport_ctx_t *tp )
{
  ipc_datalink_ctx_t *dl = ipc_tp_dl_context(tp);

  do {
    ipc_hdr_t hdr;
    const volatile uint8_t *pld;
    ipc_dl_recv(dl, &hdr, &pld);

    if (tp->link_state == IPC_TP_LINK_UP_ACK)
      ipc_tp_handle_recv(tp, hdr, pld);
    else
      ipc_tp_cfg_handle_recv(&tp->cfg, hdr, pld);

  } while (ipc_dl_can_recv(dl));
}

/* Unlike ipc_tp_sendq_work/sendq, ipc_tp_recvq_work/recvq can be
 * called when recvq is empty.
 *
 * This is because the caller of ipc_tp_recvq_work cannot, in general,
 * know implicitly whether recvq is empty or not.  For example, it may
 * just have called ipc_tp_recv_work -> ipc_tp_handle_recv, but the latter
 * can discard invalid messages and not push anything to recvq.
 *
 * The corresponding concern about masking/unmasking of ipc_dl_can_recv
 * doesn't apply here as we don't mask/unmask dynamically in the
 * same way.
 */
static void ipc_tp_recvq_work(ipc_transport_ctx_t *tp) {
  ipc_context_t *ipc = ipc_tp_ipc_context(tp);
  while (!sys_dlist_is_empty(&tp->recvq)) {
    ipc_buffer_t *buffer = ipc_tp_dlist_get(&tp->recvq);

    switch (buffer->state) {
    case IPC_BUFFER_TGT_REQ_EMPTY:
      ipc_tp_dlist_append(&tp->tgt.local, buffer);
      ipc_tp_dispatch_request(tp, buffer);
      break;

    case IPC_BUFFER_INI_RSP_EMPTY:
    case IPC_BUFFER_INI_RSP_TPERR:
      ipc_tp_dlist_append(&tp->ini.rsp, buffer);
      ipc_tp_dispatch_response(tp, buffer);
      break;

    default:
      TAWK_IPC_ASSERT(ipc, 0, "invalid/undefined buffer->state: %d\n", buffer->state);
    }
  }
}

void ipc_dl_notify_can_recv(ipc_datalink_ctx_t *dl)
{
  ipc_context_t *ipc = ipc_dl_ipc_context(dl);
  ipc_transport_ctx_t *tp = &ipc->tp;

  ipc_assert_locked(ipc);

  ipc_tp_recv_work(tp);

  if (tp->link_state == IPC_TP_LINK_UP_ACK)
    ipc_tp_recvq_work(tp);

  ipc_assert_locked(ipc);
}

void ipc_dl_notify_can_send(ipc_datalink_ctx_t *dl)
{
  ipc_context_t *ipc = ipc_dl_ipc_context(dl);
  ipc_transport_ctx_t *tp = &ipc->tp;

  ipc_assert_locked(ipc);

  if (tp->link_state == IPC_TP_LINK_UP_ACK)
    ipc_tp_sendq_work(tp);
  else
    ipc_tp_cfg_notify_can_send(&tp->cfg);

  ipc_assert_locked(ipc);
}


/*
 * The transport layer decouples the transport link (as exposed via
 * the public API) from the datalink link.  This is because we want to
 * apply a datalink timeout at the datalink layer, and thus cannot
 * block receipt of messages from the datalink layer conditional on
 * processing done at transport-layer timeouts (e.g. clearing stale
 * requests etc. on link down)/
 *
 * This decoupling is done using a simple algorithm:
 *
 * On datalink link transition to DOWN:
 *  - If the transport link is in the UP_ACK state the move into the
 *    DOWN_REQ state and then isolate the datalink and transport
 *    layers:
 *     -  Receive is backpressured.  Nothing is sent.  Error responses
 *        are synthesised for in-flight requests and newly issued
 *        requests.  Responses are dropped.
 *  - Else, if the transport state is UP_REQ, flag the missed down_req
 *    transition.
 *  - Else, do nothing.
 *
 * On datalink link transition to UP:
 *  - If the transport link is in the DOWN_ACK state, move into the
 *    UP_REQ state.
 *  - Else, do nothing.
 *
 * On transport link transition to DOWN_ACK:
 *  - If the datalink link state is UP, then move into UP_REQ state.
 *  - Else do nothing.
 *
 * On transport link transition to UP_ACK:
 *  - If the missed down_req flag is set, clear it and move into
 *    DOWN_REQ.
 *  - Else, unisolate the datalink and transport link.
 *
 *
 * The astute reader will notice an issue with the above.
 * The isolation of the datalink layer until we reach UP_ACK without
 * having missed a datalink link DOWN transition causes exactly the
 * backpressure we attempt to avoid.
 *
 * For this reason, we couple the "transport config" module to the
 * datalink layer.  This changes state between UP and DOWN sychronous
 * to the datalink state, and the decoupling described above is
 * actually between the the main transport layer and the "transport
 * config" module.  This avoid the backpressure:
 * - The "transport config" modules exchange version and capability
 *   information on datalink timescales.
 * - It's invalid to send request messages after a datalink link
 *   transition to UP until after receipt of a "ready" message.  We
 *   send the "ready" message when the transport layer transitions to
 *   the UP_ACK state.  This means that any traffic other than that
 *   expected/handled by the transport config module receieve before
 *   the UP_ACK state is a protocol violation, and we therefore don't
 *   need to backpressure the datalink layer.
 *
 *
 * The timeout timer is handled similarly for efficiency reasons.
 * It's running whenever the datalink is up.  When not in the UP_ACK
 * state, pass the timer events into the transport config logic for it
 * to use to detect timeout waiting for receipt of config messages.
 * Once in the up ACK state, the timer events are instead used to
 * check for request timeout.
 */

bool ipc_link_is_up(ipc_context_t *ipc)
{
  ipc_transport_ctx_t *tp = &ipc->tp;

  ipc_assert_locked(ipc);

  switch (tp->link_state) {
  case IPC_TP_LINK_DOWN_REQ:
  case IPC_TP_LINK_DOWN_ACK:
    return false;

  case IPC_TP_LINK_UP_REQ:
  case IPC_TP_LINK_UP_ACK_WAIT_RDY:
  case IPC_TP_LINK_UP_ACK:
    return true;

  default:
    TAWK_IPC_ASSERT(ipc, 0, "undefined link_state: %d\n", tp->link_state);
    return false;
  }
}

static void ipc_tp_ini_wait_work(ipc_transport_ctx_t *tp, ipc_buffer_pool_t *pool);
static void ipc_tp_request_pool_init(ipc_transport_ctx_t *tp, ipc_buffer_pool_t *pool);
static void ipc_tp_request_pool_fini(ipc_transport_ctx_t *tp, ipc_buffer_pool_t *pool);

static void ipc_tp_link_up_req(ipc_transport_ctx_t *tp)
{
  ipc_context_t *ipc = ipc_tp_ipc_context(tp);
  sys_dnode_t *node, *node_safe;
  size_t max_ini;
  size_t cp_size;

  TAWK_IPC_ASSERT_NO_MSG(ipc, tp->link_state == IPC_TP_LINK_DOWN_ACK);
  tp->link_state = IPC_TP_LINK_UP_REQ;

  /* We've completed our config exchange, so know how many
   * ini buffers we're allowed.  Fill the avail list accordingly. */
  TAWK_IPC_ASSERT_NO_MSG(ipc, sys_dlist_is_empty(&tp->ini.avail));
  for (int b = 0; b < ipc_tp_cfg_max_ini(&tp->cfg); b++) {
    ipc_buffer_t *buffer = ipc_tp_dlist_get(&tp->ini.unused);
    TAWK_IPC_ASSERT_NO_MSG(ipc, buffer != NULL); /* we can't negotiate more buffers
                                      * than we have locally */
    ipc_tp_dlist_append(&tp->ini.avail, buffer);
  }

  /* Create the common pool and put all available buffers into it. */
  ipc_tp_request_pool_init(tp, &tp->ini.common);
  max_ini = ipc_tp_cfg_max_ini(&tp->cfg);
  cp_size = ipc_request_pool_try_resize(ipc, &tp->ini.common, max_ini);
  (void) cp_size; /* silence warnings on DEBUG builds */
  TAWK_IPC_ASSERT_NO_MSG(ipc, cp_size == max_ini); /* We're the only pool so all
                                        * request buffers are
                                        * available and this cannot fail. */

  /* We fake up an extra handler that only acks after the loop
   * so that we don't see ack_outstanding transition to 0 until
   * after every handler has been called even if event handlers
   * calls ipc_link_event_ack immediately. */
  tp->link_event_ack_outstanding = 1;
  /* _SAFE; we want to allow handlers to remove themselves */
  SYS_DLIST_FOR_EACH_NODE_SAFE(&tp->link_event_handlers, node, node_safe) {
    ipc_link_event_cb_t *cb = CONTAINER_OF(node, ipc_link_event_cb_t, q);
    if (cb->link_up_cb != NULL) {
      tp->link_event_ack_outstanding++;

      ipc_assert_locked(ipc);
      ipc_block_forbid(ipc);

      /* no use for the handle for now; longer term intent is
       * as a debug aid to find out which handlers aren't acking */
      cb->link_up_cb(ipc, NULL, cb->cb_ctx);

      ipc_block_permit(ipc);
      ipc_assert_locked(ipc);
    }
  }
  ipc_link_up_event_ack(ipc, NULL);
}

static void ipc_tp_link_down_req(ipc_transport_ctx_t *tp)
{
  ipc_context_t *ipc = ipc_tp_ipc_context(tp);
  ipc_datalink_ctx_t *dl = ipc_tp_dl_context(tp);
  sys_dnode_t *node, *node_safe;

  TAWK_IPC_ASSERT_NO_MSG(ipc, tp->link_state == IPC_TP_LINK_UP_ACK_WAIT_RDY || tp->link_state == IPC_TP_LINK_UP_ACK);

  if (tp->link_state == IPC_TP_LINK_UP_ACK) {
    /* Hand over datalink.  Always done with can_recv unmasked and
     * can_send masked.  Ensure that can_send is now masked. */
    ipc_dl_disable_notify_can_send(dl);
  }

  tp->link_state = IPC_TP_LINK_DOWN_REQ;

  /* If we cannot transition into the IPC_TP_LINK_DOWN_ACK state
   * after a reasonable amount of time, we want to report it. */
  k_timer_start(&tp->link_down_req_timer, IPC_TP_TIMEOUT, K_NO_WAIT);

  /* We're not going to get responses for any requests with the remote peer so we need
   * to fake up error responses.  We do that here, and want to do it atomically as part
   * of the entry into DOWN_REQ, before we fake up error reponses for any other requests
   * that we try to send.  This is done to ensure that the error reponses come back in
   * the same order as the requets were sent.  Whilst the transport layer doesn't provide
   * request<->response ordering by itself, layers above may provide it for their endpoints
   * and may further want to rely on that ordering.  By ensuring errors are delivered in
   * order, we want allow them to make that assumption. */
  while (!sys_dlist_is_empty(&tp->ini.remote)) {
    ipc_buffer_t *buffer = ipc_tp_dlist_get(&tp->ini.remote);
    ipc_tp_build_tperr_respose(tp, buffer, IPC_TRANSPORT_HDR_TPERR_NOLINK);
    buffer->state = IPC_BUFFER_INI_RSP_TPERR;
    ipc_tp_dlist_append(&tp->recvq, buffer);
  }

  /* When in a DOWN_REQ state, ipc_tp_sendq_work will pass the sendq
   * to ipc_tp_do_send, which will a) drop outgoing responses and
   * b) synthesise error responses for outgoing requests and push them
   * into the recvq.  We need to call ipc_tp_sendq_work ourselves as
   * it's no longer connected to ipc_dl_can_send for obvious
   * reasons. */
  if (!sys_dlist_is_empty(&tp->sendq))
    ipc_tp_sendq_work(tp);

  /* Likewise, ipc_tp_recvq_work is no longer connected to
   * ipc_dl_can_recv so we need to poke it ourselves to drain the recvq
   * into handlers. */
  ipc_tp_recvq_work(tp);


  /* report a link down to anyone waiting on buffers */
  /* _SAFE; we want to allow error handles to remove pools */
  SYS_DLIST_FOR_EACH_NODE_SAFE(&tp->ini.pools, node, node_safe) {
    ipc_buffer_pool_t *pool = CONTAINER_OF(node, ipc_buffer_pool_t, q);
    ipc_tp_ini_wait_work(tp, pool);
  }

  /* We fake up an extra handler that only acks after the loop
   * so that we don't see ack_outstanding transition to 0 until
   * after every handler has been called even if event handlers
   * calls ipc_link_event_ack immediately. */
  tp->link_event_ack_outstanding = 1;
  /* _SAFE; we want to allow handlers to remove themselves */
  SYS_DLIST_FOR_EACH_NODE_SAFE(&tp->link_event_handlers, node, node_safe) {
    ipc_link_event_cb_t *cb = CONTAINER_OF(node, ipc_link_event_cb_t, q);
    if (cb->link_down_cb != NULL) {
      tp->link_event_ack_outstanding++;

      ipc_assert_locked(ipc);
      ipc_block_forbid(ipc);

      /* no use for the handle for now; longer term intent is
       * as a debug aid to find out which handlers aren't acking */
      cb->link_down_cb(ipc, NULL, cb->cb_ctx);

      ipc_block_permit(ipc);
      ipc_assert_locked(ipc);
    }
  }
  ipc_link_down_event_ack(ipc, NULL);
}

static void ipc_tp_link_up_ack(ipc_transport_ctx_t *tp);
static void ipc_tp_link_down_ack(ipc_transport_ctx_t *tp);

static void ipc_tp_link_up_maybe_ack(ipc_transport_ctx_t *tp)
{
  ipc_context_t *ipc = ipc_tp_ipc_context(tp);
  /* We should have returned all buffers to the free state
   * and ensure noboby was waiting on buffers on link down.
   * We should not receive more data until after we've sent
   * up_ack to the datalink.  We also choose not to complete
   * async buffer allocations until after we've sent up_ack.
   * This means that there should be nothing in flight. */
  TAWK_IPC_ASSERT(ipc, sys_dlist_is_empty(&tp->sendq), "sendq not empty");
  TAWK_IPC_ASSERT(ipc, sys_dlist_is_empty(&tp->recvq), "recvq not empty");
  TAWK_IPC_ASSERT(ipc, sys_dlist_is_empty(&tp->ini.req), "ini requests in flight");
  TAWK_IPC_ASSERT(ipc, sys_dlist_is_empty(&tp->ini.remote), "ini requests in flight");
  TAWK_IPC_ASSERT(ipc, sys_dlist_is_empty(&tp->ini.rsp), "ini requests in flight");
  TAWK_IPC_ASSERT(ipc, sys_dlist_is_empty(&tp->tgt.local), "tgt requests in flight");

  if (tp->link_event_ack_outstanding != 0)
    return;

  ipc_tp_link_up_ack(tp);
}

static void ipc_tp_link_down_maybe_ack(ipc_transport_ctx_t *tp)
{
  ipc_context_t *ipc = ipc_tp_ipc_context(tp);
  /* If there's pools other than our common pool then wait until
   * they're been freed */
  if (sys_dlist_peek_tail(&tp->ini.pools) != &tp->ini.common.q)
    return;

  /* If the common pool still has waiters then wait until they're
   * done. */
  if (!sys_dlist_is_empty(&tp->ini.common.wait))
    return;

  if (tp->buffers_outstanding > 0)
    return;
  else {
    TAWK_IPC_ASSERT(ipc, sys_dlist_is_empty(&tp->sendq), "uncounted buffers in sendq");
    TAWK_IPC_ASSERT(ipc, sys_dlist_is_empty(&tp->recvq), "uncounted buffers in recvq");
    TAWK_IPC_ASSERT(ipc, sys_dlist_is_empty(&tp->ini.req), "uncounted buffers in ini.req");
    TAWK_IPC_ASSERT(ipc, sys_dlist_is_empty(&tp->ini.remote), "uncounted buffers in ini.remote");
    TAWK_IPC_ASSERT(ipc, sys_dlist_is_empty(&tp->ini.rsp), "uncounted buffers in ini.rsp");
    TAWK_IPC_ASSERT(ipc, sys_dlist_is_empty(&tp->tgt.local), "uncounted buffers in tgt.local");
  }

  if (tp->link_event_ack_outstanding != 0)
    return;

  ipc_tp_link_down_ack(tp);
}

void ipc_dl_link_up(ipc_datalink_ctx_t *dl)
{
  ipc_context_t *ipc = ipc_dl_ipc_context(dl);
  ipc_transport_ctx_t *tp = &ipc->tp;

  ipc_tp_cfg_up(&tp->cfg);

  k_timer_start(&tp->ini.timeout_timer, IPC_TP_TIMEOUT, IPC_TP_TIMEOUT);
}

void ipc_tp_cfg_done(ipc_transport_cfg_ctx_t *cfg)
{
  ipc_transport_ctx_t *tp = CONTAINER_OF(cfg, ipc_transport_ctx_t, cfg);
  ipc_context_t *ipc = ipc_tp_ipc_context(tp);

  /* Maybe signal upper layers  */
  switch (tp->link_state) {
  case IPC_TP_LINK_DOWN_ACK:
    /* Signal the ipc_dl_link_up_req now that the config
     * handshake has completed. */
    ipc_tp_link_up_req(tp);
    break;

  case IPC_TP_LINK_UP_REQ:
    /* To get here, there must have been at least one
     * call to ipc_dl_link_down_req since the previous
     * call to ipc_tp_cfg_done.  The call to
     * ipc_dl_link_down_req will have remembered that
     * link down so that ipc_tp_link_up_ack will call
     * ipc_tp_link_down_req to catch up ...  */
    __ASSERT_NO_MSG(tp->down_req_pending);
    /* ... and ipc_tp_link_down_ack will call ipc_tp_link_up_req
     * if still relevant. */
    __ASSERT_NO_MSG(ipc_tp_cfg_is_done(&tp->cfg));
    break;

  case IPC_TP_LINK_DOWN_REQ:
    /* ipc_tp_link_down_ack will call ipc_tp_link_up_req
     * if still relevant. */
    __ASSERT_NO_MSG(ipc_tp_cfg_is_done(&tp->cfg));
    break;

  case IPC_TP_LINK_UP_ACK_WAIT_RDY:
  case IPC_TP_LINK_UP_ACK:
    /* Unreachable:
     *
     * To get here, there must have been at least one
     * call to ipc_dl_link_down_req since the previous
     * call to ipc_tp_cfg_done.
     *
     *  - If that call to ipc_dl_link_down_req happened whilst
     *    we were in states IPC_TP_LINK_UP_ACK_WAIT_RDY or
     *    IPC_TP_LINK_UP_ACK, we'll have advanced out of those
     *    states to IPC_TP_LINK_DOWN_REQ.
     *  - If that call to ipc_dl_link_down_req happened whilst
     *    we were in states IPC_TP_LINK_DOWN_REQ or
     *    IPC_TP_LINK_DOWN_ACK, we'll have remained in the same
     *    state.
     *  - If that call to ipc_dl_link_down_req happened whilst
     *    we were in state IPC_TP_LINK_UP_REQ, we'll have set
     *    the down_req_pending pending flag.  We could still be
     *    in that state, or the next possible state is
     *    IPC_TP_LINK_DOWN_REQ (i.e. ipc_tp_link_up_ack atomically
     *    moves through IPC_TP_LINK_UP_ACK_WAIT_RDY).
     *
     * Thus, all possible cases are:
     *   - IPC_TP_LINK_DOWN_REQ
     *   - IPC_TP_LINK_DOWN_ACK
     *   - IPC_TP_LINK_UP_REQ, with a next state of
     *     IPC_TP_LINK_DOWN_REQ
     *
     * In all those three cases, it's only possible to get to with
     * a call to ipc_tp_cfg_done and, as above, any two such calls
     * are guaranteed to be separated by a call to ipc_dl_link_down_req. */
    __ASSERT(0, "invalid tp->link_state: %d", tp->link_state);
    break;

  default:
    TAWK_IPC_ASSERT(ipc, 0, "undefined tp->link_state: %d", tp->link_state);
  }
}

void ipc_dl_link_down(ipc_datalink_ctx_t *dl)
{
  ipc_context_t *ipc = ipc_dl_ipc_context(dl);
  ipc_transport_ctx_t *tp = &ipc->tp;

  /* Stop checking for timeouts; if we're in a state where there can
   * be stuff outstanding then ipc_tp_link_down_req will error it. */
  k_timer_stop(&tp->ini.timeout_timer);
  k_timer_status_sync(&tp->ini.timeout_timer);
  k_work_cancel_sync(&tp->ini.timeout_work, &tp->ini.timeout_work_sync);

  ipc_tp_cfg_down(&tp->cfg);

  /* Maybe signal upper layers  */
  switch (tp->link_state) {
  case IPC_TP_LINK_UP_ACK:
  case IPC_TP_LINK_UP_ACK_WAIT_RDY:
    /* Signal the ipc_dl_link_down_req. */
    ipc_tp_link_down_req(tp);
    break;

  case IPC_TP_LINK_DOWN_REQ:
  case IPC_TP_LINK_DOWN_ACK:
    /* Nothing to do */
    break;

  case IPC_TP_LINK_UP_REQ:
    /* Unlike the DOWN_REQ case in rst_up_req, we do
     * need to remember that we need another down_req.
     * This is done to ensure that any call into us
     * from the reset state machine signally down
     * is followed us signalling down to upper layers.
     * The equivalent is not needed for up transitions. */
    tp->down_req_pending = true;
    break;

  default:
    TAWK_IPC_ASSERT(ipc, 0, "undefined tp->link_state: %d", tp->link_state);
  }
}


static void ipc_tp_link_up_ack(ipc_transport_ctx_t *tp)
{
  ipc_context_t *ipc = ipc_tp_ipc_context(tp);

  TAWK_IPC_ASSERT_NO_MSG(ipc, tp->link_state == IPC_TP_LINK_UP_REQ);

  tp->link_state = IPC_TP_LINK_UP_ACK_WAIT_RDY;

  if (tp->down_req_pending) {
    tp->down_req_pending = false;
    ipc_tp_link_down_req(tp);
  } else {
    ipc_tp_cfg_local_ready(&tp->cfg);
  }
}

void ipc_tp_cfg_both_ready(ipc_transport_cfg_ctx_t *cfg)
{
  ipc_transport_ctx_t *tp = CONTAINER_OF(cfg, ipc_transport_ctx_t, cfg);
  ipc_context_t *ipc = ipc_tp_ipc_context(tp);
  sys_dnode_t *node, *node_safe;

  TAWK_IPC_ASSERT_NO_MSG(ipc, tp->link_state == IPC_TP_LINK_UP_ACK_WAIT_RDY);

  tp->link_state = IPC_TP_LINK_UP_ACK;

  /* The sendq and recvq are empty so we don't need to call
   * XXXXq_work.  The recvq is empty because it was emptied the last
   * time the link went down and tp_cfg has owned the datalink layer
   * since then.  The sendq is empty because we emptied it the last
   * time the link went down, nothing has come in to response to, and
   * we've not allocated any ini buffers since then. */
  TAWK_IPC_ASSERT(ipc, sys_dlist_is_empty(&tp->recvq), "recvq expected empty");
  TAWK_IPC_ASSERT(ipc, sys_dlist_is_empty(&tp->sendq), "sendq expected empty");

  /* Datalink handed over.  Always done with can_recv unmasked and
   * can_send masked.  Therefore, no masking to change. */

  /* Release anyone waiting on buffers */
  SYS_DLIST_FOR_EACH_NODE_SAFE(&tp->ini.pools, node, node_safe) {
    ipc_buffer_pool_t *pool = CONTAINER_OF(node, ipc_buffer_pool_t, q);
    ipc_tp_ini_wait_work(tp, pool);
  }
}

static void ipc_tp_link_down_ack(ipc_transport_ctx_t *tp)
{
  ipc_context_t *ipc = ipc_tp_ipc_context(tp);
  size_t cp_size;

  TAWK_IPC_ASSERT_NO_MSG(ipc, tp->link_state == IPC_TP_LINK_DOWN_REQ);

  /* Empty and destroy the common pool */
  cp_size = ipc_request_pool_try_resize(ipc, &tp->ini.common, 0);
  (void) cp_size; /* silence warnings on DEBUG builds */
  TAWK_IPC_ASSERT_NO_MSG(ipc, cp_size == 0); /* Be here we should be idle, so we
                                  * should have been able to empty the
                                  * pool. */
  ipc_tp_request_pool_fini(tp, &tp->ini.common);

  /* All pools should now be gone */
  TAWK_IPC_ASSERT_NO_MSG(ipc, sys_dlist_is_empty(&tp->ini.pools));

  /* Empty the avail list.  We'll refill after link up when
   * we know how many we can use. */
  while (!sys_dlist_is_empty(&tp->ini.avail)) {
    ipc_buffer_t *buffer = ipc_tp_dlist_get(&tp->ini.avail);
    ipc_tp_dlist_append(&tp->ini.unused, buffer);
  }

  tp->link_state = IPC_TP_LINK_DOWN_ACK;

  k_timer_stop(&tp->link_down_req_timer);
  k_timer_status_sync(&tp->link_down_req_timer);

  if (ipc_tp_cfg_is_done(&tp->cfg))
    ipc_tp_link_up_req(tp);
}


static void ipc_tp_timeout_work(ipc_transport_ctx_t *tp)
{
  sys_dnode_t *node;
  SYS_DLIST_FOR_EACH_NODE(&tp->ini.remote, node) {
    ipc_buffer_t *buffer = CONTAINER_OF(node, ipc_buffer_t, q);
    if (sys_timepoint_expired(buffer->timeout_exp)) {
      ipc_tp_fatal(tp, "request timeout");

      /* No point looking further */
      break;
    }
  }
}

static void ipc_tp_timeout_work_wrap(struct k_work *w)
{
  ipc_transport_ctx_t *tp = CONTAINER_OF(w, ipc_transport_ctx_t, ini.timeout_work);
  ipc_context_t *ipc = ipc_tp_ipc_context(tp);

  bool got_lock = ipc_lock_for_work(ipc, w);
  if (!got_lock)
    return;

  if (tp->link_state == IPC_TP_LINK_UP_ACK)
    ipc_tp_timeout_work(tp);
  else
    ipc_tp_cfg_timeout_work(&tp->cfg);

  ipc_unlock(ipc);
}

static void ipc_tp_timeout_timer(struct k_timer *t)
{
  ipc_transport_ctx_t *tp = CONTAINER_OF(t, ipc_transport_ctx_t, ini.timeout_timer);
  ipc_context_t *ipc = ipc_tp_ipc_context(tp);

  k_work_submit_to_queue(&ipc->workq, &tp->ini.timeout_work);
}

static void ipc_tp_link_down_req_work_wrap(struct k_work *w)
{
  ipc_transport_ctx_t *tp = CONTAINER_OF(w, ipc_transport_ctx_t, link_down_req_work);
  ipc_context_t *ipc = ipc_tp_ipc_context(tp);

  bool got_lock = ipc_lock_for_work(ipc, w);
  if (!got_lock)
    return;

  ipc_dump(ipc);

  ipc_unlock(ipc);
}

static void ipc_tp_link_down_req_timer(struct k_timer *t)
{
  ipc_transport_ctx_t *tp = CONTAINER_OF(t, ipc_transport_ctx_t, link_down_req_timer);
  ipc_context_t *ipc = ipc_tp_ipc_context(tp);

  k_work_submit_to_queue(&ipc->workq, &tp->link_down_req_work);
}

void ipc_register_link_event_handler(ipc_context_t *ipc, ipc_link_event_cb_t *cb)
{
  ipc_transport_ctx_t *tp = &ipc->tp;
  sys_dlist_append(&tp->link_event_handlers, &cb->q);
}

void ipc_deregister_link_event_handler(ipc_context_t *ipc, ipc_link_event_cb_t *cb)
{
  sys_dlist_remove(&cb->q);
}

void ipc_link_up_event_ack(ipc_context_t *ipc, ipc_link_event_handle_t handle)
{
  ipc_transport_ctx_t *tp = &ipc->tp;
  TAWK_IPC_ASSERT(ipc, tp->link_event_ack_outstanding > 0,
           "more link_up_event_ack that link_up notifications");
  tp->link_event_ack_outstanding--;
  ipc_tp_link_up_maybe_ack(tp);
}

void ipc_link_down_event_ack(ipc_context_t *ipc, ipc_link_event_handle_t handle)
{
  ipc_transport_ctx_t *tp = &ipc->tp;
  TAWK_IPC_ASSERT(ipc, tp->link_event_ack_outstanding > 0,
           "more link_down_event_ack that link_down notifications");
  tp->link_event_ack_outstanding--;
  ipc_tp_link_down_maybe_ack(tp);
}

static void ipc_tp_request_pool_init(ipc_transport_ctx_t *tp, ipc_buffer_pool_t *pool)
{
  sys_dlist_init(&pool->free);
  sys_dlist_init(&pool->wait);
  pool->size = 0;
  sys_dlist_append(&tp->ini.pools, &pool->q);
}

static void ipc_tp_request_pool_fini(ipc_transport_ctx_t *tp, ipc_buffer_pool_t *pool)
{
  ipc_context_t *ipc = ipc_tp_ipc_context(tp);

  TAWK_IPC_ASSERT(ipc, pool->size == 0, "can't finish non-empty pools");
  TAWK_IPC_ASSERT_NO_MSG(ipc, sys_dlist_is_empty(&pool->free));
  TAWK_IPC_ASSERT(ipc, sys_dlist_is_empty(&pool->wait), "can't finish a pool with waiting allocations");
  sys_dlist_remove(&pool->q);
}

/* These wrappes exist because there's some stuff
 * we don't want to do for the common pool. */
void ipc_request_pool_init(ipc_context_t *ipc, ipc_buffer_pool_t *pool)
{
  ipc_transport_ctx_t *tp = &ipc->tp;
  ipc_tp_request_pool_init(tp, pool);
}

void ipc_request_pool_fini(ipc_context_t *ipc, ipc_buffer_pool_t *pool)
{
  ipc_transport_ctx_t *tp = &ipc->tp;
  ipc_tp_request_pool_fini(tp, pool);

  if (tp->link_state == IPC_TP_LINK_DOWN_REQ) {
    /* Back in the free pool, maybe we're quiescent now */
    ipc_tp_link_down_maybe_ack(tp);
  }
}

size_t ipc_request_pool_get_size(ipc_context_t *ipc, ipc_buffer_pool_t *pool)
{
  return pool->size;
}

size_t ipc_request_pool_try_resize(ipc_context_t *ipc, ipc_buffer_pool_t *pool, size_t new_size)
{
  ipc_transport_ctx_t *tp = &ipc->tp;

  while (pool->size < new_size) {
    ipc_buffer_t *buffer = ipc_tp_dlist_get(&tp->ini.avail);
    if (buffer == NULL)
      break;
    buffer->pool = pool;
    ipc_tp_dlist_append(&pool->free, buffer);
    pool->size++;
  }

  while (pool->size > new_size) {
    ipc_buffer_t *buffer = ipc_tp_dlist_get(&pool->free);
    if (buffer == NULL)
      break;
    buffer->pool = NULL;
    ipc_tp_dlist_append(&tp->ini.avail, buffer);
    pool->size--;
  }

  /* we may have enlarged the pool, freeing up a waiter */
  ipc_tp_ini_wait_work(tp, pool);

  return pool->size;
}

size_t ipc_request_common_pool_get_size(ipc_context_t *ipc)
{
  ipc_transport_ctx_t *tp = &ipc->tp;
  return ipc_request_pool_get_size(ipc, &tp->ini.common);
}

size_t ipc_request_common_pool_try_resize(ipc_context_t *ipc, size_t new_size)
{
  ipc_transport_ctx_t *tp = &ipc->tp;
  return ipc_request_pool_try_resize(ipc, &tp->ini.common, new_size);
}

static void ipc_tp_ini_wait_work(ipc_transport_ctx_t *tp, ipc_buffer_pool_t *pool)
{
  ipc_context_t *ipc = CONTAINER_OF(tp, ipc_context_t, tp);

  ipc_assert_locked(ipc);

  while (!sys_dlist_is_empty(&pool->wait)) {
    switch (tp->link_state) {
    case IPC_TP_LINK_DOWN_ACK:
      TAWK_IPC_ASSERT(ipc, 0, "trying to allocate buffers whilst link is down");
      break;

    case IPC_TP_LINK_UP_REQ:
    case IPC_TP_LINK_UP_ACK_WAIT_RDY:
      /* wait for UP_ACK */
      return;

    case IPC_TP_LINK_UP_ACK: {
      ipc_buffer_t *buffer = ipc_request_pool_try_alloc(ipc, pool);
      sys_dnode_t *node;
      ipc_buffer_alloc_cb_t *cb;

      if (buffer == NULL)
        return;

      node = sys_dlist_get(&pool->wait);
      TAWK_IPC_ASSERT_NO_MSG(ipc, node != NULL); /* loop condition is not empty */
      cb = CONTAINER_OF(node, ipc_buffer_alloc_cb_t, q);

      ipc_assert_locked(ipc);
      ipc_block_forbid(ipc);

      cb->cb(ipc, 0, buffer, cb->cb_ctx);

      ipc_block_permit(ipc);
      ipc_assert_locked(ipc);
    } break;

    case IPC_TP_LINK_DOWN_REQ: {
      sys_dnode_t *node = sys_dlist_get(&pool->wait);
      ipc_buffer_alloc_cb_t *cb;

      TAWK_IPC_ASSERT_NO_MSG(ipc, node != NULL); /* loop condition is not empty */
      cb = CONTAINER_OF(node, ipc_buffer_alloc_cb_t, q);

      ipc_assert_locked(ipc);
      ipc_block_forbid(ipc);

      cb->cb(ipc, ENOLINK, NULL, cb->cb_ctx);

      ipc_block_permit(ipc);
      ipc_assert_locked(ipc);

      /* That might have been the last waiter on the common pool; so
         maybe we're quiescent now. */
      ipc_tp_link_down_maybe_ack(tp);
    } break;
    }
  }

  ipc_assert_locked(ipc);
}

ipc_buffer_t *ipc_request_pool_try_alloc(ipc_context_t *ipc, ipc_buffer_pool_t *pool)
{
  ipc_transport_ctx_t *tp = &ipc->tp;
  ipc_buffer_t *buffer;

  switch (tp->link_state) {
  case IPC_TP_LINK_DOWN_ACK:
    TAWK_IPC_ASSERT(ipc, 0, "trying to allocate buffers whilst link is down");
    break;

  case IPC_TP_LINK_UP_REQ:
  case IPC_TP_LINK_UP_ACK_WAIT_RDY:
  case IPC_TP_LINK_DOWN_REQ:
    return NULL;


  case IPC_TP_LINK_UP_ACK:
    break;
  }

  buffer = ipc_tp_buffer_alloc(tp, &pool->free);
  if (buffer != NULL) {
    TAWK_IPC_ASSERT(ipc, buffer->state == IPC_BUFFER_ALLOC,
             "invalid/undefined buffer->state: %d\n", buffer->state);

    buffer->state = IPC_BUFFER_INI_REQ_FILL;
    ipc_tp_buffer_build_request(tp, buffer);
    ipc_tp_dlist_append(&tp->ini.req, buffer);
  }

  return buffer;
}

void ipc_request_pool_alloc_async(ipc_context_t *ipc, ipc_buffer_pool_t *pool, ipc_buffer_alloc_cb_t *cb)
{
  ipc_transport_ctx_t *tp = &ipc->tp;

  ipc_assert_locked(ipc);
  ipc_block_forbid(ipc);

  sys_dlist_append(&pool->wait, &cb->q);
  ipc_tp_ini_wait_work(tp, pool);

  ipc_block_permit(ipc);
  ipc_assert_locked(ipc);
}

ipc_buffer_t *ipc_request_try_alloc(ipc_context_t *ipc)
{
  ipc_transport_ctx_t *tp = &ipc->tp;
  return ipc_request_pool_try_alloc(ipc, &tp->ini.common);
}

void ipc_request_alloc_async(ipc_context_t *ipc, ipc_buffer_alloc_cb_t *cb)
{
  ipc_transport_ctx_t *tp = &ipc->tp;
  ipc_request_pool_alloc_async(ipc, &tp->ini.common, cb);
}

void ipc_request_free(ipc_context_t *ipc, ipc_buffer_t *buffer)
{
  ipc_transport_ctx_t *tp = &ipc->tp;

  ipc_assert_locked(ipc);
  ipc_block_forbid(ipc);

  switch (buffer->state) {
  case IPC_BUFFER_INI_REQ_FILL:
    break;

  default:
    TAWK_IPC_ASSERT(ipc, 0, "invalid/undefined buffer->state: %d\n", buffer->state);
  }

  ipc_tp_dlist_remove(&tp->ini.req, buffer);

  buffer->state = IPC_BUFFER_ALLOC;
  ipc_tp_buffer_free(tp, buffer, &buffer->pool->free);

  if (tp->link_state == IPC_TP_LINK_UP_ACK) {
    /* Back in the free pool, allocate to a waiter if needed */
    ipc_tp_ini_wait_work(tp, buffer->pool);
  }

  ipc_block_permit(ipc);
  ipc_assert_locked(ipc);
}

void ipc_request_make_response(ipc_context_t *ipc, ipc_buffer_t *buffer)
{
  ipc_transport_ctx_t *tp = &ipc->tp;

  ipc_assert_locked(ipc);
  ipc_block_forbid(ipc);

  switch (buffer->state) {
  case IPC_BUFFER_TGT_REQ_EMPTY:
    break;

  default:
    TAWK_IPC_ASSERT(ipc, 0, "invalid/undefined buffer->state: %d\n", buffer->state);
  }

  ipc_tp_buffer_build_response(tp, buffer);

  buffer->state = IPC_BUFFER_TGT_RSP_FILL;

  ipc_block_permit(ipc);
  ipc_assert_locked(ipc);
}

void ipc_response_free(ipc_context_t *ipc, ipc_buffer_t *buffer)
{
  ipc_transport_ctx_t *tp = &ipc->tp;

  ipc_assert_locked(ipc);
  ipc_block_forbid(ipc);

  switch (buffer->state) {
  case IPC_BUFFER_INI_RSP_EMPTY:
  case IPC_BUFFER_INI_RSP_TPERR:
    break;

  default:
    TAWK_IPC_ASSERT(ipc, 0, "invalid/undefined buffer->state: %d\n", buffer->state);
  }

  ipc_tp_dlist_remove(&tp->ini.rsp, buffer);

  buffer->state = IPC_BUFFER_ALLOC;
  ipc_tp_buffer_free(tp, buffer, &buffer->pool->free);

  if (tp->link_state == IPC_TP_LINK_UP_ACK) {
    /* Back in the free pool, allocate to a waiter if needed */
    ipc_tp_ini_wait_work(tp, buffer->pool);
  }

  ipc_block_permit(ipc);
  ipc_assert_locked(ipc);
}

int ipc_response_reuse(ipc_context_t *ipc, ipc_buffer_t *buffer)
{
  ipc_transport_ctx_t *tp = &ipc->tp;

  int rc = 0xdead;

  ipc_assert_locked(ipc);
  ipc_block_forbid(ipc);

  switch (buffer->state) {
  case IPC_BUFFER_INI_RSP_EMPTY:
  case IPC_BUFFER_INI_RSP_TPERR:
    break;

  default:
    TAWK_IPC_ASSERT(ipc, 0, "invalid/undefined buffer->state: %d\n", buffer->state);
  }

  ipc_tp_dlist_remove(&tp->ini.rsp, buffer);

  switch (tp->link_state) {
  case IPC_TP_LINK_DOWN_ACK:
  case IPC_TP_LINK_UP_REQ:
  case IPC_TP_LINK_UP_ACK_WAIT_RDY:
    /* We should have freed all buffers before going into
     * DOWN_ACK, and none should have been allocated until
     * we reached UP_ACK. */
    TAWK_IPC_ASSERT(ipc, 0, "unreachable");
    break;

  case IPC_TP_LINK_UP_ACK:
    buffer->state = IPC_BUFFER_INI_REQ_FILL;
    ipc_tp_buffer_build_request(tp, buffer);
    ipc_tp_dlist_append(&tp->ini.req, buffer);
    rc = 0;
    break;

  case IPC_TP_LINK_DOWN_REQ:
    buffer->state = IPC_BUFFER_INI_RSP_TPERR;
    ipc_tp_build_tperr_respose(tp, buffer, IPC_TRANSPORT_HDR_TPERR_NOLINK);
    rc = ENOLINK;
    break;
  }

  ipc_block_permit(ipc);
  ipc_assert_locked(ipc);
  return rc;
}

void ipc_request_send_async(ipc_context_t *ipc, ipc_buffer_t *buffer, ipc_buffer_cb *rsp_cb, void *cb_ctx)
{
  ipc_transport_ctx_t *tp = &ipc->tp;

  ipc_assert_locked(ipc);
  ipc_block_forbid(ipc);

  switch (buffer->state) {
  case IPC_BUFFER_INI_REQ_FILL:
    break;

  default:
    TAWK_IPC_ASSERT(ipc, 0, "invalid/undefined buffer->state: %d\n", buffer->state);
  }

  ipc_tp_dlist_remove(&tp->ini.req, buffer);

  /* no special handling of DOWN_REQ here, leave it to sendq_work */

  buffer->cb = rsp_cb;
  buffer->cb_ctx = cb_ctx;
  buffer->state = IPC_BUFFER_INI_REQ_SEND;

  ipc_tp_sendq_push(tp, buffer);

  if (tp->link_state == IPC_TP_LINK_DOWN_REQ) {
    /* maybe came back as an error */
    ipc_tp_recvq_work(tp);
  }

  ipc_block_permit(ipc);
  ipc_assert_locked(ipc);
}

void ipc_response_send(ipc_context_t *ipc, ipc_buffer_t *buffer)
{
  ipc_transport_ctx_t *tp = &ipc->tp;

  ipc_assert_locked(ipc);
  ipc_block_forbid(ipc);

  switch (buffer->state) {
  case IPC_BUFFER_TGT_RSP_FILL:
    break;

  default:
    TAWK_IPC_ASSERT(ipc, 0, "invalid/undefined buffer->state: %d\n", buffer->state);
  }

  ipc_tp_dlist_remove(&tp->tgt.local, buffer);

  /* no special handling of DOWN_REQ here, leave it to sendq_work */

  buffer->state = IPC_BUFFER_TGT_RSP_SEND;

  ipc_tp_sendq_push(tp, buffer);

  ipc_block_permit(ipc);
  ipc_assert_locked(ipc);
}


void ipc_register_request_handler(ipc_context_t *ipc, ipc_request_cb_t *cb)
{
  ipc_transport_ctx_t *tp = &ipc->tp;

  ipc_assert_locked(ipc);
  sys_dlist_append(&tp->request_handlers, &cb->q);
}

void ipc_deregister_request_handler(ipc_context_t *ipc, ipc_request_cb_t *cb)
{
  ipc_assert_locked(ipc);
  sys_dlist_remove(&cb->q);
}


typedef struct ipc_block_on_link_up_state_s {
  bool done;
  struct k_condvar cv;
  ipc_link_event_handle_t handle;
} ipc_block_on_link_up_state_t;

static void link_up_signal(ipc_context_t *ipc, ipc_link_event_handle_t handle, void *cb_ctx)
{
  ipc_block_on_link_up_state_t *state = cb_ctx;
  state->handle = handle;
  TAWK_IPC_ASSERT_NO_MSG(ipc, !state->done);
  state->done = true;
  k_condvar_signal(&state->cv);
}

void ipc_block_on_link_up(ipc_context_t *ipc)
{
  ipc_assert_locked(ipc);
  if (!ipc_link_is_up(ipc)) {
    ipc_link_event_cb_t cb;
    ipc_block_on_link_up_state_t state;
    cb.link_up_cb = link_up_signal;
    cb.link_down_cb = NULL;
    cb.cb_ctx = &state;

    state.done = false;
    k_condvar_init(&state.cv);

    ipc_register_link_event_handler(ipc, &cb);
    if (!state.done) {
      ipc_condvar_wait(ipc, &state.cv);
      TAWK_IPC_ASSERT_NO_MSG(ipc, state.done);
    }
    ipc_link_up_event_ack(ipc, state.handle);
    ipc_deregister_link_event_handler(ipc, &cb);
  }
  ipc_assert_locked(ipc);
}


typedef struct ipc_buffer_cb_state_s {
  bool done;
  struct k_condvar cv;
  int tp_err;
  ipc_buffer_t *buffer;
} ipc_buffer_cb_state_t;

static void ipc_buffer_cb_signal(ipc_context_t *ipc, int tp_err, ipc_buffer_t *buffer, void *cb_ctx)
{
  ipc_buffer_cb_state_t *state = cb_ctx;
  state->tp_err = tp_err;
  state->buffer = buffer;
  TAWK_IPC_ASSERT_NO_MSG(ipc, !state->done);
  state->done = true;
  k_condvar_signal(&state->cv);
}

int ipc_request_pool_alloc(ipc_context_t *ipc, ipc_buffer_pool_t *pool, ipc_buffer_t **buffer)
{
  ipc_buffer_alloc_cb_t cb;
  ipc_buffer_cb_state_t state;

  ipc_assert_locked(ipc);

  cb.cb = ipc_buffer_cb_signal;
  cb.cb_ctx = &state;

  state.done = false;
  k_condvar_init(&state.cv);

  ipc_request_pool_alloc_async(ipc, pool, &cb);
  if (!state.done) {
    ipc_condvar_wait(ipc, &state.cv);
    TAWK_IPC_ASSERT_NO_MSG(ipc, state.done);
  }

  *buffer = state.buffer;

  ipc_assert_locked(ipc);
  return state.tp_err;
}

int ipc_request_alloc(ipc_context_t *ipc, ipc_buffer_t **buffer)
{
  ipc_transport_ctx_t *tp = &ipc->tp;
  return ipc_request_pool_alloc(ipc, &tp->ini.common, buffer);
}


int ipc_request_exec(ipc_context_t *ipc, ipc_buffer_t *buffer)
{
  ipc_buffer_cb_state_t state;

  ipc_assert_locked(ipc);

  state.done = false;
  k_condvar_init(&state.cv);

  ipc_request_send_async(ipc, buffer, ipc_buffer_cb_signal, &state);
  if (!state.done) {
    ipc_condvar_wait(ipc, &state.cv);
    TAWK_IPC_ASSERT_NO_MSG(ipc, state.done);
  }

  ipc_assert_locked(ipc);
  return state.tp_err;
}


extern int ipc_request(ipc_context_t *ipc,
                       uint32_t req_ep, void *req_pld, size_t req_pld_len,
                       uint32_t *rsp_errno, void *rsp_pld, size_t *rsp_pld_len)
{
  ipc_buffer_t *buffer;
  void *pld_ptr;
  size_t pld_len;
  int rc;

  rc = ipc_request_alloc(ipc, &buffer);
  if (rc != 0)
    goto out0;

  ipc_buffer_set_req_endpoint(buffer, req_ep);
  ipc_buffer_set_pld_len(buffer, req_pld_len);

  pld_ptr = ipc_buffer_pld_ptr(buffer);
  /* This could easily be optimised to avoid the double-memcpy.  We'd
   * just store the suppled req_pld pointer in ipc_buffer_t have
   * ipc_tp_do_send use that instead of ipc_buffer_t.pld.
   *
   * I'm not sure if it's worth it unless this function is heavily
   * used. */
  memcpy(pld_ptr, req_pld, req_pld_len);

  rc = ipc_request_send(ipc, buffer);
  if (rc != 0)
    goto out1;

  *rsp_errno = ipc_buffer_get_rsp_errno(buffer);

  pld_len = ipc_buffer_get_pld_len(buffer);
  if (*rsp_pld_len > pld_len)
    *rsp_pld_len = pld_len;

  pld_ptr = ipc_buffer_pld_ptr(buffer);
  /* As above, but storing the supplied rsp_pld and rsp_pld_len
   * pointer and using them in ipc_tp_handle_recv. */
  memcpy(rsp_pld, pld_ptr, *rsp_pld_len);

 out1:
  ipc_response_free(ipc, buffer);

 out0:
  return rc;
}

void ipc_request_reset(ipc_context_t *ipc)
{
  ipc_datalink_ctx_t *dl = &ipc->dl;

  ipc_assert_locked(ipc);

  ipc_dl_request_reset(dl);
}

void ipc_maybe_request_reset(ipc_context_t *ipc)
{
  ipc_datalink_ctx_t *dl = &ipc->dl;

  ipc_assert_locked(ipc);

  if (ipc_dl_link_is_up(dl)) {
    ipc_dl_request_reset(dl);
  }
}
