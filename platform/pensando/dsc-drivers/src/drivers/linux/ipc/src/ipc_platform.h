// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#ifndef _IPC_PLATFORM_H_
#define _IPC_PLATFORM_H_

#define TAWK_DRV_MM_NAME "tawk_ipc_mm"

struct tawk_drv_platform_properties {
	uint32_t peer_id;
	bool rst_db_exists;
	uint32_t rst_db_offset;
	uint32_t rst_db_data;
	bool mbox_db_exists;
	uint32_t mbox_db_offset;
	uint32_t mbox_db_data;
	uint32_t rst_sig_out_offset;
	uint32_t rst_sig_in_offset;
	uint32_t nmboxes;
	uint32_t mbox_ctl_offset;
	uint32_t mbox_ctl_stride;
	uint32_t mbox_buffer_offset;
	uint32_t mbox_buffer_stride;
	uint32_t mbox_buffer_size;
};

#define TAWK_DRV_DB_REGION 0
#define TAWK_DRV_MSIX_REGION 1
#define TAWK_DRV_RST_SIGS_REGION 2
#define TAWK_DRV_MBOX_CTL_REGION 3
#define TAWK_DRV_MBOX_BUFFER_REGION 4

int tawk_drv_platform_register_driver(void);
void tawk_drv_platform_unregister_driver(void);

#endif /* _IPC_PLATFORM_H_ */
