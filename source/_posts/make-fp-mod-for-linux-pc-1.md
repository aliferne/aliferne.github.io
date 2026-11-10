---
title: "为 Linux PC 自制指纹验证功能 (上)"
date: 2026-11-10 22:00:00
categories:
- 技术
tags:
- c
- linux
- pam
---

副标题：指纹模块、Linux 系统编程与 PAM 模块开发二三事

本文是第一章：从串口到协议

# 前言

这个项目有点个人小玩具的色彩（笑），我的电脑是天选 5 Pro, 没有指纹识别模块，而我自己又主要用 Debian, 当时是和朋友去福州旅游，在闽都 THO 晚上的小型演唱会现场听 DJ 打碟感到无聊，然后思绪就开始发散，**想着说每次解锁电脑都要输入密码，实在是太麻烦了，因此想搞个指纹模块来玩**，于是就有了这篇博客。当然可能有人会觉得我闲得慌，也确实是闲得慌，我要是不折腾，你们看什么啊？ ~~是啊，看什么?~~

注意上面的需求，因为这个需求后面给我扩展了一下，变成了接入任意 PAM 服务，也导致了一些架构上的调整，我会在第二篇文章讲述自己的 “解题思路” 和踩的一些坑，同时我也想在第二篇讲讲我自己对 AI Coding 的一些想法。

指纹模块相关代码已经[开源][项目开源链接]， 主要逻辑部分基本完全古法编程，而审(zhao)查(cha)工作主要由 DeepSeek V4.1 Flash 负责，**这篇博客也是纯手敲**。

这个需求我个人感觉挺简单的，应该可以直接让 v4.1 vibe 一个出来，但是我是当 recreational programming 来玩的 (幻视 tsoding)，所以没有完全 AI 开发，但却没想到这玩意写了我快一个月……（虽然我每天只有大约两三个小时投入开发就是了）。

这里需要提一嘴，我是 Linux 系统编程的初学者，如下文有误，还请不吝赐教。

下面是指纹模块解锁电脑的演示视频：

<!-- more -->

{% raw %}
<div style="position: relative; width: 100%; height: 0; padding-bottom: 75%;">
    <iframe src="/images/make-fp-mod-for-linux-pc/fp-mod-unlock-demo.mp4" 
        scrolling="no" border="0" frameborder="no" framespacing="0" allowfullscreen="true" 
        style="position: absolute; width: 100%; height: 100%; Left: 0; top: 0;" >
    </iframe>
</div>
{% endraw %}

# 项目架构

这里的项目架构，我会先提到一堆名词和概念，不过这些概念会随着博客的深入被逐一解决，因此不必担心。

首先是硬件的调研，参考 deepseek 和 pdd 上的价格，最后决定使用 HLK 的 ZW111 + 常见的 CH340C USB 转 TTL 作为指纹模组，大约三十元，而市面上常见的 Windows Hello 大概是四五十到一百不等，图省事的话直接买 Windows Hello 就行，不过我用 Linux, 也想学下 Linux 系统编程等知识，于是就开始折腾起来了。

那么整个项目架构分为两部分，对应文章也是两章（第三章是讲与 AI 的合作的），

1. 从串口到协议: [Linux 串口编程][serial.c] -> [ZW111 模块通信(Linux 系统编程)][protocol.c] -> [命令行程序][cli.c]
2. PAM 模块与应用认证: [ZW111 模块通信(Linux 系统编程)][protocol.c] -> [PAM 模块联通][pam_fprint_zw111.c] -> 客制化配置文件以对接 PAM 和应用程序

而具体的，我们又会涉及到各个模块设计的一些点，这些我就不放在这里说了，整体的项目架构要解决的只有三件事：
如何与 ZW111 模块通信; 如何将 ZW111 模块串入 PAM 模块; 如何将自定义 PAM 模块对接到需要认证系统的程序中

# 串口的特点

虽然这可能有些马后炮，但是这些都是在我写代码实际实践的过程中逐渐认识到的，这里必须要先提前放出来：

1. 串口是数据的管道，而不是文件，数据一旦流走就不会再回来
2. 串口应当收发纯粹二进制流，不要对数据做任何处理
3. 串口文件是一种独占资源，同一时间段内可以允许多个进程/线程读，但绝不允许多个进程/线程写
4. 串口可能断连，因此需要考虑读写数据的稳定性

