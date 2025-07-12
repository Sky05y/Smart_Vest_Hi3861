// max30205.c
#include "max30205.h"
#include "wifiiot_i2c.h"
#include "wifiiot_i2c_ex.h"
#include "wifiiot_gpio.h"
#include "wifiiot_gpio_ex.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
// 读取寄存器函数
u32 ReadMax30205Register(u8 regAddr, u8 *dataBuffer, u8 dataLen) {
    WifiIotI2cData i2cData = {
        .sendBuf = &regAddr,
        .sendLen = 1,
        .receiveBuf = dataBuffer,
        .receiveLen = dataLen
    };
    return I2cWriteread(WIFI_IOT_I2C_IDX_1, (MAX30205_ADDRESS << 1) | 0x00, &i2cData);
}

// GPIO初始化
void max30205_IO_Init(void){
    GpioInit();
    // 配置I2C引脚
    IoSetFunc(WIFI_IOT_IO_NAME_GPIO_0, WIFI_IOT_IO_FUNC_GPIO_0_I2C1_SDA);
    IoSetFunc(WIFI_IOT_IO_NAME_GPIO_1, WIFI_IOT_IO_FUNC_GPIO_1_I2C1_SCL);
    // 初始化I2C（400kbps）
    I2cInit(WIFI_IOT_I2C_IDX_1, 400000);
    I2cSetBaudrate(WIFI_IOT_I2C_IDX_1, 400000);
}

// 启动传感器
u32 max30205begin(void){
    u32 ret;
    WifiIotI2cData i2cdata = {0};
    u8 send_data[2];   
    u8 reg_val;

    // 配置为连续测量模式（0x00）
    send_data[0] = MAX30205_CONFIGURATION;
    send_data[1] = 0x00;
    i2cdata.sendBuf = send_data;
    i2cdata.sendLen = 2;
    ret = I2cWrite(WIFI_IOT_I2C_IDX_1, (MAX30205_ADDRESS << 1) | 0x00, &i2cdata);
    
    if(ret != WIFI_IOT_SUCCESS){
        printf("初始化模式设置失败\n");
        return ret;
    }

    // 等待传感器就绪（SHUTDOWN位清零）
    do{
        send_data[0] = MAX30205_CONFIGURATION;
        i2cdata.sendBuf = send_data;
        i2cdata.sendLen = 1;
        i2cdata.receiveBuf = &reg_val;
        i2cdata.receiveLen = 1;

        ret = I2cRead(WIFI_IOT_I2C_IDX_1, (MAX30205_ADDRESS << 1) | 0x01, &i2cdata);

        if (ret != WIFI_IOT_SUCCESS) {
            printf("寄存器读取失败: %u\n", ret);
            return ret;
        }
        
        osDelay(10);  // 延时避免频繁读取

    }while (reg_val & 0x80);  // 等待SHUTDOWN位（bit7）变为0

    printf("传感器初始化成功\n");
    return ret;
}

// 读取温度值
float max30205_read_template(void){
    WifiIotI2cData i2cdata = {0};
    u8 send_data = MAX30205_TEMPERATURE;  // 修复：直接初始化发送数据
    u8 reg_val[2] = {0};  // 数组存储温度数据（2字节）

    // 读取温度寄存器（0x00）
    i2cdata.sendBuf = &send_data;
    i2cdata.sendLen = 1;
    i2cdata.receiveBuf = reg_val;  // 指向数组
    i2cdata.receiveLen = 2;

    u32 ret = I2cWriteread(WIFI_IOT_I2C_IDX_1, (MAX30205_ADDRESS << 1) | 0x00, &i2cdata);
    if (ret != WIFI_IOT_SUCCESS) {
        printf("温度读取失败: %u\n", ret);
        return -1.0f;  // 返回错误值
    }
    
    // 组合16位数据并转换为温度
    int16_t raw = (reg_val[0] << 8) | reg_val[1];
    return raw * 0.00390625f;  // 转换系数：1/256
}

// 读取温度并通过指针返回
void max30205_read_data(float* temperature){
    if (temperature != NULL) {  // 检查指针有效性
        *temperature = max30205_read_template();  // 解引用赋值
        printf("温度为%f°C\n", *temperature);  // 打印实际温度
    }
}

// 初始化入口
void max30205_init(void){
    max30205_IO_Init();
    max30205begin();
}