#ifndef __MAX30102_APP_H__
#define __MAX30102_APP_H__

#include <stdint.h>

typedef uint32_t u32;
typedef uint8_t u8;

#define STACK_SIZE 1024
#define TASK_PRIOR 25
#define SAMPLE_NUM 100
#define SAMPLE_INTERVAL_MS 40
#define MIN_PEAK_DISTANCE 15
#define MAX_PEAKS 10
#define SPO2_SAMPLE_SIZE 100

extern int g_heart_rate;
extern int g_spo2;

u8 max30102_Bus_Read(u8 reg);
void max30102_Init(void);
void max30102_Read_FIFO(u32 *red_led, u32 *ir_led);
void cir_hs(void);
void max30102_app_entry(void);

#endif
