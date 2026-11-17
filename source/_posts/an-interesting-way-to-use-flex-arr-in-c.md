---
title: C 语言的柔性数组 Hacking Methods
date: 2024-08-08 12:00:00
categories:
- 技术
tags:
- c
---

看了 Tsoding 为 C3 编译器加的新功能的视频后有感，把自己的实验代码贴出来，非常有意思的柔性数组使用技巧

<!-- more -->

```c
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#define unused(var) (void)(var)

#if 0
#define D(...) fprintf(stderr, __VA_ARGS__)
#else
#define D(...)
#endif

struct dyn_arr{
    uint32_t size;
    uint32_t capacity;
    char data[];
};

typedef struct dyn_arr da_t;

static da_t *da_init(size_t ele_sz, size_t capacity)
{
    da_t *pDa = calloc(1, sizeof(da_t) + ele_sz * capacity);
    pDa->capacity = capacity;
    D("capacity: %zu\n", pDa->capacity);

    return pDa;
}

static void da_fill(da_t *da, char *data, size_t len)
{
    if (!da) return;
    if (da->size <= len)
    {
        (void)realloc(da, da->capacity * 2);
        da->capacity *= 2;
        D("capacity: %zu\n", pDa->capacity);
    }

    memcpy(da->data, data, len);
    da->size = len;
}

static uint32_t da_size(da_t *da)
{
    if (!da) return 0;
    return da->size;
}

static uint32_t da_capacity(da_t *da)
{
    if (!da) return 0;
    return da->capacity;
}

static uint32_t da_size_access_by_vec(const void *vec)
{
    /** fvck, this is an interesting hacking way
      * accessing by vec can be somehow described like that:
      *
      *     [ header, data[0], data[1], data[2], ... ]
      *                 ↑ the pointer when using (da_t.data)
      *
      *  and you index it by `-1`
      *
      *     [ header, data[0], data[1], data[2], ... ]
      *         ↑ the pointer when using ((da_t)(da_t.data))[-1]           
      *
      * so you just firstly cast the `vec` to `da_t`, and then index it by -1
      * basically it is just like knowing an address of a structure, and how to
      * find out other elements int the structure.
      */
    if (!vec) return 0;
    return ((da_t *)(vec))[-1].size;
}

static uint32_t da_capacity_access_by_vec(const void *vec)
{
    if (!vec) return 0;
    return ((da_t *)(vec))[-1].capacity;
}

static void da_release(da_t *da)
{
    if (!da) return;
    free(da);
    D("free'd\n");
}

int main(int argc, char *argv[])
{
    unused(argc); unused(argv);
    char s[] = "Hello World";
    char *vec;

    da_t *pDa = da_init(sizeof(char), sizeof(s));
    da_fill(pDa, s, sizeof(s));
    vec = pDa->data;

    printf("capacity of dyn_arr: %zu\n", da_capacity(pDa));
    printf("size of dyn_arr: %zu\n", da_size(pDa));
    printf("capacity of dyn_arr(access by vec): %zu\n", da_capacity_access_by_vec(vec));
    printf("size of dyn_arr(access by vec): %zu\n", da_size_access_by_vec(vec));

    da_release(pDa);

    return 0;
}
```
