#include "cli.h"
#include "constants.h"
#include "errors.h"
#include "model.h"
#include "model_identity.h"
#include "model_lifecycle.h"
#include "parse.h"
#include "report.h"
#include "runner.h"
#include "test_hooks.h"
#include "unity.h"
#include <errno.h>
#include <limits.h>
#include <p101_c/p101_stdio.h>
#include <p101_c/p101_stdlib.h>
#include <p101_c/p101_string.h>
#include <p101_env/env.h>
#include <p101_error/error.h>
#include <p101_posix/p101_stdio.h>
#include <p101_posix/p101_stdlib.h>
#include <p101_posix/p101_unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static struct p101_error *error;
static struct p101_env   *env;

struct fault_state
{
    const char *call_name;
    size_t      fail_at;
    size_t      matches;
};

void setUp(void)
{
    error = p101_error_create(false);
    env   = p101_env_create(error, NULL);
}

void tearDown(void)
{
    p101_env_destroy(env);
    p101_error_destroy(error);
}

static void reset_getopt(void)
{
#ifdef __GLIBC__
    optind = 0;
#else
    extern int optreset;
    optreset = 1;
    optind   = 1;
#endif
}

static int inject_selected_failure(const struct p101_env *unused_env, const char *call_name, void *user_data)
{
    struct fault_state *state;

    (void)unused_env;
    state = (struct fault_state *)user_data;
    if(state->call_name == NULL || p101_strcmp(unused_env, state->call_name, call_name) == 0)
    {
        state->matches++;
        if(state->matches == state->fail_at)
        {
            return ENOMEM;
        }
    }
    return 0;
}

static struct call_event make_event(enum call_event_kind kind, long pid, size_t context_id, const char *call_name, const char *result)
{
    struct call_event event;

    p101_memset(env, &event, 0, sizeof(event));
    event.version                = P101_TOOL_EVENT_LOG_VERSION;
    event.pid                    = pid;
    event.context_id             = context_id;
    event.event_sequence         = 1U;
    event.monotonic_ns           = 100U;
    event.monotonic_ns_available = 1;
    event.kind                   = kind;
    event.line_number            = 17;
    event.function_name          = "caller";
    event.call_name              = call_name;
    event.arguments              = "-";
    event.result                 = result;
    event.file_name              = "trace-test.c";
    return event;
}

static void test_parse_accepts_summary_mode_and_log_path(void)
{
    char            *argv[] = {"p101-trace", "-s", "calls.log", NULL};
    struct arguments args;

    reset_getopt();
    p101_memset(env, &args, 0, sizeof(args));
    args.mode = TRACE_MODE_TREE;

    p101_trace_parse_arguments(env, error, 3, argv, &args);
    p101_trace_check_arguments(env, error, &args);

    TEST_ASSERT_FALSE(p101_error_has_error(error));
    TEST_ASSERT_EQUAL_INT(TRACE_MODE_SUMMARY, args.mode);
    TEST_ASSERT_EQUAL_STRING("calls.log", args.log_name);
}

static void test_parse_rejects_competing_modes(void)
{
    char            *argv[] = {"p101-trace", "-s", "-f", NULL};
    struct arguments args;

    reset_getopt();
    p101_memset(env, &args, 0, sizeof(args));
    args.mode = TRACE_MODE_TREE;

    p101_trace_parse_arguments(env, error, 3, argv, &args);

    TEST_ASSERT_TRUE(p101_error_is_error(error, P101_ERROR_USER, ERR_USAGE));
}

static void test_parse_call_line_accepts_enter_record(void)
{
    char              line[] = "P101CALL\t4\t42\t7\t1\t100\t200\tENTER\t17\tmain\tp101_open\tpath=/tmp/x\t-\tserver.c\n";
    struct call_event event;
    enum line_status  status;

    status = p101_trace_parse_call_line(env, line, &event);

    TEST_ASSERT_EQUAL_INT(LINE_OK, status);
    TEST_ASSERT_EQUAL_INT64(42, event.pid);
    TEST_ASSERT_EQUAL_UINT(7, event.context_id);
    TEST_ASSERT_EQUAL_INT(CALL_EVENT_ENTER, event.kind);
    TEST_ASSERT_EQUAL_INT(17, event.line_number);
    TEST_ASSERT_EQUAL_STRING("main", event.function_name);
    TEST_ASSERT_EQUAL_STRING("p101_open", event.call_name);
    TEST_ASSERT_EQUAL_STRING("path=/tmp/x", event.arguments);
    TEST_ASSERT_EQUAL_STRING("-", event.result);
    TEST_ASSERT_EQUAL_STRING("server.c", event.file_name);
}

static void test_parse_call_line_rejects_old_record(void)
{
    char              line[] = "P101CALL\t2\t42\t1\t100\t200\tEXIT\t17\tmain\tp101_open\t-\t3\tserver.c\n";
    struct call_event event;
    enum line_status  status;

    status = p101_trace_parse_call_line(env, line, &event);

    TEST_ASSERT_EQUAL_INT(LINE_BAD_VERSION, status);
}

static void test_parse_call_line_rejects_bad_version(void)
{
    char              line[] = "P101CALL\t5\t42\t1\t100\t200\tEXIT\t17\tmain\tp101_open\t-\t3\tserver.c\n";
    struct call_event event;
    enum line_status  status;

    status = p101_trace_parse_call_line(env, line, &event);

    TEST_ASSERT_EQUAL_INT(LINE_BAD_VERSION, status);
}

static void test_parse_call_line_skips_other_records(void)
{
    char              line[] = "P101FD\t4\t42\t1\t1\t100\t200\tOPEN\t3\t17\tmain\tserver.c\n";
    struct call_event event;
    enum line_status  status;

    status = p101_trace_parse_call_line(env, line, &event);

    TEST_ASSERT_EQUAL_INT(LINE_OTHER, status);
}

static void test_parse_call_line_skips_generic_resource_records(void)
{
    char              line[] = "P101RESOURCE\t4\t42\t7\t1\t100\t200\tACQUIRE\tmapping\t0x1000\t-\t4096\tprivate\t17\tmain\tserver.c\n";
    struct call_event event;
    enum line_status  status;

    status = p101_trace_parse_call_line(env, line, &event);

    TEST_ASSERT_EQUAL_INT(LINE_OTHER, status);
}

static void test_parse_completion_record(void)
{
    char              line[] = "P101COMPLETE\t4\t42\t7\t2\t160\t260\t1\t0\t0\n";
    struct call_event event;

    TEST_ASSERT_EQUAL_INT(LINE_COMPLETE, p101_trace_parse_call_line(env, line, &event));
    TEST_ASSERT_EQUAL_INT(4, event.version);
    TEST_ASSERT_EQUAL_INT(0, event.write_failed);
}

