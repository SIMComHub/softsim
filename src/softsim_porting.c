#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "qurt_thread.h"
#include "qurt_pipe.h"
#include "qapi_diag.h"
#include "qapi_timer.h"
#include "qapi_uart.h"
#include "qapi_fs_types.h"
#include "qapi_fs.h"
#include "qapi_status.h"
#include "qapi_atfwd.h"
#include "qapi_device_info.h"
#include "softsim_porting.h"

#define AT_BUFFER_SIZE 256
//FILE
#define NVRAM_EF_SOFTSIM_GROUP "/softsim"
#define NVRAM_EF_SOFTSIM_IMG_LID "/softsim/IMG"
#define NVRAM_EF_SOFTSIM_INFO_LID "/softsim/INFO"
#define NVRAM_EF_SOFTSIM_START_LID "/softsim/START"
#define NVRAM_EF_SOFTSIM_INIT_LID "/INITDATA/INIT"

#define NVRAM_EF_SOFTSIM_IMG_SIZE 2000
#define NVRAM_EF_SOFTSIM_INFO_SIZE 256

extern uint32_t use_softsim;
extern void start_sw_sim(void);
extern void stop_sw_sim(void);
extern void refresh_sw_sim(void);

const int cmd_off = sizeof("AT+PSOFTSIM") - 1;

static SIMCARDRESET_CNF g_rst_cnf_data;
static SoftsimAPDU_Rx_Data_st g_rx_data;
static SoftsimSemId_st soft_sim_data;
static SoftsimSemId_st soft_sim_rst;
//static SoftsimEvent_st *soft_sim_event_ptr;
static char imei_buffer[16] = {0};

/**************************************************************************
*                                TASK DEFINE
***************************************************************************/
#define SOFTSIM_QUEUE_SIZE 24
#define SOFTSIM_MAIN_STACK_SIZE 5120
#define SOFTSIM_COS_STACK_SIZE 5120
/**************************************************************************
*                                 GLOBAL
***************************************************************************/
/*TIMER*/
qapi_TIMER_handle_t softsim_timer;
qapi_TIMER_define_attr_t ss_timer_def_attr;
qapi_TIMER_set_attr_t ss_timer_set_attr;

/* uart config para*/

//SOFTSIM_UART_CONF_PARA uart_conf;

/* conter used to count the total run times for main task */
unsigned long main_thread_run_couter = 0;
unsigned long main_thread_run_couter1= 0;
/* conter used to count the total run times for sub1 task */
unsigned long sub1_thread_run_couter = 0;

/* thread handle */
qurt_thread_t softsim_main_thread_handle; 
//char *softsim_main_thread_stack = NULL;

qurt_thread_t softsim_cos_thread_handle; 
//char *softsim_cos_thread_stack = NULL;
/* TX QUEUE handle */
qurt_pipe_t softsim_task_queue[SOFTSIM_TASK_NUM];
//qurt_pipe_t tx_queue_main_handle;
//qurt_pipe_t tx_queue_cos_handle;

/* TX QUEUE buffer */
//void *task_comm_main = NULL;
//void *task_comm_cos = NULL;

//#define QT_Q_MAX_INFO_NUM	 2

#define QUEC_AT_RESULT_ERROR_V01 0 /**<  Result ERROR. 
                                         This is to be set in case of ERROR or CME ERROR. 
                                         The response buffer contains the entire details. */
#define QUEC_AT_RESULT_OK_V01 1    /**<  Result OK. This is to be set if the final response 
                                         must send an OK result code to the terminal. 
                                         The response buffer must not contain an OK.  */
#define QUEC_AT_MASK_EMPTY_V01  0  /**<  Denotes a feed back mechanism for AT reg and de-reg */
#define QUEC_AT_MASK_NA_V01 1 /**<  Denotes presence of Name field  */
#define QUEC_AT_MASK_EQ_V01 2 /**<  Denotes presence of equal (=) operator  */
#define QUEC_AT_MASK_QU_V01 4 /**<  Denotes presence of question mark (?)  */
#define QUEC_AT_MASK_AR_V01 8 /**<  Denotes presence of trailing argument operator */

extern int simcom_qapi_update_apps_list(char *path, uint8 listIndex);
extern int simcom_qapi_backup_apps_list_init(void);
extern int32 simcom_qapi_backup_apps_start(void);
extern int simcom_qapi_backup_apps_restore(uint8 bCoverAll);

extern uint32 qapi_DAM_Visual_AT_Open(void *func);
extern void qapi_DAM_Visual_AT_Input(const unsigned char *data, unsigned short length);
extern unsigned short qapi_DAM_Visual_AT_Output(unsigned char *data, unsigned short length);
extern void softsim_set_init_data_path(const unsigned short * file);

void softsim_unsolicited_message(int level, char *event)
{
    uint8_t *p_urc_buffer = NULL;
    //apb_proxy_at_cmd_result_t cmd_result;
    p_urc_buffer = (uint8_t *)softsim_mem_alloc(512);
    memset(p_urc_buffer,0,512);
    snprintf((char*)p_urc_buffer, 512, "+ISOFTSIM:%d,%s", level, event);
    
    /*
    cmd_result.result_code = APB_PROXY_RESULT_UNSOLICITED;
    cmd_result.pdata = p_urc_buffer;
    cmd_result.length = strlen((char*)p_urc_buffer);
    cmd_result.cmd_id = APB_PROXY_INVALID_CMD_ID;   
    if (apb_proxy_send_at_cmd_result(&cmd_result) != APB_PROXY_STATUS_OK)
    {
        // Error handling.
    }
    */
    if(qapi_atfwd_send_urc_resp("+ISOFTSIM",(char*)p_urc_buffer))
    {
        // Error handling.
    }
    softsim_mem_free(p_urc_buffer);
}


