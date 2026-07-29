#ifndef P101_TRACE_MODEL_H
#define P101_TRACE_MODEL_H

#include "event.h"
#include <p101_env/env.h>
#include <p101_error/error.h>
#include <stddef.h>

struct call_site
{
    char  *call_name;
    char  *file_name;
    char  *function_name;
    int    line_number;
    size_t enters;
    size_t exits;
    size_t exits_with_result;
    size_t likely_failures;
};

struct proc_state
{
    long   pid;
    size_t context_id;
    size_t depth;
    size_t max_depth;
    size_t unmatched_exits;
};

struct model
{
    struct call_site  *sites;
    size_t             site_count;
    size_t             site_capacity;
    struct proc_state *procs;
    size_t             proc_count;
    size_t             proc_capacity;
    size_t             records;
    size_t             skipped;
    size_t             malformed;
    size_t             bad_version;
};

struct site_rank
{
    size_t index;
    size_t total;
};

struct model      *p101_trace_model_create(const struct p101_env *env, struct p101_error *err);
void               p101_trace_model_destroy(const struct p101_env *env, struct model **model);
void               p101_trace_model_count_line(struct model *model, enum line_status status);
void               p101_trace_model_ingest(const struct p101_env *env, struct p101_error *err, struct model *model, const struct call_event *event);
size_t             p101_trace_intern_site(const struct p101_env *env, struct p101_error *err, struct model *model, const struct call_event *event);
struct proc_state *p101_trace_find_proc(const struct p101_env *env, struct p101_error *err, struct model *model, long pid, size_t context_id);

#endif    // P101_TRACE_MODEL_H