特别的，对于指纹模组这种一问一/多答的设备，同一时间段只能有一个进程/线程读写.

# Linux 串口编程与文件 IO

在正式进入主题之前，我们需要先了解一下什么是系统调用：

系统调用 (Syscall) 是受控的内核入口，借用这一机制，一个程序可以请求内核以自己的名义去执行某些动作，它 **在编程角度上来说和调用 C 语言的 API 并无区别**，但在底层角度，系统调用会涉及到处理器的用户态到核心态的切换、中断机制和汇编等内容，这些内容我们在这里并不是特别关心，但还是需要大概知道一下。

对于 Linux C 系统编程，这些 API **大部分都会在调用失败时返回 -1 并设置对应的 errno 值**，我们可以通过 `strerrno(errno)` 来将错误码转字符串，如果有些系统调用并不遵循该原则，我会特别说明。Linux Syscalls 可以通过 `man syscalls` 来查看，我们本次要用到的系统调用主要是 `open`, `write`, `read` 等。

## 串口设备的打开和基础配置

得益于 Linux 一切皆文件的设计，串口在 Linux 的表现形式为名为 `/dev/ttyXn` 的一个文件。而由于串口的一些特殊属性（波特率、奇偶校验等）， Linux 还开放了一些 API 用于设置串口属性，这些 API 主要在 `termios.h` 里面。 因此串口编程实际上要做的事情就是：打开串口文件 -> 配置串口属性[可选, 默认 9600, 8n1, 无流控] -> 读写串口文件 -> 关闭串口文件。

因此我们先来打开串口文件：

- `int open(const char *pathname, int flags);`

这个函数我相信不需要过多的介绍，`pathname` 就是文件路径，而 `flags` 就是打开文件的标志位。
我们需要用到的 `flag`: `O_RDWR`, `O_NOCTTY`, `O_NONBLOCK`(非阻塞用于实验，实际上我们需要阻塞)

我们需要读写串口，因此需要使用 `O_RDWR` 打开串口文件，而由于 Linux 会将串口抽象为 tty 设备，也就意味着默认情况下，系统可能会把串口当作一个控制终端来用，因此就可能能够发送一些特殊的控制字符，比如 `^C`，由于实际我们去使用的时候并不会发送这些特殊控制字符，因此需要借助 `O_NOCTTY` 关闭这一选项。

`O_NONBLOCK` 从字面意思上来理解就是不要阻塞，即以非阻塞的形式打开文件，这个选项的作用我会放到下面讲。

然后就轮到配置串口了：

比较基础的，如设置波特率这些的代码比较简单，写起来也很无聊，因此我是直接抄的 《嵌入式 Linux 开发教程》 里面的代码，但有些配置选项则值得说道说道：

如果说上面的选项指示系统如何打开串口文件，那么下面的选项就指示我们系统要如何与串口通信，我们期望收发纯粹的二进制数据，对于输入，由于数据手册并没要求使用流控，因此我们把软件流控关掉：

```c
opt.c_iflag &= ~(IXON | IXOFF | IXANY);
```

而对于输出显示之类的，什么 `<CR><LF>`, `<LF>` 之类的都是没有必要的字符，我们并不需要，对应的也就是关掉输出处理（使用原始输出）、关掉规范模式和关掉回显等：

```c
opt.c_oflag &= ~OPOST; // 使用原始输出
// 关掉规范模式，回显，终端信号等
opt.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
```

## 串口配置中的 `vtime` & `vmin` 以及串口通信的设计取舍

然后我们就来到了 `struct termios->c_cc` 的配置选项了，其他的选项我们并不关心，我们只需要看 `vtime` & `vmin` 即可。为什么要看这两个选项呢？这就要说一下串口和这个指纹模组的特殊地方了。

串口文件和存储在磁盘中的普通文件不同，对于普通的文件，你打开之后要读多少、写多少，你/代码心理都有数，**但是**串口是一种通信设备，我们大部分时候可能都是开着这个设备等数据，只有那么一小段时间设备突然发送一段数据流过来，**而且**通信设备是可能断连的，这就引出了两个代码设计上的思考：

1. 我们是要等到海枯石烂还是到点就走

- 有时需要死等，有时又需要超时避免卡死？

2. 如果串口断开了怎么办？

- 检测是否断开，发现断开就直接退出？还是直到下次重连？

