// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.
//
//----------------------------------------------------------------------------
///
/// \file
///  shell_cmd/commands.h
///  This file defines functions and macros for ipc-shell command output
///
//----------------------------------------------------------------------------

#ifndef __SHELL_CMD_COMMANDS_H__
#define __SHELL_CMD_COMMANDS_H__

#ifdef __cplusplus
extern "C" {
#if 0
} /* close to calm emacs autoindent */
#endif
#endif

#include "tawk_ipc_cmd/opcode.h"
#include "tawk_ipc_app/base.h"

#define SHELL_IPC_DATA_SIZE 1000

enum shell_cmd_opcode_e {
    SHELL_CMD_OP_NONE,
    SHELL_CMD_OP_SHELL_CMD_OUT,
};

#ifdef __cplusplus
}
#endif

#endif // !__SHELL_CMD_COMMANDS_H__
