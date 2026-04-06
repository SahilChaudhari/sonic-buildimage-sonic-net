// SPDX-License-Identifier: MIT or GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#include <zephyr/kernel.h>

#if defined(RTOS)
#include <zephyr/sys/device_mmio.h>
#include <string.h>
#endif

#include <tawk/ipc.h>
#include <tawk/device.h>

#include "../platform/platform.h"
#include "../ipc_internal.h"
#include "device.h"
#include "mem_layout.h"
#include "pcie_hdr.h"

#include "pcie.h"

#ifdef RTOS
#include "log_helper/log_helper.h"

LOG_HELPER_DECLARE(tawk, CONFIG_TAWK_IPC_LOG_LEVEL);
#else
LOG_MODULE_DECLARE(tawk, CONFIG_TAWK_IPC_LOG_LEVEL);
#endif

#define IPC_PCIE_BAR_HDR_MASK(field_)                           \
  GENMASK64(((TAWK_IPC_PCIE_HDR_ ## field_ ## _LBN) +           \
             (TAWK_IPC_PCIE_HDR_ ## field_ ## _WIDTH) - 1),     \
            (TAWK_IPC_PCIE_HDR_ ## field_ ## _LBN))

#define IPC_PCIE_BAR_HDR_FIELD_GET(hdr_, field_)       \
  FIELD_GET(IPC_PCIE_BAR_HDR_MASK(field_), hdr_)

#define IPC_PCIE_BAR_HDR_FIELD_PREP(field_, value_)    \
  FIELD_PREP(IPC_PCIE_BAR_HDR_MASK(field_), value_)

#define IPC_PCIE_BAR_HDR_FIELD_SET(hdr_, field_, value_)       \
  do {                                                         \
    (hdr_) &= ~IPC_PCIE_BAR_HDR_MASK(field_);                  \
    (hdr_) |= IPC_PCIE_BAR_HDR_FIELD_PREP(field_, value_);     \
  } while (0)


#if defined(RTOS)
ipc_device_ctx_t *ipc_pcie_device(ipc_device_pcie_ctx_t *pcie)
{
  /* This upcasting is ugly, but I'm not sure how this bit
   * of the code should look, so it feels fine for now. */
  return CONTAINER_OF(pcie, ipc_device_ctx_t, u.pcie);
}
#endif


#if defined(RTOS)

typedef struct ipc_pcie_dt_config_s {
#if defined(CONFIG_TAWK_IPC_WITH_INTR)
  unsigned intrb;
  unsigned intrc;
#endif

  uintptr_t bar_mem_phys_addr;
  size_t bar_mem_size; /* At least IPC_PCIE_BAR_MEM_SIZE */

  uintptr_t bar_hdr_mem_rd_phys_addr;
  size_t bar_hdr_mem_rd_size; /* At least IPC_PCIE_BAR_HDR_MEM_SIZE */

  uintptr_t bar_hdr_mem_wr_phys_addr;
  size_t bar_hdr_mem_wr_size; /* At least IPC_PCIE_BAR_HDR_MEM_SIZE */

#if defined(CONFIG_TAWK_IPC_WITH_INTR)
  unsigned db_intrb;
  unsigned db_intrc; /* In then range [0,IPC_PCIE_BAR_DB_IRQ_COUNT] */
#endif
} ipc_pcie_dt_config_t;

static void ipc_pcie_cfg_init(ipc_device_pcie_ctx_t *pcie,
                              const ipc_pcie_dt_config_t *dt_cfg);
static void ipc_pcie_bar_hdr_init(ipc_device_pcie_ctx_t *pcie);
static void ipc_pcie_bar_hdr_poll_timer(struct k_timer *t);

static int ipc_device_init_pcie_ep(ipc_context_t *ipc,
                                   const ipc_pcie_dt_config_t *dt_cfg)
{
  ipc_device_ctx_t *dev = &ipc->dev;
  ipc_device_pcie_ctx_t *pcie = &dev->u.pcie;
  int rc;

#if defined(CONFIG_TAWK_IPC_WITH_INTR)
  unsigned db_irq[IPC_PCIE_BAR_DB_IRQ_COUNT];
  uint64_t db_addr[IPC_PCIE_BAR_DB_IRQ_COUNT];
  uint32_t db_data[IPC_PCIE_BAR_DB_IRQ_COUNT];

  for (unsigned i = 0; i < IPC_PCIE_BAR_DB_IRQ_COUNT; i++) {
    if (i < dt_cfg->db_intrc) {
      rc = ipc_device_db_via_intr(dt_cfg->db_intrb + i, &db_irq[i], &db_addr[i], &db_data[i]);
      if (rc != 0)
        goto fail0;
    } else {
      db_irq[i] = IPC_DEVICE_DB_IRQ_NONE;
    }
  }

  BUILD_ASSERT(IPC_PLATFORM_RST_DB < IPC_PCIE_BAR_DB_IRQ_COUNT);
  BUILD_ASSERT(IPC_PLATFORM_MBOX_DB < IPC_PCIE_BAR_DB_IRQ_COUNT);

  unsigned rstsigs_db_irq = db_irq[IPC_PLATFORM_RST_DB];
  unsigned mbox_db_irq = db_irq[IPC_PLATFORM_MBOX_DB];
  pcie->rst_db_data = db_data[IPC_PLATFORM_RST_DB];
  pcie->mbox_db_data = db_data[IPC_PLATFORM_MBOX_DB];
#else
  static const unsigned rstsigs_db_irq = IPC_DEVICE_DB_IRQ_NONE;
  static const unsigned mbox_db_irq = IPC_DEVICE_DB_IRQ_NONE;
#endif

#if defined(CONFIG_TAWK_IPC_WITH_INTR)
  pcie->interrupt_en = false;
  pcie->rst_intr = 0;
  pcie->mbox_intr = 0;
  pcie->intrb = dt_cfg->intrb;
  pcie->intrc = dt_cfg->intrc;
#endif

  BUILD_ASSERT(IPC_PLATFORM_RSTSIGS_OUT_OFFS_EP < IPC_PCIE_BAR_MEM_SIZE);
  BUILD_ASSERT(IPC_PLATFORM_RSTSIGS_IN_OFFS_EP < IPC_PCIE_BAR_MEM_SIZE);
  BUILD_ASSERT(IPC_PLATFORM_MBOX_CONTROL_OFFS < IPC_PCIE_BAR_MEM_SIZE);
  BUILD_ASSERT((IPC_PLATFORM_MBOX_CONTROL_OFFS +
                (IPC_PLATFORM_MBOX_COUNT << IPC_PLATFORM_MBOX_CONTROL_STRIDE_LOG2)) < IPC_PCIE_BAR_MEM_SIZE);
  BUILD_ASSERT(IPC_PLATFORM_MBOX_BUFFER_OFFS < IPC_PCIE_BAR_MEM_SIZE);
  BUILD_ASSERT((IPC_PLATFORM_MBOX_BUFFER_OFFS +
                (IPC_PLATFORM_MBOX_COUNT << IPC_PLATFORM_MBOX_BUFFER_STRIDE_LOG2)) < IPC_PCIE_BAR_MEM_SIZE);

#if IPC_PS_11703_WORKAROUND
  pcie->bar_hdr_rd_base_addr = dt_cfg->bar_hdr_mem_rd_phys_addr;
#else
  device_map(&pcie->bar_hdr_rd_base_addr, dt_cfg->bar_hdr_mem_rd_phys_addr,
             IPC_PCIE_BAR_HDR_MEM_SIZE, K_MEM_CACHE_NONE);
#endif
#if IPC_PS_11703_WORKAROUND
  pcie->bar_hdr_wr_base_addr = dt_cfg->bar_hdr_mem_wr_phys_addr;
#else
  device_map(&pcie->bar_hdr_wr_base_addr, dt_cfg->bar_hdr_mem_wr_phys_addr,
             IPC_PCIE_BAR_HDR_MEM_SIZE, K_MEM_CACHE_NONE);
#endif

  ipc_pcie_cfg_init(pcie, dt_cfg);

  ipc_pcie_bar_hdr_init(pcie);

  k_timer_init(&pcie->bar_hdr_poll_timer, ipc_pcie_bar_hdr_poll_timer, NULL);
  k_timer_start(&pcie->bar_hdr_poll_timer, K_MSEC(10), K_MSEC(10));

  mm_reg_t base_addr;
#if IPC_PS_11703_WORKAROUND
  base_addr = dt_cfg->bar_mem_phys_addr;
#else
  device_map(&base_addr, dt_cfg->bar_mem_phys_addr, IPC_PCIE_BAR_MEM_SIZE, K_MEM_CACHE_NONE);
#endif

  rc = ipc_device_init(ipc,
                       IPC_PLATFORM_PEER_ID_EP,
                       /* own_mem = */true,
                       base_addr + IPC_PLATFORM_RSTSIGS_OUT_OFFS_EP,
                       base_addr + IPC_PLATFORM_RSTSIGS_IN_OFFS_EP,
#if IPC_WITH_POLLING
                       (rstsigs_db_irq == IPC_DEVICE_DB_IRQ_NONE ?
                        IPC_DEVICE_RST_POLL_INTERVAL : K_FOREVER),
#endif
                       rstsigs_db_irq,
                       IPC_DEVICE_NOTIFY_PEER_SPEC_NONE,
                       IPC_PLATFORM_MBOX_COUNT,
                       base_addr + IPC_PLATFORM_MBOX_CONTROL_OFFS,
                       IPC_PLATFORM_MBOX_CONTROL_STRIDE_LOG2,
#ifdef CONFIG_TAWK_IPC_CACHE_MANAGEMENT
                       /* mbox_buffer_cached = */false,
#endif
                       base_addr + IPC_PLATFORM_MBOX_BUFFER_OFFS,
                       IPC_PLATFORM_MBOX_BUFFER_STRIDE_LOG2,
                       IPC_PLATFORM_MBOX_BUFFER_SIZE,
#if IPC_WITH_POLLING
                       (mbox_db_irq == IPC_DEVICE_DB_IRQ_NONE ?
                        IPC_DEVICE_RST_POLL_INTERVAL : K_FOREVER),
#endif
                       mbox_db_irq,
                       IPC_DEVICE_NOTIFY_PEER_SPEC_NONE);

  if (rc != 0)
    goto fail0;

  return 0;


 fail0:
  return rc;
}

const ipc_device_pcie_config_t *ipc_device_pcie_config(ipc_context_t *ipc)
{
  ipc_device_ctx_t *dev = &ipc->dev;
  ipc_device_pcie_ctx_t *pcie = &dev->u.pcie;
  return &pcie->cfg;
}

static void ipc_pcie_cfg_init(ipc_device_pcie_ctx_t *pcie,
                              const ipc_pcie_dt_config_t *dt_cfg)
{
  ipc_device_pcie_config_t *cfg = &pcie->cfg;

#if defined(CONFIG_TAWK_IPC_WITH_INTR)
  cfg->intrb = dt_cfg->intrb;
  cfg->intrc = dt_cfg->intrc;
#endif
  cfg->mem_pa = dt_cfg->bar_mem_phys_addr;
  cfg->mem_sz = dt_cfg->bar_mem_size;
  cfg->bar_hdr_rd_pa = dt_cfg->bar_hdr_mem_rd_phys_addr;
  cfg->bar_hdr_rd_sz = dt_cfg->bar_hdr_mem_rd_size;
  cfg->bar_hdr_wr_pa = dt_cfg->bar_hdr_mem_wr_phys_addr;
  cfg->bar_hdr_wr_sz = dt_cfg->bar_hdr_mem_wr_size;
#if defined(CONFIG_TAWK_IPC_WITH_INTR)
  cfg->db_intrb = dt_cfg->db_intrb;
  cfg->db_intrc = dt_cfg->db_intrc;
#endif
}

#define IPC_PCIE_BAR_CHECK_REG(reg_)                                    \
  BUILD_ASSERT(TAWK_IPC_PCIE_HDR_ ## reg_ ## _REG >= IPC_PCIE_BAR_HDR_MEM_OFFS && \
               TAWK_IPC_PCIE_HDR_ ## reg_ ## _REG + 4 <= IPC_PCIE_BAR_HDR_MEM_OFFS + IPC_PCIE_BAR_HDR_MEM_SIZE)

static void ipc_pcie_bar_hdr_init(ipc_device_pcie_ctx_t *pcie)
{
  /* Don't leak any existing data */
  memset((void *)pcie->bar_hdr_rd_base_addr, 0xff, IPC_PCIE_BAR_HDR_MEM_SIZE);

  IPC_PCIE_BAR_CHECK_REG(MAGIC);
  IPC_PCIE_BAR_CHECK_REG(VER);
  IPC_PCIE_BAR_CHECK_REG(CAP);
  IPC_PCIE_BAR_CHECK_REG(STA);
  IPC_PCIE_BAR_CHECK_REG(CTL);
  IPC_PCIE_BAR_CHECK_REG(INT_VEC_RST);
  IPC_PCIE_BAR_CHECK_REG(INT_VEC_MBOX);
  IPC_PCIE_BAR_CHECK_REG(DBELL_RST_OFFS);
  IPC_PCIE_BAR_CHECK_REG(DBELL_RST_VAL);
  IPC_PCIE_BAR_CHECK_REG(DBELL_MBOX_OFFS);
  IPC_PCIE_BAR_CHECK_REG(DBELL_MBOX_VAL);
  IPC_PCIE_BAR_CHECK_REG(RST_SIGS_IN_OFFS);
  IPC_PCIE_BAR_CHECK_REG(RST_SIGS_OUT_OFFS);
  IPC_PCIE_BAR_CHECK_REG(MBOX_COUNT);
  IPC_PCIE_BAR_CHECK_REG(MBOX_CTL_OFFS);
  IPC_PCIE_BAR_CHECK_REG(MBOX_CTL_STRIDE);
  IPC_PCIE_BAR_CHECK_REG(MBOX_BUF_OFFS);
  IPC_PCIE_BAR_CHECK_REG(MBOX_BUF_STRIDE);
  IPC_PCIE_BAR_CHECK_REG(MBOX_BUF_SIZE);

  static const uint8_t hdr_magic[] = TAWK_IPC_PCIE_HDR_MAGIC;
  memcpy((void *)(pcie->bar_hdr_rd_base_addr + TAWK_IPC_PCIE_HDR_MAGIC_REG),
         hdr_magic, sizeof hdr_magic);

  sys_write32(CONFIG_TAWK_IPC_PCIE_DEVICE_VERSION,
              pcie->bar_hdr_rd_base_addr + TAWK_IPC_PCIE_HDR_VER_REG);

  sys_write32(IPC_PCIE_BAR_HDR_FIELD_PREP(CAP_PEER_ID,
                                          IPC_PLATFORM_PEER_ID_HOST) |
#if defined(CONFIG_TAWK_IPC_WITH_INTR)
              IPC_PCIE_BAR_HDR_FIELD_PREP(CAP_INTERRUPTS, 1) |
              IPC_PCIE_BAR_HDR_FIELD_PREP(CAP_INTR_ASSN, 1) |
#endif
              IPC_PCIE_BAR_HDR_FIELD_PREP(CAP_INTR_REGS, 0),
              pcie->bar_hdr_rd_base_addr + TAWK_IPC_PCIE_HDR_CAP_REG);

  sys_write32(0,
              pcie->bar_hdr_rd_base_addr + TAWK_IPC_PCIE_HDR_STA_REG);

  sys_write32(
#if defined(CONFIG_TAWK_IPC_WITH_INTR)
              IPC_PCIE_BAR_HDR_FIELD_PREP(CTL_INTERRUPT_EN, pcie->interrupt_en) |
#endif
              0,
              pcie->bar_hdr_rd_base_addr + TAWK_IPC_PCIE_HDR_CTL_REG);

#if defined(CONFIG_TAWK_IPC_WITH_INTR)
  sys_write32(pcie->rst_intr,
              pcie->bar_hdr_rd_base_addr + TAWK_IPC_PCIE_HDR_INT_VEC_RST_REG);

  sys_write32(pcie->mbox_intr,
              pcie->bar_hdr_rd_base_addr + TAWK_IPC_PCIE_HDR_INT_VEC_MBOX_REG);
#endif

  sys_write32(0 /* no interrupt registers */,
              pcie->bar_hdr_rd_base_addr + TAWK_IPC_PCIE_HDR_INTR_REGS_BIR_OFFS_REG);

  sys_write32(
#if defined(CONFIG_TAWK_IPC_WITH_INTR)
              (IPC_PCIE_BAR_DB_OFFS + IPC_PCIE_BAR_DB_STRIDE * IPC_PLATFORM_RST_DB) |
#endif
              0,
              pcie->bar_hdr_rd_base_addr + TAWK_IPC_PCIE_HDR_DBELL_RST_OFFS_REG);

#if defined(CONFIG_TAWK_IPC_WITH_INTR)
  sys_write32(pcie->rst_db_data,
              pcie->bar_hdr_rd_base_addr + TAWK_IPC_PCIE_HDR_DBELL_RST_VAL_REG);
#endif

  sys_write32(
#if defined(CONFIG_TAWK_IPC_WITH_INTR)
              (IPC_PCIE_BAR_DB_OFFS + IPC_PCIE_BAR_DB_STRIDE * IPC_PLATFORM_MBOX_DB) |
#endif
              0,
              pcie->bar_hdr_rd_base_addr + TAWK_IPC_PCIE_HDR_DBELL_MBOX_OFFS_REG);

#if defined(CONFIG_TAWK_IPC_WITH_INTR)
  sys_write32(pcie->mbox_db_data,
              pcie->bar_hdr_rd_base_addr + TAWK_IPC_PCIE_HDR_DBELL_MBOX_VAL_REG);
#endif

  sys_write32(IPC_PCIE_BAR_MEM_OFFS +
              IPC_PLATFORM_RSTSIGS_IN_OFFS(IPC_PLATFORM_PEER_ID_HOST),
              pcie->bar_hdr_rd_base_addr + TAWK_IPC_PCIE_HDR_RST_SIGS_IN_OFFS_REG);

  sys_write32(IPC_PCIE_BAR_MEM_OFFS +
              IPC_PLATFORM_RSTSIGS_OUT_OFFS(IPC_PLATFORM_PEER_ID_HOST),
              pcie->bar_hdr_rd_base_addr + TAWK_IPC_PCIE_HDR_RST_SIGS_OUT_OFFS_REG);

  sys_write32(IPC_PLATFORM_MBOX_COUNT,
              pcie->bar_hdr_rd_base_addr + TAWK_IPC_PCIE_HDR_MBOX_COUNT_REG);

  sys_write32(IPC_PCIE_BAR_MEM_OFFS +
              IPC_PLATFORM_MBOX_CONTROL_OFFS,
              pcie->bar_hdr_rd_base_addr + TAWK_IPC_PCIE_HDR_MBOX_CTL_OFFS_REG);

  sys_write32(IPC_PLATFORM_MBOX_CONTROL_STRIDE_LOG2,
              pcie->bar_hdr_rd_base_addr + TAWK_IPC_PCIE_HDR_MBOX_CTL_STRIDE_REG);

  sys_write32(IPC_PCIE_BAR_MEM_OFFS +
              IPC_PLATFORM_MBOX_BUFFER_OFFS,
              pcie->bar_hdr_rd_base_addr + TAWK_IPC_PCIE_HDR_MBOX_BUF_OFFS_REG);

  sys_write32(IPC_PLATFORM_MBOX_BUFFER_STRIDE_LOG2,
              pcie->bar_hdr_rd_base_addr + TAWK_IPC_PCIE_HDR_MBOX_BUF_STRIDE_REG);

  sys_write32(IPC_PLATFORM_MBOX_BUFFER_SIZE,
              pcie->bar_hdr_rd_base_addr + TAWK_IPC_PCIE_HDR_MBOX_BUF_SIZE_REG);


  memcpy((void *)pcie->bar_hdr_wr_base_addr,
         (void *)pcie->bar_hdr_rd_base_addr,
         IPC_PCIE_BAR_HDR_MEM_SIZE);

}

#if defined(CONFIG_TAWK_IPC_WITH_INTR)
static void ipc_pcie_update_intr(ipc_device_ctx_t *dev)
{
  ipc_device_pcie_ctx_t *pcie = &dev->u.pcie;

#define INTR_REL_TO_ABS(pcie_, rel_intr_)       \
  (((rel_intr_) < (pcie_)->intrc) ?             \
   ((rel_intr_) + (pcie_)->intrb) :             \
   IPC_DEVICE_INTR_NONE)

  if (pcie->interrupt_en) {
    atomic_set(&dev->rst_notify_peer.intr, INTR_REL_TO_ABS(pcie, pcie->rst_intr));
    atomic_set(&dev->mbox_notify_peer.intr, INTR_REL_TO_ABS(pcie, pcie->mbox_intr));
  } else {
    atomic_set(&dev->rst_notify_peer.intr, IPC_DEVICE_INTR_NONE);
    atomic_set(&dev->mbox_notify_peer.intr, IPC_DEVICE_INTR_NONE);
  }

#undef INTR_REL_TO_ABS
}
#endif

static void ipc_pcie_bar_hdr_write(ipc_device_pcie_ctx_t *pcie, size_t baroff, uint32_t val)
{
#if defined(CONFIG_TAWK_IPC_WITH_INTR)
  ipc_device_ctx_t *dev = ipc_pcie_device(pcie);
#endif

  TAWK_IPC_VERY_VERBOSE_LOG("%s: %x %x", __func__, (unsigned) baroff, val);

  switch (baroff) {
#if defined(CONFIG_TAWK_IPC_WITH_INTR)
  case TAWK_IPC_PCIE_HDR_CTL_REG:
    pcie->interrupt_en = IPC_PCIE_BAR_HDR_FIELD_GET(val, CTL_INTERRUPT_EN);
    ipc_pcie_update_intr(dev);
    break;

  case TAWK_IPC_PCIE_HDR_INT_VEC_RST_REG:
    pcie->rst_intr = val;
    ipc_pcie_update_intr(dev);
    break;

  case TAWK_IPC_PCIE_HDR_INT_VEC_MBOX_REG:
    pcie->mbox_intr = val;
    ipc_pcie_update_intr(dev);
    break;
#endif
  }
}

static void ipc_pcie_bar_hdr_poll_reg(ipc_device_pcie_ctx_t *pcie, size_t reg_offs)
{
  uint32_t rval = sys_read32(pcie->bar_hdr_rd_base_addr + reg_offs);
  uint32_t wval = sys_read32(pcie->bar_hdr_wr_base_addr + reg_offs);

  if (rval != wval) {
    sys_write32(wval, pcie->bar_hdr_rd_base_addr + reg_offs);
    ipc_pcie_bar_hdr_write(pcie, reg_offs, wval);
  }
}

static void ipc_pcie_bar_hdr_poll(ipc_device_pcie_ctx_t *pcie)
{
  ipc_pcie_bar_hdr_poll_reg(pcie, TAWK_IPC_PCIE_HDR_CTL_REG);
  ipc_pcie_bar_hdr_poll_reg(pcie, TAWK_IPC_PCIE_HDR_INT_VEC_RST_REG);
  ipc_pcie_bar_hdr_poll_reg(pcie, TAWK_IPC_PCIE_HDR_INT_VEC_MBOX_REG);
}

static void ipc_pcie_bar_hdr_poll_timer(struct k_timer *t)
{
  ipc_device_pcie_ctx_t *pcie = CONTAINER_OF(t, ipc_device_pcie_ctx_t, bar_hdr_poll_timer);

  ipc_pcie_bar_hdr_poll(pcie);
}


__attribute__((unused))
static int ipc_pcie_dt_wrapper_init(const struct device *dev)
{
  const ipc_pcie_dt_config_t *dt_cfg = dev->config;
  ipc_context_t *ipc = ipc_get(dev);

  int rc;

  rc = ipc_device_init_pcie_ep(ipc, dt_cfg);
  if (rc != 0)
    goto fail0;

  rc = ipc_platform_dt_wrapper_init(dev);
  if (rc != 0)
    goto fail1;

  ipc_unlock(ipc);

  return 0;


 fail1:
  ipc_device_fini(ipc);

 fail0:
  /* tawk uses +errno; zephyr uses -errno */
  return -rc;
}

#if defined(CONFIG_TAWK_IPC_WITH_INTR)
#define TAWK_IPC_PCIE_INCLUDE_FOR_INTR(...) __VA_ARGS__
#else
#define TAWK_IPC_PCIE_INCLUDE_FOR_INTR(...)
#endif

#define TAWK_IPC_PCIE_EP_INIT(node_id_)                                 \
  BUILD_ASSERT(DT_REG_SIZE(DT_PROP(node_id_, mem)) >=                   \
               IPC_PCIE_BAR_MEM_SIZE);                                  \
  BUILD_ASSERT(DT_REG_SIZE(DT_PROP(node_id_, bar_hdr_rd_mem)) >=        \
               IPC_PCIE_BAR_HDR_MEM_SIZE);                              \
  BUILD_ASSERT(DT_REG_SIZE(DT_PROP(node_id_, bar_hdr_wr_mem)) >=        \
               IPC_PCIE_BAR_HDR_MEM_SIZE);                              \
  BUILD_ASSERT(DT_PROP_BY_IDX(node_id_, db_intrs, 1) <=                 \
               IPC_PCIE_BAR_DB_IRQ_COUNT);                              \
  static const ipc_pcie_dt_config_t                                     \
  ipc_pcie_cfg_ ## node_id_ = {                                         \
TAWK_IPC_PCIE_INCLUDE_FOR_INTR(                                         \
    .intrb = DT_PROP_BY_IDX(node_id_, intrs, 0),                        \
    .intrc = DT_PROP_BY_IDX(node_id_, intrs, 1),                        \
)                                                                       \
    .bar_mem_phys_addr = DT_REG_ADDR(DT_PROP(node_id_, mem)),           \
    .bar_mem_size = DT_REG_SIZE(DT_PROP(node_id_, mem)),                \
    .bar_hdr_mem_rd_phys_addr = DT_REG_ADDR(DT_PROP(node_id_, bar_hdr_rd_mem)), \
    .bar_hdr_mem_rd_size = DT_REG_SIZE(DT_PROP(node_id_, bar_hdr_rd_mem)), \
    .bar_hdr_mem_wr_phys_addr = DT_REG_ADDR(DT_PROP(node_id_, bar_hdr_wr_mem)), \
    .bar_hdr_mem_wr_size = DT_REG_SIZE(DT_PROP(node_id_, bar_hdr_wr_mem)), \
TAWK_IPC_PCIE_INCLUDE_FOR_INTR(                                         \
    .db_intrb = DT_PROP_BY_IDX(node_id_, db_intrs, 0),                  \
    .db_intrc = DT_PROP_BY_IDX(node_id_, db_intrs, 1),                  \
)                                                                       \
  };                                                                    \
  IPC_PLATFORM_DT_WRAPPER_INIT(node_id_,                                \
                               ipc_pcie_dt_wrapper_init,                \
                               &(ipc_pcie_cfg_ ## node_id_))

DT_FOREACH_STATUS_OKAY(amd_tawk_ipc_pcie_ep, TAWK_IPC_PCIE_EP_INIT);

#elif defined(IPC_BUILD_KERNEL_MODULE)

#if CONFIG_TAWK_IPC_VERBOSE_LOG == 2
#define VERY_VERBOSE_LOG(fmt, ...) pr_debug(fmt "\n", ##__VA_ARGS__)
#else
#define VERY_VERBOSE_LOG(fmt, ...)
#endif

static int ipc_pcie_bar_hdr_write32(uint32_t val, uint32_t mask, mm_reg_t addr)
{
  VERY_VERBOSE_LOG("%s: W %llx %x", __func__, (unsigned long long) addr, val);
  sys_write32(val, addr);
  do {
    uint32_t rval = sys_read32(addr);
     VERY_VERBOSE_LOG("%s: R %llx %x", __func__, (unsigned long long) addr, rval);
    if ((rval & mask) == (val & mask))
      return 0;

    k_sleep(K_MSEC(1));
  } while (1);

  return ETIME; /* for now, unreachable */
}

static int ipc_pcie_bar_hdr_parse_init(mm_reg_t bar_base_addr,
                                       int *peer_id,
                                       bool *interrupts,
                                       mm_reg_t *rst_sigs_out_addr,
                                       mm_reg_t *rst_sigs_in_addr,
                                       ipc_device_notify_peer_spec_t *rst_dbell,
                                       int rst_int_vec,
                                       unsigned *mbox_count,
                                       mm_reg_t *mbox_control_base,
                                       unsigned *mbox_control_stride_log2,
                                       mm_reg_t *mbox_buffer_base,
                                       unsigned *mbox_buffer_stride_log2,
                                       size_t *mbox_buffer_size,
                                       ipc_device_notify_peer_spec_t *mbox_dbell,
                                       int mbox_int_vec,
                                       ipc_device_init_interrupts_fn init_interrupts_fn,
                                       ipc_device_fini_interrupts_fn fini_interrupts_fn,
                                       void *fn_ctx)
{
  uint32_t ver;
  uint32_t cap;
  uint32_t rst_dbell_offs;
  uint32_t mbox_dbell_offs;
  uint32_t ctl;
  int rc;

  static const union {
    uint32_t u32;
    uint8_t u8[4];
  } m = { .u8 = TAWK_IPC_PCIE_HDR_MAGIC };
  uint32_t magic = sys_read32(bar_base_addr + TAWK_IPC_PCIE_HDR_MAGIC_REG);

  if (magic != m.u32)
    return ENOTSUP;

  ver = sys_read32(bar_base_addr + TAWK_IPC_PCIE_HDR_VER_REG);
  if (ver < CONFIG_TAWK_IPC_PCIE_DEVICE_MIN_VERSION ||
      ver > CONFIG_TAWK_IPC_PCIE_DEVICE_MAX_VERSION)
    return ENOTSUP;

  cap = sys_read32(bar_base_addr + TAWK_IPC_PCIE_HDR_CAP_REG);
  *peer_id = IPC_PCIE_BAR_HDR_FIELD_GET(cap, CAP_PEER_ID);

  if (ver == 0) {
    /* Not all PCIe devices support interrupts when using ver=0 and they can't be detected from capabilities.
     * Don't use interrupts with devices using ver=0 to avoid issues. */
    *interrupts = false;
  }
  else if (!IPC_PCIE_BAR_HDR_FIELD_GET(cap, CAP_INTERRUPTS) ||
           !IPC_PCIE_BAR_HDR_FIELD_GET(cap, CAP_INTR_ASSN)) {
    *interrupts = false;
  }

  *rst_sigs_out_addr = bar_base_addr + sys_read32(bar_base_addr + TAWK_IPC_PCIE_HDR_RST_SIGS_OUT_OFFS_REG);
  *rst_sigs_in_addr = bar_base_addr+ sys_read32(bar_base_addr + TAWK_IPC_PCIE_HDR_RST_SIGS_IN_OFFS_REG);

  rst_dbell_offs = sys_read32(bar_base_addr + TAWK_IPC_PCIE_HDR_DBELL_RST_OFFS_REG);
  if (rst_dbell_offs == 0) {
    *rst_dbell = IPC_DEVICE_NOTIFY_PEER_SPEC_NONE;
  } else {
    uint32_t rst_dbell_val = sys_read32(bar_base_addr + TAWK_IPC_PCIE_HDR_DBELL_RST_VAL_REG);
    *rst_dbell = IPC_DEVICE_NOTIFY_PEER_SPEC_DB(bar_base_addr + rst_dbell_offs, rst_dbell_val);
  }

  *mbox_count = sys_read32(bar_base_addr + TAWK_IPC_PCIE_HDR_MBOX_COUNT_REG);
  *mbox_control_base = bar_base_addr + sys_read32(bar_base_addr + TAWK_IPC_PCIE_HDR_MBOX_CTL_OFFS_REG);
  *mbox_control_stride_log2 = sys_read32(bar_base_addr + TAWK_IPC_PCIE_HDR_MBOX_CTL_STRIDE_REG);
  *mbox_buffer_base = bar_base_addr + sys_read32(bar_base_addr + TAWK_IPC_PCIE_HDR_MBOX_BUF_OFFS_REG);
  *mbox_buffer_stride_log2 = sys_read32(bar_base_addr + TAWK_IPC_PCIE_HDR_MBOX_BUF_STRIDE_REG);
  *mbox_buffer_size = sys_read32(bar_base_addr + TAWK_IPC_PCIE_HDR_MBOX_BUF_SIZE_REG);

  mbox_dbell_offs = sys_read32(bar_base_addr + TAWK_IPC_PCIE_HDR_DBELL_MBOX_OFFS_REG);
  if (mbox_dbell_offs == 0) {
    *mbox_dbell = IPC_DEVICE_NOTIFY_PEER_SPEC_NONE;
  } else {
    uint32_t mbox_dbell_val = sys_read32(bar_base_addr + TAWK_IPC_PCIE_HDR_DBELL_MBOX_VAL_REG);
    *mbox_dbell = IPC_DEVICE_NOTIFY_PEER_SPEC_DB(bar_base_addr + mbox_dbell_offs, mbox_dbell_val);
  }


  ctl = sys_read32(bar_base_addr + TAWK_IPC_PCIE_HDR_CTL_REG);
  IPC_PCIE_BAR_HDR_FIELD_SET(ctl, CTL_INTERRUPT_EN, 0);
  rc = ipc_pcie_bar_hdr_write32(ctl, IPC_PCIE_BAR_HDR_MASK(CTL_INTERRUPT_EN),
                                bar_base_addr + TAWK_IPC_PCIE_HDR_CTL_REG);
  if (rc != 0)
    return rc;

  if (*interrupts) {
    rc = init_interrupts_fn(fn_ctx);
    if (rc != 0)
      return rc;

    rc = ipc_pcie_bar_hdr_write32(rst_int_vec, 0xffffffff,
                                  bar_base_addr + TAWK_IPC_PCIE_HDR_INT_VEC_RST_REG);
    if (rc != 0)
      goto fail0;

    rc = ipc_pcie_bar_hdr_write32(mbox_int_vec, 0xffffffff,
                                  bar_base_addr + TAWK_IPC_PCIE_HDR_INT_VEC_MBOX_REG);
    if (rc != 0)
      goto fail0;

    IPC_PCIE_BAR_HDR_FIELD_SET(ctl, CTL_INTERRUPT_EN, 1);
    rc = ipc_pcie_bar_hdr_write32(ctl, IPC_PCIE_BAR_HDR_MASK(CTL_INTERRUPT_EN),
                                  bar_base_addr + TAWK_IPC_PCIE_HDR_CTL_REG);
    if (rc != 0)
      goto fail0;
  }

  return 0;

fail0:
  fini_interrupts_fn(fn_ctx);
  return rc;
}

static void ipc_pcie_bar_hdr_fini(mm_reg_t bar_base_addr)
{
  uint32_t ctl = sys_read32(bar_base_addr + TAWK_IPC_PCIE_HDR_CTL_REG);
  IPC_PCIE_BAR_HDR_FIELD_SET(ctl, CTL_INTERRUPT_EN, 0);
  sys_write32(ctl, bar_base_addr + TAWK_IPC_PCIE_HDR_CTL_REG);
}


int ipc_device_init_pcie_host(ipc_context_t *ipc,
                              mm_reg_t bar_base_addr,
#if IPC_WITH_POLLING
                              bool poll,
#endif
                              bool *interrupts,
                              int rst_int_vec,
                              int mbox_int_vec,
                              ipc_device_init_interrupts_fn init_interrupts_fn,
                              ipc_device_fini_interrupts_fn fini_interrupts_fn,
                              void *fn_ctx,
                              struct device *dev)
{
  int rc;

  int peer_id;
  mm_reg_t rst_sigs_out_addr;
  mm_reg_t rst_sigs_in_addr;
#if IPC_WITH_POLLING
  k_timeout_t rst_poll_interval;
#endif
  ipc_device_notify_peer_spec_t rst_db;
  unsigned mbox_count;
  mm_reg_t mbox_control_base;
  unsigned mbox_control_stride_log2;
  mm_reg_t mbox_buffer_base;
  unsigned mbox_buffer_stride_log2;
  size_t mbox_buffer_size;
#if IPC_WITH_POLLING
  k_timeout_t mbox_poll_interval;
#endif
  ipc_device_notify_peer_spec_t mbox_db;

  rc = ipc_pcie_bar_hdr_parse_init(bar_base_addr,
                                   &peer_id,
                                   interrupts,
                                   &rst_sigs_out_addr,
                                   &rst_sigs_in_addr,
                                   &rst_db,
                                   rst_int_vec,
                                   &mbox_count,
                                   &mbox_control_base,
                                   &mbox_control_stride_log2,
                                   &mbox_buffer_base,
                                   &mbox_buffer_stride_log2,
                                   &mbox_buffer_size,
                                   &mbox_db,
                                   mbox_int_vec,
                                   init_interrupts_fn,
                                   fini_interrupts_fn,
                                   fn_ctx);
  if (rc != 0)
    goto fail0;

#if IPC_WITH_POLLING
  if (poll && !*interrupts) {
    rst_poll_interval = IPC_DEVICE_RST_POLL_INTERVAL;
    mbox_poll_interval = IPC_DEVICE_MBOX_POLL_INTERVAL;
  } else {
    rst_poll_interval = K_FOREVER;
    mbox_poll_interval = K_FOREVER;
  }
#endif

  rc = ipc_device_init(ipc,
                       peer_id,
                       /* own_mem = */false,
                       rst_sigs_out_addr,
                       rst_sigs_in_addr,
#if IPC_WITH_POLLING
                       rst_poll_interval,
#endif
                       rst_db,
                       mbox_count,
                       mbox_control_base,
                       mbox_control_stride_log2,
#ifdef CONFIG_TAWK_IPC_CACHE_MANAGEMENT
                       /* mbox_buffer_cached = */false,
#endif
                       mbox_buffer_base,
                       mbox_buffer_stride_log2,
                       mbox_buffer_size,
#if IPC_WITH_POLLING
                       mbox_poll_interval,
#endif
                       *interrupts ? fini_interrupts_fn : NULL,
                       fn_ctx,
                       dev,
                       mbox_db);

  if (rc != 0)
    goto fail1;

  return 0;


 fail1:
  ipc_pcie_bar_hdr_fini(bar_base_addr);

 fail0:
  return rc;
}

#else

#error "Who am I"

#endif
