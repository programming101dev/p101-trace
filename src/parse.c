#include "parse.h"
#include <p101_c/p101_string.h>
#include <p101_tool_event/event.h>

static enum line_status map_parse_status(p101_tool_event_parse_status status);
static enum line_status unknown_parse_status(void);

enum line_status p101_trace_parse_call_line(const struct p101_env *env, char *line, struct call_event *event)
{
    enum line_status              status;
    p101_tool_event_parse_status  parse_status;
    struct p101_tool_event_record record;

    status = LINE_MALFORMED;

    if(line == NULL || event == NULL)
    {
        goto done;
    }

    parse_status = p101_tool_event_parse_line(line, &record);
    status       = map_parse_status(parse_status);

    if(status != LINE_OK)
    {
        goto done;
    }

    event->version        = record.version;
    event->pid            = record.pid;
    event->context_id     = record.context_id;
    event->event_sequence = record.sequence;
    if(record.record_kind == P101_TOOL_EVENT_RECORD_COMPLETE)
    {
        event->write_failed     = record.write_failed;
        event->write_errno      = record.write_errno;
        event->events_attempted = record.events_attempted;
        status                  = LINE_COMPLETE;
        goto done;
    }
    if(record.record_kind == P101_TOOL_EVENT_RECORD_FORK)
    {
        event->kind      = CALL_EVENT_FORK;
        event->child_pid = record.child_pid;
        status           = LINE_OK;
        goto done;
    }

    if(record.record_kind != P101_TOOL_EVENT_RECORD_CALL)
    {
        status = LINE_OTHER;
        goto done;
    }

    event->pid                    = record.pid;
    event->context_id             = record.context_id;
    event->event_sequence         = record.sequence;
    event->monotonic_ns           = record.monotonic_ns;
    event->monotonic_ns_available = record.monotonic_ns_available;
    event->kind                   = (record.call_kind == P101_TOOL_EVENT_CALL_ENTER) ? CALL_EVENT_ENTER : CALL_EVENT_EXIT;
    event->line_number            = record.line_number;
    event->function_name          = record.function_name;
    event->call_name              = record.call_name;
    event->arguments              = record.arguments;
    event->result                 = record.result;
    event->file_name              = record.file_name;

    (void)env;

done:
    return status;
}

static enum line_status map_parse_status(p101_tool_event_parse_status status)
{
    enum line_status mapped;

#ifdef __clang__
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wcovered-switch-default"
#endif
    switch(status)
    {
        case P101_TOOL_EVENT_PARSE_OTHER:
        {
            mapped = LINE_OTHER;
            break;
        }
        case P101_TOOL_EVENT_PARSE_OK:
        {
            mapped = LINE_OK;
            break;
        }
        case P101_TOOL_EVENT_PARSE_BAD_VERSION:
        {
            mapped = LINE_BAD_VERSION;
            break;
        }
        case P101_TOOL_EVENT_PARSE_MALFORMED:
        {
            mapped = LINE_MALFORMED;
            break;
        }
        default:
        {
            mapped = unknown_parse_status();
            break;
        }
    }
#ifdef __clang__
    #pragma clang diagnostic pop
#endif

    return mapped;
}

static enum line_status unknown_parse_status(void)
{
    return LINE_MALFORMED;
}

#ifdef P101_TRACE_TESTING
enum line_status p101_trace_test_map_parse_status(int status)
{
    return map_parse_status((p101_tool_event_parse_status)status);
}
#endif

bool p101_trace_call_line_is_ours(const struct p101_env *env, const char *line)
{
    (void)env;
    return p101_tool_event_line_is_ours(line) != 0;
}
