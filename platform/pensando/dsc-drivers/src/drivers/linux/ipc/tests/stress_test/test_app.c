// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#include <stdio.h>
#include <pthread.h>
#include <stdint.h>
#include <assert.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>
#include <string.h>
#include <alloca.h>
#include <poll.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <ipccmd/ipc_cmd_client.h>
#include <ipccmd/ipc_cmd_dev.h>
#include <tawk_ipc_drv/ioctl.h>

/* IPC MESSAGE HDR userspace<->driver */
#include "header/protocol.h"
#include "tawktest_cmd.h"
#include "arg_parse.h"
#include "testset.h"

/*
 * ----- Theory of Operation -----
 *
 * This test application aims to place the entire TAWK IPC stack under stress
 * in a variety of different scenarios. A test is specified by a list of
 * parameters (test_params_t) and executed by test_runner.
 *
 * It is necessary that TAWKTEST is running on the remote. TAWKTEST enables
 * remote control of tests, such that one can start, poll and finish a
 * "requester" or "responder". A "requester" is a process that is sending
 * requests to the X86, and similarly a "responder" is responding to requests
 * from the X86.
 *
 * Lifecyle of a test:
 *  1. Open device file using ipccmd library
 *  2. Start "requester" or "responder" (with tawktest_start_* - uses ipccmd)
 *  3. Write/Read requests/responses (with tawktest_(handle|send)_*)
 *  4. Finish "requester" or "responder" (with tawktest_finish_* - uses ipccmd)
 *  5. Close device file using ipccmd library
 *
 * IMPORTANT: The file descriptor is shared between code using the ipccmd
 * wrapper and code doing direct reads/writes to the file descriptor. This
 * means there is a very strong chance of breakage (loss of ordering) if any
 * function that uses ipccmd is called during the main body of test. Thus, any
 * function that uses ipccmd should *only* be used before or after the main
 * body of the testcase - most likely in the setup or cleanup.
 */

/**
 * test_params_t: Test parameters
 * @n_req: Total number of requests
 * @min_delay: Min delay between writing requests
 * @max_delay: Max delay between writing requests
 * @min_pld_len: Minimum request payload length
 * @max_pld_len: Maximum request payload length
 * @tgt_ep: Remote endpoint
 * @n_in_flight: Number of requests in flight
 * @req_seed: Request random seed
 * @rsp_seed: Response random seed
 * @pool_size: Size of exclusive request buffer pool
 * @file: Name of file
 * @requester: Local acting as the "requester"
 */
typedef struct test_params {
    uint32_t n_req;
    uint32_t min_delay;
    uint32_t max_delay;
    uint32_t min_pld_len;
    uint32_t max_pld_len;
    uint32_t tgt_ep;
    uint32_t n_in_flight;
    uint32_t req_seed;
    uint32_t rsp_seed;
    uint8_t pool_size;
    char *file;
    bool requester;
} test_params_t;

struct test_responder_state {
    struct tawktest_start_requester_req start_req;
    struct tawktest_start_requester_rsp start_rsp;
    struct tawktest_poll_requester_req poll_req;
    struct tawktest_poll_requester_rsp poll_rsp;
    struct tawktest_finish_requester_req finish_req;
    struct tawktest_finish_requester_rsp finish_rsp;
};

struct test_requester_state {
    struct tawktest_start_responder_req start_req;
    struct tawktest_start_responder_rsp start_rsp;
    struct tawktest_poll_responder_req poll_req;
    struct tawktest_poll_responder_rsp poll_rsp;
    struct tawktest_finish_responder_req finish_req;
    struct tawktest_finish_responder_rsp finish_rsp;
};

/**
 * test_state_t: Test state
 * @ipc_dev: ipccmd device
 * @req_rand: Request random state
 * @rsp_rand: Response random state
 * @req_state: Request state
 * @rsp_state: Response state
 * @requester_cmd: Requester cmd requests and responses
 * @responder_cmd: Responder cmd requests and responses
 */
