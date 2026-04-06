// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <poll.h>
#include <alloca.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include "header/protocol.h"
#include <tawk_ipc_drv/ioctl.h>
#include <tawk_ipc_drv/ioctl_debug.h>
#include "../third_party/munit/munit.h"

#define DEVICE_FILE "/dev/tawkipcdev"
#define NUM_FDS 4
/* Test endpoint on A35 */
#define TEST_EP 42

struct test_data {
	int fd[NUM_FDS];
};

static void generate_req_header(tawk_drv_hdr_t *hdr,
				tawk_drv_hdr_type_t type,
				size_t pld_len,
				tawk_drv_hdr_req_endpoint_t endpoint,
				tawk_drv_hdr_tag_t tag)
{
	*hdr = 0;
	tawk_drv_hdr_set_type(hdr, type);
	tawk_drv_hdr_set_pld_len(hdr, pld_len);
	tawk_drv_hdr_set_req_endpoint(hdr, endpoint);
	tawk_drv_hdr_set_tag(hdr, tag);
}

static void verify_rsp_header(tawk_drv_hdr_t hdr,
			      tawk_drv_hdr_rsp_errno_t exp_errno,
			      int ret_len)
{
	munit_assert_uint8(tawk_drv_hdr_get_type(hdr), ==, TAWK_DRV_HDR_TYPE_RSP);
	munit_assert_uint32(tawk_drv_hdr_get_rsp_errno(hdr), ==, exp_errno);
	munit_assert_uint32(tawk_drv_hdr_get_pld_len(hdr) +
			    sizeof(tawk_drv_hdr_t),
			    ==, ret_len);
}

static MunitResult test_single_request(const MunitParameter params[],
				       void *data)
{
	int fd;
	struct test_data *t_data = (struct test_data *) data;

	if (!t_data)
		return MUNIT_ERROR;

	fd = t_data->fd[0];
	munit_assert(fd > 0);

	tawk_drv_hdr_t req_hdr;
	tawk_drv_hdr_t rsp_hdr;
	size_t pld_len = 10;
	uint8_t req_buffer[sizeof(tawk_drv_hdr_t) + pld_len];
	uint8_t rsp_buffer[sizeof(tawk_drv_hdr_t) + 1024];
	int cnt;

	req_hdr = 0;
	generate_req_header(&req_hdr, TAWK_DRV_HDR_TYPE_REQ,
			    pld_len, TEST_EP, 0);

	memcpy(&req_buffer, &req_hdr, sizeof(tawk_drv_hdr_t));
	for (int i = 0; i < pld_len; i++)
		req_buffer[sizeof(tawk_drv_hdr_t) + i] = i;

	cnt = write(fd, &req_buffer, sizeof(req_buffer));
	munit_assert(cnt == sizeof(req_buffer));

	cnt = read(fd, &rsp_buffer, sizeof(rsp_buffer));
	munit_assert(cnt > sizeof(tawk_drv_hdr_t) && cnt <= sizeof(rsp_buffer));

	memcpy(&rsp_hdr, &rsp_buffer, sizeof(tawk_drv_hdr_t));
	verify_rsp_header(rsp_hdr, 0, cnt);

	/* SUBJECT TO CHANGE: Currently A35 will:
	 * - Calculates the minimum, maximum and average value of all the bytes
	 *   in the request payload.
	 * - Create a response payload 10 bytes longer than the request payload:
	 *      - payload consists of the minimum, maximum and average values
	 *        repeated.
	 *      - If the response payload isn’t a multiple of 3 bytes long,
	 *        it’s padded with 0x11.
	 */
	munit_assert(tawk_drv_hdr_get_pld_len(rsp_hdr) == 20);
	munit_assert_uint8(rsp_buffer[sizeof(tawk_drv_hdr_t)], ==, 0);
	munit_assert_uint8(rsp_buffer[sizeof(tawk_drv_hdr_t) + 1], ==, 9);
	munit_assert_uint8(rsp_buffer[sizeof(tawk_drv_hdr_t) + 2], ==, 4);

	return MUNIT_OK;
}

