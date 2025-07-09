/***********************************************************************
* 文件说明: 综合物联网程序，包含传感器采集、MQTT通信、命令控制等功能
* 模块功能: 
* - 获取环境温度、心率、血氧、GPS 经纬度
* - 通过华为IoT平台进行MQTT消息上报与下发指令响应
************************************************************************/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "ohos_init.h"
#include "cmsis_os2.h"
#include <hi_gpio.h>
#include "wifi_connect.h"
#include "lwip/sockets.h"
#include "hi_gpio.h"
#include "gps.h"
#include "E53_IA1.h"
#include "max30102_app.h"
#include "hi_io.h"
#include "oc_mqtt.h"
#include <cJSON.h>

#define MSGQUEUE_OBJECTS 16

/***********************************************************************
* 结构体名称: MSGQUEUE_OBJ_t
* 说    明: 用于消息队列的基本结构
* 成    员: Buf - 消息字符串
*           Idx - 消息索引标识
************************************************************************/
typedef struct
{
    char *Buf;
    uint8_t Idx;
} MSGQUEUE_OBJ_t;

MSGQUEUE_OBJ_t msg;
osMessageQueueId_t mid_MsgQueue;

#define CLIENT_ID "685ff8bcd582f20018360398_20250628_0_0_2025070907"
#define USERNAME "685ff8bcd582f20018360398_20250628"
#define PASSWORD "9f14ab6d439f45b6062624205942b49eaddb3b6800c279ab121f8f072254b437"

void UartExampleEntry(void);
void RunGPS(double *lat, double *lon);

/***********************************************************************
* 枚举类型: en_msg_type_t
* 说    明: 消息类型标识
************************************************************************/
typedef enum
{
    en_msg_cmd = 0,
    en_msg_report,
} en_msg_type_t;

/***********************************************************************
* 结构体名称: cmd_t
* 说    明: 命令消息结构体
************************************************************************/
typedef struct
{
    char *request_id;
    char *payload;
} cmd_t;

/***********************************************************************
* 结构体名称: report_t
* 说    明: 上报消息结构体，包括所有传感器与位置信息
************************************************************************/
typedef struct
{
    int lum;
    int temp;
    int hum;
    int heart_rate;
    int spo2;
    double lat ;
    double lon ;
} report_t;

/***********************************************************************
* 结构体名称: app_msg_t
* 说    明: 应用消息联合结构，支持命令和上报两种类型
************************************************************************/
typedef struct
{
    en_msg_type_t msg_type;
    union
    {
        cmd_t cmd;
        report_t report;
    } msg;
} app_msg_t;

/***********************************************************************
* 结构体名称: app_cb_t
* 说    明: 控制设备的当前状态
************************************************************************/
typedef struct
{
    int connected;
    int led;
    int motor;
} app_cb_t;
static app_cb_t g_app_cb;

