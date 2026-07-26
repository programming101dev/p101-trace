#ifndef P101_TRACE_PARSE_H
#define P101_TRACE_PARSE_H

#include "event.h"
#include <p101_env/env.h>
#include <stdbool.h>

bool             p101_trace_call_line_is_ours(const struct p101_env *env, const char *line);
char            *p101_trace_split_tab(char **cursor);
bool             p101_trace_parse_long_field(const char *text, long min, long max, long *out);
enum line_status p101_trace_parse_call_line(const struct p101_env *env, char *line, struct call_event *event);

#endif    // P101_TRACE_PARSE_H
