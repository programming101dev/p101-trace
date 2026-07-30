#ifndef P101_TRACE_MODEL_TYPES_H
#define P101_TRACE_MODEL_TYPES_H

#include "event.h"
#include <p101_tool_event/event.h>
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
    size_t timed_calls;
    size_t total_duration_ns;
    size_t max_duration_ns;
};

struct active_call
{
    size_t site;
    size_t monotonic_ns;
    int    monotonic_ns_available;
};

struct proc_state
{
    long   pid;
    size_t context_id;
    size_t depth;
    size_t max_depth;
    size_t unmatched_exits;
    size_t mismatched_exits;
    size_t abandoned_at_completion;
    struct active_call *active_calls;
    size_t              active_capacity;
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
    struct p101_tool_event_stream_health stream_health;
};

struct site_rank
{
    size_t index;
    size_t total;
};

#endif    // P101_TRACE_MODEL_TYPES_H
