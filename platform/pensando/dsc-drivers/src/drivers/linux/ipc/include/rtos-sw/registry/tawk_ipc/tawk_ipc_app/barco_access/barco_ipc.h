// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.
//
//----------------------------------------------------------------------------
///
/// \file
///  barco_ipc.h
///  This file contains barco IPC structures.
///
//----------------------------------------------------------------------------

#ifndef __BARCO_ACCESS_IPC_HPP__
#define __BARCO_ACCESS_IPC_HPP__

#ifdef __cplusplus
extern "C" {
#if 0
} /* close to calm emacs autoindent */
#endif
#endif

#include "include/sdk/base.hpp"
#include "tawk_ipc_cmd/opcode.h"

#ifndef __PACKED__
#define __PACKED__        __attribute__((packed))
#endif


enum BARCO_SECURE_MSG_OPCODES {
   BARCO_MSG_READ_SECURE_MODE_FLAG       = 1,
   BARCO_MSG_READ_SECURE_BITMAP_FLAG     = 2,
   BARCO_MSG_ENGINE_INIT_SECURE_FLAG     = 3,
   BARCO_MSG_ENGINE_INIT_NON_SECURE_FLAG = 4,
   BARCO_MSG_GEN_KEY_SECURE_FLAG         = 5,
   BARCO_MSG_DEL_KEY_SECURE_FLAG         = 6,
};

#define BARCO_CMD_READ_SECURE_MODE_FLAG           IPC_CMD_MAKE(BARCO_MSG_READ_SECURE_MODE_FLAG, 1)

typedef union {
    struct {
    } req;

    struct {
        uint8_t secure_mode_flag;
    } resp;
}__PACKED__ barco_read_secure_mode_msg_t;

#define BARCO_CMD_READ_SECURE_BITMAP_FLAG         IPC_CMD_MAKE(BARCO_MSG_READ_SECURE_BITMAP_FLAG, 1)

typedef union {
    struct {
    } req;

    struct {
        uint64_t secure_bitmap;
    } resp;
}__PACKED__ barco_read_secure_bitmap_msg_t;

#define BARCO_CMD_ENGINE_INIT_SECURE_FLAG         IPC_CMD_MAKE(BARCO_MSG_ENGINE_INIT_SECURE_FLAG, 1)

typedef union {
    struct {
        uint8_t engine_id;
    } req;

    struct {
        uint32_t key_count;
    } resp;
}__PACKED__ barco_engine_init_secure_msg_t;

#define BARCO_CMD_ENGINE_INIT_NON_SECURE_FLAG      IPC_CMD_MAKE(BARCO_MSG_ENGINE_INIT_NON_SECURE_FLAG, 1)

typedef union {
    struct {
        uint8_t engine_id;
        uint32_t key_count;
        uint64_t key_array_base;
    } req;

    struct {
    } resp;
}__PACKED__ barco_engine_init_non_secure_msg_t;

#define BARCO_CMD_GEN_KEY_SECURE_FLAG      IPC_CMD_MAKE(BARCO_MSG_GEN_KEY_SECURE_FLAG, 1)

typedef union {
    struct {
        uint8_t engine_id;
        uint8_t key_index;
        uint8_t key_size;
    } req;

    struct {
    } resp;
}__PACKED__ barco_gen_key_secure_msg_t;

#define BARCO_CMD_DEL_KEY_SECURE_FLAG      IPC_CMD_MAKE(BARCO_MSG_DEL_KEY_SECURE_FLAG, 1)

typedef union {
    struct {
        uint8_t engine_id;
        uint8_t key_index;
    } req;

    struct {
    } resp;
}__PACKED__ barco_del_key_secure_msg_t;

#ifdef __cplusplus
}
#endif

#endif  // __BARCO_ACCESS_IPC_HPP__
