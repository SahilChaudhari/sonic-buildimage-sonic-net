// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#pragma once

#ifdef __cplusplus
extern "C" {
#if 0
} /* close to calm emacs autoindent */
#endif
#endif

typedef enum {
    IPC_CMD_EP_BOARDCFG     = 1,
    IPC_CMD_EP_ASIC_ERROR   = 2,
    IPC_CMD_EP_BARCO_ACCESS = 3,
    IPC_CMD_EP_PCIESVC      = 5,
    IPC_CMD_EP_PAL_FRU      = 6,
    IPC_CMD_EP_PAL_QSFP     = 7,
    IPC_CMD_EP_PAL_CPLD     = 8,
    IPC_CMD_EP_PAL_FWSEL    = 9,
    IPC_CMD_EP_PAL_SENSOR   = 10,
    IPC_CMD_EP_FLASH_OPS    = 12,
    IPC_CMD_EP_TAWKTEST     = 13,
    IPC_CMD_EP_PAL_FREQ     = 14,
    IPC_CMD_EP_MAX          = 16,
} ipc_cmd_ep_num_t;


#ifdef __cplusplus
}
#endif
