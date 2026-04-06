// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#include "testset.h"

#include "yaml.h"

static void
arr_extend(int n, int *total, void **arr_ptr, int new_items, size_t size_item)
{
    uint8_t *arr;
    int total_items;

    if (n != *total)
        return;

    total_items = *total + new_items;

    *arr_ptr = realloc(*arr_ptr, total_items * size_item);

    arr = (uint8_t *)*arr_ptr;
    memset(&arr[*total * size_item], 0, new_items * size_item);

    *total = total_items;
}

static testset_test_def_variant_t *
testset_test_def_variant_alloc(const char *name, const char *desc)
{
    testset_test_def_variant_t *var;

    var       = calloc(1, sizeof(testset_test_def_variant_t));
    var->name = name;
    var->desc = desc;
    return var;
}

static void
testset_test_def_variant_free(testset_test_def_variant_t *variant)
{
    free(variant);
}

static void
testset_test_var_defs_init(testset_test_def_variant_arr_t *vars)
{
    *vars = (testset_test_def_variant_arr_t){
        .n     = 0,
        .total = 0,
        .items = NULL,
    };
}

static void
testset_test_var_defs_add_variant(testset_test_def_variant_arr_t *vars,
                                  testset_test_def_variant_t *variant)
{
    arr_extend(vars->n, &vars->total, (void **)&vars->items, 16,
               sizeof(testset_test_def_variant_t *));

    vars->items[vars->n] = variant;
    vars->n++;

    assert(vars->n <= vars->total);
}

static testset_test_def_variant_t *
testset_test_var_defs_find(testset_test_def_variant_arr_t *vars,
                           const char *name)
{
    int i;

    for (i = 0; i < vars->n; i++) {
        if (strcmp(vars->items[i]->name, name) == 0)
            return vars->items[i];
    }

    return NULL;
}

static void
testset_test_var_defs_fini(testset_test_def_variant_arr_t *vars)
{
    int i;

    for (i = 0; i < vars->n; i++)
        testset_test_def_variant_free(vars->items[i]);
    free(vars->items);
}

testset_test_def_t *
testset_test_def_alloc(const char *test, testset_test_impl_fn impl)
{
    testset_test_def_t *test_def = calloc(1, sizeof(testset_test_def_t));

    test_def->test = test;
    test_def->impl = impl;
    testset_test_var_defs_init(&test_def->vars);

    return test_def;
}

void
testset_test_def_free(testset_test_def_t *test_def)
{
    testset_test_var_defs_fini(&test_def->vars);
    free(test_def);
}

testset_test_def_variant_t *
testset_test_def_get_var_def(testset_test_def_t *test_def, const char *name)
{
    testset_test_def_variant_t *def;

    def = testset_test_var_defs_find(&test_def->vars, name);
    if (def != NULL)
        return def;

    printf("Failed to get variant '%s', test '%s' doesn't have this variant\n",
           test_def->test, name);
    exit(1);
}

void
testset_test_def_add_var_int(testset_test_def_t *test_def, const char *name,
                             const char *desc, int dflt)
{
    testset_test_def_variant_t *var;

    assert(testset_test_var_defs_find(&test_def->vars, name) == NULL);
    var          = testset_test_def_variant_alloc(name, desc);
    var->type    = TESTSET_TEST_DEF_VARIANT_TYPE_INT;
    var->def_int = dflt;

    testset_test_var_defs_add_variant(&test_def->vars, var);
}

void
testset_test_def_add_var_str(testset_test_def_t *test_def, const char *name,
                             const char *desc, char *dflt)
{
    testset_test_def_variant_t *var;

    assert(testset_test_var_defs_find(&test_def->vars, name) == NULL);
    var          = testset_test_def_variant_alloc(name, desc);
    var->type    = TESTSET_TEST_DEF_VARIANT_TYPE_STR;
    var->def_str = dflt;

    testset_test_var_defs_add_variant(&test_def->vars, var);
}

void
testset_test_def_add_var_bool(testset_test_def_t *test_def, const char *name,
                              const char *desc, bool dflt)
{
    testset_test_def_variant_t *var;

    assert(testset_test_var_defs_find(&test_def->vars, name) == NULL);
    var           = testset_test_def_variant_alloc(name, desc);
    var->type     = TESTSET_TEST_DEF_VARIANT_TYPE_BOOL;
    var->def_bool = dflt;

    testset_test_var_defs_add_variant(&test_def->vars, var);
}

void
testset_test_defs_fini(testset_test_def_arr_t *test_defs)
{
    int i;

    for (i = 0; i < test_defs->n; i++)
        testset_test_def_free(test_defs->items[i]);
    free(test_defs->items);
}

