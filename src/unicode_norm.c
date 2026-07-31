#include "unicode_norm.h"

#include "cbase.h"

#if LRC_UNICODE_ENABLE_ICU
#include <unicode/ucasemap.h>
#include <unicode/unorm2.h>
#include <unicode/ustring.h>
#endif

#if !defined(TESTING_unicode_norm)
#define TESTING_unicode_norm 0
#endif

static void
ctc_unicode_norm_result_init(CtcUnicodeNormResult *result) {
    if (result == NULL) {
        return;
    }

    memset64(result, 0, SIZEOF(*result));

    return;
}

static void
ctc_unicode_norm_result_destroy(CtcUnicodeNormResult *result) {
    if (result == NULL) {
        return;
    }

    if (result->text) {
        free2(result->text, result->text_cap*SIZEOF(*result->text));
    }

    ctc_unicode_norm_result_init(result);

    return;
}

static bool
ctc_unicode_norm_reserve(
    CtcUnicodeNormResult *result,
    int32 needed
) {
    int32 new_cap;

    if (needed < 0) {
        return false;
    }
    if ((needed + 1) <= result->text_cap) {
        return true;
    }

    new_cap = result->text_cap;
    if (new_cap <= 0) {
        new_cap = 64;
    }
    while (new_cap < (needed + 1)) {
        new_cap *= 2;
    }

    result->text = realloc2(result->text,
                            result->text_cap,
                            new_cap,
                            SIZEOF(*result->text));
    result->text_cap = new_cap;

    return true;
}

static bool
ctc_unicode_norm_copy_fallback(
    char *text,
    int32 text_len,
    CtcUnicodeNormResult *result
) {
    if (text_len < 0) {
        return false;
    }
    if (!ctc_unicode_norm_reserve(result, text_len)) {
        return false;
    }

    if (text_len > 0) {
        memcpy64(result->text, text, text_len);
    }
    result->text[text_len] = '\0';
    result->text_len = text_len;
    result->used_icu = false;

    return true;
}

static bool
ctc_unicode_norm_icu_available(void) {
#if LRC_UNICODE_ENABLE_ICU
    return true;
#else
    return false;
#endif
}

#if LRC_UNICODE_ENABLE_ICU
static bool
ctc_unicode_norm_check_icu_status(UErrorCode status) {
    if (U_SUCCESS(status)) {
        return true;
    }
    if (status == U_BUFFER_OVERFLOW_ERROR) {
        return true;
    }

    return false;
}

static bool
ctc_unicode_norm_preflight_utf16_from_utf8(
    char *text,
    int32 text_len,
    int32 *utf16_len
) {
    UErrorCode status;

    status = U_ZERO_ERROR;
    u_strFromUTF8(NULL,
                  0,
                  (int32_t *)utf16_len,
                  text,
                  (int32_t)text_len,
                  &status);

    return ctc_unicode_norm_check_icu_status(status);
}

static bool
ctc_unicode_norm_utf16_from_utf8(
    char *text,
    int32 text_len,
    UChar *utf16,
    int32 utf16_cap,
    int32 *utf16_len
) {
    UErrorCode status;

    status = U_ZERO_ERROR;
    u_strFromUTF8(utf16,
                  (int32_t)utf16_cap,
                  (int32_t *)utf16_len,
                  text,
                  (int32_t)text_len,
                  &status);

    return U_SUCCESS(status);
}

static bool
ctc_unicode_norm_preflight_nfkc(
    UChar *utf16,
    int32 utf16_len,
    UNormalizer2 *normalizer,
    int32 *nfkc_len
) {
    UErrorCode status;

    status = U_ZERO_ERROR;
    *nfkc_len = (int32)unorm2_normalize(normalizer,
                                        utf16,
                                        (int32_t)utf16_len,
                                        NULL,
                                        0,
                                        &status);

    return ctc_unicode_norm_check_icu_status(status);
}