static MunitResult test_two_writes_two_reads(const MunitParameter params[],
				       void *data)
{
	tawk_drv_hdr_t req1_hdr, rsp1_hdr, req2_hdr, rsp2_hdr;
	struct test_data *t_data = (struct test_data *) data;
	size_t pld1_len, pld2_len;
	int fd, cnt1, cnt2;

	if (!t_data)
		return MUNIT_ERROR;

	fd = t_data->fd[0];
	munit_assert(fd > 0);

	pld1_len = 10;
	pld2_len = 12;

	uint8_t req1_buffer[sizeof(tawk_drv_hdr_t) + pld1_len];
	uint8_t rsp1_buffer[sizeof(tawk_drv_hdr_t) + 1024];
	uint8_t req2_buffer[sizeof(tawk_drv_hdr_t) + pld2_len];
	uint8_t rsp2_buffer[sizeof(tawk_drv_hdr_t) + 1024];

	generate_req_header(&req1_hdr, TAWK_DRV_HDR_TYPE_REQ,
			    pld1_len, TEST_EP, 0);
	generate_req_header(&req2_hdr, TAWK_DRV_HDR_TYPE_REQ,
			    pld2_len, TEST_EP, 1);

	memcpy(&req1_buffer, &req1_hdr, sizeof(tawk_drv_hdr_t));
	for (int i = 0; i < pld1_len; i++)
		req1_buffer[sizeof(tawk_drv_hdr_t) + i] = i + 5;

	memcpy(&req2_buffer, &req2_hdr, sizeof(tawk_drv_hdr_t));
	for (int i = 0; i < pld2_len; i++)
		req2_buffer[sizeof(tawk_drv_hdr_t) + i] = i * 2;

	cnt1 = write(fd, &req1_buffer, sizeof(req1_buffer));
	munit_assert_uint32(cnt1, ==, sizeof(req1_buffer));
	cnt2 = write(fd, &req2_buffer, sizeof(req2_buffer));
	munit_assert_uint32(cnt2, ==, sizeof(req2_buffer));

	cnt1 = read(fd, &rsp1_buffer, sizeof(rsp1_buffer));
	munit_assert(cnt1 > sizeof(tawk_drv_hdr_t) && cnt1 <= sizeof(rsp1_buffer));
	cnt2 = read(fd, &rsp2_buffer, sizeof(rsp2_buffer));
	munit_assert(cnt2 > sizeof(tawk_drv_hdr_t) && cnt2 <= sizeof(rsp2_buffer));

	memcpy(&rsp1_hdr, &rsp1_buffer, sizeof(tawk_drv_hdr_t));
	memcpy(&rsp2_hdr, &rsp2_buffer, sizeof(tawk_drv_hdr_t));

	verify_rsp_header(rsp1_hdr, 0, cnt1);
	verify_rsp_header(rsp2_hdr, 0, cnt2);

	/* SUBJECT TO CHANGE: Currently A35 will:
	 * - Calculates the minimum, maximum and average value of all the bytes
	 *   in the request payload.
	 * - Create a response payload 10 bytes longer than the request payload:
	 *      - payload consists of the minimum, maximum and average values
	 *        repeated.
	 *      - If the response payload isn’t a multiple of 3 bytes long,
	 *        it’s padded with 0x11.
	 */
	munit_assert(tawk_drv_hdr_get_pld_len(rsp1_hdr) == 20);
	munit_assert_uint8(rsp1_buffer[sizeof(tawk_drv_hdr_t)], ==, 5);
	munit_assert_uint8(rsp1_buffer[sizeof(tawk_drv_hdr_t) + 1], ==, 14);
	munit_assert_uint8(rsp1_buffer[sizeof(tawk_drv_hdr_t) + 2], ==, 9);

	munit_assert(tawk_drv_hdr_get_pld_len(rsp2_hdr) == 22);
	munit_assert_uint8(rsp2_buffer[sizeof(tawk_drv_hdr_t)], ==, 0);
	munit_assert_uint8(rsp2_buffer[sizeof(tawk_drv_hdr_t) + 1], ==, 22);
	munit_assert_uint8(rsp2_buffer[sizeof(tawk_drv_hdr_t) + 2], ==, 11);

	return MUNIT_OK;
}

