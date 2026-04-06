// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#pragma once

#include <stdint.h>

typedef struct {
  uint32_t _opaque;
} minstd_rand_t;

static inline void minstd_rand_init(minstd_rand_t *minstd_rand, uint32_t seed)
{
  minstd_rand->_opaque = seed;
}

static inline uint32_t minstd_rand_get(minstd_rand_t *minstd_rand) {
  uint32_t rv = minstd_rand->_opaque;

  minstd_rand->_opaque = ((uint64_t)minstd_rand->_opaque * 48271) % 0x7fffffff;

  return rv;
}


static inline uint32_t mod_ii(uint32_t x, uint32_t min, uint32_t max)
{
  if (max - min + 1 != 0)
    x %= (max - min + 1);
  x += min;

  return x;
}