static void test_model_computes_call_duration(void)
{
    struct call_event enter;
    struct call_event leave;
    struct model     *model;

    p101_memset(env, &enter, 0, sizeof(enter));
    p101_memset(env, &leave, 0, sizeof(leave));
    enter.pid                    = 42;
    enter.context_id             = 7U;
    enter.kind                   = CALL_EVENT_ENTER;
    enter.monotonic_ns           = 100U;
    enter.monotonic_ns_available = 1;
    enter.line_number            = 17;
    enter.function_name          = "main";
    enter.call_name              = "p101_open";
    enter.arguments              = "-";
    enter.result                 = "-";
    enter.file_name              = "server.c";
    leave                        = enter;
    leave.kind                   = CALL_EVENT_EXIT;
    leave.monotonic_ns           = 160U;
    leave.line_number            = 41;
    leave.result                 = "3";

    model = p101_trace_model_create(env, error);
    TEST_ASSERT_NOT_NULL(model);
    p101_trace_model_ingest(env, error, model, &enter);
    p101_trace_model_ingest(env, error, model, &leave);

    TEST_ASSERT_FALSE(p101_error_has_error(error));
    TEST_ASSERT_EQUAL_UINT(1U, model->site_count);
    TEST_ASSERT_EQUAL_UINT(1U, model->sites[0].exits);
    TEST_ASSERT_EQUAL_UINT(1U, model->sites[0].timed_calls);
    TEST_ASSERT_EQUAL_UINT(60U, model->sites[0].total_duration_ns);
    TEST_ASSERT_EQUAL_UINT(60U, model->sites[0].max_duration_ns);
    p101_trace_model_destroy(env, &model);
}

static void test_completion_closes_nonreturning_process_frames(void)
{
    struct call_event enter;
    struct model     *model;

    p101_memset(env, &enter, 0, sizeof(enter));
    enter.pid           = 42;
    enter.context_id    = 7U;
    enter.kind          = CALL_EVENT_ENTER;
    enter.line_number   = 17;
    enter.function_name = "worker";
    enter.call_name     = "worker";
    enter.arguments     = "-";
    enter.result        = "-";
    enter.file_name     = "worker.c";

    model = p101_trace_model_create(env, error);
    TEST_ASSERT_NOT_NULL(model);
    p101_trace_model_ingest(env, error, model, &enter);
    p101_trace_model_complete(env, error, model, enter.pid, enter.context_id);

    TEST_ASSERT_FALSE(p101_error_has_error(error));
    TEST_ASSERT_EQUAL_UINT(1U, model->proc_count);
    TEST_ASSERT_EQUAL_UINT(0U, model->procs[0].depth);
    TEST_ASSERT_EQUAL_UINT(1U, model->procs[0].abandoned_at_completion);
    TEST_ASSERT_FALSE(p101_trace_model_has_stack_errors(model));
    p101_trace_model_destroy(env, &model);
}

static void write_temp_bytes(char *path, size_t path_size, const char *bytes, size_t byte_count)
{
    FILE *stream;
    int   fd;

    p101_strncpy(env, path, "/tmp/p101-trace-test-XXXXXX", path_size);
    path[path_size - 1U] = '\0';

    fd = p101_mkstemp(env, error, path);
    TEST_ASSERT_FALSE(p101_error_has_error(error));
    TEST_ASSERT_NOT_EQUAL(-1, fd);

    stream = p101_fdopen(env, error, fd, "wb");
    TEST_ASSERT_FALSE(p101_error_has_error(error));
    TEST_ASSERT_NOT_NULL(stream);

    TEST_ASSERT_EQUAL_UINT(byte_count, p101_fwrite(env, error, bytes, 1U, byte_count, stream));
    TEST_ASSERT_FALSE(p101_error_has_error(error));

    p101_fclose(env, error, stream);
    TEST_ASSERT_FALSE(p101_error_has_error(error));
}

static void test_runner_counts_embedded_nul_call_record_as_malformed(void)
{
    static const char bytes[] = {'P', '1', '0',  '1', 'C', 'A', 'L', 'L',  '\t', '2', '\t', '4', '2', '\t', '1', '\t', '1', '0',  '0', '\t', '2', '0',  '0', '\0', '\t', 'E', 'N', 'T', 'E', 'R', '\t',
                                 '1', '7', '\t', 'm', 'a', 'i', 'n', '\t', 'p',  '1', '0',  '1', '_', 'o',  'p', 'e',  'n', '\t', '-', '\t', '-', '\t', 's', 'e',  'r',  'v', 'e', 'r', '.', 'c', '\n'};
    char              path[256];
    struct arguments  args;
    int               status;

    p101_memset(env, &args, 0, sizeof(args));
    write_temp_bytes(path, sizeof(path), bytes, sizeof(bytes));

    args.mode     = TRACE_MODE_SUMMARY;
    args.log_name = path;

    status = p101_trace_run(env, error, &args);

    TEST_ASSERT_FALSE(p101_error_has_error(error));
    TEST_ASSERT_EQUAL_INT(EXIT_TROUBLE, status);

    p101_unlink(env, error, path);
}

