// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#pragma once

#include <stdbool.h>

/** testset_node_type_t: Type of variant for test definitions */
typedef enum testset_test_def_variant_type_s {
    TESTSET_TEST_DEF_VARIANT_TYPE_INT,
    TESTSET_TEST_DEF_VARIANT_TYPE_STR,
    TESTSET_TEST_DEF_VARIANT_TYPE_BOOL,
} testset_test_def_variant_type_t;

/**
 * testset_test_def_variant_t: Variant container for test definitions
 * @name: Variant name
 * @desc: Description of variant
 * @type: Type of variant in test definition
 * @def_int: Default integer value for @TESTSET_TEST_DEF_VARIANT_TYPE_INT type
 * @def_str: Default string value for @TESTSET_TEST_DEF_VARIANT_TYPE_STR type
 * @def_bool: Default boolean value for @TESTSET_TEST_DEF_VARIANT_TYPE_BOOL type
 *
 * This type is used as a container for tests declared in the application.
 */
typedef struct testset_test_def_variant_s {
    const char *name;
    const char *desc;

    testset_test_def_variant_type_t type;
    union {
        int def_int;
        char *def_str;
        bool def_bool;
    };
} testset_test_def_variant_t;

/**
 * testset_test_def_variant_arr_t: Array with variants for test definitions
 * @n: Actual number of used variants
 * @total: Total number of allocated variants
 * @items: Real array with variants
 */
typedef struct testset_test_def_variant_arr_s {
    int n;
    int total;
    testset_test_def_variant_t **items;
} testset_test_def_variant_arr_t;

/** test_data_t: Test data (forward declaration) */
typedef struct test_data test_data_t;

/**
 * testset_test_impl_fn: Callback type to run a test
 * @data: Test data to use in test run
 */
typedef int (*testset_test_impl_fn)(test_data_t *data);

/**
 * testset_test_def_t: Test definition
 * @test: Test name to use in testsets
 * @impl: Callback to run a test
 * @vars: Array with test variants for this test definition
 */
typedef struct testset_test_def_s {
    const char *test;
    testset_test_impl_fn impl;
    testset_test_def_variant_arr_t vars;
} testset_test_def_t;

/**
 * testset_test_def_alloc: Allocate test definition using given parameters
 * @test: Test name to use in testsets
 * @impl: Callback to run a test
 */
extern testset_test_def_t *testset_test_def_alloc(const char *test,
                                                  testset_test_impl_fn impl);

/**
 * testset_test_def_free: Release test definition and related resources
 * @test_def: Test definition to release
 */
extern void testset_test_def_free(testset_test_def_t *test_def);

/**
 * testset_test_def_add_var_int: Add test integer variant to test definition
 * @test_def: Test definition to add new variant
 * @name: Name of test variant to add
 * @desc: Description of test variant
 * @dflt: Default value for this variant
 *
 * The default value should be used if we don't specify this variant in testset.
 */
extern void testset_test_def_add_var_int(testset_test_def_t *test_def,
                                         const char *name, const char *desc,
                                         int dflt);

/**
 * testset_test_def_add_var_str: Add test string variant to test definition
 * @test_def: Test definition to add new variant
 * @name: Name of test variant to add
 * @desc: Description of test variant
 * @dflt: Default value for this variant
 *
 * The default value should be used if we don't specify this variant in testset.
 */
extern void testset_test_def_add_var_str(testset_test_def_t *test_def,
                                         const char *name, const char *desc,
                                         char *dflt);

/**
 * testset_test_def_add_var_bool: Add test boolean variant to test definition
 * @test_def: Test definition to add new variant
 * @name: Name of test variant to add
 * @desc: Description of test variant
 * @dflt: Default value for this variant
 *
 * The default value should be used if we don't specify this variant in testset.
 */
extern void testset_test_def_add_var_bool(testset_test_def_t *test_def,
                                          const char *name, const char *desc,
                                          bool dflt);

/**
 * testset_test_def_arr_t: Array with test definitions
 * @n: Actual number of used test definitions
 * @total: Total number of allocated test definitions
 * @items: Real array with test definitions
 */
typedef struct testset_test_def_arr_s {
    int n;
    int total;
    testset_test_def_t **items;
} testset_test_def_arr_t;

