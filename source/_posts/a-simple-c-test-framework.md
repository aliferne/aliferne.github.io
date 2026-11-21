---
title: 一个简易的 C 语言测试框架
date: 2024-11-22 12:00:00
categories:
- 技术
tags:
- c
- test
---

记录一下简单折腾出来的一个 C 语言测试框架, 用到了 `X-Macro` 的技巧:

- [test_template.c.in](/code/a-simple-c-test-framework/test_template.c.in)
- [test_pub.h](/code/a-simple-c-test-framework/test_pub.h)

下面是一个实例:

- [test_complex.c](/code/a-simple-c-test-framework/test_complex.c)

只要借助 `gcc <filename>.c -E <output>.i`, 就可以快速查看到底编译成什么了, `X-Macro` 这个技巧还是很方便的