typedef struct test_state {
    ipc_cmd_dev_t ipc_dev;
    minstd_rand_t req_rand;
    minstd_rand_t rsp_rand;
    tawktest_req_state_t req_state;
    tawktest_rsp_state_t rsp_state;
    struct test_requester_state *requester_cmd;
    struct test_responder_state *responder_cmd;
} test_state_t;

/**
 * test_data_t: Test data
 * @params: Test parameters
 * @data: Current state
 */
typedef struct test_data {
    test_params_t params;
    test_state_t state;
} test_data_t;

/**
 * app_spec_t: Application context to use during the test run
 * @seed: seed value to initialize pseudo-random number generator
 * @extended: Use extended default tests
 * @testsets: Testset context to use during the test run
 * @testset_cnt: Current number of testsets
 * @size: Current size of testset array
 */
typedef struct app_spec_s {
    int seed;
    bool extended;
    testset_t **testsets;
    size_t testset_cnt;
    size_t size;
} app_spec_t;

/** test_defs: List of test definitions to use during test run */
testset_test_def_arr_t test_defs = TESTSET_TEST_DEFS_INITIALIZER();

/* ----- Test Infrastructure Declaration ----- */

static pthread_t testset_test_create_runner(testset_test_t *test);
static pthread_t testset_parallel_create_runner(testset_node_arr_t *parallel);

/* ----- Test Outputting ----- */

static void
print_test_case(testset_test_def_t *def, test_data_t *data)
{
    test_params_t *params = &data->params;

    printf("---------- TEST ----------\n");
    printf("Name: %s\n"
           "Num Reqs: %d\n"
           "Min-Max Delay: %d-%d\n"
           "Min-Max Pld Len: %d-%d\n"
           "Num In-flight: %d\n"
           "Exclusive Pool Size: %d\n"
           "Requester: %s\n"
           "Request Seed: %d\n"
           "Response Seed: %d\n",
           def->test, params->n_req, params->min_delay, params->max_delay,
           params->min_pld_len, params->max_pld_len, params->n_in_flight,
           params->pool_size, params->requester ? "yes" : "no",
           params->req_seed, params->rsp_seed);
}

/* ----- Test Cases ----- */

static pthread_t
testset_node_run_pthread(testset_node_t *node)
{
    switch (node->type) {
    case TESTSET_NODE_TYPE_NONE:
        break;
    case TESTSET_NODE_TYPE_TEST:
        return testset_test_create_runner(node->test);
    case TESTSET_NODE_TYPE_PARALLEL:
        return testset_parallel_create_runner(node->parallel);
    }
    assert(false);
}

static int
testset_node_run(testset_node_t *node)
{
    void *rc           = NULL;
    pthread_t tid_test = testset_node_run_pthread(node);

    if (pthread_join(tid_test, &rc) != 0 || rc != NULL)
        return -EINVAL;

    return 0;
}

/* ----- Test Infrastructure ----- */

static int
start_remote_responder(test_data_t *data)
{
    struct tawktest_start_responder_req *start_req;
    struct tawktest_start_responder_rsp *start_rsp;
    test_params_t *params = &data->params;
    test_state_t *state   = &data->state;
    int rc;

    state->requester_cmd = malloc(sizeof(struct test_requester_state));
    if (!state->requester_cmd)
        return -EFAULT;

    start_req = &state->requester_cmd->start_req;
    start_rsp = &state->requester_cmd->start_rsp;

    *start_req = (struct tawktest_start_responder_req) {
        .delay_us = {
            .min = params->min_delay,
            .max = params->max_delay,
        },
        .req_pld_len = {
            .min = params->min_pld_len,
            .max = params->max_pld_len,
        },
        .rsp_pld_len = {
            .min = params->min_pld_len,
            .max = params->max_pld_len,
        },
        .req_seed = params->req_seed,
        .rsp_seed = params->rsp_seed,
        .ep = params->tgt_ep,
    };

    return tawktest_start_responder(&state->ipc_dev, start_req, start_rsp);
}

