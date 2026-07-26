#include "arguments.h"
#include "errors.h"
#include <limits.h>
#include <p101_c/p101_ctype.h>
#include <p101_c/p101_stdio.h>
#include <p101_c/p101_stdlib.h>
#include <p101_c/p101_string.h>
#include <p101_posix/p101_string.h>
#include <p101_posix/p101_unistd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

struct call_site
{
    char  *call_name;
    char  *file_name;
    char  *function_name;
    int    line_number;
    size_t enters;
    size_t exits;
};

struct proc_state
{
    long   pid;
    size_t depth;
    size_t max_depth;
    size_t unmatched_exits;
};

struct model
{
    struct call_site  *sites;
    size_t             site_count;
    size_t             site_capacity;
    struct proc_state *procs;
    size_t             proc_count;
    size_t             proc_capacity;
    size_t             records;
    size_t             skipped;
    size_t             malformed;
    size_t             bad_version;
};

struct site_rank
{
    size_t index;
    size_t total;
};

static void               parse_arguments(const struct p101_env *env, struct p101_error *err, int argc, char *argv[], struct arguments *args);
static void               check_arguments(const struct p101_env *env, struct p101_error *err, const struct arguments *args);
static int                run_trace(const struct p101_env *env, struct p101_error *err, const struct arguments *args);
static FILE              *open_log(const struct p101_env *env, struct p101_error *err, const char *path, int *owned);
static enum line_status   parse_call_line(const struct p101_env *env, char *line, struct call_event *event);
static bool               call_line_is_ours(const struct p101_env *env, const char *line);
static char              *split_tab(char **cursor);
static bool               parse_long_field(const char *text, long min, long max, long *out);
static struct model      *model_create(const struct p101_env *env, struct p101_error *err);
static void               model_destroy(const struct p101_env *env, struct model **model);
static void               model_count_line(struct model *model, enum line_status status);
static void               model_ingest(const struct p101_env *env, struct p101_error *err, struct model *model, const struct call_event *event);
static size_t             intern_site(const struct p101_env *env, struct p101_error *err, struct model *model, const struct call_event *event);
static struct proc_state *find_proc(const struct p101_env *env, struct p101_error *err, struct model *model, long pid);
static void               print_tree_event(const struct p101_env *env, struct p101_error *err, const struct call_event *event, const struct proc_state *proc);
static void               print_flat_event(const struct p101_env *env, struct p101_error *err, const struct call_event *event);
static void               report_summary(const struct p101_env *env, struct p101_error *err, const struct model *model);
static int                compare_ranks(const void *left, const void *right);
static void               report_health(const struct p101_env *env, struct p101_error *err, const struct model *model);
_Noreturn static void     usage(const struct p101_env *env, struct p101_error *err, const char *program_name, int exit_code, const char *message);

static const char CALL_PREFIX[] = "P101CALL\t";

enum
{
    CALL_LOG_VERSION    = 1,
    LINE_MAX_BYTES      = 4096,
    MSG_LEN             = 256,
    DECIMAL_BASE        = 10,
    SITE_FIRST_CAPACITY = 32,
    PROC_FIRST_CAPACITY = 4,
    EXIT_CLEAN          = 0,
    EXIT_FINDINGS       = 1,
    EXIT_TROUBLE        = 2
};

int main(int argc, char *argv[])
{
    struct p101_error *err;
    struct p101_env   *env;
    struct arguments   args;
    int                ret_val;

    ret_val = EXIT_TROUBLE;
    err     = p101_error_create(false);
    env     = p101_env_create(err, NULL);
    p101_memset(env, &args, 0, sizeof(args));
    args.mode = TRACE_MODE_TREE;

    parse_arguments(env, err, argc, argv, &args);

    if(p101_error_has_error(err))
    {
        goto done;
    }

    if(args.verbose)
    {
        p101_env_set_tracer(env, p101_env_default_tracer);
    }

    check_arguments(env, err, &args);

    if(p101_error_has_error(err))
    {
        goto done;
    }

    ret_val = run_trace(env, err, &args);

done:
    if(p101_error_has_error(err))
    {
        if(p101_error_is_error(err, P101_ERROR_USER, ERR_USAGE))
        {
            const char *msg;

            msg = p101_error_get_message(err);
            usage(env, err, argv[0], EXIT_TROUBLE, msg);
        }

        p101_fprintf(env, err, stderr, "%s\n", p101_error_get_message(err));
        ret_val = EXIT_TROUBLE;
    }

    p101_env_destroy(env);
    p101_error_destroy(err);

    return ret_val;
}

