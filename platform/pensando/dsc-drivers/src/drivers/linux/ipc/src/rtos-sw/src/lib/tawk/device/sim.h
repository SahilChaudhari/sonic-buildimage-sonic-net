// SPDX-License-Identifier: MIT or GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#pragma once

typedef struct ipc_device_sim_ctx_s {
#if defined(RTOS)
  ipc_device_sim_config_t cfg;
#elif defined(IPC_BUILD_KERNEL_MODULE)
  int dummy;
#else
#error "Who am I"
#endif
} ipc_device_sim_ctx_t;

// just to make device.c compilation pass
#ifndef IRQ_DEFAULT_PRIORITY
#define IRQ_DEFAULT_PRIORITY 0
#endif
#ifndef IRQ_TYPE_EDGE
#define IRQ_TYPE_EDGE        0
#endif
