---
title: "【STM32】当环形队列遇上串口通信"
date: 2025-07-11 20:50
categories:
- 技术
tags:
- embedded
- mcu
- c
- data structure
---

# 零、前言

这篇文章是我记录学习STM32的USART串口通信的文章。

至于为什么会想写环形队列？实际上是因为我在几个月前玩51单片机的时候就发现过一个问题——UART老是丢包：

![1][1]

于是当时就在想有没有什么方法可以不丢包了，然后在找博客的过程当中偶然了解到了环形队列，不过当时我连最基本的双端链表都还写不出来，所以也就没细看。

现在学STM32后再回来看USART，就当是填坑了吧。顺带这里其实也可以扩展出生产-消费模型之类的东西，也可以顺带学学。

其实环形队列可以用在一切需要缓存的地方。

参考文章：

- [在 STM32 中使用环形队列的方式接收串口数据][recv-serial-data-by-circular-queue]
- [数据结构(10)---队列之环形队列][circular-queue]
- [【解决方案】关于 UART 接收数据时丢失数据的解决办法——环形缓冲存储区][method-to-solve-uart-losing-data]

<!-- more -->

# 一、USART基础和STM32上的USART

## 1.1 USART基础

USART全称Universal Synchronous/Asynchronous Receiver/Transmitter，即通用同步/异步收发器。它是一种串行通信协议，用于在微控制器和其他设备之间传输数据。

USART本身相当简单，一根VCC，一根RXD，一根TXD，一根GND，就可以实现全双工通信。

TXD：Trasmit Data，发送数据引脚，也可以叫做TX RXD：Receive Data，接收数据引脚，也可以叫做RX

```
+----------+            +----------+
| device 1 |            | device 2 |
|      VCC |------------| VCC      |
|      TXD |------------| RXD      |
|      RXD |------------| TXD      |
|      GND |------------| GND      |
+----------+            +----------+
```

UART的通信方式是异步的，即发送方和接收方不需要同步时钟信号。通信双方只需要约定好波特率、数据位、停止位和校验位等参数即可。

• 波特率：每秒钟传输的比特数。
• 数据位：每个数据包中包含的数据位数。
• 停止位：每个数据包中包含的停止位数。
• 校验位：用于检测数据传输过程中是否出现错误的位；分为奇校验（1的总和为奇数）和偶校验（1的总和为偶数）

更多详细的内容还请自行查阅博客，这并非USART基础文章，重点不在这里

## 1.2 STM32上的USART

STM32F103C8T6（我手上的这块芯片）提供了3个USART接口，但我们只会用到其中的USART1（端口为PA9: USART1_TX, PA10: USART1_RX）。

配置USART的步骤如下：

1.使能USART1的时钟和GPIOA的时钟2.配置USART1的引脚（PA9和PA10），TXD为复用推挽输出（输出走外设接口、而非输出数据寄存器），RXD为上拉输入3.配置USART1相关参数4.配置USART1的中断5.使能USART1

代码表述为：

```c
#include "stm32f10x.h"

void USART1_Init(void)
{
    /* 使能RCC  */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE);

    /* 配置USART1的引脚 */
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
    /* 复用推挽 */
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    /* 上拉输入 */
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* 配置USART1相关参数 */
    USART_InitTypeDef USART_InitStructure;
    /* 9600的波特率 */
    USART_InitStructure.USART_BaudRate = 9600;
    /* 8位数据位 */
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    /* 停止位长度为1 */
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    /* 不使用校验位 */
    USART_InitStructure.USART_Parity = USART_Parity_No;
    /* 不使用流控 */
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    /* 同时配置输出和输入 */
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART1, &USART_InitStructure);

    /* 配置USART1的中断，我们只要接收中断，这篇文章就是针对USART接收的问题的 */
    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);

    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;
    /* 优先级随便设置的，但是不要设置太高，因为下面还有优先级更高的中断操作 */
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    /* 使能USART1 */
    USART_Cmd(USART1, ENABLE);
}
```

然后我们要考虑一下接收数据，然后发送回去：

