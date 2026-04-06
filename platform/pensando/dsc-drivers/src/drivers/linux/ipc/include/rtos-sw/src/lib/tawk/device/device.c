// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#include <zephyr/kernel.h>

#if defined(RTOS)
#if defined(CONFIG_TAWK_IPC_WITH_INTR)
#include <intrutils.h>
#endif
#endif

#include <tawk/ipc.h>
#include <tawk/device.h>

#include "../platform/platform.h"
#include "../ipc_internal.h"
#include "mem_layout.h"

#include "device.h"

#ifdef RTOS
#include "log_helper/log_helper.h"

LOG_HELPER_DECLARE(tawk, CONFIG_TAWK_IPC_LOG_LEVEL);
#else
LOG_MODULE_DECLARE(tawk, CONFIG_TAWK_IPC_LOG_LEVEL);
#endif

ipc_context_t *ipc_dev_ipc_context(ipc_device_ctx_t *dev)
{
  /* This upcasting is ugly, but I'm not sure how this bit
   * of the code should look, so it feels fine for now. */
  return CONTAINER_OF(dev, ipc_context_t, dev);
}


#if IPC_WITH_POLLING
static void ipc_dev_rstsigs_timer(struct k_timer *t)
{
  ipc_device_ctx_t *dev = CONTAINER_OF(t, ipc_device_ctx_t, rstsigs_timer);
  ipc_context_t *ipc = ipc_dev_ipc_context(dev);
  ipc_hw_rst_notify(&ipc->hw.signals);
}

static void ipc_dev_mbox_timer(struct k_timer *t)
{
  ipc_device_ctx_t *dev = CONTAINER_OF(t, ipc_device_ctx_t, mbox_timer);
  ipc_context_t *ipc = ipc_dev_ipc_context(dev);
  ipc_hw_mbox_notify(&ipc->hw.mbox);
}
#endif

#if defined(RTOS)
static void ipc_dev_isr_rst(const void *arg)
{
  ipc_device_ctx_t *dev = (void*)arg;
  ipc_context_t *ipc = ipc_dev_ipc_context(dev);

  TAWK_IPC_VERY_VERBOSE_LOG(ipc, "%s: %u", __func__, dev->rstsigs_db_irq);

  ipc_hw_rst_notify(&ipc->hw.signals);
}

static void ipc_dev_isr_mbox(const void *arg)
{
  ipc_device_ctx_t *dev = (void*)arg;
  ipc_context_t *ipc = ipc_dev_ipc_context(dev);

  TAWK_IPC_VERY_VERBOSE_LOG(ipc, "%s: %u", __func__, dev->mbox_db_irq);

  ipc_hw_mbox_notify(&ipc->hw.mbox);
}

#if defined(CONFIG_TAWK_IPC_WITH_INTR)
extern int plat_alloc_msi(unsigned *irq, uint64_t *msgaddr, uint32_t *msgdata);

int ipc_device_db_via_intr(unsigned intr, unsigned *irq, uint64_t *dbaddr, uint32_t *dbdata)
{
  uint64_t msgaddr;
  uint32_t msgdata;

  int rc = plat_alloc_msi(irq, &msgaddr, &msgdata);
  if (rc < 0) {
    /* platform code uses -errno; tawk uses +errno */
    return -rc;
  }

  intr_config_local_msi(intr, msgaddr, msgdata);

  *dbaddr = intr_assert_addr(intr);
  *dbdata = intr_assert_data();

  return 0;
}
#endif
#endif

