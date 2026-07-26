#define main p101_test_unused_main
#include "../src/main.c"
#undef main

#include "unity.h"
#include <p101_c/p101_string.h>
#include <p101_env/env.h>
#include <p101_error/error.h>
#include <stdbool.h>

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

    parse_arguments(env, error, 3, argv, &args);
    check_arguments(env, error, &args);

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

    parse_arguments(env, error, 3, argv, &args);

    TEST_ASSERT_TRUE(p101_error_is_error(error, P101_ERROR_USER, ERR_USAGE));
}

static void test_parse_call_line_accepts_enter_record(void)
{
    char              line[] = "P101CALL\t1\t42\tENTER\t17\tmain\tp101_open\tpath=/tmp/x\t-\tserver.c\n";
    struct call_event event;
    enum line_status  status;

    status = parse_call_line(env, line, &event);

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

    status = parse_call_line(env, line, &event);

    TEST_ASSERT_EQUAL_INT(LINE_BAD_VERSION, status);
}

static void test_parse_call_line_skips_other_records(void)
{
    char              line[] = "P101FD\t1\t42\tOPEN\t3\t17\tmain\tserver.c\n";
    struct call_event event;
    enum line_status  status;

    status = parse_call_line(env, line, &event);

    TEST_ASSERT_EQUAL_INT(LINE_OTHER, status);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_parse_accepts_summary_mode_and_log_path);
    RUN_TEST(test_parse_rejects_competing_modes);
    RUN_TEST(test_parse_call_line_accepts_enter_record);
    RUN_TEST(test_parse_call_line_rejects_bad_version);
    RUN_TEST(test_parse_call_line_skips_other_records);
    return UNITY_END();
}