static void test_argument_parser_covers_valid_and_invalid_options(void)
{
    struct arguments   args;
    char              *flat_argv[]          = {"p101-trace", "-v", "-f", "-l", "12", "-", NULL};
    char              *missing_argv[]       = {"p101-trace", "-l", NULL};
    char              *bad_number_argv[]    = {"p101-trace", "-l", "12x", NULL};
    char              *empty_number_argv[]  = {"p101-trace", "-l", "", NULL};
    char              *huge_number_argv[]   = {"p101-trace", "-l", "999999999999999999999999999999999999", NULL};
    char              *negative_argv[]      = {"p101-trace", "-l", "-1", NULL};
    char              *reverse_modes_argv[] = {"p101-trace", "-f", "-s", NULL};
    char              *unknown_argv[]       = {"p101-trace", "-z", NULL};
    char              *extra_argv[]         = {"p101-trace", "one", "two", NULL};
    char              *empty_argv[]         = {"p101-trace", "", NULL};
    char              *no_args_argv[]       = {"p101-trace", NULL};
    struct fault_state fault;

    reset_getopt();
    p101_trace_arguments_init(env, &args);
    p101_trace_parse_arguments(env, error, 6, flat_argv, &args);
    p101_trace_check_arguments(env, error, &args);
    TEST_ASSERT_FALSE(p101_error_has_error(error));
    TEST_ASSERT_TRUE(args.verbose);
    TEST_ASSERT_EQUAL_INT(TRACE_MODE_FLAT, args.mode);
    TEST_ASSERT_EQUAL_UINT(12U, args.slow_threshold_ns);
    TEST_ASSERT_EQUAL_STRING("-", args.log_name);

    p101_error_reset(error);
    reset_getopt();
    p101_trace_arguments_init(env, &args);
    p101_trace_parse_arguments(env, error, 2, missing_argv, &args);
    TEST_ASSERT_TRUE(p101_error_is_error(error, P101_ERROR_USER, ERR_USAGE));

    p101_error_reset(error);
    reset_getopt();
    p101_trace_arguments_init(env, &args);
    p101_trace_parse_arguments(env, error, 3, bad_number_argv, &args);
    TEST_ASSERT_TRUE(p101_error_is_error(error, P101_ERROR_USER, ERR_USAGE));

    p101_error_reset(error);
    reset_getopt();
    p101_trace_arguments_init(env, &args);
    p101_trace_parse_arguments(env, error, 3, empty_number_argv, &args);
    TEST_ASSERT_TRUE(p101_error_is_error(error, P101_ERROR_USER, ERR_USAGE));

    p101_error_reset(error);
    reset_getopt();
    p101_trace_arguments_init(env, &args);
    p101_trace_parse_arguments(env, error, 3, huge_number_argv, &args);
    TEST_ASSERT_TRUE(p101_error_is_error(error, P101_ERROR_USER, ERR_USAGE));

    p101_error_reset(error);
    reset_getopt();
    p101_trace_arguments_init(env, &args);
    p101_trace_parse_arguments(env, error, 3, negative_argv, &args);
    TEST_ASSERT_TRUE(p101_error_is_error(error, P101_ERROR_USER, ERR_USAGE));

    p101_error_reset(error);
    reset_getopt();
    p101_trace_arguments_init(env, &args);
    p101_trace_parse_arguments(env, error, 3, reverse_modes_argv, &args);
    TEST_ASSERT_TRUE(p101_error_is_error(error, P101_ERROR_USER, ERR_USAGE));

    p101_error_reset(error);
    reset_getopt();
    p101_trace_arguments_init(env, &args);
    p101_trace_parse_arguments(env, error, 2, unknown_argv, &args);
    TEST_ASSERT_TRUE(p101_error_is_error(error, P101_ERROR_USER, ERR_USAGE));

    p101_error_reset(error);
    reset_getopt();
    p101_trace_arguments_init(env, &args);
    p101_trace_parse_arguments(env, error, 3, extra_argv, &args);
    TEST_ASSERT_TRUE(p101_error_is_error(error, P101_ERROR_USER, ERR_USAGE));

    p101_error_reset(error);
    reset_getopt();
    p101_trace_arguments_init(env, &args);
    p101_trace_parse_arguments(env, error, 2, empty_argv, &args);
    p101_trace_check_arguments(env, error, &args);
    TEST_ASSERT_TRUE(p101_error_is_error(error, P101_ERROR_USER, ERR_USAGE));

    p101_error_reset(error);
    reset_getopt();
    p101_trace_arguments_init(env, &args);
    p101_trace_parse_arguments(env, error, 1, no_args_argv, &args);
    p101_trace_check_arguments(env, error, &args);
    TEST_ASSERT_FALSE(p101_error_has_error(error));
    TEST_ASSERT_NULL(args.log_name);

    fault.call_name = "strtoull";
    fault.fail_at   = 1U;
    fault.matches   = 0U;
    p101_env_set_fault_injector(env, inject_selected_failure, &fault);
    reset_getopt();
    p101_trace_arguments_init(env, &args);
    p101_trace_parse_arguments(env, error, 3, bad_number_argv, &args);
    TEST_ASSERT_TRUE(p101_error_has_error(error));
    p101_env_set_fault_injector(env, NULL, NULL);

    p101_error_reset(error);
    p101_trace_arguments_init(env, &args);
    p101_trace_test_handle_option(env, error, &args, 'l', "");
    TEST_ASSERT_TRUE(p101_error_is_error(error, P101_ERROR_USER, ERR_USAGE));

    p101_error_reset(error);
    p101_trace_arguments_init(env, &args);
    p101_trace_test_handle_option(env, error, &args, 99, "0");
    TEST_ASSERT_TRUE(p101_error_is_error(error, P101_ERROR_USER, ERR_USAGE));
}

static void test_parser_covers_null_fork_exit_and_malformed_records(void)
{
    struct call_event event;
    char              fork_line[] = "P101FORK\t4\t42\t7\t1\t100\t200\t43\t17\tmain\tserver.c\n";
    char              exit_line[] = "P101CALL\t4\t42\t7\t2\t160\t260\tEXIT\t17\tmain\tp101_open\t-\t-1\tserver.c\n";
    char              malformed[] = "P101CALL\t4\tbad\n";
    char              other[]     = "ordinary text\n";

    TEST_ASSERT_EQUAL_INT(LINE_MALFORMED, p101_trace_parse_call_line(env, NULL, &event));
    TEST_ASSERT_EQUAL_INT(LINE_MALFORMED, p101_trace_parse_call_line(env, malformed, NULL));
    TEST_ASSERT_EQUAL_INT(LINE_OK, p101_trace_parse_call_line(env, fork_line, &event));
    TEST_ASSERT_EQUAL_INT(CALL_EVENT_FORK, event.kind);
    TEST_ASSERT_EQUAL_INT64(43, event.child_pid);
    TEST_ASSERT_TRUE(p101_trace_call_line_is_ours(env, exit_line));
    TEST_ASSERT_EQUAL_INT(LINE_OK, p101_trace_parse_call_line(env, exit_line, &event));
    TEST_ASSERT_EQUAL_INT(CALL_EVENT_EXIT, event.kind);
    TEST_ASSERT_EQUAL_STRING("-1", event.result);
    TEST_ASSERT_EQUAL_INT(LINE_MALFORMED, p101_trace_parse_call_line(env, malformed, &event));
    TEST_ASSERT_FALSE(p101_trace_call_line_is_ours(env, other));
    TEST_ASSERT_EQUAL_INT(LINE_OTHER, p101_trace_test_map_parse_status(P101_TOOL_EVENT_PARSE_OTHER));
    TEST_ASSERT_EQUAL_INT(LINE_MALFORMED, p101_trace_test_map_parse_status(99));
}

static void test_model_line_counters_and_null_inputs(void)
{
    struct model model;

    p101_memset(env, &model, 0, sizeof(model));
    p101_trace_model_count_line(NULL, LINE_OTHER);
    p101_trace_model_count_line(&model, LINE_OTHER);
    p101_trace_model_count_line(&model, LINE_OK);
    p101_trace_model_count_line(&model, LINE_COMPLETE);
    p101_trace_model_count_line(&model, LINE_MALFORMED);
    p101_trace_model_count_line(&model, LINE_BAD_VERSION);
    p101_trace_model_count_line(&model, (enum line_status)99);
    TEST_ASSERT_EQUAL_UINT(1U, model.skipped);
    TEST_ASSERT_EQUAL_UINT(2U, model.malformed);
    TEST_ASSERT_EQUAL_UINT(1U, model.bad_version);

    p101_trace_model_ingest(env, error, NULL, NULL);
    p101_trace_model_destroy(env, NULL);
    {
        struct model *null_model;

        null_model = NULL;
        p101_trace_model_destroy(env, &null_model);
    }
    TEST_ASSERT_FALSE(p101_error_has_error(error));
}

static void test_model_tracks_unmatched_mismatched_and_failure_results(void)
{
    static const char *failure_results[] = {"NULL", "null", "false", "EOF", "-1", NULL};
    struct model      *model;
    struct call_event  event;

    model = p101_trace_model_create(env, error);
    TEST_ASSERT_NOT_NULL(model);

    event = make_event(CALL_EVENT_EXIT, 10, 1U, "unmatched", "NULL");
    p101_trace_model_ingest(env, error, model, &event);
    TEST_ASSERT_EQUAL_UINT(1U, model->procs[0].unmatched_exits);
    TEST_ASSERT_EQUAL_UINT(1U, model->sites[0].likely_failures);

    event = make_event(CALL_EVENT_ENTER, 10, 1U, "outer", "-");
    p101_trace_model_ingest(env, error, model, &event);
    event = make_event(CALL_EVENT_EXIT, 10, 1U, "different", "3");
    p101_trace_model_ingest(env, error, model, &event);
    TEST_ASSERT_EQUAL_UINT(1U, model->procs[0].mismatched_exits);
    TEST_ASSERT_EQUAL_UINT(1U, model->procs[0].depth);

    for(size_t index = 0U; failure_results[index] != NULL; index++)
    {
        event = make_event(CALL_EVENT_EXIT, 11 + (long)index, 1U, "failure", failure_results[index]);
        p101_trace_model_ingest(env, error, model, &event);
    }
    event = make_event(CALL_EVENT_EXIT, 30, 1U, "no-result", "-");
    p101_trace_model_ingest(env, error, model, &event);
    TEST_ASSERT_FALSE(p101_error_has_error(error));
    p101_trace_model_destroy(env, &model);
}

