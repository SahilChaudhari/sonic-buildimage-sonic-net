// SPDX-License-Identifier: MIT or GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#pragma once

/* Add the macros as needed... */
#define PRIx32 "x"
#define PRIx64 "llx"

#define LOG_MODULE_REGISTER(...)
#define LOG_MODULE_DECLARE(...)

/* Please do not use LOG_ERR(), LOG_WRN(), LOG_INF(), and LOG_DBG() directly.
 * Instead, use TAWK_IPC_LOG_xxx() macros in the TAWK IPC core because they
 * add the PCIe address prefix to the log message, which helps identify the
 * affected NIC in cluster multi-NIC configurations. Use dev_xxx() or pr_xxx()
 * in the Linux-only code.
 */
