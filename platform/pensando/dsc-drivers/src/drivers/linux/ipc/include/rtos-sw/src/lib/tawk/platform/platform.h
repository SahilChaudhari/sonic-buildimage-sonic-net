// SPDX-License-Identifier: MIT or GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#pragma once

#include <tawk/ipc.h>
#if defined(RTOS)
#include <zephyr/device.h>
#include "../ipc_internal.h"
#endif

#if defined(RTOS)
extern const struct device *ipc_platform_dt_device(ipc_context_t *ipc);

typedef struct ipc_platform_dt_wrapper_s {
  const struct device *d;
  ipc_context_t ipc;
} ipc_platform_dt_wrapper_t;

extern int ipc_platform_dt_wrapper_init(const struct device *dev);

#define IPC_PLATFORM_DT_WRAPPER_INIT(node_id_, init_fn_, config_)       \
  extern const struct device DEVICE_DT_NAME_GET(node_id_);              \
  static ipc_platform_dt_wrapper_t ipc_ ## node_id_ = {                 \
    .d =  DEVICE_DT_GET(node_id_),                                      \
  };                                                                    \
  DEVICE_DT_DEFINE(node_id_,                                            \
                   /* init_fn = */(init_fn_),                           \
                   /* pm = */     NULL,                                 \
                   /* data = */   &(ipc_ ## node_id_),                  \
                   /* config = */ (config_),                            \
                   /* level = */  POST_KERNEL,                          \
                   /* prio = */   CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,  \
                   /* api = */    NULL)
#endif
