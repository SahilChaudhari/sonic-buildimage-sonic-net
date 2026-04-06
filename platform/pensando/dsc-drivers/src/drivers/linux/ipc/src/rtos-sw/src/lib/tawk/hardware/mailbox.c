// SPDX-License-Identifier: MIT or GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#ifdef __KERNEL__
#include <libc_compat.h>
#else
#include <string.h>
#endif

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include "../ipc_internal.h"
#include "hardware.h"
#include "mailbox.h"

#ifdef RTOS
#include "log_helper/log_helper.h"

LOG_HELPER_DECLARE(tawk, CONFIG_TAWK_IPC_LOG_LEVEL);
#else
LOG_MODULE_DECLARE(tawk, CONFIG_TAWK_IPC_LOG_LEVEL);
#endif

static void ipc_hw_mbox_work(struct k_work *w);


static void memset_buf(volatile uint8_t *dst, int c, size_t len)
{
  while (len--)
    *dst++ = c;
}

static void memcpy_to_buf(volatile uint8_t *dst, const uint8_t *src, size_t len)
{
  while (len--)
    *dst++ = *src++;
}

static void memcpy_from_buf(uint8_t *dst, const volatile uint8_t *src, size_t len)
{
  while (len--)
    *dst++ = *src++;
}


ipc_hardware_ctx_t *ipc_hw_mbox_hardware(ipc_hardware_mailbox_ctx_t *mb)
{
  /* This upcasting is ugly, but I'm not sure how this bit
   * of the code should look, so it feels fine for now. */
  return CONTAINER_OF(mb, ipc_hardware_ctx_t, mbox);
}

#ifdef TAWK_IPC_PROFILE

#define TAWK_IPC_NEW_MAX_THRESHOLD K_MSEC(200)

static void on_new_active_prof_max(void *ctx)
{
  ipc_hardware_mailbox_ctx_t *mb = ctx;
  ipc_hardware_ctx_t *hw = ipc_hw_mbox_hardware(mb);
  ipc_context_t *ipc = ipc_hw_ipc_context(hw);

  if (mb->active_prof.max.delta >= TAWK_IPC_NEW_MAX_THRESHOLD)
#ifdef IPC_BUILD_KERNEL_MODULE
    TAWK_IPC_LOG_WRN(ipc, "Mailbox work took %lluus to run on core %d (New max)",
                     mb->active_prof.max.delta / 1000,
                     mb->active_prof.max.cpu);
#else
    TAWK_IPC_LOG_WRN(ipc, "Mailbox work took %lluus to run (New max)",
                     mb->active_prof.max.delta / 1000);
#endif
}

static void on_new_idle_prof_max(void *ctx)
{
  ipc_hardware_mailbox_ctx_t *mb = ctx;
  ipc_hardware_ctx_t *hw = ipc_hw_mbox_hardware(mb);
  ipc_context_t *ipc = ipc_hw_ipc_context(hw);

  if (mb->idle_prof.max.delta >= TAWK_IPC_NEW_MAX_THRESHOLD)
    TAWK_IPC_LOG_WRN(ipc, "Mailbox work was idle for too long %lluus (New max)",
                     mb->idle_prof.max.delta / 1000);
}

static void on_new_lock_prof_max(void *ctx)
{
  ipc_hardware_mailbox_ctx_t *mb = ctx;
  ipc_hardware_ctx_t *hw = ipc_hw_mbox_hardware(mb);
  ipc_context_t *ipc = ipc_hw_ipc_context(hw);

  if (mb->lock_prof.max.delta >= TAWK_IPC_NEW_MAX_THRESHOLD)
    TAWK_IPC_LOG_WRN(ipc, "Mailbox work was locked for too long %lluus (New max)",
                     mb->lock_prof.max.delta / 1000);
}
#endif

