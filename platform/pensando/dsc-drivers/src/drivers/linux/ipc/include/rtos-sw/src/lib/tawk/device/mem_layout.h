// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#pragma once

#include <tawk/platform.h>

#if defined(RTOS)
#define IPC_PLATFORM_PEER_ID_EP                (0)
#define IPC_PLATFORM_PEER_ID_HOST              (1 - IPC_PLATFORM_PEER_ID_EP)

#define IPC_PLATFORM_RSTSIGS_OUT_OFFS(peer_id) (4 * peer_id)
#define IPC_PLATFORM_RSTSIGS_IN_OFFS(peer_id)  (4 * (1 - peer_id))

#define IPC_PLATFORM_RSTSIGS_OUT_OFFS_EP       IPC_PLATFORM_RSTSIGS_OUT_OFFS(IPC_PLATFORM_PEER_ID_EP)
#define IPC_PLATFORM_RSTSIGS_IN_OFFS_EP        IPC_PLATFORM_RSTSIGS_IN_OFFS(IPC_PLATFORM_PEER_ID_EP)

#define IPC_PLATFORM_MBOX_COUNT                (2) /* should be a config option */
#define IPC_PLATFORM_MBOX_CONTROL_OFFS         (0x1000)
#define IPC_PLATFORM_MBOX_CONTROL_STRIDE_LOG2  (3)
#define IPC_PLATFORM_MBOX_BUFFER_OFFS          (0x2000)
#define IPC_PLATFORM_MBOX_BUFFER_STRIDE_LOG2   (10)
#define IPC_PLATFORM_MBOX_BUFFER_SIZE          (1024) /* should be a config option */

#if defined(CONFIG_TAWK_IPC_WITH_INTR)
#define IPC_PLATFORM_RST_DB (0)
#define IPC_PLATFORM_MBOX_DB (1)
#endif
#endif