static void test_model_timing_handles_unavailable_reverse_overflow_and_maximum(void)
{
    struct model     *model;
    struct call_event enter;
    struct call_event leave;

    model = p101_trace_model_create(env, error);
    TEST_ASSERT_NOT_NULL(model);

    enter                        = make_event(CALL_EVENT_ENTER, 1, 1U, "clock", "-");
    leave                        = make_event(CALL_EVENT_EXIT, 1, 1U, "clock", "0");
    enter.monotonic_ns_available = 0;
    p101_trace_model_ingest(env, error, model, &enter);
    p101_trace_model_ingest(env, error, model, &leave);
    TEST_ASSERT_EQUAL_UINT(0U, model->sites[0].timed_calls);

    enter              = make_event(CALL_EVENT_ENTER, 1, 1U, "clock", "-");
    leave              = make_event(CALL_EVENT_EXIT, 1, 1U, "clock", "0");
    enter.monotonic_ns = 200U;
    leave.monotonic_ns = 100U;
    p101_trace_model_ingest(env, error, model, &enter);
    p101_trace_model_ingest(env, error, model, &leave);
    TEST_ASSERT_EQUAL_UINT(0U, model->sites[0].timed_calls);

    enter                        = make_event(CALL_EVENT_ENTER, 1, 1U, "clock", "-");
    leave                        = make_event(CALL_EVENT_EXIT, 1, 1U, "clock", "0");
    leave.monotonic_ns_available = 0;
    p101_trace_model_ingest(env, error, model, &enter);
    p101_trace_model_ingest(env, error, model, &leave);
    TEST_ASSERT_EQUAL_UINT(0U, model->sites[0].timed_calls);

    model->sites[0].total_duration_ns = SIZE_MAX - 1U;
    model->sites[0].max_duration_ns   = 1000U;
    enter                             = make_event(CALL_EVENT_ENTER, 1, 1U, "clock", "-");
    leave                             = make_event(CALL_EVENT_EXIT, 1, 1U, "clock", "0");
    leave.monotonic_ns                = 110U;
    p101_trace_model_ingest(env, error, model, &enter);
    p101_trace_model_ingest(env, error, model, &leave);
    TEST_ASSERT_EQUAL_UINT(SIZE_MAX, model->sites[0].total_duration_ns);
    TEST_ASSERT_EQUAL_UINT(1000U, model->sites[0].max_duration_ns);

    enter              = make_event(CALL_EVENT_ENTER, 1, 1U, "clock", "-");
    leave              = make_event(CALL_EVENT_EXIT, 1, 1U, "clock", "0");
    leave.monotonic_ns = 2200U;
    p101_trace_model_ingest(env, error, model, &enter);
    p101_trace_model_ingest(env, error, model, &leave);
    TEST_ASSERT_EQUAL_UINT(2100U, model->sites[0].max_duration_ns);

    p101_trace_model_destroy(env, &model);
}

static void test_model_grows_and_guards_active_call_storage(void)
{
    struct model      *model;
    struct call_event  event;
    struct proc_state *proc;

    model = p101_trace_model_create(env, error);
    event = make_event(CALL_EVENT_ENTER, 70, 1U, "deep", "-");
    for(size_t index = 0U; index <= ACTIVE_FIRST_CAPACITY; index++)
    {
        p101_trace_model_ingest(env, error, model, &event);
    }
    TEST_ASSERT_FALSE(p101_error_has_error(error));
    proc = p101_trace_find_proc(env, error, model, 70, 1U);
    TEST_ASSERT_TRUE(proc->active_capacity > ACTIVE_FIRST_CAPACITY);

    proc->depth           = SIZE_MAX / sizeof(struct active_call) + 1U;
    proc->active_capacity = proc->depth;
    p101_trace_model_ingest(env, error, model, &event);
    TEST_ASSERT_TRUE(p101_error_has_error(error));

    proc->depth           = 0U;
    proc->active_capacity = 0U;
    p101_trace_model_destroy(env, &model);
}

static void test_model_allocation_failures_are_reported(void)
{
    struct fault_state fault;
    struct model      *model;
    struct call_event  event;

    fault.call_name = "calloc";
    fault.fail_at   = 1U;
    fault.matches   = 0U;
    p101_env_set_fault_injector(env, inject_selected_failure, &fault);
    model = p101_trace_model_create(env, error);
    TEST_ASSERT_NULL(model);
    TEST_ASSERT_TRUE(p101_error_has_error(error));

    p101_error_reset(error);
    p101_env_set_fault_injector(env, NULL, NULL);
    model = p101_trace_model_create(env, error);
    TEST_ASSERT_NOT_NULL(model);
    event = make_event(CALL_EVENT_ENTER, 1, 1U, "allocation", "-");

    fault.call_name = "realloc";
    fault.fail_at   = 1U;
    fault.matches   = 0U;
    p101_env_set_fault_injector(env, inject_selected_failure, &fault);
    p101_trace_model_ingest(env, error, model, &event);
    TEST_ASSERT_TRUE(p101_error_has_error(error));
    TEST_ASSERT_EQUAL_UINT(0U, model->site_count);

    p101_error_reset(error);
    p101_env_set_fault_injector(env, NULL, NULL);
    (void)p101_trace_intern_site(env, error, model, &event);
    TEST_ASSERT_FALSE(p101_error_has_error(error));
    model->procs = (struct proc_state *)p101_calloc(env, error, 1U, sizeof(*model->procs));
    TEST_ASSERT_NOT_NULL(model->procs);
    model->proc_count               = 1U;
    model->proc_capacity            = 1U;
    model->procs[0].pid             = 1;
    model->procs[0].context_id      = 1U;
    model->procs[0].depth           = SIZE_MAX / 2U + 1U;
    model->procs[0].active_capacity = model->procs[0].depth;
    p101_trace_model_ingest(env, error, model, &event);
    TEST_ASSERT_TRUE(p101_error_has_error(error));

    p101_env_set_fault_injector(env, NULL, NULL);
    p101_trace_model_destroy(env, &model);
}

