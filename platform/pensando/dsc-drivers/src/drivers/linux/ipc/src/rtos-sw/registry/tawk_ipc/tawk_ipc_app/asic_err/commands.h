// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.
//
//----------------------------------------------------------------------------
///
/// \file
///  ipc edac commands.h
///  This file defines functions and macros for ipc-edac.
///
//----------------------------------------------------------------------------

#ifndef __ASIC_ERR_IPC_HDR__
#define __ASIC_ERR_IPC_HDR__

#ifdef __cplusplus
extern "C" {
#if 0
} /* close to calm emacs autoindent */
#endif
#endif

#include "tawk_ipc_cmd/opcode.h"
#include "tawk_ipc_app/base.h"

enum ASIC_ERR_CMD_MSG_OPCODES {
    EDAC_MSG_MC_INFO = 0,
};
#define EDAC_CMD_MC_INFO IPC_CMD_MAKE(EDAC_MSG_MC_INFO, 1)

enum ASIC_ERR_NOTIF_MSG_OPCODES {
    EDAC_MSG_MC_ERRS = 0,
};
#define EDAC_NOTIF_MC_ERRS IPC_CMD_MAKE(EDAC_MSG_MC_ERRS, 1)

#define MAX_NUM_MCS 4

typedef enum hw_event_mc_err_ {
    EVENT_ERR_CORRECTED,
    EVENT_ERR_UNCORRECTED,
} hw_event_mc_err_e;

typedef struct edac_mc_info_ {
    uint32_t mc_idx;
    uint32_t ue_cnt;
    uint32_t ce_cnt;
}__PACKED__ edac_mc_info_t;

typedef struct edac_mc_details_ {
    bool            mc_present;
    uint32_t        num_mcs;
    edac_mc_info_t  mc[MAX_NUM_MCS];
}__PACKED__  edac_mc_details_t;

typedef struct edac_mc_err_desc_ {
    hw_event_mc_err_e type;
    uint16_t          mc_idx;
    uint16_t          synd;
    uint16_t          id;
    uint64_t          ddr_addr;
    uint32_t          ue_cnt;
    uint32_t          ce_cnt;
}__PACKED__ edac_mc_err_desc_t;

typedef union {
    struct {
    }req;
    struct {
        edac_mc_details_t mc_info;
    }resp;
}__PACKED__ edac_mc_info_msg_t;

typedef union {
    struct {
        edac_mc_err_desc_t err_desc;
    }req;

    struct {
    }resp;
}__PACKED__ edac_mc_err_msg_t;

#ifdef __cplusplus
}
#endif

#endif /* __PAL_IPC_ASIC_ERR_H__ */