/***********************************************************************
* 函数名称: deal_report_msg
* 说    明: 处理上报消息，构造 MQTT 属性报告内容并发送
* 参    数: report - 指向上报数据结构体
* 返 回 值: 无
************************************************************************/
static void deal_report_msg(report_t *report)
{
    oc_mqtt_profile_service_t service;
    oc_mqtt_profile_kv_t temperature;
    oc_mqtt_profile_kv_t humidity;
    oc_mqtt_profile_kv_t luminance;
    oc_mqtt_profile_kv_t heart_rate;
    oc_mqtt_profile_kv_t spo2;
    oc_mqtt_profile_kv_t led;
    oc_mqtt_profile_kv_t motor;
    oc_mqtt_profile_kv_t lat;
    oc_mqtt_profile_kv_t lon;

    char lat_str[32], lon_str[32];
    snprintf(lat_str, sizeof(lat_str), "%.6f", report->lat);
    snprintf(lon_str, sizeof(lon_str), "%.6f", report->lon);
    
    service.event_time = NULL;
    service.service_id = "Agriculture";
    service.service_property = &temperature;
    service.nxt = NULL;

    temperature.key = "Temperature";
    temperature.value = &report->temp;
    temperature.type = EN_OC_MQTT_PROFILE_VALUE_INT;
    temperature.nxt = &humidity;

    humidity.key = "Humidity";
    humidity.value = &report->hum;
    humidity.type = EN_OC_MQTT_PROFILE_VALUE_INT;
    humidity.nxt = &luminance;

    luminance.key = "Luminance";
    luminance.value = &report->lum;
    luminance.type = EN_OC_MQTT_PROFILE_VALUE_INT;
    luminance.nxt = &heart_rate;

    heart_rate.key = "Heart_rate";
    heart_rate.value = &report->heart_rate;
    heart_rate.type = EN_OC_MQTT_PROFILE_VALUE_INT;
    heart_rate.nxt = &spo2;

    spo2.key = "Spo2";
    spo2.value = &report->spo2;
    spo2.type = EN_OC_MQTT_PROFILE_VALUE_INT;
    spo2.nxt = &led;

    led.key = "LightStatus";
    led.value = g_app_cb.led ? "ON" : "OFF";
    led.type = EN_OC_MQTT_PROFILE_VALUE_STRING;
    led.nxt = &motor;

    motor.key = "MotorStatus";
    motor.value = g_app_cb.motor ? "ON" : "OFF";
    motor.type = EN_OC_MQTT_PROFILE_VALUE_STRING;
    motor.nxt = &lat;

    lat.key = "Lat";
    lat.value = lat_str;
    lat.type = EN_OC_MQTT_PROFILE_VALUE_STRING;
    lat.nxt = &lon;

    lon.key = "Lon";
    lon.value = lon_str;
    lon.type = EN_OC_MQTT_PROFILE_VALUE_STRING;
    lon.nxt = NULL;

    oc_mqtt_profile_propertyreport(USERNAME, &service);
    return;
}

/***********************************************************************
* 函数名称: oc_cmd_rsp_cb
* 说    明: 接收到平台下发命令后的回调函数
* 参    数: recv_data - 命令数据
*           recv_size - 数据长度
*           resp_data - 返回数据
*           resp_size - 返回长度
* 返 回 值: 无
************************************************************************/
void oc_cmd_rsp_cb(uint8_t *recv_data, size_t recv_size, uint8_t **resp_data, size_t *resp_size)
{
    app_msg_t *app_msg;

    int ret = 0;
    app_msg = malloc(sizeof(app_msg_t));
    app_msg->msg_type = en_msg_cmd;
    app_msg->msg.cmd.payload = (char *)recv_data;

    printf("recv data is %.*s\n", recv_size, recv_data);
    ret = osMessageQueuePut(mid_MsgQueue, &app_msg, 0U, 0U);
    if (ret != 0)
    {
        free(recv_data);
    }
    *resp_data = NULL;
    *resp_size = 0;
}