void
testset_test_defs_add_test_def(testset_test_def_arr_t *test_defs,
                               testset_test_def_t *test_def)
{
    arr_extend(test_defs->n, &test_defs->total, (void **)&test_defs->items, 16,
               sizeof(testset_test_def_t *));

    test_defs->items[test_defs->n] = test_def;
    test_defs->n++;

    assert(test_defs->n <= test_defs->total);
}

static testset_test_def_t *
testset_test_defs_get(testset_test_def_arr_t *test_defs, const char *test_name)
{
    int i;

    for (i = 0; i < test_defs->n; i++) {
        if (strcmp(test_defs->items[i]->test, test_name) == 0)
            return test_defs->items[i];
    }

    printf("Failed to get test definition '%s'\n", test_name);
    exit(1);
}

static testset_test_variant_t *
testset_test_variant_alloc(void)
{
    return calloc(1, sizeof(testset_test_variant_t));
}

static void
testset_test_variant_free(testset_test_variant_t *variant)
{
    free(variant->name);
    free(variant->value);
    free(variant);
}

static void
testset_test_vars_init(testset_test_variant_arr_t *vars)
{
    *vars = (testset_test_variant_arr_t){
        .n     = 0,
        .total = 0,
        .items = NULL,
    };
}

static void
testset_test_vars_add_variant(testset_test_variant_arr_t *vars,
                              testset_test_variant_t *variant)
{
    arr_extend(vars->n, &vars->total, (void **)&vars->items, 16,
               sizeof(testset_test_variant_t *));

    vars->items[vars->n] = variant;
    vars->n++;

    assert(vars->n <= vars->total);
}

static testset_test_variant_t *
testset_test_vars_find(testset_test_variant_arr_t *vars, const char *name)
{
    int i;

    for (i = 0; i < vars->n; i++) {
        if (strcmp(vars->items[i]->name, name) == 0)
            return vars->items[i];
    }

    return NULL;
}

static void
testset_test_vars_fini(testset_test_variant_arr_t *vars)
{
    int i;

    for (i = 0; i < vars->n; i++)
        testset_test_variant_free(vars->items[i]);
    free(vars->items);
}

static testset_test_t *
testset_test_alloc(testset_test_variant_t *variant)
{
    testset_test_t *test = calloc(1, sizeof(testset_test_t));

    test->def = testset_test_defs_get(&test_defs, variant->value);

    testset_test_vars_init(&test->vars);

    return test;
}

#define random_limit(upper) ({ (uint64_t)rand() * upper / RAND_MAX; })

#define random_in_range(upper, lower)                                          \
    ({ (((uint64_t)rand()) % (upper - lower + 1)) + lower; })

static bool
str_to_int(const char *str, int *value)
{
    char *end;

    *value = strtol(str, &end, 0);
    return end[0] == '\0';
}

int
testset_test_var_as_int(testset_test_t *test, const char *name)
{
    int result;
    testset_test_variant_t *variant;

    variant = testset_test_vars_find(&test->vars, name);
    if (variant == NULL) {
        testset_test_def_variant_t *def;

        def = testset_test_def_get_var_def(test->def, name);
        assert(def->type == TESTSET_TEST_DEF_VARIANT_TYPE_INT);
        return def->def_int;
    }

    if (strstr(variant->value, "random_limit") == variant->value) {
        char *value = strchr(variant->value, ':');

        if (value != NULL) {
            if (str_to_int(value + 1, &result))
                return (int)random_limit(result);
        }
    }

    if (!str_to_int(variant->value, &result)) {
        printf("Failed to parse variant value '%s' as int\n", variant->value);
        exit(1);
    }

    return result;
}

char *
testset_test_var_as_str(testset_test_t *test, const char *name)
{
    testset_test_variant_t *variant;

    variant = testset_test_vars_find(&test->vars, name);
    if (variant == NULL) {
        testset_test_def_variant_t *def;

        def = testset_test_def_get_var_def(test->def, name);
        assert(def->type == TESTSET_TEST_DEF_VARIANT_TYPE_STR);
        return def->def_str;
    }

    return variant->value;
}

bool
testset_test_var_as_bool(testset_test_t *test, const char *name)
{
    testset_test_variant_t *variant;

    variant = testset_test_vars_find(&test->vars, name);
    if (variant == NULL) {
        testset_test_def_variant_t *def;

        def = testset_test_def_get_var_def(test->def, name);
        assert(def->type == TESTSET_TEST_DEF_VARIANT_TYPE_BOOL);
        return def->def_bool;
    }

    if (strcmp(variant->value, "true") == 0)
        return true;
    if (strcmp(variant->value, "false") == 0)
        return false;

    printf("Failed to parse variant value '%s' as bool\n", variant->value);
    exit(1);
}

