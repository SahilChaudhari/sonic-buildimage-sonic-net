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

#ifndef __packed
#define __packed __attribute__((__packed__))
#endif

#define IPC_CMD_PROTO_MAGIC  0x4d435049 /* IPCM le */
#define IPC_CMD_PROTO_V1  1

typedef struct __packed ipc_cmd_hdr_s {
  uint32_t magic;
  uint8_t cmd_proto_version;
  uint8_t cmd_version;
  uint16_t cmd_opcode;
} ipc_cmd_hdr_t;

#ifdef __cplusplus
}
#endif