static MunitResult test_single_request_from_two_fds(
			const MunitParameter params[], void *data)
{
	tawk_drv_hdr_t req1_hdr, rsp1_hdr, req2_hdr, rsp2_hdr;
	struct test_data *t_data = (struct test_data *) data;
	size_t pld_len1, pld_len2;
	int fd1, fd2, cnt1, cnt2;

	if (!t_data)
		return MUNIT_ERROR;

	fd1 = t_data->fd[0];
	fd2 = t_data->fd[1];

	pld_len1 = 6;
	pld_len2 = 9;

	munit_assert(fd1 > 0);
	munit_assert(fd2 > 0);
	munit_assert(fd1 != fd2);

	uint8_t req1_buffer[sizeof(tawk_drv_hdr_t) + pld_len1];
	uint8_t rsp1_buffer[sizeof(tawk_drv_hdr_t) + 1024];
	uint8_t req2_buffer[sizeof(tawk_drv_hdr_t) + pld_len2];
	uint8_t rsp2_buffer[sizeof(tawk_drv_hdr_t) + 1024];

	generate_req_header(&req1_hdr, TAWK_DRV_HDR_TYPE_REQ,
			    pld_len1, TEST_EP, 0);

	generate_req_header(&req2_hdr, TAWK_DRV_HDR_TYPE_REQ,
			    pld_len2, TEST_EP, 0);

	memcpy(&req1_buffer, &req1_hdr, sizeof(tawk_drv_hdr_t));
	for (int i = 0; i < pld_len1; i++)
		req1_buffer[sizeof(tawk_drv_hdr_t) + i] = i;

	memcpy(&req2_buffer, &req2_hdr, sizeof(tawk_drv_hdr_t));
	for (int i = 0; i < pld_len2; i++)
		req2_buffer[sizeof(tawk_drv_hdr_t) + i] = i * i;

	cnt1 = write(fd1, &req1_buffer, sizeof(req1_buffer));
	cnt2 = write(fd2, &req2_buffer, sizeof(req2_buffer));
	munit_assert(cnt1 == sizeof(req1_buffer));
	munit_assert(cnt2 == sizeof(req2_buffer));

	cnt1 = read(fd1, &rsp1_buffer, sizeof(rsp1_buffer));
	cnt2 = read(fd2, &rsp2_buffer, sizeof(rsp2_buffer));
	memcpy(&rsp1_hdr, &rsp1_buffer, sizeof(tawk_drv_hdr_t));
	memcpy(&rsp2_hdr, &rsp2_buffer, sizeof(tawk_drv_hdr_t));

	verify_rsp_header(rsp1_hdr, 0, cnt1);
	verify_rsp_header(rsp2_hdr, 0, cnt2);

	/* SUBJECT TO CHANGE: Currently A35 will:
	 * - Calculates the minimum, maximum and average value of all the bytes
	 *   in the request payload.
	 * - Create a response payload 10 bytes longer than the request payload:
	 *      - payload consists of the minimum, maximum and average values
	 *        repeated.
	 *      - If the response payload isn’t a multiple of 3 bytes long,
	 *        it’s padded with 0x11.
	 */
	munit_assert(tawk_drv_hdr_get_pld_len(rsp1_hdr) == 16);
	munit_assert_uint8(rsp1_buffer[sizeof(tawk_drv_hdr_t)], ==, 0);
	munit_assert_uint8(rsp1_buffer[sizeof(tawk_drv_hdr_t) + 1], ==, 5);
	munit_assert_uint8(rsp1_buffer[sizeof(tawk_drv_hdr_t) + 2], ==, 2);

	munit_assert(tawk_drv_hdr_get_pld_len(rsp2_hdr) == 19);
	munit_assert_uint8(rsp2_buffer[sizeof(tawk_drv_hdr_t)], ==, 0);
	munit_assert_uint8(rsp2_buffer[sizeof(tawk_drv_hdr_t) + 1], ==, 64);
	munit_assert_uint8(rsp2_buffer[sizeof(tawk_drv_hdr_t) + 2], ==, 22);

	return MUNIT_OK;
}

static MunitResult test_two_writes_two_reads_from_two_fds(
			const MunitParameter params[], void *data)
{
	uint8_t rsp_buffers[4][sizeof(tawk_drv_hdr_t) + 1024];
	struct test_data *t_data = (struct test_data *) data;
	tawk_drv_hdr_t req_hdrs[4];
	tawk_drv_hdr_t rsp_hdrs[4];
	size_t pld_lens[4];
	int cnt[4];
	int fd[2];

	if (!t_data)
		return MUNIT_ERROR;

	fd[0] = t_data->fd[0];
	fd[1] = t_data->fd[1];

	for (int i = 0; i < 4; i++)
		pld_lens[i] = i + 4;

	munit_assert(fd[0] > 0);
	munit_assert(fd[1] > 0);
	munit_assert(fd[0] != fd[1]);

	uint8_t req1_buffer[sizeof(tawk_drv_hdr_t) + pld_lens[0]];
	uint8_t req2_buffer[sizeof(tawk_drv_hdr_t) + pld_lens[1]];
	uint8_t req3_buffer[sizeof(tawk_drv_hdr_t) + pld_lens[2]];
	uint8_t req4_buffer[sizeof(tawk_drv_hdr_t) + pld_lens[3]];

	for (int i = 0; i < 4; i++)
		generate_req_header(&req_hdrs[i], TAWK_DRV_HDR_TYPE_REQ,
				    pld_lens[i], TEST_EP, 0);

	memcpy(&req1_buffer, &req_hdrs[0], sizeof(tawk_drv_hdr_t));
	for (int i = 0; i < pld_lens[0]; i++)
		req1_buffer[sizeof(tawk_drv_hdr_t) + i] = i*i;

	memcpy(&req2_buffer, &req_hdrs[1], sizeof(tawk_drv_hdr_t));
	for (int i = 0; i < pld_lens[1]; i++)
		req2_buffer[sizeof(tawk_drv_hdr_t) + i] = i*2;

	memcpy(&req3_buffer, &req_hdrs[2], sizeof(tawk_drv_hdr_t));
	for (int i = 0; i < pld_lens[2]; i++)
		req3_buffer[sizeof(tawk_drv_hdr_t) + i] = i+1;

	memcpy(&req4_buffer, &req_hdrs[3], sizeof(tawk_drv_hdr_t));
	for (int i = 0; i < pld_lens[3]; i++)
		req4_buffer[sizeof(tawk_drv_hdr_t) + i] = i+1+(i%3);

	cnt[0] = write(fd[0], &req1_buffer, sizeof(req1_buffer));
	cnt[1] = write(fd[1], &req2_buffer, sizeof(req2_buffer));
	cnt[2] = write(fd[0], &req3_buffer, sizeof(req3_buffer));
	cnt[3] = write(fd[1], &req4_buffer, sizeof(req4_buffer));
	munit_assert(cnt[0] == sizeof(req1_buffer));
	munit_assert(cnt[1] == sizeof(req2_buffer));
	munit_assert(cnt[2] == sizeof(req3_buffer));
	munit_assert(cnt[3] == sizeof(req4_buffer));

	for (int i = 0; i < 4; i++) {
		cnt[i] = read(fd[i % 2], rsp_buffers[i], sizeof(rsp_buffers[i]));
		memcpy(&rsp_hdrs[i], rsp_buffers[i], sizeof(tawk_drv_hdr_t));
		verify_rsp_header(rsp_hdrs[i], 0, cnt[i]);
	}

	/* SUBJECT TO CHANGE: Currently A35 will:
	 * - Calculates the minimum, maximum and average value of all the bytes
	 *   in the request payload.
	 * - Create a response payload 10 bytes longer than the request payload:
	 *      - payload consists of the minimum, maximum and average values
	 *        repeated.
	 *      - If the response payload isn’t a multiple of 3 bytes long,
	 *        it’s padded with 0x11.
	 */

	for (int i = 0; i < 4; i++)
		munit_assert(tawk_drv_hdr_get_pld_len(rsp_hdrs[i]) == pld_lens[i] + 10);

	munit_assert_uint8(rsp_buffers[0][sizeof(tawk_drv_hdr_t)], ==, 0);
	munit_assert_uint8(rsp_buffers[0][sizeof(tawk_drv_hdr_t) + 1], ==, 9);
	munit_assert_uint8(rsp_buffers[0][sizeof(tawk_drv_hdr_t) + 2], ==, 3);

	munit_assert_uint8(rsp_buffers[1][sizeof(tawk_drv_hdr_t)], ==, 0);
	munit_assert_uint8(rsp_buffers[1][sizeof(tawk_drv_hdr_t) + 1], ==, 8);
	munit_assert_uint8(rsp_buffers[1][sizeof(tawk_drv_hdr_t) + 2], ==, 4);

	munit_assert_uint8(rsp_buffers[2][sizeof(tawk_drv_hdr_t)], ==, 1);
	munit_assert_uint8(rsp_buffers[2][sizeof(tawk_drv_hdr_t) + 1], ==, 6);
	munit_assert_uint8(rsp_buffers[2][sizeof(tawk_drv_hdr_t) + 2], ==, 3);

	munit_assert_uint8(rsp_buffers[3][sizeof(tawk_drv_hdr_t)], ==, 1);
	munit_assert_uint8(rsp_buffers[3][sizeof(tawk_drv_hdr_t) + 1], ==, 8);
	munit_assert_uint8(rsp_buffers[3][sizeof(tawk_drv_hdr_t) + 2], ==, 4);

	return MUNIT_OK;
}

