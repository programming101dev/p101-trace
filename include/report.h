#ifndef P101_TRACE_REPORT_H
#define P101_TRACE_REPORT_H

#include "event.h"
#include "model.h"
#include <p101_env/env.h>
#include <p101_error/error.h>

void p101_trace_print_tree_event(const struct p101_env *env, struct p101_error *err, const struct call_event *event, const struct proc_state *proc);
void p101_trace_print_flat_event(const struct p101_env *env, struct p101_error *err, const struct call_event *event);
void p101_trace_report_summary(const struct p101_env *env, struct p101_error *err, const struct model *model);
void p101_trace_report_health(const struct p101_env *env, struct p101_error *err, const struct model *model);

#endif    // P101_TRACE_REPORT_H
