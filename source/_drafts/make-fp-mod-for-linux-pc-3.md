---
title: "为 Linux PC 自制指纹验证功能 (下)"
date: 2026-11-14 22:00:00
categories:
- 技术
tags:
- c
- linux
- pam
- agent
---

副标题：指纹模块、Linux 系统编程与 PAM 模块开发二三事

本文是第三章：AI 协作二三事

前情提要：

- [上][make-fp-mod-for-linux-pc-1]
- [中][make-fp-mod-for-linux-pc-2]

<!-- more -->

---

相对来说，一些不重要的内容我会让 AI 去帮我处理。
我还是要强调，我是 Recreational Programming，不是互联网公司这些天在搞的自动化编程，**我很重视自己到底理解了没有，如果没有这种求知的欲望和花大半天从零到一搭建一个系统的成就感，我压根就不会编程**，所以 AI 在我这里只是生成 demo 和处理琐事/分支的小助手，而不是我只需要站在旁边简单看看有没有什么大问题的数控机床（其实某种程度上来说我感觉脑力劳动的社会化大生产革命就要到来了）。

一些开发 sudo 认证的小插曲:
我发现 `pamtester` 可以通过 `^C` 中断测试，但是 `sudo` 做不到，于是一开始以为是串口的配置问题——我没把串口当控制终端、禁止了 `^C` 等控制字符的处理，后来发现不是的，就算我这么配了之后， `sudo` 照样无法被中断，于是问 deepseek，找到了 [sudo_auth 的源代码][sudo_auth.c]，然后发现**除了** sudo 自己的密码验证以外， **所有和中断/退出有关的信号都被屏蔽了**，你大爷的……

```c
/* 看注释：
 * We treat authentication as a critical section and block
 * keyboard-generated signals such as SIGINT and SIGQUIT
 * which might otherwise interrupt a sleep(3).
 * They are temporarily unblocked by auth_getpass().
 */
sigemptyset(&mask);
sigaddset(&mask, SIGINT);
sigaddset(&mask, SIGQUIT);
(void) sigprocmask(SIG_BLOCK, &mask, &omask);
```

所以如果要在指纹验证失败的情况下要想快速 fallback 到密码上，就只能靠缩短等待时间 & 尝试次数来实现了

---

[make-fp-mod-for-linux-pc-1]: https://aliferne.github.io/2026/11/10/make-fp-mod-for-linux-pc-1/
[make-fp-mod-for-linux-pc-2]: TODO

[sudo_auth.c]: https://github.com/sudo-project/sudo/blob/main/plugins/sudoers/auth/sudo_auth.c

