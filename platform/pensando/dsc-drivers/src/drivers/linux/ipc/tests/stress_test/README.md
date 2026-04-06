# Stress Test Application

This stress test application provides a generic API for easily creating testcases by specifying a set of parameters; it aims to facilitate putting the TAWK IPC protocol under stress. On the remote end, tawktest is used to respond to requests or send requests to the X86 host.

## Build/Run instructions

```sh
make test_app
sudo ./test_app [-t <test_file>] # By default default.yml
```

> [!NOTE]
> The TAWK IPC driver must be loaded beforehand and tawktest must be running on the remote end.

## Specifying tests

Tests are specified using a YAML configuration file and run sequentially, in parallel or both. A test is either "blocking" or "non_blocking"; a blocking test uses blocking file I/O and likewise non-blocking tests don't block.

### Test paramaters

| Param         | Description |
| ------------- | ----------- |
| test          | "test_blocking" of "test_non_blocking" |
| n_req         | Total number of requests |
| min_delay     | Minimum response delay |
| max_delay     | Maximum response delay |
| min_pld_len   | Minimum payload length |
| max_pld_len   | Maximum payload length |
| tgt_ep        | Target remote endpoint (on Zephyr side) |
| n_in_flight   | Maximum number of requests in-flight |
| req_seed      | Request random seed (used for payload generation/validation) |
| rsp_seed      | Response random seed (used for payload generation/validation) |
| pool_size     | Size of exclusive request buffer pool |
| file          | Devfs file (/dev/tawkipcdev*) |
| requester     | True if host is acting as the "requester" and remote as "responder" |

### Example: Sequential tests

```yaml
---
  - test: test_blocking
    n_req: 10

  - test: test_blocking
    n_req: "random_limit:1000"
    max_delay: 1000

  - test: test_blocking
    n_req: "random_limit:10000"
    max_pld_len: 8
    n_in_flight: 16

  - test: test_non_blocking
    n_req: 1000
    max_delay: 1000
    n_in_flight: 5
```

### Example: Parallel tests

```yaml
- parallel:
  - test: test_blocking
    n_req: "random_limit:1000"
    req_seed: 1
    rsp_seed: 2
    tgt_ep: 42

  - test: test_blocking
    n_req: "random_limit:1000"
    max_delay: 300
    req_seed: 100
    rsp_seed: 1000
    tgt_ep: 43

  - test: test_blocking
    n_req: "random_limit:1000"
    max_delay: 10000
    tgt_ep: 44

  - test: test_non_blocking
    n_req: 5000
    n_in_flight: 8
    tgt_ep: 45
```

It is crucial to specify a different endpoint for each parallel test. Using the same endpoint for multiple threads (tests) will likely cause requests/responses to come back with incorrect payloads.

---

```yaml
SPDX-License-Identifier: MIT
SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.
```
