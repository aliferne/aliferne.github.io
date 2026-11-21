#include <assert.h>
#include <stdio.h>

static int test_failures = 0;
static int test_assertions = 0;

#define ANSI_RED     "\033[31m"
#define ANSI_GREEN   "\033[32m"
#define ANSI_WHITE   "\033[37m"

#define TEST_DECL(name) \
    static void test_##name(void)

#define RUN_TEST(name) \
    do { \
        printf("[ %-9s ] %s...\n", "RUNNING", #name); \
        test_##name();                        \
        printf("[ %-9s ] %s!\n", "DONE", #name); \
    } while(0)

#define TEST_CHECK(condition)                                          \
    do {                                                               \
        test_assertions++;                                             \
        if (!(condition)) {                                            \
            test_failures++;                                           \
            fprintf(stderr,                                            \
                    ANSI_RED "[FAIL]"  ANSI_WHITE                      \
                    " %s:%d: %s\n",                                    \
                    __FILE__, __LINE__, #condition);                   \
        }                                                              \
    } while (0)

#define TEST_SUMMARY()                                                  \
    do {                                                                \
        printf( ANSI_GREEN "%d assertions, " ANSI_WHITE                 \
                ANSI_RED   "%d failures\n"   ANSI_WHITE,                \
                test_assertions, test_failures);                        \
    } while (0)

