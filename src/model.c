#include "model.h"
#include "constants.h"
#include "model_identity.h"
#include <errno.h>
#include <p101_c/p101_stdlib.h>
#include <p101_c/p101_string.h>
#include <p101_posix/p101_string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

static bool result_looks_like_failure(const struct p101_env *env, const char *result);
static bool reserve_active_call(const struct p101_env *env, struct p101_error *err, struct proc_state *proc);
static bool event_matches_site(const struct p101_env *env, const struct call_event *event, const struct call_site *site);
static void count_exit_result(const struct p101_env *env, struct call_site *site, const struct call_event *event);

struct model *p101_trace_model_create(const struct p101_env *env, struct p101_error *err)
{
    P101_TRACE_SCOPE(env);
    return (struct model *)p101_calloc(env, err, 1, sizeof(struct model));
}

void p101_trace_model_destroy(const struct p101_env *env, struct model **model)
{
    struct model *victim;

    P101_TRACE_SCOPE(env);

    if(model == NULL || *model == NULL)
    {
        goto done;
    }

    victim = *model;

    for(size_t i = 0; i < victim->site_count; i++)
    {
        p101_free(env, victim->sites[i].call_name);
        p101_free(env, victim->sites[i].file_name);
        p101_free(env, victim->sites[i].function_name);
    }

    p101_free(env, victim->sites);
    for(size_t i = 0U; i < victim->proc_count; i++)
    {
        p101_free(env, victim->procs[i].active_calls);
    }
    p101_free(env, victim->procs);
    p101_tool_event_stream_health_destroy(&victim->stream_health);
    p101_free(env, victim);
    *model = NULL;

done:
    return;
}

void p101_trace_model_count_line(struct model *model, enum line_status status)
{
    if(model == NULL)
    {
        goto done;
    }

#ifdef __clang__
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wcovered-switch-default"
#endif
    switch(status)
    {
        case LINE_OTHER:
        {
            model->skipped++;
            break;
        }
        case LINE_OK:
        case LINE_COMPLETE:
        {
            break;
        }
        case LINE_MALFORMED:
        {
            model->malformed++;
            break;
        }
        case LINE_BAD_VERSION:
        {
            model->bad_version++;
            break;
        }
        default:
        {
            model->malformed++;
            break;
        }
    }
#ifdef __clang__
    #pragma clang diagnostic pop
#endif

done:
    return;
}

struct proc_state *p101_trace_model_ingest(const struct p101_env *env, struct p101_error *err, struct model *model, const struct call_event *event)
{
    struct proc_state *proc;

    proc = NULL;
    if(model == NULL || event == NULL)
    {
        goto done;
    }

    proc = p101_trace_find_proc(env, err, model, event->pid, event->context_id);

    if(proc == NULL)
    {
        goto done;
    }

    model->records++;

    if(event->kind == CALL_EVENT_ENTER)
    {
        size_t            site_index;
        struct call_site *site;

        site_index = p101_trace_intern_site(env, err, model, event);
        if(p101_error_has_error(err))
        {
            goto done;
        }
        site = &model->sites[site_index];
        if(!reserve_active_call(env, err, proc))
        {
            goto done;
        }
        proc->active_calls[proc->depth].site                   = site_index;
        proc->active_calls[proc->depth].monotonic_ns           = event->monotonic_ns;
        proc->active_calls[proc->depth].monotonic_ns_available = event->monotonic_ns_available;
        site->enters++;
        proc->depth++;

        if(proc->depth > proc->max_depth)
        {
            proc->max_depth = proc->depth;
        }
    }
    else
    {
        if(proc->depth == 0)
        {
            size_t site_index;

            site_index = p101_trace_intern_site(env, err, model, event);
            if(p101_error_has_error(err))
            {
                goto done;
            }
            count_exit_result(env, &model->sites[site_index], event);
            proc->unmatched_exits++;
        }
        else
        {
            const struct active_call *active;
            struct call_site         *site;

            active = &proc->active_calls[proc->depth - 1U];
            site   = &model->sites[active->site];
            if(!event_matches_site(env, event, site))
            {
                size_t exit_site;

                exit_site = p101_trace_intern_site(env, err, model, event);
                if(p101_error_has_no_error(err))
                {
                    count_exit_result(env, &model->sites[exit_site], event);
                }
                proc->mismatched_exits++;
                goto done;
            }
            count_exit_result(env, site, event);
            if(active->monotonic_ns_available != 0 && event->monotonic_ns_available != 0 && event->monotonic_ns >= active->monotonic_ns)
            {
                size_t duration;

                duration = event->monotonic_ns - active->monotonic_ns;
                model->sites[active->site].timed_calls++;
                if(model->sites[active->site].total_duration_ns <= SIZE_MAX - duration)
                {
                    model->sites[active->site].total_duration_ns += duration;
                }
                else
                {
                    model->sites[active->site].total_duration_ns = SIZE_MAX;
                }
                if(duration > model->sites[active->site].max_duration_ns)
                {
                    model->sites[active->site].max_duration_ns = duration;
                }
            }
            proc->depth--;
        }
    }

done:
    return proc;
}

static bool event_matches_site(const struct p101_env *env, const struct call_event *event, const struct call_site *site)
{
    if(p101_strcmp(env, event->call_name, site->call_name) != 0 || p101_strcmp(env, event->function_name, site->function_name) != 0 || p101_strcmp(env, event->file_name, site->file_name) != 0)
    {
        return false;
    }
    return true;
}

static void count_exit_result(const struct p101_env *env, struct call_site *site, const struct call_event *event)
{
    site->exits++;
    if(event->result != NULL && p101_strcmp(env, event->result, "-") != 0)
    {
        site->exits_with_result++;
        if(result_looks_like_failure(env, event->result))
        {
            site->likely_failures++;
        }
    }
}

static bool reserve_active_call(const struct p101_env *env, struct p101_error *err, struct proc_state *proc)
{
    struct active_call *grown;
    size_t              capacity;

    if(proc->depth < proc->active_capacity)
    {
        return true;
    }
    if(proc->active_capacity > SIZE_MAX / 2U)
    {
        P101_ERROR_RAISE_ERRNO(err, ENOMEM);
        return false;
    }
    capacity = proc->active_capacity == 0U ? ACTIVE_FIRST_CAPACITY : proc->active_capacity * 2U;
    if(capacity > SIZE_MAX / sizeof(*grown))
    {
        P101_ERROR_RAISE_ERRNO(err, ENOMEM);
        return false;
    }
    grown = (struct active_call *)p101_realloc(env, err, proc->active_calls, capacity * sizeof(*grown));
    if(grown == NULL)
    {
        return false;
    }
    proc->active_calls    = grown;
    proc->active_capacity = capacity;
    return true;
}

static bool result_looks_like_failure(const struct p101_env *env, const char *result)
{
    bool ret_val;

    ret_val = false;

    if(p101_strcmp(env, result, "NULL") == 0 || p101_strcmp(env, result, "null") == 0 || p101_strcmp(env, result, "false") == 0 || p101_strcmp(env, result, "EOF") == 0 || result[0] == '-')
    {
        ret_val = true;
    }
    return ret_val;
}