我们这里的实验代码参考 [rd_nb.c][rd_nb.c]

对于第一个权衡点，这时就要提到之前说的 `O_NONBLOCK` 标志位了：
串口在非阻塞读的情况下，不论有无数据，都会立即返回成功，所以你会看到非常抽象的情况：

```bash
19:35:34 DEBUG ./rd_nb.c:23: write 12 bytes
write:        EF 01 FF FF FF FF 01 00 03 35 00 39
19:35:34 DEBUG ./rd_nb.c:22: current errno: 0 => Success
19:35:34 ERROR ./rd_nb.c:37: read failed: Success # 读取失败但是成功，这是因为 read 直接返回 0
```

非阻塞读的配置是一种 **异步** 模式，我们并不想等待结果，只要调用了就直接返回 ~~是一种 “你给路大油” 的思维~~，而下面的阻塞读则是 **同步** 模式，只有返回结果才会接着往下执行。

**因此如果设置了阻塞，那么就需要考虑是彻底阻塞还是等到不再满足条件为止**，所以此时对串口的读操作状态就会**取决于** `vtime` 和 `vmin` 这两个变量，前者表示单次读取的最大等待时间(单位 0.1s, 等待时间范围为 0 ~ 25.5s，对应传入 0 ~ 255)，后者表示单次读取需要接收到的最少字节数， 这两个变量有点类似于 `timeout` 的性质，设置的值有如下四种情况：

1. `vtime = 0, vmin = 0`: 此时对应不阻塞，不论如何都会直接返回，一般来说都不建议这种方式
2. `vtime > 0, vmin = 0`: 此时会等待最大 vtime 的时间，只要有数据就会返回
3. `vtime = 0, vmin > 0`: 此时要求至少收集到 vmin 个字节后才返回，因此少于 vmin 个字节的行为是未定义的
4. `vtime > 0, vmin > 0`: 此时要求只要拿到 vmin 个字节，或者到了 vtime 要求的时间， read 就会成功返回

需要注意由于 `vtime`, `vmin` 和 `O_NONBLOCK` 是矛盾的，因此如果设置 **非阻塞** 读，这两个变量就必须要是 0, 不然会导致读越界的 SegFault 问题(我没去琢磨源码，如果有知道原因的大佬还请不吝赐教)。篇幅原因我们只需要了解上面这些内容就足够了，如果想要了解更多东西，可以参考[这篇文章][vtime & vmin]

对于第二点，实际上我们有一个很好用的检测设备是否断连的方法，我们只需要借助 `tcgetattr` 即可，这个函数在尝试获取串口配置失败时会返回 -1, 并设置 errno 为一些特定的值，如果 `errno == EIO`，那么一般就是串口断连了。严谨一些的话可以用 `poll`，但是我没用，这里会在后面解释 TODO:

这里可能会有人问，为什么不直接判断 `read` 后的 `errno`，问题在于第一次 `read` 返回的读取字节数为 0, 此时代表的实际上是 `read` 认为读到 EOF 了，这是成功的标志，因此不会设置 `errno`，你就无法通过这个来判断，因此要判断设备是否被拔出就必须再操作一次串口文件，因此就需要调用 `tcgetattr`，这是比较简单的：

- 不调用 `tcgetattr` 的情况（那个 `serial_is_dev_hangup()` 函数是我的一个简易封装，就是我在讲解第二点一开始时提到的方法）:

![check errno when tcgetattr not called][check errno when tcgetattr not called]

- 调用 `tcgetattr` 的情况，其中 gdb 终端那一行表示显示调用 errno 对应的函数并解引用返回的地址后得到的返回值，可以看到返回值为 5(`EIO`):

![check errno when tcgetattr called][check errno when tcgetattr called]

## 串口多进程的数据竞争

我一开始说了，由于我自己犯贱改需求，所以本来只是用于解锁 KDE 锁屏的，并不需要考虑多进程问题，但扩展需求之后就必须要考虑多进程情形了。
比如最简单的，一个终端跑 `sudo su`， 一个终端跑 `sudo apt update && sudo apt upgrade -y`，这种情况下，如果你使用指纹模块，你要如何确保不会导致数据竞争？万一两边此时都要求验证了，然后同时对设备发送 `auto_identify`，万一有一边发送了 `cancel`，那么另一边还验证不验证…这些问题很麻烦，但都可以通过**最朴素的加锁方式**来实现。