static void parse_arguments(const struct p101_env *env, struct p101_error *err, int argc, char *argv[], struct arguments *args)
{
    int opt;

    P101_TRACE(env);
    opterr = 0;

    while((opt = p101_getopt(env, argc, argv, ":hvfs")) != -1 && p101_error_has_no_error(err))
    {
        switch(opt)
        {
            case 'h':
            {
                usage(env, err, argv[0], EXIT_CLEAN, NULL);
            }
            case 'v':
            {
                args->verbose = true;
                break;
            }
            case 'f':
            {
                if(args->mode != TRACE_MODE_TREE)
                {
                    P101_ERROR_RAISE_USER(err, "Choose only one report mode.", ERR_USAGE);
                    break;
                }

                args->mode = TRACE_MODE_FLAT;
                break;
            }
            case 's':
            {
                if(args->mode != TRACE_MODE_TREE)
                {
                    P101_ERROR_RAISE_USER(err, "Choose only one report mode.", ERR_USAGE);
                    break;
                }

                args->mode = TRACE_MODE_SUMMARY;
                break;
            }
            case ':':
            {
                char msg[MSG_LEN];

                p101_snprintf(env, err, msg, sizeof(msg), "Option '-%c' requires an argument.", optopt ? optopt : '?');
                P101_ERROR_RAISE_USER(err, msg, ERR_USAGE);
                break;
            }
            case '?':
            {
                char msg[MSG_LEN];

                if(p101_isprint(env, optopt))
                {
                    p101_snprintf(env, err, msg, sizeof(msg), "Unknown option '-%c'.", optopt);
                }
                else
                {
                    p101_snprintf(env, err, msg, sizeof(msg), "Unknown option character 0x%02X.", (unsigned)(unsigned char)optopt);
                }

                P101_ERROR_RAISE_USER(err, msg, ERR_USAGE);
                break;
            }
            default:
            {
                char msg[MSG_LEN];

                p101_snprintf(env, err, msg, sizeof(msg), "Internal error: unhandled option '-%c' returned by getopt.", p101_isprint(env, opt) ? opt : '?');
                P101_ERROR_RAISE_USER(err, msg, ERR_USAGE);
                break;
            }
        }
    }

    if(p101_error_has_no_error(err))
    {
        if(optind < argc)
        {
            args->log_name = argv[optind];
            optind++;
        }

        if(optind < argc)
        {
            P101_ERROR_RAISE_USER(err, "Unexpected extra argument.", ERR_USAGE);
        }
    }
}

static void check_arguments(const struct p101_env *env, struct p101_error *err, const struct arguments *args)
{
    P101_TRACE(env);

    if(args->log_name != NULL && args->log_name[0] == '\0')
    {
        P101_ERROR_RAISE_USER(err, "The call log path must not be empty.", ERR_USAGE);
    }
}

static int run_trace(const struct p101_env *env, struct p101_error *err, const struct arguments *args)
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
    stream  = open_log(env, err, args->log_name, &owned);

    if(stream == NULL || p101_error_has_error(err))
    {
        goto done;
    }

    model = model_create(env, err);

    if(model == NULL || p101_error_has_error(err))
    {
        goto done;
    }

    while(p101_fgets(env, err, line, (int)sizeof(line), stream) != NULL)
    {
        struct call_event event;
        enum line_status  status;
        size_t            length;

        if(p101_error_has_error(err))
        {
            goto done;
        }

        length = p101_strlen(env, line);

        if(length == sizeof(line) - 1U && line[length - 1U] != '\n')
        {
            enum line_status long_line_status;
            int              c;

            do
            {
                c = p101_fgetc(env, err, stream);
            } while(c != '\n' && c != EOF);

            long_line_status = LINE_OTHER;

            if(call_line_is_ours(env, line))
            {
                long_line_status = LINE_MALFORMED;
            }

            model_count_line(model, long_line_status);
            continue;
        }

        status = parse_call_line(env, line, &event);
        model_count_line(model, status);

        if(status == LINE_OK)
        {
            model_ingest(env, err, model, &event);

            if(args->mode == TRACE_MODE_TREE)
            {
                const struct proc_state *proc;

                proc = find_proc(env, err, model, event.pid);

                if(proc != NULL)
                {
                    print_tree_event(env, err, &event, proc);
                }
            }
            else if(args->mode == TRACE_MODE_FLAT)
            {
                print_flat_event(env, err, &event);
            }
        }
    }

    if(p101_error_has_error(err))
    {
        goto done;
    }

    if(args->mode == TRACE_MODE_SUMMARY)
    {
        report_summary(env, err, model);
    }

    report_health(env, err, model);
    ret_val = (model->malformed == 0 && model->bad_version == 0) ? EXIT_CLEAN : EXIT_FINDINGS;