```c
// 发送字节
void Serial_SendByte(uint8_t byte)
{
    USART_SendData(USART1, byte);
    /* 等待发送完成，由于数据手册中提及TXE再在数据发送完之后会手动清零，所以不需要清零 */
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
}

// 发送数组
void Serial_SendArray(uint8_t arr[], uint32_t length)
{
    uint32_t i = 0;
    for (i = 0; i < length; i++) {
        Serial_SendByte(arr[i]);
    }
}

// 发送字符串
void Serial_SendString(uint8_t *str)
{
   // 用for循环也行，一样的
    while (*str++) { // 字符串最后有一个 \0，while识别到这个就会退出
        Serial_SendByte(*str);
    }
}

// 用于串口的幂函数，返回 x ^ y
uint32_t Serial_Pow(uint32_t x, uint32_t y)
{
    uint32_t result = 1;
    while (y--) {
        result *= x;
    }
    return result;
}
// num是数字，length是数字长度
void Serial_SendNumber(uint32_t num, uint8_t length)
{
    for (uint8_t i = 0; i < length; i++) {
        // + '0' 可以转对应的 ASCII 码
        // 注意 length - i -1
        Serial_SendByte(num / Serial_Pow(10, length - i - 1) % 10 + '0');
    }
}
```

接下来我们要写一下中断函数，刚开始的demo将会十分的简单：

```c
void USART1_IRQHandler(void)
{
    /* 判断是否是接收中断 */
    if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET)
    {
        /* 读取接收到的数据 */
        uint8_t data = USART_ReceiveData(USART1);
        /* 发送回去 */
        Serial_SendByte(data);
        /* 清除中断标志位 */
        USART_ClearITPendingBit(USART1, USART_IT_RXNE);
    }
}
```

然后我们让MCU发送给上位机

```c
/// @brief 延时函数，因为行数比较少就用了内联
void inline delay(uint32_t x)
{
    while (x--);
}

int main(void)
{
    USART1_Init();
    while (1) {
        Serial_SendString("Hello World!\r\n");
        delay(1000000);
    }
}
```

![2][2]

可以看到这个时候就存在数据丢失了，这种情况下由于发送的数据相对简单，我们完全可以用一个很骚的解决方案：

`Serial_SendString("aHello World!\r\n");` 。

你不是首字节丢失吗？那我就给你个没卵用的信息，你丢呗！反正我又不需要让上位机接收这个信息。

![3][3]

# 二、USART之缓冲区存储

## 2.1 为什么需要缓冲区

上面的解决方案看起来貌似还不错，那为什么需要环形队列？看起来这种骚方案完全能解决问题啊！

实际上，单片机并非只执行简单的转发工作，它还可能有其他任务，比如说处理定时中断，解析串口数据并执行对应操作，等等，如果说我们只是简单的收发，那么假设某个时间段，不巧有个比串口中断优先级更高的中断发生了，然后 MCU 停下了转发工作，转而去处理那个中断，那么就会导致数据丢失的问题，因此我们需要缓冲区。

当然，口说无凭，我们来配个定时中断看看，这里定了 1s 的时间：

```c
void TIM2_Init(void)
{
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE); // 使能定时器2时钟
    // 选择内部时钟
    TIM_InternalClockConfig(TIM2);
    // 配置时基单元
    TIM_TimeBaseInitTypeDef TIM_TIMBaseInitStructure;
    // 指定时钟分频
    TIM_TIMBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1; // 不分频
    // 选择计数模式
    TIM_TIMBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up; // 向上计数
    // 选择自动重装器的值（ARR）
    TIM_TIMBaseInitStructure.TIM_Period = 10000 - 1;
    // 选择计数器预分频值（PSC）
    TIM_TIMBaseInitStructure.TIM_Prescaler = 7200 - 1;
    // 选择重复计数器（在多个周期之后才输出中断信号，这里不需要）
    TIM_TIMBaseInitStructure.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM2, &TIM_TIMBaseInitStructure);

    // 配置中断
    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);
    // 使能NVIC
    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = TIM2_IRQn;
    /* 这里的优先级需要高于串口收发中断 */
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
    // 使能定时器
    TIM_Cmd(TIM2, ENABLE);
}

void TIM2_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET) {
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
        /* 我们就执行一个等待操作 */
        delay(100000);
    }
}
```

![4][4]

因此我们需要引入缓冲区，先存储串口数据，然后再发送

## 2.2 线性缓冲区的实现

