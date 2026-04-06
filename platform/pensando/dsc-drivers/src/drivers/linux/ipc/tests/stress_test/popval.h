// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "header/protocol.h"
#include "minstd.h"

/* Parameters describing a stream of requests */
typedef struct tawktest_req_params_s {
	size_t min_pld_size, max_pld_size;
	uint32_t ep;
} tawktest_req_params_t;

/* Parameters describing a stream of responses */
typedef struct tawktest_rsp_params_s {
	size_t min_pld_size, max_pld_size;
} tawktest_rsp_params_t;

/* State tracking an ordered stream of requests */
typedef struct tawktest_req_state_s {
	tawktest_req_params_t params;
	minstd_rand_t prng;
} tawktest_req_state_t;

/* State tracking an ordered stream of responses */
typedef struct tawktest_rsp_state_s {
	tawktest_rsp_params_t params;
	minstd_rand_t prng;
} tawktest_rsp_state_t;



size_t tawktest_req_populate(tawktest_req_state_t *state, uint8_t *buffer);
bool tawktest_req_validate(tawktest_req_state_t *state, uint8_t *buffer);
size_t tawktest_rsp_populate(tawktest_rsp_state_t *state, uint8_t *buffer,
			     tawk_drv_hdr_tag_t user_tag);
bool tawktest_rsp_validate(tawktest_rsp_state_t *state, uint8_t *buffer);