static MunitResult test_TAWKIPCREGEP(const MunitParameter params[], void *data)
{
	struct test_data *t_data = (struct test_data *) data;
	int fd1, fd2, rc;
	uint32_t ep = 42;

	fd1 = t_data->fd[0];
	fd2 = t_data->fd[1];

	rc = ioctl(fd1, TAWKIPCREGEP, ep);
	munit_assert_int64(rc, ==, 0);
	rc = ioctl(fd2, TAWKIPCREGEP, ep);
	munit_assert_int64(rc, ==, -1);
	return MUNIT_OK;
}

static MunitResult test_TAWKIPCDEREGEP(const MunitParameter params[], void *data)
{
	struct test_data *t_data = (struct test_data *) data;
	int fd1, fd2, rc;
	uint32_t ep = 42;

	fd1 = t_data->fd[0];
	fd2 = t_data->fd[1];

	for (int i = 0; i < 10000; i++) {
		rc = ioctl(fd1, TAWKIPCREGEP, ep + i);
		munit_assert_int64(rc, ==, 0);
	}

	for (int i = 0; i < 10000; i++) {
		rc = ioctl(fd2, TAWKIPCREGEP, ep + i);
		munit_assert_int64(rc, ==, -1);
	}

	for (int i = 0; i < 10000; i++) {
		rc = ioctl(fd1, TAWKIPCDEREGEP, ep + i);
		munit_assert_int64(rc, ==, 0);
	}

	for (int i = 0; i < 10000; i++) {
		rc = ioctl(fd2, TAWKIPCREGEP, ep + i);
		munit_assert_int64(rc, ==, 0);
	}
	return MUNIT_OK;
}

static MunitResult test_TAWKIPCDEREGEP_correct_ownership(const MunitParameter params[],
							 void *data)
{

	struct test_data *t_data = (struct test_data *) data;
	int fd1, fd2, rc;
	uint32_t ep = 42;

	fd1 = t_data->fd[0];
	fd2 = t_data->fd[1];

	rc = ioctl(fd1, TAWKIPCREGEP, ep);
	munit_assert_int64(rc, ==, 0);

	rc = ioctl(fd2, TAWKIPCDEREGEP, ep);
	munit_assert_int64(rc, ==, -1);

	rc = ioctl(fd1, TAWKIPCDEREGEP, ep);
	munit_assert_int64(rc, ==, 0);

	return MUNIT_OK;
}

