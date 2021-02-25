#ifndef __SOFTSIM_H__
#define __SOFTSIM_H__

#include "eat_fs_errcode.h"
#include "eat_fs_type.h"
#include "qapi_uart.h"

#define SOFTSIM_NULL 0
#define NVRAM_EF_SOFTSIM_WHITECARD_LID "softsim/WHITECARD"
typedef enum
{
    SOFTSIM_FALSE,
    SOFTSIM_TRUE
} SOFTSIM_bool;

typedef enum
{
    SOFTSIM_INFO,
    SOFTSIM_WARNING,
    SOFTSIM_ERROR,
    SOFTSIM_REBOOT
} SOFTSIM_event;

typedef enum
{
    SOFTSIM_EF_IMG,
    SOFTSIM_EF_INFO
}SoftsimNvid_enum;

typedef struct
{
    unsigned char  txData[261];
    unsigned int   txSize;
}SoftsimAPDU_Tx_Data_st;

typedef struct
{
    unsigned char  resData[261];
    unsigned int   resSize;
    unsigned char ret_code;
}SoftsimUssdRes_st;

typedef struct
{
    unsigned char  rxData[261];
    unsigned int   rxSize;
    unsigned short statusWord;
}SoftsimAPDU_Rx_Data_st;

typedef enum {
    PRE_GLOBAL = -1,
    GLOBAL,
    LOCAL
} SOFTSIM_STATUS;

typedef struct
{
    unsigned char user_name[32];
    unsigned char pwd[16];
    unsigned char apn[64];
    unsigned char auth_type[16];
    unsigned char dial_num[16];
    unsigned char net_type[16];
    unsigned char pdp_type[16];
} SoftsimApnProfile_st;

typedef struct
{
    unsigned short nYear;    // years since 1900
    unsigned char  nMonth;   // months since January - [0, 11]
    unsigned char  nDay;     // day of the month - [1, 31]
    unsigned char  nHour;    // hours since midnight - [0, 23]
    unsigned char  nMin;     // minutes after the hour - [0, 59]
    unsigned char  nSec;     // seconds after the minute - [0, 60] including leap second
    char  nZone;             // zone index - [-12, +12]
} SoftsimTime_st;

typedef struct
{
    SOFTSIM_STATUS status;
    SoftsimTime_st expireDate;
    SoftsimApnProfile_st apn;
} SoftsimOrderInfo_st;

typedef enum
{
    SOFTSIM_SOFTSIM_UNKNOWN_POWER_CLASS = 0,
    SOFTSIM_SOFTSIM_CLASS_A_50V = 1,
    SOFTSIM_SOFTSIM_CLASS_B_30V = 2,
    SOFTSIM_SOFTSIM_CLASS_AB    = 3,
    SOFTSIM_SOFTSIM_CLASS_C_18V = 4,
    SOFTSIM_SOFTSIM_ClASS_BC    = 6,
    SOFTSIM_SOFTSIM_CLASS_ABC   = 7
}SOFTSIM_SOFTSIM_POWER;

typedef struct SIMCARDAPDU_DATATag
{
	unsigned short  v_len;
	unsigned char   a_RData[258];
	unsigned char   v_unused1;
	unsigned char   v_unused2;
	unsigned char   v_unused3;
	unsigned char   v_unused4;
}SIMCARDAPDU_DATA;

typedef struct
{
    SOFTSIM_SOFTSIM_POWER  voltage;
}SIMCARDRESET_REQ;

typedef struct SIMCARDRESET_CNFTag
{
    unsigned char	v_VoltageValue;
    unsigned char	a_AnswerToReset[33];
    unsigned char	unused1;
    unsigned char	unused2;
}SIMCARDRESET_CNF;

typedef enum
{
    SOFTSIM_MAIN_TASK_ID,
    SOFTSIM_COS_TASK_ID,
    SOFTSIM_TASK_NUM
}SoftsimTask_enum;

typedef enum
{
    SOFTSIM_EVENT_NULL = 0,
    SOFTSIM_EVENT_USER_MSG, 
    SOFTSIM_EVENT_SIM_APDU_DATA_IND,
    SOFTSIM_EVENT_SIM_RESET_REQ,
    SOFTSIM_EVENT_USSD_RES_DATA_IND,
    SOFTSIM_EVENT_TIMER,
    SOFTSIM_EVENT_NUM
}SoftsimEvent_enum;

typedef struct
{
    TX_SEMAPHORE unused;
} *SoftsimSemId_st;
//typedef TX_SEMAPHORE SoftsimSemId_st;

typedef enum
{
    SOFTSIM_NO_WAIT,
    SOFTSIM_INFINITE_WAIT
}SoftsimWaitMode_enum;

