#include "cli.h"
#include "constants.h"
#include "errors.h"
#include "model.h"
#include "model_lifecycle.h"
#include "parse.h"
#include "runner.h"
#include "unity.h"
#include <p101_c/p101_stdio.h>
#include <p101_c/p101_string.h>
#include <p101_env/env.h>
#include <p101_error/error.h>
#include <p101_posix/p101_stdio.h>
#include <p101_posix/p101_stdlib.h>
#include <p101_posix/p101_unistd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

static struct p101_error *error;
static struct p101_env   *env;

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
    static const char bytes[] = {'P', '1', '0', '1', 'C', 'A', 'L', 'L', '\t', '2', '\t', '4', '2', '\t', '1', '\t', '1', '0', '0', '\t', '2', '0', '0', '\0', '\t', 'E', 'N', 'T', 'E', 'R', '\t', '1', '7', '\t', 'm', 'a', 'i', 'n', '\t', 'p', '1', '0', '1', '_', 'o', 'p', 'e', 'n', '\t', '-', '\t', '-', '\t', 's', 'e', 'r', 'v', 'e', 'r', '.', 'c', '\n'};
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
    return UNITY_END();
}
