// SPDX-License-Identifier: MIT or GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#if defined(IPC_BUILD_KERNEL_MODULE)
#include <linux/slab.h>
#endif

#include <zephyr/kernel.h>

#include <tawk/ipc.h>
#include <tawk/platform.h>

#include "../ipc_internal.h"

#include "platform.h"


#if defined(RTOS) && !defined(CONFIG_BOARD_SIM)
static ipc_platform_dt_wrapper_t *ipc_platform_dt_wrapper(ipc_context_t *ipc)
{
  return CONTAINER_OF(ipc, ipc_platform_dt_wrapper_t, ipc);
}

const struct device *ipc_platform_dt_device(ipc_context_t *ipc)
{
  ipc_platform_dt_wrapper_t *ipcw = ipc_platform_dt_wrapper(ipc);
  return ipcw->d;
}

int ipc_platform_dt_wrapper_init(const struct device *dev)
{
  ipc_platform_dt_wrapper_t *ipcw = dev->data;
  (void) ipcw;
  return 0;
}

#elif defined(IPC_BUILD_KERNEL_MODULE)
/* kmalloc */
#endif

#if defined(RTOS)
ipc_context_t *ipc_get(const struct device *dev)
{
  ipc_platform_dt_wrapper_t *ipcw = dev->data;
  return &ipcw->ipc;
}
#else
ipc_context_t *ipc_alloc(void)
{
  ipc_context_t *ipc;

#if defined(IPC_BUILD_KERNEL_MODULE)
  ipc = kmalloc(sizeof *ipc, GFP_KERNEL);
#else
#error "Who am I"
#endif

  return ipc;
}

void ipc_free(ipc_context_t *ipc)
{
#if defined(IPC_BUILD_KERNEL_MODULE)
  kfree(ipc);
#else
#error "Who am I"
#endif
}
#endif

#if defined(RTOS)
ipc_context_t *ipc_global_context_get(void)
{
#if !defined(CONFIG_BOARD_SIM)
  return ipc_get(DEVICE_DT_GET(DT_CHOSEN(amd_tawkipc)));
#else
  return NULL;
#endif
}
#endif