int ipc_hw_mbox_init(ipc_hardware_mailbox_ctx_t *mb,
                     bool own_mem,
                     unsigned mbox_count,
                     mm_reg_t control_base,
                     unsigned control_stride_log2,
#ifdef CONFIG_TAWK_IPC_CACHE_MANAGEMENT
                     bool buffer_cached,
#endif
                     mm_reg_t buffer_base,
                     unsigned buffer_stride_log2,
                     size_t buffer_size)
{
  k_work_init(&mb->work, ipc_hw_mbox_work);


  mb->own_mem = own_mem;

  if ((mbox_count & (mbox_count - 1)) != 0)
    return EINVAL;
  mb->mailbox_count = mbox_count;

  mb->control_base = control_base;
  mb->control_stride_log2 = control_stride_log2;

#ifdef CONFIG_TAWK_IPC_CACHE_MANAGEMENT
  mb->buffer_cached = buffer_cached;
#endif
  mb->buffer_base = buffer_base;
  mb->buffer_stride_log2 = buffer_stride_log2;
  mb->buffer_size = buffer_size;

#ifdef TAWK_IPC_PROFILE
  ipc_prof_init(&mb->active_prof, on_new_active_prof_max, mb);
  ipc_prof_init(&mb->idle_prof, on_new_idle_prof_max, mb);
  ipc_prof_init(&mb->lock_prof, on_new_lock_prof_max, mb);

  ipc_prof_begin(&mb->idle_prof);
#endif

  return 0;
}

static void ipc_hw_mbox_deinit(ipc_hardware_mailbox_ctx_t *mb)
{
  k_work_cancel_sync(&mb->work, &mb->work_sync);

#ifdef TAWK_IPC_PROFILE
  ipc_prof_end(&mb->idle_prof);
#endif
}

void ipc_hw_mbox_quarantine(ipc_hardware_mailbox_ctx_t *mb)
{
  ipc_hw_mbox_deinit(mb);

  memset(mb->quarantine, 0xff, sizeof(mb->quarantine));
  mb->mailbox_count = TAWK_IPC_QUARANTINE_MBOX_CNT;

  mb->control_base = (mm_reg_t)&mb->quarantine[TAWK_IPC_QUARANTINE_MBOX_CTL_BASE];
  mb->control_stride_log2 = TAWK_IPC_QUARANTINE_MBOX_CTL_STRIDE_LOG2;

  mb->buffer_base = (mm_reg_t)&mb->quarantine[TAWK_IPC_QUARANTINE_MBOX_BUF_BASE];
  mb->buffer_stride_log2 = TAWK_IPC_QUARANTINE_MBOX_BUF_STRIDE_LOG2;
  mb->buffer_size = TAWK_IPC_QUARANTINE_MBOX_BUF_SZ;
}

void ipc_hw_mbox_fini(ipc_hardware_mailbox_ctx_t *mb)
{
  ipc_hw_mbox_deinit(mb);
}


/* Guaranteed to be a power of 2 */
unsigned ipc_hw_mbox_count(ipc_hardware_mailbox_ctx_t *mb)
{
  return mb->mailbox_count;
}

size_t ipc_hw_mbox_buffer_size(ipc_hardware_mailbox_ctx_t *mb)
{
  return mb->buffer_size;
}


static inline mm_reg_t ipc_hw_mbox_control_reg(ipc_hardware_mailbox_ctx_t *mb, unsigned mbox_idx)
{
  return mb->control_base + ((size_t)mbox_idx << mb->control_stride_log2);
}

static uint64_t ipc_hw_mbox_control_read(ipc_hardware_mailbox_ctx_t *mb, unsigned mbox_idx)
{
  ipc_hardware_ctx_t *hw = ipc_hw_mbox_hardware(mb);
  ipc_context_t *ipc = ipc_hw_ipc_context(hw);
  mm_reg_t addr;
  uint64_t ctl;

  ARG_UNUSED(ipc); /* if TAWK_IPC_VERBOSE_LOG expands to nothing this is unused */

  addr = ipc_hw_mbox_control_reg(mb, mbox_idx);
  ctl = sys_read64(addr);
  ctl = sys_le64_to_cpu(ctl);

  TAWK_IPC_VERY_VERBOSE_LOG(ipc, "%s(%p/%u) %u %llx", __func__, mb, ipc_local_peer_id(ipc), mbox_idx, (unsigned long long)ctl);

  return ctl;
}

