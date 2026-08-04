#if !defined(ARRAY_UTIL_H)
#define ARRAY_UTIL_H

#include "cbase.h"

#define LRC_ARRAY_RESERVE(ARRAY, NEEDED_COUNT) \
    ARRAY_RESERVE((ARRAY), (NEEDED_COUNT))
#define LRC_ARRAY_INIT_COUNT(ARRAY, COUNT) \
    ARRAY_INIT_COUNT((ARRAY), (COUNT))

#define lrc_array_capacity generic_array_capacity
#define lrc_array_set_count generic_array_set_count

#endif /* ARRAY_UTIL_H */
