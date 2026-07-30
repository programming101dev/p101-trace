#ifndef P101_TRACE_EVENT_H
#define P101_TRACE_EVENT_H

#include <stddef.h>

enum call_event_kind
{
    CALL_EVENT_ENTER = 0,
    CALL_EVENT_EXIT,
    CALL_EVENT_FORK
};

enum line_status
{
    LINE_OTHER = 0,
    LINE_OK,
    LINE_COMPLETE,
    LINE_MALFORMED,
    LINE_BAD_VERSION
};

struct call_event
{
    int                  version;
    size_t               sequence;
    long                 pid;
    long                 child_pid;
    size_t               context_id;
    size_t               event_sequence;
    size_t               monotonic_ns;
    int                  monotonic_ns_available;
    enum call_event_kind kind;
    int                  line_number;
    const char          *function_name;
    const char          *call_name;
    const char          *arguments;
    const char          *result;
    const char          *file_name;
    int                  write_failed;
    int                  write_errno;
    size_t               events_attempted;
};

#endif    // P101_TRACE_EVENT_H