static int
start_remote_requester(test_data_t *data)
{
    struct tawktest_start_requester_req *start_req;
    struct tawktest_start_requester_rsp *start_rsp;
    test_params_t *params = &data->params;
    test_state_t *state = &data->state;

    state->responder_cmd = calloc(1, sizeof(struct test_responder_state));
    if (!state->responder_cmd)
        return -EFAULT;

    start_req = &state->responder_cmd->start_req;
    start_rsp = &state->responder_cmd->start_rsp;

    *start_req = (struct tawktest_start_requester_req) {
        .n_req = params->n_req,
        .n_req_in_flight = params->n_in_flight,
        .delay_us = {
            .min = params->min_delay,
            .max = params->max_delay,
        },
        .req_pld_len = {
            .min = params->min_pld_len,
            .max = params->max_pld_len,
        },
        .rsp_pld_len = {
            .min = params->min_pld_len,
            .max = params->max_pld_len,
        },
        .req_seed = params->req_seed,
        .rsp_seed = params->rsp_seed,
        .ep = params->tgt_ep,
    };

    return tawktest_start_requester(&state->ipc_dev, start_req, start_rsp);
}

static int
start_remote(test_data_t *data)
{
    test_params_t *params = &data->params;

    if (params->requester)
        return start_remote_responder(data);
    else
        return start_remote_requester(data);
}

static int
finish_remote_responder(test_data_t *data)
{
    struct tawktest_finish_responder_req *finish_req;
    struct tawktest_finish_responder_rsp *finish_rsp;
    test_state_t *state = &data->state;

    finish_req         = &state->requester_cmd->finish_req;
    finish_rsp         = &state->requester_cmd->finish_rsp;
    finish_req->handle = state->requester_cmd->start_rsp.handle;

    return tawktest_finish_responder(&state->ipc_dev, finish_req, finish_rsp);
}

static int
finish_remote_requester(test_data_t *data)
{
    struct tawktest_finish_requester_req *finish_req;
    struct tawktest_finish_requester_rsp *finish_rsp;
    test_state_t *state = &data->state;

    finish_req = &state->responder_cmd->finish_req;
    finish_rsp = &state->responder_cmd->finish_rsp;
    finish_req->handle = state->responder_cmd->start_rsp.handle;

    return tawktest_finish_requester(&state->ipc_dev, finish_req, finish_rsp);
}

/* Only call when completely finished with any test state */
static int
finish_remote(test_data_t *data)
{
    test_params_t *params = &data->params;
    test_state_t *state   = &data->state;
    int rc;

    if (params->requester) {
        struct tawktest_finish_responder_rsp *finish_rsp =
            &state->requester_cmd->finish_rsp;
        rc = finish_remote_responder(data);
        if (rc) {
            free(state->requester_cmd);
            return rc;
        }

        printf("FINISHED REQUESTER:\n"
               "RSP SENT: %d\n"
               "REQ RCVD: %d\n"
               "REQ VAL_ERR: %d\n",
               finish_rsp->rsp_sent, finish_rsp->req_rcvd,
               finish_rsp->req_val_err);
        free(state->requester_cmd);
    } else {
        struct tawktest_finish_requester_rsp *finish_rsp =
            &state->responder_cmd->finish_rsp;
        rc = finish_remote_requester(data);
        if (rc) {
            free(state->responder_cmd);
            return rc;
        }

        printf("FINISHED REQUESTER:\n"
               "RSP RCVD: %d\n"
               "RSP TP ERR: %d\n"
               "RSP VAL ERR: %d\n",
               finish_rsp->rsp_rcvd, finish_rsp->rsp_tp_err,
               finish_rsp->rsp_val_err);
        free(state->responder_cmd);
    }

    return rc;
}

static int test_blocking(test_data_t *data);