static void test_model_ingest_covers_each_failure_boundary(void)
{
    struct model      *model;
    struct call_event  event;
    struct call_event  different;
    struct fault_state fault;

    model = p101_trace_model_create(env, error);
    event = make_event(CALL_EVENT_ENTER, 8, 1U, "boundary", "-");
    p101_trace_model_ingest(env, error, model, NULL);
    p101_trace_model_ingest(env, error, NULL, &event);
    TEST_ASSERT_FALSE(p101_error_has_error(error));

    TEST_ASSERT_NOT_NULL(p101_trace_find_proc(env, error, model, event.pid, event.context_id));
    fault.call_name = "realloc";
    fault.fail_at   = 1U;
    fault.matches   = 0U;
    p101_env_set_fault_injector(env, inject_selected_failure, &fault);
    p101_trace_model_ingest(env, error, model, &event);
    TEST_ASSERT_TRUE(p101_error_has_error(error));

    p101_env_set_fault_injector(env, NULL, NULL);
    p101_error_reset(error);
    (void)p101_trace_intern_site(env, error, model, &event);
    TEST_ASSERT_FALSE(p101_error_has_error(error));
    fault.call_name = "realloc";
    fault.fail_at   = 1U;
    fault.matches   = 0U;
    p101_env_set_fault_injector(env, inject_selected_failure, &fault);
    p101_trace_model_ingest(env, error, model, &event);
    TEST_ASSERT_TRUE(p101_error_has_error(error));

    p101_env_set_fault_injector(env, NULL, NULL);
    p101_error_reset(error);
    event = make_event(CALL_EVENT_EXIT, 9, 1U, "unmatched-failure", "0");
    TEST_ASSERT_NOT_NULL(p101_trace_find_proc(env, error, model, event.pid, event.context_id));
    fault.call_name = "strdup";
    fault.fail_at   = 1U;
    fault.matches   = 0U;
    p101_env_set_fault_injector(env, inject_selected_failure, &fault);
    p101_trace_model_ingest(env, error, model, &event);
    TEST_ASSERT_TRUE(p101_error_has_error(error));

    p101_env_set_fault_injector(env, NULL, NULL);
    p101_error_reset(error);
    event = make_event(CALL_EVENT_ENTER, 10, 1U, "matched", "-");
    p101_trace_model_ingest(env, error, model, &event);
    different       = make_event(CALL_EVENT_EXIT, 10, 1U, "different", "0");
    fault.call_name = "strdup";
    fault.fail_at   = 1U;
    fault.matches   = 0U;
    p101_env_set_fault_injector(env, inject_selected_failure, &fault);
    p101_trace_model_ingest(env, error, model, &different);
    TEST_ASSERT_TRUE(p101_error_has_error(error));

    p101_env_set_fault_injector(env, NULL, NULL);
    p101_error_reset(error);
    different               = make_event(CALL_EVENT_EXIT, 10, 1U, "matched", "0");
    different.function_name = "different-function";
    p101_trace_model_ingest(env, error, model, &different);
    TEST_ASSERT_FALSE(p101_error_has_error(error));

    p101_trace_model_complete(env, error, model, 10, 1U);
    event = make_event(CALL_EVENT_ENTER, 10, 1U, "matched", "-");
    p101_trace_model_ingest(env, error, model, &event);
    different           = make_event(CALL_EVENT_EXIT, 10, 1U, "matched", "0");
    different.file_name = "different-file.c";
    p101_trace_model_ingest(env, error, model, &different);
    TEST_ASSERT_FALSE(p101_error_has_error(error));

    event = make_event(CALL_EVENT_EXIT, 11, 1U, "null-result", NULL);
    p101_trace_model_ingest(env, error, model, &event);
    TEST_ASSERT_FALSE(p101_error_has_error(error));

    p101_env_set_fault_injector(env, NULL, NULL);
    p101_trace_model_destroy(env, &model);
}

static void test_identity_reuses_entries_and_reports_allocation_failures(void)
{
    struct model      *model;
    struct call_event  event;
    struct fault_state fault;
    size_t             first;

    model = p101_trace_model_create(env, error);
    event = make_event(CALL_EVENT_ENTER, 5, 7U, "identity", "-");
    first = p101_trace_intern_site(env, error, model, &event);
    TEST_ASSERT_EQUAL_UINT(first, p101_trace_intern_site(env, error, model, &event));
    TEST_ASSERT_EQUAL_PTR(p101_trace_find_proc(env, error, model, 5, 7U), p101_trace_find_proc(env, error, model, 5, 7U));
    event.line_number++;
    TEST_ASSERT_NOT_EQUAL(first, p101_trace_intern_site(env, error, model, &event));
    event.line_number--;
    event.call_name = "other-call";
    TEST_ASSERT_NOT_EQUAL(first, p101_trace_intern_site(env, error, model, &event));
    event.call_name = "identity";
    event.file_name = "other-file.c";
    TEST_ASSERT_NOT_EQUAL(first, p101_trace_intern_site(env, error, model, &event));
    event.file_name     = "trace-test.c";
    event.function_name = "other-function";
    TEST_ASSERT_NOT_EQUAL(first, p101_trace_intern_site(env, error, model, &event));
    TEST_ASSERT_NOT_NULL(p101_trace_find_proc(env, error, model, 5, 8U));
    TEST_ASSERT_NOT_NULL(p101_trace_find_proc(env, error, model, 6, 7U));

    p101_trace_model_destroy(env, &model);
    for(size_t fail_at = 1U; fail_at <= 3U; fail_at++)
    {
        model           = p101_trace_model_create(env, error);
        fault.call_name = "strdup";
        fault.fail_at   = fail_at;
        fault.matches   = 0U;
        p101_env_set_fault_injector(env, inject_selected_failure, &fault);
        (void)p101_trace_intern_site(env, error, model, &event);
        TEST_ASSERT_TRUE(p101_error_has_error(error));
        TEST_ASSERT_EQUAL_UINT(0U, model->site_count);
        p101_error_reset(error);
        p101_trace_model_destroy(env, &model);
    }

    model           = p101_trace_model_create(env, error);
    fault.call_name = "realloc";
    fault.fail_at   = 1U;
    fault.matches   = 0U;
    p101_env_set_fault_injector(env, inject_selected_failure, &fault);
    TEST_ASSERT_NULL(p101_trace_find_proc(env, error, model, 5, 7U));
    TEST_ASSERT_TRUE(p101_error_has_error(error));
    p101_env_set_fault_injector(env, NULL, NULL);
    p101_trace_model_destroy(env, &model);
}

