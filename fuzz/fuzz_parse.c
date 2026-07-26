/*
 * libFuzzer harness for p101-trace's argument parser
 * (src/main.c: parse_arguments()). This fuzzes the code YOU write, not a
 * library function.
 *
 * parse_arguments() is static, so we #include the whole main.c: that makes it
 * visible AND compiles it WITH the fuzzer instrumentation, which is what makes
 * this coverage-guided (watch "cov:" climb) instead of a blind black-box run.
 * Two collisions are handled by -D defines in fuzz/CMakeLists.txt, so nothing
 * in src/ has to change:
 *
 *   -Dmain=p101_unused_main       the program's main() would clash with
 *                                 libFuzzer's own main(); it is renamed aside.
 *   -Dp101_exit=p101_fuzz_exit    usage() is _Noreturn and calls p101_exit().
 *                                 Redirect it into a longjmp so -h is a normal
 *                                 input, not the end of the fuzz process.
 */
#include <p101_c/p101_setjmp.h>
#include <p101_c/p101_stdlib.h>
#include <p101_c/p101_string.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* longjmp target for the redirected p101_exit(). */
static jmp_buf g_fuzz_exit_jmp;

/* The code under test. main -> p101_unused_main, p101_exit -> p101_fuzz_exit. */
#include "../src/main.c"

/* The redirected p101_exit(): unwind back into the harness instead of terminating
 * the process. _Noreturn matches p101_exit()'s contract (usage() is _Noreturn);
 * p101_longjmp guarantees it never actually returns. */
_Noreturn void p101_fuzz_exit(const struct p101_env *env, int code)
{
    (void)code;
    p101_longjmp(env, g_fuzz_exit_jmp, 1);
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

    /* If parse_arguments takes the -h path, usage()->p101_exit()->p101_longjmp lands
     * here with a non-zero return -- a normal outcome, not a crash. */
    if(p101_setjmp(env, g_fuzz_exit_jmp) == 0)
    {
        parse_arguments(env, err, argc, argv, &args);

        if(p101_error_has_no_error(err))
        {
            check_arguments(env, err, &args);
        }

    }

done:
    p101_free(env, buf);
    p101_env_destroy(env);
    p101_error_destroy(err);
    return 0;
}