当然，首先看下加锁与不加锁的区别，下面是不加锁的：

{% raw %}
<div style="position: relative; width: 100%; height: 0; padding-bottom: 75%;">
    <iframe src="/images/make-fp-mod-for-linux-pc/fp-zw111-test-without-lock.mp4"
        scrolling="no" border="0" frameborder="no" framespacing="0" allowfullscreen="true" 
        style="position: absolute; width: 100%; height: 100%; Left: 0; top: 0;" >
    </iframe>
</div>
{% endraw %}

而这个是加锁的：

{% raw %}
<div style="position: relative; width: 100%; height: 0; padding-bottom: 75%;">
    <iframe src="/images/make-fp-mod-for-linux-pc/fp-zw111-test-with-lock.mp4"
        scrolling="no" border="0" frameborder="no" framespacing="0" allowfullscreen="true" 
        style="position: absolute; width: 100%; height: 100%; Left: 0; top: 0;" >
    </iframe>
</div>
{% endraw %}

看完对比之后你就会理解为什么要加锁了，那么问题来了：怎么加锁？加什么锁？

# Linux 中的文件锁

## 并发编程是什么

多线程/多进程这种并发编程是一个很有趣但也很恶心的话题，原因在于 **解决多个线程/进程之间的竞争是一件很棘手的事情**。假设对于一个变量 a，如果不去做同步，那么只是读的话还好，但如果是写入操作，我们都知道修改变量在汇编层面上分为三步——读取、修改、写入。假如进程 A 把 a 改成 1, 但尚未写入，进程 B 就进来插手准备修改了，并成功把 a 改成 2, 此时进程 A 恢复，将 a = 1 写入内存中，此时 a 又变成 1 了，但是下一次再运行这段程序，这次反而是进程 B 写到一半被进程 A 插手成功了，那么 a 应该是 1 还是 2 呢？因此我们需要同步协调多个进程/线程对一段资源的访问顺序，技术上来说有很多手段，例如信号、信号量、锁等等，同样的，对于文件来说，我们也有文件锁，用于同步这些线程/进程对该文件资源的访问顺序。

## 劝告锁与强制锁

Linux 的文件锁分为劝告锁和 [强制锁][mandatory locking] 两种，**劝告锁是君子协定，你要不要遵守都行，强制锁是强制你遵守的**，不过值得注意的是，强制锁似乎已经[被移除][the removal of mandatory lock]出 Linux 内核了，我不很确定，因为我也找到了一个说需要通过 `-o mand` 重新挂载文件系统来获取强制锁支持的[文档][the mand option]，不论如何，强制锁总归是很麻烦的， kernel.org 自己都不推荐，**因此我们肯定是使用劝告锁的**。

## 互斥锁与共享锁

考虑完了劝告锁和强制锁的区别之后，我们再来看看是应该使用共享锁还是互斥锁：

- 共享锁允许多个进程并发读，但是不允许并发写——一份笔记可以多个人看，但是不能多个人写，不然就会乱
- 互斥锁要求同一个时间段内只能有一个进程/线程访问该资源——同一个时间内只能有一个人在上厕所，其他人都得在外面等

我的想法是 **使用互斥锁**，共享锁在这里没有意义，原因如下：

1. 共享锁允许所有其他共享锁获得锁，但是不允许互斥锁获得锁，因此只有存在互斥锁的情况下，共享锁才有意义
2. 串口应当是独占的文件资源，一个进程有完整的读写要求，即使是共享锁，也无法确保代码里就不会进行写操作，这种情况下依然等价于没加锁
3. 这个设备是问答式的，除非你其他进程只打开文件并监听，并且有一个主进程发起请求（比如解锁），这种情况下才有可能让所有进程同时读到验证通过的消息

可以看到上面的三点在这个程序里面要做起来实在是很麻烦，因此索性直接互斥锁得了。

一些题外话：在写这篇文章的时候我手动改了一下已经写好的代码做了点实验，事实证明我是对的，因为使用共享锁，后启动的进程直接卡死了，无论加不加 `LOCK_NB` 选项防止阻塞都一样:

{% raw %}
<div style="position: relative; width: 100%; height: 0; padding-bottom: 75%;">
    <iframe src="/images/make-fp-mod-for-linux-pc/useless-shared-lock.mp4"
        scrolling="no" border="0" frameborder="no" framespacing="0" allowfullscreen="true" 
        style="position: absolute; width: 100%; height: 100%; Left: 0; top: 0;" >
    </iframe>
