// SPDX-License-Identifier: MIT or GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (C) 2025 Advanced Micro Devices, Inc.

#include <tawk_drv_debug/profile.h>

void ipc_prof_init(ipc_prof_t *p, ipc_prof_fn *on_new_max, void *fn_ctx)
{
  memset(p, 0, sizeof(*p));

  p->min.delta = K_FOREVER;

  p->on_new_max = on_new_max;
  p->fn_ctx = fn_ctx;
}

void ipc_prof_begin(ipc_prof_t *p)
{
  ipc_prof_timepoint_t *sample = &p->sample_set[p->sample_set_i];

  sample->at = sys_now();

  /* Invalidate. */
  sample->delta = K_FOREVER;

#ifdef IPC_BUILD_KERNEL_MODULE
  /* The "best effort" to get an unstable CPU number,
   * i.e. without interfering with preemption. */
  sample->cpu = raw_smp_processor_id();
#endif
}

void ipc_prof_end(ipc_prof_t *p)
{
  ipc_prof_timepoint_t *sample = &p->sample_set[p->sample_set_i];

  sample->delta = sys_now() - sample->at;

  if (sample->delta < p->min.delta)
    p->min = *sample;

  if (sample->delta > p->max.delta) {
    p->max = *sample;

    if (p->on_new_max)
      p->on_new_max(p->fn_ctx);
  }

  p->sample_set_i = (p->sample_set_i + 1) % IPC_PROF_SAMPLE_SET_SIZE_MAX;
  if (p->sample_set_size < IPC_PROF_SAMPLE_SET_SIZE_MAX)
    ++p->sample_set_size;
}

k_timepoint_t ipc_prof_mean(ipc_prof_t *p)
{
  k_timepoint_t res = 0;
  unsigned n = 0;

  for (int b = 0; b < p->sample_set_size; b++) {
    ipc_prof_timepoint_t *sample = &p->sample_set[b];
    if (sample->delta == K_FOREVER)
      continue;

    res = res + sample->delta;
    n = n + 1;
  }

  if (n > 0)
    res = res / n;

  return res;
}
