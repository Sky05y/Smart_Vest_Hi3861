#include <hi_i2c.h>
#include <hi_io.h>
#include <hi_time.h>
#include <stdio.h>
#include <stdint.h>

#define u8 uint8_t
#define u32 uint32_t

#define max30102_WR_address 0xAE
#define max30102_RE_address 0xAF
#define MAX30102_I2C_ADDR 0x57

/***********************************************************************
* 函数名称: max30102_ReadReg
* 功    能: 读取 MAX30102 寄存器的一个字节数据
* 参    数: reg - 寄存器地址
* 返 回 值: 寄存器读取的值（失败返回 0xFF）
************************************************************************/
u8 max30102_ReadReg(u8 reg)
{
    u8 value = 0;
    hi_i2c_data i2cData;
    i2cData.send_buf = &reg;
    i2cData.send_len = 1;
    if (hi_i2c_write(HI_I2C_IDX_0, max30102_WR_address, &i2cData) != HI_ERR_SUCCESS) {
        printf("!!! Write addr 0x%02X failed.\n", reg);
        return 0xFF;
    }
    i2cData.receive_buf = &value;
    i2cData.receive_len = 1;
    if (hi_i2c_read(HI_I2C_IDX_0, max30102_RE_address, &i2cData) != HI_ERR_SUCCESS) {
        return 0xFF;
    }

    return value;
}

/***********************************************************************
* 函数名称: max30102_CheckConfig
* 功    能: 打印读取 MAX30102 配置相关寄存器的值，用于调试配置是否正确
* 参    数: 无
* 返 回 值: 无
************************************************************************/
void max30102_CheckConfig(void)
{
    u8 val09 = max30102_ReadReg(0x09);
    u8 val0A = max30102_ReadReg(0x0A);
    u8 val0C = max30102_ReadReg(0x0C);
    u8 val0D = max30102_ReadReg(0x0D);
    u8 val00 = max30102_ReadReg(0x00);
    u8 val01 = max30102_ReadReg(0x01);

    printf(">>> MODE_CONFIG (0x09) = 0x%02X\n", val09);
    printf(">>> SPO2_CONFIG (0x0A) = 0x%02X\n", val0A);
    printf(">>> LED1 (0x0C) = 0x%02X\n", val0C);
    printf(">>> LED2 (0x0D) = 0x%02X\n", val0D);
    printf(">>> INT_STATUS (0x00) = 0x%02X\n", val00);
    printf(">>> INT_ENABLE (0x01) = 0x%02X\n", val01);
}

/***********************************************************************
* 函数名称: I2C0_Init
* 功    能: 初始化 I2C0 接口，用于与 MAX30102 通信
* 参    数: 无
* 返 回 值: 无
************************************************************************/
void I2C0_Init(void)
{
    printf("I2C Initing...\n");
    hi_io_set_func(HI_IO_NAME_GPIO_9, HI_IO_FUNC_GPIO_9_I2C0_SCL);
    hi_io_set_func(HI_IO_NAME_GPIO_10, HI_IO_FUNC_GPIO_10_I2C0_SDA);
    hi_i2c_init(HI_I2C_IDX_0, 400000);
    printf("I2C Init Finish...\n");
}

/***********************************************************************
* 函数名称: max30102_Bus_Read
* 功    能: 读取 MAX30102 寄存器的一个字节（带错误码打印）
* 参    数: Register_Address - 寄存器地址
* 返 回 值: 寄存器值（失败返回错误码）
************************************************************************/
u8 max30102_Bus_Read(u8 Register_Address)
{
    u8 data1;
    u32 result;
    u8 data[1];
    u8 buffer[] = {Register_Address};
    hi_i2c_data i2cData = {0};
    i2cData.send_buf = buffer;
    i2cData.send_len = sizeof(buffer);

    result = hi_i2c_write(HI_I2C_IDX_0, max30102_WR_address, &i2cData);
    if (result != HI_ERR_SUCCESS) {
        printf("I2C write error = 0x%x\r\n", result);
        return result;
    }

    hi_i2c_data i2cData1 = {0};
    i2cData1.receive_buf = data;
    i2cData1.receive_len = 1;

    result = hi_i2c_read(HI_I2C_IDX_0, max30102_RE_address, &i2cData1);
    if (result != HI_ERR_SUCCESS) {
        printf("I2C read error = 0x%x\r\n", result);
        return result;
    }

    data1 = data[0];
    return data1;
}

