#include "cli.h"
#include "constants.h"
#include "errors.h"
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
    char              line[] = "P101CALL\t1\t42\tENTER\t17\tmain\tp101_open\tpath=/tmp/x\t-\tserver.c\n";
    struct call_event event;
    enum line_status  status;

    status = p101_trace_parse_call_line(env, line, &event);

    TEST_ASSERT_EQUAL_INT(LINE_OK, status);
    TEST_ASSERT_EQUAL_INT64(42, event.pid);
    TEST_ASSERT_EQUAL_INT(CALL_EVENT_ENTER, event.kind);
    TEST_ASSERT_EQUAL_INT(17, event.line_number);
    TEST_ASSERT_EQUAL_STRING("main", event.function_name);
    TEST_ASSERT_EQUAL_STRING("p101_open", event.call_name);
    TEST_ASSERT_EQUAL_STRING("path=/tmp/x", event.arguments);
    TEST_ASSERT_EQUAL_STRING("-", event.result);
    TEST_ASSERT_EQUAL_STRING("server.c", event.file_name);
}

static void test_parse_call_line_rejects_bad_version(void)
{
    char              line[] = "P101CALL\t2\t42\tEXIT\t17\tmain\tp101_open\t-\t3\tserver.c\n";
    struct call_event event;
    enum line_status  status;

    status = p101_trace_parse_call_line(env, line, &event);

    TEST_ASSERT_EQUAL_INT(LINE_BAD_VERSION, status);
}

static void test_parse_call_line_skips_other_records(void)
{
    char              line[] = "P101FD\t1\t42\tOPEN\t3\t17\tmain\tserver.c\n";
    struct call_event event;
    enum line_status  status;

    status = p101_trace_parse_call_line(env, line, &event);

    TEST_ASSERT_EQUAL_INT(LINE_OTHER, status);
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
    static const char bytes[] = {'P', '1', '0', '1', 'C', 'A', 'L', 'L', '\t', '1', '\t', '4', '2', '\0', '\t', 'E', 'N', 'T', 'E', 'R', '\t', '1', '7', '\t', 'm', 'a', 'i', 'n', '\t', 'p', '1', '0', '1', '_', 'o', 'p', 'e', 'n', '\t', '-', '\t', '-', '\t', 's', 'e', 'r', 'v', 'e', 'r', '.', 'c', '\n'};
    char              path[256];
    struct arguments  args;
    int               status;

    p101_memset(env, &args, 0, sizeof(args));
    write_temp_bytes(path, sizeof(path), bytes, sizeof(bytes));

    args.mode     = TRACE_MODE_SUMMARY;
    args.log_name = path;

    status = p101_trace_run(env, error, &args);

    TEST_ASSERT_FALSE(p101_error_has_error(error));
    TEST_ASSERT_EQUAL_INT(EXIT_FINDINGS, status);

    p101_unlink(env, error, path);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_parse_accepts_summary_mode_and_log_path);
    RUN_TEST(test_parse_rejects_competing_modes);
    RUN_TEST(test_parse_call_line_accepts_enter_record);
    RUN_TEST(test_parse_call_line_rejects_bad_version);
    RUN_TEST(test_parse_call_line_skips_other_records);
    RUN_TEST(test_runner_counts_embedded_nul_call_record_as_malformed);
    return UNITY_END();
}