static MunitResult test_non_blocking_read(const MunitParameter params[],
					  void *data)
{
	struct test_data *t_data = (struct test_data *) data;
	size_t len = 1024 + TAWK_DRV_HDR_LEN;
	int fd = t_data->fd[0];
	uint8_t *rsp_buffer;
	int rc = 0;

	rsp_buffer = alloca(len);

	errno = 0;
	rc = read(fd, rsp_buffer, len);
	munit_assert_int64(rc, ==, -1);
	munit_assert_int64(errno, ==, EAGAIN);

	return MUNIT_OK;
}

static MunitResult test_poll_read_readiness(const MunitParameter params[],
					    void *data)
{
	struct test_data *t_data = (struct test_data *) data;
	int fd = t_data->fd[0];
	struct pollfd poll_fd;
	tawk_drv_hdr_t hdr;
	uint8_t *buffer;
	int rc = 0;

	poll_fd.fd = fd;
	poll_fd.events = POLLIN; /* Read ready */

	rc = poll(&poll_fd, 1, 0);
	munit_assert_int64(rc, ==, 0); /* Nothing to read */

	generate_req_header(&hdr, TAWK_DRV_HDR_TYPE_REQ, 0, 42, 0);

	rc = write(fd, &hdr, TAWK_DRV_HDR_LEN);
	munit_assert_int64(rc, ==, TAWK_DRV_HDR_LEN);

	/* Sleep while waiting for response */
	sleep(1);

	rc = poll(&poll_fd, 1, 0);
	munit_assert_int64(rc, ==, 1); /* There is one fd descriptor that is readable */
	munit_assert(poll_fd.revents & POLLIN); /* The fd is read ready */

	buffer = alloca(TAWK_DRV_HDR_LEN + 1024);
	read(fd, buffer, TAWK_DRV_HDR_LEN + 1024);

	return MUNIT_OK;
}

static MunitResult test_select_read_readiness(const MunitParameter params[],
					      void *data)
{
	struct test_data *t_data = (struct test_data *) data;
	int fd = t_data->fd[0];
	tawk_drv_hdr_t hdr;
	struct timeval t;
	uint8_t *buffer;
	fd_set r_fds;
	int rc = 0;

	FD_ZERO(&r_fds);
	FD_SET(fd, &r_fds);
	t.tv_sec = 0;
	t.tv_usec = 0;

	rc = select(fd + 1, &r_fds, NULL, NULL, &t);
	munit_assert_int64(rc, ==, 0);

	generate_req_header(&hdr, TAWK_DRV_HDR_TYPE_REQ, 0, 42, 0);

	rc = write(fd, &hdr, TAWK_DRV_HDR_LEN);
	munit_assert_int64(rc, ==, TAWK_DRV_HDR_LEN);

	sleep(1);

	FD_SET(fd, &r_fds);
	t.tv_sec = 0;
	t.tv_usec = 0;

	rc = select(fd + 1, &r_fds, NULL, NULL, &t);
	munit_assert_int64(rc, >, 0);
	munit_assert(FD_ISSET(fd, &r_fds));

	buffer = alloca(TAWK_DRV_HDR_LEN + 1024);
	read(fd, buffer, TAWK_DRV_HDR_LEN + 1024);

	return MUNIT_OK;
}

static MunitResult test_poll_write_readiness(const MunitParameter params[],
					    void *data)
{
	struct test_data *t_data = (struct test_data *) data;
	int fd = t_data->fd[0];
	struct pollfd poll_fd;
	tawk_drv_hdr_t hdr;
	uint8_t *buffer;
	int rc = 0;

	poll_fd.fd = fd;
	poll_fd.events = POLLOUT; /* Write ready */

	rc = poll(&poll_fd, 1, 0);
	munit_assert_int64(rc, ==, 1);
	munit_assert(poll_fd.revents & POLLOUT); /* Writeable */

	generate_req_header(&hdr, TAWK_DRV_HDR_TYPE_REQ, 0, 42, 0);

	rc = write(fd, &hdr, TAWK_DRV_HDR_LEN);
	munit_assert_int64(rc, ==, TAWK_DRV_HDR_LEN);

	sleep(1);

	buffer = alloca(TAWK_DRV_HDR_LEN + 1024);
	read(fd, buffer, TAWK_DRV_HDR_LEN + 1024);

	return MUNIT_OK;
}

