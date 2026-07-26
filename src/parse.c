#include "parse.h"
#include "constants.h"
#include <limits.h>
#include <p101_c/p101_stdlib.h>
#include <p101_c/p101_string.h>
#include <stdbool.h>
#include <stdlib.h>

enum line_status p101_trace_parse_call_line(const struct p101_env *env, char *line, struct call_event *event)
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
    version_text         = p101_trace_split_tab(&cursor);
    pid_text             = p101_trace_split_tab(&cursor);
    kind_text            = p101_trace_split_tab(&cursor);
    line_text            = p101_trace_split_tab(&cursor);
    event->function_name = p101_trace_split_tab(&cursor);
    event->call_name     = p101_trace_split_tab(&cursor);
    event->arguments     = p101_trace_split_tab(&cursor);
    event->result        = p101_trace_split_tab(&cursor);
    event->file_name     = cursor;

    if(event->function_name == NULL || event->call_name == NULL || event->arguments == NULL || event->result == NULL || event->file_name == NULL)
    {
        goto done;
    }

    if(!p101_trace_parse_long_field(version_text, 0, LONG_MAX, &version))
    {
        goto done;
    }

    if(version != CALL_LOG_VERSION)
    {
        status = LINE_BAD_VERSION;
        goto done;
    }

    if(!p101_trace_parse_long_field(pid_text, 0, LONG_MAX, &pid))
    {
        goto done;
    }

    if(!p101_trace_parse_long_field(line_text, 0, INT_MAX, &line_number))
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

bool p101_trace_call_line_is_ours(const struct p101_env *env, const char *line)
{
    return (p101_strncmp(env, line, CALL_PREFIX, sizeof(CALL_PREFIX) - 1U) == 0) != 0;
}

char *p101_trace_split_tab(char **cursor)
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

bool p101_trace_parse_long_field(const char *text, long min, long max, long *out)
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
