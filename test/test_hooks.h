#ifndef P101_TRACE_TEST_HOOKS_H
#define P101_TRACE_TEST_HOOKS_H

#include "arguments.h"
#include "model_types.h"
#include <p101_env/env.h>
#include <p101_error/error.h>
#include <stdbool.h>

bool             p101_trace_test_reserve_sites(const struct p101_env *env, struct p101_error *err, struct model *model);
bool             p101_trace_test_reserve_procs(const struct p101_env *env, struct p101_error *err, struct model *model);
enum line_status p101_trace_test_map_parse_status(int status);
int              p101_trace_test_compare_ranks(const struct site_rank *left, const struct site_rank *right);
void             p101_trace_test_handle_option(const struct p101_env *env, struct p101_error *err, struct arguments *args, int option, const char *option_argument);

#endif