static MunitResult test_create_and_destroy_exclusive_pool(const MunitParameter params[],
							  void *data)
{
	struct test_data *t_data = (struct test_data *) data;
	int fd = t_data->fd[0];
	tawk_drv_hdr_t hdr;
	uint8_t *buffer;
	int rc = 0;

	rc = ioctl(fd, TAWKIPCGETPLSIZE, 0);
	munit_assert_int64(rc, ==, 0);

	generate_req_header(&hdr, TAWK_DRV_HDR_TYPE_REQ, 0, 42, 0);

	rc = write(fd, &hdr, TAWK_DRV_HDR_LEN);
	munit_assert_int64(rc, ==, TAWK_DRV_HDR_LEN);

	sleep(1);

	buffer = alloca(TAWK_DRV_HDR_LEN + 1024);
	rc = read(fd, buffer, TAWK_DRV_HDR_LEN + 1024);
	munit_assert_int64(rc, >=, TAWK_DRV_HDR_LEN);

	rc = ioctl(fd, TAWKIPCSETPLSIZE, 1);
	munit_assert_int64(rc, ==, 1);

	rc = write(fd, &hdr, TAWK_DRV_HDR_LEN);
	munit_assert_int64(rc, ==, TAWK_DRV_HDR_LEN);

	sleep(1);

	memset(buffer, 0, TAWK_DRV_HDR_LEN + 1024);
	rc = read(fd, buffer, TAWK_DRV_HDR_LEN + 1024);
	munit_assert_int64(rc, >=, TAWK_DRV_HDR_LEN);

	rc = ioctl(fd, TAWKIPCGETPLSIZE, 0);
	munit_assert_int64(rc, ==, 1);

	rc = ioctl(fd, TAWKIPCSETPLSIZE, 0);
	munit_assert_int64(rc, ==, 0);

	rc = write(fd, &hdr, TAWK_DRV_HDR_LEN);
	munit_assert_int64(rc, ==, TAWK_DRV_HDR_LEN);

	sleep(1);

	memset(buffer, 0, TAWK_DRV_HDR_LEN + 1024);
	rc = read(fd, buffer, TAWK_DRV_HDR_LEN + 1024);
	munit_assert_int64(rc, >=, TAWK_DRV_HDR_LEN);

	return MUNIT_OK;
}

static MunitResult test_TAWKIPCSETPLSIZE(const MunitParameter params[],
					 void *data)
{
	struct test_data *t_data = (struct test_data *) data;
	int fd = t_data->fd[0];
	int rc = 0;

	rc = ioctl(fd, TAWKIPCSETPLSIZE, -1);
	munit_assert_int64(rc, ==, -1);

	rc = ioctl(fd, TAWKIPCGETPLSIZE, 0);
	munit_assert_int64(rc, ==, 0);

	rc = ioctl(fd, TAWKIPCSETPLSIZE, 1000);
	munit_assert_int64(rc, ==, -1);

	rc = ioctl(fd, TAWKIPCGETPLSIZE, 0);
	munit_assert_int64(rc, ==, 0);

	rc = ioctl(fd, TAWKIPCSETPLSIZE, 12);
	munit_assert_int64(rc, ==, 12);

	rc = ioctl(fd, TAWKIPCGETPLSIZE, 0);
	munit_assert_int64(rc, ==, 12);

	rc = ioctl(fd, TAWKIPCSETPLSIZE, 0);
	munit_assert_int64(rc, ==, 0);

	rc = ioctl(fd, TAWKIPCGETPLSIZE, 0);
	munit_assert_int64(rc, ==, 0);

	rc = ioctl(fd, TAWKIPCSETPLSIZE, 12);
	munit_assert_int64(rc, ==, 12);

	rc = ioctl(fd, TAWKIPCSETPLSIZE, 12);
	munit_assert_int64(rc, ==, 12);

	return MUNIT_OK;
}

static MunitResult test_TAWKIPCSETPLSIZE_2FD(const MunitParameter params[],
					 void *data)
{
	struct test_data *t_data = (struct test_data *) data;
	int fd1 = t_data->fd[0];
	int fd2 = t_data->fd[1];
	int rc = 0;

	/* By default there are 16 request buffers. The common pool must have
	 * at least 1 request buffer. Create exclusive pool with all available
	 * request buffers. Then attempt to create exclusive pool from another
	 * FD.
	 */
	rc = ioctl(fd1, TAWKIPCSETPLSIZE, 15);
	munit_assert_int64(rc, ==, 15);

	rc = ioctl(fd1, TAWKIPCGETPLSIZE, 0);
	munit_assert_int64(rc, ==, 15);

	rc = ioctl(fd2, TAWKIPCSETPLSIZE, 1);
	munit_assert_int64(rc, ==, 0); /* Unable to allocate pool size 1 */

	rc = ioctl(fd2, TAWKIPCGETPLSIZE, 0);
	munit_assert_int64(rc, ==, 0);

	rc = ioctl(fd1, TAWKIPCSETPLSIZE, 14);
	munit_assert_int64(rc, ==, 14);

	rc = ioctl(fd1, TAWKIPCGETPLSIZE, 0);
	munit_assert_int64(rc, ==, 14);

	rc = ioctl(fd2, TAWKIPCSETPLSIZE, 1);
	munit_assert_int64(rc, ==, 1); /* Able to allocate pool size 1 */

	rc = ioctl(fd2, TAWKIPCGETPLSIZE, 0);
	munit_assert_int64(rc, ==, 1);

	return MUNIT_OK;
}

