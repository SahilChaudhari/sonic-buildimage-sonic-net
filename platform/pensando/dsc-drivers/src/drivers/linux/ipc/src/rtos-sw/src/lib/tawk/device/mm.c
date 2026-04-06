// SPDX-License-Identifier: MIT or GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#include <zephyr/kernel.h>

#if defined(RTOS)
#include <zephyr/sys/device_mmio.h>
#include <string.h>
#endif

#include <tawk/ipc.h>
#include <tawk/device.h>

#if defined(CONFIG_TAWK_IPC_WITH_INTR)
#include <intrutils.h>
#endif

#include "../platform/platform.h"
#include "../ipc_internal.h"
#include "device.h"
#include "mem_layout.h"

#include "mm.h"


#if defined(RTOS)
/* For PCIe, this is programmable via the
 * BAR header.  For MM devices, it's
 * hardcoded here and exposed to the
 * driver via device tree. */
#define IPC_DEVICE_MM_MBOX_INTR (0)
#define IPC_DEVICE_MM_RST_INTR (1)
#endif

#if defined(RTOS)
ipc_device_ctx_t *ipc_mm_device(ipc_device_mm_ctx_t *mm)
{
  /* This upcasting is ugly, but I'm not sure how this bit
   * of the code should look, so it feels fine for now. */
  return CONTAINER_OF(mm, ipc_device_ctx_t, u.mm);
}
#endif


#if defined(RTOS)

typedef struct ipc_mm_dt_config_s {
#if defined(CONFIG_TAWK_IPC_WITH_INTR)
  unsigned intrb;
  unsigned intrc;
#endif

  uintptr_t mem_phys_addr;
  size_t mem_size; /* At least IPC_MM_MEM_SIZE */

#if defined(CONFIG_TAWK_IPC_WITH_INTR)
  unsigned db_intrb;
  unsigned db_intrc; /* In then range [0,IPC_MM_BAR_DB_IRQ_COUNT] */
#endif
} ipc_mm_dt_config_t;

static void ipc_mm_cfg_init(ipc_device_mm_ctx_t *mm,
                            const ipc_mm_dt_config_t *dt_cfg
#if defined(CONFIG_TAWK_IPC_WITH_INTR)
                            , const uint64_t db_addr[IPC_PCIE_BAR_DB_IRQ_COUNT],
                            const uint32_t db_data[IPC_PCIE_BAR_DB_IRQ_COUNT]
#endif
                            );

