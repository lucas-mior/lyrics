#if !defined(ARRAY_UTIL_H)
#define ARRAY_UTIL_H

#include "cbase.h"

static bool
lrc_array_reserve_raw(
    void **array,
    int32 needed_count,
    int64 item_size
) {
    GenericArrayHeader *header;
    int32 old_count;

    if (array == NULL) {
        return false;
    }
    if (needed_count < 0) {
        return false;
    }
    if (needed_count == 0) {
        return true;
    }
    if (*array == NULL) {
        *array = generic_array_init(needed_count, item_size);
        return true;
    }

    header = ARRAY_HEADER(*array);
    if (needed_count <= header->cap) {
        return true;
    }

    old_count = header->count;
    if (header->cap <= 0) {
        free2(header, SIZEOF(*header));
        *array = generic_array_init(needed_count, item_size);
        ARRAY_HEADER(*array)->count = old_count;
        return true;
    }

    while (needed_count > header->cap) {
        header->count = header->cap;
        *array = generic_array_grow(*array, item_size);
        header = ARRAY_HEADER(*array);
    }
    header->count = old_count;

    return true;
}

static int32
lrc_array_capacity(void *array) {
    if (array == NULL) {
        return 0;
    }

    return ARRAY_HEADER(array)->cap;
}

static void
lrc_array_set_count(void *array, int32 count) {
    if (array == NULL) {
        return;
    }

    ARRAY_HEADER(array)->count = count;

    return;
}

#define LRC_ARRAY_RESERVE(ARRAY, NEEDED_COUNT) \
    lrc_array_reserve_raw((void **)&(ARRAY), \
                          (NEEDED_COUNT), \
                          SIZEOF(*(ARRAY)))
#define LRC_ARRAY_INIT_COUNT(ARRAY, COUNT) do { \
    ARRAY_INIT((ARRAY), (COUNT)); \
    lrc_array_set_count((ARRAY), (COUNT)); \
} while (0)

#endif /* ARRAY_UTIL_H */
