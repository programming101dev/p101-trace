#include "model_identity.h"
#include "constants.h"
#include <errno.h>
#include <p101_c/p101_stdlib.h>
#include <p101_c/p101_string.h>
#include <p101_posix/p101_string.h>
#include <stdbool.h>
#include <stdint.h>

static bool reserve_sites(const struct p101_env *env, struct p101_error *err, struct model *model);
static bool reserve_procs(const struct p101_env *env, struct p101_error *err, struct model *model);

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

    if(!reserve_sites(env, err, model))
    {
        goto done;
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

        model->sites[index].call_name         = call_name;
        model->sites[index].file_name         = file_name;
        model->sites[index].function_name     = function_name;
        model->sites[index].line_number       = event->line_number;
        model->sites[index].enters            = 0;
        model->sites[index].exits             = 0;
        model->sites[index].exits_with_result = 0;
        model->sites[index].likely_failures   = 0;
        model->sites[index].timed_calls       = 0;
        model->sites[index].total_duration_ns = 0;
        model->sites[index].max_duration_ns   = 0;
    }

    model->site_count++;

done:
    return index;
}

struct proc_state *p101_trace_find_proc(const struct p101_env *env, struct p101_error *err, struct model *model, long pid, size_t context_id)
{
    struct proc_state *proc;

    proc = NULL;

    for(size_t i = 0; i < model->proc_count; i++)
    {
        if(model->procs[i].pid == pid && model->procs[i].context_id == context_id)
        {
            proc = &model->procs[i];
            goto done;
        }
    }

    if(!reserve_procs(env, err, model))
    {
        goto done;
    }

    proc = &model->procs[model->proc_count];
    p101_memset(env, proc, 0, sizeof(*proc));
    proc->pid        = pid;
    proc->context_id = context_id;
    model->proc_count++;

done:
    return proc;
}

static bool reserve_sites(const struct p101_env *env, struct p101_error *err, struct model *model)
{
    size_t            capacity;
    struct call_site *grown;

    if(model->site_count < model->site_capacity)
    {
        return true;
    }
    if(model->site_count > model->site_capacity)
    {
        P101_ERROR_RAISE_ERRNO(err, EINVAL);
        return false;
    }
    if(model->site_capacity > SIZE_MAX / 2U)
    {
        P101_ERROR_RAISE_ERRNO(err, ENOMEM);
        return false;
    }
    capacity = (model->site_capacity == 0U) ? SITE_FIRST_CAPACITY : model->site_capacity * 2U;
    if(capacity > SIZE_MAX / sizeof(*grown))
    {
        P101_ERROR_RAISE_ERRNO(err, ENOMEM);
        return false;
    }
    grown = (struct call_site *)p101_realloc(env, err, model->sites, capacity * sizeof(*grown));
    if(grown == NULL)
    {
        return false;
    }
    model->sites         = grown;
    model->site_capacity = capacity;
    return true;
}

static bool reserve_procs(const struct p101_env *env, struct p101_error *err, struct model *model)
{
    size_t             capacity;
    struct proc_state *grown;

    if(model->proc_count < model->proc_capacity)
    {
        return true;
    }
    if(model->proc_count > model->proc_capacity)
    {
        P101_ERROR_RAISE_ERRNO(err, EINVAL);
        return false;
    }
    if(model->proc_capacity > SIZE_MAX / 2U)
    {
        P101_ERROR_RAISE_ERRNO(err, ENOMEM);
        return false;
    }
    capacity = (model->proc_capacity == 0U) ? PROC_FIRST_CAPACITY : model->proc_capacity * 2U;
    if(capacity > SIZE_MAX / sizeof(*grown))
    {
        P101_ERROR_RAISE_ERRNO(err, ENOMEM);
        return false;
    }
    grown = (struct proc_state *)p101_realloc(env, err, model->procs, capacity * sizeof(*grown));
    if(grown == NULL)
    {
        return false;
    }
    p101_memset(env, &grown[model->proc_capacity], 0, (capacity - model->proc_capacity) * sizeof(*grown));
    model->procs         = grown;
    model->proc_capacity = capacity;
    return true;
}

#ifdef P101_TRACE_TESTING
bool p101_trace_test_reserve_sites(const struct p101_env *env, struct p101_error *err, struct model *model)
{
    return reserve_sites(env, err, model);
}

bool p101_trace_test_reserve_procs(const struct p101_env *env, struct p101_error *err, struct model *model)
{
    return reserve_procs(env, err, model);
}
#endif
