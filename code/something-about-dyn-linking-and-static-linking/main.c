/** compile commands:
  *  - dynamic version:
  *     - preparation: 
  *          cc -shared -fPIC add.c -o add.so && cc -shared -fPIC dyn.c -o dyn.so
  *
  *     - no raylib call in main: 
  *          cc main.c -ldl -lraylib -DDYN_COMPILE -o dyn_main && ./dyn_main
  *
  *     - with raylib call in main: 
  *          cc main.c -ldl -lraylib -DDYN_COMPILE -DCALL_RAYLIB_FUNC -o dyn_main && ./dyn_main
  *
  *     - exposing functions in add.c to global: 
  *          cc main.c -ldl -lraylib -DDYN_COMPILE -DADD_TO_GLOBAL -o dyn_main && ./dyn_main
  *
  *     - forced to keep raylib even when no raylib function called: 
  *          cc main.c -DDYN_COMPILE -DNO_AS_NEED -o dyn_main -ldl -Wl,--no-as-need -lraylib && ./dyn_main
  *
  *  - static version: 
  *       cc main.c static.c -o sta_main && ./sta_main
  */


#include <stdio.h>
#include <stdlib.h>
#include <raylib.h>

#define errExit(msg)        \
    do {                    \
        perror(msg);        \
        exit(EXIT_FAILURE); \
    } while(0)

#ifdef DYN_COMPILE
    #include <dlfcn.h>

    #define FILE_ADD "./add.so"
    #define FILE_DYN "./dyn.so"
#else
    extern void test_call_static(void);
    void OpenURL(const char *url) {
        printf("[%s] url: %s opened\n", __FILE__, url);
    }
#endif

int main()
{
#ifdef CALL_RAYLIB_FUNC
   (void)IsWindowReady();
#endif

#ifndef DYN_COMPILE
    test_call_static();
#else
    // defined functions in add.c
    int (*add_int)(int, int);
    // defined functions in dyn.c
    void (*test_call_raylib)(void);
    void (*test_call_add)(void);

    void *handle = NULL;
#ifdef ADD_TO_GLOBAL
    handle = dlopen(FILE_ADD, RTLD_LAZY | RTLD_GLOBAL);
#else 
    handle = dlopen(FILE_ADD, RTLD_LAZY);
#endif

    void *dyn_handle = dlopen(FILE_DYN, RTLD_LAZY);

    if (!handle || !dyn_handle) {
        errExit(dlerror());
    }
    dlerror();    /* Clear any existing error */

    add_int = dlsym(handle, "add_int");
    test_call_raylib = dlsym(dyn_handle, "test_call_raylib");
    test_call_add = dlsym(dyn_handle, "test_call_add");

    printf("test_add_int: %d + %d = %d\n", 1, 2, add_int(1, 2));

#ifdef ADD_TO_GLOBAL
    test_call_add();
#endif

#if defined(CALL_RAYLIB_FUNC) || defined(NO_AS_NEEDED)
    test_call_raylib();
#endif

    if (dlclose(handle) != 0) {
        errExit("dlclose");
    }

    if (dlclose(dyn_handle) != 0) {
        errExit("dlclose");
    }
#endif
    exit(EXIT_SUCCESS);
}