static void
test_blocking_def_add_common_vars(testset_test_def_t *test_def)
{
    testset_test_def_add_var_int(test_def, "n_req", "Total number of requests",
                                 10);
    testset_test_def_add_var_int(test_def, "min_delay",
                                 "Min delay between writing requests", 0);
    testset_test_def_add_var_int(test_def, "max_delay",
                                 "Max delay between writing requests", 0);
    testset_test_def_add_var_int(test_def, "min_pld_len",
                                 "Minimum request payload length", 0);
    testset_test_def_add_var_int(test_def, "max_pld_len",
                                 "Maximum request payload length", 1000);
    testset_test_def_add_var_int(test_def, "tgt_ep", "Remote endpoint", 42);
    testset_test_def_add_var_int(test_def, "n_in_flight",
                                 "Number of requests in flight", 1);
    testset_test_def_add_var_int(test_def, "req_seed", "Request random seed",
                                 1);
    testset_test_def_add_var_int(test_def, "rsp_seed", "Response random seed",
                                 2);
    testset_test_def_add_var_int(test_def, "pool_size",
                                 "Size of exclusive request buffer pool", 0);
    testset_test_def_add_var_str(test_def, "file", "Name of file",
                                 "/dev/tawkipcdev");
    testset_test_def_add_var_bool(test_def, "requester",
                                  "Local acting as the \"requester\"", true);
}

static void test_blocking_def(void) __attribute__((constructor));

static void
test_blocking_def(void)
{
    testset_test_def_t *test_def;

    test_def = testset_test_def_alloc("test_blocking", test_blocking);
    test_blocking_def_add_common_vars(test_def);
    testset_test_defs_add_test_def(&test_defs, test_def);
}

static int
test_blocking_requester(test_data_t *data)
{
    test_params_t *params = &data->params;
    test_state_t *state = &data->state;
    int rc, num_done, num_sent;

    rc = num_done = num_sent = 0;

    while (num_done < params->n_req) {
        if (num_sent < params->n_req &&
            num_sent - num_done < params->n_in_flight) {
            rc = tawktest_send_request(&state->ipc_dev, &state->req_state);
            if (rc)
                return rc;

            num_sent++;
        } else {
            rc = tawktest_handle_response(&state->ipc_dev, &state->rsp_state);
            if (!rc) {
                rc = -EPROTO;
                return rc;
            }

            num_done++;
        }
    }

    while (num_done < num_sent) {
        rc = tawktest_handle_response(&state->ipc_dev, &state->rsp_state);
        if (!rc) {
            rc = -EPROTO;
            return rc;
        }
        num_done++;
    }

    return 0;
}

static int
test_blocking_responder(test_data_t *data)
{
    test_params_t *params = &data->params;
    test_state_t *state = &data->state;
    tawk_drv_hdr_tag_t usr_tag;
    int rc, num_done;

    rc = num_done = 0;

    while (num_done < params->n_req) {
        rc = tawktest_handle_request(&state->ipc_dev, &state->req_state,
                                     &usr_tag);
        if (!rc)
            return -EPROTO;

        rc = tawktest_send_response(&state->ipc_dev, &state->rsp_state, usr_tag);
        if (rc)
            return rc;

        num_done++;
    }

    return 0;
}

static int
test_blocking(test_data_t *data)
{
    test_params_t *params = &data->params;
    test_state_t *state   = &data->state;
    int rc;

    rc = ipc_cmd_dev_open2(&state->ipc_dev, params->file);
    if (rc)
        return rc;

    /* If X86 acting as the "responder", ensure that a request handler is
     * registered on "tgt_ep".
     */
    if (!params->requester) {
        rc = ipc_cmd_dev_bind(&state->ipc_dev, params->tgt_ep);
        if (rc)
            goto err_close_fd;
    }

    rc = start_remote(data);
    if (rc)
        goto err_unbind_ep;

    if (params->requester)
        rc = test_blocking_requester(data);
    else
        rc = test_blocking_responder(data);

    finish_remote(data);
err_unbind_ep:
    if (!params->requester)
        ipc_cmd_dev_unbind(&state->ipc_dev, params->tgt_ep);
err_close_fd:
    ipc_cmd_dev_close(&state->ipc_dev);
    return rc;
}