代码如下：

```c
/* 定义缓冲数组和长度 */
#define SIZE 16
uint8_t arr[SIZE];

/* 中断标志位，这里加volatile是为了防止被编译器优化 */
volatile uint8_t RxFlag = 0;
uint8_t cnt    = 0;

int main(void)
{
    TIM2_Init();
    USART1_Init();
    while (1) {
        if (RxFlag) {
            RxFlag = 0; // 清除中断标志位
            for (int i = 0; i < cnt; i++) {
                Serial_SendByte(arr[i]);
            }
            /* 清空缓冲区 */
            memset(arr, 0, SIZE);
            cnt = 0;    // 计数归0
        }
    }
}

void USART1_IRQHandler(void)
{
    /* 判断是否是接收中断 */
    if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET) {
        /* 读取接收到的数据 */
        if (cnt < SIZE) {
            arr[cnt++] = USART_ReceiveData(USART1);
        } else {
            cnt = 0;
        }
        /* 清除中断标志位 */
        USART_ClearITPendingBit(USART1, USART_IT_RXNE);
        RxFlag = 1;
    }
}
```

不过此时依然会存在数据丢失的问题，只要通信数据量一大，频率一快，该错位还是错位

下面这张图的数据丢失发生在第四行蓝字处，丢失了ab两个字母

![5][5]

备注：以上实验均为50ms周期的频率下发送的结果，实际发送频率不太可能这么快，这里只是压测而已。

因此接下来我们来讲讲环形队列，但在此之前需要先理解环形队列

# 三、环形队列

## 3.1 环形队列基础

环形队列是一种数据结构，本质上来说它是一个数组，但是在逻辑层面上它是首尾相连的

![6][6]

环形队列的特点如下：

1.队列长度创建后固定，首尾相接，形成环状2.队列在队首出队，队尾入队3.环形队列逻辑上为环形，物理上为定长数组

所以环形队列为空时，队首和队尾就是相同的，而环形队列满了的时候，队尾 + 1之后逻辑上就应该回到队首，从而队尾 + 1 与长度取模会得到0，也就是队首。

第二个特征可以看出，实际上环形队列是FIFO的。

由于环形队列的物理特征为定长数组，数组可不会头尾相接，所以必然有一个操作可以让队尾在满足一个条件之后索引到队首去，也就是绕了个大圈绕回来了。

## 3.2 环形队列实现

我们来明确一下实现环形队列的需求是什么：

1.首先，我们需要初始化一个定长数组2.其次，我们要能通过队首出队；队尾入队3.最后，我们需要判断队列为空和队列已满的情况

总结一下，需要明确四个变量：队首索引、队尾索引、队列长度、队列数组。配这东西最方便的方式就是结构体：

### 3.2.1 环形队列结构体

```c
typedef struct {
    int *data;  // 数据指针
    int front;  // 队首位置
    int rear;   // 队尾位置
    int length; // 队列长度
} CircularQueue;
```

我们来玩个骚一点的操作，毕竟如果我们想要移植这段代码的话，等会一个个改类型实在是麻烦，我们直接拿宏一换多：

```c
#define CQ_DATA_TYPE int

typedef struct {
    CQ_DATA_TYPE *data;  // 数据指针
    int front;  // 队首位置
    int rear;   // 队尾位置
    int length; // 队列长度
} CircularQueue;
```

### 3.2.2 环形队列初始化操作

然后是初始化函数，我们需要申请内存空间，很自然就会想到malloc对吧，所以#include <stdlib.h>

那么malloc的函数声明是这样的：void *malloc(size_t /* size */);

void类型的指针，我们等会还要强转，然后还要再多写个sizeof算类型大小，好麻烦哦！

没关系！上宏函数：

```c
/// @brief 动态申请指定类型和数量的地址空间
#define MALLOC_KxTYPE(type, k) ((type *)malloc(sizeof(type) * (k)))
```

然后我们直接这样初始化：