</div>
{% endraw %}

## 选择文件锁 API

那么文件锁有三种选择：`flock`, `fcntl`, `lockf`，其中 `lockf` 是基于 `fcntl` 的封装，我们选择的是 `lockf`。
这里选择 `lockf` 其实纯粹是惯性原因，我们简单讲一下这三个锁的区别（以下片段直接来自 TLPI 这本经典书籍）：

1. `flock` 只能对整个文件加锁，而 `fcntl`/`lockf` 可以部分加锁
2. 很多 NFS 实现不认 `flock` 的锁
3. `flock` 只能放劝告锁
4. `fcntl`/`lockf` 不会继承父进程的记录锁，从而父进程也就不会因为子进程的释放而失去这把锁
5. 记录锁在 `exec()` 中会得到保留
6. 一个进程中的所有线程会共享同一组记录锁

首先，我们一开始就决定要用劝告锁，所以第 3 点对我们而言不是缺点，然后，我们不会使用 `fork`/`exec`，第 4, 5 点对我们来说也没什么实际影响，其次，我们要考虑的是多进程的并发，而不是多线程的并发，因此第 6 点其实也没什么，因此看下来似乎只有 1 和 2 会影响我们的选择？但是第 1 点，我们一开始就说过了，串口文件是特殊的文件，它更像是一种通信管道，你无法 `lseek`，局部加锁也就无从谈起，因此似乎只有第 2 点才是影响因素了，但是我的电脑用的是 brtfs，不需要考虑 NFS。 **所以选择 `lockf` 纯粹只是惯性原因，我看它自由度比较高就选了（笑），`lockf` 默认就是互斥锁模式，不像 `fcntl` 要配一堆东西，省事了才选的它**，当然如果你要改成 `flock`，那也是一样的，没什么区别

需要注意，**我不打算让程序一直阻塞下去，因此只要失败了就会直接返回**，这也是上面视频中一个时刻始终不会出现两个 `sudo` 验证程序的原因，这里看你取舍，我觉得这样子挺好的就没配置阻塞，如果你愿意的话也可以把我的代码拿去稍作修改。

## 如何读/写，以及为何不使用 `poll`

读操作和写操作其实都是很简单的，无非就是下面的两个 API：

- `ssize_t write(int fd, const void *buf, size_t count);`
- `ssize_t read(int fd, void *buf, size_t count);`

