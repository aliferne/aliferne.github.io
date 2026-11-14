---
title: "动态链接与静态链接二三事"
date: 2025-04-23 12:00:00
categories:
- 技术
tags:
- c
- 链接
---

# 简介

主要是想要搞清 gcc 链接的一些东西，文章偏实验性质，不太会解释太多东西，想要了解具体理论的话推荐如下两本书：

- CSAPP 第七章
- 程序员的自我修养：链接、装载与库

# 实验代码

先简单看看实验代码，本次会用到的实验代码如下：

- [main.c][main.c]: 主文件
- [add.c][add.c]: 定义了 `add` 函数（动态链接）
- [dyn.c][dyn.c]: 要变成动态链接库的文件
- [static.c][static.c]: 要静态链接的文件

本次实验依赖 `raylib`

# 实验

## 一、静态链接

运行 `make static`:

```bash
> make static
cc main.c static.c -o sta_main 
------------------------ sta_main ------------------------
[static.c] (test_call_static) called
[main.c] url: https://www.baidu.com/ opened
```

此时链接程序会将两个文件链接在一起，没有动态链接符号(.dymsym)，符号表里面是：

```bash
> readelf --syms sta_main | grep OpenURL # 查看符号表
    25: 0000000000001149    52 FUNC    GLOBAL DEFAULT   14 OpenURL # 表示是全局的函数

> readelf --dym-syms sta_main | grep OpenURL # 查看动态链接的符号表

```

也就是说此时对 `OpenURL` 的引用来自于两个程序静态链接之后共同形成的符号是默认的符号表，只要在源代码中正确声明其他源文件的函数，就可以调用

## 二、在 main.c 中不调用 Raylib 函数的动态链接

此时需要用到 Makefile 中对于 `prepare` 的定义，我们需要提前使用 `-fPIC -shared` 来编译动态库。
`-fPIC` 的作用是生成位置无关的代码（动态链接强制要求必须带上这个标志位，位置无关代码指可以加载而不需要重定位的代码）

当然，我加入各个 `dyn*` 可执行文件的编译的依赖中了，因此让我们直接：

```bash
> make dyn_no_rb_call
cc add.c -shared -fPIC -o add.so && cc dyn.c -shared -fPIC -o dyn.so
cc main.c -ldl -lraylib -DDYN_COMPILE -o dyn_no_rb_main
------------------------ dyn_no_rb_call ------------------------
test_add_int: 1 + 2 = 3
```

此时是只执行了 `test_add_int`，不过只是这么实验的话其实还是挺没意思的，让我们修改一下：

```c
// #if defined(CALL_RAYLIB_FUNC) || defined(NO_AS_NEEDED)
#if 1
    test_call_raylib();
#endif
```

此时就会变成：

```bash
> make dyn_no_rb_call 2>&1
cc add.c -shared -fPIC -o add.so && cc dyn.c -shared -fPIC -o dyn.so
cc main.c -ldl -lraylib -DDYN_COMPILE -o dyn_no_rb_main
------------------------ dyn_no_rb_call ------------------------
./dyn_no_rb_main: symbol lookup error: ./dyn.so: undefined symbol: OpenURL
make: *** [Makefile:29：dyn_no_rb_call] 错误 127
```

可以看到运行错误了，提示是没有找到符号，因此让我们来看看：

```bash
> readelf --syms dyn_no_rb_main | grep OpenURL # 查看符号表

> readelf --dyn-syms dyn_no_rb_main | grep OpenURL # 查看动态链接符号表

```

可以看到都是空输出，也就是说确实没有符号，这是为什么？
因为我们并没有在 `main.c` 中使用 raylib 的函数，因此链接器判断不需要 `-lraylib` 了，没有引用符号

需要注意，如果你去查看的话，会发现只有 `main` 函数里面调用的，由其他动态链接库提供的函数才会被标记到动态链接符号表里面。
**因此我们接下来应该看的是有没有对我们需要的动态链接库的引用**，如果有，那么我们自己写的程序相关部分自然就可以调用

可以借助：`readelf -d <executable> | grep NEEDED` 来查看是否依赖某个动态链接库

因此我们来看看这个程序：

```bash
> readelf -d dyn_no_rb_main | grep NEEDED
 0x0000000000000001 (NEEDED)             共享库：[libc.so.6]
```

