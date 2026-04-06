// SPDX-License-Identifier: MIT or GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (C) 2025 Advanced Micro Devices, Inc.

#pragma once

/* Forward declarations to avoid redefinition of typedefs. */
typedef struct ipc_hardware_ctx_s ipc_hardware_ctx_t;
typedef struct ipc_hardware_mailbox_ctx_s ipc_hardware_mailbox_ctx_t;
typedef struct ipc_hardware_rstsigs_ctx_s ipc_hardware_rstsigs_ctx_t;

/* "Quarantine".
 *
 * A quarantine is a state where instances of the mailbox and rstsigs types
 * are deinitialised but not freed and need to gracefully handle possible
 * invocations from the upper layers.
 *
 * The key characteristic of the quarantine state is that the mailbox and
 * rstsigs types are reinitialised to possibly operate on a regular memory
 * area in the kernel address space whilst assuming it is the genuine
 * hardware memory mappings.
 *
 * Below is the quarantined memory layout.
 *
 * Mailbox:
 *
 *   Offset    Name                    Size
 *   --------------------------------------
 *   0x0000    Control Register 0         8
 *   0x0008    Control Register 1         8
 *   0x0010    Buffer Base 0           1024
 *   0x0410    Buffer Base 1           1024
 */
#define TAWK_IPC_QUARANTINE_MBOX_CNT                        2

#define TAWK_IPC_QUARANTINE_MBOX_CTL_BASE              0x0000
#define TAWK_IPC_QUARANTINE_MBOX_CTL_STRIDE_LOG2            3

#define TAWK_IPC_QUARANTINE_MBOX_BUF_BASE              0x0010
#define TAWK_IPC_QUARANTINE_MBOX_BUF_STRIDE_LOG2           10
#define TAWK_IPC_QUARANTINE_MBOX_BUF_SZ                  1024

#define TAWK_IPC_QUARANTINE_MBOX_SZ                    0x0810

/* Reset Registers:
 *
 *   Offset    Name                    Size
 *   --------------------------------------
 *     0x00    Out Register               8
 *     0x08    In Register                8
 */

#define TAWK_IPC_QUARANTINE_RST_SIGS_SZ                    16
#define TAWK_IPC_QUARANTINE_RST_SIGS_OUT_REG             0x00
#define TAWK_IPC_QUARANTINE_RST_SIGS_IN_REG              0x08