static MunitResult test_REORDER(const MunitParameter params[], void *data)
{
	struct test_data *t_data = (struct test_data *) data;
	int fd = t_data->fd[0];
	int rc = 0;

	rc = ioctl(fd, TAWKIPCGETREORDER, 0);
	munit_assert_int64(rc, ==, 1);

	rc = ioctl(fd, TAWKIPCSETREORDER, 1);
	munit_assert_int64(rc, ==, 0);

	rc = ioctl(fd, TAWKIPCSETREORDER, 0);
	munit_assert_int64(rc, ==, -1);
	munit_assert_int64(errno, ==, EOPNOTSUPP);

	return MUNIT_OK;
}

static int test_file_exists(const MunitParameter params[])
{
	const char *file = munit_parameters_get(params, "file");
	return access(file, F_OK) == 0;
}

static void test_msleep(unsigned int msec)
{
	struct timespec ts;
	int rc;

	ts.tv_sec = msec / 1000;
	ts.tv_nsec = (msec % 1000) * 1000 * 1000;

	rc = nanosleep(&ts, &ts);
	munit_assert_int64(rc, ==, 0);
}

/* Give the driver some time to propagate the link status. */
#define TEST_LINK_DOWN_SLEEP_MSEC 100

static MunitResult test_link_down_mode(const MunitParameter params[],
				       void *data)
{
	struct test_data *t_data = (struct test_data *) data;

	int fd1 = t_data->fd[0];
	int fd2 = t_data->fd[1];
	int i;

	int rc;

	/* The first app (fd1) runs in the "strict" mode,
	 * TAWK_DRV_LINK_DOWN_MODE_ALL_REOPEN.
	 */
	rc = ioctl(fd1, TAWKIPCSETLINKDOWNMODE, 0);
	munit_assert_int64(rc, ==, 0);

	rc = ioctl(fd1, TAWKIPCGETLINKDOWNMODE, 0);
	munit_assert_int64(rc, ==, 0);

	/* The second app (fd2) runs in a less "strict" mode,
	 * TAWK_DRV_LINK_DOWN_MODE_REOPEN.
	 */
	rc = ioctl(fd2, TAWKIPCSETLINKDOWNMODE, 1);
	munit_assert_int64(rc, ==, 0);

	rc = ioctl(fd2, TAWKIPCGETLINKDOWNMODE, 0);
	munit_assert_int64(rc, ==, 1);

	/* The remaining apps run in the "invincible" mode,
	 * TAWK_DRV_LINK_DOWN_MODE_IGNORE.
	 */
	for (i = 2; i < NUM_FDS; i++) {
		int fd = t_data->fd[i];

		rc = ioctl(fd, TAWKIPCSETLINKDOWNMODE, 2);
		munit_assert_int64(rc, ==, 0);

		rc = ioctl(fd, TAWKIPCGETLINKDOWNMODE, 0);
		munit_assert_int64(rc, ==, 2);
	}

	/* Provoke the link down and examine the state.
	 *
	 * NOTE it requires the driver to be built with TAWK_DRV_DEBUG.
	 */
	rc = ioctl(fd1, TAWKIPCRESETLINK, 0);
	munit_assert_int64(rc, ==, 0);

	/* Initially, the link should stay down due to f1's preference
	 * to use TAWK_DRV_LINK_DOWN_MODE_REOPEN.
	 */
	test_msleep(TEST_LINK_DOWN_SLEEP_MSEC);
	munit_assert_int64(test_file_exists(params), ==, 0);

	/* Close f1, it should bring the link up. */
	close(fd1);
	test_msleep(TEST_LINK_DOWN_SLEEP_MSEC);
	munit_assert_int64(test_file_exists(params), ==, 1);

	/* Meanwhile, f2 still requires the file reopen. */
	rc = ioctl(fd2, TAWKIPCGETLINKDOWNMODE, 0);
	munit_assert_int64(rc, ==, -1);
	munit_assert_int64(errno, ==, ENOLINK);

	/* The remaining files must be in the operational state,
	 * but must refuse to change the link down mode.
	 */
	for (i = 2; i < NUM_FDS; i++) {
		int fd = t_data->fd[i];

		rc = ioctl(fd, TAWKIPCSETLINKDOWNMODE, 0);
		munit_assert_int64(rc, ==, -1);
		munit_assert_int64(errno, ==, ENOLINK);

		rc = ioctl(fd, TAWKIPCGETLINKDOWNMODE, 0);
		munit_assert_int64(rc, ==, 2);
	}

	return MUNIT_OK;
}

static void *test_request_setup(const MunitParameter params[],
				void *user_data)
{
	struct test_data *t_data;
	const char *file;

	t_data = malloc(sizeof(struct test_data));
	if (!t_data)
		return NULL;

	file = munit_parameters_get(params, "file");
	for (int i = 0; i < NUM_FDS; i++)
		t_data->fd[i] = open(file, O_RDWR);
	return (void *) t_data;
}

