#include <hi_task.h>
#include <hi_time.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

#ifndef __HI_TASK_TYPEDEF_FIX
#define __HI_TASK_TYPEDEF_FIX
typedef uint32_t hi_task_handle;
#endif

#define u32 uint32_t
#define u8 uint8_t
u8 max30102_Bus_Read(u8 reg);
int max30102_Bus_Write(u8 reg, u8 value);

extern void max30102_Init(void);
extern void max30102_Read_FIFO(u32 *red_led, u32 *ir_led);

#define STACK_SIZE 2048      // 增大栈空间到4KB
#define TASK_PRIOR 25

#define SAMPLE_NUM 100     // 缓冲区大小，4秒数据
#define SAMPLE_INTERVAL_MS 40  // 采样间隔，40ms->25Hz
#define MIN_PEAK_DISTANCE 15   // 峰最小间隔，防抖，单位样本数
#define MAX_PEAKS 10            // 平均心率使用最大峰数量
#define SPO2_SAMPLE_SIZE 100    // 血氧样本量
#define MAX_SAMPLES 15          // HRV计算样本量

static u32 ir_buffer[SAMPLE_NUM] = {0};
static int buffer_index = 0;
static int last_peak_index = -MIN_PEAK_DISTANCE;

int intervals[MAX_SAMPLES] = {0};
int sample_count = 0;

int peak_intervals[MAX_PEAKS] = {0};
int peak_count = 0;

u32 red_buffer[SPO2_SAMPLE_SIZE] = {0};
u32 ir_buffer_raw[SPO2_SAMPLE_SIZE] = {0};
int spo2_index = 0;

int g_heart_rate = 0;
int g_spo2 = 0;



/***********************************************************************
* 函数名称: mean
* 功    能: 计算整型数组的均值
* 参    数: arr - 输入整型数组
*           n   - 数组长度
* 返 回 值: 平均值（double）
************************************************************************/
double mean(int *arr, int n) {
    double s = 0;
    for (int i = 0; i < n; i++) s += arr[i];
    return s / n;
}

/***********************************************************************
* 函数名称: stddev
* 功    能: 计算整型数组的标准差（用于近似HRV）
* 参    数: arr - 输入整型数组
*           n   - 数组长度
* 返 回 值: 标准差（double）
************************************************************************/
double stddev(int *arr, int n) {
    double m = mean(arr, n);
    double sum_sq = 0;
    for (int i = 0; i < n; i++) {
        double diff = arr[i] - m;
        sum_sq += diff * diff;
    }
    return sqrt(sum_sq / n);
}

// 排序比较函数
int compare_int(const void *a, const void *b) {
    return (*(int*)a - *(int*)b);
}

// 计算心率
int interval_to_hr(int interval_ms) {
    if(interval_ms == 0) return 0;
    return 60000 / interval_ms;
}

/***********************************************************************
* 函数名称: simple_mood_estimate
* 功    能: 根据当前心率和HRV进行简易心理情绪判断
* 参    数: current_hr - 当前心率（BPM）
*           hrv        - 心率变异性（标准差）
* 返 回 值: 无（打印状态信息）
************************************************************************/
void simple_mood_estimate(int current_hr, double hrv) {
    const int baseline_hr = 70;
    const double hrv_threshold = 20.0;

    const char* mood;

    if (hrv < hrv_threshold && current_hr > baseline_hr * 1.1) {
        mood = "焦虑、不安";
    } else if (hrv >= hrv_threshold && current_hr <= baseline_hr * 1.1) {
        mood = "平静、放松";
    } else {
        mood = "一般、正常";
    }

    printf("🧠 简易心理情绪判断：%s\n", mood);
}