static void ipc_hw_mbox_control_write(ipc_hardware_mailbox_ctx_t *mb, unsigned mbox_idx, uint64_t ctl)
{
  ipc_hardware_ctx_t *hw = ipc_hw_mbox_hardware(mb);
  ipc_context_t *ipc = ipc_hw_ipc_context(hw);
  mm_reg_t addr;

  ARG_UNUSED(ipc); /* if TAWK_IPC_VERBOSE_LOG expands to nothing this is unused */

  addr = ipc_hw_mbox_control_reg(mb, mbox_idx);

  TAWK_IPC_VERY_VERBOSE_LOG(ipc, "%s(%p/%u) %u %llx", __func__, mb, ipc_local_peer_id(ipc), mbox_idx, (unsigned long long)ctl);

  sys_write64(sys_cpu_to_le64(ctl), addr);
}


static inline volatile void *ipc_hw_mbox_buffer(ipc_hardware_mailbox_ctx_t *mb, unsigned mbox_idx)
{
  return (void *)(mb->buffer_base + ((size_t)mbox_idx << mb->buffer_stride_log2));
}


void ipc_hw_mbox_scrub(ipc_hardware_mailbox_ctx_t *mb, unsigned mbox_idx, bool own_mbox)
{
  ipc_hardware_ctx_t *hw = ipc_hw_mbox_hardware(mb);
  ipc_context_t *ipc = ipc_hw_ipc_context(hw);
  volatile void *buffer = ipc_hw_mbox_buffer(mb, mbox_idx);

  if (mb->own_mem) {
    ipc_hdr_t hdr = ipc_hdr_noop;

    ipc_hdr_set_owner(&hdr, own_mbox ? ipc_local_peer_id(ipc) : ipc_remote_peer_id(ipc));

    TAWK_IPC_VERY_VERBOSE_LOG(ipc, "%s(%p/%u) %u %llx", __func__, mb, ipc_local_peer_id(ipc), mbox_idx,
                              (unsigned long long)hdr);

    ipc_hw_mbox_control_write(mb, mbox_idx, hdr);
    memset_buf(buffer, 0x1c, mb->buffer_size);
  } else {
    TAWK_IPC_VERY_VERBOSE_LOG(ipc, "%s(%p/%u) %u ---", __func__, mb, ipc_local_peer_id(ipc), mbox_idx);
  }

#ifdef CONFIG_TAWK_IPC_CACHE_MANAGEMENT
  if (mb->buffer_cached) {
    if (mb->own_mem) {
      if (owned)
        sys_cache_data_flush_range(buffer, mb->buffer_size);
      else
        sys_cache_data_flush_and_invd_range(buffer, mb->buffer_size);
    } else {
      sys_cache_data_invd_range(buffer, mb->buffer_size);
    }
  }
#endif

}

bool ipc_hw_mbox_get(ipc_hardware_mailbox_ctx_t *mb, unsigned mbox_idx, ipc_hdr_t *hdr, const volatile uint8_t **payload)
{
  ipc_hardware_ctx_t *hw = ipc_hw_mbox_hardware(mb);
  ipc_context_t *ipc = ipc_hw_ipc_context(hw);

  *hdr = ipc_hw_mbox_control_read(mb, mbox_idx);
  if (ipc_hdr_get_owner(*hdr) == ipc_local_peer_id(ipc)) {
    TAWK_IPC_VERY_VERBOSE_LOG(ipc, "%s(%p/%u) %u %llx", __func__, mb, ipc_local_peer_id(ipc), mbox_idx,
                              (unsigned long long)*hdr);
    /* sys_read64 provided an rmb */
    *payload = ipc_hw_mbox_buffer(mb, mbox_idx);
    return true;
  }

  return false;
}

