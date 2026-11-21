#include "complex.h"
#include "test_pub.h"

TEST_DECL(ComplexMultiply)
{
    Complex i1     = { 0, 1 };
    Complex i2     = { 0, 1 };
    Complex i3     = { 2, 1 };
    Complex i4     = { 3, 4 };
    Complex expect = { -1, 0 };
    Complex res;

    res = ComplexMultiply(i1, i2);
    TEST_CHECK(ComplexIsEqual(res, expect));

    res = ComplexMultiply(i2, i1);
    TEST_CHECK(ComplexIsEqual(res, expect));

    expect.x = 0; expect.y = -1;
    res = ComplexMultiply(res, i1);
    TEST_CHECK(ComplexIsEqual(res, expect));

    expect.x = 1; expect.y = 0;
    res = ComplexMultiply(res, i1);
    TEST_CHECK(ComplexIsEqual(res, expect));

    expect.x = 2; expect.y = 11;
    res = ComplexMultiply(i3, i4);
    TEST_CHECK(ComplexIsEqual(res, expect));
}

TEST_DECL(Complex2Matrix)
{
}

int main()
{
#define TEST_LIST(X) \
    X(Complex2Matrix)   \
    X(ComplexMultiply)

#define TEST_MAIN(name) RUN_TEST(name);
    TEST_LIST(TEST_MAIN)
#undef TEST_MAIN
    TEST_SUMMARY();
}

