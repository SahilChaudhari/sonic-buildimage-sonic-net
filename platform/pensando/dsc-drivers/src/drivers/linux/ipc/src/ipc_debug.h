// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#ifndef _IPC_DEBUG_H_
#define _IPC_DEBUG_H_

struct dentry;

struct tawk_drv_client;

/* Returns an instance of dentry on success or an error code on failure */
struct dentry *
ipc_debug_client_init(struct tawk_drv_client *client);
void ipc_debug_client_fini(struct dentry *dentry);

#endif /* _IPC_DEBUG_H_ */