#if defined(RTOS)
#define irq_disconnect_dynamic(...) // not implemented?
#endif
int ipc_device_init(ipc_context_t *ipc,
                    int peer_id,
                    bool own_mem,
                    mm_reg_t rst_sigs_out_addr,
                    mm_reg_t rst_sigs_in_addr,
#if IPC_WITH_POLLING
                    k_timeout_t rst_poll_interval,
#endif
#if defined(RTOS)
                    unsigned rst_db_irq,
#endif
                    ipc_device_notify_peer_spec_t rst_notify_peer,
                    unsigned mbox_count,
                    mm_reg_t mbox_control_base,
                    unsigned mbox_control_stride_log2,
#ifdef CONFIG_TAWK_IPC_CACHE_MANAGEMENT
                    bool mbox_buffer_cached,
#endif
                    mm_reg_t mbox_buffer_base,
                    unsigned mbox_buffer_stride_log2,
                    size_t mbox_buffer_size,
#if IPC_WITH_POLLING
                    k_timeout_t mbox_poll_interval,
#endif
#if defined(RTOS)
                    unsigned mbox_db_irq,
#endif
#if defined(IPC_BUILD_KERNEL_MODULE)
                    ipc_device_fini_interrupts_fn *fini_interrupts_fn,
                    void *fini_interrupts_ctx,
                    struct device *device,
#endif
                    ipc_device_notify_peer_spec_t mbox_notify_peer)
{
  ipc_device_ctx_t *dev = &ipc->dev;
  int rc;

#if IPC_WITH_POLLING
  k_timer_init(&dev->rstsigs_timer, ipc_dev_rstsigs_timer, NULL);
  k_timer_init(&dev->mbox_timer, ipc_dev_mbox_timer, NULL);
#endif

#if defined(RTOS)
  dev->rstsigs_db_irq = rst_db_irq;
  dev->mbox_db_irq = mbox_db_irq;

  if (dev->rstsigs_db_irq != IPC_DEVICE_DB_IRQ_NONE &&
      irq_connect_dynamic(dev->rstsigs_db_irq, IRQ_DEFAULT_PRIORITY,
                          ipc_dev_isr_rst, ipc, IRQ_TYPE_EDGE) < 0) {
    rc = ENOMEM;
    goto fail0;
  }

  if (dev->mbox_db_irq != IPC_DEVICE_DB_IRQ_NONE &&
      irq_connect_dynamic(dev->mbox_db_irq, IRQ_DEFAULT_PRIORITY,
                          ipc_dev_isr_mbox, ipc, IRQ_TYPE_EDGE) < 0) {
    rc = ENOMEM;
    goto fail1;
  }
#endif

  dev->rst_notify_peer = rst_notify_peer;
  dev->mbox_notify_peer = mbox_notify_peer;

#if defined(IPC_BUILD_KERNEL_MODULE)
  dev->fini_interrupts_fn = fini_interrupts_fn;
  dev->fini_interrupts_ctx = fini_interrupts_ctx;

  dev->dev = device;
#endif

  rc = ipc_init(ipc,
                peer_id,
                own_mem,
                rst_sigs_out_addr,
                rst_sigs_in_addr,
                mbox_count,
                mbox_control_base,
                mbox_control_stride_log2,
#ifdef CONFIG_TAWK_IPC_CACHE_MANAGEMENT
                mbox_buffer_cached,
#endif
                mbox_buffer_base,
                mbox_buffer_stride_log2,
                mbox_buffer_size);

  if (rc != 0)
    goto fail2;

#if defined(RTOS)
  if (dev->rstsigs_db_irq != IPC_DEVICE_DB_IRQ_NONE)
    irq_enable(dev->rstsigs_db_irq);

  if (dev->mbox_db_irq != IPC_DEVICE_DB_IRQ_NONE)
    irq_enable(dev->mbox_db_irq);
#endif

#if IPC_WITH_POLLING
  if (!K_TIMEOUT_EQ(rst_poll_interval, K_FOREVER))
    k_timer_start(&dev->rstsigs_timer, rst_poll_interval, rst_poll_interval);

  if (!K_TIMEOUT_EQ(mbox_poll_interval, K_FOREVER))
    k_timer_start(&dev->mbox_timer, mbox_poll_interval, mbox_poll_interval);
#endif

  return 0;

 fail2:

#if defined(RTOS)
  irq_disconnect_dynamic(dev->mbox_db_irq, IRQ_DEFAULT_PRIORITY,
                         ipc_dev_isr_mbox, pcie, IRQ_TYPE_EDGE);
 fail1:

  irq_disconnect_dynamic(pcie->rstsigs_db_.irq, IRQ_DEFAULT_PRIORITY,
                         ipc_dev_isr_rst, pcie, IRQ_TYPE_EDGE);

 fail0:
#endif

  return rc;
}

