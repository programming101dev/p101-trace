#ifndef P101_TRACE_EVENT_H
#define P101_TRACE_EVENT_H

enum call_event_kind
{
    CALL_EVENT_ENTER = 0,
    CALL_EVENT_EXIT
};

enum line_status
{
    LINE_OTHER = 0,
    LINE_OK,
    LINE_MALFORMED,
    LINE_BAD_VERSION
};

struct call_event
{
    long                 pid;
    enum call_event_kind kind;
    int                  line_number;
    const char          *function_name;
    const char          *call_name;
    const char          *arguments;
    const char          *result;
    const char          *file_name;
};

#endif    // P101_TRACE_EVENT_H
