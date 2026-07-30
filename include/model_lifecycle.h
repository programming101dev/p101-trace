#ifndef P101_TRACE_MODEL_LIFECYCLE_H
#define P101_TRACE_MODEL_LIFECYCLE_H

#include "model_types.h"
#include <p101_env/env.h>
#include <p101_error/error.h>
#include <stdbool.h>

void p101_trace_model_fork(const struct p101_env *env, struct p101_error *err, struct model *model, const struct call_event *event);
void p101_trace_model_complete(const struct p101_env *env, struct p101_error *err, struct model *model, long pid, size_t context_id);
bool p101_trace_model_has_stack_errors(const struct model *model);

#endif    // P101_TRACE_MODEL_LIFECYCLE_H