/***********************************************************************
* 函数名称: deal_cmd_msg
* 说    明: 处理平台下发的命令
* 参    数: cmd - 指向命令结构体
* 返 回 值: 无
************************************************************************/
static void deal_cmd_msg(cmd_t *cmd)
{
    cJSON *obj_root;
    cJSON *obj_cmdname;
    cJSON *obj_paras;
    cJSON *obj_para;

    int cmdret = 1;
    oc_mqtt_profile_cmdresp_t cmdresp;
    obj_root = cJSON_Parse(cmd->payload);
    if (NULL == obj_root)
    {
        goto EXIT_JSONPARSE;
    }

    obj_cmdname = cJSON_GetObjectItem(obj_root, "command_name");
    if (NULL == obj_cmdname)
    {
        goto EXIT_CMDOBJ;
    }
    if (0 == strcmp(cJSON_GetStringValue(obj_cmdname), "Agriculture_Control_light"))
    {
        obj_paras = cJSON_GetObjectItem(obj_root, "paras");
        if (NULL == obj_paras)
        {
            goto EXIT_OBJPARAS;
        }
        obj_para = cJSON_GetObjectItem(obj_paras, "Light");
        if (NULL == obj_para)
        {
            goto EXIT_OBJPARA;
        }
        ///< operate the LED here
        if (0 == strcmp(cJSON_GetStringValue(obj_para), "ON"))
        {
            g_app_cb.led = 1;
            Light_StatusSet(ON);
            printf("Light On!");
        }
        else
        {
            g_app_cb.led = 0;
            Light_StatusSet(OFF);
            printf("Light Off!");
        }
        cmdret = 0;
    }
    else if (0 == strcmp(cJSON_GetStringValue(obj_cmdname), "Agriculture_Control_Motor"))
    {
        obj_paras = cJSON_GetObjectItem(obj_root, "Paras");
        if (NULL == obj_paras)
        {
            goto EXIT_OBJPARAS;
        }
        obj_para = cJSON_GetObjectItem(obj_paras, "Motor");
        if (NULL == obj_para)
        {
            goto EXIT_OBJPARA;
        }
        ///< operate the Motor here
        if (0 == strcmp(cJSON_GetStringValue(obj_para), "ON"))
        {
            g_app_cb.motor = 1;
            Motor_StatusSet(ON);
            printf("Motor On!");
        }
        else
        {
            g_app_cb.motor = 0;
            Motor_StatusSet(OFF);
            printf("Motor Off!");
        }
        cmdret = 0;
    }

EXIT_OBJPARA:
EXIT_OBJPARAS:
EXIT_CMDOBJ:
    cJSON_Delete(obj_root);
EXIT_JSONPARSE:
    ///< do the response
    cmdresp.paras = NULL;
    cmdresp.request_id = cmd->request_id;
    cmdresp.ret_code = cmdret;
    cmdresp.ret_name = NULL;
    (void)oc_mqtt_profile_cmdresp(NULL, &cmdresp);
    return;
}

/***********************************************************************
* 函数名称: task_main_entry
* 说    明: 任务主线程，处理队列中接收到的消息
* 参    数: 无
* 返 回 值: 0（不退出）
************************************************************************/
static int task_main_entry(void)
{
    app_msg_t *app_msg;

    uint32_t ret = WifiConnect("1000后", "12345678");   //需要设置连接WIFI

    device_info_init(CLIENT_ID, USERNAME, PASSWORD);    //配置设备信息
    oc_mqtt_init(); //初始化oc_mqtt
    oc_set_cmd_rsp_cb(oc_cmd_rsp_cb);      //设置命令的回调函数oc_cmd_rsp_cb

    while (1)
    {
        app_msg = NULL;
        (void)osMessageQueueGet(mid_MsgQueue, (void **)&app_msg, NULL, 0U);     //获取队列里面的消息
        if (NULL != app_msg)
        {
            switch (app_msg->msg_type)
            {
            case en_msg_cmd:    //如果获取到cmd的消息，就调用cmd消息处理函数
                deal_cmd_msg(&app_msg->msg.cmd);
                break;
            case en_msg_report: //如果获取到report消息，就对数据进行上报处理
                deal_report_msg(&app_msg->msg.report);
                break;
            default:
                break;
            }
            free(app_msg);
        }
    }
    return 0;
}

/***********************************************************************
* 函数名称: InitIO2
* 说    明: 初始化GPIO2用于控制电平输出
* 参    数: 无
* 返 回 值: 无
************************************************************************/
#define IO2_GPIO_NAME HI_GPIO_IDX_2
void InitIO2(void)
{
    hi_gpio_init();  // 初始化 GPIO 模块
    hi_gpio_set_dir(IO2_GPIO_NAME, HI_GPIO_DIR_OUT);  // 设置为输出模式
}

