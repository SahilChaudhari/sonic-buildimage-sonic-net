// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include "header/protocol.h"

/* Basic test app to demonstrate read/write */
#define DEVICE "/dev/tawkipcdev"

int main(void)
{
	int cnt, cnt2, fd;
	tawk_drv_hdr_t hdr_out_1;
	tawk_drv_hdr_t hdr_out_2;
	tawk_drv_hdr_t hdr;
	tawk_drv_hdr_t hdr2;
	size_t pld_len = 16;
	size_t pld_len2 = 6;
	uint8_t req_buffer[8 + pld_len];
	uint8_t req_buffer2[8 + pld_len2];
	uint8_t rsp_buffer[8 + 1024];
	uint8_t rsp_buffer2[8 + 1024];

	fd = open(DEVICE, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "failed to open IPC device\n");
		return -1;
	}

	hdr = 0;
	hdr2 = 0;

	tawk_drv_hdr_set_type(&hdr, TAWK_DRV_HDR_TYPE_REQ);
	tawk_drv_hdr_set_pld_len(&hdr, pld_len);
	tawk_drv_hdr_set_req_endpoint(&hdr, 42);
	tawk_drv_hdr_set_tag(&hdr, 0);

	tawk_drv_hdr_set_type(&hdr2, TAWK_DRV_HDR_TYPE_REQ);
	tawk_drv_hdr_set_pld_len(&hdr2, pld_len2);
	tawk_drv_hdr_set_req_endpoint(&hdr2, 42);
	tawk_drv_hdr_set_tag(&hdr2, 1);

	memcpy(&req_buffer, &hdr, sizeof(hdr));
	for (int i = 0; i < pld_len; i++)
		req_buffer[8 + i] = i;

	memcpy(&req_buffer2, &hdr2, sizeof(hdr2));
	for (int i = 0; i < pld_len2; i++)
		req_buffer2[8 + i] = i * i;

	cnt = write(fd, &req_buffer, sizeof(req_buffer));
	if (cnt != sizeof(req_buffer))
		printf("Req 1. Expected write length: %ld Actual write length: %d\n",
		       sizeof(req_buffer), cnt);

	cnt = write(fd, &req_buffer2, sizeof(req_buffer2));
	if (cnt != sizeof(req_buffer2))
		printf("Req 2. Expected write length: %ld Actual write length: %d\n",
		       sizeof(req_buffer2), cnt);

	cnt = read(fd, &rsp_buffer, sizeof(rsp_buffer));
	cnt2 = read(fd, &rsp_buffer2, sizeof(rsp_buffer2));

	printf("--- RESPONSE BUFFER 1 ---\n");
	memcpy(&hdr_out_1, &rsp_buffer, 8);
	printf("TYPE: %d\n", tawk_drv_hdr_get_type(hdr_out_1));
	printf("TAG: %d\n", tawk_drv_hdr_get_tag(hdr_out_1));
	printf("ERROR NO: %d\n", tawk_drv_hdr_get_rsp_errno(hdr_out_1));
	for (int i = 8; i < cnt; i++)
		printf("PAYLOAD: %d\n", rsp_buffer[i]);

	printf("--- RESPONSE BUFFER 2 ---\n");
	memcpy(&hdr_out_2, &rsp_buffer2, 8);
	printf("TYPE: %d\n", tawk_drv_hdr_get_type(hdr_out_2));
	printf("TAG: %d\n", tawk_drv_hdr_get_tag(hdr_out_2));
	printf("ERROR NO: %d\n", tawk_drv_hdr_get_rsp_errno(hdr_out_2));
	for (int i = 8; i < cnt2; i++)
		printf("PAYLOAD: %d\n", rsp_buffer2[i]);

	close(fd);
}