static int ipc_device_init_mm_dev(ipc_context_t *ipc,
                                  const ipc_mm_dt_config_t *dt_cfg)
{
  ipc_device_ctx_t *dev = &ipc->dev;
  ipc_device_mm_ctx_t *mm = &dev->u.mm;
  int rc;

#if defined(CONFIG_TAWK_IPC_WITH_INTR)
  unsigned db_irq[IPC_MM_DB_IRQ_COUNT];
  uint64_t db_addr[IPC_MM_DB_IRQ_COUNT];
  uint32_t db_data[IPC_MM_DB_IRQ_COUNT];

  for (unsigned i = 0; i < IPC_MM_DB_IRQ_COUNT; i++) {
    if (i < dt_cfg->db_intrc) {
      int rc = ipc_device_db_via_intr(dt_cfg->db_intrb + i, &db_irq[i], &db_addr[i], &db_data[i]);
      if (rc != 0)
        goto fail0;
    } else {
      db_irq[i] = IPC_DEVICE_DB_IRQ_NONE;
    }
  }

  BUILD_ASSERT(IPC_PLATFORM_RST_DB < IPC_MM_DB_IRQ_COUNT);
  BUILD_ASSERT(IPC_PLATFORM_MBOX_DB < IPC_MM_DB_IRQ_COUNT);

  unsigned rstsigs_db_irq = db_irq[IPC_PLATFORM_RST_DB];
  unsigned mbox_db_irq = db_irq[IPC_PLATFORM_MBOX_DB];

  for (unsigned i = 0; i < dt_cfg->intrc; i++) {
    intr_config_local_msi(dt_cfg->intrb + i, 0x0, 0x0);
    /* Start masked so that we don't attempt to raise interrupts
     * before the host has configured address/data. */
    intr_msixcfg(dt_cfg->intrb + i, 0x0, 0x0, 1);
  }
#else
  static const unsigned rstsigs_db_irq = IPC_DEVICE_DB_IRQ_NONE;
  static const unsigned mbox_db_irq = IPC_DEVICE_DB_IRQ_NONE;
#endif

  BUILD_ASSERT(IPC_PLATFORM_RSTSIGS_OUT_OFFS_EP < IPC_MM_MEM_SIZE);
  BUILD_ASSERT(IPC_PLATFORM_RSTSIGS_IN_OFFS_EP < IPC_MM_MEM_SIZE);
  BUILD_ASSERT(IPC_PLATFORM_MBOX_CONTROL_OFFS < IPC_MM_MEM_SIZE);
  BUILD_ASSERT((IPC_PLATFORM_MBOX_CONTROL_OFFS +
                (IPC_PLATFORM_MBOX_COUNT << IPC_PLATFORM_MBOX_CONTROL_STRIDE_LOG2)) < IPC_MM_MEM_SIZE);
  BUILD_ASSERT(IPC_PLATFORM_MBOX_BUFFER_OFFS < IPC_MM_MEM_SIZE);
  BUILD_ASSERT((IPC_PLATFORM_MBOX_BUFFER_OFFS +
                (IPC_PLATFORM_MBOX_COUNT << IPC_PLATFORM_MBOX_BUFFER_STRIDE_LOG2)) < IPC_MM_MEM_SIZE);

  ipc_mm_cfg_init(mm, dt_cfg
#if defined(CONFIG_TAWK_IPC_WITH_INTR)
                  , db_addr, db_data
#endif
                  );

  mm_reg_t base_addr;
#if IPC_PS_11703_WORKAROUND
  base_addr = dt_cfg->mem_phys_addr;
#else
  device_map(&base_addr, dt_cfg->mem_phys_addr, IPC_MM_MEM_SIZE, K_MEM_CACHE_NONE);
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
#if defined(CONFIG_TAWK_IPC_WITH_INTR)
                       (dt_cfg->intrc > IPC_DEVICE_MM_MBOX_INTR) ?
                       IPC_DEVICE_NOTIFY_PEER_SPEC_INTR(dt_cfg->intrb + IPC_DEVICE_MM_MBOX_INTR) :
#endif
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
#if defined(CONFIG_TAWK_IPC_WITH_INTR)
                       (dt_cfg->intrc > IPC_DEVICE_MM_RST_INTR) ?
                       IPC_DEVICE_NOTIFY_PEER_SPEC_INTR(dt_cfg->intrb + IPC_DEVICE_MM_RST_INTR) :
#endif
                       IPC_DEVICE_NOTIFY_PEER_SPEC_NONE);

  if (rc != 0)
    goto fail0;

  return 0;


 fail0:
  return rc;
}

const ipc_device_mm_config_t *ipc_device_mm_config(ipc_context_t *ipc)
{
  ipc_device_ctx_t *dev = &ipc->dev;
  ipc_device_mm_ctx_t *mm = &dev->u.mm;
  return &mm->cfg;
}

