// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arg_parse.h"

/* Special RC value to display help message and terminate test run */
#define RC_HELP 1

static void
arg_parse_usage(char *argv0, arg_parse_option_t *opts)
{
    int i;
    int len_max    = 1;
    char *app_name = strrchr(argv0, '/');

    if (app_name == NULL)
        app_name = argv0;
    else
        app_name = app_name + 1;

    for (i = 0; !opts[i].end; i++) {
        int len = 1;

        if (opts[i].short_name != '\0')
            len += 2;

        if (opts[i].long_name != NULL) {
            /* for delimiter (", ") between names */
            if (len != 0)
                len += 2;
            len += 2 + sizeof(opts[i].long_name);
        }
        if (len_max < len)
            len_max = len;
    }

    printf("usage: %s [options...]\n\n", app_name);

    printf("options:\n");
    for (i = 0; !opts[i].end; i++) {
        char names[len_max];

        if (opts[i].short_name == '\0') {
            snprintf(names, len_max, "-%c", opts[i].short_name);
        } else if (opts[i].long_name == NULL) {
            snprintf(names, len_max, "--%s", opts[i].long_name);
        } else {
            snprintf(names, len_max, "-%c, --%s", opts[i].short_name,
                     opts[i].long_name);
        }
        printf("  %*s %s\n", -len_max, names, opts[i].desc);
    }
}

int
app_set_help(void *ctx, const char *value)
{
    return RC_HELP;
}

static arg_parse_option_t *
find_arg_by_short_name(const char *name, arg_parse_option_t *opts)
{
    int i;

    if (strlen(name) != 1)
        return NULL;

    for (i = 0; !opts[i].end; i++) {
        if (name[0] == opts[i].short_name)
            return &opts[i];
    }

    return NULL;
}

static arg_parse_option_t *
find_arg_by_long_name(const char *name, const char *end,
                      arg_parse_option_t *opts)
{
    int i;
    int len;

    if (end == NULL)
        len = strlen(name);
    else
        len = end - name;

    for (i = 0; !opts[i].end; i++) {
        if (strlen(opts[i].long_name) == len &&
            memcmp(opts[i].long_name, name, len) == 0) {
            return &opts[i];
        }
    }

    return NULL;
}

void
arg_parse(int argc, char *argv[], arg_parse_option_t *opts)
{
    int i;
    int num_args;

    for (i = 1; i < argc; i += num_args) {
        int rc;
        arg_parse_option_t *arg;
        const char *name  = argv[i];
        const char *value = NULL;

        num_args = 1;

        if (name[0] != '-') {
            arg = NULL;
        } else if (name[1] != '-') {
            arg = find_arg_by_short_name(name + 1, opts);
            if (i + 1 < argc)
                value = argv[i + 1];
            if (arg != NULL && !arg->is_flag)
                num_args = 2;
        } else {
            const char *eq = strchr(name, '=');

            arg = find_arg_by_long_name(name + 2, eq, opts);
            if (eq != NULL) {
                if (arg->is_flag) {
                    printf("ERROR: failed to parse '%s' option, "
                           "don't use '=' for flag option",
                           name);
                    exit(1);
                }
                value = eq + 1;
            } else {
                if (i + 1 < argc)
                    value = argv[i + 1];
                if (arg != NULL && !arg->is_flag)
                    num_args = 2;
            }
        }

        rc = -ENOENT;
        if (arg != NULL)
            rc = arg->set_fn(arg->set_ctx, value);

        switch (rc) {
        case 0:
            break;

        case RC_HELP:
            arg_parse_usage(argv[0], opts);
            exit(0);

        default:
            printf("ERROR: failed to parse '%s' option", name);
            if (value != NULL)
                printf("with value:'%s'", value);
            printf("\n");

            arg_parse_usage(argv[0], opts);
            exit(1);
        }
    }
}
