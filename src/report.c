#include "report.h"
#include <p101_c/p101_stdio.h>
#include <p101_c/p101_stdlib.h>
#include <p101_c/p101_string.h>
#include <stdlib.h>

static int compare_ranks(const void *left, const void *right);

void p101_trace_print_tree_event(const struct p101_env *env, struct p101_error *err, const struct call_event *event, const struct proc_state *proc)
{
    size_t depth;

    if(event->kind == CALL_EVENT_EXIT && proc->depth > 0)
    {
        depth = proc->depth;
    }
    else
    {
        depth = (event->kind == CALL_EVENT_ENTER && proc->depth > 0) ? proc->depth - 1U : 0U;
    }

    p101_printf(env, err, "#%zu pid %ld ", event->sequence, event->pid);

    for(size_t i = 0; i < depth; i++)
    {
        p101_fputs(env, err, "  ", stdout);
    }

    if(event->kind == CALL_EVENT_ENTER)
    {
        p101_printf(env, err, "%s(", event->call_name);

        if(p101_strcmp(env, event->arguments, "-") != 0)
        {
            p101_fputs(env, err, event->arguments, stdout);
        }

        p101_printf(env, err, ")  [%s:%d]\n", event->file_name, event->line_number);
    }
    else
    {
        p101_printf(env, err, "-> %s", event->call_name);

        if(p101_strcmp(env, event->result, "-") != 0)
        {
            p101_printf(env, err, " = %s", event->result);
        }

        p101_printf(env, err, "  [%s:%d]\n", event->file_name, event->line_number);
    }
}

void p101_trace_print_flat_event(const struct p101_env *env, struct p101_error *err, const struct call_event *event)
{
    p101_printf(env,
                err,
                "%zu\t%ld\t%s\t%s\t%s\t%s\t%s\t%d\t%s\n",
                event->sequence,
                event->pid,
                (event->kind == CALL_EVENT_ENTER) ? "ENTER" : "EXIT",
                event->function_name,
                event->call_name,
                event->arguments,
                event->result,
                event->line_number,
                event->file_name);
}

void p101_trace_report_summary(const struct p101_env *env, struct p101_error *err, const struct model *model)
{
    struct site_rank *ranks;

    ranks = NULL;
    p101_printf(env, err, "event_schema=p101-event-format-v1 event_id_policy=derived-1-based-input-sequence\n");
    p101_printf(env, err, "records=%zu processes=%zu skipped=%zu malformed=%zu bad_version=%zu\n", model->records, model->proc_count, model->skipped, model->malformed, model->bad_version);

    for(size_t i = 0; i < model->proc_count; i++)
    {
        p101_printf(env, err, "pid %ld max_depth=%zu open_at_end=%zu unmatched_exits=%zu\n", model->procs[i].pid, model->procs[i].max_depth, model->procs[i].depth, model->procs[i].unmatched_exits);
    }

    if(model->site_count == 0)
    {
        goto done;
    }

    ranks = (struct site_rank *)p101_calloc(env, err, model->site_count, sizeof(struct site_rank));

    if(ranks == NULL)
    {
        goto done;
    }

    for(size_t i = 0; i < model->site_count; i++)
    {
        ranks[i].index = i;
        ranks[i].total = model->sites[i].enters + model->sites[i].exits;
    }

    p101_qsort(env, ranks, model->site_count, sizeof(struct site_rank), compare_ranks);
    p101_fputs(env, err, "enter  exit  result  suspect  total  where\n", stdout);

    for(size_t i = 0; i < model->site_count; i++)
    {
        const struct call_site *site;

        site = &model->sites[ranks[i].index];
        p101_printf(env,
                    err,
                    "%5zu  %4zu  %6zu  %7zu  %5zu  %s at %s:%d in %s()\n",
                    site->enters,
                    site->exits,
                    site->exits_with_result,
                    site->likely_failures,
                    site->enters + site->exits,
                    site->call_name,
                    site->file_name,
                    site->line_number,
                    site->function_name);
    }

done:
    p101_free(env, ranks);
}

static int compare_ranks(const void *left, const void *right)
{
    const struct site_rank *a;
    const struct site_rank *b;
    int                     result;

    a      = (const struct site_rank *)left;
    b      = (const struct site_rank *)right;
    result = 0;

    if(a->total != b->total)
    {
        result = (a->total < b->total) ? 1 : -1;
    }
    else if(a->index != b->index)
    {
        result = (a->index < b->index) ? -1 : 1;
    }

    return result;
}

void p101_trace_report_health(const struct p101_env *env, struct p101_error *err, const struct model *model)
{
    if(model->malformed == 0 && model->bad_version == 0)
    {
        goto done;
    }

    p101_fprintf(env, err, stderr, "p101-trace: %zu malformed record%s, %zu unsupported-version record%s.\n", model->malformed, model->malformed == 1 ? "" : "s", model->bad_version, model->bad_version == 1 ? "" : "s");

done:
    return;
}
