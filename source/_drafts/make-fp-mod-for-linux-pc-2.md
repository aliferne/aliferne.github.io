---
title: "为 Linux PC 自制指纹验证功能 (中)"
date: 2026-11-14 22:00:00
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

<!-- more -->

# PAM 模块

网上简中博客的 PAM 模块相关资料以 [配置文件][pam module config file] 居多，并不涉及如何开发 `so` 动态库，因此本章将结束这一局面，根据 [一手资料][libpam-wiki] 再结合自己的理解写一篇模块开发教程。

## PAM 模块工作流程

PAM 应用程序指定对应的 service --> 应用程序发起 `auth` 请求 --> libpam 在 service 内部配置找到对应 so 文件/模块 --> `dlopen` 打开 so 文件并调用对应函数 --> 处理结果返回 libpam 并进行下一步处理 --> PAM 程序收到 API 的返回码并进行对应的处理

PAM 配置文件有一定的语法规则，由于配置文件的相关资料很多，这里只对需要用到的做简要说明。

基本语法规则： `<type> <flag> <path> <args>`
比如这个例子： `auth required xxx.so`，就表示 authentication （用户身份验证） 要求使用 `xxx.so` 这个模块， `xxx.so` 需要实现一些 PAM 模块要求的函数，然后通过返回 `PAM_*` 状态码告知是验证通过还是失败

我们会用到的只有 `auth`, `required` 和 `sufficient`：

- `auth` 就是鉴权用的，验证你是不是对应的用户
- `required` 和另一个叫 `requisite` 的很像， `required` 表示调用所有需要的模块，即使有模块失败了也先不管，等整体调用完了再返回失败， 而另一个则是一失败就立马返回
- `sufficient` 表示只要验证通过就直接返回成功，并且自动忽略前面模块的 fail。

由于 `sufficient` 有成功就直接返回的特性，这个其实就会有安全上的顾虑 (试想一下如果写的是 `auth sufficient pam_permit.so`， 那整个 PAM 就炸掉了)，我不是特别建议使用。

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
sudo ln $(pwd)/pam_auth_test.so /lib/x86_64-linux-gnu/security
```

我个人认为链接最好用硬链接，只要你的盘在同一个文件系统内就行，硬链接的好处在于两个文件都指向了同一 inode 且修改任何一个文件的内容，其他文件都会跟着更新，删除任意文件都不会导致已有的文件失效，除非全删光。

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
sudo ln $(pwd)/pam-auth-test /etc/pam.d
```

这样子通路就基本搭建完毕了，然后我们接下来自己写一个 [应用程序][test_pam.c]

然后编译：

```bash
# 注意链接 pam 库
gcc -o test_pam.c test_pam -lpam
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

接下来我们简单讲解一下 PAM 应用与模块的通信：

## PAM 应用与模块的通信机制


# 为其他认证应用打通 任督 PAM 脉

## KDE kscreenlocker

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

接下来，删除 `pam_kde_auth_test.so`，那一行，这里我会建议注释原来的 `pam_fprintd.so` 选项，改用自己自定义的 `.so` 作为指纹模块：
TODO: 这里是否可以借鉴： `auth required pam_succeed_if.so user != root quiet_success` 的编辑思路来实现两个文件其中一个参与？

```plain text
#auth	  required	      pam_fprintd.so
auth    required        pam_fprint_zw111.so  # 这个是我们添加的
```

新建 `pam_fprint_zw111.c` 并编写模块交互代码。

```bash
> gcc -fPIC -shared -o pam_fprint_zw111.so pam_fprint_zw111.c && sudo ln $(pwd)/pam_fprint_zw111.so /lib/x86_64-linux-gnu/security
```

然后测试，发现可以直接解锁，然后我们只要对接写好的指纹模块即可

# 打通 “任督 PAM 脉”

我们之前一直都在用 `/dev/ttyUSB1` 这样的来访问设备，然而这样子访问是很不方便的，原因在于设备名称可能发生变化，因此我们必须要以什么手段固定设备名称，在 Linux 下我们可以通过 udev 实现两个目标：

1. 固定设备名称
2. 不论设备插入哪个 USB 口，都能够以相同名称访问

因此我们可以执行如下操作：

```bash
> udevadm info -a -n /dev/ttyUSB1
...
looking at device '/devices/pci0000:00/0000:00:08.1/0000:06:00.4/usb3/3-1/3-1.2/3-1.2:1.0/ttyUSB1/tty/t>
  KERNEL=="ttyUSB1"
  SUBSYSTEM=="tty"
  DRIVER==""
# 我省略了相当一部分内容，你需要自己找到 idProduct, idVendor, serial 三个属性
# 这两个属性是实现上面两点的关键
...
```

然后在 `/etc/udev/rules.d/` 创建文件 `fprint_zw111.rules` 并写入：

```
SUBSYSTEMS=="usb", ATTRS{idProduct}=="0002", ATTRS{idVendor}=="1d6b", ATTRS{serial}=="0000:06:00.4", MODE:="0666", SYMLINK+="FP_ZW111"
```

最后再 `sudo udevadm control --reload-rules && sudo udevadm trigger` 以使得规则生效

然后就会得到：

```bash
> ls /dev/FP_ZW111
/dev/FP_ZW111
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