/** test_defs: List of test definitions to use during test run */
extern testset_test_def_arr_t test_defs;

/** TESTSET_TEST_DEFS_INITIALIZER: Macro to initialize testset_test_def_arr_t */
#define TESTSET_TEST_DEFS_INITIALIZER()                                        \
    (testset_test_def_arr_t) { .n = 0, .total = 0, .items = NULL, }

/**
 * testset_test_defs_fini: Function to release allocated test definitions
 * @test_defs: Array with test definitions to release
 */
extern void testset_test_defs_fini(testset_test_def_arr_t *test_defs);

/**
 * testset_test_defs_add_test_def: Add new test definition to array
 * @test_defs: Array with test definitions to add the new test definition
 * @test_def: Test definition to add
 */
extern void testset_test_defs_add_test_def(testset_test_def_arr_t *test_defs,
                                           testset_test_def_t *test_def);

/**
 * testset_test_variant_t: Test variant container
 * @name: Name of test variant
 * @value: Test variant value
 *
 * This type is used as a container for any testset source, e.g: yaml, json or
 * command line.
 */
typedef struct testset_test_variant_s {
    char *name;
    char *value;
} testset_test_variant_t;

/**
 * testset_test_variant_arr_t: Array with test variants
 * @n: Actual number of used test variants
 * @total: Total number of allocated test variants
 * @items: Real array with test variants
 */
typedef struct testset_test_variant_arr_s {
    int n;
    int total;
    testset_test_variant_t **items;
} testset_test_variant_arr_t;

/**
 * testset_test_t: Test container
 * @test: Test definition
 * @vars: Array with test variants
 */
typedef struct testset_test_s {
    testset_test_def_t *def;
    testset_test_variant_arr_t vars;
} testset_test_t;

/**
 * testset_test_var_as_int: Function to get variant as integer
 * @test: Test container to get variant
 * @name: Name of required test variant
 */
extern int testset_test_var_as_int(testset_test_t *test, const char *name);

/**
 * testset_test_var_as_int: Function to get variant as string
 * @test: Test container to get variant
 * @name: Name of required test variant
 */
extern char *testset_test_var_as_str(testset_test_t *test, const char *name);

/**
 * testset_test_var_as_int: Function to get variant as boolean
 * @test: Test container to get variant
 * @name: Name of required test variant
 */
extern bool testset_test_var_as_bool(testset_test_t *test, const char *name);

/** testset_node_type_t: Type of testset node */
typedef enum testset_node_type_s {
    TESTSET_NODE_TYPE_NONE,
    TESTSET_NODE_TYPE_TEST,
    TESTSET_NODE_TYPE_PARALLEL,
} testset_node_type_t;

typedef struct testset_node_arr_s testset_node_arr_t;

/**
 * testset_node_t: Testset node
 * @type: Type of testset node
 * @test: Test container
 * @parallel: Array with testset nodes to run in parallel
 */
typedef struct testset_node_s {
    testset_node_type_t type;
    union {
        testset_test_t *test;
        testset_node_arr_t *parallel;
    };
} testset_node_t;

/**
 * testset_node_arr_t: Array with testset nodes
 * @n: Actual number of used testset nodes
 * @total: Total number of allocated testset nodes
 * @items: Real array with testset nodes
 */
typedef struct testset_node_arr_s {
    int n;
    int total;
    testset_node_t **items;
} testset_node_arr_t;

/**
 * testset_t: Context with information about required tests and test parameters
 * @fname: Name of file with definition of testset
 * @nodes: Array with testset nodes
 */
typedef struct testset_s {
    const char *fname;
    testset_node_arr_t nodes;
} testset_t;

/**
 * testset_alloc: Allocate testset with given file name to use
 * @fname: Name of file with definition of testset
 *
 * Note: This function will not load the given file, just keep it in testset_t
 */
extern testset_t *testset_alloc(const char *fname);

/**
 * testset_alloc: Release testset and all related resources
 * @testset: Testset context to release resources
 */
extern void testset_free(testset_t *testset);

/**
 * testset_load: Load testset resources from file
 * @testset: Testset context to load
 */
extern void testset_load(testset_t *testset);
