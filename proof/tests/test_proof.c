#include "trytr/proof.h"

#include <stdio.h>
#include <string.h>

#define TEST_ASSERT(condition)                                                   \
    do {                                                                         \
        if (!(condition)) {                                                       \
            fprintf(stderr, "%s:%d: assertion failed: %s\n", __FILE__, __LINE__, \
                    #condition);                                                  \
            return false;                                                        \
        }                                                                        \
    } while (false)

static const char *one_plus_one =
    "theorem one_plus_one: add(succ(0), succ(0)) = succ(succ(0)) {\n"
    "  rewrite add_succ;\n"
    "  rewrite add_zero;\n"
    "}\n";

static bool accepts_peano_addition_proof(void)
{
    const TrytrProofResult result = trytr_proof_check(one_plus_one);
    TEST_ASSERT(result.valid);
    TEST_ASSERT(result.offset > 0U);
    return true;
}

static bool accepts_reflexive_equality(void)
{
    const TrytrProofResult result = trytr_proof_check("theorem reflexive: succ(0) = succ(0) {}\n");
    TEST_ASSERT(result.valid);
    return true;
}

static bool accepts_nested_recursive_addition(void)
{
    const TrytrProofResult result = trytr_proof_check(
        "theorem one_plus_two: add(succ(0), succ(succ(0))) = "
        "succ(succ(succ(0))) {"
        " rewrite add_succ; rewrite add_succ; rewrite add_zero; }"
    );
    TEST_ASSERT(result.valid);
    return true;
}

static bool rejects_false_goal(void)
{
    const TrytrProofResult result = trytr_proof_check(
        "theorem false_goal: add(succ(0), succ(0)) = succ(succ(succ(0))) {"
        " rewrite add_succ; rewrite add_zero; }"
    );
    TEST_ASSERT(!result.valid);
    TEST_ASSERT(result.message[0] != '\0');
    return true;
}

static bool rejects_non_matching_rewrite(void)
{
    const TrytrProofResult result = trytr_proof_check(
        "theorem bad_step: succ(0) = 0 { rewrite add_zero; }"
    );
    TEST_ASSERT(!result.valid);
    return true;
}

static bool rejects_invented_axiom(void)
{
    const TrytrProofResult result = trytr_proof_check(
        "theorem arbitrary: 0 = succ(0) { rewrite assume_anything; }"
    );
    TEST_ASSERT(!result.valid);
    return true;
}

static bool rejects_malformed_term(void)
{
    const TrytrProofResult result = trytr_proof_check(
        "theorem malformed: add(0) = 0 {}"
    );
    TEST_ASSERT(!result.valid);
    return true;
}

static bool rejects_trailing_theorem(void)
{
    const TrytrProofResult result = trytr_proof_check(
        "theorem first: 0 = 0 {} theorem second: 0 = 0 {}"
    );
    TEST_ASSERT(!result.valid);
    return true;
}

static bool rejects_null_input(void)
{
    const TrytrProofResult result = trytr_proof_check(NULL);
    TEST_ASSERT(!result.valid);
    TEST_ASSERT(result.message[0] != '\0');
    return true;
}

static bool rejects_excessive_nesting(void)
{
    char source[1024];
    const char *prefix = "theorem deep: ";
    size_t cursor = strlen(prefix);
    (void)memcpy(source, prefix, cursor);
    for (size_t index = 0U; index < 140U; ++index) {
        source[cursor++] = 's';
        source[cursor++] = 'u';
        source[cursor++] = 'c';
        source[cursor++] = 'c';
        source[cursor++] = '(';
    }
    source[cursor++] = '0';
    for (size_t index = 0U; index < 140U; ++index) {
        source[cursor++] = ')';
    }
    (void)memcpy(source + cursor, " = 0 {}", sizeof(" = 0 {}"));
    const TrytrProofResult result = trytr_proof_check(source);
    TEST_ASSERT(!result.valid);
    TEST_ASSERT(strstr(result.message, "nesting limit") != NULL);
    return true;
}

static bool rejects_input_over_limit(void)
{
    static char source[65538];
    (void)memset(source, ' ', sizeof(source) - 1U);
    source[sizeof(source) - 1U] = '\0';
    const TrytrProofResult result = trytr_proof_check(source);
    TEST_ASSERT(!result.valid);
    TEST_ASSERT(strstr(result.message, "64 KiB") != NULL);
    return true;
}

typedef bool (*TestFunction)(void);

typedef struct TestCase {
    const char *name;
    TestFunction function;
} TestCase;

int main(void)
{
    static const TestCase tests[] = {
        {"accepts Peano addition proof", accepts_peano_addition_proof},
        {"accepts reflexive equality", accepts_reflexive_equality},
        {"accepts nested recursive addition", accepts_nested_recursive_addition},
        {"rejects false theorem goal", rejects_false_goal},
        {"rejects non-matching rewrite", rejects_non_matching_rewrite},
        {"rejects invented axiom", rejects_invented_axiom},
        {"rejects malformed term", rejects_malformed_term},
        {"rejects trailing theorem", rejects_trailing_theorem},
        {"rejects null input", rejects_null_input},
        {"rejects excessive nesting", rejects_excessive_nesting},
        {"rejects input over limit", rejects_input_over_limit},
    };
    const size_t count = sizeof(tests) / sizeof(tests[0]);
    for (size_t index = 0U; index < count; ++index) {
        if (!tests[index].function()) {
            fprintf(stderr, "[FAIL] %s\n", tests[index].name);
            return 1;
        }
    }
    printf("%zu proof tests passed\n", count);
    return 0;
}
