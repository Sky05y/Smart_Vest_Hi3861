#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "hi_io.h"
#include "hi_uart.h"
#include "iot_gpio_ex.h"
#include "ohos_init.h"
#include "cmsis_os2.h"
#include "hi_uart.h"
#include "hi_watchdog.h"
#include "hi_gpio.h"
#include <sys/time.h>

#define UART_BUFF_SIZE 1024
#define U_SLEEP_TIME   700000

/***********************************************************************
* 函数名称: Uart1GpioInit
* 说    明: 初始化 UART1 所需的 GPIO 引脚，配置为串口功能
* 参    数: 无
* 返 回 值: 无
************************************************************************/
void Uart1GpioInit(void)
{
    hi_gpio_init();
    hi_io_set_func(IOT_IO_NAME_GPIO_6, IOT_IO_FUNC_GPIO_0_UART1_TXD);
    hi_io_set_func(IOT_IO_NAME_GPIO_5, IOT_IO_FUNC_GPIO_1_UART1_RXD);
}

/***********************************************************************
* 函数名称: Uart1Config
* 说    明: 配置 UART1 的串口参数，包括波特率、数据位等
* 参    数: 无
* 返 回 值: 无
************************************************************************/
void Uart1Config(void)
{
    hi_uart_attribute uart_attr =
    {
        .baud_rate = 9600,
        .data_bits = HI_UART_DATA_BIT_8,
        .stop_bits = HI_UART_STOP_BIT_1,
        .parity = HI_UART_PARITY_NONE,
        .pad = 0,
    };

    hi_uart_extra_attr uart_extra_attr = 
    {
        .tx_fifo_line = HI_FIFO_LINE_ONE_EIGHT,
        .rx_fifo_line = HI_FIFO_LINE_ONE_QUARTER,
        .flow_fifo_line = HI_FIFO_LINE_ONE_EIGHT,
        .tx_block = HI_UART_BLOCK_STATE_BLOCK,
        .rx_block = HI_UART_BLOCK_STATE_BLOCK,
        .tx_buf_size = 256,
        .rx_buf_size = 256,
        .tx_use_dma = HI_UART_NONE_DMA,
        .rx_use_dma = HI_UART_NONE_DMA,
    };

    hi_uart_init(HI_UART_IDX_1, &uart_attr, &uart_extra_attr);
    hi_uart_set_flow_ctrl(HI_UART_IDX_1, HI_FLOW_CTRL_NONE);

}

/***********************************************************************
* 函数名称: dm_to_dd
* 说    明: 将 GPS 输出的度分格式 (ddmm.mmmm) 转换为度格式 (dd.dddd)
* 参    数: dm：度分格式的浮点数值
* 返 回 值: 转换后的度值（小数形式）
************************************************************************/
double dm_to_dd(double dm) 
{
    double degree = (int)(dm / 100);
    double minute = dm - degree * 100;
    return degree + minute / 60;
}

/***********************************************************************
* 函数名称: hi_uart_read_timeout23
* 说    明: 带超时机制的 UART 读取函数，超过 timeout_ms 仍无数据则返回 0
* 参    数: uart_idx：串口编号
*           buf：读取数据的缓存区
*           buf_size：缓存区大小
*           timeout_ms：最大等待时间（毫秒）
* 返 回 值: 实际读取到的数据长度，0 表示超时未读取到数据
************************************************************************/
int hi_uart_read_timeout23(int uart_idx, unsigned char *buf, int buf_size, int timeout_ms) {
    struct timeval start, now;
    gettimeofday(&start, NULL);

    int len = 0;
    while (1) 
    {
        len = hi_uart_read(uart_idx, buf, buf_size);
        if (len > 0) 
        {
            return len;
        }

        usleep(100);

        gettimeofday(&now, NULL);
        double elapsed = (now.tv_sec - start.tv_sec) * 1000.0 +  (now.tv_usec - start.tv_usec) / 1000.0;
        if (elapsed >= timeout_ms) 
        {
            printf("超时\n");
            return 0;
        }
    }
}

/***********************************************************************
* 函数名称: UartTask
* 说    明: 串口任务初始化函数，配置 UART 引脚与参数
* 参    数: 无
* 返 回 值: 无
************************************************************************/
static void UartTask(void)
{
    printf("UartTask Initing\n");
    Uart1GpioInit();
    Uart1Config();
    printf("UartTask Init Ended\n");
}

/***********************************************************************
* 函数名称: RunGPS
* 说    明: 解析 UART 中读取的 $GNGGA NMEA 语句，提取并计算经纬度
* 参    数: lat：指向纬度变量的指针
*           lon：指向经度变量的指针
* 返 回 值: 无（经纬度通过指针返回）
************************************************************************/
void RunGPS(double *lat, double *lon){
    int len = 0;
    unsigned char uartReadBuff[UART_BUFF_SIZE] = {0};
    int located = 0;
        usleep(U_SLEEP_TIME);
        len = hi_uart_read_timeout23(HI_UART_IDX_1, uartReadBuff, UART_BUFF_SIZE, 2000);
        if (len > 0) 
        {
            uartReadBuff[len] = 0;
            char *nmea_gngga = strstr((char *)uartReadBuff, "$GNGGA");
            if (nmea_gngga != NULL) {
                char *token;
                token = strtok(nmea_gngga, ",");
                char *nmea_fields[15];
                int i = 0;
                while (token != NULL && i < 15) 
                {
                    nmea_fields[i++] = token;
                    token = strtok(NULL, ",");
                }

                if (i > 6 && (strcmp(nmea_fields[6], "1") == 0 || strcmp(nmea_fields[6], "2") == 0)) 
                {
                    printf("确认定位成功\n");
                    located = 1;

                    char ddmm_output[128];
                    sprintf(ddmm_output, "%s %s,%s %s", nmea_fields[2], nmea_fields[3], nmea_fields[4], nmea_fields[5]);

                    double dd0 = dm_to_dd(atof(nmea_fields[2]));
                    double dd1 = dm_to_dd(atof(nmea_fields[4]));
                    *lat = dd0;
                    *lon = dd1;
                    printf("[$GNGGA,WGS-84]经纬度数据 : %0.5lf, %0.5lf\n", dd0, dd1);

                    return; 
                }
            }
        }

}

/***********************************************************************
* 函数名称: UartExampleEntry
* 说    明: UART 示例入口函数，禁用看门狗并创建 UART 任务线程
* 参    数: 无
* 返 回 值: 无
************************************************************************/
void UartExampleEntry(void)
{
    osThreadAttr_t attr;
    hi_watchdog_disable();

    attr.name = "UartTask";
    attr.attr_bits = 0U;
    attr.cb_mem = NULL;
    attr.cb_size = 0U;
    attr.stack_mem = NULL;
    attr.stack_size = 25 * 1024;
    attr.priority = osPriorityNormal;

    if (osThreadNew((osThreadFunc_t)UartTask, NULL, &attr) == NULL) 
    {
        printf("[UartTask] Failed to create UartTask!\n");
    }
}
