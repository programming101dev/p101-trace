#include "model_lifecycle.h"
#include "model_identity.h"
#include <p101_c/p101_stdlib.h>
#include <p101_c/p101_string.h>

void p101_trace_model_fork(const struct p101_env *env, struct p101_error *err, struct model *model, const struct call_event *event)
{
    struct proc_state       *child;
    const struct proc_state *parent;
    size_t                   parent_index;

    if(model == NULL || event == NULL || event->pid < 0 || event->child_pid < 0 || event->pid == event->child_pid)
    {
        P101_ERROR_RAISE_CHECK(err);
        return;
    }
    parent = p101_trace_find_proc(env, err, model, event->pid, event->context_id);
    if(parent == NULL || p101_error_has_error(err))
    {
        model->records++;
        return;
    }
    parent_index = (size_t)(parent - model->procs);
    child        = p101_trace_find_proc(env, err, model, event->child_pid, event->context_id);
    parent       = &model->procs[parent_index];
    model->records++;
    if(child == NULL || p101_error_has_error(err) || child->depth != 0U)
    {
        return;
    }
    if(parent->depth > 0U)
    {
        struct active_call *grown;

        grown = (struct active_call *)p101_realloc(env, err, child->active_calls, parent->depth * sizeof(*child->active_calls));
        if(grown == NULL)
        {
            return;
        }
        child->active_calls = grown;
        p101_memcpy(env, child->active_calls, parent->active_calls, parent->depth * sizeof(*child->active_calls));
        child->active_capacity = parent->depth;
        child->depth           = parent->depth;
        if(p101_strcmp(env, model->sites[child->active_calls[child->depth - 1U].site].call_name, "p101_fork") == 0)
        {
            child->depth--;
        }
        child->max_depth = parent->depth;
    }
}

void p101_trace_model_complete(const struct p101_env *env, struct p101_error *err, struct model *model, long pid, size_t context_id)
{
    struct proc_state *proc;

    if(model == NULL)
    {
        P101_ERROR_RAISE_CHECK(err);
        return;
    }
    proc = p101_trace_find_proc(env, err, model, pid, context_id);
    if(proc != NULL)
    {
        proc->abandoned_at_completion += proc->depth;
        proc->depth = 0U;
    }
}

bool p101_trace_model_has_stack_errors(const struct model *model)
{
    if(model == NULL)
    {
        return true;
    }
    for(size_t index = 0U; index < model->proc_count; index++)
    {
        if(model->procs[index].depth != 0U || model->procs[index].unmatched_exits != 0U || model->procs[index].mismatched_exits != 0U)
        {
            return true;
        }
    }
    return false;
}
