#ifndef P101_TRACE_CONSTANTS_H
#define P101_TRACE_CONSTANTS_H

enum
{
    LINE_MAX_BYTES      = 4096,
    MSG_LEN             = 256,
    SITE_FIRST_CAPACITY = 32,
    PROC_FIRST_CAPACITY = 4,
    EXIT_CLEAN          = 0,
    EXIT_FINDINGS       = 1,
    EXIT_TROUBLE        = 2
};

#endif    // P101_TRACE_CONSTANTS_H
