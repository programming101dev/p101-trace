/*
 * libFuzzer harness for p101-trace's argument parser and call-record parser.
 */
#include "cli.h"
#include "constants.h"
#include "event.h"
#include "parse.h"
#include <p101_c/p101_stdlib.h>
#include <p101_c/p101_string.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* longjmp target for the redirected p101_exit(). */
static jmp_buf g_fuzz_exit_jmp;

/* The redirected p101_exit(): unwind back into the harness instead of terminating
 * the process. _Noreturn matches p101_exit()'s contract (usage() is _Noreturn);
 * longjmp guarantees it never actually returns. */
_Noreturn void p101_fuzz_exit(const struct p101_env *env, int code)
{
    (void)env;
    (void)code;
    longjmp(g_fuzz_exit_jmp, 1);
}

#define FUZZ_MAX_ARGS 64

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    char              *buf;
    char              *argv[FUZZ_MAX_ARGS];
    int                argc;
    char              *p;
    struct p101_error *err;
    struct p101_env   *env;
    struct arguments   args;

    err = p101_error_create(false);
    env = p101_env_create(err, NULL);

    /* getopt/argv need a writable, NUL-terminated C string. */
    buf = (char *)p101_malloc(env, err, size + 1);
    if(buf == NULL)
    {
        goto done;
    }
    p101_memcpy(env, buf, data, size);
    buf[size] = '\0';

    /* Carve the input into an argv, splitting on whitespace. argv[0] is a fixed
     * program name; the fuzzer controls every token after it. */
    argv[0] = (char *)"prog";
    argc    = 1;
    p       = buf;
    while(argc < FUZZ_MAX_ARGS - 1)
    {
        while(*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\v' || *p == '\f')
        {
            p++;
        }
        if(*p == '\0')
        {
            break;
        }
        argv[argc++] = p;
        while(*p != '\0' && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r' && *p != '\v' && *p != '\f')
        {
            p++;
        }
        if(*p != '\0')
        {
            *p++ = '\0';
        }
    }
    argv[argc] = NULL;

    /* getopt keeps a global cursor across calls; reset it before every parse. */
#ifdef __GLIBC__
    optind = 0; /* glibc: 0 forces a full re-init */
#else
    {
        extern int optreset; /* BSD / macOS / FreeBSD getopt */
        optreset = 1;
        optind   = 1;
    }
#endif

    p101_memset(env, &args, 0, sizeof(args));
    args.mode = TRACE_MODE_TREE;

    if(setjmp(g_fuzz_exit_jmp) == 0)
    {
        p101_trace_parse_arguments(env, err, argc, argv, &args);

        if(p101_error_has_no_error(err))
        {
            p101_trace_check_arguments(env, err, &args);
        }

        if(size < LINE_MAX_BYTES)
        {
            struct call_event event;

            p101_memcpy(env, buf, data, size);
            buf[size] = '\0';
            p101_memset(env, &event, 0, sizeof(event));
            (void)p101_trace_parse_call_line(env, buf, &event);
        }
    }

done:
    p101_free(env, buf);
    p101_env_destroy(env);
    p101_error_destroy(err);
    return 0;
}
