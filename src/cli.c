#include "cli.h"
#include "constants.h"
#include "errors.h"
#include <p101_c/p101_ctype.h>
#include <p101_c/p101_stdio.h>
#include <p101_c/p101_stdlib.h>
#include <p101_c/p101_string.h>
#include <p101_posix/p101_unistd.h>
#include <stdlib.h>

void p101_trace_arguments_init(const struct p101_env *env, struct arguments *args)
{
    P101_TRACE(env);
    p101_memset(env, args, 0, sizeof(*args));
    args->mode = TRACE_MODE_TREE;
}

void p101_trace_parse_arguments(const struct p101_env *env, struct p101_error *err, int argc, char *argv[], struct arguments *args)
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
                p101_trace_usage(env, err, argv[0], EXIT_CLEAN, NULL);
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

void p101_trace_check_arguments(const struct p101_env *env, struct p101_error *err, const struct arguments *args)
{
    P101_TRACE(env);

    if(args->log_name != NULL && args->log_name[0] == '\0')
    {
        P101_ERROR_RAISE_USER(err, "The call log path must not be empty.", ERR_USAGE);
    }
}

_Noreturn void p101_trace_usage(const struct p101_env *env, struct p101_error *err, const char *program_name, int exit_code, const char *message)
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
