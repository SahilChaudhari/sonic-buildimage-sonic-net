// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#pragma once

#include <stdbool.h>

/**
 * arg_parse_option_set_fn: Callback type to parse option
 * @ctx: Context to use during the parse
 * @value: Real value to parse for this option (unused for flag option)
 */
typedef int (*arg_parse_option_set_fn)(void *ctx, const char *value);

/**
 * app_set_help: Special callback for help option
 * @ctx: Context (unused, but we must have it for arg_parse_option_set_fn types)
 * @value: Real value to parse (unused, we don't have value for flag options)
 */
extern int app_set_help(void *ctx, const char *value);

/**
 * arg_parse_option_t: Type to specify test option
 * @short_name: Symbol for short test option form (e.g: 's' for -s)
 * @long_name: Name for long test option form (e.g: "name" for --name)
 * @desc: Description for specified test option
 * @set_fn: Callback to clarify parsing of test option
 * @set_ctx: Context for @set_fn callback
 * @is_flag: Type of option:
 *           - flag (just test option name) or
 *           - option pair (--key=value or --key value)
 * @end: Flag to finalize list of test options
 */
typedef struct arg_parse_option_s {
    char short_name;
    const char *long_name;
    const char *desc;
    arg_parse_option_set_fn set_fn;
    void *set_ctx;

    bool is_flag;
    bool end;
} arg_parse_option_t;

/** ARG_PARSE_OPTION_HELP: Macro to declare help test option */
#define ARG_PARSE_OPTION_HELP                                                  \
    (arg_parse_option_t)                                                       \
    {                                                                          \
        'h', "help", "Show this help message and exit", app_set_help,          \
            .is_flag = true                                                    \
    }

/** ARG_PARSE_OPTION_END: Macro to finalize list of test options */
#define ARG_PARSE_OPTION_END                                                   \
    (arg_parse_option_t) { .end = true }

/**
 * arg_parse: Function to parse test options using given arguments
 * @argc: Number of arguments in @argv
 * @argv: Array with arguments from command line
 * @opts: List of possible test options with @ARG_PARSE_OPTION_END at the end
 */
extern void arg_parse(int argc, char *argv[], arg_parse_option_t *opts);
