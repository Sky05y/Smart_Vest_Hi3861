// max30205.h
#ifndef __MAX30205_H__
#define __MAX30205_H__

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <stdlib.h>
#include <stdbool.h>
#include "cmsis_os2.h"
#include "wifiiot_errno.h"
#include "wifiiot_gpio.h"
#include "wifiiot_gpio_ex.h"
#include "wifiiot_i2c.h"
#include "wifiiot_i2c_ex.h"

// 数据类型宏定义
#define u8 uint8_t
#define u16 uint16_t
#define u32 uint32_t

// 寄存器地址定义
#define MAX30205_ADDRESS        0x48
#define MAX30205_TEMPERATURE    0x00
#define MAX30205_CONFIGURATION  0x01
#define MAX30205_THYST          0x02
#define MAX30205_TOS            0x03

// 配置寄存器位定义
typedef enum {
    SHUTDOWN      = 0x80,  // 关机模式
    COMPARATOR    = 0x02,  // 比较器模式
    OS_POLARITY   = 0x04,  // 输出极性
    FAULT_QUEUE_0 = 0x08,  // 故障队列位0
    FAULT_QUEUE_1 = 0x10,  // 故障队列位1
    DATA_FORMAT   = 0x20,  // 数据格式
    TIME_OUT      = 0x40,  // 超时位
    ONE_SHOT      = 0x80   // 单次测量
} Max30205Config;

// 函数声明
u32 ReadMax30205Register(u8 regAddr, u8 *dataBuffer, u8 dataLen);
void max30205_IO_Init(void);
u32 max30205begin(void);
float max30205_read_template(void);
void max30205_read_data(float* temperature);
void max30205_init(void);

#endif // __MAX30205_H__