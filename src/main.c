#include "cli.h"
#include "constants.h"
#include "errors.h"
#include "runner.h"
#include <p101_c/p101_stdio.h>
#include <p101_c/p101_stdlib.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    struct p101_error *err;
    struct p101_env   *env;
    struct arguments   args;
    int                ret_val;

    ret_val = EXIT_TROUBLE;
    err     = p101_error_create(false);
    env     = p101_env_create(err, NULL);

    p101_trace_arguments_init(env, &args);
    p101_trace_parse_arguments(env, err, argc, argv, &args);

    if(p101_error_has_error(err))
    {
        goto done;
    }

    if(args.verbose)
    {
        p101_env_set_tracer(env, p101_env_default_tracer);
    }

    p101_trace_check_arguments(env, err, &args);

    if(p101_error_has_error(err))
    {
        goto done;
    }

    ret_val = p101_trace_run(env, err, &args);

done:
    if(p101_error_has_error(err))
    {
        if(p101_error_is_error(err, P101_ERROR_USER, ERR_USAGE))
        {
            p101_trace_usage(env, err, argv[0], EXIT_TROUBLE, p101_error_get_message(err));
        }

        p101_fprintf(env, err, stderr, "%s\n", p101_error_get_message(err));
        ret_val = EXIT_TROUBLE;
    }

    p101_env_destroy(env);
    p101_error_destroy(err);

    return ret_val;
}