static void *test_non_blocking_request_setup(const MunitParameter params[],
					      void *user_data)
{
	struct test_data *t_data;
	const char *file;

	t_data = malloc(sizeof(struct test_data));
	if (!t_data)
		return NULL;

	file = munit_parameters_get(params, "file");
	for (int i = 0; i < NUM_FDS; i++)
		t_data->fd[i] = open(file, O_RDWR|O_NONBLOCK);
	return (void *) t_data;
}

static void test_request_tear_down(void *data)
{
	struct test_data *t_data = (struct test_data *) data;

	for (int i = 0; i < NUM_FDS; i++)
		close(t_data->fd[i]);
	free(t_data);
}

static char *dev_files[] = {
	(char *) DEVICE_FILE,
	NULL
};

static MunitParameterEnum test_request_params[] = {
	{ (char *) "file", dev_files },
	{ NULL, NULL }
};

static MunitTest test_suite_tests[] = {
	{
		(char *) "/single_request",
		test_single_request,
		test_request_setup,
		test_request_tear_down,
		MUNIT_TEST_OPTION_NONE,
		test_request_params,
	},
	{
		(char *) "/two_writes_two_reads",
		test_two_writes_two_reads,
		test_request_setup,
		test_request_tear_down,
		MUNIT_TEST_OPTION_NONE,
		test_request_params,
	},
	{
		(char *) "/single_request_from_two_fds",
		test_single_request_from_two_fds,
		test_request_setup,
		test_request_tear_down,
		MUNIT_TEST_OPTION_NONE,
		test_request_params,
	},
	{
		(char *) "/two_writes_two_read_from_two_fds",
		test_two_writes_two_reads_from_two_fds,
		test_request_setup,
		test_request_tear_down,
		MUNIT_TEST_OPTION_NONE,
		test_request_params,
	},
	{
		(char *) "/TAWKIPCREGEP",
		test_TAWKIPCREGEP,
		test_request_setup,
		test_request_tear_down,
		MUNIT_TEST_OPTION_NONE,
		test_request_params,
	},
	{
		(char *) "/TAWKIPCDEREGEP",
		test_TAWKIPCDEREGEP,
		test_request_setup,
		test_request_tear_down,
		MUNIT_TEST_OPTION_NONE,
		test_request_params,
	},
	{
		(char *) "/TAWKIPCDEREGEP_correct_ownership",
		test_TAWKIPCDEREGEP_correct_ownership,
		test_request_setup,
		test_request_tear_down,
		MUNIT_TEST_OPTION_NONE,
		test_request_params,
	},
	{
		(char *) "/Non_blocking_read",
		test_non_blocking_read,
		test_non_blocking_request_setup,
		test_request_tear_down,
		MUNIT_TEST_OPTION_NONE,
		test_request_params,
	},
	{
		(char *) "/Poll_Read_Readiness",
		test_poll_read_readiness,
		test_non_blocking_request_setup,
		test_request_tear_down,
		MUNIT_TEST_OPTION_NONE,
		test_request_params,
	},
	{
		(char *) "/Select_Read_Readiness",
		test_select_read_readiness,
		test_non_blocking_request_setup,
		test_request_tear_down,
		MUNIT_TEST_OPTION_NONE,
		test_request_params,
	},
	{
		(char *) "/Poll_Write_Readiness",
		test_poll_write_readiness,
		test_non_blocking_request_setup,
		test_request_tear_down,
		MUNIT_TEST_OPTION_NONE,
		test_request_params,
	},
	{
		(char *) "/Create_Destroy_Exclusive_Pool",
		test_create_and_destroy_exclusive_pool,
		test_non_blocking_request_setup,
		test_request_tear_down,
		MUNIT_TEST_OPTION_NONE,
		test_request_params,
	},
	{
		(char *) "/TAWKIPCSETPLSIZE",
		test_TAWKIPCSETPLSIZE,
		test_non_blocking_request_setup,
		test_request_tear_down,
		MUNIT_TEST_OPTION_NONE,
		test_request_params,
	},
	{
		(char *) "/TAWKIPCSETPLSIZE_2FD",
		test_TAWKIPCSETPLSIZE_2FD,
		test_non_blocking_request_setup,
		test_request_tear_down,
		MUNIT_TEST_OPTION_NONE,
		test_request_params,
	},
	{
		(char *) "/Reorder",
		test_REORDER,
		test_request_setup,
		test_request_tear_down,
		MUNIT_TEST_OPTION_NONE,
		test_request_params,
	},
	{
		(char *) "/Link_Down_Mode",
		test_link_down_mode,
		test_request_setup,
		test_request_tear_down,
		MUNIT_TEST_OPTION_NONE,
		test_request_params,
	},
	{ NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }
};

static const MunitSuite test_suite = {
	/* Name to be pre-pended to all test names */
	(char *) "/system_test",
	/* Arry of munit tests */
	test_suite_tests,
	/* Sub-test-suites */
	NULL,
	/* Run multiple iterations of tests */
	1,
	/* Additional settings */
	MUNIT_SUITE_OPTION_NONE
};

int main(int argc, char *argv[MUNIT_ARRAY_PARAM(argc + 1)])
{
	return munit_suite_main(&test_suite, (void *) "IPC driver tests", argc, argv);
}
