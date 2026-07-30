#ifndef P101_TRACE_MODEL_IDENTITY_H
#define P101_TRACE_MODEL_IDENTITY_H

#include "model_types.h"
#include <p101_env/env.h>
#include <p101_error/error.h>

size_t             p101_trace_intern_site(const struct p101_env *env, struct p101_error *err, struct model *model, const struct call_event *event);
struct proc_state *p101_trace_find_proc(const struct p101_env *env, struct p101_error *err, struct model *model, long pid, size_t context_id);

#endif    // P101_TRACE_MODEL_IDENTITY_H