done:
    if(owned && stream != NULL)
    {
        p101_fclose(env, err, stream);
    }

    model_destroy(env, &model);

    return ret_val;
}

static FILE *open_log(const struct p101_env *env, struct p101_error *err, const char *path, int *owned)
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

static enum line_status parse_call_line(const struct p101_env *env, char *line, struct call_event *event)
{
    char            *cursor;
    const char      *version_text;
    const char      *pid_text;
    const char      *kind_text;
    const char      *line_text;
    long             version;
    long             pid;
    long             line_number;
    enum line_status status;

    status = LINE_MALFORMED;

    if(line == NULL || event == NULL)
    {
        goto done;
    }

    if(p101_strncmp(env, line, CALL_PREFIX, sizeof(CALL_PREFIX) - 1U) != 0)
    {
        status = LINE_OTHER;
        goto done;
    }

    {
        size_t length;

        length = p101_strlen(env, line);

        while(length > 0U && (line[length - 1U] == '\n' || line[length - 1U] == '\r'))
        {
            line[length - 1U] = '\0';
            length--;
        }
    }

    cursor               = line + (sizeof(CALL_PREFIX) - 1U);
    version_text         = split_tab(&cursor);
    pid_text             = split_tab(&cursor);
    kind_text            = split_tab(&cursor);
    line_text            = split_tab(&cursor);
    event->function_name = split_tab(&cursor);
    event->call_name     = split_tab(&cursor);
    event->arguments     = split_tab(&cursor);
    event->result        = split_tab(&cursor);
    event->file_name     = cursor;

    if(event->function_name == NULL || event->call_name == NULL || event->arguments == NULL || event->result == NULL || event->file_name == NULL)
    {
        goto done;
    }

    if(!parse_long_field(version_text, 0, LONG_MAX, &version))
    {
        goto done;
    }

    if(version != CALL_LOG_VERSION)
    {
        status = LINE_BAD_VERSION;
        goto done;
    }

    if(!parse_long_field(pid_text, 0, LONG_MAX, &pid))
    {
        goto done;
    }

    if(!parse_long_field(line_text, 0, INT_MAX, &line_number))
    {
        goto done;
    }

    if(kind_text != NULL && p101_strcmp(env, kind_text, "ENTER") == 0)
    {
        event->kind = CALL_EVENT_ENTER;
    }
    else if(kind_text != NULL && p101_strcmp(env, kind_text, "EXIT") == 0)
    {
        event->kind = CALL_EVENT_EXIT;
    }
    else
    {
        goto done;
    }

    event->pid         = pid;
    event->line_number = (int)line_number;
    status             = LINE_OK;

done:
    return status;
}

static bool call_line_is_ours(const struct p101_env *env, const char *line)
{
    return (p101_strncmp(env, line, CALL_PREFIX, sizeof(CALL_PREFIX) - 1U) == 0) != 0;
}

static char *split_tab(char **cursor)
{
    char *start;
    char *tab;

    start = *cursor;

    if(start == NULL)
    {
        goto done;
    }

    tab = start;

    while(*tab != '\0' && *tab != '\t')
    {
        tab++;
    }

    if(*tab == '\0')
    {
        *cursor = NULL;
    }
    else
    {
        *tab    = '\0';
        *cursor = tab + 1;
    }

done:
    return start;
}

static bool parse_long_field(const char *text, long min, long max, long *out)
{
    const char *cursor;
    long        value;
    bool        result;

    cursor = text;
    value  = 0;
    result = false;

    if(cursor == NULL || *cursor == '\0')
    {
        goto done;
    }

    while(*cursor != '\0')
    {
        int digit;

        if(*cursor < '0' || *cursor > '9')
        {
            goto done;
        }

        digit = *cursor - '0';

        if(value > (LONG_MAX - (long)digit) / DECIMAL_BASE)
        {
            goto done;
        }

        value = (value * DECIMAL_BASE) + digit;
        cursor++;
    }

    if(value < min || value > max)
    {
        goto done;
    }

    *out   = value;
    result = true;

done:
    return result;
}

