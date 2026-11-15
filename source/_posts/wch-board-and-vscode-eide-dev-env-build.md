---
title: "【WCH】在沁恒微电子申请的板卡初体验及Vscode+EIDE环境的搭建方案"
date: 2025-07-22 13:58:00
categories:
- 技术
tags:
- embedded
- mcu
- c
- vscode
- eide
---

# 零、前言

这个暑假在B站UP主Expert电子实验室那里偶然了解到WCH（沁恒微电子）有免费的板卡样品申请活动，只需要自己付运费就行，抱着试一试的心态，我就去申请了，然后前几天到货之后开发了几个简单的程序体验了一下，不过当时使用的是人家基于Vscode改的MounRiver Studio，然后通过邮件方式和人家反应了一些问题，以及询问是否有配置Vscode的方案（官方的MRS砍掉了Vscode的插件功能，这点我不太喜欢），他们的工程师回复说没有，于是我打算自己踩踩坑（其实也没多少）写一篇配置教程出来。

另附带一些链接：

- [沁恒微电子官网][沁恒微电子官网]
- [样品申请途径][样品申请途径]
- [CHV203相关介绍及SDK][CHV203相关介绍及SDK]

希望各位能够多多支持国产芯片，WCH的SDK和ST的标准库大差不差，如果你学过标准库，那很快就能上手。

<!-- more -->

# 一、前置知识

本篇文章基于WCH的V203C8T6评估板，配置Vscode+EIDE的开发教程

WCH评估板的实物图如下，配置成功的实验现象为使用跳线连接的LED灯闪烁（接线为PA0到LED端口）

![1][1]

下面是WLinkE的实物图，靠近串口的那里有两个LED灯，D1为红灯，D2为蓝灯，这个是指示当前烧录模式为RISCV还是ARM的指示灯

![2][2]

下面是指示灯对应的烧录模式

![3][3]

此外我发现WLinkE貌似默认是ARM模式，我们需要切换成RISCV模式，这个需要借助WCH-LinkUtility软件来实现。

# 二、配置过程

我们可以直接去移植现有的模板，在EIDE里面对应的是移植远程模板：

![4][4]

接下来我们需要选择裸机模板，然后输入WCH，接下来选择RISCV，找到CHV203的模板，然后新建就可以了。

如果不使用MounRiver Studio（MRS）提供的RISC-V GCC也可以，但沁恒自研的青稞RISC-V貌似有一些自定义的指令，为了支持这些指令，需要选择MRS的RISC-V GCC

具体方法是选择EIDE插件，然后选择设置工具链，再将RISC-V GCC配置到对应的路径即可，需要注意的是，EIDE会帮你解析目录，切换到bin文件夹去，所以你只需要：

/path/to/you/RISCV GCC，而不需要/path/to/you/RISCV GCC/bin

这步由于我没找到EIDE临时配置的文件，所以上面的方法是配置全局的RISC-V GCC的，不过应该是可以开发其他的RISC-V芯片的，后面想还原回默认设置的话也很简单。

我的RISC-V GCC就在 E:\MounRiverStudio_RISC-V_IDE\MounRiver_Studio2\resources\app\resources\win32\components\WCH\ToolChain 底下，此外WCH根目录下还有一些其他的工具，比如我们等会要用到的WCH-LinkUtility软件，对了，MounRiverStudio_RISC-V_IDE是我自定义的安装目录，你们自己指定安装目录的话可能不是这个

OpenOCD官方不支持WLinkE，需要使用MRS的OpenOCD

为了避免在EIDE中直接修改OpenOCD配置会影响其他设备的烧录，我们选择修改download.cmd为如下内容：

```cmd
set HEXFILE=%1

set "HEXFILE=%HEXFILE:\=/%"

path/to/your/openocd.exe -f ./tools/wch-target.cfg -c init -c halt -c "flash erase_sector wch_riscv 0 last " -c "program %HEXFILE%" -c "verify_image %HEXFILE%" -c wlink_reset_resume -c exit
```

不过这样子会增加一些依赖（需要你对应目录下真的有这个OpenOCD才能跑），如果只是本机开发的话这样子搞没问题，多人协作的话最好还是慎重。

