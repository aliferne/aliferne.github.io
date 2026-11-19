---
title: "为 Linux PC 自制指纹验证功能 (中)"
date: 2026-11-18 22:00:00
categories:
- 技术
tags:
- c
- linux
- pam
---

副标题：指纹模块、Linux 系统编程与 PAM 模块开发二三事

本文是第二章：打通任督 `PAM` 脉

前情提要：

- [上][make-fp-mod-for-linux-pc-1]

本文分为主线和支线部分，主线主要是 PAM 模块的开发讲解、如何和 KDE 的 kscreenlocker 串起来、以及自己开发的 PAM 模块可能存在的一些安全问题，而支线我做了特别标记，这部分内容将向你们揭示 Linux-PAM 的部分原理、以及如何去定制一个 KDE 锁屏界面

<!-- more -->

# 零、注意

在你所看到的版本中， 我编写的 PAM 模块(应该)默认不启用 log.h 库的日志输出 (Makefile 相关编译选项)，此时你需要通过：

```bash
journalctl -f <path-to-executable>
```

来观察 `pam_syslog` 输出的日志，比如对于 `pamtester` 的测试，我们可以：

```bash
journalctl -f $(which pamtester)
```

如果你需要 log.h 的输出，不要 `pam_syslog`，那么可以在 Makefile 中取消对于 `DEFFLAGS += -DPAM_USE_LOG_H` 的注释

**如果，我是说如果，你真的要动手改 sudo 的 PAM 认证文件了，一定一定一定要留一个已经开了 root 的终端**

我有两次把 sudo 玩炸的经历，而我是用 `pkexec` 救回来的，我不知道你们有没有这程序，如果没有的话…

![sudo boomed][sudo boomed]

# 一、PAM 模块开发

网上简中博客的 PAM 模块相关资料以 [配置文件][pam module config file] 居多，并不涉及如何开发 `so` 动态库，因此本章将打破这一局面，根据 [一手资料][libpam-wiki] 再结合自己的理解写一篇模块开发教程。

## PAM 模块工作流程

PAM 应用程序指定对应的 service --> 应用程序发起 `auth` 请求 --> libpam 在 service 内部配置找到对应 so 文件/模块 --> `dlopen` 打开 so 文件并调用对应函数 --> 处理结果返回 libpam 并进行下一步处理 --> PAM 程序收到 API 的返回码并进行对应的处理

上面的工作流程将会在支线中展示一整套调用链，如果有兴趣的话可以跳转过去看看，不看也没什么影响

PAM 配置文件有一定的语法规则，由于配置文件的相关资料很多，这里只对需要用到的做简要说明。

推荐一手资料: https://kerrisk.com/linux/man-pages/man5/pam.d.5@@linux-pam.html

基本语法规则： `<type> <flag> <path> <args>`
比如这个例子： `auth required xxx.so`，就表示 authentication （用户身份验证） 要求使用 `xxx.so` 这个模块， `xxx.so` 需要实现一些 PAM 模块要求的函数，然后通过返回 `PAM_*` 状态码告知是验证通过还是失败

我们会用到的有 `auth`, `required` 和 `sufficient` 和一些特殊标志位

- `auth` 就是鉴权用的，验证你是不是对应的用户
- `required` 和另一个叫 `requisite` 的很像， `required` 表示调用所有需要的模块，即使有模块失败了也先不管，等整体调用完了再返回失败， 而另一个则是一失败就立马返回
- `sufficient` 表示只要验证通过就直接返回成功，如果失败则忽略并继续向下执行

由于 `sufficient` 有成功就直接返回的特性，这个其实就会有安全上的顾虑 (试想一下如果写的是 `auth sufficient pam_permit.so`， 那整个 PAM 就炸掉了)，我不是特别建议使用。

当然还有一些更加特殊的，比如 `[success=1]` 这样的控制位，这里表示如果当前模块成功就会跳过下一个模块的调用，细节可以问一下 AI，这里介绍这么多也就差不多了

## 开发 PAM 模块

在开发 PAM 模块之前，首先需要先安装 libpam：

```bash
# PAM 开发模块名称取决于发行版
sudo apt install libpam0g-dev -y
```

编写 [pam_auth_test.c][pam_auth_test.c] 文件，我们这里只作为测试用，需要注意永远不要明文将密钥写入代码中，比如下面用 ltrace 去看的：

```bash
> ltrace -e strcmp ./test_pam pam-auth-test ss ss 2>&1 | grep "pam_auth_test.so"
pam_auth_test.so->strcmp("ss", "test", 4)       = -1
```

一眼就知道密码是什么了

然后编译，注意需要编译成共享库：

```bash
# 这是共享库，因此不需要链接 pam 模块
gcc -fPIC -shared -o pam_auth_test.so pam_auth_test.c
```

