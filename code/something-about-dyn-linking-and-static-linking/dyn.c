#include <stdio.h>
#include <raylib.h>

void test_call_raylib(void)
{
    // actually you cannot compile without including stdio.h and math.h
    // since you need a declaration
    printf("[%s] (test_call_raylib) called\n", __FILE__);
    OpenURL("https://www.baidu.com/");
}

// external decl needed
extern int add_int(int a, int b);

void test_call_add(void) 
{
    int a = 1, b = 3;
    printf("[%s] (test_call_add) called\n"
           "%d + %d = %d\n", __FILE__, a, b, add_int(a, b));
}

