// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#include <stdlib.h>
#include <string.h>
#include "popval.h"

static size_t pld_populate(uint8_t *buffer,
			   tawk_drv_hdr_t *hdr,
			   minstd_rand_t *prng,
			   size_t min_pld_size,
			   size_t max_pld_size)
{
	size_t pld_len = mod_ii(minstd_rand_get(prng),
				min_pld_size,
				max_pld_size);
	uint32_t pld_a = minstd_rand_get(prng);
	uint32_t pld_b = minstd_rand_get(prng);
	uint32_t pld_c = minstd_rand_get(prng);

	tawk_drv_hdr_set_pld_len(hdr, pld_len);
	memcpy(buffer, hdr, TAWK_DRV_HDR_LEN);

	for (size_t s = TAWK_DRV_HDR_LEN; s < pld_len + TAWK_DRV_HDR_LEN; s++) {
		buffer[s] = (uint8_t)pld_a;
		pld_a += pld_b;
		pld_b += pld_c;
	}

	return pld_len + TAWK_DRV_HDR_LEN;
}

static bool pld_validate(tawk_drv_hdr_t *hdr,
			 uint8_t *buffer,
			 minstd_rand_t *prng,
			 size_t min_pld_size,
			 size_t max_pld_size)
{
	size_t pld_len = mod_ii(minstd_rand_get(prng),
				min_pld_size,
				max_pld_size);

	uint32_t pld_a = minstd_rand_get(prng);
	uint32_t pld_b = minstd_rand_get(prng);
	uint32_t pld_c = minstd_rand_get(prng);

	if (tawk_drv_hdr_get_pld_len(*hdr) != pld_len)
		return false;

	for (size_t s = TAWK_DRV_HDR_LEN; s < pld_len + TAWK_DRV_HDR_LEN; s++) {
		if (buffer[s] != (uint8_t)pld_a)
			return false;
		pld_a += pld_b;
		pld_b += pld_c;
	}

	return true;
}


size_t tawktest_req_populate(tawktest_req_state_t *state, uint8_t *buffer)
{
	tawk_drv_hdr_t hdr;

	tawk_drv_hdr_set_type(&hdr, TAWK_DRV_HDR_TYPE_REQ);
	tawk_drv_hdr_set_req_endpoint(&hdr, state->params.ep);
	return pld_populate(buffer,
			    &hdr,
			    &state->prng,
			    state->params.min_pld_size,
			    state->params.max_pld_size);
}

bool tawktest_req_validate(tawktest_req_state_t *state, uint8_t *buffer)
{
	tawk_drv_hdr_t hdr;

	memcpy(&hdr, buffer, TAWK_DRV_HDR_LEN);

	if (tawk_drv_hdr_get_req_endpoint(hdr) != state->params.ep)
		return false;

	return pld_validate(&hdr, buffer, &state->prng,
			    state->params.min_pld_size,
			    state->params.max_pld_size);
}

size_t tawktest_rsp_populate(tawktest_rsp_state_t *state,
			     uint8_t *buffer,
			     tawk_drv_hdr_tag_t user_tag)
{
	uint32_t rsp_errno = minstd_rand_get(&state->prng);
	tawk_drv_hdr_t hdr = 0;

	tawk_drv_hdr_set_type(&hdr, TAWK_DRV_HDR_TYPE_RSP);
	tawk_drv_hdr_set_rsp_errno(&hdr, rsp_errno);
	tawk_drv_hdr_set_tag(&hdr, user_tag);

	return pld_populate(buffer,
			    &hdr,
			    &state->prng,
			    state->params.min_pld_size,
			    state->params.max_pld_size);
}

bool tawktest_rsp_validate(tawktest_rsp_state_t *state, uint8_t *buffer)
{
	uint32_t rsp_errno = minstd_rand_get(&state->prng);
	tawk_drv_hdr_t hdr;

	memcpy(&hdr, buffer, TAWK_DRV_HDR_LEN);

	if (tawk_drv_hdr_get_rsp_errno(hdr) != rsp_errno)
		return false;

	return pld_validate(&hdr, buffer, &state->prng,
			    state->params.min_pld_size,
			    state->params.max_pld_size);
}