## 应用层面的超时机制

源代码见这里： [waiter.c][waiter.c]



`/lib/x86_64-linux-gnu/libexec/kscreenlocker_greet --testing`

查了一下发现可以通过 `pam_error` 来让锁屏界面显示一些提示信息
于是 `pam_error(pamh, "你还有 %d 次重试机会", MAX_RETRY - retry);`
但是发现会卡窗口期，如果验证失败之后被弹跳动效搞了的话，就会导致提示信息显示不及时
我们发现锁屏测试的时候指纹验证不通过的动效

找到了 `/usr/share/plasma/shells/org.kde.plasma.desktop/`，里面的 QML 和锁屏界面有关，看到 lockscreen 目录，可以修改这里的进行配置，clone 了 kscreenlocker 的源码进行查看，发现使用了 PAM 的对话机制 `PamAuthenticator::converse( ... )`， `Q_EMIT` 会发送 `pam_info` 和 `pam_error` 发出去的 `msg` 给到 QML, 但是 QML 只会显示 `pam_error` 的 `msg`， 原因在于只有这个是 `onNoninteractiveError` 的，我们需要扩充一下。

简单学习一下 QML， Qt 之间的机制，然后改动 QML，新增:

```qml
// File: .../lockscreen/MainBlock.qml
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

然后可以显示 `pam_info` 给出的东西，并且完全避免我不想要的失败动效
当然如果想要创建一个新 Label 的话，就是复制一下 `FailableLabel` 并粘贴，再删掉多余部分（Timer, 动画等），最后声明一下这个组件就行（即最后补上 `NewLabel { ... }`）。

qml 之间似乎通过一些规约互相引用，需要了解一下
这里应该只会改动 `MainBlock.qml`

由于是声明式的，所以只需要把组件声明之后放在那里就行，显示顺序取决于声明顺序

然后我们执行如下步骤：

```bash
> mkdir -p ~/.local/share/plasma/shells/org.kde.plasma.desktop/ && cp -r /usr/share/plasma/shells/org.kde.plasma.desktop/ ~/.local/share/plasma/shells/

> ln -f /home/ferne/code/self-proj/fingerprint/org.kde.plasma.desktop/contents/lockscreen/MainBlock.qml ~/.local/share/plasma/shells/org.kde.plasma.desktop/contents/lockscreen/MainBlock.qml
```

然后锁屏界面就会替换成你自己自定义的

---

comments: 这部分是当时代码里面新加了 `TestLabel` 的片段，酌情考虑是否加入博客中
现在需要加入了，我觉得还是新建一个标签好一些

[gettext]

然后发现 label 无法汉化，
查询 i18nd => GNU gettext & po/mo 文件
=> `i18nd( "<filename>.mo", "<msgid>" )`
=> `msgunfmt /usr/share/locale/zh_CN/LC_MESSAGES/plasma_shell_org.kde.plasma.desktop.mo -o ./plasmashell.po`
=> find `Unlock`
=> 找到对应汉化文本：解锁

如果要使用 `i18nd`，那么就是把 plasmashell.po 给改一下加点内容，然后编译成 plasma_shell_org.kde.plasma.desktop.mo 并丢到 `~/.local/share/locale/zh_CN/LC_MESSAGES/` 里面就行
如果不想做那么标准的国际化操作，直接写文字就行。

---

建议是写一个文件然后在其他文件里面 `include` 这个文件，
我写了个 [pam-fp-zw111](./test/pam_test/pam-fp-zw111)，然后链接过去
接下来测试就可以 `pamtester pam-fp-zw111 $USER authenticate` 来写，更方便

# PAM 模块安全问题

TODO: 讲解 PAM 模块的安全问题

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

---

[directly-unlocked]: /images/make-fp-mod-for-linux-pc/directly-unlocked.jpg

[make-fp-mod-for-linux-pc-1]: https://aliferne.github.io/2026/11/10/make-fp-mod-for-linux-pc-1/
[pam module config file]: https://zhuanlan.zhihu.com/p/663757925
[libpam-wiki]: http://www.fifi.org/doc/libpam-doc/html
[pamtester-doc]: https://pamtester.sourceforge.net/
[dyn-linking-sta-linking]: https://aliferne.github.io/2025/04/23/something-about-dyn-linking-and-static-linking/
[fPIC]: https://zhuanlan.zhihu.com/p/368364069
[gettext]: https://www.cnblogs.com/jjxhp/p/12036187.html
[pam_fprint_zw111.c]: https://github.com/aliferne/fp-zw111-unlocker/blob/main/src/pam_fprint_zw111.c
[pam_auth_test.c]: https://github.com/aliferne/fp-zw111-unlocker/blob/main/test/pam-test/pam_auth_test.c
[test_pam.c]: https://github.com/aliferne/fp-zw111-unlocker/blob/main/test/pam-test/test_pam.c
[pam_kde_auth_test.c]: https://github.com/aliferne/fp-zw111-unlocker/blob/main/test/pam-test/pam_kde_auth_test.c
[waiter.c]: https://github.com/aliferne/fp-zw111-unlocker/blob/main/src/waiter.c