这里关于不需要链接 PAM 模块的原因，以及使用 `-fPIC` 的原因，可以参考我之前写的 [这篇博客][dyn-linking-sta-linking] 以及讲解 fPIC 的[这篇博客][fPIC]，如果不想看的话，简单来说就是因为接下来的主程序 (`test_pam`) 会链接 PAM 模块，因此这个共享库可以找到 PAM 模块的函数，你想在这里就链接的话其实也没什么问题

接下来将 so 链接到对应的位置，我这里在 `/lib/x86_64-linux-gnu/security` 中

```bash
# 测完之后记得删掉
sudo ln $(pwd)/build/pam_auth_test.so /lib/x86_64-linux-gnu/security
```

我个人认为链接最好用硬链接，只要你的盘在同一个文件系统内就行，硬链接的好处在于两个文件都指向了同一 inode 且修改任何一个文件的内容，其他文件都会跟着更新，删除任意文件都不会导致已有的文件失效，除非全删光。

不过最后开发完成之后我会删除链接文件，复制过去，并改动一些东西，你往下看就会知道原因了，我也将这部分需要用到的命令写成了 `make install` (目前暂时不包含对 kscreenlocker 的定制内容)

然后编写服务文件

```bash
tee pam-auth-test << EOF
auth required pam_auth_test.so
account required pam_auth_test.so
EOF
```

最后链接这个服务文件

```bash
# 测完之后记得删掉
sudo ln $(pwd)/build/pam-auth-test /etc/pam.d
```

这样子通路就基本搭建完毕了，然后我们接下来自己写一个 [应用程序][test_pam.c]

然后编译：

```bash
# 注意链接 pam 库
gcc test_pam.c -o test_pam -lpam
```

接下来运行：

```bash
> ./test_pam pam-auth-test testusr testpwd
[pam_testauth] pam_sm_authenticate called
[test_pam] here
[test_pam] msg: Password:
[pam_testauth] got user='testusr' pass='testpwd'
[pam_testauth] MATCH -> PAM_SUCCESS
pam_authenticate -> 0 (Success)
```

但是我们测试还是会用更加成熟的 [pamtester][pamtester-doc] ，首先安装它

```bash
> sudo apt install pamtester -y
```

然后我们来演示下这个工具的用法：

```bash
> pamtester -v pam-auth-test testusr authenticate
pamtester: invoking pam_start(pam-auth-test, testusr, ...)
pamtester: performing operation - authenticate
[pam_testauth] pam_sm_authenticate called
Password:
[pam_testauth] got user='testusr' pass='testpwd'
[pam_testauth] MATCH -> PAM_SUCCESS
pamtester: successfully authenticated
```

