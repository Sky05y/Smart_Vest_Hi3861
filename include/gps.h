#ifndef __GPS_H__
#define __GPS_H__

#include <stdint.h>

#define UART_BUFF_SIZE 1024
#define U_SLEEP_TIME   700000

void Uart1GpioInit(void);
void Uart1Config(void);
double dm_to_dd(double dm);
void RunGPS(double *lat, double *lon);
void UartExampleEntry(void);

#endif
