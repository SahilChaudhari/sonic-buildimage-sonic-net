// SPDX-License-Identifier: MIT or GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#include <stdio.h>
#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
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

#include "sim.h"


#if defined(RTOS) && !defined(CONFIG_BOARD_SIM)
ipc_device_ctx_t *ipc_sim_device(ipc_device_sim_ctx_t *sim)
{
  /* This upcasting is ugly, but I'm not sure how this bit
   * of the code should look, so it feels fine for now. */
  return CONTAINER_OF(sim, ipc_device_ctx_t, u.sim);
}

typedef struct ipc_sim_dt_config_s {
  uintptr_t mem_addr;
  const char *shm_hdl;
  size_t shm_size;

} ipc_sim_dt_config_t;

static void ipc_sim_cfg_init(ipc_device_sim_ctx_t *sim,
                            const ipc_sim_dt_config_t *dt_cfg
                            );

static int ipc_device_init_sim_dev(ipc_context_t *ipc,
                                  const ipc_sim_dt_config_t *dt_cfg)
{
  ipc_device_ctx_t *dev = &ipc->dev;
  ipc_device_sim_ctx_t *sim = &dev->u.sim;
  int rc;

  static const unsigned rstsigs_db_irq = IPC_DEVICE_DB_IRQ_NONE;
  static const unsigned mbox_db_irq = IPC_DEVICE_DB_IRQ_NONE;

  BUILD_ASSERT(IPC_PLATFORM_RSTSIGS_OUT_OFFS_EP < IPC_MM_MEM_SIZE);
  BUILD_ASSERT(IPC_PLATFORM_RSTSIGS_IN_OFFS_EP < IPC_MM_MEM_SIZE);
  BUILD_ASSERT(IPC_PLATFORM_MBOX_CONTROL_OFFS < IPC_MM_MEM_SIZE);
  BUILD_ASSERT((IPC_PLATFORM_MBOX_CONTROL_OFFS +
                (IPC_PLATFORM_MBOX_COUNT << IPC_PLATFORM_MBOX_CONTROL_STRIDE_LOG2)) < IPC_MM_MEM_SIZE);
  BUILD_ASSERT(IPC_PLATFORM_MBOX_BUFFER_OFFS < IPC_MM_MEM_SIZE);
  BUILD_ASSERT((IPC_PLATFORM_MBOX_BUFFER_OFFS +
                (IPC_PLATFORM_MBOX_COUNT << IPC_PLATFORM_MBOX_BUFFER_STRIDE_LOG2)) < IPC_MM_MEM_SIZE);

  ipc_sim_cfg_init(sim, dt_cfg);

  mm_reg_t base_addr;
#if IPC_PS_11703_WORKAROUND
  base_addr = dt_cfg->mem_addr;
#else
  device_map(&base_addr, dt_cfg->mem_addr, IPC_MM_MEM_SIZE, K_MEM_CACHE_NONE);
#endif

  rc = ipc_device_init(ipc,
                       IPC_PLATFORM_PEER_ID_EP,
                       /* own_mem = */true,
                       base_addr + IPC_PLATFORM_RSTSIGS_OUT_OFFS_EP,
                       base_addr + IPC_PLATFORM_RSTSIGS_IN_OFFS_EP,
#if IPC_WITH_POLLING
                       IPC_DEVICE_RST_POLL_INTERVAL,
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
                       IPC_DEVICE_RST_POLL_INTERVAL,
#endif
                       mbox_db_irq,
                       IPC_DEVICE_NOTIFY_PEER_SPEC_NONE);

  if (rc != 0)
    goto fail0;

  return 0;


 fail0:
  return rc;
}

const ipc_device_sim_config_t *ipc_device_sim_config(ipc_context_t *ipc)
{
  ipc_device_ctx_t *dev = &ipc->dev;
  ipc_device_sim_ctx_t *sim = &dev->u.sim;
  return &sim->cfg;
}