static void
testset_test_free(testset_test_t *test)
{
    testset_test_vars_fini(&test->vars);
    free(test);
}

static void
testset_test_check_vars(testset_test_t *test)
{
    int i;
    testset_test_def_variant_arr_t *var_defs = &test->def->vars;

    for (i = 0; i < test->vars.n; i++) {
        testset_test_variant_t *var = test->vars.items[i];
        testset_test_def_variant_t *def;

        def = testset_test_var_defs_find(var_defs, var->name);
        if (def == NULL) {
            printf("Failed to check test '%s', '%s' variant is unknown\n",
                   test->def->test, var->name);
            exit(1);
        }
    }
}

static testset_node_t *
testset_node_alloc(void)
{
    testset_node_t *node = calloc(1, sizeof(testset_node_t));

    node->type = TESTSET_NODE_TYPE_NONE;

    return node;
}

static void testset_nodes_fini(testset_node_arr_t *nodes);

static void
testset_node_free(testset_node_t *node)
{
    switch (node->type) {
    case TESTSET_NODE_TYPE_NONE:
        break;
    case TESTSET_NODE_TYPE_TEST:
        testset_test_free(node->test);
        break;
    case TESTSET_NODE_TYPE_PARALLEL:
        testset_nodes_fini(node->parallel);
        free(node->parallel);
        break;
    }
    free(node);
}

static void
testset_nodes_init(testset_node_arr_t *nodes)
{
    *nodes = (testset_node_arr_t){
        .n     = 0,
        .total = 0,
        .items = NULL,
    };
}

static void
testset_nodes_add_node(testset_node_arr_t *nodes, testset_node_t *node)
{
    arr_extend(nodes->n, &nodes->total, (void **)&nodes->items, 16,
               sizeof(testset_node_t *));

    nodes->items[nodes->n] = node;
    nodes->n++;

    assert(nodes->n <= nodes->total);
}

static void
testset_nodes_fini(testset_node_arr_t *nodes)
{
    int i;

    for (i = 0; i < nodes->n; i++)
        testset_node_free(nodes->items[i]);
    free(nodes->items);
}

testset_t *
testset_alloc(const char *fname)
{
    testset_t *testset = calloc(1, sizeof(testset_t));

    testset->fname = fname;
    return testset;
}

void
testset_free(testset_t *testset)
{
    testset_nodes_fini(&testset->nodes);
    free(testset);
}

static const char *
yaml_token_type_to_str(yaml_token_type_t type)
{
#define CASE_TO_STR(x_)                                                        \
    case YAML_##x_##_TOKEN:                                                    \
        return #x_

    switch (type) {
        CASE_TO_STR(NO);
        CASE_TO_STR(STREAM_START);
        CASE_TO_STR(STREAM_END);
        CASE_TO_STR(VERSION_DIRECTIVE);
        CASE_TO_STR(TAG_DIRECTIVE);
        CASE_TO_STR(DOCUMENT_START);
        CASE_TO_STR(DOCUMENT_END);
        CASE_TO_STR(BLOCK_SEQUENCE_START);
        CASE_TO_STR(BLOCK_MAPPING_START);
        CASE_TO_STR(BLOCK_END);
        CASE_TO_STR(FLOW_SEQUENCE_START);
        CASE_TO_STR(FLOW_SEQUENCE_END);
        CASE_TO_STR(FLOW_MAPPING_START);
        CASE_TO_STR(FLOW_MAPPING_END);
        CASE_TO_STR(BLOCK_ENTRY);
        CASE_TO_STR(FLOW_ENTRY);
        CASE_TO_STR(KEY);
        CASE_TO_STR(VALUE);
        CASE_TO_STR(ALIAS);
        CASE_TO_STR(ANCHOR);
        CASE_TO_STR(TAG);
        CASE_TO_STR(SCALAR);
    }

    return "<UNKNOWN>";
}

static int
parse_test_variant(yaml_parser_t *parser, yaml_token_t *token,
                   testset_test_variant_t *variant)
{
    assert(token->type == YAML_KEY_TOKEN);
    yaml_token_delete(token);

    yaml_parser_scan(parser, token);
    assert(token->type == YAML_SCALAR_TOKEN);
    variant->name = strdup((const char *)token->data.scalar.value);
    yaml_token_delete(token);

    yaml_parser_scan(parser, token);
    assert(token->type == YAML_VALUE_TOKEN);
    yaml_token_delete(token);

    yaml_parser_scan(parser, token);
    if (token->type == YAML_SCALAR_TOKEN) {
        variant->value = strdup((const char *)token->data.scalar.value);
        yaml_token_delete(token);

        yaml_parser_scan(parser, token);
    }

    return 0;
}

