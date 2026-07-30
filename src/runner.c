#include "runner.h"
#include "constants.h"
#include "model.h"
#include "model_identity.h"
#include "model_lifecycle.h"
#include "parse.h"
#include "report.h"
#include <p101_c/p101_stdio.h>
#include <p101_c/p101_string.h>
#include <p101_env/env.h>
#include <p101_tool_event/event.h>
#include <stdio.h>
#include <stdlib.h>

int p101_trace_run(const struct p101_env *env, struct p101_error *err, const struct arguments *args)
{
    struct model *model;
    FILE         *stream;
    char          line[LINE_MAX_BYTES];
    int           owned;
    int           ret_val;

    P101_TRACE_SCOPE(env);
    model   = NULL;
    stream  = NULL;
    owned   = 0;
    ret_val = EXIT_TROUBLE;
    stream  = p101_trace_open_log(env, err, args->log_name, &owned);

    if(stream == NULL)
    {
        goto done;
    }

    model = p101_trace_model_create(env, err);

    if(model == NULL)
    {
        goto done;
    }

    while(p101_error_has_no_error(err))
    {
        struct call_event           event;
        enum line_status            status;
        p101_tool_event_line_status line_status;

        line_status = p101_tool_event_read_line(err, stream, line, sizeof(line));

        if(line_status == P101_TOOL_EVENT_LINE_EOF)
        {
            break;
        }

        if(line_status == P101_TOOL_EVENT_LINE_ERROR)
        {
            goto done;
        }

        if(line_status == P101_TOOL_EVENT_LINE_MALFORMED)
        {
            enum line_status malformed_status;

            malformed_status = LINE_OTHER;

            if(p101_trace_call_line_is_ours(env, line))
            {
                malformed_status = LINE_MALFORMED;
            }

            p101_trace_model_count_line(model, malformed_status);
            continue;
        }

        status = p101_trace_parse_call_line(env, line, &event);
        p101_trace_model_count_line(model, status);

        if(status == LINE_OK)
        {
            struct p101_tool_event_record observed;
            struct proc_state            *proc;

            p101_memset(env, &observed, 0, sizeof(observed));
            observed.version     = event.version;
            observed.record_kind = event.kind == CALL_EVENT_FORK ? P101_TOOL_EVENT_RECORD_FORK : P101_TOOL_EVENT_RECORD_CALL;
            observed.pid         = event.pid;
            observed.context_id  = event.context_id;
            observed.sequence    = event.event_sequence;
            (void)p101_tool_event_stream_health_observe(&model->stream_health, &observed);
            event.sequence = model->records + 1U;
            if(event.kind == CALL_EVENT_FORK)
            {
                p101_trace_model_fork(env, err, model, &event);
                continue;
            }
            proc = p101_trace_model_ingest(env, err, model, &event);
            if(p101_error_has_error(err))
            {
                goto done;
            }

            if(args->mode == TRACE_MODE_TREE)
            {
                p101_trace_print_tree_event(env, err, &event, proc);
            }
            else if(args->mode == TRACE_MODE_FLAT)
            {
                p101_trace_print_flat_event(env, err, &event);
            }
        }
        else if(status == LINE_COMPLETE)
        {
            struct p101_tool_event_record completion;

            p101_memset(env, &completion, 0, sizeof(completion));
            completion.version          = event.version;
            completion.record_kind      = P101_TOOL_EVENT_RECORD_COMPLETE;
            completion.pid              = event.pid;
            completion.context_id       = event.context_id;
            completion.sequence         = event.event_sequence;
            completion.events_attempted = event.events_attempted;
            completion.write_failed     = event.write_failed;
            completion.write_errno      = event.write_errno;
            (void)p101_tool_event_stream_health_observe(&model->stream_health, &completion);
            p101_trace_model_complete(env, err, model, event.pid, event.context_id);
        }
    }

    if(p101_error_has_error(err))
    {
        goto done;
    }

    if(args->mode == TRACE_MODE_SUMMARY)
    {
        p101_trace_report_summary(env, err, model);
        p101_trace_report_slow_calls(env, err, model, args->slow_threshold_ns);
    }

    p101_trace_report_health(env, err, model);
    if(model->malformed != 0U || model->bad_version != 0U || !p101_tool_event_stream_health_is_complete(&model->stream_health))
    {
        ret_val = EXIT_TROUBLE;
    }
    else
    {
        if(p101_trace_model_has_stack_errors(model))
        {
            ret_val = EXIT_FINDINGS;
        }
        else
        {
            ret_val = EXIT_CLEAN;
        }
    }

done:
    if(owned)
    {
        p101_fclose(env, err, stream);
    }

    p101_trace_model_destroy(env, &model);

    return ret_val;
}

FILE *p101_trace_open_log(const struct p101_env *env, struct p101_error *err, const char *path, int *owned)
{
    FILE *stream;

    P101_TRACE_SCOPE(env);
    *owned = 0;

    if(path == NULL || p101_strcmp(env, path, "-") == 0)
    {
        stream = stdin;
        goto done;
    }

    stream = p101_fopen(env, err, path, "r");

    if(stream != NULL)
    {
        *owned = 1;
    }

done:
    return stream;
}