static void ipc_sim_cfg_init(ipc_device_sim_ctx_t *sim,
                            const ipc_sim_dt_config_t *dt_cfg)
{
  ipc_device_sim_config_t *cfg = &sim->cfg;

  cfg->remote_peer_id = IPC_PLATFORM_PEER_ID_HOST;
  cfg->db_pa = UINTPTR_MAX;
  cfg->db_sz = 0;
  cfg->db_rst_offs = SIZE_MAX;
  cfg->db_rst_val = 0xdeadbeef;
  cfg->db_mbox_offs = SIZE_MAX;
  cfg->db_mbox_val = 0xdeadbeef;
  cfg->msix_pa = UINTPTR_MAX;
  cfg->msix_sz = 0;

  cfg->msix_mbox_offs = SIZE_MAX;
  cfg->msix_rst_offs = SIZE_MAX;

  cfg->rstsigs_pa = dt_cfg->mem_addr + MIN(IPC_PLATFORM_RSTSIGS_OUT_OFFS(IPC_PLATFORM_PEER_ID_HOST),
                                                IPC_PLATFORM_RSTSIGS_IN_OFFS(IPC_PLATFORM_PEER_ID_HOST));
  cfg->rstsigs_sz = 8;
  cfg->rstsigs_out_offs = dt_cfg->mem_addr + IPC_PLATFORM_RSTSIGS_OUT_OFFS(IPC_PLATFORM_PEER_ID_HOST) - cfg->rstsigs_pa;
  cfg->rstsigs_in_offs = dt_cfg->mem_addr + IPC_PLATFORM_RSTSIGS_IN_OFFS(IPC_PLATFORM_PEER_ID_HOST) - cfg->rstsigs_pa;

  cfg->mbox_count = IPC_PLATFORM_MBOX_COUNT;

  cfg->mbox_control_pa = dt_cfg->mem_addr + IPC_PLATFORM_MBOX_CONTROL_OFFS;
  cfg->mbox_control_sz = (1U << IPC_PLATFORM_MBOX_CONTROL_STRIDE_LOG2) * IPC_PLATFORM_MBOX_COUNT;
  cfg->mbox_control_offs = 0;
  cfg->mbox_control_stride_log2 = IPC_PLATFORM_MBOX_CONTROL_STRIDE_LOG2;

  cfg->mbox_buffer_pa = dt_cfg->mem_addr + IPC_PLATFORM_MBOX_BUFFER_OFFS;
  cfg->mbox_buffer_sz = (1U << IPC_PLATFORM_MBOX_BUFFER_STRIDE_LOG2) * IPC_PLATFORM_MBOX_COUNT;
  cfg->mbox_buffer_offs = 0;
  cfg->mbox_buffer_stride_log2 = IPC_PLATFORM_MBOX_BUFFER_STRIDE_LOG2;
  cfg->mbox_buffer_size = IPC_PLATFORM_MBOX_BUFFER_SIZE;

}

static uintptr_t
shared_mem_init (const char *name, size_t size)
{
    int fd;
    struct stat st;
    mode_t old_umask;
    uintptr_t shared_mem_addr;

    old_umask = umask(0);
    fd = shm_open(name, O_RDWR | O_CREAT, 0666);
    if (fd == -1) {
        printk("open %s failed: %s:%s:%d, %s\n",
                name, __FILE__, __FUNCTION__, __LINE__,
                strerror(errno));
    }
    assert(fd != -1);
    umask(old_umask);

    fstat(fd, &st);
    assert(st.st_size == 0 || st.st_size == size);
    assert(ftruncate(fd, size) == 0);

    shared_mem_addr = (uintptr_t)mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    assert(shared_mem_addr);
    return shared_mem_addr;
}

__attribute__((unused))
static int ipc_sim_dt_wrapper_init(const struct device *dev)
{
  const ipc_sim_dt_config_t *dt_cfg = dev->config;
  ipc_context_t *ipc = ipc_get(dev);
  int rc;

  ((ipc_sim_dt_config_t *)dev->config)->mem_addr = shared_mem_init(dt_cfg->shm_hdl, dt_cfg->shm_size);

  rc = ipc_device_init_sim_dev(ipc, dt_cfg);
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

#define TAWK_IPC_SIM_DEV_INIT(node_id_)                               \
  static ipc_sim_dt_config_t                                          \
  ipc_sim_cfg_ ## node_id_ = {                                        \
    .shm_hdl = DT_PROP(node_id_, shm),                                \
    .shm_size = DT_PROP(node_id_, shm_size),                          \
  };                                                                  \
  IPC_PLATFORM_DT_WRAPPER_INIT(node_id_,                              \
                               ipc_sim_dt_wrapper_init,               \
                               &(ipc_sim_cfg_ ## node_id_))

DT_FOREACH_STATUS_OKAY(amd_tawk_ipc_sim_dev, TAWK_IPC_SIM_DEV_INIT);

#elif defined(IPC_BUILD_KERNEL_MODULE)

int ipc_device_init_sim_host(ipc_context_t *ipc,
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
                            uint32_t mbox_db_val)
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
                       mbox_db_exists ?
                       IPC_DEVICE_NOTIFY_PEER_SPEC_DB(mbox_db_addr, mbox_db_val) :
                       IPC_DEVICE_NOTIFY_PEER_SPEC_NONE);

  if (rc != 0)
    goto fail0;

  return 0;


 fail0:
  return rc;
}
#endif