但是要处理好串口的读和写是一件很麻烦的事情，原因在于上面提到的 [串口的特点](#串口的特点) ，设备可能断连，因此需要考虑部分读/部分写，但是如果你实际去看我的代码就会发现我其实并没有这么干，原因是因为 ~~我懒~~ 这里要读写的字节数量实际上比较少，我个人判断不太需要部分读/部分写。

我们一直使用的都是默认配置： 57.6kbps 波特率， 8 数据位， 1
停止位，无校验，对于最简单的一帧数据构成：包头 + 设备地址 + 包标识 + 包长度 + 指令 + 0 数据 +
校验和， 总字节长度：(1 + 8 + 1) * (2 + 4 + 1 + 2 + 1 + 2) = 120 bits, 因此一帧数据的发送/接收时长：0.120 / 57.6 = 0.002083 = 2ms 这么短的窗口期，设备比较难刚刚好就在收发数据时断连，因此我们更多要考虑的应该是断连后的重连问题。

至于为什么不用 `poll`，那就需要看看 `poll` 是做什么的了：

- `int poll(struct pollfd *fds, nfds_t nfds, int timeout);`

```linux man page
DESCRIPTION
       poll()  performs  a similar task to select(2): it waits for one of a set of file descriptors to become
       ready to perform I/O.  The Linux-specific epoll(7) API performs a similar task,  but  offers  features
       beyond those found in poll().
```

因此 `poll` 实际上是为了解决 IO 多路复用的问题而存在的，但我们这里只有单路 IO，因此如果你用 `poll`，那么只能说明你看上了它的如下两点：

1. `timeout` 超时机制：可以由 `vtime` & `vmin` 解决
2. 设备断连检测(`struct pollfd` 中的 `revents`)：可以由 `tcgetattr` & `errno` 解决

因此这里没理由用 `poll`。

## 解锁和关闭串口文件

解锁：
- `lockf(fd, F_ULOCK, 0);`

关闭串口文件：
- `close(fd);`

这里没什么要说的，就是检查下 `fd` 是合法的( >= 0 )，然后先解锁再关文件就行。

当然这是一个比较粗粒度的同步措施，在这种假设下我们认为一个进程直到结束之前都不应该释放串口资源给其他进程使用，如果想要一个细粒度的并发，那么就可以在这方面下手，当然对于验证程序来说也确实要一直验证直到结束才能释放串口资源，所以我选择了一个粗粒度的措施。

# ZW111 模块开发

- [产品规格][zw111 datasheets]
- [用户手册][zw111 user manual]

这部分不想过多赘述，基本上就是照着手册开发就行。

从需求的角度上来说我们需要开发的功能实际上只有：
- 注册指纹(`auto_enroll`)
- 验证指纹(`auto_identify`)

其余的比如 `read_sys_para` 和 `check_sensor` 之类的其实是衍生功能(所以我这里就没有对应的支持，比如 `read_sys_para` 读下来的数据我并没有去做解析)，比如指纹满了要删除点库存、取消本次自动验证/注册，等等，但是 `get_image` 是确实有必要的，我在开发过程中测试发现如果你不在指纹验证错误并要求重试时加入这个，并要求用户移开手指，那么指纹模组会直接开始第二次识别，也就是说可能会白白浪费很多重试次数。

代码见[这里][protocol.c]，此外基于这套代码，我让 AI 生成了一个[命令行程序][cli.c]，用于快速录入指纹等。

# 总结

最后我们再来梳理一下整体的流程：

- 打开串口文件 -> 确认文件是 tty 设备[可选] -> 给文件加锁 -> 配置串口选项 -> 返回文件描述符
- 在锁内对串口文件进行各种读写操作
- 检验 `fd` 是否合法 -> 解锁 -> 关闭文件

自此整个 Linux 串口和系统编程部分也就结束了，下一章节我们将聚焦于 PAM 模块的开发，以及与 KDE 锁屏 kscreenlocker 和 Root 验证程序 sudo 的联动，敬请期待！

---

[项目开源链接]: https://github.com/aliferne/fp-zw111-unlocker
[vtime & vmin]: http://unixwiz.net/techtips/termios-vmin-vtime.html
[mandatory locking]: https://www.kernel.org/doc/html/v5.14/filesystems/mandatory-locking.html
[the mand option]: https://www.kernel.org/doc/html/v5.14/filesystems/mandatory-locking.html#the-mand-mount-option
[the removal of mandatory lock]: https://lkml.iu.edu/hypermail/linux/kernel/2108.2/05805.html
[rd_nb.c]: https://github.com/aliferne/fp-zw111-unlocker/blob/main/test/wrrd-test/rd_nb.c
[serial.c]: https://github.com/aliferne/fp-zw111-unlocker/blob/main/src/serial.c
[protocol.c]: https://github.com/aliferne/fp-zw111-unlocker/blob/main/src/protocol.c
[cli.c]: https://github.com/aliferne/fp-zw111-unlocker/blob/main/src/cli.c
[pam_fprint_zw111.c]: https://github.com/aliferne/fp-zw111-unlocker/blob/main/src/pam_fprint_zw111.c
[zw111 datasheets]: https://github.com/aliferne/fp-zw111-unlocker/blob/main/docs/ZW111%E5%8D%8A%E5%AF%BC%E4%BD%93%E6%8C%87%E7%BA%B9%E5%A4%84%E7%90%86%E6%A8%A1%E5%9D%97%E8%A7%84%E6%A0%BC%E4%B9%A6V1.2.pdf
[zw111 user manual]: https://github.com/aliferne/fp-zw111-unlocker/blob/main/docs/%E6%8C%87%E7%BA%B9%E6%A8%A1%E7%BB%84%E4%BA%A7%E5%93%81%E7%94%A8%E6%88%B7%E6%89%8B%E5%86%8C_V1.5.1.pdf

[check errno when tcgetattr not called]: /images/make-fp-mod-for-linux-pc/check-errno-when-tcgetattr-not-called.png
[check errno when tcgetattr called]: /images/make-fp-mod-for-linux-pc/check-errno-when-tcgetattr-called.png

