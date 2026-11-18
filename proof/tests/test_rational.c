#include "trytr/algebra.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(test) do { if (!(test)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test); return false; } } while (false)

static bool checks_general_rational_identities_without_erasing_domains(void)
{
    const char *const cases[] = {
        "x/x=1", "0/x=0", "(x^2-1)/(x-1)=x+1", "1/x+1/y=(x+y)/(xy)",
        "\\frac{\\frac{x}{y}}{\\frac{z}{y}}=x/z", "1/(1/x)=x",
        "(x/x)^0=1", "0*(1/x)=0", "(x/x)-(x/x)=0", "x^-2=1/x^2",
        "(-x)^{-2}=1/x^2", "x/(-x)=-1", "(1/x)^{-1}=x",
        "\\frac{a^2-b^2}{a-b}=a+b", "(1/x)^2=1/(x*x)"
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const TrytrAlgebraResult result = trytr_algebra_check_rational(cases[i]);
        if (result.status != TRYTR_ALGEBRA_CONDITIONAL) fprintf(stderr, "%s: %s\n", cases[i], result.message);
        REQUIRE(result.status == TRYTR_ALGEBRA_CONDITIONAL);
        REQUIRE(result.condition_count > 0u);
        for (size_t j = 0u; j < result.condition_count; ++j) {
            REQUIRE(result.conditions[j].offset <= strlen(cases[i]));
            REQUIRE(result.conditions[j].length > 0u);
            REQUIRE(result.conditions[j].length <= strlen(cases[i]) - result.conditions[j].offset);
        }
    }
    const TrytrAlgebraResult cancelled = trytr_algebra_check_rational("(x^2-1)/(x-1)=x+1");
    REQUIRE(cancelled.condition_count == 1u);
    REQUIRE(cancelled.conditions[0].offset == 8u);
    REQUIRE(cancelled.conditions[0].length == 5u);
    return true;
}

static bool distinguishes_false_invalid_unsupported_and_unconditional_claims(void)
{
    const struct {const char *source; TrytrAlgebraStatus expected;} cases[] = {
        {"x/(x+1)=1",TRYTR_ALGEBRA_NOT_IDENTITY}, {"x/x=2",TRYTR_ALGEBRA_NOT_IDENTITY},
        {"1/x+1/y=(x-y)/(xy)",TRYTR_ALGEBRA_NOT_IDENTITY},
        {"0/(x-x)=0",TRYTR_ALGEBRA_INVALID}, {"1/(x/x-1)=0",TRYTR_ALGEBRA_INVALID},
        {"0*(1/(x-x))=0",TRYTR_ALGEBRA_INVALID}, {"0^-1=0",TRYTR_ALGEBRA_INVALID},
        {"\\frac{x}{}=0",TRYTR_ALGEBRA_INVALID}, {"x/=0",TRYTR_ALGEBRA_INVALID},
        {"x=y=z",TRYTR_ALGEBRA_INVALID}, {"x^{1/2}=x",TRYTR_ALGEBRA_INVALID},
        {"$x=x$$",TRYTR_ALGEBRA_INVALID}, {"$$x=x$",TRYTR_ALGEBRA_INVALID},
        {"sin(x)=sin(x)",TRYTR_ALGEBRA_UNSUPPORTED}, {"\\sqrt{x^2}=x",TRYTR_ALGEBRA_UNSUPPORTED},
        {"x^{13}=x",TRYTR_ALGEBRA_UNSUPPORTED}, {"1000000000^3=0",TRYTR_ALGEBRA_UNSUPPORTED},
        {"a+b+c+d+e+f+g+h+i=0",TRYTR_ALGEBRA_UNSUPPORTED},
        {"(a+b+c+d+e+f+g+h)^4=0",TRYTR_ALGEBRA_UNSUPPORTED},
        {"1/(x-x+1)=1",TRYTR_ALGEBRA_VERIFIED}, {"x^{-0}=1",TRYTR_ALGEBRA_VERIFIED},
        {"0.5x=x/2",TRYTR_ALGEBRA_VERIFIED}, {"$$x=x$$",TRYTR_ALGEBRA_VERIFIED}
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const TrytrAlgebraResult result = trytr_algebra_check_rational(cases[i].source);
        if (result.status != cases[i].expected) fprintf(stderr, "%s: %d %s\n", cases[i].source, result.status, result.message);
        REQUIRE(result.status == cases[i].expected);
    }
    REQUIRE(trytr_algebra_check_rational(NULL).status == TRYTR_ALGEBRA_INVALID);
    REQUIRE(trytr_algebra_check("x/x=1").status == TRYTR_ALGEBRA_UNSUPPORTED);
    return true;
}

static bool enforces_source_nesting_condition_and_work_limits(void)
{
    char *source = malloc(65538u);
    REQUIRE(source != NULL);
    memset(source, ' ', 65537u);
    source[65537u] = '\0';
    REQUIRE(trytr_algebra_check_rational(source).status == TRYTR_ALGEBRA_INVALID);
    memset(source, '(', 40u);
    source[40u] = 'x';
    memset(source + 41u, ')', 40u);
    strcpy(source + 81u, "=x");
    REQUIRE(trytr_algebra_check_rational(source).status == TRYTR_ALGEBRA_UNSUPPORTED);
    source[0] = '\0';
    for (size_t i = 0u; i < 33u; ++i) strcat(source, "1/x+");
    strcat(source, "0=0");
    TrytrAlgebraResult result = trytr_algebra_check_rational(source);
    REQUIRE(result.status == TRYTR_ALGEBRA_UNSUPPORTED);
    REQUIRE(strstr(result.message, "condition limit") != NULL);
    size_t length = 0u;
    const char term[] = "(a+b+c+d+e+f+g+h)^2+";
    for (size_t i = 0u; i < 2000u; ++i) {
        memcpy(source + length, term, sizeof(term) - 1u);
        length += sizeof(term) - 1u;
    }
    strcpy(source + length, "0=0");
    result = trytr_algebra_check_rational(source);
    REQUIRE(result.status == TRYTR_ALGEBRA_UNSUPPORTED);
    REQUIRE(strstr(result.message, "operation budget") != NULL);
    free(source);
    return true;
}

int main(void)
{
    if (!checks_general_rational_identities_without_erasing_domains() ||
        !distinguishes_false_invalid_unsupported_and_unconditional_claims() ||
        !enforces_source_nesting_condition_and_work_limits()) return 1;
    puts("rational identities, domain preservation, and adversarial bounds passed");
    return 0;
}
