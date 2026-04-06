// SPDX-License-Identifier: MIT or GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#pragma once

#if defined(RTOS)
#include <zephyr/device.h>
#endif
#include "ipc.h"


#if defined(RTOS)
/* Returns the context; invalid to call until initialised */
extern ipc_context_t *ipc_get(const struct device *dev);
#else
extern ipc_context_t *ipc_alloc(void);
extern void ipc_free(ipc_context_t *ipc);
#endif


#if defined(RTOS)
/* For now, in firmware builds, there's a single IPC stack.  These
 * functions are wrappers around the above get functions that
 * implicitly operate on that singleton stack. */
extern ipc_context_t *ipc_global_context_get(void);
#endif