WLinkE要切换RISC-V模式的话，需要使用WCH-LinkUtility软件，你可以通过打开MRS -> 选择工具 -> 选择WCH-Link本地烧录工具 -> 设置当前WCHLink模式 来打开软件，也可以直接定位到人家的可执行文件的目录，我这里是：E:\MounRiverStudio_RISC-V_IDE\MounRiver_Studio2\resources\app\resources\win32\components\WCH\Others\SWDTool\default\WCH-LinkUtility.exe。

# 三、构建时的一些小问题

默认情况下是可以直接构建的，但是会有Warning

```cmd
./src/main.c: In function 'main':
./src/main.c:59:24: warning: format '%d' expects argument of type 'int', but argument 2 has type 'uint32_t' {aka 'long unsigned int'} [-Wformat=]
     printf("SystemClk:%d\r\n", SystemCoreClock);
                       ~^       ~~~~~~~~~~~~~~~
                       %ld
./src/main.c:60:24: warning: format '%x' expects argument of type 'unsigned int', but argument 2 has type 'uint32_t' {aka 'long unsigned int'} [-Wformat=]
     printf( "ChipID:%08x\r\n", DBGMCU_GetCHIPID() );
                     ~~~^       ~~~~~~~~~~~~~~~~~~
                     %08lx
```

这个是由于Github的模板中main.c文件导致的，这个可以随便改，SystemCoreClock是uint32_t类型的，DBGMCU_GetCHIPID()的返回类型也是uint32_t，所以需要夹一个l

core_riscv.h的一些问题：Github克隆下来的模板是：__asm volatile ("wfi");，但我发现有的EVT是asm volatile ("wfi");，如果使用这种EVT会导致报错：

```cmd
sdk/Core/core_riscv.h: In function '__WFI':
sdk/Core/core_riscv.h:291:3: error: 'asm' undeclared (first use in this function)
   asm volatile ("wfi");
   ^~~
```

我转到__asm里面去看了，发现其实是有宏定义的：#define __asm asm，所以其实理论上来说写成asm应该也不影响，但是就是会报错，因为用旧版的core_riscv.h也没啥问题，所以这个问题可以不管。此外我发现Github的模板也不算特别老，所以可以不需要下载人家的EVT SDK再替换。

# 四、程序代码

和STM32的标准库其实很像，如果你学过标准库的话基本可以无痛迁移WCH的板卡，这点可以放心。也希望大家能够支持国产芯片，助力国产芯片的发展。

```c
/********************************** (C) COPYRIGHT *******************************
 * File Name          : main.c
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2021/06/06
 * Description        : Main program body.
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/

/*
 *@Note
 *GPIO routine:
 *PA0 push-pull output.
 *
 */

#include "debug.h"

/* Global define */

/* Global Variable */

/*********************************************************************
 * @fn      GPIO_Toggle_INIT
 *
 * @brief   Initializes GPIOA.0
 *
 * @return  none
 */
void GPIO_Toggle_INIT(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
}

/*********************************************************************
 * @fn      main
 *
 * @brief   Main program.
 *
 * @return  none
 */
int main(void)
{
    u8 i = 0;

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    SystemCoreClockUpdate();
    Delay_Init();
    USART_Printf_Init(115200);
    printf("SystemClk:%ld\r\n", SystemCoreClock);
    printf( "ChipID:%08lx\r\n", DBGMCU_GetCHIPID() );
    printf("GPIO Toggle TEST\r\n");
    GPIO_Toggle_INIT();

    while(1)
    {
        Delay_Ms(250);
        GPIO_WriteBit(GPIOA, GPIO_Pin_0, (i == 0) ? (i = Bit_SET) : (i = Bit_RESET));
    }
}
```

---

[沁恒微电子官网]: http://www.wch.cn/
[样品申请途径]: https://www.wch.cn/services/request_sample.html
[CHV203相关介绍及SDK]: https://www.wch.cn/products/CH32V203.html

[1]: /images/wch-board-and-vscode-eide-dev-env-build/01.jpg
[2]: /images/wch-board-and-vscode-eide-dev-env-build/02.jpg
[3]: /images/wch-board-and-vscode-eide-dev-env-build/03.png
[4]: /images/wch-board-and-vscode-eide-dev-env-build/04.png