void softsim_trace(char *fmt,...)
{
   //QAPI_MSG_SPRINTF(MSG_SSID_LINUX_DATA , MSG_LEGACY_HIGH, "softsim event:%s",event);  
  //char buf_ptr[1024];
  va_list ap;
  char *buf_ptr = NULL;
  buf_ptr = (char *)malloc(1024);
  memset(buf_ptr,0,1024);

  va_start(ap, fmt);
  vsnprintf( buf_ptr, (size_t)1024, fmt, ap );
  va_end( ap );
  QAPI_MSG_SPRINTF( MSG_SSID_LINUX_DATA, MSG_LEGACY_HIGH, "%s", buf_ptr);
  if(buf_ptr)
  	free(buf_ptr);
}

SOFTSIM_bool system_set_apn(void)
{
  char apn_buffer[128];
  unsigned short ret;
  int i;
  SoftsimApnProfile_st *apn=NULL;

  apn = (SoftsimApnProfile_st *)malloc(sizeof(SoftsimApnProfile_st));

  if(apn == NULL)
  {
    softsim_trace("malloc failed");
    return (SOFTSIM_bool)(0);
  }

  memset(apn, 0, sizeof(SoftsimApnProfile_st));

  softsim_get_apn(apn);

  QAPI_MSG_SPRINTF(MSG_SSID_LINUX_DATA , MSG_LEGACY_HIGH, "user %s, pwd %s, apn_name %s", apn->user_name, apn->pwd, apn->apn);
  QAPI_MSG_SPRINTF(MSG_SSID_LINUX_DATA , MSG_LEGACY_HIGH, "auth_type %s, net_type %s, pdp_type %s", apn->auth_type, apn->net_type, apn->pdp_type);

  //////// visual AT com to set apn ////////
  if (qapi_DAM_Visual_AT_Open(NULL))
  {
    for (i = 0; i < 3; ++i)
    {
      sprintf((char* )apn_buffer,"at+cgdcont=1,\"IPV4V6\",\"%s\"\r\n",apn->apn);
      qapi_DAM_Visual_AT_Input((const unsigned char*)apn_buffer,strlen(apn_buffer));
      qapi_Timer_Sleep(200, QAPI_TIMER_UNIT_MSEC, true);
      ret=qapi_DAM_Visual_AT_Output((unsigned char*)apn_buffer,sizeof(apn_buffer));
      if (strstr((const char*)apn_buffer,"OK"))
      {
        QAPI_MSG_SPRINTF(MSG_SSID_LINUX_DATA , MSG_LEGACY_HIGH, "at+cgdcont OK, ret=%d",ret);
        break;
      }else
      {
        QAPI_MSG_SPRINTF(MSG_SSID_LINUX_DATA , MSG_LEGACY_HIGH, "at+cgdcont ERROR, ret=%d,%s",ret,apn_buffer);
      }
    }
  }else
  {
    QAPI_MSG_SPRINTF(MSG_SSID_LINUX_DATA , MSG_LEGACY_HIGH, "failed to open visual at");
  }
  //////////////////////////////////////////
  free(apn);
  return (SOFTSIM_bool)(0);
}


void softsim_event(int level, char *event)
{
    //TODO: report softsim event to MCU, implement your method if have different way (optional)
    QAPI_MSG_SPRINTF(MSG_SSID_LINUX_DATA , MSG_LEGACY_HIGH, "softsim event:%s",event); 
    softsim_unsolicited_message(level, event);
    if (level == SOFTSIM_REBOOT)
    {
        //TODO: level == SOFTSIM_REBOOT, need do reboot to refresh softsim
        //TODO: implement your method if have different way (OTA mandatory)
        refresh_sw_sim();
    }
    else if (level == SOFTSIM_INFO && !strcmp(event, "Personalize success"))
    {
        //TODO: personalize finished, please change USB setting
        //TODO: from AT function to normal function (optional)
    }
    else if (level == SOFTSIM_INFO && !strcmp(event, "ready"))
    {
        if (use_softsim) 
        {
            system_set_apn();
            start_sw_sim();
        }
    }
}

/*
static int32_t user_cgsn_callback(ril_cmd_response_t *response)
{
    ril_serial_number_rsp_t *parm = (ril_serial_number_rsp_t *)response->cmd_param;
    if (parm && parm->value.imei)
    {
        memcpy(imei_buffer, parm->value.imei, strlen(parm->value.imei));
        imei_buffer[15] = '\0';
    }
    return 0;
}
*/

int softsim_get_device_imei(char *buffer, int bufferlen)
{
    /*
    //TODO: implement real fetch imei method (mandatory)
    ril_request_serial_number(RIL_EXECUTE_MODE, 1, user_cgsn_callback, NULL);
    strncpy(buffer, imei_buffer, bufferlen);
    return 0;
    */
    qapi_Device_Info_t *dev_info;

    dev_info = (qapi_Device_Info_t *) malloc(sizeof(qapi_Device_Info_t));
    memset(dev_info, 0, sizeof(qapi_Device_Info_t));
    qapi_Device_Info_Init();
    qapi_Device_Info_Get(QAPI_DEVICE_INFO_IMEI_E, dev_info);   
    memcpy(imei_buffer, dev_info->u.valuebuf.buf, dev_info->u.valuebuf.len);
    strncpy(buffer, imei_buffer, bufferlen);
    if(dev_info)
  	free(dev_info);
    return 0;
}

SOFTSIM_bool softsim_get_event_for_user(SoftsimTask_enum user, SoftsimEvent_st *event)
{
    SoftsimQueue_st queue_buf;

    if (event == NULL || (user != SOFTSIM_MAIN_TASK_ID && user != SOFTSIM_COS_TASK_ID))
    {
        return SOFTSIM_FALSE;
    }
    /* rec data from main task by queue */
    //QAPI_MSG_SPRINTF(MSG_SSID_LINUX_DATA , MSG_LEGACY_HIGH, "rec data from main/cos task by queue!"); 
    //queue_buf.ptr = soft_sim_event_ptr;	
    qurt_pipe_receive(softsim_task_queue[user], &queue_buf);

    memcpy(event, queue_buf.ptr, sizeof(SoftsimEvent_st));
    free(queue_buf.ptr);
    QAPI_MSG_SPRINTF(MSG_SSID_LINUX_DATA , MSG_LEGACY_HIGH, "rec data from queue,event id:%d!",event->event); 

    return SOFTSIM_TRUE;
}