如果你对于 PAM 应用和模块之间的通信机制有兴趣的话，可以参考 [支线](#四支线) 中的讲解，这部分内容和自定义 kscreenlocker 的 UI 界面原理相关，如果你不在乎支线，那么可以忽略，但是如果你想要自定义一下的话，那我还是推荐你看一下的

# 二、为其他应用打通 “任督 PAM 脉”

## `udev` 固定设备名称

我们之前一直都在用 `/dev/ttyUSB0` 这样的来访问设备，然而这样子访问是很不方便的，原因在于设备名称可能发生变化，因此我们必须要以什么手段固定设备名称，在 Linux 下我们可以通过 udev 实现两个目标：

1. 固定设备名称
2. 不论设备插入哪个 USB 口，都能够以相同名称访问

因此我们可以执行如下操作：

```bash
> udevadm info -a -n /dev/ttyUSB0


Udevadm info starts with the device specified by the devpath and then
walks up the chain of parent devices. It prints for every device
found, all possible attributes in the udev rules key format.
A rule to match, can be composed by the attributes of the device
and the attributes from one single parent device.

  looking at device '/devices/pci0000:00/0000:00:08.1/0000:06:00.4/usb3/3-1/3-1.2/3-1.2:1.0/ttyUSB0/tty/ttyUSB0':
    KERNEL=="ttyUSB0"
    SUBSYSTEM=="tty"
    DRIVER==""
    ATTR{power/async}=="disabled"
    ATTR{power/control}=="auto"
    ATTR{power/runtime_active_kids}=="0"
    ATTR{power/runtime_active_time}=="0"
    ATTR{power/runtime_enabled}=="disabled"
    ATTR{power/runtime_status}=="unsupported"
    ATTR{power/runtime_suspended_time}=="0"
    ATTR{power/runtime_usage}=="0"

  looking at parent device '/devices/pci0000:00/0000:00:08.1/0000:06:00.4/usb3/3-1/3-1.2/3-1.2:1.0/ttyUSB0':
    KERNELS=="ttyUSB0"
    SUBSYSTEMS=="usb-serial"
    DRIVERS=="ch341-uart"
    ATTRS{port_number}=="0"
    ATTRS{power/async}=="enabled"
    ATTRS{power/control}=="auto"
    ATTRS{power/runtime_active_kids}=="0"
    ATTRS{power/runtime_active_time}=="0"
    ATTRS{power/runtime_enabled}=="disabled"
    ATTRS{power/runtime_status}=="unsupported"
    ATTRS{power/runtime_suspended_time}=="0"
    ATTRS{power/runtime_usage}=="0"

  looking at parent device '/devices/pci0000:00/0000:00:08.1/0000:06:00.4/usb3/3-1/3-1.2/3-1.2:1.0':
    KERNELS=="3-1.2:1.0"
    SUBSYSTEMS=="usb"               # 我们选择这里 
    DRIVERS=="ch341"                # 和这里
    ATTRS{authorized}=="1"
    ATTRS{bAlternateSetting}==" 0"
    ATTRS{bInterfaceClass}=="ff"
    ATTRS{bInterfaceNumber}=="00"
    ATTRS{bInterfaceProtocol}=="02"
    ATTRS{bInterfaceSubClass}=="01"
    ATTRS{bNumEndpoints}=="03"
    ATTRS{power/async}=="enabled"
    ATTRS{power/runtime_active_kids}=="0"
    ATTRS{power/runtime_enabled}=="enabled"
    ATTRS{power/runtime_status}=="suspended"
    ATTRS{power/runtime_usage}=="0"
    ...
```

然后在 `/etc/udev/rules.d/` 创建文件 `fprint_zw111.rules` 并写入：

```
SUBSYSTEMS=="usb", DRIVERS=="ch341", MODE="0660", SYMLINK+="FP_ZW111"
```

最后再 `sudo udevadm control --reload-rules && sudo udevadm trigger` 以使得规则生效

然后就会得到：

```bash
> ls -l /dev/FP_ZW111
lrwxrwxrwx 1 root root 7 11月18日 17:18 /dev/FP_ZW111 -> ttyUSB0
```

此时我们完成了固定命名的操作，可以更加愉快地开发了。

然后我们继续使用 `pamtester` 测试：

```bash
> sudo pamtester -v kde-fingerprint $USER authenticate
pamtester: invoking pam_start(kde-fingerprint, ferne, ...)
pamtester: performing operation - authenticate
23:09:41 INFO  src/pam_fprint_zw111.c:38: pam_sm_authenticate called
23:09:41 INFO  src/pam_fprint_zw111.c:40: now openning device /dev/FP_ZW111...
23:09:41 INFO  src/pam_fprint_zw111.c:51: device /dev/FP_ZW111 opened, now configuring terminal parameters...
23:09:41 DEBUG src/serial.c:63: request from fd: 3, set baudrate: 4097, databit: 3, stopbit: 0, parity: 0, vtime: 0, vmin: 0

23:09:41 INFO  src/protocol.c:289: ====================================================
23:09:41 DEBUG src/protocol.c:99: fill pack: id=1 code=32 data_len=5 total_len=17 checksum=0240
send:         EF 01 FF FF FF FF 01 00 08 32 02 FF FF 00 05 02 40
23:09:41 INFO  src/protocol.c:128: write 17 bytes
recv:         EF 01 FF FF FF FF 07 00 08 00 05 00 01 07 BB 00 D7
23:09:42 DEBUG src/protocol.c:194: nbr: 17, p_recv->code: 0x00  # 此处是我把手指放上去了
23:09:42 INFO  src/protocol.c:309: identified!
23:09:42 INFO  src/protocol.c:323: ====================================================
Password: # 直接按 enter 就行
pamtester: successfully authenticated
```

不过后面我们依然需要锁屏测试，那么怎么看日志呢？很简单，借助 `ctrl+alt+fx` 就能打开任意 tty 界面（不过需要注意 KDE 和 SDDM 本身会占用 tty, 不要开到这两个），然后登录一下，输入 `sudo journalctl -f | grep kwin_wayland_wrapper` 并确认，此时这个 tty 界面会一直显示该服务相关内容，我们就可以对 ZW111 模块的情况进行监视了。

## 测试 KDE kscreenlocker

我的桌面环境是 KDE Plasma 6：

```bash
> plasmashell --version
plasmashell 6.3.6
```

KDE 本身自带一个锁屏登录模块，而刚开机时则一般由 SDDM 或者其他的 Display Manager 接管，这里不详细讨论这个问题，只要知道它是独立于 Desktop Environment 的一个应用，用于在 shell 日志滚完之后接管其显示，启动顺序是 shell -> DM -> DE， KDE 一般和 SDDM 搭配使用，在 SDDM 验证身份之后，由 KDE 接替接下来的显示工作（占用一个新的 tty），并且之后也是 KDE 来负责会话的管理（锁屏，解锁，切换用户等）

KDE 和 SDDM 各自会占用一个 tty， SDDM 在验证通过之后你再切过去 (ctrl + alt + fx, x 是多少自己试一下就知道了) 会发现是卡死的，不过这点对于我们接下来要做的事来说不重要。

首先还是老规矩，简单编写一个[模块][pam_kde_auth_test.c] 并链接过去，然后测试一下

```bash
# 这里测完之后记得删掉
> gcc -fPIC -shared -o pam_kde_auth_test.so pam_kde_auth_test.c && sudo ln $(pwd)/pam_kde_auth_test.so /lib/x86_64-linux-gnu/security
```

注意， KDE 的 PAM 服务文件在 `/usr/lib/pam.d` 里面，你 `ls` 之后会发现有三个 kde 相关的，分别对应三种解锁方式：密码，指纹，卡片，这三个验证方式是独立的，任意一个验证成功都能解锁。

但是我们之前一直在讲的都是 `/etc/pam.d` 里面的文件，那么这里就有个配置文件查询优先级的问题，KDE 的这个 PAM 模块是先找 `/etc/pam.d` 有没有对应文件，如果没有就回退到 `/usr/lib/pam.d` 里面。

因此现在让我们把文件复制过去

```bash
> cp /usr/lib/pam.d/kde-fingerprint /etc/pam.d/kde-fingerprint
```

然后编辑：

```plain text
# ...
auth	  required	      pam_fprintd.so
auth    required        pam_kde_auth_test.so  # 这个是我们添加的
# ...
```

再借助 `pamtester`， 我们来调用下这个服务测试一下：

```bash
> sudo pamtester -v kde-fingerprint $USER authenticate
pamtester: invoking pam_start(kde-fingerprint, ferne, ...)
pamtester: performing operation - authenticate
[kde_authtest] called
Password: ^C

>
```

接下来我们验证下锁屏是否能触发，锁屏之后再解锁，日志会出现如下内容：

```bash
> sudo journalctl -f | grep -i kde_auth
10月 19 20:50:42 fernelaptop kwin_wayland_wrapper[219614]: [kde_authtest] called
```

然后再把 `<flag>` 从 `required` 改成 `sufficient`，保存，此时如果你锁屏的话，就会发现直接就能解锁：

![directly-unlocked][directly-unlocked]

接下来，删除 `pam_kde_auth_test.so`，那一行，然后做如下修改：

```plain text
# 成功验证就跳过，否则继续往下执行 pam_fprintd.so
# 之后这里还会做出一些参数上的调整，具体看 /config/kde-fingerprint
auth [success=1 default=ignore] pam_fprint_zw111.so
# 这一个是默认的指纹模块
auth	  required	      pam_fprintd.so
```

新建 `pam_fprint_zw111.c` 并编写模块交互代码。

```bash
> gcc -fPIC -shared -o pam_fprint_zw111.so pam_fprint_zw111.c && sudo ln $(pwd)/build/pam_fprint_zw111.so /lib/x86_64-linux-gnu/security
```

然后测试，发现可以直接解锁，然后我们只要对接写好的指纹模块即可

这里我们不再借助 kde-fingerprint 这个服务文件去测试，而是自己写一个 [pam-fp-zw111][pam-fp-zw111]， 接下来测试就可以 `pamtester pam-fp-zw111 $USER authenticate` 来写，更方便

## 应用层面的超时机制

源代码见这里： [waiter.c][waiter.c]

而在写完了 [pam_fprint_zw111.c][pam_fprint_zw111.c] 之后，我发现打开设备最好是需要有个超时机制，不然 sudo 之类的程序就会卡住，因此我借助函数指针和回调函数的方法简单写了一版

```c
static bool pam_connect_dev(pam_handle_t *pamh, struct pam_dev_ctx *pCtx,
                            bool is_reconnect) {
  enum wait_stat wst;
  if (is_reconnect)
    fp_close(pCtx->fd);

  wst = exec_fn_until(MAX_TIMEOUT, 1000, _open_dev, pCtx);

  if (wst != WAIT_DONE) {
    log_error("can not open device %s (err: %s)", FINGER_MOD, wstatstr(wst));
    pam_info(pamh, "无法连接设备");
    return false;
  }
  return true;
}
```

然后就可以如上操作，上面就是打开设备的超时

至于为什么整个验证逻辑没有放进超时框架里面，嘛，这个是因为我用的枚举，这么操作会导致抽象泄漏的问题，所以整个验证逻辑就一直维持原样，虽然又臭又长，但至少能用

# 三、PAM 模块安全问题

## 1:N 认证问题

那么我们自己开发的一个模块，肯定会有一些小缺点，这些如果你是自用的话其实不算什么大问题，但如果接入公共验证系统的话，我还是建议慎重，可能我的代码并不适合你。

首先是我第一轮就想到的问题 —— **既然 PAM 模块是接入 `/etc/` 的，那么是不是意味着这个设置对所有用户都生效？**

于是我就再加了一个用户，然后用这个用户名去测试，结果简直 Amazing 啊(毕导上身)：

```bash
> sudo adduser "testusr"

> pamtester pam-fp-zw111 "testusr" authenticate
17:27:46 INFO  src/pam_fprint_zw111.c:92: pam_sm_authenticate called with flags: 0x00, argc: 0
17:27:46 INFO  src/pam_fprint_zw111.c:59: now openning device: /dev/FP_ZW111...
17:27:46 INFO  src/protocol.c:255: now openning device /dev/FP_ZW111...
17:27:46 INFO  src/protocol.c:269: device /dev/FP_ZW111 opened, now configuring terminal parameters...
17:27:46 INFO  src/pam_fprint_zw111.c:78: device opened!
设备已连接
17:27:46 INFO  src/pam_fprint_zw111.c:113: please place your finger on the module
17:27:46 DEBUG src/protocol.c:124: fill pack: id=1 code=32 data_len=5 total_len=17 checksum=0240
send:         EF 01 FF FF FF FF 01 00 08 32 02 FF FF 00 05 02 40
17:27:46 DEBUG src/protocol.c:163: write 17 bytes
recv:         EF 01 FF FF FF FF 07 00 08 00 05 00 01 04 A9 00 C2
17:27:48 DEBUG src/protocol.c:244: nbr: 17, p_recv->code: 0x00
17:27:48 INFO  src/pam_fprint_zw111.c:161: identified!
pamtester: successfully authenticated
```

会导致这个问题的代码主要是 [pam_fprint_zw111.c][pam_fprint_zw111.c] 中的这一句：

```c
while ((status = fp_auto_identify(*pfd, 2, 0xFFFF, true, false)) != FP_OK) {
```

0xFFFF 这个 ID 在手册中表示是一个 1:N 的匹配机制，也就意味着只要录入指纹，那么不管是谁都能匹配到，所以才会出现上面那个问题，我想到的一个解法是为用户分配一个 ID 表格，规定哪些用户可以使用哪些指纹，然后再去做对应的匹配，当然因为我是自用就懒得折腾这个了

## PAM 模块权限问题

还记得我们之前的硬链接吗，实际上这种方法会把当前用户的元数据也写过去，也就是说：

```bash
> ls -l /usr/lib/x86_64-linux-gnu/security/pam_fprint_zw111.so

-rwxrwxr-x 2 ferne ferne 63488 11月17日 18:29 /usr/lib/x86_64-linux-gnu/security/pam_fprint_zw111.so
```

可以看到用户是 `ferne:ferne`，这也就意味着：

```bash
> echo "Replaced" > /usr/lib/x86_64-linux-gnu/security/pam_fprint_zw111.so

> cat /usr/lib/x86_64-linux-gnu/security/pam_fprint_zw111.so
Replaced
```

也就是说如果有人知道你这个玩意并且你把电脑给了他，那他不需要 root 就能轻松让你的验证程序失效，他也可以替换成 `pam_permit.so` 的内容，然后你的 root 就形同虚设了

```bash
> cat /usr/lib/x86_64-linux-gnu/security/pam_permit.so > /usr/lib/x86_64-linux-gnu/security/pam_fprint_zw111.so

> pamtester pam-fp-zw111 $USER authenticate
pamtester: successfully authenticated
```

因此我们需要和其他程序一样设置成如下权限：

```bash
> ls -l /usr/lib/x86_64-linux-gnu/security/pam_permit.so | awk '{ print $1 " " $3 " " $4 }'
-rw-r--r-- root root
```

因此让我们依次执行以下命令：

```bash
1 export FP_MOD="/usr/lib/x86_64-linux-gnu/security/pam_fprint_zw111.so"
2 sudo rm $FP_MOD
3 sudo cp /home/ferne/code/self-proj/fingerprint/build/pam_fprint_zw111.so $FP_MOD
4 sudo chmod 644 $FP_MOD
5 ls -l $FP_MOD | awk '{ print $1 " " $3 " " $4 }'
```

对应的输出就会变成:

```
-rw-r--r-- root root
```

这样我们的验证模块文件就不再依赖硬链接，并被设置到正常的权限了

## 指纹模块服务权限问题

这里其实是和上面那个同理的

```bash
> ls -l /etc/pam.d/pam-fp-zw111 | awk '{ print $1 " " $3 " " $4 }'
-rw-rw-r-- ferne ferne

> ls -l /etc/pam.d/sudo | awk '{ print $1 " " $3 " " $4 }'
-rw-r--r-- root root
```

因此操作方法也就和上面的差不多，我就不再赘述了

## AI 的一些意见

我不清楚对不对啊，因此只是列出来供大家参考

```
SUBSYSTEMS=="usb", DRIVERS=="ch341", MODE="0666", SYMLINK+="FP_ZW111"
```

比如它说这里的 MODE 应当是 0660，不能给 others 以读写权限，我感觉也许是对的，所以上面的配置是这么写的。

# 四、支线

## libpam 工作原理解析 & PAM 应用与模块通信机制

此处代码参考：

- [应用代码][test_pam.c]
- [模块代码][pam_auth_test.c]


应用程序发起的验证流程为： `pam_start` -> `pam_authenticate` -> `pam_end`

而对应到 Linux-PAM 里面的调用流程就变成了：

```
// 箭头指向表示函数调用的方向
pam_start -+
           |
           v
_pam_init_handlers  -+
                     |
_pam_load_conf_file <-> _pam_parse_conf_file -> _pam_add_handler -> _pam_load_module
```

Linux-PAM 在加载完对应的模块之后，会先返回 `mod` 的地址，然后 `_pam_add_handler` 开始一路往上返回
`PAM_SUCCESS`，然后用户检测到 `PAM_SUCCESS` 之后就可以调用 `pam_authenticate` 了，接下来的调用链是：

```
pam_authenticate -> _pam_dispatch -> _pam_dispatch_aux
    -> `h->func(pamh, flags, argc, argv)`
```

其中 `h->func` 就是我们的 `pam_sm_authenticate`，然后也是一路返回验证结果，最后调用 `pam_end` 释放资源， 这就是整个 Linux-PAM 模块的工作流程了 (请允许我小小自豪一下，我全程用的 vim + grep 抓逻辑，只用了大概 15 分钟就研究出来了)

```
/** 这里只展示了 /linux-pam/libpam/pam_get_authtok.c 里面的调用链
 *  像我们常见的 Linux 的密码提示的更上层调用来自于 /libpam/modules/pam_unix.c
 *  由于我们并不研究各个模块，这部分我也就没什么兴趣去探索
 *  如果你有兴趣的话可以自行克隆源码去研究
 *  但整体上来说代码是非常 make sense 的
 *  你甚至不需要 GDB 和 IDE 就能读个七七八八
 *  除了这代码库的缩进风格属实让人心生爱慕以外没什么别的缺点
 *  我快把 % 和 [[ 按烂了，最后还是决定一个 gg=G 过去算了
 */
pam_get_authtok ->  pam_get_authtok_internal 
    -> pam_prompt ->  pam_vprompt -> conv_func(我们自定义的通信函数)
```

而我们 [之前](#PAM-模块工作流程) 提到， PAM 应用发起请求，然后再由 PAM 模块返回认证结果，这里就依赖于 PAM 应用和模块的通信，通信的函数需要满足如下签名：

```c
/// @file /linux-pam/libpam/include/security/_pam_types.h
int (*conv)(int num_msg, const struct pam_message **msg,
		struct pam_response **resp, void *appdata_ptr);
```

这里是对应的调用代码，我们构造的 `appdata_ptr` 会被作为调用对象，此时我们需要在特定的 `msg->style` 进行特定的操作(如示例代码中的设置 `reply` 和 `resp`)，比如代码中的几个例子，这是因为在 Linux-PAM 内部，需要覆写这里相关的内存，然后交由我们编写的 PAM 模块进行验证

```c
// /linux-pam/libpam/pam_vprompt.c
  // ...
  msg.msg_style = style;
  msg.msg = msgbuf;
  pmsg = &msg;

  retval = conv->conv (1, &pmsg, &pam_resp, conv->appdata_ptr);
  if (retval != PAM_SUCCESS && pam_resp != NULL)
    pam_syslog(pamh, LOG_WARNING,
      "unexpected response from failed conversation function");
  if (response)
    *response = pam_resp == NULL ? NULL : pam_resp->resp;
  else if (pam_resp && pam_resp->resp)
    {
      pam_overwrite_string (pam_resp->resp);
      _pam_drop (pam_resp->resp);
    }
  pam_overwrite_string (msgbuf);
  _pam_drop (pam_resp);
  // ...
```

其实大概的流程是很简单的，比如拿我们编写的代码举例：

```
./test_pam pam-auth-test testusr testpwd
  | (calling `pam_start`)
  |
  +--> Linux-PAM (parsing config file)
       | (calling `pam_authenticate`)
       |
       +--> pam_auth_test.so
            |
            +--> `retval = pam_get_authtok(pamh, PAM_AUTHTOK, &password, "Password: ");`
                  |
                  +--> `conv_func(...)`
                        |
                        +--> `if (... && strcmp(password, TEST_PASSWD) == 0)`
                              |
                              +--> `return PAM_SUCCESS;` / `return PAM_AUTH_ERR;`
                                    |
                                    +--> `pam_end(...);`
                                         |
                                         +--> exit with status code
```

这就是一整个通信机制的调用链，没什么复杂的，如果你搞不清某个抽象层的工作原理，可以的话其实直接去读对应的代码就行了，有些博客反而会误导人，我写的也不一定全对，**但我鼓励编程小白们不要害怕读别人的代码，其实你按照整个调用链来读，你会发现在代码世界里面遨游是很有趣的**，优秀的代码是十分 make sense 的，不需要 IDE 和 GDB 也能读

## 定制 kscreenlocker 锁屏界面

本来我是想放在主线的，但是思来想去发现这部分放在支线更加合理，因为你几乎必然要了解 PAM 通信原理之后才能客制化

我稍微查了一下，发现可以通过 `pam_error` 来让锁屏界面显示一些提示信息，于是就可以使用 `pam_error(pamh, "你还有 %d 次重试机会", MAX_RETRY - retry);` 来提示重试次数

然而最后发现这种改法会卡窗口期 —— 如果验证失败之后被弹跳动效搞了的话，就会导致提示信息显示不及时，我们发现锁屏测试的时候指纹验证不通过的动效

KDE 是使用 Qt 写的，新版本的 Qt 带有 QML，这是一种有些类似于 HTML 效果的一种特殊语言，为了客制化，我们需要定位到 `/usr/share/plasma/shells/org.kde.plasma.desktop/`，其下有一个 `lockscreen` 目录，这就是我们的 kscreenlocker 的 UI 界面了

我们需要修改的只有 [MainBlock.qml][MainBlock.qml]

在此之前我们先简单看一下 kscreenlocker 的源代码，由于有了上文的铺垫，我们只要直接找到相关的对话函数即可：

```cpp
// kscreenlocker: pamauthenticator.cpp: line 53
int PamWorker::converse(int n, const struct pam_message **msg, struct pam_response **resp, void *data)
{
    PamWorker *c = static_cast<PamWorker *>(data);
        // ...
        case PAM_ERROR_MSG: {
            const QString error = QString::fromLocal8Bit(msg[i]->msg);
            qCDebug(KSCREENLOCKER_GREET, "[PAM worker %s] Message: Error message: %s", qUtf8Printable(c->m_service), qUtf8Printable(error));
            Q_EMIT c->errorMessage(error);
            break;
        }
        // ...
```

kscreenlocker 源码给我的观感十分不好，比如有 `pamauthenticator.cpp` 和 `pamauthenticators.cpp`，我看这可能就是没想清楚应该怎么写导致的，反正看的我挺难受的

简单点说就是通过上面那个 `converse` 方法，传过来的 `error` 会被设置成一个叫 `m_errorMessage` 的全局变量，然后相关的一些代码会告诉 QML 信息有改动，然后 QML 就去读取对应的信息，因为这个玩意给我的观感不太好，我没仔细去读，也不敢保证完全就是我说的这样，但总体上来说我认为是大差不差的。

……越看越讨厌 C++ …… 好多诡异写法……

我们想要规避掉 `pam_error` 的抖动，因此我就打算用 `pam_info` 去覆盖它，这里也要吐槽一下 kscreenlocker ，明明提供了 `infoMessage`，为什么 QML 没有相关配置？我觉得大概率就是他们也没想好具体要怎么操作罢

```qml
// File: /.../lockscreen/MainBlock.qml
component FPLabel : PlasmaComponents3.Label {
    id: _fpLabel
    required property int kind
    required property string label

    visible: authenticator.authenticatorTypes & kind
    text: label
    textFormat: Text.PlainText
    horizontalAlignment: Text.AlignHCenter
    Layout.fillWidth: true

    Connections {
        target: authenticator

        function onNoninteractiveInfo(kind, authenticator) {
            if (kind & _fpLabel.kind) {
                // 基本上就是照抄就行了，把 errorMessage 换成 infoMessage 就可以
                _fpLabel.text = Qt.binding(() => authenticator.infoMessage)
           }
       }
    }
}

FPLabel {
    kind: ScreenLocker.Authenticator.Fingerprint
    label: "(此处显示与指纹模块 ZW111 有关的信息)"
}
```

然后由于 QML 是声明式的，声明的顺序决定了 UI 显示出来的顺序

接下来我们执行：

```bash
> mkdir -p ~/.local/share/plasma/shells/org.kde.plasma.desktop/ && cp -r /usr/share/plasma/shells/org.kde.plasma.desktop/ ~/.local/share/plasma/shells/

> ln -f /home/ferne/code/self-proj/fingerprint/org.kde.plasma.desktop/contents/lockscreen/MainBlock.qml ~/.local/share/plasma/shells/org.kde.plasma.desktop/contents/lockscreen/MainBlock.qml
```

然后锁屏界面就会替换成你自己自定义的

## `i18nd` 与 `gettext`

这是一个可有可无的支线，大概就是我因为好奇尝试了 [MainBlock.qml][MainBlock.qml] 里面的 `i18nd`，但是发现并不会被汉化，后面才发现原来这个和 gnu gettext & po/mo 文件有关，算是一个支线中的支线

国际化的大概操作是：`i18nd( "<filename>.mo", "<msgid>" )`

因此我们执行这个命令：

```bash
msgunfmt /usr/share/locale/zh_CN/LC_MESSAGES/plasma_shell_org.kde.plasma.desktop.mo -o ./plasmashell.po
```

然后打开 po 文件，你就会发现里面有原文和对应的文本，仿照案例抄一下就可以了

如果要使用 `i18nd`，那么就是把 plasmashell.po 给改一下加点内容，然后编译成 `plasma_shell_org.kde.plasma.desktop.mo` 并丢到 `~/.local/share/locale/zh_CN/LC_MESSAGES/` 里面就行， 如果不想做那么标准的国际化操作，直接写文字就行。

我最后图省事，就这么操作了：

```qml
FPLabel {
    kind: ScreenLocker.Authenticator.Fingerprint
    label: "(此处显示与指纹模块 ZW111 有关的信息)"
}
```

最后的效果大概是(注意密码框下面的 UI)：

{% raw %}
<div id="kscreenlocker-demo" style="position: relative; width: 100%; height: 0; padding-bottom: 75%;">
    <iframe src="/images/make-fp-mod-for-linux-pc/kscreenlocker-unlock-demo.mp4" 
        scrolling="no" border="0" frameborder="no" framespacing="0" allowfullscreen="true" 
        style="position: absolute; width: 100%; height: 100%; Left: 0; top: 0;" >
    </iframe>
</div>
{% endraw %}

# 五、总结

本文主要涉及到了：

- PAM 模块编写方法
- 为其他程序打通 “任督 PAM 脉”
- 指纹模块安全问题
- PAM 应用与模块的通信原理

等等等等，[下一篇][make-fp-mod-for-linux-pc-3] 是一篇碎碎念，主要是讲一些 AI 辅助开发的内容

---

[directly-unlocked]: /images/make-fp-mod-for-linux-pc/directly-unlocked.jpg
[sudo boomed]: /images/make-fp-mod-for-linux-pc/sudo-pam-file-not-found.png

[make-fp-mod-for-linux-pc-1]: https://aliferne.github.io/2026/11/10/make-fp-mod-for-linux-pc-1/
[make-fp-mod-for-linux-pc-3]: https://aliferne.github.io/2026/11/19/make-fp-mod-for-linux-pc-3/

[pam module config file]: https://zhuanlan.zhihu.com/p/663757925
[libpam-wiki]: http://www.fifi.org/doc/libpam-doc/html
[pamtester-doc]: https://pamtester.sourceforge.net/
[dyn-linking-sta-linking]: https://aliferne.github.io/2025/04/23/something-about-dyn-linking-and-static-linking/
[fPIC]: https://zhuanlan.zhihu.com/p/368364069
[gettext]: https://www.cnblogs.com/jjxhp/p/12036187.html

[pam-fp-zw111]: https://github.com/aliferne/fp-zw111-unlocker/blob/main/configs/pam-fp-zw111
[pam_fprint_zw111.c]: https://github.com/aliferne/fp-zw111-unlocker/blob/main/src/pam_fprint_zw111.c
[pam_auth_test.c]: https://github.com/aliferne/fp-zw111-unlocker/blob/main/test/pam-test/pam_auth_test.c
[test_pam.c]: https://github.com/aliferne/fp-zw111-unlocker/blob/main/test/pam-test/test_pam.c
[pam_kde_auth_test.c]: https://github.com/aliferne/fp-zw111-unlocker/blob/main/test/pam-test/pam_kde_auth_test.c
[waiter.c]: https://github.com/aliferne/fp-zw111-unlocker/blob/main/src/waiter.c
[MainBlock.qml]: https://github.com/aliferne/fp-zw111-unlocker/blob/main/org.kde.plasma.desktop/contents/lockscreen/MainBlock.qml