static void ipc_device_deinit(ipc_context_t *ipc)
{
#if IPC_WITH_POLLING
  ipc_device_ctx_t *dev = &ipc->dev;

  k_timer_stop(&dev->rstsigs_timer);
  k_timer_stop(&dev->mbox_timer);
  k_timer_status_sync(&dev->rstsigs_timer);
  k_timer_status_sync(&dev->mbox_timer);
#endif

#if defined(RTOS)
  if (dev->mbox_db_irq != IPC_DEVICE_DB_IRQ_NONE) {
    irq_disable(dev->mbox_db_irq);
    irq_disconnect_dynamic(dev->mbox_db_irq, IRQ_DEFAULT_PRIORITY,
                           ipc_dev_isr_mbox, pcie, IRQ_TYPE_EDGE);
    dev->mbox_db_irq = IPC_DEVICE_DB_IRQ_NONE;
  }

  if (dev->rstsigs_db_irq != IPC_DEVICE_DB_IRQ_NONE) {
    irq_disable(dev->rstsigs_db_irq);
    irq_disconnect_dynamic(dev->rstsigs_db_.irq, IRQ_DEFAULT_PRIORITY,
                           ipc_dev_isr_rst, pcie, IRQ_TYPE_EDGE);
    dev->rstsigs_db_irq = IPC_DEVICE_DB_IRQ_NONE;
  }
#endif

#if defined(IPC_BUILD_KERNEL_MODULE)
  dev->dev = NULL;

  if (dev->fini_interrupts_fn) {
    dev->fini_interrupts_fn(dev->fini_interrupts_ctx);
    dev->fini_interrupts_fn = NULL;
  }
#endif
}

void ipc_device_quarantine(ipc_context_t *ipc)
{
  ipc_device_deinit(ipc);
  ipc_quarantine(ipc);
}

void ipc_device_fini(ipc_context_t *ipc)
{
  ipc_device_deinit(ipc);
  ipc_fini(ipc);
}


static void ipc_dev_notify_peer(ipc_context_t *ipc, ipc_device_notify_peer_spec_t *spec)
{
  if (IPC_DEVICE_NOTIFY_PEER_SPEC_IS_NONE(*spec))
    return;

#if defined(RTOS)
#if defined(CONFIG_TAWK_IPC_WITH_INTR)
  atomic_val_t intr = atomic_get(&spec->intr);
  /* PS-11921: remove this condition */
  if (intr != IPC_DEVICE_INTR_NONE) {
    TAWK_IPC_VERY_VERBOSE_LOG(ipc, "%s: %lx", __func__, (long)intr);
    intr_assert(intr);
  }
#else
  __ASSERT_NO_MSG(0); /* unreachable */
#endif
#elif defined(IPC_BUILD_KERNEL_MODULE)
  TAWK_IPC_VERY_VERBOSE_LOG(ipc, "%s: %llx %lx", __func__, (unsigned long long)spec->addr, (unsigned long)spec->val);
  sys_write32(spec->val, spec->addr);
#else
#error "Who am I"
#endif
}

void ipc_hw_rst_notify_peer(ipc_hardware_rstsigs_ctx_t *sigs)
{
  ipc_hardware_ctx_t *hw = ipc_hw_rst_hardware(sigs);
  ipc_context_t *ipc = ipc_hw_ipc_context(hw);
  ipc_device_ctx_t *dev = &ipc->dev;
  ipc_device_notify_peer_spec_t *spec = &dev->rst_notify_peer;

  ipc_dev_notify_peer(ipc, spec);
}

void ipc_hw_mbox_notify_peer(ipc_hardware_mailbox_ctx_t *mb)
{
  ipc_hardware_ctx_t *hw = ipc_hw_mbox_hardware(mb);
  ipc_context_t *ipc = ipc_hw_ipc_context(hw);
  ipc_device_ctx_t *dev = &ipc->dev;
  ipc_device_notify_peer_spec_t *spec = &dev->mbox_notify_peer;

  ipc_dev_notify_peer(ipc, spec);
}

#if defined(IPC_BUILD_KERNEL_MODULE)
void ipc_device_rst_notify_poll(ipc_context_t *ipc)
{
  ipc_hw_rst_notify(&ipc->hw.signals);
}

void ipc_device_mbox_notify_poll(ipc_context_t *ipc)
{
  ipc_hw_mbox_notify(&ipc->hw.mbox);
}

void ipc_device_rst_notify_interrupt(ipc_context_t *ipc)
{
  ipc_hw_rst_notify(&ipc->hw.signals);
}

void ipc_device_mbox_notify_interrupt(ipc_context_t *ipc)
{
  ipc_hw_mbox_notify(&ipc->hw.mbox);
}
#endif
