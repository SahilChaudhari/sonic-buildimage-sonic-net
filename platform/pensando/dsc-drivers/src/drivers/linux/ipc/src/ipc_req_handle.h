// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#ifndef _IPC_REQ_HANDLE_H_
#define _IPC_REQ_HANDLE_H_

void tawk_drv_req_handler_free_client_handlers(struct tawk_drv_client *client);
int tawk_drv_req_handler_register(struct tawk_drv_client *client,
				  ipc_request_cb *cb,
				  uint32_t ep);
int tawk_drv_req_handler_deregister(struct tawk_drv_client *client,
				    uint32_t ep);

#endif /* _IPC_REQ_HANDLE_H_ */