static void test_identity_capacity_guards_and_growth_failures(void)
{
    struct model      *model;
    struct fault_state fault;

    model = p101_trace_model_create(env, error);
    TEST_ASSERT_TRUE(p101_trace_test_reserve_sites(env, error, model));
    TEST_ASSERT_TRUE(p101_trace_test_reserve_sites(env, error, model));
    model->site_count = model->site_capacity + 1U;
    TEST_ASSERT_FALSE(p101_trace_test_reserve_sites(env, error, model));
    TEST_ASSERT_TRUE(p101_error_has_error(error));

    p101_error_reset(error);
    model->site_count    = SIZE_MAX / 2U + 1U;
    model->site_capacity = model->site_count;
    TEST_ASSERT_FALSE(p101_trace_test_reserve_sites(env, error, model));
    TEST_ASSERT_TRUE(p101_error_has_error(error));

    p101_error_reset(error);
    model->site_count    = SIZE_MAX / sizeof(struct call_site) + 1U;
    model->site_capacity = model->site_count;
    TEST_ASSERT_FALSE(p101_trace_test_reserve_sites(env, error, model));
    TEST_ASSERT_TRUE(p101_error_has_error(error));

    model->site_count    = 1U;
    model->site_capacity = 1U;
    p101_error_reset(error);
    fault.call_name = "realloc";
    fault.fail_at   = 1U;
    fault.matches   = 0U;
    p101_env_set_fault_injector(env, inject_selected_failure, &fault);
    TEST_ASSERT_FALSE(p101_trace_test_reserve_sites(env, error, model));
    TEST_ASSERT_TRUE(p101_error_has_error(error));

    p101_env_set_fault_injector(env, NULL, NULL);
    p101_error_reset(error);
    model->site_count    = 0U;
    model->site_capacity = 0U;
    p101_free(env, model->sites);
    model->sites = NULL;
    TEST_ASSERT_TRUE(p101_trace_test_reserve_procs(env, error, model));
    TEST_ASSERT_TRUE(p101_trace_test_reserve_procs(env, error, model));
    model->proc_count = model->proc_capacity + 1U;
    TEST_ASSERT_FALSE(p101_trace_test_reserve_procs(env, error, model));
    TEST_ASSERT_TRUE(p101_error_has_error(error));

    p101_error_reset(error);
    model->proc_count    = SIZE_MAX / 2U + 1U;
    model->proc_capacity = model->proc_count;
    TEST_ASSERT_FALSE(p101_trace_test_reserve_procs(env, error, model));
    TEST_ASSERT_TRUE(p101_error_has_error(error));

    p101_error_reset(error);
    model->proc_count    = SIZE_MAX / sizeof(struct proc_state) + 1U;
    model->proc_capacity = model->proc_count;
    TEST_ASSERT_FALSE(p101_trace_test_reserve_procs(env, error, model));
    TEST_ASSERT_TRUE(p101_error_has_error(error));

    model->proc_count    = 1U;
    model->proc_capacity = 1U;
    p101_error_reset(error);
    fault.call_name = "realloc";
    fault.fail_at   = 1U;
    fault.matches   = 0U;
    p101_env_set_fault_injector(env, inject_selected_failure, &fault);
    TEST_ASSERT_FALSE(p101_trace_test_reserve_procs(env, error, model));
    TEST_ASSERT_TRUE(p101_error_has_error(error));

    p101_env_set_fault_injector(env, NULL, NULL);
    model->site_count    = 0U;
    model->site_capacity = 0U;
    model->proc_count    = 0U;
    model->proc_capacity = 0U;
    p101_trace_model_destroy(env, &model);
}

static void test_fork_completion_and_stack_integrity_paths(void)
{
    struct model      *model;
    struct call_event  event;
    struct proc_state *parent;
    struct proc_state *child;
    struct fault_state fault;

    model = p101_trace_model_create(env, error);
    event = make_event(CALL_EVENT_FORK, 1, 1U, "p101_fork", "-");
    p101_trace_model_fork(env, error, NULL, &event);
    TEST_ASSERT_TRUE(p101_error_has_error(error));
    p101_error_reset(error);
    p101_trace_model_fork(env, error, model, NULL);
    TEST_ASSERT_TRUE(p101_error_has_error(error));
    p101_error_reset(error);
    event.pid = -1;
    p101_trace_model_fork(env, error, model, &event);
    TEST_ASSERT_TRUE(p101_error_has_error(error));
    p101_error_reset(error);
    event.pid       = 1;
    event.child_pid = -1;
    p101_trace_model_fork(env, error, model, &event);
    TEST_ASSERT_TRUE(p101_error_has_error(error));
    p101_error_reset(error);
    event.child_pid = 1;
    p101_trace_model_fork(env, error, model, &event);
    TEST_ASSERT_TRUE(p101_error_has_error(error));

    p101_error_reset(error);
    p101_trace_model_complete(env, error, NULL, 1, 1U);
    TEST_ASSERT_TRUE(p101_error_has_error(error));

    p101_error_reset(error);
    event.child_pid = 2;
    p101_trace_model_fork(env, error, model, &event);
    TEST_ASSERT_FALSE(p101_error_has_error(error));
    child = p101_trace_find_proc(env, error, model, 2, 1U);
    TEST_ASSERT_EQUAL_UINT(0U, child->depth);
    P101_ERROR_RAISE_CHECK(error);
    p101_trace_model_fork(env, error, model, &event);
    TEST_ASSERT_TRUE(p101_error_has_error(error));
    p101_error_reset(error);

    p101_trace_model_destroy(env, &model);
    model           = p101_trace_model_create(env, error);
    event           = make_event(CALL_EVENT_FORK, 1, 1U, "p101_fork", "-");
    event.child_pid = 2;
    fault.call_name = "realloc";
    fault.fail_at   = 1U;
    fault.matches   = 0U;
    p101_env_set_fault_injector(env, inject_selected_failure, &fault);
    p101_trace_model_fork(env, error, model, &event);
    TEST_ASSERT_TRUE(p101_error_has_error(error));
    p101_env_set_fault_injector(env, NULL, NULL);
    p101_error_reset(error);
    p101_trace_model_destroy(env, &model);

    model = p101_trace_model_create(env, error);
    TEST_ASSERT_NOT_NULL(p101_trace_find_proc(env, error, model, 1, 1U));
    model->proc_capacity = 1U;
    fault.call_name      = "realloc";
    fault.fail_at        = 1U;
    fault.matches        = 0U;
    p101_env_set_fault_injector(env, inject_selected_failure, &fault);
    p101_trace_model_fork(env, error, model, &event);
    TEST_ASSERT_TRUE(p101_error_has_error(error));
    p101_env_set_fault_injector(env, NULL, NULL);
    p101_error_reset(error);
    p101_trace_model_destroy(env, &model);

    model = p101_trace_model_create(env, error);
    event = make_event(CALL_EVENT_ENTER, 1, 1U, "p101_fork", "-");
    p101_trace_model_ingest(env, error, model, &event);
    event.kind      = CALL_EVENT_FORK;
    event.child_pid = 2;
    p101_trace_model_fork(env, error, model, &event);
    parent = p101_trace_find_proc(env, error, model, 1, 1U);
    child  = p101_trace_find_proc(env, error, model, 2, 1U);
    TEST_ASSERT_EQUAL_UINT(1U, parent->depth);
    TEST_ASSERT_EQUAL_UINT(0U, child->depth);
    child->depth = 1U;
    p101_trace_model_fork(env, error, model, &event);
    TEST_ASSERT_EQUAL_UINT(1U, child->depth);
    child->depth = 0U;

    event = make_event(CALL_EVENT_ENTER, 1, 1U, "ordinary", "-");
    p101_trace_model_ingest(env, error, model, &event);
    event.kind      = CALL_EVENT_FORK;
    event.child_pid = 3;
    p101_trace_model_fork(env, error, model, &event);
    child = p101_trace_find_proc(env, error, model, 3, 1U);
    TEST_ASSERT_EQUAL_UINT(2U, child->depth);
    TEST_ASSERT_TRUE(p101_trace_model_has_stack_errors(model));

    p101_trace_model_complete(env, error, model, 3, 1U);
    TEST_ASSERT_EQUAL_UINT(0U, child->depth);
    parent->depth           = 0U;
    parent->unmatched_exits = 1U;
    TEST_ASSERT_TRUE(p101_trace_model_has_stack_errors(model));
    parent->unmatched_exits  = 0U;
    parent->mismatched_exits = 1U;
    TEST_ASSERT_TRUE(p101_trace_model_has_stack_errors(model));
    parent->mismatched_exits = 0U;
    TEST_ASSERT_FALSE(p101_trace_model_has_stack_errors(model));
    TEST_ASSERT_TRUE(p101_trace_model_has_stack_errors(NULL));

    p101_error_reset(error);
    p101_trace_model_destroy(env, &model);
    model = p101_trace_model_create(env, error);
    event = make_event(CALL_EVENT_ENTER, 1, 1U, "ordinary", "-");
    p101_trace_model_ingest(env, error, model, &event);
    event.kind      = CALL_EVENT_FORK;
    event.child_pid = 2;
    fault.call_name = "realloc";
    fault.fail_at   = 1U;
    fault.matches   = 0U;
    p101_env_set_fault_injector(env, inject_selected_failure, &fault);
    p101_trace_model_fork(env, error, model, &event);
    TEST_ASSERT_TRUE(p101_error_has_error(error));

    p101_env_set_fault_injector(env, NULL, NULL);
    p101_error_reset(error);
    p101_trace_model_destroy(env, &model);
    model           = p101_trace_model_create(env, error);
    fault.call_name = "realloc";
    fault.fail_at   = 1U;
    fault.matches   = 0U;
    p101_env_set_fault_injector(env, inject_selected_failure, &fault);
    p101_trace_model_complete(env, error, model, 50, 1U);
    TEST_ASSERT_TRUE(p101_error_has_error(error));

    p101_env_set_fault_injector(env, NULL, NULL);
    p101_trace_model_destroy(env, &model);
}