#define SOFTSIM_USER_MSG_MAX_SIZE 64
typedef struct {
    SoftsimTask_enum  src;
    unsigned char use_point;
    unsigned char len;
    unsigned char data[SOFTSIM_USER_MSG_MAX_SIZE];
    const void *  data_p;
}SoftsimUserMsg_st;

typedef union {
    SoftsimUserMsg_st       user_msg;
    SoftsimAPDU_Tx_Data_st  tx_apdu_data;
    SoftsimUssdRes_st       res_ussd_data;
    SIMCARDRESET_REQ        sim_rst_req;
}SoftsimEventData_union;

typedef struct
{
    SoftsimEvent_enum event;
    SoftsimEventData_union data;
}SoftsimEvent_st;

typedef struct
{
   SoftsimEvent_st *ptr;
} SoftsimQueue_st;

extern void softsim_trace_hex(const uint8_t *data, uint32_t len);
extern void cos_thread(void);
extern void softsim_main_task(void);
extern int  softsim_do_at_cmd(char *command, char *resp, int resplen);
extern void softsim_enable_logfile(SOFTSIM_bool enable);
extern void softsim_get_apn(SoftsimApnProfile_st *apn);
extern void softsim_get_order_info(SoftsimOrderInfo_st *order);
extern SOFTSIM_bool softsim_switch_profile(void);
extern SOFTSIM_bool softsim_switch_profile_list(const char *switch_data);
extern int  softsim_get_profile_list(const char *query_data, char *list_buffer, int buffer_size);
extern void softsim_set_init_data_path(const unsigned short * file);

extern void softsim_trace(char *fmt,...);
extern void softsim_event(int level, char *event);
extern int softsim_get_device_imei(char *buffer, int bufferlen);
extern SOFTSIM_bool softsim_get_event_for_user(SoftsimTask_enum user, SoftsimEvent_st *event);
extern SOFTSIM_bool softsim_send_msg_to_user(SoftsimTask_enum user_src, SoftsimTask_enum user_dst, SOFTSIM_bool use_point, unsigned char len, const unsigned char *data, const unsigned char **data_p);
extern SoftsimSemId_st softsim_create_sem(unsigned char *sem_name_ptr, int initial_count);
extern SOFTSIM_bool softsim_sem_get(SoftsimSemId_st sem_id, SoftsimWaitMode_enum wait_mode);
extern void softsim_sem_put(SoftsimSemId_st sem_id);
extern void softsim_mem_init(void* mem_pool, unsigned int mem_size);
extern void* softsim_mem_alloc(unsigned int size);
/*SIMCOM qinxiaotao 2020-04-23 add for softsim crash start*/
extern void* softsim_mem_realloc(void *mem_addr, unsigned int size);
extern void* softsim_mem_calloc(unsigned int num, unsigned int size);
/*SIMCOM qinxiaotao 2020-04-23 add for softsim crash end*/
extern void softsim_mem_free(void *mem_addr);
extern unsigned int softsim_get_current_time(void);
extern int softsim_fs_Open(const unsigned short * FileName, UINT Flag);
extern int softsim_fs_Close(FS_HANDLE FileHandle);
extern int softsim_fs_Read(FS_HANDLE FileHandle, void * DataPtr, UINT Length, UINT * Read);
extern int softsim_fs_Write(FS_HANDLE FileHandle, void * DataPtr, UINT Length, UINT * Written);
extern int softsim_fs_Seek(FS_HANDLE FileHandle, int Offset, int Whence);
extern int softsim_fs_Commit(FS_HANDLE FileHandle);
extern int softsim_fs_GetFileSize(FS_HANDLE FileHandle, UINT * Size);
extern int softsim_fs_Delete(const WCHAR * FileName);
extern int softsim_fs_Rename(const WCHAR * FileName, const WCHAR * NewName);

extern void softsim_SendSIMCardResetCnfToModem(SIMCARDRESET_CNF cnf);
extern void softsim_SoftSendAPDUCnfToModem(SIMCARDAPDU_DATA rx_data);

extern SOFTSIM_bool softsim_load_nvram(SoftsimNvid_enum nv_id, char *buf, int buflen);
extern SOFTSIM_bool softsim_save_nvram(SoftsimNvid_enum nv_id, char *buf, int buflen);
extern SOFTSIM_bool softsim_load_flash(char *buf, int buflen);
extern SOFTSIM_bool softsim_save_flash(char *buf, int buflen);
extern SOFTSIM_bool softsim_send_ussd_req(char *buf, int buflen);

extern SOFTSIM_bool softsim_timer_start(unsigned int period, SOFTSIM_bool loop);
extern SOFTSIM_bool softsim_timer_stop(void);
extern SOFTSIM_bool softsim_get_network_time(SoftsimTime_st *time);

#endif
