// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#pragma once

#ifdef __cplusplus
extern "C" {
#if 0
} /* close to calm emacs autoindent */
#endif
#endif

#include <tawk_ipc_cmd/opcode.h>

#define BOARDCFG_CMD_NPARTS IPC_CMD_MAKE(1, 1)

typedef union {
    struct {
    } req;

    struct {
        uint32_t nparts;
    } resp;
} boardcfg_cmd_nparts_t;

#define BOARDCFG_CMD_SIZE   IPC_CMD_MAKE(2, 1)

typedef union {
    struct {
        uint32_t part_id;
    } req;

    struct {
        uint32_t size;
    } resp;
} boardcfg_cmd_size_t;

#define BOARDCFG_CMD_READ   IPC_CMD_MAKE(3, 1)

typedef union {
    struct {
        uint32_t cookie;
        uint32_t part_id;
        uint32_t offset;
        uint32_t size;
    } req;

    struct {
        uint32_t cookie;
        uint32_t size;
        uint8_t data[];
    } resp;
} boardcfg_cmd_read_t;

#define BOARDCFG_CMD_WRITE  IPC_CMD_MAKE(4, 1)

typedef union {
    struct {
        uint32_t cookie;
        uint32_t part_id;
        uint32_t offset;
        uint32_t size;
        uint8_t data[];
    } req;

    struct {
        uint32_t cookie;
    } resp;
} boardcfg_cmd_write_t;

#define BOARDCFG_CMD_ERASE  IPC_CMD_MAKE(5, 1)

typedef union {
    struct {
        uint32_t part_id;
    } req;

    struct {
    } resp;
} boardcfg_cmd_erase_t;

typedef union {
    struct {
    } req;

    struct {
    } resp;
} boardcfg_cmd_reload_t;

#define BOARDCFG_CMD_RELOAD IPC_CMD_MAKE(6, 1)

#ifdef __cplusplus
}
#endif