static int test_non_blocking(test_data_t *data);

static void test_non_blocking_def(void) __attribute__((constructor));

static void
test_non_blocking_def(void)
{
    testset_test_def_t *test_def;

    test_def = testset_test_def_alloc("test_non_blocking", test_non_blocking);
    test_blocking_def_add_common_vars(test_def);
    testset_test_defs_add_test_def(&test_defs, test_def);
}

static int
test_non_blocking_requester(test_data_t *data)
{
    test_params_t *params = &data->params;
    test_state_t *state = &data->state;
    int rc, num_sent, num_done;
    struct pollfd poll_fd;

    rc = num_done = num_sent = 0;

    poll_fd.fd = state->ipc_dev.fd;

    while (num_sent < params->n_req) {
        if (num_sent - num_done < params->n_in_flight)
            poll_fd.events = POLLOUT;
        else
            poll_fd.events = POLLIN;

        poll(&poll_fd, 1, 0);

        if (poll_fd.revents & POLLOUT) {
            assert(num_sent - num_done < params->n_in_flight);
            rc = tawktest_send_request(&state->ipc_dev, &state->req_state);
            if (rc)
                return rc;

            num_sent++;
        } else if (poll_fd.revents & POLLIN) {
            rc = tawktest_handle_response(&state->ipc_dev, &state->rsp_state);
            if (!rc) {
                return -EPROTO;
            }
            num_done++;
        } else {
            usleep(50);
        }
    }

    while (num_done < num_sent) {
        rc = tawktest_handle_response(&state->ipc_dev, &state->rsp_state);
        if (rc)
            num_done++;
    }

    return 0;
}

static int
test_non_blocking_responder(test_data_t *data)
{
    test_params_t *params = &data->params;
    test_state_t *state = &data->state;
    tawk_drv_hdr_tag_t usr_tag;
    struct pollfd poll_fd;
    int rc, num_done;

    rc = num_done = 0;

    poll_fd.fd = state->ipc_dev.fd;

    while (num_done < params->n_req) {
        poll_fd.events = POLLIN; /* Readable */

        poll(&poll_fd, 1, 0);

        if (poll_fd.revents & POLLIN) {
            rc = tawktest_handle_request(&state->ipc_dev, &state->req_state,
                                         &usr_tag);
            if (!rc)
                return -EPROTO;

            poll_fd.events = POLLOUT; /* Writeable */
            while (poll(&poll_fd, 1, 0) <= 0) {
                usleep(50);
            }

            rc = tawktest_send_response(&state->ipc_dev, &state->rsp_state,
                                       usr_tag);
            if (rc)
                return rc;

            num_done++;
        }
    }

    return 0;
}

static int
test_non_blocking(test_data_t *data)
{
    test_params_t *params = &data->params;
    test_state_t *state   = &data->state;
    int rc;

    rc = ipc_cmd_dev_open2(&state->ipc_dev, params->file);
    if (rc)
        return rc;

    if (params->pool_size) {
        rc = ioctl(state->ipc_dev.fd, TAWKIPCSETPLSIZE, params->pool_size);
        if (rc != params->pool_size)
            goto err_close_fd;
    }

    /* If X86 acting as the "responder", ensure that a request handler is
     * registered on "tgt_ep".
     */
    if (!params->requester) {
        rc = ipc_cmd_dev_bind(&state->ipc_dev, params->tgt_ep);
        if (rc)
            goto err_close_fd;
    }

    rc = start_remote(data);
    if (rc)
        goto err_unbind_ep;

    rc = fcntl(state->ipc_dev.fd, F_SETFL, O_RDWR | O_NONBLOCK);
    if (rc)
        goto err_finish_remote;

    if (params->requester)
        rc = test_non_blocking_requester(data);
    else
        rc = test_non_blocking_responder(data);

    fcntl(state->ipc_dev.fd, F_SETFL, O_RDWR);

err_finish_remote:
    finish_remote(data);
err_unbind_ep:
    if (!params->requester)
        ipc_cmd_dev_unbind(&state->ipc_dev, params->tgt_ep);
err_close_fd:
    ipc_cmd_dev_close(&state->ipc_dev);
    return rc;
}

