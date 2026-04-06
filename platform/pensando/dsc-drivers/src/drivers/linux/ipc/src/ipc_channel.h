// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#ifndef _IPC_CHANNEL_H_
#define _IPC_CHANNEL_H_

void tawk_drv_channel_fini(struct tawk_drv_client *client);
int tawk_drv_channel_get_pool_size(struct tawk_drv_client *client);
int tawk_drv_channel_set_pool_size(struct tawk_drv_client *client,
                                   uint32_t requested_size);

#endif /* _IPC_CHANNEL_H_ */