可以看到只引用了默认的 glibc， 没有 raylib，因此自然会提示找不到符号

## 三、在 main.c 中调用 Raylib 函数的动态链接

将上面的 `#if 1` 还原成原来的代码，然后：

```bash
> make dyn_with_rb_call
cc add.c -shared -fPIC -o add.so && cc dyn.c -shared -fPIC -o dyn.so
cc main.c -ldl -lraylib -DDYN_COMPILE -DCALL_RAYLIB_FUNC -o dyn_with_rb_main
------------------------ dyn_with_rb_call ------------------------
test_add_int: 1 + 2 = 3
[dyn.c] (test_call_raylib) called
```

此时 `OpenURL` 发力，用我的默认浏览器打开了百度，说明应该链接到了 raylib：

```bash
> readelf -d dyn_with_rb_main | grep NEEDED
 0x0000000000000001 (NEEDED)             共享库：[libraylib.so.600]
 0x0000000000000001 (NEEDED)             共享库：[libc.so.6]
```

可以看到此时就有 raylib 了

## 四、强行保住对 raylib 的链接

```bash
> make dyn_no_as_need
cc add.c -shared -fPIC -o add.so && cc dyn.c -shared -fPIC -o dyn.so
cc main.c -DDYN_COMPILE -DNO_AS_NEEDED -o dyn_no_as_need_main -Wl,--no-as-needed -ldl -lraylib
------------------------ dyn_no_as_need ------------------------
test_add_int: 1 + 2 = 3
[dyn.c] (test_call_raylib) called
```

```bash
> readelf -d dyn_no_as_need_main | grep NEEDED
 0x0000000000000001 (NEEDED)             共享库：[libraylib.so.600]
 0x0000000000000001 (NEEDED)             共享库：[libc.so.6]
```

## 五、将共享库符号开放给别的库

同理，做如下修改

```c
#if 1
// #ifdef ADD_TO_GLOBAL
      test_call_add();
#endif
```

然后我们分别使用 `make dyn_to_global` 和其他选项来调用：

```bash
> make dyn_to_gbl
cc add.c -shared -fPIC -o add.so && cc dyn.c -shared -fPIC -o dyn.so
cc main.c -ldl -lraylib -DDYN_COMPILE -DADD_TO_GLOBAL -o dyn_to_gbl_main
------------------------ dyn_to_gbl ------------------------
test_add_int: 1 + 2 = 3
[dyn.c] (test_call_add) called
1 + 3 = 4

> make dyn_no_rb_call
cc add.c -shared -fPIC -o add.so && cc dyn.c -shared -fPIC -o dyn.so
cc main.c -ldl -lraylib -DDYN_COMPILE -o dyn_no_rb_main
------------------------ dyn_no_rb_call ------------------------
test_add_int: 1 + 2 = 3
./dyn_no_rb_main: symbol lookup error: ./dyn.so: undefined symbol: add_int
make: *** [Makefile:29：dyn_no_rb_call] 错误 127
```

因此 `RTLD_GLOBAL` 标志位可以将 `add.so` 暴露给 `dyn.so` 的函数使用，但是这要求 `add.c` 内的函数不可以是 `static`，否则依然没办法调用（即 使用 `readelf` 查看 `add.so` 对应函数的符号表，要求必须是 `GLOBAL`）

# 总结

1. 静态链接会将所有内容打包进入一个可执行文件，文件之间能否互相调用各自的函数取决于全局符号表是不是有这个函数，以及是否在源文件中正确声明函数
2. 动态链接需要查看是否存在对库的依赖，以判断是否有调用该库
3. 默认情况下 gcc 会删除没有被调用的动态库的链接
4. 如果需要将一个库共享给其他库调用，需要在 `dlopen` 时使用特殊的标志位

编译不通过时，见到 `ld` 的问题就可以怀疑有没有正确包含头文件（即有没有正确的函数声明），遇到 `dlopen` 的问题就要查看是否真的链接到了动态库

---

[main.c]: /code/something-about-dyn-linking-and-static-linking/main.c
[add.c]: /code/something-about-dyn-linking-and-static-linking/add.c
[dyn.c]: /code/something-about-dyn-linking-and-static-linking/dyn.c
[static.c]: /code/something-about-dyn-linking-and-static-linking/static.c
[Makefile]: /code/something-about-dyn-linking-and-static-linking/Makefile