static void ipc_mm_emit_host_dts(const ipc_device_mm_config_t *cfg)
{
#define CELLS_OFFS_PRI_FMT "0x%lx"
#define CELLS_OFFS_PRI_ARGS(x_)  (unsigned long)(x_)

#define CELLS_SIZE_PRI_FMT "0x%lx 0x%lx"
#define CELLS_SIZE_PRI_ARGS(x_)  (unsigned long)((x_) >> 32), (unsigned long)((x_) & 0xffffffff)

#define CELLS_ADDR_PRI_FMT "0x%lx 0x%lx"
#define CELLS_ADDR_PRI_ARGS(x_)  (unsigned long)((x_) >> 32), (unsigned long)((x_) & 0xffffffff)

  printk("tawkipc {\n");
  printk("\tcompatible = \"amd,tawk-ipc\";\n");
  printk("\tpeer-id = <%u>;\n", cfg->remote_peer_id);
  printk("\t#address-cells = <2>;\n");
  printk("\t#size-cells = <1>;\n");

  printk("\treg = <" CELLS_ADDR_PRI_FMT " " CELLS_SIZE_PRI_FMT ">,\t/* doorbells */\n",
         CELLS_ADDR_PRI_ARGS(cfg->db_pa != UINTPTR_MAX ? cfg->db_pa : 0x0),
         CELLS_SIZE_PRI_ARGS(cfg->db_pa != UINTPTR_MAX ? cfg->db_sz : 0x0));

  printk("\t      <" CELLS_ADDR_PRI_FMT " " CELLS_SIZE_PRI_FMT ">,\t/* msix-like table */\n",
         CELLS_ADDR_PRI_ARGS(cfg->msix_pa != UINTPTR_MAX ? cfg->msix_pa : 0x0),
         CELLS_SIZE_PRI_ARGS(cfg->msix_pa != UINTPTR_MAX ? cfg->msix_sz : 0x0));

  printk("\t      <" CELLS_ADDR_PRI_FMT " " CELLS_SIZE_PRI_FMT ">,\t/* reset signal registers */\n",
         CELLS_ADDR_PRI_ARGS(cfg->rstsigs_pa != UINTPTR_MAX ? cfg->rstsigs_pa : 0x0),
         CELLS_SIZE_PRI_ARGS(cfg->rstsigs_pa != UINTPTR_MAX ? cfg->rstsigs_sz : 0x0));

  printk("\t      <" CELLS_ADDR_PRI_FMT " " CELLS_SIZE_PRI_FMT ">,\t/* mailbox control registers */\n",
         CELLS_ADDR_PRI_ARGS(cfg->mbox_control_pa != UINTPTR_MAX ? cfg->mbox_control_pa : 0x0),
         CELLS_SIZE_PRI_ARGS(cfg->mbox_control_pa != UINTPTR_MAX ? cfg->mbox_control_sz : 0x0));

  printk("\t      <" CELLS_ADDR_PRI_FMT " " CELLS_SIZE_PRI_FMT ">;\t/* mailbox buffers */\n",
         CELLS_ADDR_PRI_ARGS(cfg->mbox_buffer_pa != UINTPTR_MAX ? cfg->mbox_buffer_pa : 0x0),
         CELLS_SIZE_PRI_ARGS(cfg->mbox_buffer_pa != UINTPTR_MAX ? cfg->mbox_buffer_sz : 0x0));

  if (cfg->db_rst_offs != SIZE_MAX)
    printk("\trst-db = <" CELLS_OFFS_PRI_FMT " 0x%lx>;\n",
           CELLS_OFFS_PRI_ARGS(cfg->db_rst_offs), (unsigned long)cfg->db_rst_val);

  if (cfg->db_mbox_offs != SIZE_MAX)
    printk("\tmbox-db = <" CELLS_OFFS_PRI_FMT " 0x%lx>;\n",
           CELLS_OFFS_PRI_ARGS(cfg->db_mbox_offs), (unsigned long)cfg->db_mbox_val);

  if (cfg->msix_rst_offs != SIZE_MAX)
    printk("\trst-msix = <" CELLS_OFFS_PRI_FMT ">;\n",
           CELLS_OFFS_PRI_ARGS(cfg->msix_rst_offs));

  if (cfg->msix_mbox_offs != SIZE_MAX)
    printk("\tmbox-msix = <" CELLS_OFFS_PRI_FMT ">;\n",
           CELLS_OFFS_PRI_ARGS(cfg->msix_mbox_offs));

  printk("\trst-sigs = <" CELLS_OFFS_PRI_FMT " " CELLS_OFFS_PRI_FMT ">;\n",
         CELLS_OFFS_PRI_ARGS(cfg->rstsigs_out_offs),
         CELLS_OFFS_PRI_ARGS(cfg->rstsigs_in_offs));

  printk("\tnmboxes = <%zu>;\n", cfg->mbox_count);

  printk("\tmbox-ctl = <" CELLS_OFFS_PRI_FMT " 0x%x>;\n",
         CELLS_OFFS_PRI_ARGS(cfg->mbox_control_offs),
         cfg->mbox_control_stride_log2);

  printk("\tmbox-buf = <" CELLS_OFFS_PRI_FMT " 0x%x 0x%zx>;\n",
         CELLS_OFFS_PRI_ARGS(cfg->mbox_buffer_offs),
         cfg->mbox_buffer_stride_log2,
         cfg->mbox_buffer_size);

  printk("};\n");

}
static void ipc_mm_cfg_init(ipc_device_mm_ctx_t *mm,
                            const ipc_mm_dt_config_t *dt_cfg
#if defined(CONFIG_TAWK_IPC_WITH_INTR)
                            , const uint64_t db_addr[IPC_PCIE_BAR_DB_IRQ_COUNT],
                            const uint32_t db_data[IPC_PCIE_BAR_DB_IRQ_COUNT]
#endif
                            )
{
  ipc_device_mm_config_t *cfg = &mm->cfg;

  cfg->remote_peer_id = IPC_PLATFORM_PEER_ID_HOST;

#if defined(CONFIG_TAWK_IPC_WITH_INTR)
  if (dt_cfg->db_intrc > 0) {
    cfg->db_pa = db_addr[0];
    cfg->db_sz = (db_addr[1] - db_addr[0]) * dt_cfg->db_intrc;
  } else
#endif
  {
    cfg->db_pa = UINTPTR_MAX;
    cfg->db_sz = 0;
  }

#if defined(CONFIG_TAWK_IPC_WITH_INTR)
  if (dt_cfg->db_intrc > IPC_PLATFORM_RST_DB) {
    cfg->db_rst_offs = db_addr[IPC_PLATFORM_RST_DB] - cfg->db_pa;
    cfg->db_rst_val = db_data[IPC_PLATFORM_RST_DB];
  } else
#endif
  {
    cfg->db_rst_offs = SIZE_MAX;
    cfg->db_rst_val = 0xdeadbeef;
  }

#if defined(CONFIG_TAWK_IPC_WITH_INTR)
  if (dt_cfg->db_intrc > IPC_PLATFORM_MBOX_DB) {
    cfg->db_mbox_offs = db_addr[IPC_PLATFORM_MBOX_DB] - cfg->db_pa;
    cfg->db_mbox_val = db_data[IPC_PLATFORM_MBOX_DB];
  } else
#endif
  {
    cfg->db_mbox_offs = SIZE_MAX;
    cfg->db_mbox_val = 0xdeadbeef;
  }

#if defined(CONFIG_TAWK_IPC_WITH_INTR)
  if (dt_cfg->intrc > 0) {
    cfg->msix_pa = intr_msixcfg_addr(dt_cfg->intrb);
    cfg->msix_sz = intr_msixcfg_size(dt_cfg->intrc);
  } else
#endif
  {
    cfg->msix_pa = UINTPTR_MAX;
    cfg->msix_sz = 0;
  }

#if defined(CONFIG_TAWK_IPC_WITH_INTR)
  if (dt_cfg->intrc > IPC_DEVICE_MM_MBOX_INTR)
    cfg->msix_mbox_offs = intr_msixcfg_addr(dt_cfg->intrb + IPC_DEVICE_MM_MBOX_INTR) - cfg->msix_pa;
  else
#endif
    cfg->msix_mbox_offs = SIZE_MAX;

#if defined(CONFIG_TAWK_IPC_WITH_INTR)
  if (dt_cfg->intrc > IPC_DEVICE_MM_RST_INTR)
    cfg->msix_rst_offs = intr_msixcfg_addr(dt_cfg->intrb + IPC_DEVICE_MM_RST_INTR) - cfg->msix_pa;
  else
#endif
    cfg->msix_rst_offs = SIZE_MAX;

  cfg->rstsigs_pa = dt_cfg->mem_phys_addr + MIN(IPC_PLATFORM_RSTSIGS_OUT_OFFS(IPC_PLATFORM_PEER_ID_HOST),
                                                IPC_PLATFORM_RSTSIGS_IN_OFFS(IPC_PLATFORM_PEER_ID_HOST));
  cfg->rstsigs_sz = 8;
  cfg->rstsigs_out_offs = dt_cfg->mem_phys_addr + IPC_PLATFORM_RSTSIGS_OUT_OFFS(IPC_PLATFORM_PEER_ID_HOST) - cfg->rstsigs_pa;
  cfg->rstsigs_in_offs = dt_cfg->mem_phys_addr + IPC_PLATFORM_RSTSIGS_IN_OFFS(IPC_PLATFORM_PEER_ID_HOST) - cfg->rstsigs_pa;

  cfg->mbox_count = IPC_PLATFORM_MBOX_COUNT;

  cfg->mbox_control_pa = dt_cfg->mem_phys_addr + IPC_PLATFORM_MBOX_CONTROL_OFFS;
  cfg->mbox_control_sz = (1U << IPC_PLATFORM_MBOX_CONTROL_STRIDE_LOG2) * IPC_PLATFORM_MBOX_COUNT;
  cfg->mbox_control_offs = 0;
  cfg->mbox_control_stride_log2 = IPC_PLATFORM_MBOX_CONTROL_STRIDE_LOG2;

  cfg->mbox_buffer_pa = dt_cfg->mem_phys_addr + IPC_PLATFORM_MBOX_BUFFER_OFFS;
  cfg->mbox_buffer_sz = (1U << IPC_PLATFORM_MBOX_BUFFER_STRIDE_LOG2) * IPC_PLATFORM_MBOX_COUNT;
  cfg->mbox_buffer_offs = 0;
  cfg->mbox_buffer_stride_log2 = IPC_PLATFORM_MBOX_BUFFER_STRIDE_LOG2;
  cfg->mbox_buffer_size = IPC_PLATFORM_MBOX_BUFFER_SIZE;

  if (0)
    ipc_mm_emit_host_dts(cfg);
}


