// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#if 0
} /* close to calm emacs autoindent */
#endif
#endif

typedef uint32_t ipc_cmd_opcode_t;

#define IPC_CMD_OP(_opcode) ((uint16_t)(_opcode))
#define IPC_CMD_VER(_opcode) ((uint8_t)((_opcode) >> 16))
#define IPC_CMD_MAKE(_op, _ver) ((((_ver) & 0xff) << 16) | ((_op) & 0xffff))

#ifdef __cplusplus
}
#endif