SOFTSIM_bool softsim_send_ussd_req(char *buf, int buflen)
{
    //TODO: if l4c_ss_exe_ussd_req() can not work, please implement method to send USSD (OTA mandatory)
    return SOFTSIM_FALSE;
}

SOFTSIM_bool softsim_send_msg_to_user(SoftsimTask_enum user_src, SoftsimTask_enum user_dst, SOFTSIM_bool use_point, unsigned char len, const unsigned char *data, const unsigned char **data_p)
{
    SoftsimQueue_st req;
	
    if(user_dst != SOFTSIM_MAIN_TASK_ID && user_dst != SOFTSIM_COS_TASK_ID)
    {
        return SOFTSIM_FALSE;
    }

    QAPI_MSG_SPRINTF(MSG_SSID_LINUX_DATA , MSG_LEGACY_HIGH, "send msg to user by queue!"); 
	
    //soft_sim_event_ptr = (SoftsimEvent_st *)malloc(sizeof(SoftsimEvent_st));
   
    //req.ptr = soft_sim_event_ptr;
    //req.ptr = ( SoftsimEvent_st *)malloc(sizeof(SoftsimEvent_st));
    req.ptr = ( SoftsimEvent_st *)malloc(sizeof(SoftsimEvent_st));
    req.ptr->event = SOFTSIM_EVENT_USER_MSG;
    req.ptr->data.user_msg.data_p = *data_p ;
    req.ptr->data.user_msg.src    = user_src;          
    /* send data to sub1 task by queue */
    qurt_pipe_send(softsim_task_queue[user_dst], &req);
    //if(req.ptr)
  	//free(req.ptr);
    return SOFTSIM_TRUE;
}

SoftsimSemId_st softsim_create_sem(unsigned char *sem_name_ptr, int initial_count)
{
    //SemaphoreHandle_t sem_id = xSemaphoreCreateCounting(10, initial_count);
    TX_SEMAPHORE *sem_id;
    int result = 0;
    
    sem_id = (TX_SEMAPHORE *)malloc(sizeof(TX_SEMAPHORE));
    
    result = tx_semaphore_create(sem_id, (char *)sem_name_ptr, initial_count);
    QAPI_MSG_SPRINTF(MSG_SSID_LINUX_DATA , MSG_LEGACY_HIGH, "creat sem %s result:%d",sem_name_ptr, result); 
    //if (&sem_id == NULL)
    //{
    //	  softsim_trace("failed to create sem %s", sem_name_ptr);	
    //}
    return (SoftsimSemId_st)sem_id;
}

SOFTSIM_bool softsim_sem_get(SoftsimSemId_st sem_id, SoftsimWaitMode_enum wait_mode)
{
    //QAPI_MSG_SPRINTF(MSG_SSID_LINUX_DATA , MSG_LEGACY_HIGH, "get sem!"); 
    return tx_semaphore_get((TX_SEMAPHORE *)sem_id, wait_mode == SOFTSIM_NO_WAIT ? 0 : TX_WAIT_FOREVER) == TX_SUCCESS? SOFTSIM_TRUE : SOFTSIM_FALSE;       
}

void softsim_sem_put(SoftsimSemId_st sem_id)
{
    //QAPI_MSG_SPRINTF(MSG_SSID_LINUX_DATA , MSG_LEGACY_HIGH, "put sem!");
    //if (&sem_id != NULL)
    //{
        //xSemaphoreGive((SemaphoreHandle_t)sem_id);
        tx_semaphore_put((TX_SEMAPHORE *)sem_id);
    //}
}

void softsim_mem_init(void* mem_pool, unsigned int mem_size)
{

}

void* softsim_mem_alloc(unsigned int size)
{
    /*SIMCOM qinxiaotao 2020-04-23 add for softsim crash start*/
    //return malloc(size);
    void * p = NULL;
    p = malloc(size);

    if(p == NULL)
    {
        softsim_trace("malloc failed");
        qapi_Timer_Sleep(2, QAPI_TIMER_UNIT_SEC, true);
    }
    return p;
    /*SIMCOM qinxiaotao 2020-04-23 add for softsim crash end*/
}

void softsim_mem_free(void *mem_addr)
{
    free(mem_addr);
}

/*SIMCOM qinxiaotao 2020-04-23 add for softsim crash start*/
void* softsim_mem_realloc(void *mem_addr, unsigned int size)
{
    void * p = NULL;
    p = realloc(mem_addr, size);

    if(p == NULL)
    {
        softsim_trace("malloc failed");
        qapi_Timer_Sleep(2, QAPI_TIMER_UNIT_SEC, true);
    }
    return p;
}

void* softsim_mem_calloc(unsigned int num, unsigned int size)
{
    return calloc(num,size);
}
/*SIMCOM qinxiaotao 2020-04-23 add for softsim crash end*/

unsigned int softsim_get_current_time(void)
{
    qapi_time_get_t curr_time;
    //return ust_get_current_time();
    qapi_time_get(QAPI_TIME_SECS, &curr_time);
    return curr_time.time_secs;
}

int softsim_fs_Open(const unsigned short * FileName, UINT Flag)
{
    //int  open_flag = 0;
    int  write_fd = -1;
    
    //open_flag = QAPI_FS_O_WRONLY_E | QAPI_FS_O_CREAT_E | QAPI_FS_O_TRUNC_E;
    if(QAPI_OK != qapi_FS_Open((const char *)FileName, Flag, &write_fd))
    {
        return EAT_FS_FILE_NOT_FOUND;
    }
    return write_fd;
}

