#ifndef P101_TRACE_MODEL_H
#define P101_TRACE_MODEL_H

#include "model_types.h"
#include <p101_env/env.h>
#include <p101_error/error.h>

struct model      *p101_trace_model_create(const struct p101_env *env, struct p101_error *err);
void               p101_trace_model_destroy(const struct p101_env *env, struct model **model);
void               p101_trace_model_count_line(struct model *model, enum line_status status);
struct proc_state *p101_trace_model_ingest(const struct p101_env *env, struct p101_error *err, struct model *model, const struct call_event *event);

#endif    // P101_TRACE_MODEL_H
