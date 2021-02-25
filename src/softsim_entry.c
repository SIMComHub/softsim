/*============================================================================
*        
*     Copyright (c) 2019 LINKSFIELD Ltd.       
*        
*============================================================================
* @file name	:	softsim_entry.c
* @author		:   
* @date			:   
* @description	:	softsim task
*============================================================================*/
#include "rcinit.h"
#include "qapi_timer.h"
#include "qapi_diag.h"

extern links_softsim_init(void);

void quectel_softsim_entry(int ignored)
{
	rcinit_handshake_startup();

	//while(1)
	{   
		// Hyman_20190424, below codes just for test
		QAPI_MSG_SPRINTF(MSG_SSID_LINUX_DATA , MSG_LEGACY_HIGH, "quectel_softsim_entry, loop entry");
        qapi_Timer_Sleep(1, QAPI_TIMER_UNIT_SEC, true);//21s delay to catch boot log; 1s is normal
		links_softsim_init();
		QAPI_MSG_SPRINTF(MSG_SSID_LINUX_DATA , MSG_LEGACY_HIGH, "quectel_softsim_entry, loop end");
		qapi_Timer_Sleep(1, QAPI_TIMER_UNIT_SEC, true);
		// Hyman_20190424, above codes just for test
	}
	
}

