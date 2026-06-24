---
title: FreeRTOS 的上下文切换与栈溢出 —— 从 PendSV 到 HardFault 的调试全流程
date: 2026-06-24 17:42:34
categories:
- embedded
tags:
- mcu
- rtos
---

让我们[书接上回](https://aliferne.github.io/2026/06/21/freertos-how-to-debug-stackovf-effectively/)。

## 深挖栈溢出异常

### 调用堆栈

我们回到刚看到 `HardFault` 的时候，此时我们来观察调用堆栈：

```
HardFault_Handler@0x080009ec (/home/ferne/code/self-proj/Self-DIY-Kindle/Core/Src/stm32f4xx_it.c:95)
<signal handler called>@0xfffffff1 (未知源:0)
PendSV_Handler@0x0800774e (/home/ferne/code/self-proj/Self-DIY-Kindle/Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F/port.c:443)
```

得，一个 PendSV，一个 HardFault，还有个不知道什么情况的 signal handler 返回码，准备翻手册吧。
由于有 [搭建 Zig 开发环境那篇文章](https://aliferne.github.io/2026/02/17/build-up-zig-dev-env-on-stm32g431/) 的经验，查手册还是非常轻车熟路的。

在 Cortex-M4 内核中有一个特殊的寄存器叫 SCB，这个寄存器隶属于 NVIC 部分，由此可以推知它肯定和中断、异常有关，那么这个寄存器叫做系统控制块，这里面会存储一些和 CPU 信息，以及各种中断有关的信息，我们主要需要集中在 `SCB->HFSR`, `SCB->CFSR` 这两个寄存器，前者告诉我们 HardFault 相关的信息，后者则告诉我们 MemManageFault, BusFault 等的配置信息，如果需要深度调试 HardFault, 则看 SCB 的这几个寄存器几乎是必不可少的环节。

首先看 0xE000ED2C，先确认一下 HFSR 是个什么情况，看到值又是 0x40000000，依然非常熟悉的 FORCED 置位，说明真凶另有他人，接下来让我们看到 CFSR(0xE000ED28)，
发现值为 0x0100 0000，查阅内核手册即可得知对应 Bit 24 被置位（即 UNALIGNED 标志位），这说明以未对齐的方式访问了内存。

最后是 signal handler 返回的是 0xFFFFFFF1 （顺带说一句，正常情况下应当为 0xFFFFFFFD, 在你完整看完了理论部分自然就会知道为什么，这里由于不是很重要，就不讲了），对应 `EXC_RETURN` 的值为这个，我们会在[附录](#exc_return)讲解 `EXC_RETURN`，把之前搭建 Zig 环境的坑填上。

由于理论部分过长，我将其放在 debug 主线之外，作为[补充资料](#理论知识)

那么调用堆栈里面有 `PendSV_Handler`，我们自然而然需要研究一下这个函数:

### FreeRTOS 内的 PendSV 实现

首先先给不知道 PendSV 是什么的读者做个基本介绍（详细讲解请参考[附录](#什么是-pendsv)），这个异常是 Cortex-M4 为了支持 OS 的上下文切换而特地造出来的一个异常（另一个有些相似的是 SVC 异常，这里不介绍）。

由于发生中断时不宜进行上下文切换（否则中断不能得到及时响应），因此 Cortex-M4 内核让 PendSV 负责了上下文切换的任务，我们在实际使用时将其优先级调到最低，就可以保证先响应中断，再进行上下文切换。

让我们定位到 port.c 的这个函数：

```c
void xPortPendSVHandler( void )
```

其中：

```c
void xPortPendSVHandler( void ) __attribute__ (( naked ));
```

需要注意，宏定义里面加入了 `__attribute__((naked))`， 这说明该函数是裸函数(即告诉编译器自己手动管理堆栈，不要替自己优化)，只能内联汇编。

由于 FreeRTOS 这部分的实现还涉及到一些不相关的内容（比如 `isb`, `dsb`），这些感兴趣的自行查阅，我们只抽出最重要的部分，此外让我们假设我们不使用 FPU （~~从而又能少讲几条指令，好耶！~~）

让我们来详细讲解一下 FreeRTOS 的 PendSV_Handler 都做了什么

#### 保存任务 A 的上下文（压栈）

下面这部分代码就是保存任务 A 上下文的代码：

```asm
/* mrs 表示将栈指针地址读到寄存器 */
mrs r0, psp

/* ldr: load register, 即加载数据到寄存器 */
ldr r3, pxCurrentTCBConst
/* 比如这一句就等价于 r2 = r3[0] */
ldr r2, [r3]

/* 
 * stmdb: 存储多个寄存器的值到内存中, 与 ldmia 配对
 * 如果说 stmdb 是 push 的话，那么 ldmia 就是 pop
 * 所以下面这一句是把 r4~r11, r14 的值写到 r0 对应的内存中
 *
 * 这一句是 AAPCS 规范的要求，可以查看附录
 * 或者你只需要记住 AAPCS 规范就是函数调用的时候，
 * 要先行把这么几个寄存器存到栈中
 */
stmdb r0!, {r4-r11, r14}
/* str: store register, 即将寄存器的值存入内存 */
str r0, [r2]
```

第一条指令将 psp 的值读取到 r0 中，而第二句和 `pxCurrentTCB` 这个变量有关(见 tasks.c 文件)：

```c
/*lint -save -e956 A manual analysis and inspection has been used to determine
which static variables must be declared volatile. */
PRIVILEGED_DATA TCB_t * volatile pxCurrentTCB = NULL;
```

第二句实际上表示将 `pxCurrentTCB` 赋值给 r3，而因为这个变量是个指针，因此就需要解引用，而这就是第三句干的事情了，此外十分注意， `ldr r2, [r3]` 实际上表示的是拿出 r3 寄存器对应内存中的第一个值，并赋值给 r2，那么第一个值是什么呢，让我们来看：

```c
/*
 * Task control block.  A task control block (TCB) is allocated for each task,
 * and stores task state information, including a pointer to the task's context
 * (the task's run time environment, including register values)
 */
typedef struct tskTaskControlBlock 			/* The old naming convention is used to prevent breaking kernel aware debuggers. */
{
	volatile StackType_t	*pxTopOfStack;	/*< Points to the location of the last item placed on the tasks stack.  THIS MUST BE THE FIRST MEMBER OF THE TCB STRUCT. */
	/* ...... */
} tskTCB;
```

现在知道为什么任务控制块 TCB 还要特地声明必须要把 `pxTopOfStack` 放在结构体的第一个变量了吧？

```c
	volatile StackType_t	*pxTopOfStack;	/*< Points to the location of the last item placed on the tasks stack.  THIS MUST BE THE FIRST MEMBER OF THE TCB STRUCT. */
```

本质上来说其实就是因为这两句以及其他类似的操作。

然后 `stmdb r0!, {r4-r11, r14}`, `str r0, [r2]` 就没什么好说的了，前一句就是把相关的寄存器存到任务 A 的堆栈去，第二句就是把新的栈指针存回去。

#### 切换任务上下文

下面这一段是切换任务上下文的代码：

```asm
stmdb sp!, {r0, r3}
/* ... */
bl vTaskSwitchContext
/* ... */
ldmia sp!, {r0, r3}
```

这一段实际上就是一个完整的汇编代码调用函数的标准流程: prologue(压栈，设值) => body(调用) => epilogue(出栈，复原)

因此我们只看 `bl vTaskSwitchContext`，这个函数位于 tasks.c

```c
void vTaskSwitchContext( void )
{
    /* ... */
    /* 如果你配置了我上篇文章说的宏的话，这里就会正常调用 */
		taskCHECK_FOR_STACK_OVERFLOW();
		/* ... */
		
		/* Select a new task to run using either the generic C or port
		optimised asm code. */
		taskSELECT_HIGHEST_PRIORITY_TASK(); /*lint !e9079 void * is used as this macro is used with timers and co-routines too.  Alignment is known to be fine as the type of the pointer stored and retrieved is the same. */
		/* ... */
}
```

我们只需要看 `taskSELECT_HIGHEST_PRIORITY_TASK()`，这个宏函数的实现，其中一句是：

```c
		listGET_OWNER_OF_NEXT_ENTRY( pxCurrentTCB, &( pxReadyTasksLists[ uxTopPriority ] ) );		\
```

对应：

```c
#define listGET_OWNER_OF_NEXT_ENTRY( pxTCB, pxList )										\
{																							\
List_t * const pxConstList = ( pxList );													\
	/* Increment the index to the next item and return the item, ensuring */				\
	/* we don't return the marker used at the end of the list.  */							\
	( pxConstList )->pxIndex = ( pxConstList )->pxIndex->pxNext;							\
	if( ( void * ) ( pxConstList )->pxIndex == ( void * ) &( ( pxConstList )->xListEnd ) )	\
	{																						\
		( pxConstList )->pxIndex = ( pxConstList )->pxIndex->pxNext;						\
	}																						\
	( pxTCB ) = ( pxConstList )->pxIndex->pvOwner;											\
}
```

因此 `pxCurrentTCB` 会通过 `vTaskSwitchContext` 自动更新，回到 PendSV 之后，这里就是一个等待执行的任务了。

#### 找到任务 B 的栈

下面这一段负责找到任务 B 的栈

```asm
ldr r1, [r3]
ldr r0, [r1]
```

没什么好说的，记住 `r3 = pxCurrentTCB` 即可，所以 r0 是任务 B 的栈顶指针。

#### 加载任务 B 的上下文（出栈）

这个相对简单些：

`ldmia sp!, {r4-r11, r14}`

本质上来说就是

`pop {r4-r11, r14}`

然后再设置一下 `psp` 为任务 B 的栈指针(`msr psp, r0`)，执行 `bx r14` 以从异常中返回，并把控制权交还给任务 B 即可。

## 本次 HardFault 的因果链

有了这些前置的知识，我们可以来 debug 了。

由于之前提到的错误是 UNALIGNED, 所以我们只要锚定与内存访问有关的函数和指令即可

由于我们已经有了调用堆栈，所以我们可以知道栈溢出错误是在切换上下文的时候产生的，那么自然就是找 PendSV 处理函数的问题，由于我在调试的时候已经定位到是 `ldmia r0!, {r4-r11, r14}` 的问题了，那么就让我们打断点到改变了 r0 寄存器值的指令吧！

因此断点设置在 `ldr r0, [r1]`，这一步意味着从 `pxCurrentTCB` 中拿到 `pxTopOfStack` 的值。

注意观察左侧 Register 栏的 r0 和 r1：

![Step1][调试过程Step1]

目前是很正常的。

![Step2][调试过程Step2]

但是到了这里：

![Step3][调试过程Step3]

看, r3 是正常的，但是 r2 和 r1 变成了 0x1fff, 这说明在程序运行过程中，有指令将 `pxCurrentTCB` 对应的值修改为 0x1fff，进而导致 r0 从 r1 读出了垃圾值 0x3027b46，由于这个值不是 4 字节对齐的，最后使用 `ldmia` 对 UITask 的栈进行访问的时候就理所当然地炸掉了。

那么假如说，我们想找到是谁修改的呢？也很简单，借助 GDB 即可，为了确认是谁修改了这玩意对应的值，我们需要先找到它在内存中对应的地址，那么：

```shell
(gdb) p &pxCurrentTCB
$1 = (TCB_t * volatile *) 0x20004630 <pxCurrentTCB>
```

然后我们将断点打在 `StartUITask` 的 `os_delay_ms(500)`，等运行到那里之后，再输入：

```shell
# 这一句表示当 *pxCurrentTCB == 0x1fff 时，暂停运行代码
(gdb) watch *(uint32_t *)0x20004630 if *(uint32_t *)0x20004630 == 0x1fff
Hardware watchpoint 1: *(uint32_t *)0x20004630
```

然后继续运行，接下来我们可以看到：

![是谁修改了pxCurrentTCB对应的值][是谁修改了pxCurrentTCB对应的值]

注意我这里为什么标了 r2, 是因为一开始保存任务 A 的上下文的时候有一句 `ldr r2, [r3]`，说明 r2 实际上就是 `*pxCurrentTCB`，所以很明显值是在 `bl vTaskSwitchContext` 中被修改的， GDB 执行完这一句之后发现值被修改，于是停了下来，正好落在把 r0 清零的汇编上。

或者更具体些（复现方法是在 `bl vTaskSwitchContext` 中打断点，运行到之前的 `os_delay_ms(500)` 之后，第一次 `bl` 是正常的，我们继续运行，第二次 `bl` 就需要进去这个函数里面看是哪里被修改了）：

![调试过程-找出哪一句修改了pxCurrentTCB对应的值](../images/调试过程-找出哪一句修改了pxCurrentTCB对应的值.png)

这三句是“帮凶”：

```asm
/* 将原本 r3[4] 的值重新给 r3 */
ldr r3, [r3, #4]
/* 将 r3[12] 的值（被篡改成 0x1fff）取出并放到 r2 */
ldr r2, [r3, #12]
/* 将 pxCurrentTCB 存到 r3 */
ldr r3, [pc, #28]
/* 将当前 r2 的值更新到 r3[0] (pxTopOfStack) */
str r2, [r3, #0]
```

执行完这三句之后我们可以看到确实是被修改了的：

![调试过程-GDB验证pxCurrentTCB值是否被修改](../images/调试过程-GDB验证pxCurrentTCB值是否被修改.png)

进而导致下面的 `ldmia` 因为垃圾值内存没对齐而炸掉。

然后我们再来找一下真凶，由于值在 `r3[12]`， 而 r3 此时为 `0x200014cc`（执行 `ldr r3, [r3, #4]` 那一句时），于是我们重新开始调试，并在 GDB 中输入：

```shell
(gdb) watch *(uint32_t *)(0x200014cc+0x0c) if *(uint32_t *)(0x200014cc+0x0c) == 0x1fff
```

然后你会发现 LVGL 库里的这一句被反复暂停：

![是谁改变了pxCurrentTCB对应的值][是谁改变了pxCurrentTCB对应的值]

接下来执行 `os_delay_ms(500)`，然后两次 `bl vTaskSwitchContext` 之后“帮凶”成功把“鬼子领进村了”。

（第一次 `bl vTaskSwitchContext` 并进去之后，看上面提到的四句，r3 的值为 0x20000d94, 而第二次 `bl` 则为 0x200014cc）。

我们再来看一开始写的 Hook 函数，现在我们做如下修改：

```c
#include "FreeRTOS.h"
#include "task.h"
/* 临时暴露 TCB 结构体定义以访问栈指针 */
struct tskTaskControlBlock {
    volatile StackType_t *pxTopOfStack;
    ListItem_t xStateListItem;
    ListItem_t xEventListItem;
    UBaseType_t uxPriority;
    StackType_t *pxStack;
};

void vApplicationStackOverflowHook(TaskHandle_t xTask,
                                   char *pcTaskName)
{
    int32_t stack_usage =
        (uint32_t)xTask->pxTopOfStack - (uint32_t)xTask->pxStack;
    int32_t diff = stack_usage - 512;
    printf("Stack overflow in task %s\r\n"
           "=> [pxTopOfStack: %p]\r\n"
           "=> [pxStack: %p]\r\n"
           "=> [pxTopOfStack - pxStack: %d,\tdiff: %d\tlt: %s]\r\n"
           "=======================\r\n",
           pcTaskName, xTask->pxTopOfStack, xTask->pxStack,
           stack_usage, diff,
           diff > 0 ? "TRUE" : "FALSE");

    if (strcmp(pcTaskName, "UITask") == 0) {
        gpio_write(&usr_led, GPIO_Level_High);
    }
}
```

并且此时让我们将 `configCHECK_FOR_STACK_OVERFLOW` 改成 2,以获得更高的测量精度：

```c
#if( ( configCHECK_FOR_STACK_OVERFLOW > 1 ) && ( portSTACK_GROWTH < 0 ) )

	#define taskCHECK_FOR_STACK_OVERFLOW()																\
	{																									\
		const uint32_t * const pulStack = ( uint32_t * ) pxCurrentTCB->pxStack;							\
		const uint32_t ulCheckValue = ( uint32_t ) 0xa5a5a5a5;											\
																										\
		if( ( pulStack[ 0 ] != ulCheckValue ) ||												\
			( pulStack[ 1 ] != ulCheckValue ) ||												\
			( pulStack[ 2 ] != ulCheckValue ) ||												\
			( pulStack[ 3 ] != ulCheckValue ) )												\
		{																								\
			vApplicationStackOverflowHook( ( TaskHandle_t ) pxCurrentTCB, pxCurrentTCB->pcTaskName );	\
		}																								\
	}
```

上面这段代码就表示是使用一段固定值填充栈的起始地址（详情请查阅 `prvInitialiseNewTask` 函数的宏 `tskSET_NEW_STACKS_TO_KNOWN_VALUE`），然后每次切换任务（`vTaskSwitchContext`）时，检查这些值和原本的已知值相符，如不相符，则说明发生了栈溢出。

打印信息例子如下：

```
Stack overflow in task UITask
=> [pxTopOfStack: 0x200018dc]
=> [pxStack: 0x20001528]
=> [pxTopOfStack - pxStack: 948,        diff: 436       lt: TRUE]
=======================
Stack overflow in task UITask
=> [pxTopOfStack: 0x20001a54]
=> [pxStack: 0x20001528]
=> [pxTopOfStack - pxStack: 1324,       diff: 812       lt: TRUE]
=======================
```

其中 `pxStack` 表示栈起始地址， `pxTopOfStack` 表示栈最后一个元素所在地址，lt 表示实际栈空间大小是否超出我们预设的 512 Words

我们来统计一下栈溢出并且栈指针完全越界的次数，我总共测试了三次：

```bash
> cat StackOvf.txt | grep "TRUE" | wc -l
590
> cat StackOvf.txt | grep "FALSE" | wc -l
64
```

所以被覆写的概率是非常非常大的，因此到这里基本就可以推断是栈溢出 => pxCurrentTCB->pxTopOfStack 被覆写 => PendSV 切换任务时读取了非法的栈指针，并尝试通过 `ldmia` 加载相关寄存器的值 => 触发内存未对齐访问 => 导致 UsageFault => 重定向为 HardFault.

最后再说一句无关紧要的，只要在初始化时加入 `SCB->SHCSR |= (1 << 18U);`，就会变成触发 UsageFault 了，不过我不是很理解为什么这些异常居然默认不打开，而是重定向到 HardFault，我想这可能得问 Cortex 内核的设计人员了。

## 总结

这次其实确确实实暴露了我对 FreeRTOS 了解不深的严重问题，也正好和我的初心相符了，算是一种求锤得锤吧（笑）。**如果遇到了什么玄学问题，首先看下堆栈啥的正不正常，这也许是各种 RTOS 在调试时先要确认的一步吧**！此外还因为这个 BUG 而被迫看了 FreeRTOS 的代码，说实话写得还真的非常漂亮，仿佛在欣赏一件艺术品，已严肃偷师（笑）。

## 理论知识

### 前言

在本附录中我们将会讲解 Cortex-M4 的内核设计和错误机制处理，这有助于你理解第二篇文章的大部分内容。

以下内容均选自《Arm Cortex-M3 与 Cortex-M4 权威指南》，下称《权威指南》，需要注意的是部分整合了个人的理解，因此下面的内容只能当作二道贩子兜售的二手知识，想要有更详细的理解，还是需要看原著，并且自己亲手调一遍 HardFault，这些知识才会是你的。

### 编程模型

M4 内核的编程模型是 “二二二” 模型：
- 两种操作状态：调试状态；Thumb 状态
- 两种模式：处理模式(Handler)；线程模式(Thread)
- 两种权限：特权级；非特权级（下称用户级）

![操作状态和模式][操作状态和模式]

这篇文章中我们重点关注模式和权限：

线程模式是处理器正常执行代码时对应的模式，而处理模式是处理器进入异常/中断时的处理模式；处理器一般默认在特权级状态运行，但是当有 RTOS 时，执行任务则一般以用户级模式运行程序。

软件可以把自身从特权级切换到用户级，但是要想切回来就必须借助异常机制。

通过区分特权级和用户级，我们实际上可以实现对一些关键资源的保护，以及提供一个基本的安全模型。

### 栈设计与影子栈

我们都知道，程序运行时需要使用栈存储局部变量，由此就需要用到栈指针，一般来说一个栈指针就已经足够了，但是 Cortex-M3/4 内核为了方便嵌入式 OS，特地设置了两套栈指针：

- 主栈指针 MSP
- 进程栈指针 PSP

之所以叫影子栈，我想是因为对于一般程序来说，在同一时刻不能同时见到两个栈指针，从而也就实现了一个栈为另一个栈的“影子”，看不见也摸不着（《权威指南》并没有说为什么叫影子栈，但提到了同一时刻无法同时看到两个栈）。

在不使用 RTOS 的情况下，程序只需要主栈指针即可，但是在使用 RTOS 的情况下，则需要进程栈指针，分开设置两套栈指针的目的，实际上是为了安全和高效：

- 在有 RTOS 时，内核使用主栈，而任务使用进程栈，这样在某个任务的任务栈溢出之后，内核和其他任务的栈不会受到影响（栈空间的分配不是连续的，因此不太可能覆写其他栈）。
- 每个任务只需要满足自身栈的最大需求和从上下文切换中保存的上一级栈的栈帧，以及一些特定的条件即可，不需要考虑 ISR 和嵌套中断的栈，这就使得静态分配栈空间成为可能。
- OS 还能借助存储器保护单元（MPU）还能访问某个栈区的任务，同时在它有栈溢出风险时，MPU 可以触发 `MemManageFault` 并避免栈空间以外的存储区域被覆盖。

### 异常处理机制

#### 什么是异常

《权威指南》中对于异常的定义为：改变程序流的事件。当异常发生时，处理器会暂停当前任务，并转而去执行一段被称为异常处理的程序，在执行完毕之后回到之前的任务。

其实从上面的叙述中，你已经可以把异常和中断画个约等号了（中断实际上就是异常的一种），我们在初学中断的时候也是这么一个定义方法，不过这段程序被称为中断服务程序(ISR).

然而，我们常见~~且可恨~~的 `HardFault` 实际上不是中断，而是系统异常（《权威指南》 Chapter 4.5.1, P74）。而对应的 `Handler` 里面的程序是异常处理程序，而非 ISR。

对于 Cortex-M3/4 内核来说，有几个异常为错误处理异常，处理器检测到错误时则会触发这些异常，比如 `HardFault`, `UsageFault`, `MemManageFault`, `BusFault`，然而我们总是见到 `HardFault` 的死循环，而不是其他的，这是为什么呢？实际上内核的缺省行为是只使能了 `HardFault`，从而其他所有的错误处理异常都会被重定向到 `HardFault` 中，而在内核中 `HardFault` 有一块专门的寄存器 `SCB->HFSR`，这个寄存器的第 31 位 (`FORCED`) 会告诉你 `HardFault` 是不是由其他错误重定向而来。基本上 75% 以上的情况，我们都能认为 `HardFault` 是被重定向而来的(其自身发生的概率仅为 25%，如果只算理论值的话)。

### 异常全流程

异常全流程按照时间线可以分为：接受异常请求，异常进入，异常处理和异常返回。

我们不会讲得特别详细，我只需要保证你有个基本概念就行，详细的请看《权威指南》。

我们在学怎么给单片机开中断的时候都学到过，要使能并开启中断，对于异常来说也是同理的，首先是异常请求，要触发内核的异常事件，你得保证异常被使能了，没被屏蔽且优先级高于当前执行的任务，此时内核才会去处理异常。而后就会进入异常，这里也和中断相似，首先我们要保存好当前任务的上下文（比如 PC 和一些寄存器），方便到时候处理完了回来还能找的到路，然后再从向量表中取出异常向量，准备去执行异常处理函数，并且更新和异常等相关的寄存器。完成了这些步骤之后就会正式开始处理异常，此时 MCU 会保证自己运行在特权模式，且使用 MSP 操作栈（这里我们不考虑异常嵌套）。最后到了异常返回时，MCU 会把一个叫做 `EXC_RETURN` (exception return) 的特殊值存进 LR 里面，当这个值被某个允许的异常返回指令写入 PC 时，就会触发异常返回流程。

### `EXC_RETURN`

然后我们来说道说道这个东西。

它实际上就是一个普通地不能再普通的值了，只是说起到了一些对于当前处于什么运行状态的指示作用。
这个值在使用一些特定指令加载到程序计数器 PC 时，比如 BX, POP 或者 LDR/LDM ，就会触发异常返回机制。

![EXC_RETURN][EXC_RETURN]

看到这张图，我想你应该会知道为什么我之前说正常情况下应该返回 `0xFFFFFFFD`，因为我没用 FPU, 且我在任务调度，所以下一刻应该换任务，这就要求必须切换到 PSP 和线程模式，所以对应就只能是这个值了。

有了这些前置知识，我想你应该可以自己分析这张图：

![异常返回全流程][异常返回全流程]

### 什么是 OS，什么是 RTOS

这部分是我个人的拙见，如有错误还请多多海涵，不吝赐教。

OS，按照教科书上的定义，指的是管理计算机硬件与软件资源的程序。打个比方，就好比你去餐厅吃饭，跟服务员点菜，服务员负责把订单交给厨师，此外服务员同时负责维持餐厅的秩序，这里服务员就承担着类似于 OS 的角色，服务员既要和厨师（底层硬件）交流，又要负责安排客人入座，维持现场秩序（管理各种资源）。

而 RTOS 则相对而言更加简化一些，它依然需要管理各种资源，也依然需要和底层硬件沟通，但是 RTOS 则更加侧重于 RT (Real-Time)，就好比一个火爆的大餐厅，服务员不一定顾得上你，而一个苍蝇馆子，服务员则更能快速给你上菜。

广义上来说，一个 OS 可以分为三个组成部分：内核、 Shell，和一些杂七杂八的软件。但对于一个 OS 来说，**最核心的概念实际上是内核**，也就是**负责线程/进程/任务管理、内存管理、驱动管理等的程序**。这也是为什么 Linux 有那么多发行版，但它们仍然是 Linux； 各种 RTOS 虽然复杂程度不一（FreeRTOS 只有非常轻量的内核，RTT 和 Zephyr 可以有宛如 Linux 般的 dts 等复杂驱动适配），但它们都提供了任务创建、调度、信号量、各种锁，因为 OS 的核心与灵魂就是内核，而 RTOS 则是任务调度.

因此理解 RTOS 的内核在干些什么，就理解了 RTOS 在干什么。不过我们在这里不会讲调度算法，而是会更加侧重于 OS 的启动流程和上下文切换逻辑。我们将在下面介绍 Cortex-M 内核用于支持嵌入式 OS 的两个异常，并由此引出启动流程和上下文切换的逻辑。

### 什么是 PendSV

PendSV 中文名为可挂起的系统调用，其挂起状态可以在更高优先级的异常处理内设置，并且还会在高优先级处理完之后才执行，也就是说，只要将这玩意的优先级设置为最低，就可以让 PendSV 在其他中断任务搞定之后再执行，由于此特性，该异常对于上下文切换十分有用，这也是嵌入式 OS 设计的关键所在。

由于上下文切换属于任务调度，而任务调度属于 OS 内核，那么就得说下内核是个什么情况。

OS 内核的执行可由下面条件触发：
- 应用任务中 SVC 指令的执行（当应用在等一批数据或因为一些情况被耽搁，可以调用系统服务以向内核申请切换任务）
- 周期性的 SysTick 异常

一般来说， OS 会在 SysTick 触发时决定要换到什么任务去，但是如果此时发生了中断请求（IRQ），则 OS 不应当执行上下文切换，否则会导致 IRQ 处理延迟。

那么可以设想一下如果没有 PendSV 会是个什么情况：

没有 PendSV，那么就意味着发生 IRQ 时，你必须要优先处理 IRQ，且避免上下文切换，这看起来似乎很容易就解决了，然而当 IRQ 和 SysTick 发生频率相近呢？此时就会导致这两个中断“共振”——本来我在 SysTick 要换任务的，给你一搞结果啥都干不了。这会影响系统的性能。

然而，有了 PendSV 之后，我大可以在 PendSV 里面去处理，如果发生了 IRQ，那就先处理 IRQ，处理到最后再执行 PendSV， 这就保证了上下文虽然可能比较晚切换，但是一定能切换。

![PendSV-IRQ 上下文切换示例][PendSV-IRQ 上下文切换示例]

由于 PendSV 负责切换上下文，那么就必然涉及到任务的上下文保存和上下文加载，也就是说，对于准备切换的任务 A， PendSV 需要将它的寄存器值保存下来，而对于即将运行的任务 B，PendSV 需要恢复它的寄存器值，保存什么，恢复什么，都遵循 AAPCS 规范。

因此 PendSV 要做的事情简单点说就可以概括为四步：

1. 保存任务 A 的上下文（压栈）
2. 决定要切换成哪个任务（这里假定为任务 B）
3. 找到任务 B 的栈
4. 加载任务 B 的上下文（出栈）

然后把主动权还给任务 B 即可 (`bx r14`)。

![PendSV 上下文切换][PendSV 上下文切换]

[FreeRTOS 内的代码](#freertos-内的-pendsv-实现)

### AAPCS 规范

AAPCS 规范，即 ARM 架构过程调用标准，它规定了编译器生成的汇编代码对 CPU 的操作约定（手写汇编最好也要遵守）。

该标准允许 C 函数修改 R0～R3，R12，R14（LR） 以及 PSR，如果要修改 R4～R11，则应当将这些寄存器保存到栈中，并在函数结束前将其恢复。

R0~R3, R12, LR, PSR 为“调用者保存寄存器”，若在函数调用后还需要使用这些寄存器的数值，就必须先行保存到内存中，而 R4~R11 为“被调用者保存寄存器”，被调用的子程序/函数需要保证这些值在函数结束时不会发生变化，不过在执行时可以发生变化，只是要确保函数返回时要恢复为初始值。

对于浮点单元也是类似的，这里不讲。

一般来说函数调用将 R0～R3 作为输入参数，R0 用作返回结果，若返回值为 64 位则 R1 也作为返回结果。

特别的，当处理异常时，返回地址（PC）的数值并不存在 LR, LR 存 `EXC_RETURN`，因此异常流程需要自行保存返回地址，也就是说此时异常处理需要保存八个寄存器（没有或者不启用 FPU 时），分别为 r4~r11, r14(lr)。

---

<p style="display:none">图片链接：</p>

[EXC_RETURN]: /images/freertos-stackovf/EXC_RETURN.png
[PendSV-IRQ上下文切换示例]: /images/freertos-stackovf/PendSV-IRQ上下文切换示例.png
[PendSV上下文切换]: /images/freertos-stackovf/PendSV上下文切换.png
[异常返回全流程]: /images/freertos-stackovf/异常返回全流程.png
[操作状态和模式]: /images/freertos-stackovf/操作状态和模式.png
[是谁修改了pxCurrentTCB对应的值]: /images/freertos-stackovf/是谁修改了pxCurrentTCB对应的值.png
[是谁改变了pxCurrentTCB对应的值]: /images/freertos-stackovf/是谁改变了pxCurrentTCB对应的值.png
[调试过程-GDB验证pxCurrentTCB值是否被修改]: /images/freertos-stackovf/调试过程-GDB验证pxCurrentTCB值是否被修改.png
[调试过程-找出哪一句修改了pxCurrentTCB对应的值]: /images/freertos-stackovf/调试过程-找出哪一句修改了pxCurrentTCB对应的值.png
[调试过程Step1]: /images/freertos-stackovf/调试过程Step1.png
[调试过程Step2]: /images/freertos-stackovf/调试过程Step2.png
[调试过程Step3]: /images/freertos-stackovf/调试过程Step3.png

<p style="display:none">参考资料：</p>

[STM32 Cortex M4 编程手册]: https://www.st.com/content/ccc/resource/technical/document/programming_manual/6c/3a/cb/e7/e4/ea/44/9b/DM00046982.pdf/files/DM00046982.pdf/jcr:content/translations/en.DM00046982.pdf
[ARM CM3/4 异常进入与返回的过程]: https://shequ.stmicroelectronics.cn/thread-604515-1-1.html
[RTOS 基础知识]: https://wwww.freertos.org/zh-cn-cmn-s/Documentation/01-FreeRTOS-quick-start/01-Beginners-guide/01-RTOS-fundamentals
[深入理解操作系统的概念及定位]: https://www.cnblogs.com/kevinbee/p/18678187
[FreeRTOS 堆栈使用和堆栈溢出检查]: https://w.freertos.org/zh-cn-cmn-s/Documentation/02-Kernel/02-Kernel-features/09-Memory-management/02-Stack-usage-and-stack-overflow-checking
[RTOS Ref]: https://www.zhihu.com/question/593816921/answer/1957909257069520682?share_code=14IBMqKlAx7yM&utm_psn=2050173737027236484