/***********************************************************************
* 函数名称: max30102_Bus_Write
* 功    能: 向 MAX30102 指定寄存器写入一个字节
* 参    数: Register_Address - 寄存器地址
*           Word_Data        - 写入的字节数据
* 返 回 值: 写入结果（0 表示成功）
************************************************************************/
u8 max30102_Bus_Write(u8 Register_Address, u8 Word_Data)
{
    uint8_t buffer[] = {Register_Address, Word_Data};
    hi_i2c_data i2cData = {0};
    i2cData.send_buf = buffer;
    i2cData.send_len = sizeof(buffer);

    return hi_i2c_write(HI_I2C_IDX_0, max30102_WR_address, &i2cData);
}

/***********************************************************************
* 函数名称: max30102_Init
* 功    能: 初始化 MAX30102 模块，包括 I2C 初始化、寄存器配置、清除 FIFO
* 参    数: 无
* 返 回 值: 无
************************************************************************/
void max30102_Init(void)
{
    I2C0_Init();
    printf("I2C init done.\r\n");
    max30102_Bus_Write(0x09, 0x03);
    max30102_Bus_Write(0x0A, 0x27);
    max30102_Bus_Write(0x0C, 0x24);
    max30102_Bus_Write(0x0D, 0x24);
    max30102_Bus_Write(0x08, 0x00);
    max30102_Bus_Write(0x04, 0x00);
    max30102_Bus_Write(0x06, 0x00);
    max30102_CheckConfig();
}

/***********************************************************************
* 函数名称: write_fifo_with_retry
* 功    能: 使用重试机制写入 FIFO 寄存器地址（0x07）
* 参    数: 无
* 返 回 值: 无
************************************************************************/
void write_fifo_with_retry(void)
{
    u8 reg = 0x07;
    hi_i2c_data i2cData = {0};
    i2cData.send_buf = &reg;
    i2cData.send_len = 1;

    u32 result;
    int retry = 0;

    while (1) {
        result = hi_i2c_write(HI_I2C_IDX_0, max30102_WR_address, &i2cData);
        if (result == HI_ERR_SUCCESS) {
            break;
        }
        retry++;
        hi_udelay(1000000);
    }
}

/***********************************************************************
* 函数名称: max30102_Read_FIFO
* 功    能: 从 MAX30102 FIFO 中读取 6 字节数据，转换为红光/红外数据
* 参    数: red_led - 指向红光数据的指针
*           ir_led  - 指向红外数据的指针
* 返 回 值: 无（通过指针返回转换结果）
************************************************************************/
void max30102_Read_FIFO(u32 *red_led, u32 *ir_led)
{
    u8 data[6] = {0};
    u32 result;
    write_fifo_with_retry();
    hi_i2c_data i2cData1 = {0};
    i2cData1.receive_buf = data;
    i2cData1.receive_len = 6;
    result = hi_i2c_read(HI_I2C_IDX_0, max30102_RE_address, &i2cData1); // 1
    if (result != HI_ERR_SUCCESS) 
    {
        printf("!!! FIFO read failed, err = 0x%X\n", result);
        return;
    }
    *red_led = ((u32)data[0] << 16 | (u32)data[1] << 8 | data[2]) & 0x03FFFF;
    *ir_led  = ((u32)data[3] << 16 | (u32)data[4] << 8 | data[5]) & 0x03FFFF;
}