static bool
ctc_unicode_norm_nfkc(
    UChar *utf16,
    int32 utf16_len,
    UNormalizer2 *normalizer,
    UChar *nfkc,
    int32 nfkc_cap,
    int32 *nfkc_len
) {
    UErrorCode status;

    status = U_ZERO_ERROR;
    *nfkc_len = (int32)unorm2_normalize(normalizer,
                                        utf16,
                                        (int32_t)utf16_len,
                                        nfkc,
                                        (int32_t)nfkc_cap,
                                        &status);

    return U_SUCCESS(status);
}

static bool
ctc_unicode_norm_preflight_lower(
    UChar *nfkc,
    int32 nfkc_len,
    int32 *lower_len
) {
    UErrorCode status;

    status = U_ZERO_ERROR;
    *lower_len = (int32)u_strToLower(NULL,
                                     0,
                                     nfkc,
                                     (int32_t)nfkc_len,
                                     "",
                                     &status);

    return ctc_unicode_norm_check_icu_status(status);
}

static bool
ctc_unicode_norm_lower(
    UChar *nfkc,
    int32 nfkc_len,
    UChar *lower,
    int32 lower_cap,
    int32 *lower_len
) {
    UErrorCode status;

    status = U_ZERO_ERROR;
    *lower_len = (int32)u_strToLower(lower,
                                     (int32_t)lower_cap,
                                     nfkc,
                                     (int32_t)nfkc_len,
                                     "",
                                     &status);

    return U_SUCCESS(status);
}

static bool
ctc_unicode_norm_preflight_utf8_from_utf16(
    UChar *utf16,
    int32 utf16_len,
    int32 *utf8_len
) {
    UErrorCode status;

    status = U_ZERO_ERROR;
    u_strToUTF8(NULL,
                0,
                (int32_t *)utf8_len,
                utf16,
                (int32_t)utf16_len,
                &status);

    return ctc_unicode_norm_check_icu_status(status);
}

static bool
ctc_unicode_norm_utf8_from_utf16(
    UChar *utf16,
    int32 utf16_len,
    CtcUnicodeNormResult *result
) {
    UErrorCode status;
    int32 utf8_len;

    if (!ctc_unicode_norm_preflight_utf8_from_utf16(utf16,
                                                    utf16_len,
                                                    &utf8_len)) {
        return false;
    }
    if (!ctc_unicode_norm_reserve(result, utf8_len)) {
        return false;
    }

    status = U_ZERO_ERROR;
    u_strToUTF8(result->text,
                (int32_t)result->text_cap,
                (int32_t *)&result->text_len,
                utf16,
                (int32_t)utf16_len,
                &status);
    if (!U_SUCCESS(status)) {
        return false;
    }

    result->text[result->text_len] = '\0';
    result->used_icu = true;

    return true;
}

