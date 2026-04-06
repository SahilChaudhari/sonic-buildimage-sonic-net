// SPDX-License-Identifier: MIT or GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#pragma once

struct dentry;
struct ipc_context_s;

int ipc_debug_init(struct ipc_context_s *ipc);
void ipc_debug_fini(struct ipc_context_s *ipc);
uint32_t ipc_debug_sys_read32(uintptr_t a);
void ipc_debug_sys_write32(uint32_t v, uintptr_t a);
uint64_t ipc_debug_sys_read64(uintptr_t a);
void ipc_debug_sys_write64(uint64_t v, uintptr_t a);

struct dentry *ipc_debug_get_root(void);
void ipc_debug_put_root(void);
