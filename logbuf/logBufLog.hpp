#include <stdio.h>
#ifdef LOGBUF_DEBUG
    #define LOGBUF_LOG_DEBUG(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#else
    #define LOGBUF_LOG_DEBUG(fmt, ...)
#endif
