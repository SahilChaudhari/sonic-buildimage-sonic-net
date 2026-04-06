// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#ifndef _PROTOCOL_H_
#define _PROTOCOL_H_

#ifdef __KERNEL__
#include <libc_compat.h>
#endif
#include <zephyr/kernel.h>

#include <tawk_ipc_drv/protocol_defs.h>


#define TAWK_DRV_HDR_LEN (TAWK_DRV_HDR_WIDTH / 8)

typedef uint64_t tawk_drv_hdr_t;
BUILD_ASSERT(TAWK_DRV_HDR_WIDTH == 64, "tawk_drv_hdr_t matches TAWK_DRV_HDR_WIDTH");

typedef uint8_t tawk_drv_hdr_type_t;
typedef uint8_t tawk_drv_hdr_tag_t;
typedef uint32_t tawk_drv_hdr_req_endpoint_t;
typedef uint32_t tawk_drv_hdr_rsp_errno_t;

extern tawk_drv_hdr_type_t tawk_drv_hdr_get_type(tawk_drv_hdr_t hdr);
extern void tawk_drv_hdr_set_type(tawk_drv_hdr_t *hdr,
				  tawk_drv_hdr_type_t type);

extern tawk_drv_hdr_tag_t tawk_drv_hdr_get_tag(tawk_drv_hdr_t hdr);
extern void tawk_drv_hdr_set_tag(tawk_drv_hdr_t *hdr,
				 tawk_drv_hdr_tag_t tag);

extern size_t tawk_drv_hdr_get_pld_len(tawk_drv_hdr_t hdr);
extern void tawk_drv_hdr_set_pld_len(tawk_drv_hdr_t *hdr,
				     size_t len);

extern tawk_drv_hdr_req_endpoint_t tawk_drv_hdr_get_req_endpoint(tawk_drv_hdr_t hdr);
extern void tawk_drv_hdr_set_req_endpoint(tawk_drv_hdr_t *hdr,
					  tawk_drv_hdr_req_endpoint_t ep);

extern tawk_drv_hdr_rsp_errno_t tawk_drv_hdr_get_rsp_errno(tawk_drv_hdr_t hdr);
extern void tawk_drv_hdr_set_rsp_errno(tawk_drv_hdr_t *hdr,
				       tawk_drv_hdr_rsp_errno_t rsp_errno);

extern int tawk_drv_hdr_get_tperr_errno(tawk_drv_hdr_t hdr);
extern void tawk_drv_hdr_set_tperr_errno(tawk_drv_hdr_t *hdr,
					 int errno);


#endif /* _PROTOCOL_H_ */