int softsim_fs_Close(FS_HANDLE FileHandle)
{
     return qapi_FS_Close(FileHandle);     
}

int softsim_fs_Read(FS_HANDLE FileHandle, void * DataPtr, UINT Length, UINT * Read)
{
    UINT i=0,j=0;
    int ret=-1;
    while (i<Length)
    {
        j= Length-i;
        //30k
        if (j>30720)
        {
            j=30720;
        }
        ret=qapi_FS_Read(FileHandle,(uint8*)((char*)DataPtr+i),j,(uint32 *)Read);

        if (ret || *Read!=j)
        {
            *Read +=i;
            return ret;
        }
        i += j;
    }
    *Read=i;
    return ret;
}

int softsim_fs_Write(FS_HANDLE FileHandle, void * DataPtr, UINT Length, UINT * Written)
{
    return qapi_FS_Write(FileHandle, DataPtr, Length, (uint32 *)Written);
}

int softsim_fs_Seek(FS_HANDLE FileHandle, int Offset, int Whence)
{
    qapi_FS_Offset_t seek_offset = 0;
    QAPI_MSG_SPRINTF(MSG_SSID_LINUX_DATA , MSG_LEGACY_HIGH, "fs:%d!",FileHandle);
    Whence++;
    if(Whence == 3) Whence++;
    qapi_FS_Seek(FileHandle, Offset, Whence, &seek_offset);
    QAPI_MSG_SPRINTF(MSG_SSID_LINUX_DATA , MSG_LEGACY_HIGH, "fs:%d,offset:%d,result_offset:%d!",FileHandle,Offset,seek_offset); 
    return seek_offset;
}

int softsim_fs_Commit(FS_HANDLE FileHandle)
{
    return EAT_FS_NO_ERROR;
}

int softsim_fs_GetFileSize(FS_HANDLE FileHandle, UINT * Size)
{   
    struct qapi_FS_Stat_Type_s stat;

    stat.st_size = 0;
    qapi_FS_Stat_With_Handle(FileHandle, &stat);
    *Size = stat.st_size;
    return EAT_FS_NO_ERROR;
}

int softsim_fs_Delete(const WCHAR * FileName)
{
    return qapi_FS_Unlink((const char *)FileName);
}

int softsim_fs_Rename(const WCHAR * FileName, const WCHAR * NewName)
{
    return qapi_FS_Rename((const char *)FileName,(const char *)NewName);
}

void vTimerCallback(uint32 userData)
{
    //SoftsimEvent_st req;
     SoftsimQueue_st req;

    //req.ptr = ( SoftsimEvent_st *)malloc(sizeof(SoftsimEvent_st));
    //soft_sim_event_ptr = (SoftsimEvent_st *)malloc(sizeof(SoftsimEvent_st));
    QAPI_MSG_SPRINTF(MSG_SSID_LINUX_DATA , MSG_LEGACY_HIGH, " timer callback!");
    //req.ptr = soft_sim_event_ptr;
    req.ptr = ( SoftsimEvent_st *)malloc(sizeof(SoftsimEvent_st));
    req.ptr->event = SOFTSIM_EVENT_TIMER;
    /* send data to sub1 task by queue */
    //tx_queue_send(softsim_task_queue[SOFTSIM_MAIN_TASK_ID], &req, TX_WAIT_FOREVER);
    qurt_pipe_send(softsim_task_queue[SOFTSIM_MAIN_TASK_ID], &req);
    //if(req.ptr)
  	//free(req.ptr);
}


/* Timer start/stop functions */
SOFTSIM_bool softsim_timer_start(unsigned int period, SOFTSIM_bool loop)
{
    qapi_Status_t result = QAPI_ERROR;

    if(softsim_timer == NULL)
    {
        QAPI_MSG_SPRINTF(MSG_SSID_LINUX_DATA , MSG_LEGACY_HIGH, " start timer!");
        memset(&ss_timer_def_attr, 0, sizeof(ss_timer_def_attr));
        ss_timer_def_attr.cb_type	= QAPI_TIMER_FUNC1_CB_TYPE;
        ss_timer_def_attr.deferrable = false;
        ss_timer_def_attr.sigs_func_ptr = vTimerCallback;
        ss_timer_def_attr.sigs_mask_data = 0x11;
        result = qapi_Timer_Def(&softsim_timer, &ss_timer_def_attr);
        //qt_uart_dbg(uart_conf.hdlr,"[TIMER] status[%d]", result);
        if(result != QAPI_OK)
        {
            return SOFTSIM_FALSE;
        }

        memset(&ss_timer_set_attr, 0, sizeof(ss_timer_set_attr));
        ss_timer_set_attr.reload = (loop == SOFTSIM_TRUE ? period:0);
        ss_timer_set_attr.time = period;
        ss_timer_set_attr.unit = QAPI_TIMER_UNIT_MSEC;
        result = qapi_Timer_Set(softsim_timer, &ss_timer_set_attr);
        //qt_uart_dbg(uart_conf.hdlr,"[TIMER] status[%d]", result);
        if(result != QAPI_OK)
        {
            return SOFTSIM_FALSE;
        }
    }
    return SOFTSIM_TRUE;
}

SOFTSIM_bool softsim_timer_stop(void)
{
    if(softsim_timer  != NULL)
    {
        QAPI_MSG_SPRINTF(MSG_SSID_LINUX_DATA , MSG_LEGACY_HIGH, " stop a running timer!");
        // stop a running timer
        qapi_Timer_Stop(softsim_timer);
        // Undef the timer. Releases memory allocated in qapi_Timer_Def()
        qapi_Timer_Undef(softsim_timer);
    }
    return SOFTSIM_TRUE;
}

