#ifndef P101_TRACE_ARGUMENTS_H
#define P101_TRACE_ARGUMENTS_H

#include <stdbool.h>
#include <stddef.h>

enum trace_mode
{
    TRACE_MODE_TREE = 0,
    TRACE_MODE_SUMMARY,
    TRACE_MODE_FLAT
};

struct arguments
{
    const char     *log_name;
    bool            verbose;
    enum trace_mode mode;
    size_t          slow_threshold_ns;
};

#endif    // P101_TRACE_ARGUMENTS_H
