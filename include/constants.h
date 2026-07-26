#ifndef P101_TRACE_CONSTANTS_H
#define P101_TRACE_CONSTANTS_H

#define CALL_PREFIX "P101CALL\t"

enum
{
    CALL_LOG_VERSION    = 1,
    LINE_MAX_BYTES      = 4096,
    MSG_LEN             = 256,
    DECIMAL_BASE        = 10,
    SITE_FIRST_CAPACITY = 32,
    PROC_FIRST_CAPACITY = 4,
    EXIT_CLEAN          = 0,
    EXIT_FINDINGS       = 1,
    EXIT_TROUBLE        = 2
};

#endif    // P101_TRACE_CONSTANTS_H
