// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <ipccmd/ipc_cmd_client.h>
#include <tawk_ipc_cmd/protocol.h>
#include <tawk_ipc_app/endpoints.h>
#include "header/protocol.h"
#include "popval.h"
#include "tawktest_cmd.h"

/* Upper bound on the value that can be returned by ipc_buffer_pld_size.
 * TODO: This should be discoverable via an ioctl when the necessary
 * infrastructure is in place.
 */
#define IPC_BUFFER_PLD_SIZE_MAX (1024)
#define MAX_BUF_SIZE (IPC_BUFFER_PLD_SIZE_MAX + TAWK_DRV_HDR_LEN)

/*
 * Tawktest userspace command issuers:
 *	- tawktest_send_cmd (INTERNAL USE ONLY)
 *	- tawktest_start_requester
 *	- tawktest_poll_requester
 *	- tawktest_finish_requester
 *	- tawktest_start_responder
 *	- tawktest_poll_responder
 *	- tawktest_finish_responder
 *
 * These commands are *only* safe to use when a test is quiescent, that is
 * when there are no further requests. These functions should be used with
 * care.
 *
 * Using these commands mid-test risks reading a test request/response,
 * rather than the intended command response.
 */

static int tawktest_send_cmd(ipc_cmd_dev_t *ipc, uint32_t opcode,
			     void *data_in, size_t in_len, void *data_out,
			     size_t out_len)
{
	struct iovec vec_in[1], vec_out[1];
	uint32_t err;
	int rc;

	vec_in->iov_base = data_in;
	vec_in->iov_len = in_len;
	vec_out->iov_base = data_out;
	vec_out->iov_len = out_len;

	rc = ipc_cmd_send_req_v(ipc, IPC_CMD_EP_TAWKTEST, opcode, vec_in, 1,
				vec_out, 1, &err);
	if (rc < 0)
		return -EIO; /* tranport error or ipccmd internal error */
	else if (err)
		return -EINVAL; /* error response from command handler */
	else if (rc != out_len)
		return -EMSGSIZE; /* "successful" response but unexpected length */

	return 0;
}

int tawktest_start_requester(ipc_cmd_dev_t *ipc,
			     struct tawktest_start_requester_req *start_req,
			     struct tawktest_start_requester_rsp *start_rsp)
{
	return tawktest_send_cmd(ipc, TAWKTEST_CMD_START_REQUESTER, start_req,
				 sizeof(*start_req), start_rsp,
				 sizeof(*start_rsp));
}

int tawktest_poll_requester(ipc_cmd_dev_t *ipc,
			    struct tawktest_poll_requester_req *poll_req,
			    struct tawktest_poll_requester_rsp *poll_rsp)
{
	return tawktest_send_cmd(ipc, TAWKTEST_CMD_POLL_REQUESTER, poll_req,
				 sizeof(*poll_req), poll_rsp,
				 sizeof(*poll_rsp));
}

int tawktest_finish_requester(ipc_cmd_dev_t *ipc,
			      struct tawktest_finish_requester_req *finish_req,
			      struct tawktest_finish_requester_rsp *finish_rsp)
{
	return tawktest_send_cmd(ipc, TAWKTEST_CMD_FINISH_REQUESTER, finish_req,
				 sizeof(*finish_req), finish_rsp,
				 sizeof(*finish_rsp));
}

int tawktest_start_responder(ipc_cmd_dev_t *ipc,
			     struct tawktest_start_responder_req *start_req,
			     struct tawktest_start_responder_rsp *start_rsp)
{
	return tawktest_send_cmd(ipc, TAWKTEST_CMD_START_RESPONDER, start_req,
				 sizeof(*start_req), start_rsp,
				 sizeof(*start_rsp));
}

int tawktest_poll_responder(ipc_cmd_dev_t *ipc,
			    struct tawktest_poll_responder_req *poll_req,
			    struct tawktest_poll_requester_rsp *poll_rsp)
{
	return tawktest_send_cmd(ipc, TAWKTEST_CMD_POLL_RESPONDER, poll_req,
				 sizeof(*poll_req), poll_rsp, sizeof(*poll_rsp));
}

int tawktest_finish_responder(ipc_cmd_dev_t *ipc,
			      struct tawktest_finish_responder_req *finish_req,
			      struct tawktest_finish_responder_rsp *finish_rsp)
{
	return tawktest_send_cmd(ipc, TAWKTEST_CMD_FINISH_RESPONDER, finish_req,
				 sizeof(*finish_req), finish_rsp,
				 sizeof(*finish_rsp));
}

/*
 * Tawktest userspace sender/handler functions:
 *	- tawktest_send_request
 *	- tawktest_handle_request
 *	- tawktest_send_response
 *	- tawktest_handle_response
 *
 * Unlike the Tawktest command issuers above, these functions do not use the
 * ipccmd wrapper since it is not asynchronous and has no support for
 * non-blocking behaviour. These functions should be used with care.
 */

int tawktest_send_request(ipc_cmd_dev_t *ipc, tawktest_req_state_t *req_state)
{
	uint8_t *buffer = alloca(MAX_BUF_SIZE);
	size_t size;
	int rc;

	ipc->tag++;
	size = tawktest_req_populate(req_state, buffer);

	rc = write(ipc->fd, buffer, size);
	if (rc != size)
		return -1;

	return 0;
}

bool tawktest_handle_request(ipc_cmd_dev_t *ipc,
			     tawktest_req_state_t *req_state,
			     tawk_drv_hdr_tag_t *user_tag)
{
	uint8_t *buffer = alloca(MAX_BUF_SIZE);
	tawk_drv_hdr_t *hdr;
	int rc;

	rc = read(ipc->fd, buffer, MAX_BUF_SIZE);
	if (rc < TAWK_DRV_HDR_LEN)
		return false;

	hdr = (tawk_drv_hdr_t *) buffer;
	*user_tag = tawk_drv_hdr_get_tag(*hdr);

	return tawktest_req_validate(req_state, buffer);
}

int tawktest_send_response(ipc_cmd_dev_t *ipc, tawktest_rsp_state_t *rsp_state,
			   tawk_drv_hdr_tag_t tag)
{
	uint8_t *buffer = alloca(MAX_BUF_SIZE);
	size_t size;
	int rc;

	size = tawktest_rsp_populate(rsp_state, buffer, tag);

	rc = write(ipc->fd, buffer, size);
	if (rc != size)
		return -1;

	return 0;

}

bool tawktest_handle_response(ipc_cmd_dev_t *ipc,
			      tawktest_rsp_state_t *rsp_state)
{
	uint8_t *buffer = alloca(MAX_BUF_SIZE);
	int rc;

	rc = read(ipc->fd, buffer, MAX_BUF_SIZE);
	if (rc < TAWK_DRV_HDR_LEN)
		return false;

	return tawktest_rsp_validate(rsp_state, buffer);
}