SOFTSIM_bool softsim_get_network_time(SoftsimTime_st *time)
{
    //TODO: when no network connected or no valid network time got, please return SOFTSIM_FALSE. 
    //TODO: otherwise, please fill the time struct refer to SoftsimTime_st definition, and return SOFTSIM_TRUE. 
    return SOFTSIM_FALSE;
}

void softsim_take_soft_sim_data_sem(void)
{
    softsim_sem_get(soft_sim_data, SOFTSIM_INFINITE_WAIT);
}

void softsim_give_soft_sim_data_sem(void)
{
    softsim_sem_put(soft_sim_data);
}

void softsim_take_soft_sim_rst_sem(void)
{
    softsim_sem_get(soft_sim_rst, SOFTSIM_INFINITE_WAIT);
}

void softsim_give_soft_sim_rst_sem(void)
{
    softsim_sem_put(soft_sim_rst);
}

SIMCARDRESET_CNF *softsim_get_rst_cnf_data(void)
{
    return &g_rst_cnf_data;
}

void softsim_SendSIMCardResetCnfToModem(SIMCARDRESET_CNF cnf)
{
    memcpy(&g_rst_cnf_data, &cnf, sizeof(SIMCARDRESET_CNF));
    softsim_give_soft_sim_rst_sem();
}

void softsim_apdu_send(SoftsimAPDU_Tx_Data_st *tx_data)
{
    //SoftsimEvent_st req;
     SoftsimQueue_st req;
    QAPI_MSG_SPRINTF(MSG_SSID_LINUX_DATA , MSG_LEGACY_HIGH, "softsim apdu send!");
    //req.ptr = ( SoftsimEvent_st *)malloc(sizeof(SoftsimEvent_st));
    //soft_sim_event_ptr = (SoftsimEvent_st *)malloc(sizeof(SoftsimEvent_st));
    //req.ptr = soft_sim_event_ptr;
    req.ptr = ( SoftsimEvent_st *)malloc(sizeof(SoftsimEvent_st));
    req.ptr->event = SOFTSIM_EVENT_SIM_APDU_DATA_IND;
    memcpy(&req.ptr->data.tx_apdu_data, tx_data, sizeof(SoftsimAPDU_Tx_Data_st));
    /* send data to sub1 task by queue */
    //tx_queue_send(softsim_task_queue[SOFTSIM_MAIN_TASK_ID], &req, TX_WAIT_FOREVER);
    qurt_pipe_send(softsim_task_queue[SOFTSIM_MAIN_TASK_ID], &req);
    //if(req.ptr)
  	//free(req.ptr);
}
/*
void softsim_ussd_response(SoftsimUssdRes_st *res_data)
{
	SoftsimEvent_st req;
	req.event = SOFTSIM_EVENT_USSD_RES_DATA_IND;
	memcpy(&req.data.res_ussd_data, res_data, sizeof(SoftsimUssdRes_st));
	xQueueSend(softsim_task_queue[SOFTSIM_MAIN_TASK_ID], &req, 0);
}
*/
SoftsimAPDU_Rx_Data_st *softsim_get_apdu_rx_data(void)
{
    return &g_rx_data;
}

void softsim_SoftSendAPDUCnfToModem(SIMCARDAPDU_DATA rx_data)
{
    if (rx_data.v_len > 2)
    {
        memcpy(&g_rx_data.rxData[0],&rx_data.a_RData[0],rx_data.v_len - 2);
    }
    g_rx_data.rxSize = rx_data.v_len - 2;
    g_rx_data.statusWord = (rx_data.a_RData[rx_data.v_len - 1] | (rx_data.a_RData[rx_data.v_len - 2] << 8));
    softsim_give_soft_sim_data_sem();
}

void softsim_rst_send(void)
//void softsim_rst_send(DCL_SIM_POWER ExpectVolt)
{
    SoftsimQueue_st req;

    //req.ptr = ( SoftsimEvent_st *)malloc(sizeof(SoftsimEvent_st));
    //soft_sim_event_ptr = (SoftsimEvent_st *)malloc(sizeof(SoftsimEvent_st));
    QAPI_MSG_SPRINTF(MSG_SSID_LINUX_DATA , MSG_LEGACY_HIGH, "softsim rst send!");
    //req.ptr = soft_sim_event_ptr;
    req.ptr = ( SoftsimEvent_st *)malloc(sizeof(SoftsimEvent_st));
    req.ptr->event = SOFTSIM_EVENT_SIM_RESET_REQ;
    qurt_pipe_send(softsim_task_queue[SOFTSIM_MAIN_TASK_ID], &req);
}

SOFTSIM_bool softsim_load_nvram(SoftsimNvid_enum nv_id, char *buf, int buflen)
{
    int  ret_val = -1;
	char *nv_buffer;
	const char* id;
	uint32_t buffer_size;
    int    fd = -1;
    int bytes_read;

    //QAPI_MSG_SPRINTF(MSG_SSID_LINUX_DATA , MSG_LEGACY_HIGH, "softsim load nvram!");  

	if (nv_id == SOFTSIM_EF_IMG)
	{
		id = NVRAM_EF_SOFTSIM_IMG_LID;
		buffer_size = NVRAM_EF_SOFTSIM_IMG_SIZE;
	}
	else if (nv_id == SOFTSIM_EF_INFO)
	{
		id = NVRAM_EF_SOFTSIM_INFO_LID;
		buffer_size = NVRAM_EF_SOFTSIM_INFO_SIZE;
	}
	else
	{
		return SOFTSIM_FALSE;
	}
	nv_buffer = softsim_mem_alloc(buffer_size);
    ret_val = qapi_FS_Open(id, QAPI_FS_O_RDWR_E | QAPI_FS_O_CREAT_E, &fd);
	if (nv_buffer == NULL)
	{
		return SOFTSIM_FALSE;
	}
	memset(nv_buffer, 0, buffer_size);
    //ret_val = nvdm_read_data_item(NVRAM_EF_SOFTSIM_GROUP, id, (uint8_t *)nv_buffer, &buffer_size);
    ret_val == qapi_FS_Read(fd, (uint8*)nv_buffer, buffer_size, (uint32 *)&bytes_read);

	memcpy(buf, nv_buffer, buflen > buffer_size ? buffer_size : buflen);
    //softsim_trace_hex((uint8_t*)buf, buflen); 
	//softsim_trace("[%s] id %s, result %d", __FUNCTION__, id, ret_val);
    qapi_FS_Close(fd);
    softsim_mem_free(nv_buffer);
	return ret_val == QAPI_OK ? SOFTSIM_TRUE : SOFTSIM_FALSE;
}