/***********************************************************************
* 函数名称: task_sensor_entry
* 说    明: 传感器数据采集任务，获取环境数据并上报
* 参    数: 无
* 返 回 值: 0（不退出）
************************************************************************/
static int task_sensor_entry(void)
{
    app_msg_t *app_msg;
    E53_IA1_Data_TypeDef data;
    double lat = 0.0, lon = 0.0;  // 存储GPS经纬度
    E53_IA1_Init();
    max30102_app_entry();
    printf("初始化完成\n");
    UartExampleEntry();
    printf("初始化定位成功\n");
    while (1)
    {
        E53_IA1_Read_Data(&data);
        RunGPS(&lat, &lon);
        app_msg = malloc(sizeof(app_msg_t));
        cir_hs();
        printf("SENSOR:lum:%.2f temp:%.2f hum:%.2f lat=%.6f, lon=%.6f \r\n", data.Lux, data.Temperature, data.Humidity, lat, lon);
        printf("SENSOR:Heart_rate: %d\nSO2: %d\r\n",g_heart_rate,g_spo2);
        if (data.Temperature > 31.0 || g_heart_rate > 99) {
            hi_io_set_func(HI_IO_NAME_GPIO_2, HI_IO_FUNC_GPIO_1_GPIO);
            hi_gpio_set_dir(HI_GPIO_IDX_2, HI_GPIO_DIR_OUT);               
            hi_gpio_set_ouput_val(HI_GPIO_IDX_2, HI_GPIO_VALUE1);       
        } else {
            hi_io_set_func(HI_IO_NAME_GPIO_2, HI_IO_FUNC_GPIO_1_GPIO);
            hi_gpio_set_dir(HI_GPIO_IDX_2, HI_GPIO_DIR_OUT);         
            hi_gpio_set_ouput_val(HI_GPIO_IDX_2, HI_GPIO_VALUE0); 
        }
        if (NULL != app_msg)
        {
            app_msg->msg_type = en_msg_report;
            app_msg->msg.report.hum = (int)data.Humidity;
            app_msg->msg.report.lum = (int)data.Lux;
            app_msg->msg.report.temp = (int)data.Temperature;
            app_msg->msg.report.heart_rate = g_heart_rate;
            app_msg->msg.report.spo2 = g_spo2;
            app_msg->msg.report.lat = lat;
            app_msg->msg.report.lon = lon;
            if (0 != osMessageQueuePut(mid_MsgQueue, &app_msg, 0U, 0U))
            {
                free(app_msg);
            }
        }
        osDelay(500);
    }
    return 0;
}


/***********************************************************************
* 函数名称: OC_Demo
* 说    明: 应用程序主入口，创建主任务与传感器任务
* 参    数: 无
* 返 回 值: 无
************************************************************************/
static void OC_Demo(void)
{
    mid_MsgQueue = osMessageQueueNew(MSGQUEUE_OBJECTS, 10, NULL);
    if (mid_MsgQueue == NULL)
    {
        printf("Falied to create Message Queue!\n");
    }

    osThreadAttr_t attr;

    attr.name = "task_main_entry";
    attr.attr_bits = 0U;
    attr.cb_mem = NULL;
    attr.cb_size = 0U;
    attr.stack_mem = NULL;
    attr.stack_size = 4096;
    attr.priority = 24;

    if (osThreadNew((osThreadFunc_t)task_main_entry, NULL, &attr) == NULL)
    {
        printf("Falied to create task_main_entry!\n");
    }
    attr.stack_size = 5170;
    attr.priority = 25;
    attr.name = "task_sensor_entry";
    printf("创建传感器任务\n");
    if (osThreadNew((osThreadFunc_t)task_sensor_entry, NULL, &attr) == NULL)
    {
        printf("Falied to create task_sensor_entry!\n");
    }
}

APP_FEATURE_INIT(OC_Demo);
