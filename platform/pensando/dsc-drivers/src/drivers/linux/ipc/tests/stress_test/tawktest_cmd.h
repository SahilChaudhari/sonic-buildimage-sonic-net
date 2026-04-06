// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#pragma once

#include <tawk_ipc_app/tawktest/commands.h>
#include <ipccmd/ipc_cmd_client.h>
#include "popval.h"

int tawktest_start_requester(ipc_cmd_dev_t *ipc,
			     struct tawktest_start_requester_req *start_req,
			     struct tawktest_start_requester_rsp *start_rsp);
int tawktest_poll_requester(ipc_cmd_dev_t *ipc,
			    struct tawktest_poll_requester_req *poll_req,
			    struct tawktest_poll_requester_rsp *poll_rsp);
int tawktest_finish_requester(ipc_cmd_dev_t *ipc,
			      struct tawktest_finish_requester_req *finish_req,
			      struct tawktest_finish_requester_rsp *finish_rsp);
int tawktest_start_responder(ipc_cmd_dev_t *ipc,
			     struct tawktest_start_responder_req *start_req,
			     struct tawktest_start_responder_rsp *start_rsp);
int tawktest_poll_responder(ipc_cmd_dev_t *ipc,
			    struct tawktest_poll_responder_req *poll_req,
			    struct tawktest_poll_requester_rsp *poll_rsp);
int tawktest_finish_responder(ipc_cmd_dev_t *ipc,
			      struct tawktest_finish_responder_req *finish_req,
			      struct tawktest_finish_responder_rsp *finish_rsp);
int tawktest_send_request(ipc_cmd_dev_t *ipc, tawktest_req_state_t *req_state);
bool tawktest_handle_request(ipc_cmd_dev_t *ipc,
			     tawktest_req_state_t *req_state,
			     tawk_drv_hdr_tag_t *user_tag);
int tawktest_send_response(ipc_cmd_dev_t *ipc, tawktest_rsp_state_t *rsp_state,
			   tawk_drv_hdr_tag_t tag);
bool tawktest_handle_response(ipc_cmd_dev_t *ipc,
			      tawktest_rsp_state_t *rsp_state);