SOFTSIM_bool softsim_save_nvram(SoftsimNvid_enum nv_id, char *buf, int buflen)
{
	int  ret_val = -1;
	char *nv_buffer;
	const char* id;
	uint32_t buffer_size;
      int    fd = -1;
      int   wr_bytes = 0;

    QAPI_MSG_SPRINTF(MSG_SSID_LINUX_DATA , MSG_LEGACY_HIGH, "softsim save nvram!");   

	if (nv_id == SOFTSIM_EF_IMG)
	{
		id = NVRAM_EF_SOFTSIM_IMG_LID;
		buffer_size = NVRAM_EF_SOFTSIM_IMG_SIZE;
	}
	else if (nv_id == SOFTSIM_EF_INFO)
	{
		id = NVRAM_EF_SOFTSIM_INFO_LID;
		buffer_size = NVRAM_EF_SOFTSIM_INFO_SIZE;
	}
	else
	{
		return SOFTSIM_FALSE;
	}
	nv_buffer = softsim_mem_alloc(buffer_size);
	if (nv_buffer == NULL)
	{
		return SOFTSIM_FALSE;
	}
	memset(nv_buffer, 0, buffer_size);
	memcpy(nv_buffer, buf, buflen > buffer_size ? buffer_size : buflen);

    //softsim_trace_hex((uint8_t*)nv_buffer, buffer_size); 
    ret_val = qapi_FS_Open(id, QAPI_FS_O_RDWR_E | QAPI_FS_O_CREAT_E, &fd);
    ret_val == qapi_FS_Write(fd, (uint8*)nv_buffer, buffer_size, (uint32 *)&wr_bytes);
    qapi_FS_Close(fd);
	//ret_val = nvdm_write_data_item(NVRAM_EF_SOFTSIM_GROUP, id,  NVDM_DATA_ITEM_TYPE_RAW_DATA, (uint8_t *)nv_buffer, buffer_size);
	//softsim_trace("[%s] id %s, result %d", __FUNCTION__, id, ret_val);
	softsim_mem_free(nv_buffer);
	return ret_val == QAPI_OK ? SOFTSIM_TRUE : SOFTSIM_FALSE;
}

SOFTSIM_bool softsim_clear_flash(void)
{
    simcom_qapi_update_apps_list("", 9); 
    simcom_qapi_backup_apps_list_init();
    simcom_qapi_backup_apps_start();
    QAPI_MSG_SPRINTF(MSG_SSID_LINUX_DATA , MSG_LEGACY_HIGH, "clear backup flash!");
    return SOFTSIM_TRUE;
}

SOFTSIM_bool softsim_load_flash(char *buf, int buflen)
{
	//TODO: backup softsim factory data into a no-erase flash area, please implement function to read flash backup in your platform. otherwise, return SOFTSIM_FALSE
	//TODO: if don't support backup function, softsim data will lost every time to re-flash device (optional)
	//return SOFTSIM_FALSE;
    int  ret_val = -1;
	
    ret_val = qapi_FS_Mk_Dir(NVRAM_EF_SOFTSIM_GROUP, QAPI_FS_S_IRUSR_E|QAPI_FS_S_IWUSR_E|QAPI_FS_S_IXUSR_E);
    QAPI_MSG_SPRINTF( MSG_SSID_LINUX_DATA , MSG_LEGACY_HIGH, "creating softsim directory result:%d", ret_val );
    //del IMG file first,then restore
	qapi_FS_Unlink(NVRAM_EF_SOFTSIM_IMG_LID);  
    ret_val = simcom_qapi_backup_apps_restore(FALSE);  
    if(ret_val < 0)
    {
        QAPI_MSG_SPRINTF( MSG_SSID_LINUX_DATA , MSG_LEGACY_HIGH, "load backup flash error:%d", ret_val );
        return SOFTSIM_FALSE;
    } 
    softsim_load_nvram(SOFTSIM_EF_IMG, buf, buflen);
    QAPI_MSG_SPRINTF(MSG_SSID_LINUX_DATA , MSG_LEGACY_HIGH, "load backup flash!");
    //after restore,the backup will be released,so backup it again.
    //simcom_qapi_update_apps_list("/softsim/IMG", 9);
    //simcom_qapi_backup_apps_list_init();
    //simcom_qapi_backup_apps_start();
    return SOFTSIM_TRUE;
}

SOFTSIM_bool softsim_save_flash(char *buf, int buflen)
{
	//TODO: backup softsim factory data into a no-erase flash area, please implement function to write flash backup in your platform. otherwise, return SOFTSIM_FALSE
	//TODO: if don't support backup function, softsim data will lost every time to re-flash device (optional)
	//return SOFTSIM_FALSE;
    simcom_qapi_update_apps_list("/softsim/IMG", 9);
    simcom_qapi_backup_apps_list_init();
    simcom_qapi_backup_apps_start();
    QAPI_MSG_SPRINTF(MSG_SSID_LINUX_DATA , MSG_LEGACY_HIGH, "save backup flash!");
    return SOFTSIM_TRUE;
}