static void test_reports_cover_tree_flat_summary_slow_and_health_variants(void)
{
    struct model      *model;
    struct call_event  enter;
    struct call_event  leave;
    struct proc_state *proc;
    struct fault_state fault;

    model = p101_trace_model_create(env, error);
    enter = make_event(CALL_EVENT_ENTER, 1, 1U, "alpha", "-");
    p101_trace_model_ingest(env, error, model, &enter);
    proc = p101_trace_find_proc(env, error, model, 1, 1U);
    p101_trace_print_tree_event(env, error, &enter, proc);
    p101_trace_print_flat_event(env, error, &enter);

    leave              = enter;
    leave.kind         = CALL_EVENT_EXIT;
    leave.result       = "7";
    leave.monotonic_ns = 150U;
    leave.line_number  = 19;
    p101_trace_print_tree_event(env, error, &leave, proc);
    p101_trace_print_flat_event(env, error, &leave);
    p101_trace_model_ingest(env, error, model, &leave);
    proc->depth = 0U;
    p101_trace_print_tree_event(env, error, &leave, proc);
    p101_trace_print_tree_event(env, error, &enter, proc);

    enter           = make_event(CALL_EVENT_ENTER, 2, 1U, "beta", "ignored");
    enter.arguments = "x=1";
    p101_trace_model_ingest(env, error, model, &enter);
    proc = p101_trace_find_proc(env, error, model, 2, 1U);
    p101_trace_print_tree_event(env, error, &enter, proc);
    leave              = enter;
    leave.kind         = CALL_EVENT_EXIT;
    leave.result       = "-";
    leave.monotonic_ns = 120U;
    p101_trace_print_tree_event(env, error, &leave, proc);
    p101_trace_model_ingest(env, error, model, &leave);

    enter = make_event(CALL_EVENT_ENTER, 3, 1U, "gamma", "-");
    p101_trace_model_ingest(env, error, model, &enter);
    leave              = enter;
    leave.kind         = CALL_EVENT_EXIT;
    leave.result       = "0";
    leave.monotonic_ns = 130U;
    p101_trace_model_ingest(env, error, model, &leave);
    model->sites[0].enters      = 5U;
    model->sites[1].enters      = 1U;
    model->sites[2].enters      = 3U;
    model->sites[2].timed_calls = 0U;

    p101_trace_report_summary(env, error, model);
    p101_trace_report_slow_calls(env, error, model, 0U);
    p101_trace_report_slow_calls(env, error, model, 40U);

    model->malformed                       = 1U;
    model->bad_version                     = 1U;
    model->stream_health.producer_count    = 2U;
    model->stream_health.producer_capacity = 2U;
    model->stream_health.producers         = (struct p101_tool_event_producer_health *)p101_calloc(env, error, 2U, sizeof(*model->stream_health.producers));
    TEST_ASSERT_NOT_NULL(model->stream_health.producers);
    model->stream_health.producers[0].completion_records = 1U;
    model->stream_health.completion_records              = 1U;
    model->stream_health.producer_write_failures         = 1U;
    model->stream_health.last_write_errno                = EIO;
    model->stream_health.duplicate_sequences             = 1U;
    model->stream_health.nonmonotonic_sequences          = 1U;
    model->stream_health.attempted_count_mismatches      = 1U;
    model->stream_health.records_after_completion        = 1U;
    model->procs[0].unmatched_exits                      = 1U;
    p101_trace_report_health(env, error, model);

    model->malformed                                = 2U;
    model->bad_version                              = 2U;
    model->stream_health.producer_write_failures    = 2U;
    model->stream_health.duplicate_sequences        = 2U;
    model->stream_health.nonmonotonic_sequences     = 2U;
    model->stream_health.attempted_count_mismatches = 2U;
    model->stream_health.records_after_completion   = 2U;
    p101_trace_report_health(env, error, model);

    model->malformed                                = 0U;
    model->bad_version                              = 0U;
    model->stream_health.completion_records         = 0U;
    model->stream_health.producer_write_failures    = 0U;
    model->stream_health.duplicate_sequences        = 0U;
    model->stream_health.nonmonotonic_sequences     = 0U;
    model->stream_health.attempted_count_mismatches = 0U;
    model->stream_health.records_after_completion   = 0U;
    model->procs[0].unmatched_exits                 = 0U;
    p101_trace_report_health(env, error, model);

    model->malformed                                     = 1U;
    model->stream_health.completion_records              = 1U;
    model->stream_health.producer_count                  = 1U;
    model->stream_health.producers[0].completion_records = 1U;
    p101_trace_report_health(env, error, model);

    model->stream_health.duplicate_sequences    = 0U;
    model->stream_health.nonmonotonic_sequences = 1U;
    p101_trace_report_health(env, error, model);
    model->stream_health.nonmonotonic_sequences     = 0U;
    model->stream_health.attempted_count_mismatches = 1U;
    p101_trace_report_health(env, error, model);
    model->stream_health.attempted_count_mismatches = 0U;
    model->stream_health.records_after_completion   = 1U;
    p101_trace_report_health(env, error, model);
    model->stream_health.records_after_completion = 0U;

    fault.call_name = "calloc";
    fault.fail_at   = 1U;
    fault.matches   = 0U;
    p101_env_set_fault_injector(env, inject_selected_failure, &fault);
    p101_trace_report_summary(env, error, model);
    TEST_ASSERT_TRUE(p101_error_has_error(error));
    p101_env_set_fault_injector(env, NULL, NULL);
    p101_error_reset(error);

    p101_trace_model_destroy(env, &model);
    model = p101_trace_model_create(env, error);
    p101_trace_report_summary(env, error, model);
    model->stream_health.records_observed  = 1U;
    model->stream_health.producer_count    = 1U;
    model->stream_health.producer_capacity = 1U;
    model->stream_health.producers         = (struct p101_tool_event_producer_health *)p101_calloc(env, error, 1U, sizeof(*model->stream_health.producers));
    TEST_ASSERT_NOT_NULL(model->stream_health.producers);
    model->stream_health.producers[0].completion_records = 1U;
    p101_trace_report_health(env, error, model);
    TEST_ASSERT_FALSE(p101_error_has_error(error));
    p101_trace_model_destroy(env, &model);
}

