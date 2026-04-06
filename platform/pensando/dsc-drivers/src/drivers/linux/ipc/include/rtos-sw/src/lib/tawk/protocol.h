// SPDX-License-Identifier: MIT or GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#pragma once

#ifdef __KERNEL__
#include <libc_compat.h>
#else
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#endif

#include <zephyr/kernel.h>

#include <tawk_ipc_core/protocol_defs.h>


#define IPC_TRANSPORT_HDR_LEN (IPC_TRANSPORT_HDR_WIDTH / 8)

typedef uint64_t ipc_hdr_t;
BUILD_ASSERT(IPC_TRANSPORT_HDR_WIDTH == 64, "ipc_hdr_t matches IPC_TRANSPORT_HDR_WIDTH");

typedef uint8_t ipc_hdr_type_t;
typedef uint8_t ipc_hdr_tpmsg_msgcode_t;
typedef uint8_t ipc_hdr_tag_t;
typedef uint32_t ipc_hdr_req_endpoint_t;
typedef uint32_t ipc_hdr_rsp_errno_t;
typedef uint32_t ipc_hdr_tperr_errcode_t;


extern const ipc_hdr_t ipc_hdr_noop;


extern bool ipc_hdr_validate(ipc_hdr_t hdr);

extern unsigned ipc_hdr_get_owner(ipc_hdr_t hdr);
extern void ipc_hdr_set_owner(ipc_hdr_t *hdr, unsigned owner);

extern ipc_hdr_type_t ipc_hdr_get_type(ipc_hdr_t hdr);
extern void ipc_hdr_set_type(ipc_hdr_t *hdr,
                             ipc_hdr_type_t type);

extern ipc_hdr_tpmsg_msgcode_t ipc_hdr_get_tpmsg_code(ipc_hdr_t hdr);
extern void ipc_hdr_set_tpmsg_code(ipc_hdr_t *hdr,
                                   ipc_hdr_tpmsg_msgcode_t msgcode);

extern void ipc_hdr_clr_tpmsg_noop_rsvd0(ipc_hdr_t *hdr);

extern uint16_t ipc_hdr_get_tpmsg_cfg_ver_min(ipc_hdr_t hdr);
extern void ipc_hdr_set_tpmsg_cfg_ver_min(ipc_hdr_t *hdr,
                                             uint16_t ver_min);

extern uint16_t ipc_hdr_get_tpmsg_cfg_ver_max(ipc_hdr_t hdr);
extern void ipc_hdr_set_tpmsg_cfg_ver_max(ipc_hdr_t *hdr,
                                             uint16_t ver_max);

extern uint16_t ipc_hdr_get_tpmsg_cfg_caps_ini_max(ipc_hdr_t hdr);
extern void ipc_hdr_set_tpmsg_cfg_caps_ini_max(ipc_hdr_t *hdr,
                                                  uint16_t ini_max);

extern uint16_t ipc_hdr_get_tpmsg_cfg_caps_tgt_max(ipc_hdr_t hdr);
extern void ipc_hdr_set_tpmsg_cfg_caps_tgt_max(ipc_hdr_t *hdr,
                                                  uint16_t tgt_max);

extern void ipc_hdr_clr_tpmsg_ready_rsvd0(ipc_hdr_t *hdr);

extern ipc_hdr_tag_t ipc_hdr_get_tag(ipc_hdr_t hdr);
extern void ipc_hdr_set_tag(ipc_hdr_t *hdr,
                            ipc_hdr_tag_t tag);

extern size_t ipc_hdr_get_pld_len(ipc_hdr_t hdr);
extern void ipc_hdr_set_pld_len(ipc_hdr_t *hdr,
                                size_t len);

extern ipc_hdr_req_endpoint_t ipc_hdr_get_req_endpoint(ipc_hdr_t hdr);
extern void ipc_hdr_set_req_endpoint(ipc_hdr_t *hdr,
                                     ipc_hdr_req_endpoint_t ep);

extern ipc_hdr_rsp_errno_t ipc_hdr_get_rsp_errno(ipc_hdr_t hdr);
extern void ipc_hdr_set_rsp_errno(ipc_hdr_t *hdr,
                                  ipc_hdr_rsp_errno_t rsp_errno);

extern ipc_hdr_tperr_errcode_t ipc_hdr_get_tperr_errcode(ipc_hdr_t hdr);
extern void ipc_hdr_set_tperr_errcode(ipc_hdr_t *hdr,
                                      ipc_hdr_tperr_errcode_t errcode);