void softsim_task_main(void *args)
{
    uint32_t rsize = sizeof(use_softsim);
    int    fd = -1;
    int bytes_read;
    //qapi_FS_Status_t retval = 0;

    qapi_FS_Open(NVRAM_EF_SOFTSIM_START_LID, QAPI_FS_O_RDWR_E | QAPI_FS_O_CREAT_E, &fd);
    //QAPI_MSG_SPRINTF( MSG_SSID_LINUX_DATA , MSG_LEGACY_HIGH, "open file result:%d", retval );
    qapi_FS_Read(fd,  (uint8_t *)&use_softsim, rsize, (uint32 *)&bytes_read);
    qapi_FS_Close(fd);
    //QAPI_MSG_SPRINTF( MSG_SSID_LINUX_DATA , MSG_LEGACY_HIGH, "read file resullt:%d", retval );
    //start_sw_sim();
    //use_softsim = 1;
    softsim_main_task();
}

void softsim_task_cos(void *args)
{
	cos_thread();
}

void softsim_atfwd_cmd_handler_cb(boolean is_reg, char *atcmd_name,
                                 uint8* at_fwd_params, uint8 mask,
                                 uint32 at_handle)
{
    int    fd = -1;
    int   wr_bytes = 0;
    uint8_t *p_out_buffer = NULL;

    if(is_reg)  //Registration Successful,is_reg return 1 
    {
        if(mask==QUEC_AT_MASK_EMPTY_V01)
	    {
	        return;
        }
    	if( !strncasecmp(atcmd_name, "+PSOFTSIM",strlen(atcmd_name)) )
	    {
	        //Execute Mode
	        if ((QUEC_AT_MASK_NA_V01) == mask)//AT+PSOFTSIM
	        {
	            qapi_atfwd_send_resp(atcmd_name, "", QUEC_AT_RESULT_OK_V01);
		        return;
	        }
	        //Read Mode or Test Mode
	        else if (((QUEC_AT_MASK_NA_V01 | QUEC_AT_MASK_QU_V01) == mask)  //AT+PSOFTSIM?
	            |((QUEC_AT_MASK_NA_V01 | QUEC_AT_MASK_EQ_V01 | QUEC_AT_MASK_QU_V01) == mask))//AT+PSOFTSIM=?
				
	        {
	            uint8_t cmd[8] = {0};
                p_out_buffer = (uint8_t *)malloc(AT_BUFFER_SIZE);
                memset(p_out_buffer, 0, AT_BUFFER_SIZE);
                strcat((char *)cmd, "?");
                if (softsim_do_at_cmd((char *)cmd, (char *)p_out_buffer, AT_BUFFER_SIZE))
                {
                    //cmd_result.result_code = APB_PROXY_RESULT_ERROR;
                    qapi_atfwd_send_resp(atcmd_name, "", QUEC_AT_RESULT_ERROR_V01);
                }
	        }
	        //Write Mode
	        else if ((QUEC_AT_MASK_NA_V01 | QUEC_AT_MASK_EQ_V01 | QUEC_AT_MASK_AR_V01) == mask)//AT+PSOFTSIM=<value>
	        {
	            p_out_buffer = (uint8_t *)malloc(AT_BUFFER_SIZE);
                memset(p_out_buffer, 0, AT_BUFFER_SIZE);
                softsim_trace("%s", (char *)at_fwd_params);
                softsim_trace_hex((uint8_t*)at_fwd_params, strlen((char *)at_fwd_params)); 
                if (!strncmp((char *)at_fwd_params, "start", 5))
                {
                    //if (at_fwd_params[5] == ',')
                    if (at_fwd_params[5] == 0x00)
                    {
                        int start;
                        if (at_fwd_params[6] == '1')
                        {
                            start = 1;
                        }
                        else if (at_fwd_params[6] == '2')
                        {
                            //refresh
                            start = use_softsim;
                            refresh_sw_sim();
                        }
                        else if (!strncmp((char *)at_fwd_params + 6, "ER000", 5))
                        {
                            //erase
                            qapi_FS_Unlink(NVRAM_EF_SOFTSIM_IMG_LID);
                            qapi_FS_Unlink(NVRAM_EF_SOFTSIM_INFO_LID);
                            softsim_clear_flash();
                            start = 0;
                        }
                        else
                        {
                            start = 0;
                        }
                        if (start != use_softsim)
                        {
                            use_softsim = start;
                            qapi_FS_Open(NVRAM_EF_SOFTSIM_START_LID, QAPI_FS_O_RDWR_E | QAPI_FS_O_CREAT_E, &fd);
                            qapi_FS_Write(fd, (uint8*)&use_softsim, sizeof(use_softsim), (uint32 *)&wr_bytes);
                            qapi_FS_Close(fd);
                            //nvdm_write_data_item(NVRAM_EF_SOFTSIM_GROUP, NVRAM_EF_SOFTSIM_START_LID, NVDM_DATA_ITEM_TYPE_RAW_DATA, (uint8_t *)&use_softsim, sizeof(use_softsim));
                            if (use_softsim)
                                start_sw_sim();
                            else
                                stop_sw_sim();
                        }
                    }
                    else if (at_fwd_params[5] == '?')
                    {
                        strcpy((char *)p_out_buffer, "+PSOFTSIM:");
                        p_out_buffer[strlen((char *)p_out_buffer)] = '0' + use_softsim;
                    }else if (softsim_do_at_cmd((char *)at_fwd_params, (char *)p_out_buffer, AT_BUFFER_SIZE))
					{
						free((void *)p_out_buffer);
						qapi_atfwd_send_resp(atcmd_name, (char *)at_fwd_params, QUEC_AT_RESULT_ERROR_V01);
						return;
					}
                }
                else if (softsim_do_at_cmd((char *)at_fwd_params, (char *)p_out_buffer, AT_BUFFER_SIZE))
                {
                    //cmd_result.result_code = APB_PROXY_RESULT_ERROR;
                    //softsim_trace_hex((uint8_t*)order_info, sizeof(order_info)); 
                    free((void *)p_out_buffer); //bugfix
                    qapi_atfwd_send_resp(atcmd_name, "", QUEC_AT_RESULT_ERROR_V01);
		            return;
                }			  
                softsim_trace("%s", p_out_buffer); 
	        }
            else
            {
	            qapi_atfwd_send_resp(atcmd_name, "", QUEC_AT_RESULT_ERROR_V01);
	            return;
            }
        }
        else
        {
	        qapi_atfwd_send_resp(atcmd_name, "", QUEC_AT_RESULT_ERROR_V01);
	        return;
        }
	    qapi_atfwd_send_resp(atcmd_name, (char *)p_out_buffer, QUEC_AT_RESULT_OK_V01);

        if (p_out_buffer != NULL)
        {
            free((void *)p_out_buffer);
            p_out_buffer = NULL;
        }
    }
    return;
}