static void test_rank_comparison_is_total_and_deterministic(void)
{
    struct site_rank left;
    struct site_rank right;

    left.index  = 1U;
    left.total  = 1U;
    right.index = 2U;
    right.total = 2U;
    TEST_ASSERT_TRUE(p101_trace_test_compare_ranks(&left, &right) > 0);
    TEST_ASSERT_TRUE(p101_trace_test_compare_ranks(&right, &left) < 0);

    left.total  = 2U;
    right.total = 2U;
    TEST_ASSERT_TRUE(p101_trace_test_compare_ranks(&left, &right) < 0);
    TEST_ASSERT_TRUE(p101_trace_test_compare_ranks(&right, &left) > 0);
    right.index = left.index;
    TEST_ASSERT_EQUAL_INT(0, p101_trace_test_compare_ranks(&left, &right));
}

static void test_open_log_handles_stdin_regular_and_missing_paths(void)
{
    char  path[256];
    int   owned;
    FILE *stream;

    stream = p101_trace_open_log(env, error, NULL, &owned);
    TEST_ASSERT_EQUAL_PTR(stdin, stream);
    TEST_ASSERT_EQUAL_INT(0, owned);
    stream = p101_trace_open_log(env, error, "-", &owned);
    TEST_ASSERT_EQUAL_PTR(stdin, stream);
    TEST_ASSERT_EQUAL_INT(0, owned);

    write_temp_bytes(path, sizeof(path), "text\n", 5U);
    stream = p101_trace_open_log(env, error, path, &owned);
    TEST_ASSERT_NOT_NULL(stream);
    TEST_ASSERT_EQUAL_INT(1, owned);
    p101_fclose(env, error, stream);
    p101_unlink(env, error, path);

    p101_error_reset(error);
    stream = p101_trace_open_log(env, error, "/definitely/not/a/p101-trace-file", &owned);
    TEST_ASSERT_NULL(stream);
    TEST_ASSERT_TRUE(p101_error_has_error(error));
}

static void test_runner_covers_model_read_and_ingest_failures(void)
{
    static const char  call_record[] = "P101CALL\t4\t42\t7\t1\t100\t200\tENTER\t17\tmain\tp101_open\t-\t-\tserver.c\n";
    static const char  complete_record[] = "P101COMPLETE\t4\t42\t7\t2\t160\t260\t1\t0\t0\n";
    struct arguments   args;
    struct fault_state fault;
    char               path[256];

    p101_memset(env, &args, 0, sizeof(args));
    write_temp_bytes(path, sizeof(path), call_record, sizeof(call_record) - 1U);
    args.log_name = path;
    args.mode     = TRACE_MODE_TREE;

    fault.call_name = "calloc";
    fault.fail_at   = 1U;
    fault.matches   = 0U;
    p101_env_set_fault_injector(env, inject_selected_failure, &fault);
    TEST_ASSERT_EQUAL_INT(EXIT_TROUBLE, p101_trace_run(env, error, &args));
    TEST_ASSERT_TRUE(p101_error_has_error(error));

    p101_env_set_fault_injector(env, NULL, NULL);
    p101_error_reset(error);
    fault.call_name = "realloc";
    fault.fail_at   = 1U;
    fault.matches   = 0U;
    p101_env_set_fault_injector(env, inject_selected_failure, &fault);
    TEST_ASSERT_EQUAL_INT(EXIT_TROUBLE, p101_trace_run(env, error, &args));
    TEST_ASSERT_TRUE(p101_error_has_error(error));

    p101_env_set_fault_injector(env, NULL, NULL);
    p101_error_reset(error);
    write_temp_bytes(path, sizeof(path), complete_record, sizeof(complete_record) - 1U);
    args.log_name  = path;
    fault.call_name = "realloc";
    fault.fail_at   = 1U;
    fault.matches   = 0U;
    p101_env_set_fault_injector(env, inject_selected_failure, &fault);
    TEST_ASSERT_EQUAL_INT(EXIT_TROUBLE, p101_trace_run(env, error, &args));
    TEST_ASSERT_TRUE(p101_error_has_error(error));

    p101_env_set_fault_injector(env, NULL, NULL);
    p101_error_reset(error);
    args.log_name = "/tmp";
    TEST_ASSERT_EQUAL_INT(EXIT_TROUBLE, p101_trace_run(env, error, &args));
    TEST_ASSERT_TRUE(p101_error_has_error(error));

    p101_env_set_fault_injector(env, NULL, NULL);
    p101_error_reset(error);
    p101_unlink(env, error, path);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_parse_accepts_summary_mode_and_log_path);
    RUN_TEST(test_parse_rejects_competing_modes);
    RUN_TEST(test_parse_call_line_accepts_enter_record);
    RUN_TEST(test_parse_call_line_rejects_old_record);
    RUN_TEST(test_parse_call_line_rejects_bad_version);
    RUN_TEST(test_parse_call_line_skips_other_records);
    RUN_TEST(test_parse_call_line_skips_generic_resource_records);
    RUN_TEST(test_parse_completion_record);
    RUN_TEST(test_model_computes_call_duration);
    RUN_TEST(test_completion_closes_nonreturning_process_frames);
    RUN_TEST(test_runner_counts_embedded_nul_call_record_as_malformed);
    RUN_TEST(test_argument_parser_covers_valid_and_invalid_options);
    RUN_TEST(test_parser_covers_null_fork_exit_and_malformed_records);
    RUN_TEST(test_model_line_counters_and_null_inputs);
    RUN_TEST(test_model_tracks_unmatched_mismatched_and_failure_results);
    RUN_TEST(test_model_timing_handles_unavailable_reverse_overflow_and_maximum);
    RUN_TEST(test_model_grows_and_guards_active_call_storage);
    RUN_TEST(test_model_allocation_failures_are_reported);
    RUN_TEST(test_model_ingest_covers_each_failure_boundary);
    RUN_TEST(test_identity_reuses_entries_and_reports_allocation_failures);
    RUN_TEST(test_identity_capacity_guards_and_growth_failures);
    RUN_TEST(test_fork_completion_and_stack_integrity_paths);
    RUN_TEST(test_reports_cover_tree_flat_summary_slow_and_health_variants);
    RUN_TEST(test_rank_comparison_is_total_and_deterministic);
    RUN_TEST(test_open_log_handles_stdin_regular_and_missing_paths);
    RUN_TEST(test_runner_covers_model_read_and_ingest_failures);
    return UNITY_END();
}
