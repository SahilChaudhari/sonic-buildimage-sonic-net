// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.
//
//----------------------------------------------------------------------------
///
/// \file
///  techsupport/commands.h
///  This file defines functions and macros for ipc-techsupport
///
//----------------------------------------------------------------------------

#ifndef __TECHSUPPORT_COMMANDS_H__
#define __TECHSUPPORT_COMMANDS_H__

#ifdef __cplusplus
extern "C" {
#if 0
} /* close to calm emacs autoindent */
#endif
#endif

#include "tawk_ipc_cmd/opcode.h"
#include "tawk_ipc_app/base.h"

#define TS_CMDS_VERSION_1 1

enum ts_cmd_opcode_e {
    TS_CMD_OP_NONE = 0,
    TS_CMD_OP_COREDUMP = 1,
    TS_CMD_OP_CONSOLE  = 2,
    TS_CMD_OP_PROCESS  = 3,
    TS_CMD_OP_FW_VERSION  = 4,
    TS_CMD_OP_QUERY_LOG_BACKENDS = 5,
    TS_CMD_OP_QUERY_LOG_SRCS = 6,
    TS_CMD_OP_CHANGE_LOG_VERBOSITY = 7,
    TS_CMD_OP_SHELL_CMD = 8,
};

#define TS_CMD_COREDUMP IPC_CMD_MAKE(TS_CMD_OP_COREDUMP, TS_CMDS_VERSION_1)

typedef union {
    struct {
        uint32_t offset;
        uint32_t size;
    } req;

    struct {
        uint32_t size;
        uint32_t more;
        uint8_t data[];
    } resp;
}__PACKED__ ts_cmd_coredump_t;

#define TS_CMD_CONSOLE IPC_CMD_MAKE(TS_CMD_OP_CONSOLE, TS_CMDS_VERSION_1)

typedef union {
    struct {
        uint8_t start;
    } req;

    struct {
        uint8_t more;
        uint8_t data[];
    } resp;
}__PACKED__ ts_cmd_console_t;

#define TS_CMD_CONSOLE_V2 IPC_CMD_MAKE(TS_CMD_OP_CONSOLE, 2)

typedef enum {
    TS_CMD_CONSOLE_SOURCE_V2_FLASH = 0,
    TS_CMD_CONSOLE_SOURCE_V2_RAM = 1,
} ts_cmd_console_source_v2_t;

typedef union {
    struct {
        uint8_t start;
        uint8_t source; /* A value from ts_cmd_console_source_v2_t */
    } req;

    struct {
        uint8_t more;
        uint8_t data[];
    } resp;
}__PACKED__ ts_cmd_console_v2_t;

#define TS_CMD_PROCESS IPC_CMD_MAKE(TS_CMD_OP_PROCESS, TS_CMDS_VERSION_1)

typedef union {
    struct {
    } req;

    struct {
        uint8_t more;
        uint8_t data[];
    } resp;
}__PACKED__ ts_cmd_process_t;

#define TS_CMD_FW_VERSION IPC_CMD_MAKE(TS_CMD_OP_FW_VERSION, TS_CMDS_VERSION_1)

typedef union {
    struct {
    } req;

    struct {
        uint32_t size;
        uint8_t data[];
    } resp;
}__PACKED__ ts_cmd_fw_version_t;

#define TS_CMD_QUERY_LOG_BACKENDS IPC_CMD_MAKE(TS_CMD_OP_QUERY_LOG_BACKENDS, 1)

typedef struct {
    uint32_t id;
    uint8_t len;
    uint8_t name[];
}__PACKED__ ts_cmd_query_log_backend_t;

typedef union {
    struct {
    } req;

    struct {
        uint16_t len;
        uint8_t  num_backends;
        uint8_t  data[]; // An array of ts_cmd_query_log_backend_t
    } resp;
}__PACKED__ ts_cmd_query_log_backends_t;

#define TS_CMD_QUERY_LOG_SRCS IPC_CMD_MAKE(TS_CMD_OP_QUERY_LOG_SRCS, 1)

typedef struct {
    uint32_t id;
    uint8_t len;
    uint8_t log_level;
    uint8_t name[];
}__PACKED__ ts_cmd_query_log_src_t;

typedef union {
    struct {
        uint32_t backend; // The backend for which we want to know the sources' logging level
        uint32_t start_from; // from what ID the report should start (pass 0 on the first call)
    } req;

    struct {
        uint16_t len;
        uint8_t more; // there are more sources to report, call again this command
        uint8_t data[]; // an array of ts_cmd_query_log_src_t
    } resp;
}__PACKED__ ts_cmd_query_log_srcs_t;

#define TS_CMD_CHANGE_LOG_VERBOSITY IPC_CMD_MAKE(TS_CMD_OP_CHANGE_LOG_VERBOSITY, 1)

typedef union {
    struct {
        uint32_t backend; // The backend where we want to change the source's logging level
        uint32_t id; // The log source ID
        uint8_t level; // The new log level to use with the module
    } req;

    struct {
        uint8_t level; // The log level now in use with the module
    } resp;
}__PACKED__ ts_cmd_update_log_verbosity_t;

#define TS_CMD_SHELL_CMD IPC_CMD_MAKE(TS_CMD_OP_SHELL_CMD, TS_CMDS_VERSION_1)

#define SHELL_CMD_MAX_SIZE     250

typedef union {
    struct {
        uint32_t recv_ipc_ep;
        uint32_t recv_ipc_cmd;
        char shell_cmd[];
    } req;

    struct {
        char data[1];
    } resp;
}__PACKED__ ts_cmd_shell_cmd_t;

#ifdef __cplusplus
}
#endif

#endif // !__TECHSUPPORT_COMMANDS_H__
