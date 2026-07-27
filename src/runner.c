#include "runner.h"
#include "constants.h"
#include "model.h"
#include "parse.h"
#include "report.h"
#include <p101_c/p101_stdio.h>
#include <p101_c/p101_string.h>
#include <p101_env/env.h>
#include <stdio.h>
#include <stdlib.h>

int p101_trace_run(const struct p101_env *env, struct p101_error *err, const struct arguments *args)
{
    struct model *model;
    FILE         *stream;
    char          line[LINE_MAX_BYTES];
    int           owned;
    int           ret_val;

    P101_TRACE(env);
    model   = NULL;
    stream  = NULL;
    owned   = 0;
    ret_val = EXIT_TROUBLE;
    stream  = p101_trace_open_log(env, err, args->log_name, &owned);

    if(stream == NULL || p101_error_has_error(err))
    {
        goto done;
    }

    model = p101_trace_model_create(env, err);

    if(model == NULL || p101_error_has_error(err))
    {
        goto done;
    }

    while(p101_error_has_no_error(err))
    {
        struct call_event          event;
        enum line_status           status;
        p101_env_event_line_status line_status;

        line_status = p101_env_read_event_line(err, stream, line, sizeof(line));

        if(line_status == P101_ENV_EVENT_LINE_EOF)
        {
            break;
        }

        if(line_status == P101_ENV_EVENT_LINE_ERROR)
        {
            goto done;
        }

        if(line_status == P101_ENV_EVENT_LINE_MALFORMED)
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
            event.sequence = model->records + 1U;
            p101_trace_model_ingest(env, err, model, &event);

            if(args->mode == TRACE_MODE_TREE)
            {
                const struct proc_state *proc;

                proc = p101_trace_find_proc(env, err, model, event.pid);

                if(proc != NULL)
                {
                    p101_trace_print_tree_event(env, err, &event, proc);
                }
            }
            else if(args->mode == TRACE_MODE_FLAT)
            {
                p101_trace_print_flat_event(env, err, &event);
            }
        }
    }

    if(p101_error_has_error(err))
    {
        goto done;
    }

    if(args->mode == TRACE_MODE_SUMMARY)
    {
        p101_trace_report_summary(env, err, model);
    }

    p101_trace_report_health(env, err, model);
    ret_val = (model->malformed == 0 && model->bad_version == 0) ? EXIT_CLEAN : EXIT_FINDINGS;

done:
    if(owned && stream != NULL)
    {
        p101_fclose(env, err, stream);
    }

    p101_trace_model_destroy(env, &model);

    return ret_val;
}

FILE *p101_trace_open_log(const struct p101_env *env, struct p101_error *err, const char *path, int *owned)
{
    FILE *stream;

    P101_TRACE(env);
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
