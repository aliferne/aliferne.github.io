#include <stdio.h>

extern void OpenURL(const char *url);

void test_call_static(void)
{
    printf("[%s] (test_call_static) called\n", __FILE__);
    OpenURL("https://www.baidu.com/");
}

