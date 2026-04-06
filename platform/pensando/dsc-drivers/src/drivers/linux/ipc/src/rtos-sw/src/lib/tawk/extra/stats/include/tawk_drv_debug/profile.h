// SPDX-License-Identifier: MIT or GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (C) 2025 Advanced Micro Devices, Inc.

#pragma once

#include <zephyr/kernel.h>

#define IPC_PROF_SAMPLE_SET_SIZE_MAX 4096

typedef struct ipc_prof_timepoint_s {
  k_timepoint_t at;

  /* An instance of this type is invalid if delta is K_FOREVER. */
  k_timepoint_t delta;

#ifdef IPC_BUILD_KERNEL_MODULE
  int cpu;
#endif
} ipc_prof_timepoint_t;

typedef void (ipc_prof_fn)(void *fn_ctx);

typedef struct ipc_prof_s {
  ipc_prof_timepoint_t min;
  ipc_prof_timepoint_t max;

  ipc_prof_fn *on_new_max;
  void *fn_ctx;

  ipc_prof_timepoint_t sample_set[IPC_PROF_SAMPLE_SET_SIZE_MAX];
  unsigned sample_set_size, sample_set_i;
} ipc_prof_t;

void ipc_prof_init(ipc_prof_t *p, ipc_prof_fn *on_new_max, void *fn_ctx);

void ipc_prof_begin(ipc_prof_t *p);
void ipc_prof_end(ipc_prof_t *p);

k_timepoint_t ipc_prof_mean(ipc_prof_t *p);
