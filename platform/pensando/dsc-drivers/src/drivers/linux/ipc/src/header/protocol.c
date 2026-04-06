// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#include <zephyr/kernel.h>
#include "protocol.h"

#define TAWK_DRV_HDR_MASK(field_)                          \
	GENMASK64(((TAWK_DRV_HDR_ ## field_ ## _LBN) +           \
						 (TAWK_DRV_HDR_ ## field_ ## _WIDTH) - 1),     \
						(TAWK_DRV_HDR_ ## field_ ## _LBN))

#define TAWK_DRV_HDR_FIELD_GET(hdr_, field_)       \
	FIELD_GET(TAWK_DRV_HDR_MASK(field_), hdr_)

#define TAWK_DRV_HDR_FIELD_PREP(field_, value_)    \
	FIELD_PREP(TAWK_DRV_HDR_MASK(field_), value_)

#define TAWK_DRV_HDR_FIELD_SET(hdr_, field_, value_)       \
	do {                                                          \
		(hdr_) &= ~TAWK_DRV_HDR_MASK(field_);                  \
		(hdr_) |= TAWK_DRV_HDR_FIELD_PREP(field_, value_);     \
	} while (0)


tawk_drv_hdr_type_t tawk_drv_hdr_get_type(tawk_drv_hdr_t hdr)
{
	return TAWK_DRV_HDR_FIELD_GET(hdr, TYPE);
}

void tawk_drv_hdr_set_type(tawk_drv_hdr_t *hdr, tawk_drv_hdr_type_t hdr_type)
{
	switch (hdr_type) {
	case TAWK_DRV_HDR_TYPE_REQ:
	case TAWK_DRV_HDR_TYPE_RSP:
	case TAWK_DRV_HDR_TYPE_TPERR:
		break;

	default:
		__ASSERT(0, "undefined hdr_type: %u", hdr_type);
		break;
	}

	TAWK_DRV_HDR_FIELD_SET(*hdr, TYPE, hdr_type);
}

tawk_drv_hdr_tag_t tawk_drv_hdr_get_tag(tawk_drv_hdr_t hdr)
{
	tawk_drv_hdr_type_t hdr_type = tawk_drv_hdr_get_type(hdr);

	switch (hdr_type) {
	case TAWK_DRV_HDR_TYPE_REQ:
	case TAWK_DRV_HDR_TYPE_RSP:
	case TAWK_DRV_HDR_TYPE_TPERR:
		break;

	default:
		__ASSERT(0, "invalid hdr_type for tag field: %u", hdr_type);
		break;
	}

	return TAWK_DRV_HDR_FIELD_GET(hdr, TAG);
}

void tawk_drv_hdr_set_tag(tawk_drv_hdr_t *hdr, tawk_drv_hdr_tag_t tag)
{
	tawk_drv_hdr_type_t hdr_type = tawk_drv_hdr_get_type(*hdr);

	switch (hdr_type) {
	case TAWK_DRV_HDR_TYPE_REQ:
	case TAWK_DRV_HDR_TYPE_RSP:
	case TAWK_DRV_HDR_TYPE_TPERR:
		break;

	default:
		__ASSERT(0, "invalid hdr_type for tag field: %u", hdr_type);
		break;
	}

	TAWK_DRV_HDR_FIELD_SET(*hdr, TAG, tag);
}

size_t tawk_drv_hdr_get_pld_len(tawk_drv_hdr_t hdr)
{
	tawk_drv_hdr_type_t hdr_type = tawk_drv_hdr_get_type(hdr);

	switch (hdr_type) {
	case TAWK_DRV_HDR_TYPE_REQ:
	case TAWK_DRV_HDR_TYPE_RSP:
	case TAWK_DRV_HDR_TYPE_TPERR:
		break;

	default:
		__ASSERT(0, "invalid hdr_type for pld len: %u", hdr_type);
		break;
	}

	return TAWK_DRV_HDR_FIELD_GET(hdr, PLD_LEN);
}

void tawk_drv_hdr_set_pld_len(tawk_drv_hdr_t *hdr, size_t pld_len)
{
	tawk_drv_hdr_type_t hdr_type = tawk_drv_hdr_get_type(*hdr);

	switch (hdr_type) {
	case TAWK_DRV_HDR_TYPE_REQ:
	case TAWK_DRV_HDR_TYPE_RSP:
		break;

	case TAWK_DRV_HDR_TYPE_TPERR:
		__ASSERT(pld_len == 0, "pld_len must be 0 in tperr");
		break;

	default:
		__ASSERT(0, "invalid hdr_type for pld len: %u", hdr_type);
		break;
	}

	TAWK_DRV_HDR_FIELD_SET(*hdr, PLD_LEN, pld_len);
}

tawk_drv_hdr_req_endpoint_t tawk_drv_hdr_get_req_endpoint(tawk_drv_hdr_t hdr)
{
	tawk_drv_hdr_type_t hdr_type = tawk_drv_hdr_get_type(hdr);

	switch (hdr_type) {
	case TAWK_DRV_HDR_TYPE_REQ:
		break;

	default:
		__ASSERT(0, "invalid hdr_type for endpoint field: %u", hdr_type);
		break;
	}

	return TAWK_DRV_HDR_FIELD_GET(hdr, REQ_ENDPOINT);
}

void tawk_drv_hdr_set_req_endpoint(tawk_drv_hdr_t *hdr,
				   tawk_drv_hdr_req_endpoint_t ep)
{
	tawk_drv_hdr_type_t hdr_type = tawk_drv_hdr_get_type(*hdr);

	switch (hdr_type) {
	case TAWK_DRV_HDR_TYPE_REQ:
		break;

	default:
		__ASSERT(0, "invalid hdr_type for endpoint field: %u", hdr_type);
		break;
	}

	TAWK_DRV_HDR_FIELD_SET(*hdr, REQ_ENDPOINT, ep);
}


tawk_drv_hdr_rsp_errno_t tawk_drv_hdr_get_rsp_errno(tawk_drv_hdr_t hdr)
{
	tawk_drv_hdr_type_t hdr_type = tawk_drv_hdr_get_type(hdr);

	switch (hdr_type) {
	case TAWK_DRV_HDR_TYPE_RSP:
		break;

	default:
		__ASSERT(0, "invalid hdr_type for errno field: %u", hdr_type);
		break;
	}

	return TAWK_DRV_HDR_FIELD_GET(hdr, RSP_ERRNO);
}

void tawk_drv_hdr_set_rsp_errno(tawk_drv_hdr_t *hdr,
				tawk_drv_hdr_rsp_errno_t rsp_errno)
{
	tawk_drv_hdr_type_t hdr_type = tawk_drv_hdr_get_type(*hdr);

	switch (hdr_type) {
	case TAWK_DRV_HDR_TYPE_RSP:
		break;

	default:
		__ASSERT(0, "invalid hdr_type for errno field: %u", hdr_type);
		break;
	}

	TAWK_DRV_HDR_FIELD_SET(*hdr, RSP_ERRNO, rsp_errno);
}

int tawk_drv_hdr_get_tperr_errno(tawk_drv_hdr_t hdr)
{
	tawk_drv_hdr_type_t hdr_type = tawk_drv_hdr_get_type(hdr);

	switch (hdr_type) {
	case TAWK_DRV_HDR_TYPE_TPERR:
		break;

	default:
		__ASSERT(0, "invalid hdr_type for tperr code field: %u", hdr_type);
		break;
	}

	return TAWK_DRV_HDR_FIELD_GET(hdr, TPERR_ERRNO);
}

void tawk_drv_hdr_set_tperr_errno(tawk_drv_hdr_t *hdr,
				  int errno)
{
	tawk_drv_hdr_type_t hdr_type = tawk_drv_hdr_get_type(*hdr);

	switch (hdr_type) {
	case TAWK_DRV_HDR_TYPE_TPERR:
		break;

	default:
		__ASSERT(0, "invalid hdr_type for tperr code field: %u", hdr_type);
		break;
	}

	TAWK_DRV_HDR_FIELD_SET(*hdr, TPERR_ERRNO, errno);
}