/***********************************************************************
* 函数名称: update_heart_status
* 功    能: 更新心率状态，计算HRV、活力和心理状态
* 参    数: current_interval - 当前心跳间隔样本（单位ms）
*           positive_count   - 正向情绪计数
*           neutral_count    - 中性情绪计数
*           total_count      - 总样本数量
* 返 回 值: 无（打印分析信息）
************************************************************************/
void update_heart_status(int current_interval, int positive_count, int neutral_count, int total_count) {
    intervals[sample_count % MAX_SAMPLES] = current_interval;
    sample_count++;

    if (sample_count >= MAX_SAMPLES) {
        int tmp[MAX_SAMPLES];
        memcpy(tmp, intervals, sizeof(tmp));
        qsort(tmp, MAX_SAMPLES, sizeof(int), compare_int);

        int valid_count = MAX_SAMPLES - 4;
        int valid_intervals[valid_count];
        memcpy(valid_intervals, &tmp[2], valid_count * sizeof(int));

        double hrv = stddev(valid_intervals, valid_count);
        const char* vitality_state;
        if (hrv > 40) vitality_state = "活力满满";
        else if (hrv >= 20) vitality_state = "活力正常";
        else vitality_state = "活力不佳";

        double positive_ratio = 0.0;
        if (total_count > 0) {
            positive_ratio = ((double)(positive_count + neutral_count) / total_count) * 100.0;
        }

        const char* mood_state;
        if (positive_ratio >= 60.0) mood_state = "心情积极";
        else mood_state = "心情负向";

        int avg_interval = (int)mean(valid_intervals, valid_count);
        int current_hr = interval_to_hr(avg_interval);

        printf("❤️ 当前心率: %d BPM\n", current_hr);
        printf("🩺 身体活力 (HRV): %.2f ms，状态: %s\n", hrv, vitality_state);
        printf("😊 心理情绪正向比: %.2f%%，状态: %s\n", positive_ratio, mood_state);

        simple_mood_estimate(current_hr, hrv);
    } else {
        int current_hr = interval_to_hr(current_interval);
        printf("❤️ 当前心率（采样中）: %d BPM，等待更多数据...\n", current_hr);
    }
}

u32 smooth_ir(u32 *buffer, int len) {
    u32 sum = 0;
    for (int i = 0; i < len; i++) {
        sum += buffer[i];
    }
    return sum / len;
}

/***********************************************************************
* 函数名称: compute_spo2
* 功    能: 计算血氧饱和度（SpO2）
* 参    数: spo2_result - 指向SpO2结果的指针
* 返 回 值: 无（结果通过指针返回）
************************************************************************/
void compute_spo2(int *spo2_result) {
    u32 red_sum = 0, ir_sum = 0;
    u32 red_min = 0xFFFFFFFF, red_max = 0;
    u32 ir_min = 0xFFFFFFFF, ir_max = 0;

    for (int i = 0; i < SPO2_SAMPLE_SIZE; i++) {
        red_sum += red_buffer[i];
        ir_sum += ir_buffer_raw[i];
        if (red_buffer[i] < red_min) red_min = red_buffer[i];
        if (red_buffer[i] > red_max) red_max = red_buffer[i];
        if (ir_buffer_raw[i] < ir_min) ir_min = ir_buffer_raw[i];
        if (ir_buffer_raw[i] > ir_max) ir_max = ir_buffer_raw[i];
    }

    float red_dc = red_sum / (float)SPO2_SAMPLE_SIZE;
    float ir_dc = ir_sum / (float)SPO2_SAMPLE_SIZE;
    float red_ac = red_max - red_min;
    float ir_ac = ir_max - ir_min;

    if (ir_ac == 0 || ir_dc == 0 || red_dc == 0) {
        *spo2_result = -1;
        return;
    }

    float r = (red_ac / red_dc) / (ir_ac / ir_dc);
    float spo2 = 110.0f - 25.0f * r;
    if (spo2 > 100.0f) spo2 = 100.0f;
    if (spo2 < 0.0f) spo2 = 0.0f;

    g_spo2 = (int)(spo2 + 0.5f);
    *spo2_result = g_spo2;
}

/***********************************************************************
* 函数名称: get_current_stack_usage
* 功    能: 获取当前任务栈的使用量
* 参    数: 无
* 返 回 值: 栈使用字节数（uint32_t）
************************************************************************/
uint32_t get_current_stack_usage(void) {
    static uint32_t stack_bottom = 0;
    if (stack_bottom == 0) {
        stack_bottom = (uint32_t)&stack_bottom;
    }
    
    uint32_t current_sp;
    __asm__("mv %0, sp" : "=r" (current_sp));
    return (stack_bottom > current_sp) ? (stack_bottom - current_sp) : 0;
}

/***********************************************************************
* 函数名称: max30102_WriteReg
* 功    能: 向MAX30102寄存器写入值（带重试机制）
* 参    数: reg   - 寄存器地址
*           value - 写入的值
* 返 回 值: 0 表示成功，-1 表示写入失败
************************************************************************/
int max30102_WriteReg(u8 reg, u8 value) {
    const int max_retries = 3;
    int retries = 0;
    
    while (retries < max_retries) {
        if (max30102_Bus_Write(reg, value) == 0) {
            return 0;
        }
        
        printf("Warning: I2C write to 0x%02X failed, retry %d/%d\n", reg, retries+1, max_retries);
        retries++;
        hi_sleep(10);
    }
    
    printf("Error: Failed to write to 0x%02X after %d retries\n", reg, max_retries);
    return -1;
}