static bool
ctc_unicode_norm_nfkc_lower_icu(
    char *text,
    int32 text_len,
    CtcUnicodeNormResult *result
) {
    UNormalizer2 *normalizer;
    UChar *utf16;
    UChar *nfkc;
    UChar *lower;
    UErrorCode status;
    int32 utf16_len;
    int32 nfkc_len;
    int32 lower_len;
    int32 utf16_cap;
    int32 nfkc_cap;
    int32 lower_cap;
    bool ok;

    if (text_len < 0) {
        return false;
    }

    status = U_ZERO_ERROR;
    normalizer = (UNormalizer2 *)unorm2_getNFKCInstance(&status);
    if (!U_SUCCESS(status) || (normalizer == NULL)) {
        return false;
    }

    if (!ctc_unicode_norm_preflight_utf16_from_utf8(text,
                                                    text_len,
                                                    &utf16_len)) {
        return false;
    }

    utf16_cap = utf16_len + 1;
    utf16 = malloc2((int64)utf16_cap*SIZEOF(*utf16));
    nfkc = NULL;
    lower = NULL;
    ok = false;

    if (!ctc_unicode_norm_utf16_from_utf8(text,
                                          text_len,
                                          utf16,
                                          utf16_cap,
                                          &utf16_len)) {
        goto done;
    }
    if (!ctc_unicode_norm_preflight_nfkc(utf16,
                                         utf16_len,
                                         normalizer,
                                         &nfkc_len)) {
        goto done;
    }

    nfkc_cap = nfkc_len + 1;
    nfkc = malloc2((int64)nfkc_cap*SIZEOF(*nfkc));
    if (!ctc_unicode_norm_nfkc(utf16,
                               utf16_len,
                               normalizer,
                               nfkc,
                               nfkc_cap,
                               &nfkc_len)) {
        goto done;
    }
    if (!ctc_unicode_norm_preflight_lower(nfkc,
                                          nfkc_len,
                                          &lower_len)) {
        goto done;
    }

    lower_cap = lower_len + 1;
    lower = malloc2((int64)lower_cap*SIZEOF(*lower));
    if (!ctc_unicode_norm_lower(nfkc,
                                nfkc_len,
                                lower,
                                lower_cap,
                                &lower_len)) {
        goto done;
    }

    ok = ctc_unicode_norm_utf8_from_utf16(lower, lower_len, result);

done:
    if (lower) {
        free2(lower, (int64)lower_cap*SIZEOF(*lower));
    }
    if (nfkc) {
        free2(nfkc, (int64)nfkc_cap*SIZEOF(*nfkc));
    }
    free2(utf16, (int64)utf16_cap*SIZEOF(*utf16));

    return ok;
}
#endif

static bool
ctc_unicode_norm_nfkc_lower(
    char *text,
    int32 text_len,
    CtcUnicodeNormResult *result
) {
    if ((text == NULL) || (result == NULL)) {
        return false;
    }

    ctc_unicode_norm_result_destroy(result);
#if LRC_UNICODE_ENABLE_ICU
    if (ctc_unicode_norm_nfkc_lower_icu(text, text_len, result)) {
        return true;
    }
#endif

    return ctc_unicode_norm_copy_fallback(text, text_len, result);
}

#if TESTING_unicode_norm

#define CBASE_IMPLEMENT
#include "cbase.h"

static int32
unicode_norm_test_fail(char *name) {
    error2("unicode norm test failed: %s\n", name);

    return 1;
}

static int32
unicode_norm_test_fallback_or_icu_copy(void) {
    CtcUnicodeNormResult result;

    ctc_unicode_norm_result_init(&result);
    if (!ctc_unicode_norm_nfkc_lower(STRLIT("Hello"), &result)) {
        return unicode_norm_test_fail("normalize ascii");
    }

#if LRC_UNICODE_ENABLE_ICU
    ASSERT(ctc_unicode_norm_icu_available());
    ASSERT(result.used_icu);
    ASSERT(strequal2(result.text, result.text_len, STRLIT("hello")));
#else
    ASSERT(!ctc_unicode_norm_icu_available());
    ASSERT(!result.used_icu);
    ASSERT(strequal2(result.text, result.text_len, STRLIT("Hello")));
#endif

    ctc_unicode_norm_result_destroy(&result);

    return 0;
}

#if LRC_UNICODE_ENABLE_ICU
static int32
unicode_norm_test_icu_nfkc_lower(void) {
    CtcUnicodeNormResult result;

    ctc_unicode_norm_result_init(&result);
    if (!ctc_unicode_norm_nfkc_lower(STRLIT("ＡÉ"), &result)) {
        return unicode_norm_test_fail("normalize nfkc lower");
    }

    ASSERT(result.used_icu);
    ASSERT(strequal2(result.text, result.text_len, STRLIT("aé")));

    ctc_unicode_norm_result_destroy(&result);

    return 0;
}
#endif

int
main(void) {
    int32 status;

    status = 0;

    status += unicode_norm_test_fallback_or_icu_copy();
#if LRC_UNICODE_ENABLE_ICU
    status += unicode_norm_test_icu_nfkc_lower();
#endif

    return status;
}
#endif /* TESTING_unicode_norm */