__attribute__((unused))
static int ipc_mm_dt_wrapper_init(const struct device *dev)
{
  const ipc_mm_dt_config_t *dt_cfg = dev->config;
  ipc_context_t *ipc = ipc_get(dev);

  int rc;

  rc = ipc_device_init_mm_dev(ipc, dt_cfg);
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
#define TAWK_IPC_MM_INCLUDE_FOR_INTR(...) __VA_ARGS__
#else
#define TAWK_IPC_MM_INCLUDE_FOR_INTR(...)
#endif

#define TAWK_IPC_MM_DEV_INIT(node_id_)                                \
  BUILD_ASSERT(DT_REG_SIZE(DT_PROP(node_id_, mem)) >=                 \
               IPC_MM_MEM_SIZE);                                      \
  BUILD_ASSERT(DT_PROP_BY_IDX(node_id_, db_intrs, 1) <=               \
               IPC_MM_DB_IRQ_COUNT);                                  \
  static const ipc_mm_dt_config_t                                     \
  ipc_mm_cfg_ ## node_id_ = {                                         \
TAWK_IPC_MM_INCLUDE_FOR_INTR(                                         \
    .intrb = DT_PROP_BY_IDX(node_id_, intrs, 0),                      \
    .intrc = DT_PROP_BY_IDX(node_id_, intrs, 1),                      \
)                                                                     \
    .mem_phys_addr = DT_REG_ADDR(DT_PROP(node_id_, mem)),             \
    .mem_size = DT_REG_SIZE(DT_PROP(node_id_, mem)),                  \
TAWK_IPC_MM_INCLUDE_FOR_INTR(                                         \
    .db_intrb = DT_PROP_BY_IDX(node_id_, db_intrs, 0),                \
    .db_intrc = DT_PROP_BY_IDX(node_id_, db_intrs, 1),                \
)                                                                     \
  };                                                                  \
  IPC_PLATFORM_DT_WRAPPER_INIT(node_id_,                              \
                               ipc_mm_dt_wrapper_init,                \
                               &(ipc_mm_cfg_ ## node_id_))

DT_FOREACH_STATUS_OKAY(amd_tawk_ipc_mm_dev, TAWK_IPC_MM_DEV_INIT);

#elif defined(IPC_BUILD_KERNEL_MODULE)

int ipc_device_init_mm_host(ipc_context_t *ipc,
                            int peer_id,
#if IPC_WITH_POLLING
                            bool poll,
#endif
                            mm_reg_t rst_sigs_out_addr,
                            mm_reg_t rst_sigs_in_addr,
                            bool rst_db_exists,
                            mm_reg_t rst_db_addr,
                            uint32_t rst_db_val,
                            unsigned mbox_count,
                            mm_reg_t mbox_control_base,
                            unsigned mbox_control_stride_log2,
                            mm_reg_t mbox_buffer_base,
                            unsigned mbox_buffer_stride_log2,
                            size_t mbox_buffer_size,
                            bool mbox_db_exists,
                            mm_reg_t mbox_db_addr,
                            uint32_t mbox_db_val,
                            struct device *dev)
{
  int rc;

  rc = ipc_device_init(ipc,
                       peer_id,
                       /* own_mem = */false,
                       rst_sigs_out_addr,
                       rst_sigs_in_addr,
#if IPC_WITH_POLLING
                       poll ? IPC_DEVICE_RST_POLL_INTERVAL : K_FOREVER,
#endif
                       rst_db_exists ?
                       IPC_DEVICE_NOTIFY_PEER_SPEC_DB(rst_db_addr, rst_db_val) :
                       IPC_DEVICE_NOTIFY_PEER_SPEC_NONE,
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
                       poll ? IPC_DEVICE_MBOX_POLL_INTERVAL : K_FOREVER,
#endif
                       /* Interrupt disable callback + context */ NULL, NULL,
                       dev,
                       mbox_db_exists ?
                       IPC_DEVICE_NOTIFY_PEER_SPEC_DB(mbox_db_addr, mbox_db_val) :
                       IPC_DEVICE_NOTIFY_PEER_SPEC_NONE);

  if (rc != 0)
    goto fail0;

  return 0;


 fail0:
  return rc;
}

#else

#error "Who am I"

#endif
