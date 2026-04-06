// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.
//
//----------------------------------------------------------------------------
///
/// \file
///  tawk_ipc_app/pciesvc/commands.h
///
//----------------------------------------------------------------------------

#pragma once

#include "tawk_ipc_cmd/opcode.h"

enum pciesvc_cmd_msg_opcodes {
    PCIESVC_MSG_EV = 0,
};

#define PCIESVC_CMD_EV IPC_CMD_MAKE(PCIESVC_MSG_EV, 1)


#define PCIESVC_TAWK_EV_MEM_RD  1
#define PCIESVC_TAWK_EV_MEM_WR  2
#define PCIESVC_TAWK_EV_MEM_OF  3
#define PCIESVC_TAWK_EV_NUM_VFS 4
#define PCIESVC_TAWK_EV_RESET   5
#define PCIESVC_TAWK_EV_MGMTCHG 6

typedef struct pciesvc_tawk_ev_hdr_s {
  uint8_t evtype;
  uint8_t port;
  uint16_t lifb;
  uint16_t lifc;
} __attribute__((packed)) pciesvc_tawk_ev_hdr_t;

typedef struct pciesvc_tawk_ev_mem_rd_s {
  uint64_t baraddr;          /* PCIe bar address */
  uint64_t baroffset;        /* bar-local offset */
  uint8_t cfgidx;            /* bar cfgidx */
  uint8_t pad0;
  uint16_t pad1;
  uint32_t size;             /* i/o size */
  uint64_t localpa;          /* local physical address */
} __attribute__((packed)) pciesvc_tawk_ev_mem_rd_t;

typedef struct pciesvc_tawk_ev_mem_wr_s {
  uint64_t baraddr;          /* PCIe bar address */
  uint64_t baroffset;        /* bar-local offset */
  uint8_t cfgidx;            /* bar cfgidx */
  uint8_t pad0;
  uint16_t pad1;
  uint32_t size;             /* i/o size */
  uint64_t localpa;          /* local physical address */
  uint64_t data;             /* data, if write */
} __attribute__((packed)) pciesvc_tawk_ev_mem_wr_t;

typedef struct pciesvc_tawk_ev_num_vfs_s {
  uint16_t num_vfs;
} __attribute__((packed)) pciesvc_tawk_ev_num_vfs_t;

typedef struct pciesvc_tawk_ev_mgmtchg_s {
  uint32_t bus;
} __attribute__((packed)) pciesvc_tawk_ev_mgmtchg_t;

#define PCIESVC_TAWK_EV_RST_BUS 1
#define PCIESVC_TAWK_EV_RST_FLR 2
#define PCIESVC_TAWK_EV_RST_VFE 3

typedef struct pciesvc_tawk_ev_rst_e {
  uint8_t rsttype;
} __attribute__((packed)) pciesvc_tawk_ev_rst_t;

