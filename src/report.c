#include "report.h"
#include "model_lifecycle.h"
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

    p101_printf(env, err, "#%zu pid %ld context %zu ", event->sequence, event->pid, event->context_id);

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
                "%zu\t%ld\t%zu\t%zu\t%s\t%s\t%s\t%s\t%s\t%d\t%s\n",
                event->sequence,
                event->pid,
                event->context_id,
                event->event_sequence,
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
    p101_printf(env, err, "event_schema=" P101_TOOL_EVENT_SCHEMA_NAME " event_id_policy=wire-sequence-with-derived-input-order\n");
    p101_printf(env, err, "records=%zu execution_contexts=%zu skipped=%zu malformed=%zu bad_version=%zu\n", model->records, model->proc_count, model->skipped, model->malformed, model->bad_version);

    for(size_t i = 0; i < model->proc_count; i++)
    {
        p101_printf(env,
                    err,
                    "pid %ld context %zu max_depth=%zu open_at_end=%zu abandoned_at_completion=%zu unmatched_exits=%zu mismatched_exits=%zu\n",
                    model->procs[i].pid,
                    model->procs[i].context_id,
                    model->procs[i].max_depth,
                    model->procs[i].depth,
                    model->procs[i].abandoned_at_completion,
                    model->procs[i].unmatched_exits,
                    model->procs[i].mismatched_exits);
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
    p101_fputs(env, err, "enter  exit  result  suspect  timed      total-ns        max-ns  where\n", stdout);

    for(size_t i = 0; i < model->site_count; i++)
    {
        const struct call_site *site;

        site = &model->sites[ranks[i].index];
        p101_printf(env,
                    err,
                    "%5zu  %4zu  %6zu  %7zu  %5zu  %12zu  %12zu  %s at %s:%d in %s()\n",
                    site->enters,
                    site->exits,
                    site->exits_with_result,
                    site->likely_failures,
                    site->timed_calls,
                    site->total_duration_ns,
                    site->max_duration_ns,
                    site->call_name,
                    site->file_name,
                    site->line_number,
                    site->function_name);
    }

done:
    p101_free(env, ranks);
}

void p101_trace_report_slow_calls(const struct p101_env *env, struct p101_error *err, const struct model *model, size_t threshold_ns)
{
    if(threshold_ns == 0U)
    {
        return;
    }

    p101_printf(env, err, "slow_calls_threshold_ns=%zu\n", threshold_ns);
    for(size_t i = 0U; i < model->site_count; i++)
    {
        const struct call_site *site;

        site = &model->sites[i];
        if(site->timed_calls > 0U && site->max_duration_ns >= threshold_ns)
        {
            p101_printf(env, err, "%s max=%zu ns total=%zu ns samples=%zu at %s:%d in %s()\n", site->call_name, site->max_duration_ns, site->total_duration_ns, site->timed_calls, site->file_name, site->line_number, site->function_name);
        }
    }
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

#ifdef P101_TRACE_TESTING
int p101_trace_test_compare_ranks(const struct site_rank *left, const struct site_rank *right)
{
    return compare_ranks(left, right);
}
#endif

void p101_trace_report_health(const struct p101_env *env, struct p101_error *err, const struct model *model)
{
    if(model->malformed == 0 && model->bad_version == 0 && p101_tool_event_stream_health_is_complete(&model->stream_health))
    {
        goto done;
    }

    p101_fprintf(env, err, stderr, "p101-trace: %zu malformed record%s, %zu unsupported-version record%s.\n", model->malformed, model->malformed == 1 ? "" : "s", model->bad_version, model->bad_version == 1 ? "" : "s");
    if(model->stream_health.completion_records == 0U)
    {
        p101_fputs(env, err, "p101-trace: no producer completion record; the trace is incomplete evidence.\n", stderr);
    }
    else if(p101_tool_event_stream_health_incomplete_producers(&model->stream_health) > 0U)
    {
        p101_fprintf(env,
                     err,
                     stderr,
                     "p101-trace: %zu of %zu producer context%s did not emit exactly one clean completion record.\n",
                     p101_tool_event_stream_health_incomplete_producers(&model->stream_health),
                     model->stream_health.producer_count,
                     model->stream_health.producer_count == 1U ? "" : "s");
    }
    if(model->stream_health.producer_write_failures > 0U)
    {
        p101_fprintf(env,
                     err,
                     stderr,
                     "p101-trace: producer reported %zu event-write failure%s (last errno %d).\n",
                     model->stream_health.producer_write_failures,
                     model->stream_health.producer_write_failures == 1U ? "" : "s",
                     model->stream_health.last_write_errno);
    }
    if(model->stream_health.duplicate_sequences > 0U || model->stream_health.nonmonotonic_sequences > 0U || model->stream_health.attempted_count_mismatches > 0U || model->stream_health.records_after_completion > 0U)
    {
        p101_fprintf(env,
                     err,
                     stderr,
                     "p101-trace: stream integrity failed (%zu duplicate sequence%s, %zu non-monotonic sequence%s, %zu count mismatch%s, %zu record%s after completion).\n",
                     model->stream_health.duplicate_sequences,
                     model->stream_health.duplicate_sequences == 1U ? "" : "s",
                     model->stream_health.nonmonotonic_sequences,
                     model->stream_health.nonmonotonic_sequences == 1U ? "" : "s",
                     model->stream_health.attempted_count_mismatches,
                     model->stream_health.attempted_count_mismatches == 1U ? "" : "es",
                     model->stream_health.records_after_completion,
                     model->stream_health.records_after_completion == 1U ? "" : "s");
    }
    if(p101_trace_model_has_stack_errors(model))
    {
        p101_fputs(env, err, "p101-trace: call stack integrity failed (open, unmatched, or mismatched call records).\n", stderr);
    }

done:
    return;
}