static void
test_init_state(test_data_t *data)
{
    test_params_t *t_params = &data->params;
    test_state_t *t_state   = &data->state;

    minstd_rand_init(&t_state->req_rand, t_params->req_seed);
    minstd_rand_init(&t_state->rsp_rand, t_params->rsp_seed);

    t_state->req_state = (tawktest_req_state_t) {
        .params = {
            .ep = t_params->tgt_ep,
            .min_pld_size = t_params->min_pld_len,
            .max_pld_size = t_params->max_pld_len,
        },
        .prng = t_state->req_rand,
    };

    t_state->rsp_state = (tawktest_rsp_state_t) {
        .params = {
            .min_pld_size = t_params->min_pld_len,
            .max_pld_size = t_params->max_pld_len,
        },
        .prng = t_state->rsp_rand,
    };
}

static int
test_runner(testset_test_t *test)
{
    int rc;
    test_data_t data = {
        .params.n_req       = testset_test_var_as_int(test, "n_req"),
        .params.min_delay   = testset_test_var_as_int(test, "min_delay"),
        .params.max_delay   = testset_test_var_as_int(test, "max_delay"),
        .params.min_pld_len = testset_test_var_as_int(test, "min_pld_len"),
        .params.max_pld_len = testset_test_var_as_int(test, "max_pld_len"),
        .params.tgt_ep      = testset_test_var_as_int(test, "tgt_ep"),
        .params.n_in_flight = testset_test_var_as_int(test, "n_in_flight"),
        .params.file        = testset_test_var_as_str(test, "file"),
        .params.requester   = testset_test_var_as_bool(test, "requester"),
        .params.req_seed    = testset_test_var_as_int(test, "req_seed"),
        .params.rsp_seed    = testset_test_var_as_int(test, "rsp_seed"),
        .params.pool_size   = testset_test_var_as_int(test, "pool_size"),
    };

    print_test_case(test->def, &data);

    test_init_state(&data);

    rc = test->def->impl(&data);

    printf("------ TEST COMPLETE -----\n");
    printf("TEST %s\n", !rc ? "PASSED" : "FAILED");
    printf("\n");

    return rc;
}

static pthread_t
testset_test_create_runner(testset_test_t *test)
{
    pthread_t tid;

    pthread_create(&tid, NULL, (void *)&test_runner, test);
    return tid;
}

/* ----- Parallel Infrastructure ----- */

static int
parallel_runner(testset_node_arr_t *parallel)
{
    int i;
    void *rc             = NULL;
    int total_errors     = 0;
    pthread_t *tid_tests = calloc(parallel->n, sizeof(pthread_t));

    printf("-------- PARALLEL --------\n");

    for (i = 0; i < parallel->n; i++)
        tid_tests[i] = testset_node_run_pthread(parallel->items[i]);

    for (i = 0; i < parallel->n; i++) {
        if (pthread_join(tid_tests[i], &rc) != 0)
            total_errors++;
        else if (rc != NULL)
            total_errors++;
    }

    free(tid_tests);

    printf("--- PARALLEL COMPLETE ----\n");
    printf("TEST %s (total tests: %d, errors: %d)\n",
           total_errors == 0 ? "PASSED" : "FAILED", parallel->n, total_errors);
    printf("\n");

    return total_errors;
}

static pthread_t
testset_parallel_create_runner(testset_node_arr_t *parallel)
{
    pthread_t tid;

    pthread_create(&tid, NULL, (void *)&parallel_runner, parallel);
    return tid;
}

/* ----- Main Loop ----- */

static bool
str_to_int(const char *str, int *value)
{
    char *end;

    *value = strtol(str, &end, 0);
    return end[0] == '\0';
}

