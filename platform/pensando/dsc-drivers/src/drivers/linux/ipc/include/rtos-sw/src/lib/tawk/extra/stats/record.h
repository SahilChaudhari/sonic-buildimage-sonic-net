// SPDX-License-Identifier: MIT or GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#pragma once

#include <linux/atomic.h>

struct ipc_debug_record {
	spinlock_t lock;
	atomic_t count;
	uint64_t min;
	uint64_t min_ts;
	uint64_t max;
	uint64_t max_ts;
	uint64_t avg;
};

struct ipc_debug_entry {
	struct ipc_debug_record read_record;
	struct ipc_debug_record write_record;
};

static inline void ipc_debug_set_counter(struct ipc_debug_record *record,
					 int count)
{
	atomic_set(&record->count, count);
}

static inline int ipc_debug_get_counter(struct ipc_debug_record *record)
{
	return atomic_read(&record->count);
}

static inline void ipc_debug_inc_counter(struct ipc_debug_record *record)
{
	atomic_inc(&record->count);
}

static inline void ipc_debug_set_min(struct ipc_debug_record *record, int min)
{
	record->min = min;
}

static inline uint64_t ipc_debug_get_min(struct ipc_debug_record *record)
{
	return record->min;
}

static inline void ipc_debug_set_max(struct ipc_debug_record *record, int max)
{
	record->max = max;
}

static inline uint64_t ipc_debug_get_max(struct ipc_debug_record *record)
{
	return record->max;
}

static inline void ipc_debug_set_avg(struct ipc_debug_record *record, int avg)
{
	record->avg = avg;
}

static inline uint64_t ipc_debug_get_avg(struct ipc_debug_record *record)
{
	return record->avg;
}

static inline void ipc_debug_set_min_ts(struct ipc_debug_record *record,
					uint64_t ts)
{
	record->min_ts = ts;
}

static inline uint64_t ipc_debug_get_min_ts(struct ipc_debug_record *record)
{
	return record->min_ts;
}

static inline void ipc_debug_set_max_ts(struct ipc_debug_record *record,
					uint64_t ts)
{
	record->max_ts = ts;
}

static inline uint64_t ipc_debug_get_max_ts(struct ipc_debug_record *record)
{
	return record->max_ts;
}