static struct model *model_create(const struct p101_env *env, struct p101_error *err)
{
    P101_TRACE(env);
    return (struct model *)p101_calloc(env, err, 1, sizeof(struct model));
}

static void model_destroy(const struct p101_env *env, struct model **model)
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

static void model_count_line(struct model *model, enum line_status status)
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

static void model_ingest(const struct p101_env *env, struct p101_error *err, struct model *model, const struct call_event *event)
{
    size_t             site_index;
    struct call_site  *site;
    struct proc_state *proc;

    if(model == NULL || event == NULL)
    {
        goto done;
    }

    site_index = intern_site(env, err, model, event);

    if(p101_error_has_error(err))
    {
        goto done;
    }

    proc = find_proc(env, err, model, event->pid);

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

static size_t intern_site(const struct p101_env *env, struct p101_error *err, struct model *model, const struct call_event *event)
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

static struct proc_state *find_proc(const struct p101_env *env, struct p101_error *err, struct model *model, long pid)
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

static void print_tree_event(const struct p101_env *env, struct p101_error *err, const struct call_event *event, const struct proc_state *proc)
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

    p101_printf(env, err, "pid %ld ", event->pid);

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

static void print_flat_event(const struct p101_env *env, struct p101_error *err, const struct call_event *event)
{
    p101_printf(env, err, "%ld\t%s\t%s\t%s\t%s\t%s\t%d\t%s\n", event->pid, (event->kind == CALL_EVENT_ENTER) ? "ENTER" : "EXIT", event->function_name, event->call_name, event->arguments, event->result, event->line_number, event->file_name);
}

static void report_summary(const struct p101_env *env, struct p101_error *err, const struct model *model)
{
    struct site_rank *ranks;

    ranks = NULL;
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
    p101_fputs(env, err, "enter  exit  total  where\n", stdout);

    for(size_t i = 0; i < model->site_count; i++)
    {
        const struct call_site *site;

        site = &model->sites[ranks[i].index];
        p101_printf(env, err, "%5zu  %4zu  %5zu  %s at %s:%d in %s()\n", site->enters, site->exits, site->enters + site->exits, site->call_name, site->file_name, site->line_number, site->function_name);
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

static void report_health(const struct p101_env *env, struct p101_error *err, const struct model *model)
{
    if(model->malformed == 0 && model->bad_version == 0)
    {
        goto done;
    }

    p101_fprintf(env, err, stderr, "p101-trace: %zu malformed record%s, %zu unsupported-version record%s.\n", model->malformed, model->malformed == 1 ? "" : "s", model->bad_version, model->bad_version == 1 ? "" : "s");

done:
    return;
}

_Noreturn static void usage(const struct p101_env *env, struct p101_error *err, const char *program_name, int exit_code, const char *message)
{
    P101_TRACE(env);

#ifndef P101_SUPPRESS_USAGE_TEXT
    if(message != NULL)
    {
        p101_fprintf(env, err, stderr, "%s\n\n", message);
    }

    p101_fprintf(env, err, stderr, "Usage: %s [-h] [-v] [-s|-f] [<call-log>]\n", program_name);
    p101_fputs(env, err, "\n", stderr);
    p101_fputs(env, err, "Reads P101CALL records from a p101 call log and prints a portable\n", stderr);
    p101_fputs(env, err, "strace/ltrace-style view. With no file, or with \"-\", reads stdin.\n", stderr);
    p101_fputs(env, err, "\n", stderr);
    p101_fputs(env, err, "Options:\n", stderr);
    p101_fputs(env, err, "  -h        Display this help message and exit\n", stderr);
    p101_fputs(env, err, "  -v        Enable verbose p101 tracing in p101-trace itself\n", stderr);
    p101_fputs(env, err, "  -s        Summary counts instead of the call tree\n", stderr);
    p101_fputs(env, err, "  -f        Flat normalized records instead of the call tree\n", stderr);
#else
    (void)message;
    (void)program_name;
#endif

    p101_exit(env, exit_code);
}