static int
parse_node_test_vars(yaml_parser_t *parser, yaml_token_t *token,
                     testset_test_variant_arr_t *vars)
{
    int rc;

    while (token->type == YAML_KEY_TOKEN) {
        testset_test_variant_t *variant = testset_test_variant_alloc();

        rc = parse_test_variant(parser, token, variant);
        if (rc != 0)
            return rc;

        testset_test_vars_add_variant(vars, variant);
    }

    return 0;
}

static int parse_nodes(yaml_parser_t *parser, yaml_token_t *token,
                       testset_node_arr_t *nodes);

static int
parse_node(yaml_parser_t *parser, yaml_token_t *token, testset_node_t *node)
{
    int rc;
    testset_test_variant_t *variant;

    assert(token->type == YAML_BLOCK_ENTRY_TOKEN);
    yaml_token_delete(token);

    yaml_parser_scan(parser, token);
    assert(token->type == YAML_BLOCK_MAPPING_START_TOKEN);
    yaml_token_delete(token);

    variant = testset_test_variant_alloc();
    yaml_parser_scan(parser, token);
    rc = parse_test_variant(parser, token, variant);
    if (rc != 0)
        return rc;

    if (strcmp(variant->name, "test") == 0) {
        testset_test_t *test = testset_test_alloc(variant);

        testset_test_variant_free(variant);

        rc = parse_node_test_vars(parser, token, &test->vars);
        if (rc != 0)
            return rc;

        testset_test_check_vars(test);

        *node = (testset_node_t){
            .type = TESTSET_NODE_TYPE_TEST,
            .test = test,
        };
    } else if (strcmp(variant->name, "parallel") == 0) {
        testset_node_arr_t *parallel = calloc(1, sizeof(testset_node_arr_t));

        testset_test_variant_free(variant);

        parse_nodes(parser, token, parallel);

        *node = (testset_node_t){
            .type     = TESTSET_NODE_TYPE_PARALLEL,
            .parallel = parallel,
        };
    } else {
        printf("Unknown type of testset node to use: '%s'\n", variant->name);
        return -EINVAL;
    }

    return 0;
}

static int
parse_nodes(yaml_parser_t *parser, yaml_token_t *token,
            testset_node_arr_t *nodes)
{
    int rc;

    assert(token->type == YAML_BLOCK_ENTRY_TOKEN);

    testset_nodes_init(nodes);

    while (true) {
        switch (token->type) {
        case YAML_BLOCK_ENTRY_TOKEN: {
            testset_node_t *node = calloc(1, sizeof(testset_node_t));

            rc = parse_node(parser, token, node);
            if (rc != 0)
                return rc;
            testset_nodes_add_node(nodes, node);
            assert(token->type == YAML_BLOCK_END_TOKEN);
            break;
        }

        case YAML_BLOCK_END_TOKEN:
            return 0;

        default:
            return -EINVAL;
        }
        yaml_token_delete(token);

        yaml_parser_scan(parser, token);
    }

    return 0;
}

void
testset_load(testset_t *testset)
{
    yaml_parser_t parser;
    FILE *fh = fopen(testset->fname, "r");
    yaml_token_t token;

    if (fh == NULL) {
        fprintf(stderr, "Failed to open file: '%s'\n", testset->fname);
        exit(1);
    }

    yaml_parser_initialize(&parser);
    yaml_parser_set_input_file(&parser, fh);

    do {
        int rc = 0;

        yaml_parser_scan(&parser, &token);

        switch (token.type) {
        case YAML_DOCUMENT_START_TOKEN:
        case YAML_STREAM_START_TOKEN:
        case YAML_STREAM_END_TOKEN:
            break;

        case YAML_BLOCK_SEQUENCE_START_TOKEN:
            assert(testset->nodes.n == 0);
            yaml_token_delete(&token);

            yaml_parser_scan(&parser, &token);
            rc = parse_nodes(&parser, &token, &testset->nodes);
            break;

        default:
            rc = -EINVAL;
        }

        if (rc != 0) {
            printf("Failed to load testset '%s', "
                   "got unexpected %s (%d) token "
                   "(rc = %d, line:%zu, column:%zu)\n",
                   testset->fname, yaml_token_type_to_str(token.type),
                   token.type, rc, token.start_mark.line,
                   token.start_mark.column);
            exit(1);
        }

        if (token.type != YAML_STREAM_END_TOKEN)
            yaml_token_delete(&token);
    } while (token.type != YAML_STREAM_END_TOKEN);

    yaml_parser_delete(&parser);

    fclose(fh);
}