void ipc_hw_mbox_get_payload(uint8_t *dst, const volatile uint8_t *payload, size_t pld_len)
{
  memcpy_from_buf(dst, payload, pld_len);
}

void ipc_hw_mbox_put(ipc_hardware_mailbox_ctx_t *mb, unsigned mbox_idx, ipc_hdr_t hdr, const uint8_t *payload, size_t payload_len)
{
  ipc_hardware_ctx_t *hw = ipc_hw_mbox_hardware(mb);
  ipc_context_t *ipc = ipc_hw_ipc_context(hw);

  if (payload_len > 0) {
    volatile uint8_t *buffer = ipc_hw_mbox_buffer(mb, mbox_idx);
    memcpy_to_buf(buffer, payload, payload_len);
#ifdef CONFIG_TAWK_IPC_CACHE_MANAGEMENT
    if (mb->buffer_cached) {
      ipc_hw_mbox_mem_flush_and_invd_pld(mb, buffer, payload_len);
    }
#endif
  }

  TAWK_IPC_VERY_VERBOSE_LOG(ipc, "%s(%p/%u) %u %llx", __func__, mb, ipc_local_peer_id(ipc), mbox_idx, (unsigned long long)hdr);

  ipc_hdr_set_owner(&hdr, ipc_remote_peer_id(ipc));

  /* sys_write64 provides a wmb */
  ipc_hw_mbox_control_write(mb, mbox_idx, hdr);

  ipc_hw_mbox_notify_peer(mb);
}


void ipc_hw_mbox_flush(ipc_hardware_mailbox_ctx_t *mb, unsigned mbox_idx)
{
  /* noop, writes have already landed */
}

static void ipc_hw_mbox_work(struct k_work *w)
{
  bool got_lock;
  ipc_hardware_mailbox_ctx_t *mb = CONTAINER_OF(w, ipc_hardware_mailbox_ctx_t, work);
  ipc_hardware_ctx_t *hw = ipc_hw_mbox_hardware(mb);
  ipc_context_t *ipc = ipc_hw_ipc_context(hw);

#ifdef TAWK_IPC_PROFILE
  ipc_prof_end(&mb->idle_prof);
  ipc_prof_begin(&mb->lock_prof);
#endif

  got_lock = ipc_lock_for_work(ipc, w);

#ifdef TAWK_IPC_PROFILE
  ipc_prof_end(&mb->lock_prof);
  ipc_prof_begin(&mb->active_prof);
#endif
  if (!got_lock)
    goto out;

  ipc_hw_mbox_alert(mb);

  ipc_unlock(ipc);

out:
#ifdef TAWK_IPC_PROFILE
  ipc_prof_end(&mb->active_prof);
  ipc_prof_begin(&mb->idle_prof);
#endif
  ;
}

void ipc_hw_mbox_notify(ipc_hardware_mailbox_ctx_t *mb)
{
  ipc_hardware_ctx_t *hw = ipc_hw_mbox_hardware(mb);
  ipc_context_t *ipc = ipc_hw_ipc_context(hw);

  k_work_submit_to_queue(&ipc->workq, &mb->work);
}

void ipc_hw_mbox_dump(ipc_hardware_mailbox_ctx_t *mb)
{
  ipc_hardware_ctx_t *hw = ipc_hw_mbox_hardware(mb);
  ipc_context_t *ipc = ipc_hw_ipc_context(hw);

  for (unsigned mbox_idx = 0; mbox_idx < ipc_hw_mbox_count(mb); mbox_idx++) {
    TAWK_IPC_DUMP(ipc, "hw_mbox %u: ctrl reg %016" PRIx64, mbox_idx, ipc_hw_mbox_control_read(mb, mbox_idx));
    TAWK_IPC_HEXDUMP(ipc, (void *)ipc_hw_mbox_buffer(mb, mbox_idx), 64, "Dump of the first 64 bytes of payload ");
  }
}
