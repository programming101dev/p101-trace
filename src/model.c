#include "model.h"
#include "constants.h"
#include <p101_c/p101_stdlib.h>
#include <p101_c/p101_string.h>
#include <p101_posix/p101_string.h>
#include <stdlib.h>

struct model *p101_trace_model_create(const struct p101_env *env, struct p101_error *err)
{
    P101_TRACE(env);
    return (struct model *)p101_calloc(env, err, 1, sizeof(struct model));
}

void p101_trace_model_destroy(const struct p101_env *env, struct model **model)
{
    struct model *victim;

    P101_TRACE(env);

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
    p101_free(env, victim->procs);
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

void p101_trace_model_ingest(const struct p101_env *env, struct p101_error *err, struct model *model, const struct call_event *event)
{
    size_t             site_index;
    struct call_site  *site;
    struct proc_state *proc;

    if(model == NULL || event == NULL)
    {
        goto done;
    }

    site_index = p101_trace_intern_site(env, err, model, event);

    if(p101_error_has_error(err))
    {
        goto done;
    }

    proc = p101_trace_find_proc(env, err, model, event->pid);

    if(proc == NULL)
    {
        goto done;
    }

    model->records++;
    site = &model->sites[site_index];

    if(event->kind == CALL_EVENT_ENTER)
    {
        site->enters++;
        proc->depth++;

        if(proc->depth > proc->max_depth)
        {
            proc->max_depth = proc->depth;
        }
    }
    else
    {
        site->exits++;

        if(proc->depth == 0)
        {
            proc->unmatched_exits++;
        }
        else
        {
            proc->depth--;
        }
    }

done:
    return;
}

size_t p101_trace_intern_site(const struct p101_env *env, struct p101_error *err, struct model *model, const struct call_event *event)
{
    size_t index;

    index = model->site_count;

    for(size_t i = 0; i < model->site_count; i++)
    {
        const struct call_site *site;

        site = &model->sites[i];

        if(site->line_number == event->line_number && p101_strcmp(env, site->call_name, event->call_name) == 0 && p101_strcmp(env, site->file_name, event->file_name) == 0 && p101_strcmp(env, site->function_name, event->function_name) == 0)
        {
            index = i;
            goto done;
        }
    }

    if(model->site_count == model->site_capacity)
    {
        size_t            capacity;
        struct call_site *grown;

        capacity = (model->site_capacity == 0) ? SITE_FIRST_CAPACITY : model->site_capacity * 2U;
        grown    = (struct call_site *)p101_realloc(env, err, model->sites, capacity * sizeof(struct call_site));

        if(grown == NULL)
        {
            goto done;
        }

        model->sites         = grown;
        model->site_capacity = capacity;
    }

    {
        char *call_name;
        char *file_name;
        char *function_name;

        call_name     = p101_strdup(env, err, event->call_name);
        file_name     = p101_strdup(env, err, event->file_name);
        function_name = p101_strdup(env, err, event->function_name);

        if(call_name == NULL || file_name == NULL || function_name == NULL)
        {
            p101_free(env, call_name);
            p101_free(env, file_name);
            p101_free(env, function_name);
            goto done;
        }

        model->sites[index].call_name     = call_name;
        model->sites[index].file_name     = file_name;
        model->sites[index].function_name = function_name;
        model->sites[index].line_number   = event->line_number;
        model->sites[index].enters        = 0;
        model->sites[index].exits         = 0;
    }

    model->site_count++;

done:
    return index;
}

struct proc_state *p101_trace_find_proc(const struct p101_env *env, struct p101_error *err, struct model *model, long pid)
{
    struct proc_state *proc;

    proc = NULL;

    for(size_t i = 0; i < model->proc_count; i++)
    {
        if(model->procs[i].pid == pid)
        {
            proc = &model->procs[i];
            goto done;
        }
    }

    if(model->proc_count == model->proc_capacity)
    {
        size_t             capacity;
        struct proc_state *grown;

        capacity = (model->proc_capacity == 0) ? PROC_FIRST_CAPACITY : model->proc_capacity * 2U;
        grown    = (struct proc_state *)p101_realloc(env, err, model->procs, capacity * sizeof(struct proc_state));

        if(grown == NULL)
        {
            goto done;
        }

        p101_memset(env, &grown[model->proc_capacity], 0, (capacity - model->proc_capacity) * sizeof(struct proc_state));
        model->procs         = grown;
        model->proc_capacity = capacity;
    }

    proc = &model->procs[model->proc_count];
    p101_memset(env, proc, 0, sizeof(*proc));
    proc->pid = pid;
    model->proc_count++;

done:
    return proc;
}