```c
/**
 * @brief 创建一个指定长度的环形队列
 *
 * @note 队列的长度为length + 1，初始情况下，队首和队尾都指向0
 * @note 我觉得很有必要说明一下为什么length + 1
 * @note 让我们考虑这么一种情况，假设我给data的初始化要求是4个int
 * @note 接下来我将 1 2 3 4 送进队列里面
 * @note 由于循环队列从队尾开始进队，刚开始的时候，队尾和队首重合，我们又没有+1
 * @note 所以整个数组总的长度为4
 * @note 此时1进入队列，现在就是 front(1), rear的情况（可见rear此时自己占据了一格）
 * @note 然后2进入队列，现在是 front(1), 2, rear
 * @note 到3，front(1), 2, 3, rear
 * @note 到4，front(1), 2, 3, 4, rear
 * @note 可是不对啊！数组长度只有4，按照循环队列的设计，此时rear已经和front重叠了
 * @note 而enque是不允许覆盖的（enque内部使用的判断队列满的逻辑为`((queue->rear + 1) % (queue->length) == (queue->front))`）
 * @note 所以实际进入队列的数据只有 front(1), 2, 3，rear自己会占用一格
 * @note 所以如果不malloc length + 1的话，那实际得到的数组长度只能是 length - 1，+1就是为了补上这个洞的
 */
CircularQueue *createCircularQueue(int length)
{
    CircularQueue *queue = MALLOC_KxTYPE(CircularQueue, 1);
    /* 注意这里需要 + 1，我们得预留好空位给rear */
    /* 避免到时候绕了个圈之后意外发生越界 */
    /* 同时也能保证完全利用完申请的内存 */
    queue->data  = MALLOC_KxTYPE(CQ_DATA_TYPE, length + 1);
    queue->front = 0;
    queue->rear  = 0;
    /* 长度其实也应该 + 1，只有这样子才能访问完整个分配的内存区域 */
    /* 从而front -> rear总共length+1个单位，但是只有length个单位是能存储的 */
    queue->length = length + 1;
    return queue;
}
```

注意这里不能用内联函数哈，内联函数要类型检查的，你很难把类型作为参数传进去。

### 3.2.3 环形队列判空判满操作

超级简单，一开始（第三章开头）我们就把逻辑说好了，这里需要先实现判空判满，因为入队和出队要用到。

```c
/**
 * @brief 判断环形队列是否为空
 *
 * @note 当队首和队尾相等时，表示队列为空
 */
bool isCircularQueueEmpty(const CircularQueue *queue)
{
    return (queue->front == queue->rear);
}

/**
 * @brief 判断环形队列是否已满
 *
 * @note 当队尾的下一个位置为队首时，表示队列已满
 */
bool isCircularQueueFull(const CircularQueue *queue)
{
    return ((queue->rear + 1) % (queue->length) == (queue->front));
}
```

### 3.2.4 环形队列入队出队操作

入队的操作还是相当简单的，逻辑就是，首先我们要判断队列是否满了，满了就不可以加了，然后我们再通过队尾加入元素，最后我们判断一下有没有正好到队列长度，如果是的话，我们就让队尾绕回来。

```c
/**
 * @brief 向环形队列中入队一个元素
 *
 * @note 如果队列已满，则返回false
 */
bool enCircularQueue(CircularQueue *queue, int value)
{
    if (isCircularQueueFull(queue)) {
        return false;
    }
    queue->data[queue->rear] = value;
    // if (queue->rear == queue->length) {
    //     queue->rear = 0;
    // }
    /* 可以简化为取模运算 */
    queue->rear = (queue->rear + 1) % queue->length;
    return true;
}
```

然后是出队操作，出队操作稍微复杂一点，因为我们需要返回出队的元素，所以需要传入一个指针，然后我们再判断一下队列是否为空，空了就不可以减了，然后我们通过队首取出元素，最后我们判断一下有没有正好到队列长度，如果是的话，我们就让队首绕回来。

```c
/**
 * @brief 从环形队列中出队一个元素
 *
 * @note 如果队列为空，则返回false
 */
bool deCircularQueue(CircularQueue *queue, CQ_DATA_TYPE *value)
{
    if (isCircularQueueEmpty(queue)) {
        return false;
    }
    *value = queue->data[queue->front];
    // if (queue->front == queue->length) {
    //     queue->front = 0;
    // }
    /* 可以简化为取模运算 */
    queue->front = (queue->front + 1) % queue->length;
    return true;
}
```

