// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#ifndef _PROTOCOL_DEFS_H_
#define _PROTOCOL_DEFS_H_


#define TAWK_DRV_HDR_WIDTH 64

#define TAWK_DRV_HDR_TYPE_LBN 0
#define TAWK_DRV_HDR_TYPE_WIDTH 3

#define TAWK_DRV_HDR_TYPE_REQ 0x2
#define TAWK_DRV_HDR_TYPE_RSP 0x3
#define TAWK_DRV_HDR_TYPE_TPERR 0x4

#define TAWK_DRV_HDR_TAG_LBN 8
#define TAWK_DRV_HDR_TAG_WIDTH 8

#define TAWK_DRV_HDR_PLD_LEN_LBN 16
#define TAWK_DRV_HDR_PLD_LEN_WIDTH 16

/* Only valid for REQ */
#define TAWK_DRV_HDR_REQ_ENDPOINT_LBN 32
#define TAWK_DRV_HDR_REQ_ENDPOINT_WIDTH 32

/* Only valid for RSP */
#define TAWK_DRV_HDR_RSP_ERRNO_LBN 32
#define TAWK_DRV_HDR_RSP_ERRNO_WIDTH 32

/* Only valid for TPERR */
#define TAWK_DRV_HDR_TPERR_ERRNO_LBN 32
#define TAWK_DRV_HDR_TPERR_ERRNO_WIDTH 32

#endif /* _PROTOCOL_DEFS_H_ */