void links_softsim_init(void)
{
    qurt_thread_attr_t    Thread_Attribte;
    //qurt_thread_t  Thread_Handle;
    qurt_pipe_attr_t attr;
    int   Thread_Result;
    softsim_set_init_data_path((const unsigned short *)"/softsim/softsim.init.dat");
    //ret_val = qapi_FS_Mk_Dir(NVRAM_EF_SOFTSIM_GROUP, QAPI_FS_S_IRUSR_E|QAPI_FS_S_IWUSR_E|QAPI_FS_S_IXUSR_E);
    //QAPI_MSG_SPRINTF( MSG_SSID_LINUX_DATA , MSG_LEGACY_HIGH, "creating softsim directory result:%d", ret_val );
    //softsim_set_init_data_path(NVRAM_EF_SOFTSIM_INIT_LID);
    //soft_sim_event_ptr = (SoftsimEvent_st *)malloc(sizeof(SoftsimEvent_st));
    QAPI_MSG_SPRINTF(MSG_SSID_LINUX_DATA , MSG_LEGACY_HIGH, " create a new queue : SOFTSIM MAIN QUEUE");
    /* create a new queue : SOFTSIM MAIN QUEUE */
    qurt_pipe_attr_init(&attr);
    qurt_pipe_attr_set_elements(&attr, SOFTSIM_QUEUE_SIZE);
    qurt_pipe_attr_set_element_size(&attr,sizeof(SoftsimQueue_st));
    Thread_Result = qurt_pipe_create (&softsim_task_queue[SOFTSIM_MAIN_TASK_ID], &attr);
    if ( 0 != Thread_Result )
    {
        QAPI_MSG_SPRINTF( MSG_SSID_LINUX_DATA , MSG_LEGACY_HIGH, "creating pipe error:%d", Thread_Result );
	  //PSM_LOG_INFO("Error creating pipe");
        return;
    }
    QAPI_MSG_SPRINTF(MSG_SSID_LINUX_DATA , MSG_LEGACY_HIGH, " create a new task : SOFTSIM MAIN");
    /* create a new task : SOFTSIM MAIN */
    qurt_thread_attr_init(&Thread_Attribte);
    qurt_thread_attr_set_name(&Thread_Attribte, "Softsim Main Task Thread");
    qurt_thread_attr_set_priority(&Thread_Attribte, 148);
    qurt_thread_attr_set_stack_size(&Thread_Attribte, SOFTSIM_MAIN_STACK_SIZE);

    Thread_Result = qurt_thread_create(&softsim_main_thread_handle, &Thread_Attribte, softsim_task_main, NULL);

    if(Thread_Result != 0)
    {
        QAPI_MSG_SPRINTF( MSG_SSID_LINUX_DATA , MSG_LEGACY_HIGH, "%d", Thread_Result );
        //result = SOFTSIM_FALSE;
        return;
    }
    QAPI_MSG_SPRINTF(MSG_SSID_LINUX_DATA , MSG_LEGACY_HIGH, " create a new queue : SOFTSIM COS QUEUE");
    /* create a new queue : SOFTSIM COS QUEUE */
    qurt_pipe_attr_init(&attr);
    qurt_pipe_attr_set_elements(&attr, SOFTSIM_QUEUE_SIZE);
    qurt_pipe_attr_set_element_size(&attr,sizeof(SoftsimQueue_st));
    if ( 0 != qurt_pipe_create (&softsim_task_queue[SOFTSIM_COS_TASK_ID], &attr) )
    {
        //result = SOFTSIM_FALSE;
        return;
    }
    QAPI_MSG_SPRINTF(MSG_SSID_LINUX_DATA , MSG_LEGACY_HIGH, " create a new task : SOFTSIM COS");
    /* create a new task : SOFTSIM COS */
    qurt_thread_attr_init(&Thread_Attribte);
    qurt_thread_attr_set_name(&Thread_Attribte, "Softsim Cos Task Thread");
    qurt_thread_attr_set_priority(&Thread_Attribte, 148);
    qurt_thread_attr_set_stack_size(&Thread_Attribte, SOFTSIM_COS_STACK_SIZE);

    Thread_Result = qurt_thread_create(&softsim_cos_thread_handle, &Thread_Attribte, softsim_task_cos, NULL);

    if(Thread_Result != 0)
    {
        //result = SOFTSIM_FALSE;
        return;
    }

    soft_sim_data = softsim_create_sem((unsigned char*)"SOFT_SIM_DATA_DCL", 0);
	soft_sim_rst  = softsim_create_sem((unsigned char*)"SOFT_SIM_RST_DCL", 0);

    #if 1
    //AT CONFIG
    if (qapi_atfwd_reg("+PSOFTSIM", softsim_atfwd_cmd_handler_cb) != QAPI_OK)
    {
        //result = SOFTSIM_FALSE;
        return;  
    }
    #endif
}