理论上来说其实出队操作可以直接返回值，不需要传指针，但问题在于enqueue和dequeue此时就会存在接口不统一的问题，这倒是其次，最主要的是如果队列为空，你要返回什么值表示出队失败呢？要知道C语言并没有异常控制流，你只能通过返回值来表示错误。所以这里的设计保证了接口统一和错误处理的正常执行。

### 3.2.5 其他操作

剩下看你打算怎么搞，环形队列的主要功能到这里其实就结束了，你也可以写一些函数来索引队首和队尾元素，或者返回结构体的一些值，这些不是重点，所以不放在这里。

## 3.3 环形队列测试

测试环境为 Ubuntu 24.04.2 LTS，使用的编译器为 GCC，内存检查工具为 Valgrind，用于检查循环队列的实现是否存在内存泄漏。

这种情况是只 `malloc` 了 `length` 个字节的情况:

![7][7]

这种情况是 `malloc` 了 `length + 1` 个字节的情况:

![8][8]

当然结果是没有内存泄漏啊，我懒得把调试信息全搞下来了。

# 四、环形队列+USART

代码如下：

```c
/* 中断标志位，这里加volatile是为了防止被编译器优化 */
volatile uint8_t RxFlag = 0;
CircularQueue *RxQueue; // 环形队列

int main(void)
{
    uint8_t data = 0;

    TIM2_Init();
    USART1_Init();
    RxQueue = createCircularQueue(SIZE);
    while (1) {
        if (RxFlag) {
            RxFlag = 0; // 清除中断标志位
            while (!isCircularQueueEmpty(RxQueue)) {
                {
                    /* 数据出队列 */
                    deCircularQueue(RxQueue, &data);
                    Serial_SendByte(data);
                }
            }
        }
    }
}

void USART1_IRQHandler(void)
{
    /* 判断是否是接收中断 */
    if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET) {
        /* 读取接收到的数据 */
        if (!isCircularQueueFull(RxQueue)) {
            enCircularQueue(RxQueue, USART_ReceiveData(USART1));
        }
        /* 清除中断标志位 */
        USART_ClearITPendingBit(USART1, USART_IT_RXNE);
        RxFlag = 1;
    }
}
```

![9][9]

备注：以上实验均为50ms周期的频率下发送的结果，实际发送频率不太可能这么快，这里只是压测而已。

# 五、总结

简单来说，环形队列实际上就是利用数组实现了缓存，但这种缓存相较于：

```
if (cnt < SIZE) {
    arr[cnt++] = USART_ReceiveData(USART1);
} else {
    cnt = 0;
}
```

的无脑覆盖来说，更加地平稳，它要求你队列满了之后必须先出队，不然不准再往里面加新东西，从而避免了数据的覆盖。

当然，环形队列在极端的压测情况下依然可能存在数据丢失（我上面的实验上了50ms，没怎么丢包，相对于线性缓冲区来说表现十分优异，但在10ms的频率下会存在几个字母的丢包），不过它依然够用且好用，它的实现相对来说比较简单，只要你曾经搓出过链表，那么你离环形队列其实只有一步之遥。所以我推荐在需要数据较稳定的情况下使用环形队列。

本来计划是打算顺着环形队列顺带讲讲生产-消费模型和消息队列的，但是现在字数已经快六千了，就不往下写了

---

[1]: /images/stm32-circular-queue-encountered-serial-communication/01.png
[2]: /images/stm32-circular-queue-encountered-serial-communication/02.png
[3]: /images/stm32-circular-queue-encountered-serial-communication/03.png
[4]: /images/stm32-circular-queue-encountered-serial-communication/04.png
[5]: /images/stm32-circular-queue-encountered-serial-communication/05.png
[6]: /images/stm32-circular-queue-encountered-serial-communication/06.png
[7]: /images/stm32-circular-queue-encountered-serial-communication/07.png
[8]: /images/stm32-circular-queue-encountered-serial-communication/08.png
[9]: /images/stm32-circular-queue-encountered-serial-communication/09.png

[recv-serial-data-by-circular-queue]: https://www.cnblogs.com/huameixiao/p/17968052
[circular-queue]: https://blog.csdn.net/qq_48711800/article/details/118962516
[method-to-solve-uart-losing-data]: https://blog.csdn.net/qq_42417071/article/details/142689022

