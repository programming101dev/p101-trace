#ifndef P101_TRACE_RUNNER_H
#define P101_TRACE_RUNNER_H

#include "arguments.h"
#include <p101_env/env.h>
#include <p101_error/error.h>
#include <stdio.h>

int   p101_trace_run(const struct p101_env *env, struct p101_error *err, const struct arguments *args);
FILE *p101_trace_open_log(const struct p101_env *env, struct p101_error *err, const char *path, int *owned);

#endif    // P101_TRACE_RUNNER_H
