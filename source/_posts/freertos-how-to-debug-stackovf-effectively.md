---
title: 如何在 FreeRTOS 中高效调试栈溢出错误
date: 2026-06-21 13:42:34
categories:
- embedded
tags:
- mcu
- rtos
---

## 前言

打完比赛之后感到无事可做，因此最近正在自己 DIY 一个掌上阅读器，为了更好地~~折磨~~提升自己，决定引入一堆自己以前几乎从来没学过的东西，并且还尝试学 Linux 驱动代码的风格去做一个芯片无关的 BSP 层，不过这些不会是这篇文章的重点，等这个项目完工之后应该会整理文档并发博客，不过那都是后话了。

本文将分为上下两篇，上篇你会学到：

1. 如何判断栈溢出、推算栈的占用情况
2. 掌握 FreeRTOS 针对堆栈溢出检测的钩子函数

而在下篇，我们则会聚焦于：

1. Cortex-M4 内核的编程模型与错误处理机制
2. FreeRTOS 的内核初始化与上下文切换逻辑

本文的下篇同时可视为对之前 [STM32 搭建 Zig 开发环境](https://aliferne.github.io/2026/02/17/build-up-zig-dev-env-on-stm32g431/) 理论部分的详细补充。

## 事故代码

那么就先介绍下这个问题的背景吧，由于掌上阅读器肯定需要 UI 和 SD 卡，所以我就**引入了 LVGL 和 FatFs**，而因为我雄心相对较大（这不完全只是一个掌上阅读器），所以还**引入了 FreeRTOS**，不过我的水平只局限于能创建一些任务，这个项目也顺带附带着我学习高级用法的想法。

之后我为了测试方便就索性一股脑全写到 ui_task.c 文件中，下面是源代码：

```c
void StartUITask(void const *argument)
{
    lv_init();
    lv_port_disp_init();

    /* 文件操作部分 ------------------------- */
    FIL fp;
    /* 本质是 f_open */
    FRESULT res = storage_open(&sdcard, &fp, "test.txt", FA_READ | FA_OPEN_EXISTING);
    UINT fnum   = 0;
    ASSERT_FAIL(res != FR_OK,
                /* 本质是 f_close */
                storage_close(&fp);
                /* 本质是 vTaskDelay(1000); */
                for (;;) { os_delay_ms(1000); });
    /* 本质是 f_read */
    res = storage_read(&fp, buf, LEN(buf), &fnum);
    ASSERT_FAIL(res != FR_OK,
                storage_close(&fp);
                for (;;) { os_delay_ms(1000); });
    storage_close(&fp);
    /* 文件操作部分 End ------------------------- */

    /* UI 部分 ------------------------- */
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_radius(&style, 5);

    /*Make a gradient*/
    lv_style_set_width(&style, 128);
    lv_style_set_height(&style, LV_SIZE_CONTENT);

    lv_style_set_pad_ver(&style, 0);
    lv_style_set_pad_left(&style, 0);

    lv_style_set_x(&style, lv_pct(0));
    lv_style_set_y(&style, 0);

    /*Create an object with the new style*/
    lv_obj_t *obj = lv_obj_create(lv_scr_act());
    lv_obj_add_style(obj, &style, 0);

    lv_obj_t *label = lv_label_create(obj);

    if (buf[0] != 0)
        lv_label_set_text(label, buf);
    else
        lv_label_set_text(label, "确认");
    /* UI 部分 End ------------------------- */

    for (;;) {
        /* 以约 500ms 的周期闪烁 LED */
        gpio_toggle(&usr_led);

        lv_timer_handler();
        os_delay_ms(500);
    }
}
```

其中 `ASSERT_FAIL` 是一个自己写的很简单的宏：

```c
/* 失败断言宏，断言 cond 一定为假，否则执行 actions 操作 */
#define ASSERT_FAIL(cond, actions) \
    do {                           \
        if ((cond)) {              \
            actions;               \
        }                          \
    } while (0)
```

而任务创建函数则为：

```c
osThreadDef(UITask, StartUITask, osPriorityNormal, 0, 512);
UITaskHandle = osThreadCreate(osThread(UITask), NULL);
```

相信一看代码，老手基本就知道是什么情况了，**确实就是栈分配得不够**，不过还是想请你们看我的一整套 Debug 流程，做一个抛砖引玉的作用，希望能够得到高人的指导，比如更快定位问题的方法，一些 Debug 的方法论等等，在此谢谢大家了。

## 事故现场复现

本次事故的表现是 LED 灯不闪烁，而屏幕可以正常显示。

这个名为 test.txt 的文件是存在的，因此不存在卡死在一开始的 `for` 循环的情况，而读取操作完全正常， 而 UI 界面可以显示文件内容，这就使我犯了难，至少表面上看起来不像是代码问题，不过还是得看下代码的。

由于其他任务均为空实现，我因此最先怀疑的就是 ui_task 的代码问题，上面代码很明显可以分为两部分操作，一部分是文件 IO，另一部分是 UI 绘制，我在源代码上均做了标注，我的测试有三个步骤：

1. 注释 *文件 IO* 和 *UI 绘制*, 烧录发现 LED 正常闪烁，进入 debug 发现上下文正常切换
2. 注释 *文件 IO*, 烧录发现 LED 无法闪烁，进入 debug 发现触发 HardFault
3. 注释 *UI 绘制*, 现象同第一次尝试

到这里基本可以确定肯定是 LVGL 啥的有点问题，不过不可能怀疑到人家源代码上，~~毕竟人家水平比我高多了~~所以没办法，也只能进 Debug 看堆栈了。

我的断点设置在两句 `ASSERT_FAIL` 处，以及最后 `for` 循环的三个函数内。逐步执行，两句宏的断言均通过，而最后在 `os_delay_ms(500)` 处，按步执行之后不再正常进入此任务，而正常来说应当会从 `gpio_toggle` 再度执行循环内函数。

我们继续 debug，按下暂停，发现进了 `HardFault_Handler`，而 HardFault 一般都会跟内存访问，或者写了一些不该写的东西之类的有关，根据上面的测试情况，我们很容易想到注释一些东西之后执行的内容会变少，也就是栈空间占用变少，那么我们就来计算一下栈空间的使用情况吧。

## 计算堆栈调用情况

我们先来看这个 `FIL`，它是个结构体：

```c
typedef struct {
	FFOBJID	obj;		/* Object identifier (must be the 1st member to detect invalid object pointer) */
	BYTE	flag;		/* File status flags */
	BYTE	err;		/* Abort flag (error code) */
	FSIZE_t	fptr;		/* File read/write pointer (0 on open) */
	DWORD	clust;		/* Current cluster of fptr (invalid when fptr is 0) */
	LBA_t	sect;		/* Sector number appearing in buf[] (0:invalid) */
#if !FF_FS_READONLY
	LBA_t	dir_sect;	/* Sector number containing the directory entry (not used in exFAT) */
	BYTE*	dir_ptr;	/* Pointer to the directory entry in the win[] (not used in exFAT) */
#endif
#if FF_USE_FASTSEEK
	DWORD*	cltbl;		/* Pointer to the cluster link map table (nulled on open; set by application) */
#endif
#if !FF_FS_TINY
	BYTE	buf[FF_MAX_SS];	/* File private data read/write window */
#endif
} FIL;
```

由于这里面的宏全都是满足条件的，即所有的变量都被启用了，我们需要全部计算：

首先是第一部分：
`BYTE + BYTE + DWORD + BYTE* + DWORD* + BYTE * FF_MAX_SS`

32 位单片机的地址是 32 位的，所以是四个字节，那么对于第一部分就有：
1 + 1 + 4 + 4 + 4 + 1 * 512 = 526

然后第二部分：
`FFOBJID + FSIZE_t + LBA_t + LBA_t`

其中 `FFOBJID = FATFS* + WORD + BYTE + BYTE + DWORD + FSIZE_t + DWORD * 5` (我没启用 `FF_FS_LOCK` 宏)

那么就是：
(4 + 2 + 1 + 1 + 4 + 8 + 4 * 5) + 8 + 4 + 4 = 56

两部分合起来为 582 Bytes，这是理论值，我们没考虑结构体对齐的问题，实际会更大些。
如果你使用 Vscode + Clangd 插件的话，借助 `sizeof` 然后鼠标悬浮一下就可以看到值的大小，我这里显示为 600 Bytes.

更加准确的做法是：

```bash
arm-none-eabi-objdump -d ./build/Self-DIY-Kindle/Self-DIY-Kindel.elf --disassemble=StartUITask
```

然后找到这一行：

```asm
0803067c <StartUITask>:
 803067c:	b530      	push	{r4, r5, lr}
 803067e:	f5ad 7d19 	sub.w	sp, sp, #612	@ 0x264
```

这里的 612 是实际占用堆栈的大小，汇编码为 `sub.w` 表示其是宽字节（32位的）。
这里分配了 612 Bytes

剩下的是函数调用，实际上不好估算，根据函数调用堆栈的模型，我们**需要找到调用链最深的**，因为这个估算起来太麻烦了我们就不找了。

## 解决方法

很简单，因为我们确定了直接原因是栈溢出，那么只要把 `FIL` 变成全局变量，让它到 bss 段去占整个 Flash 的空间，别挤在栈里基本就差不多了，如果还不放心则可以给任务分配大点栈空间。

## 简易方法实现栈溢出检测

上面那一套算来算去还要看反汇编的太麻烦了，并且我还发现如果没有 `FIL` 这个变量，连 `sub.w sp` 这个操作都不会有，就会更难看出栈大小，而我闲着无聊翻 FreeRTOS 的文档的时候偶然发现一个[简便的方法][FreeRTOS 堆栈使用和堆栈溢出检查].

简单总结一下就是人家提供了 `configCHECK_FOR_STACK_OVERFLOW` 宏的配置（CubeMX 有这个配置选项，不过默认是 Disable，所以我看 config 文件的时候一直以为没有对应的调试方法，还是得多看文档啊），通过定义为不同的数值来对应设置如何检查堆栈溢出情况，并通过如下钩子函数：

```c
void vApplicationStackOverflowHook( TaskHandle_t xTask,
                                    char *pcTaskName );
```

来执行你自定义的调试信息。

我给这个宏设置为了 1, 则对应应该是这段代码被启用：

```c
#if( ( configCHECK_FOR_STACK_OVERFLOW == 1 ) && ( portSTACK_GROWTH < 0 ) )

	/* Only the current stack state is to be checked. */
	#define taskCHECK_FOR_STACK_OVERFLOW()																\
	{																									\
		/* Is the currently saved stack pointer within the stack limit? */								\
		if( pxCurrentTCB->pxTopOfStack <= pxCurrentTCB->pxStack )										\
		{																								\
			vApplicationStackOverflowHook( ( TaskHandle_t ) pxCurrentTCB, pxCurrentTCB->pcTaskName );	\
		}																								\
	}

#endif /* configCHECK_FOR_STACK_OVERFLOW == 1 */
```

然后自定义逻辑则为：

```c
void vApplicationStackOverflowHook(TaskHandle_t xTask,
                                   char *pcTaskName)
{
    printf("Stack Overflow! %s\n", pcTaskName);
    if (strcmp(pcTaskName, "UITask") == 0) {
        gpio_write(&usr_led, GPIO_Level_High);
    }
}
```

串口便确实能打印出这个信息，而 LED 也如预想中亮起了（缺省行为是熄灭的）。

不过这并没有结束，我们目前只知道是栈溢出，对于只想在用 FreeRTOS
时定位有无栈溢出的读者来说，这一篇基本上够你用了，但是我不想止步于此，因此我还会定位栈溢出究竟导致了什么，才会使得
MCU 触发了 HardFault.
非常不幸的是，想要搞清楚这方面的问题，你几乎必然需要了解内核，因此我们在第二篇将会花费大量精力去讲解
Cortex-M4
内核的一些设计，这势必会劝退很多读者，不过如果你对这类话题感兴趣的话，那么欢迎来观看我的下一篇博客！

---

<p style="display:none">参考资料：</p>

[STM32 Cortex M4 编程手册]: https://www.st.com/content/ccc/resource/technical/document/programming_manual/6c/3a/cb/e7/e4/ea/44/9b/DM00046982.pdf/files/DM00046982.pdf/jcr:content/translations/en.DM00046982.pdf
[ARM CM3/4 异常进入与返回的过程]: https://shequ.stmicroelectronics.cn/thread-604515-1-1.html
[RTOS 基础知识]: https://wwww.freertos.org/zh-cn-cmn-s/Documentation/01-FreeRTOS-quick-start/01-Beginners-guide/01-RTOS-fundamentals
[深入理解操作系统的概念及定位]: https://www.cnblogs.com/kevinbee/p/18678187
[FreeRTOS 堆栈使用和堆栈溢出检查]: https://w.freertos.org/zh-cn-cmn-s/Documentation/02-Kernel/02-Kernel-features/09-Memory-management/02-Stack-usage-and-stack-overflow-checking
[RTOS Ref]: https://www.zhihu.com/question/593816921/answer/1957909257069520682?share_code=14IBMqKlAx7yM&utm_psn=2050173737027236484
