#include "cli.h"
#include "constants.h"
#include "errors.h"
#include <errno.h>
#include <limits.h>
#include <p101_c/p101_ctype.h>
#include <p101_c/p101_stdio.h>
#include <p101_c/p101_stdlib.h>
#include <p101_c/p101_string.h>
#include <p101_posix/p101_unistd.h>
#include <stdint.h>
#include <stdlib.h>

static void handle_option(const struct p101_env *env, struct p101_error *err, struct arguments *args, int option, const char *option_argument, int option_character, const char *program_name);

void p101_trace_arguments_init(const struct p101_env *env, struct arguments *args)
{
    P101_TRACE_SCOPE(env);
    p101_memset(env, args, 0, sizeof(*args));
    args->mode = TRACE_MODE_TREE;
}

void p101_trace_parse_arguments(const struct p101_env *env, struct p101_error *err, int argc, char *argv[], struct arguments *args)
{
    int opt;

    P101_TRACE_SCOPE(env);
    opterr = 0;

    if(argc == 2 && p101_strcmp(env, argv[1], "--help") == 0)
    {
        p101_trace_usage(env, err, argv[0], EXIT_SUCCESS, NULL);
    }

    while((opt = p101_getopt(env, argc, argv, ":hvfsl:")) != -1 && p101_error_has_no_error(err))
    {
        handle_option(env, err, args, opt, optarg, optopt, argv[0]);
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

static void handle_option(const struct p101_env *env, struct p101_error *err, struct arguments *args, int option, const char *option_argument, int option_character, const char *program_name)
{
    switch(option)
    {
        case 'h':
        {
            p101_trace_usage(env, err, program_name, EXIT_CLEAN, NULL);
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
        case 'l':
        {
            char              *end;
            unsigned long long value;

            if(option_argument[0] == '\0' || option_argument[0] == '-')
            {
                P101_ERROR_RAISE_USER(err, "The slow-call threshold must be a non-negative integer number of nanoseconds.", ERR_USAGE);
                break;
            }
            errno = 0;
            end   = NULL;
            value = p101_strtoull(env, err, option_argument, &end, DECIMAL_RADIX);
            if(p101_error_has_error(err) || *end != '\0')
            {
                P101_ERROR_RAISE_USER(err, "The slow-call threshold must be a non-negative integer number of nanoseconds.", ERR_USAGE);
                break;
            }
#if SIZE_MAX < ULLONG_MAX
            if(value > SIZE_MAX)
            {
                P101_ERROR_RAISE_USER(err, "The slow-call threshold is too large for this platform.", ERR_USAGE);
                break;
            }
#endif
            args->slow_threshold_ns = (size_t)value;
            break;
        }
        case ':':
        {
            char msg[MSG_LEN];

            p101_snprintf(env, err, msg, sizeof(msg), "Option '-%c' requires an argument.", option_character);
            P101_ERROR_RAISE_USER(err, msg, ERR_USAGE);
            break;
        }
        case '?':
        {
            char msg[MSG_LEN];

            if(p101_isprint(env, option_character))
            {
                p101_snprintf(env, err, msg, sizeof(msg), "Unknown option '-%c'.", option_character);
            }
            else
            {
                p101_snprintf(env, err, msg, sizeof(msg), "Unknown option character 0x%02X.", (unsigned)(unsigned char)option_character);
            }
            P101_ERROR_RAISE_USER(err, msg, ERR_USAGE);
            break;
        }
        default:
        {
            char msg[MSG_LEN];

            p101_snprintf(env, err, msg, sizeof(msg), "Internal error: unhandled option value %d returned by getopt.", option);
            P101_ERROR_RAISE_USER(err, msg, ERR_USAGE);
            break;
        }
    }
}

#ifdef P101_TRACE_TESTING
void p101_trace_test_handle_option(const struct p101_env *env, struct p101_error *err, struct arguments *args, int option, const char *option_argument)
{
    handle_option(env, err, args, option, option_argument, option, "p101-trace-test");
}
#endif

void p101_trace_check_arguments(const struct p101_env *env, struct p101_error *err, const struct arguments *args)
{
    P101_TRACE_SCOPE(env);

    if(args->log_name != NULL && args->log_name[0] == '\0')
    {
        P101_ERROR_RAISE_USER(err, "The call log path must not be empty.", ERR_USAGE);
    }
}

_Noreturn void p101_trace_usage(const struct p101_env *env, struct p101_error *err, const char *program_name, int exit_code, const char *message)
{
    P101_TRACE_SCOPE(env);

#ifndef P101_SUPPRESS_USAGE_TEXT
    if(message != NULL)
    {
        p101_fprintf(env, err, stderr, "%s\n\n", message);
    }

    p101_fprintf(env, err, stderr, "Usage: %s [-h] [-v] [-s|-f] [-l <nanoseconds>] [<call-log>]\n", program_name);
    p101_fputs(env, err, "\n", stderr);
    p101_fputs(env, err, "Reads P101CALL records from a p101 call log and prints a portable\n", stderr);
    p101_fputs(env, err, "strace/ltrace-style view. With no file, or with \"-\", reads stdin.\n", stderr);
    p101_fputs(env, err, "\n", stderr);
    p101_fputs(env, err, "Options:\n", stderr);
    p101_fputs(env, err, "  -h        Display this help message and exit\n", stderr);
    p101_fputs(env, err, "  -v        Enable verbose p101 tracing in p101-trace itself\n", stderr);
    p101_fputs(env, err, "  -s        Summary counts instead of the call tree\n", stderr);
    p101_fputs(env, err, "  -f        Flat normalized records instead of the call tree\n", stderr);
    p101_fputs(env, err, "  -l <ns>   In summary mode, list call sites whose maximum duration meets this threshold\n", stderr);
#else
    (void)message;
    (void)program_name;
#endif

    p101_exit(env, exit_code);
}
