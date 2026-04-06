// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

enum {
  TAWKTEST_CMD_START_REQUESTER = 1,
  TAWKTEST_CMD_POLL_REQUESTER = 2,
  TAWKTEST_CMD_FINISH_REQUESTER = 3,
  TAWKTEST_CMD_START_RESPONDER = 4,
  TAWKTEST_CMD_POLL_RESPONDER = 5,
  TAWKTEST_CMD_FINISH_RESPONDER = 6,
};

struct tawktest_start_requester_req {
  uint32_t n_req;
  uint32_t n_req_in_flight;
  struct {
    uint32_t min, max;
  } delay_us, req_pld_len, rsp_pld_len;
  uint32_t req_seed, rsp_seed;
  uint32_t ep;
};

struct tawktest_start_requester_rsp {
  uint64_t handle;
};


struct tawktest_poll_requester_req {
  uint64_t handle;
};

struct tawktest_poll_requester_rsp {
  uint32_t done;
  uint32_t req_sent;
  uint32_t rsp_rcvd, rsp_tp_err, rsp_val_err;
};


struct tawktest_finish_requester_req {
  uint64_t handle;
};

struct tawktest_finish_requester_rsp {
  uint32_t req_sent;
  uint32_t rsp_rcvd, rsp_tp_err, rsp_val_err;
};


struct tawktest_start_responder_req {
  struct {
    uint32_t min, max;
  } delay_us, req_pld_len, rsp_pld_len;
  uint32_t req_seed, rsp_seed;
  uint32_t ep;
};

struct tawktest_start_responder_rsp {
  uint64_t handle;
};


struct tawktest_poll_responder_req {
  uint64_t handle;
};

struct tawktest_poll_responder_rsp {
  uint32_t done;
  uint32_t req_rcvd, req_val_err;
  uint32_t rsp_sent;
};


struct tawktest_finish_responder_req {
  uint64_t handle;
};

struct tawktest_finish_responder_rsp {
  uint32_t req_rcvd, req_val_err;
  uint32_t rsp_sent;
};
