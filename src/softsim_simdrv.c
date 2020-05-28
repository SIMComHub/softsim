#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
//#include "FreeRTOS.h"
#include "qapi_device_info.h"

extern int init_modem_connection(void);
extern int connect_modem(void);
extern int disconnect_modem(void);
extern int deinit_modem_connection(void);

uint32_t use_softsim = 0;

void start_sw_sim()
{
    init_modem_connection();
    connect_modem();
}

void stop_sw_sim()
{
    disconnect_modem();
    deinit_modem_connection();
}

void refresh_sw_sim()
{
    qapi_Device_Info_Reset();
    //stop_sw_sim();
    //qapi_Timer_Sleep(2,QAPI_TIMER_UNIT_SEC,true);
    //start_sw_sim();
}