static int
app_alloc_testsets(app_spec_t *app)
{
    app->testsets = calloc(1, sizeof(testset_t *));
    if (!app->testsets)
        return -ENOMEM;

    app->size = 1;
    app->testset_cnt = 0;
    return 0;
}

/* Forward declaration */
static int app_alloc_default_testsets(app_spec_t *app);

static int
app_init_testsets(app_spec_t *app)
{
    if (app->testset_cnt == 0)
        return app_alloc_default_testsets(app);

    return 0;
}

static void
app_fini_testsets(app_spec_t *app)
{
    for (int i = 0; i < app->testset_cnt; i++) {
        testset_free(app->testsets[i]);
    }

    free(app->testsets);
}

static int
app_resize_testsets(app_spec_t *app)
{
    app->testsets = realloc(app->testsets, app->size * 2);
    if (!app->testsets)
        return -ENOMEM;

    app->size *= 2;
    return 0;
}

static int
app_add_testset(app_spec_t *app, const char *fname)
{
    testset_t *testset;
    int rc;

    if (app->testset_cnt == app->size) {
        rc = app_resize_testsets(app);
        if (rc)
            return rc;
    }

    testset = testset_alloc(fname);

    app->testsets[app->testset_cnt] = testset;
    app->testset_cnt++;
    return 0;
}

static int
app_set_seed(void *ctx, const char *value)
{
    app_spec_t *app = (app_spec_t *)ctx;

    if (strcmp(value, "random") == 0) {
        app->seed = (int)time(NULL);
        return 0;
    }

    if (!str_to_int(value, &app->seed)) {
        printf("Failed to parse seed value '%s' to int\n", value);
        return -EINVAL;
    }

    return 0;
}

static int
app_set_testset(void *ctx, const char *value)
{
    app_spec_t *app = (app_spec_t *)ctx;

    if (access(value, F_OK) != 0) {
        printf("Failed to find test set to use ('%s')\n", value);
        return -EINVAL;
    }

    return app_add_testset(app, value);
}

static int
app_alloc_default_testsets(app_spec_t *app)
{
    int rc = 0;

    rc = app_set_testset(app, "default.yml");
    if (rc)
        goto err_free_testsets;

    if (app->extended) {
        rc = app_set_testset(app, "parallel.yml");
        if (rc)
            goto err_free_testsets;
    }

    return 0;

err_free_testsets:
    app_fini_testsets(app);
    return rc;
}

static int
app_set_extended(void *ctx, const char *value)
{
    app_spec_t *app = (app_spec_t *)ctx;

    app->extended = true;
    return 0;
}

int
main(int argc, char *argv[])
{
    int i;
    int rc;
    int final_rc   = 0;
    testset_t *testset;
    app_spec_t app = {
        .seed = (int)time(NULL),
    };

    rc = app_alloc_testsets(&app);
    if (rc)
        return rc;

    arg_parse_option_t opts[] = {
        { 's', "seed", "Specify seed value to run tests", app_set_seed, &app },
        { 't', "testset", "Specify file with test sets to run", app_set_testset,
          &app },
        { 'e', "extended", "Specify default extended test files", app_set_extended,
          &app, true },
        ARG_PARSE_OPTION_HELP,
        ARG_PARSE_OPTION_END,
    };

    arg_parse(argc, argv, opts);

    srand(app.seed);

    rc = app_init_testsets(&app);
    if (rc)
        return rc;

    for (int i = 0; i < app.testset_cnt; i++) {
        testset_load(app.testsets[i]);

        for (int j = 0; j < app.testsets[i]->nodes.n; j++) {
            printf("\nTest Case %d:\n", j + 1);
            rc = testset_node_run(app.testsets[i]->nodes.items[j]);
            printf("%s\n", rc == 0 ? "PASS" : "FAIL");
            if (final_rc == 0)
                final_rc = rc;
        }
    }

    printf("\n");

    app_fini_testsets(&app);
    testset_test_defs_fini(&test_defs);

    return final_rc;

err_free_testsets:
    app_fini_testsets(&app);
    return rc;
}