/***********************************************************************
* 函数名称: cir_hs
* 功    能: 心率和血氧计算主函数（每轮采样调用一次）
* 参    数: 无
* 返 回 值: 0 表示成功，非0表示失败
************************************************************************/
int cir_hs(void)
{
    u32 red = 0, ir = 0;
    max30102_Read_FIFO(&red, &ir);
    ir_buffer[buffer_index] = ir;
    red_buffer[spo2_index] = red;
    ir_buffer_raw[spo2_index] = ir;
    
    u32 ir_avg = smooth_ir(ir_buffer, SAMPLE_NUM);
    int prev = (buffer_index - 1 + SAMPLE_NUM) % SAMPLE_NUM;
    int next = (buffer_index + 1) % SAMPLE_NUM;
    u32 current = ir_buffer[buffer_index];

    if (current > ir_buffer[prev] && current > ir_buffer[next] &&
        current > ir_avg + 1000) {
        int interval = buffer_index - last_peak_index;
        if (interval < 0) interval += SAMPLE_NUM;
        if (interval >= MIN_PEAK_DISTANCE) {
            peak_intervals[peak_count % MAX_PEAKS] = interval;
            peak_count++;
            last_peak_index = buffer_index;
            int total = 0;
            int valid = (peak_count < MAX_PEAKS) ? peak_count : MAX_PEAKS;
            for (int i = 0; i < valid; i++) {
                total += peak_intervals[i];
            }
            int avg_interval = total / valid;
            g_heart_rate = (60 * 1000) / (avg_interval * SAMPLE_INTERVAL_MS);
            update_heart_status(avg_interval, 0, 0, 1);
        }
    }
    buffer_index = (buffer_index + 1) % SAMPLE_NUM;
    spo2_index++;
    if (spo2_index >= SPO2_SAMPLE_SIZE) {
        spo2_index = 0;
        int spo2 = 0;
        compute_spo2(&spo2);
    }
    
    return 0;
}

/***********************************************************************
* 函数名称: max30102_Task
* 功    能: MAX30102任务主循环，持续采集心率/血氧数据
* 参    数: arg - 任务参数（默认NULL）
* 返 回 值: NULL（任务结束后返回）
************************************************************************/
void *max30102_Task(void *arg)
{
    (void)arg;
    printf("Starting max30102_Init\n");
    max30102_Init();
    printf("max30102 Init Ending!\n");
    
    u8 id = max30102_Bus_Read(0xFF);
    u8 part_id = max30102_Bus_Read(0xFE);
    printf("MAX30102 Revision ID = 0x%02X, Part ID = 0x%02X\r\n", id, part_id);
    
    if(id != 0x15) {
        printf("Warning: MAX30102 Part ID mismatch! Check wiring and power.\n");
    }

    int failure_count = 0;
    
    while (1) {
        static int stack_check_counter = 0;
        if (++stack_check_counter >= 100) {
            stack_check_counter = 0;
            uint32_t stack_usage = get_current_stack_usage();
        }
        
        if (cir_hs() != 0) {
            failure_count++;
            printf("Warning: Heart rate calculation failed! Count: %d\n", failure_count);
            
            if (failure_count >= 10) {
                printf("Error: Too many failures, resetting sensor...\n");
                max30102_Init();
                failure_count = 0;
            }
        } else {
            failure_count = 0;
        }
        
        hi_sleep(SAMPLE_INTERVAL_MS);
    }

    return NULL;
}


/***********************************************************************
* 函数名称: max30102_app_init
* 功    能: 创建max30102任务并初始化相关属性
* 参    数: 无
* 返 回 值: 无
************************************************************************/
void max30102_app_init(void)
{
    printf("RUNNING max30102_app_init\n");
    hi_task_attr attr = {
        .stack_size = STACK_SIZE,
        .task_prio = TASK_PRIOR,
        .task_name = "max30102_task",
    };
    printf("Starting create hi task with stack size: %d\n", attr.stack_size);
    hi_task_handle handle;
    hi_task_create(&handle, &attr, max30102_Task, NULL);
}

/***********************************************************************
* 函数名称: max30102_app_entry
* 功    能: MAX30102应用入口函数，初始化任务
* 参    数: 无
* 返 回 值: 无
************************************************************************/
void max30102_app_entry(void)
{
    printf("RUNNING max30102_app_entry\n");
    max30102_app_init();
}