#ifndef __ERROR_CHECK_H__
#define __ERROR_CHECK_H__

#include <errno.h>
#include <stdlib.h>
#include <string.h>  
#include "log.h" 

#define ARGS_CHECK(argc, expected) \
    do { \
        if ((argc) != (expected)) { \
            LOG_ERROR("Args number error! Expected %d, got %d", expected, argc); \
            exit(1); \
        } \
    } while (0)


#define ERROR_CHECK(ret, error_flag, msg) \
    do { \
        if ((ret) == (error_flag)) { \
            LOG_ERROR("%s: %s", msg, strerror(errno)); \
            exit(1); \
        } \
    } while (0)

#define THREAD_ERROR_CHECK(ret, msg) \
    do { \
        if (0 != (ret)) { \
            LOG_ERROR("%s: %s", msg, strerror(ret)); \
            exit(1); \
        } \
    } while (0)

#endif // __ERROR_CHECK_H__