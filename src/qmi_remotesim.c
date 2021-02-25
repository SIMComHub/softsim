#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
//#include "softsim_log.h"
//#include "softsim.h"
//#include "thread_ipc.h"
//#include "sync_time.h"
//#include "interface.h"
//#include "modem_interface.h"

#define QMI_UIMREMOTE

#ifdef QMI_UIMREMOTE

//#ifdef ANDROID_AOSP
#include "qapi_diag.h"
#include "qapi_timer.h"
#include "qapi_types.h"
#include "qmi_cci_target.h"
#include "qmi_client.h"
#include "qmi_idl_lib.h"
#include "qmi_cci_common.h"
#include "qmi_idl_lib_internal.h"
#include "common_v01.h"
//#include "qmi.h"
#include "device_management_service_v01.h"
#include "user_identity_module_remote_v01.h"
/*
#else
#include "qmi-framework/qmi_cci_target.h"
#include "qmi-framework/qmi_client.h"
#include "qmi-framework/qmi_idl_lib.h"
#include "qmi-framework/qmi_cci_common.h"
#include "qmi-framework/qmi_idl_lib_internal.h"
#include "qmi-framework/common_v01.h"
#include "qmi/qmi.h"
#include "qmi/device_management_service_v01.h"
#include "qmi/user_identity_module_remote_v01.h"
#endif
*/
#include "softsim_porting.h"

extern void softsim_take_soft_sim_data_sem(void);
extern void softsim_give_soft_sim_data_sem(void);
extern void softsim_take_soft_sim_rst_sem(void);
extern void softsim_give_soft_sim_rst_sem(void);
extern void softsim_rst_send(void);
extern void softsim_apdu_send(SoftsimAPDU_Tx_Data_st * tx_data);
extern SIMCARDRESET_CNF *softsim_get_rst_cnf_data(void);
extern SoftsimAPDU_Rx_Data_st *softsim_get_apdu_rx_data(void);

extern int start_cos_thread(unsigned char* atr, int len);
extern void stop_cos_thread(void);
extern void stop_cos_thread_nosave(void);

typedef enum
{
    UIM_REMOTE_CARD_DISCONNECTED,
    UIM_REMOTE_CARD_CONNECTED,
    UIM_REMOTE_CARD_POWERED
} uim_remote_card_state;

typedef struct
{
    qmi_client_type *clnt;
    uim_remote_card_state state;
    uim_remote_slot_type_enum_v01 slot;
    qmi_txn_handle txn;
} uim_remote_card;

typedef union
{
    uim_remote_reset_resp_msg_v01 reset;
    uim_remote_event_resp_msg_v01 event;
    uim_remote_apdu_resp_msg_v01 apdu;
} uim_remote_resp_msg;

static uim_remote_card card;
static qmi_idl_service_object_type uim_remote_service_object;
static qmi_client_type clnt, notifier;
static int card_slot = 1;
static uim_remote_resp_msg async_resp;

//static int E_flag = 0;

#define MAX_DIAG_LOG_MSG_SIZE       512

void LOGI(char *fmt, ...)
{
  char buf_ptr[MAX_DIAG_LOG_MSG_SIZE];
  va_list ap;
  
  va_start(ap, fmt);
  vsnprintf( buf_ptr, (size_t)MAX_DIAG_LOG_MSG_SIZE, fmt, ap );
  va_end( ap );

  QAPI_MSG_SPRINTF( MSG_SSID_LINUX_DATA, MSG_LEGACY_HIGH, "softsim:%s", buf_ptr);
}

void uim_remote_rx_cb(
    qmi_client_type user_handle,
    unsigned int msg_id,
    void *buf,
    unsigned int len,
    void *resp_cb_data,
    qmi_client_error_type transp_err)
{
    //LOGI("uim_remote_rx_cb receive: msg_id=0x%x result %d error %d",(unsigned int)msg_id, ((qmi_response_type_v01*)buf)->result, ((qmi_response_type_v01*)buf)->error);
}

int uim_remote_reset(
    uim_remote_card *card)
{
    int rc;
    //uim_remote_reset_req_msg_v01 req;

    rc = qmi_client_send_msg_async(*card->clnt, QMI_UIM_REMOTE_RESET_REQ_V01, NULL, 0, &async_resp, sizeof(async_resp), uim_remote_rx_cb, card, &card->txn);
    LOGI("uim_remote_reset return %d", rc);
    return rc;
}

int uim_remote_event(
    uim_remote_card *card,
    uim_remote_event_type_enum_v01 event,
    uim_remote_slot_type_enum_v01 slot,
    uint8_t *atr, uint32_t atrlen,
    uint8_t wakeup_support,
    uim_remote_card_error_type_enum_v01 error)
{
    int rc;
    uim_remote_event_req_msg_v01 req;

    memset(&req, 0, sizeof(uim_remote_event_req_msg_v01));
    req.event_info.event = event;
    req.event_info.slot = slot;
    if (atr && atrlen)
    {
        if (atrlen > QMI_UIM_REMOTE_MAX_ATR_LEN_V01)
        {
            LOGI("uim_remote_event ATR len > QMI_UIM_REMOTE_MAX_ATR_LEN_V01");
            atrlen = QMI_UIM_REMOTE_MAX_ATR_LEN_V01;
        }
        req.atr_valid = 1;
        req.atr_len = atrlen;
        memcpy(req.atr, atr, atrlen);
    }
    if (wakeup_support)
    {
        req.wakeup_support_valid = 1;
        req.wakeup_support = wakeup_support;
    }
    if (error >= 0)
    {
        req.error_cause_valid = 1;
        req.error_cause = error;
    }
    rc = qmi_client_send_msg_async(*card->clnt, QMI_UIM_REMOTE_EVENT_REQ_V01, &req, sizeof(req), &async_resp, sizeof(async_resp), uim_remote_rx_cb, card, &card->txn);
    LOGI("uim_remote_event %d return %d", event, rc);
    return rc;
}

int uim_remote_apdu_resp(
    uim_remote_card *card,
    qmi_result_type_v01 status,
    uim_remote_slot_type_enum_v01 slot,
    uint32_t apdu_id,
    uint32_t apdu_size,
    uint8_t *seg_data, uint32_t offset_in_apdu, uint32_t seg_len)
{
    int rc;
    uim_remote_apdu_req_msg_v01 *req = NULL;
	req=malloc(sizeof(uim_remote_apdu_req_msg_v01));
	if(req == NULL)
	{
		LOGI("can't allocate memory space.");
		return QMI_RESULT_FAILURE_V01; 
	}
    memset(req, 0x00, sizeof(uim_remote_apdu_req_msg_v01));
    req->apdu_status = status;
    req->slot = slot;
    req->apdu_id = apdu_id;
    req->response_apdu_info_valid = 0;
    req->response_apdu_segment_valid = 0;
    if (apdu_size && seg_data && seg_len && (apdu_size >= offset_in_apdu + seg_len) && (seg_len <= QMI_UIM_REMOTE_MAX_RESPONSE_APDU_SEGMENT_LEN_V01))
    {
        req->response_apdu_info_valid = 1;
        req->response_apdu_info.total_response_apdu_size = apdu_size;
        req->response_apdu_info.response_apdu_segment_offset = offset_in_apdu;

        req->response_apdu_segment_valid = 1;
        req->response_apdu_segment_len = seg_len;
        memcpy(req->response_apdu_segment, seg_data, seg_len);
    }
    else if (apdu_size || seg_data)
    {
        LOGI("uim_remote_apdu_resp wrong arg!");
        return QMI_RESULT_FAILURE_V01;
    }
    rc = qmi_client_send_msg_async(*card->clnt, QMI_UIM_REMOTE_APDU_REQ_V01, req, sizeof(uim_remote_apdu_req_msg_v01), &async_resp, sizeof(async_resp), uim_remote_rx_cb, card, &card->txn);
    LOGI("uim_remote_apdu_resp return %d", rc);
	free(req);
    return rc;
}

int uim_remote_connect_card(uim_remote_card *card)
{
    if (card->state != UIM_REMOTE_CARD_DISCONNECTED)
        return QMI_RESULT_SUCCESS_V01;
    return uim_remote_event(card, UIM_REMOTE_CONNECTION_AVAILABLE_V01, card->slot, NULL, 0, 0, UIM_REMOTE_CARD_ERROR_TYPE_ENUM_MIN_ENUM_VAL_V01);
}

int uim_remote_disconnect_card(uim_remote_card *card)
{
    if (card->state == UIM_REMOTE_CARD_DISCONNECTED)
        return QMI_RESULT_SUCCESS_V01;
    return uim_remote_event(card, UIM_REMOTE_CONNECTION_UNAVAILABLE_V01, card->slot, NULL, 0, 0, UIM_REMOTE_CARD_ERROR_TYPE_ENUM_MIN_ENUM_VAL_V01);
}

int uim_remote_send_atr(uim_remote_card *card, unsigned char *atr, int atrlen)
{
    if (card->state != UIM_REMOTE_CARD_POWERED || !atr || !atrlen)
        return QMI_RESULT_FAILURE_V01;
    return uim_remote_event(card, UIM_REMOTE_CARD_RESET_V01, card->slot, atr, atrlen, 0, UIM_REMOTE_CARD_ERROR_TYPE_ENUM_MIN_ENUM_VAL_V01);
}

int uim_remote_reset_send_atr(uim_remote_card *card)
{
    unsigned char atr[64];
    int atrlen;
    stop_cos_thread_nosave();
    atrlen = start_cos_thread(atr, sizeof(atr));
    return uim_remote_send_atr(card, atr, atrlen);
}

int uim_remote_replug_card(uim_remote_card *card)
{
    unsigned char atr[64];
    int atrlen;
    LOGI("need reset card.");
    uim_remote_event(card, UIM_REMOTE_CARD_REMOVED_V01, card->slot, NULL, 0, 0, UIM_REMOTE_CARD_ERROR_TYPE_ENUM_MIN_ENUM_VAL_V01);
    LOGI("do hotswap card, sleep 3s ...");
    //sleep(3);
    qapi_Timer_Sleep(3,QAPI_TIMER_UNIT_SEC,true);
    atrlen = start_cos_thread(atr, sizeof(atr));
    return uim_remote_event(card, UIM_REMOTE_CARD_INSERTED_V01, card->slot, atr, atrlen, 0, UIM_REMOTE_CARD_ERROR_TYPE_ENUM_MIN_ENUM_VAL_V01);
}

int uim_remote_send_resp(uim_remote_card *card, int apdu_id, unsigned char *resp, int resplen)
{
    if (card->state != UIM_REMOTE_CARD_POWERED || !resp || !resplen)
        return QMI_RESULT_FAILURE_V01;
    return uim_remote_apdu_resp(card, QMI_RESULT_SUCCESS_V01, card->slot, apdu_id, resplen, resp, 0, resplen);
}
static unsigned char atr[18] = {0x3B,0x7D,0x96,0x00,0x00,0x57,0x44,0x4B,0x4F,0x46,0x86,0x93,0x09,0x00,0x00,0x00,0x00,0x00};
void uim_remote_ind_cb(
    qmi_client_type user_handle,
    unsigned int msg_id,
    void *ind_buf,
    unsigned int ind_buf_len,
    void *ind_cb_data)
{
    uim_remote_card *card = (uim_remote_card *)ind_cb_data;

    static int atrlen = 0;
    switch (msg_id)
    {
    case QMI_UIM_REMOTE_APDU_IND_V01:
    {
        uim_remote_apdu_ind_msg_v01 apdu_msg;
        qmi_client_error_type rc = qmi_client_message_decode(user_handle, QMI_IDL_INDICATION, msg_id, ind_buf, ind_buf_len, &apdu_msg, sizeof(apdu_msg));
        LOGI("uim_remote_ind_cb: msg_id=0x%x buf_len=%d slot=%d card(slot=%d state=%d)",(unsigned int)msg_id, ind_buf_len, apdu_msg.slot,card->slot, card->state);
        if (rc == QMI_NO_ERR && card->slot == apdu_msg.slot && (card->state == UIM_REMOTE_CARD_CONNECTED || card->state == UIM_REMOTE_CARD_POWERED))
        {
            unsigned char resp[QMI_UIM_REMOTE_MAX_RESPONSE_APDU_SEGMENT_LEN_V01];
            int resplen;
            //resplen = sizeof(resp);             
            SoftsimAPDU_Tx_Data_st tx_data = {0};
            SoftsimAPDU_Rx_Data_st *rx_data = NULL;
                      
            softsim_trace_hex((uint8_t*)apdu_msg.command_apdu, apdu_msg.command_apdu_len);    
            memcpy(tx_data.txData, apdu_msg.command_apdu, apdu_msg.command_apdu_len);
            tx_data.txSize = apdu_msg.command_apdu_len;
            softsim_apdu_send(&tx_data);
            softsim_take_soft_sim_data_sem();
            rx_data = (SoftsimAPDU_Rx_Data_st *)softsim_get_apdu_rx_data();
            memcpy(resp, rx_data->rxData, rx_data->rxSize);
            resplen = rx_data->rxSize;
            resp[resplen++] = (rx_data->statusWord&0xFF00)>>8;
            resp[resplen++] = rx_data->statusWord&0x00FF;
			
            softsim_trace_hex((uint8_t*)resp, resplen); 
            uim_remote_send_resp(card, apdu_msg.apdu_id, resp, resplen);
        }
    }
    break;
    case QMI_UIM_REMOTE_CONNECT_IND_V01:
    {
        uim_remote_connect_ind_msg_v01 msg;
        qmi_client_error_type rc = qmi_client_message_decode(user_handle, QMI_IDL_INDICATION, msg_id, ind_buf, ind_buf_len, &msg, sizeof(msg));
        LOGI("uim_remote_ind_cb: msg_id=0x%x buf_len=%d slot=%d card(slot=%d state=%d)",
             (unsigned int)msg_id, ind_buf_len, msg.slot,
             card->slot, card->state);
        if (rc == QMI_NO_ERR && card->slot == msg.slot && card->state == UIM_REMOTE_CARD_DISCONNECTED)
        {
            softsim_rst_send();
            softsim_take_soft_sim_rst_sem();
            atrlen = sizeof(atr);           
            softsim_trace_hex((uint8_t*)atr, atrlen);  
            card->state = UIM_REMOTE_CARD_CONNECTED;
            LOGI("uim_remote_ind_cb: card is connected.");
        } 
        uim_remote_send_atr(card, atr, atrlen);
    }
    break;
    case QMI_UIM_REMOTE_DISCONNECT_IND_V01:
    {
        uim_remote_disconnect_ind_msg_v01 msg;
        qmi_client_error_type rc = qmi_client_message_decode(user_handle, QMI_IDL_INDICATION, msg_id, ind_buf, ind_buf_len, &msg, sizeof(msg));
        LOGI("uim_remote_ind_cb: msg_id=0x%x buf_len=%d slot=%d card(slot=%d state=%d)",
             (unsigned int)msg_id, ind_buf_len, msg.slot,
             card->slot, card->state);
        if (rc == QMI_NO_ERR && card->slot == msg.slot && card->state == UIM_REMOTE_CARD_CONNECTED)
        {
            card->state = UIM_REMOTE_CARD_DISCONNECTED;
            LOGI("uim_remote_ind_cb: card is disconnected.");
        }
    }
    break;
    case QMI_UIM_REMOTE_CARD_POWER_UP_IND_V01:
    {
        uim_remote_card_power_up_ind_msg_v01 msg;
        qmi_client_error_type rc = qmi_client_message_decode(user_handle, QMI_IDL_INDICATION, msg_id, ind_buf, ind_buf_len, &msg, sizeof(msg));
        LOGI("uim_remote_ind_cb: msg_id=0x%x buf_len=%d slot=%d card(slot=%d state=%d)",
             (unsigned int)msg_id, ind_buf_len, msg.slot,
             card->slot, card->state);
        if (rc == QMI_NO_ERR && card->slot == msg.slot && card->state >= UIM_REMOTE_CARD_CONNECTED)
        {
            if (card->state == UIM_REMOTE_CARD_CONNECTED)
            {
                //atrlen = start_cos_thread(atr, sizeof(atr));
                 
                //SIMCARDRESET_CNF *simcard_rst_cnf;
                softsim_rst_send();
                softsim_take_soft_sim_rst_sem();
                //simcard_rst_cnf = softsim_get_rst_cnf_data();
                //atrlen = sizeof(simcard_rst_cnf->a_AnswerToReset);
                //memcpy(atr, simcard_rst_cnf->a_AnswerToReset, atrlen);
                atrlen = sizeof(atr);           
                softsim_trace_hex((uint8_t*)atr, atrlen);  
                card->state = UIM_REMOTE_CARD_POWERED;
                LOGI("uim_remote_ind_cb: card is powered up.");
            }
             
            uim_remote_send_atr(card, atr, atrlen);
        }
    }
    break;
    case QMI_UIM_REMOTE_CARD_POWER_DOWN_IND_V01:
    {
        uim_remote_card_power_down_ind_msg_v01 msg;
        qmi_client_error_type rc = qmi_client_message_decode(user_handle, QMI_IDL_INDICATION, msg_id, ind_buf, ind_buf_len, &msg, sizeof(msg));
        LOGI("uim_remote_ind_cb: msg_id=0x%x buf_len=%d slot=%d card(slot=%d state=%d)",
             (unsigned int)msg_id, ind_buf_len, msg.slot,
             card->slot, card->state);
        if (rc == QMI_NO_ERR && card->slot == msg.slot && card->state == UIM_REMOTE_CARD_POWERED)
        {
            //stop_cos_thread_nosave();
            card->state = UIM_REMOTE_CARD_CONNECTED;
            LOGI("uim_remote_ind_cb: card is powered down.");
        }
    }
    break;
    case QMI_UIM_REMOTE_CARD_RESET_IND_V01:
    {
        uim_remote_card_reset_ind_msg_v01 msg;
        qmi_client_error_type rc = qmi_client_message_decode(user_handle, QMI_IDL_INDICATION, msg_id, ind_buf, ind_buf_len, &msg, sizeof(msg));
        LOGI("uim_remote_ind_cb: msg_id=0x%x buf_len=%d slot=%d card(slot=%d state=%d)",
             (unsigned int)msg_id, ind_buf_len, msg.slot,
             card->slot, card->state);
        if (rc == QMI_NO_ERR && card->slot == msg.slot && card->state >= UIM_REMOTE_CARD_CONNECTED)
        {
            if (card->state == UIM_REMOTE_CARD_CONNECTED)
            {
                //atrlen = start_cos_thread(atr, sizeof(atr));
                //SIMCARDRESET_CNF *simcard_rst_cnf;
                softsim_rst_send();
                softsim_take_soft_sim_rst_sem();
                //simcard_rst_cnf = softsim_get_rst_cnf_data();
                //atrlen = sizeof(simcard_rst_cnf->a_AnswerToReset);
                //memcpy(atr, simcard_rst_cnf->a_AnswerToReset, atrlen); 
                atrlen = sizeof(atr);
                card->state = UIM_REMOTE_CARD_POWERED;
                LOGI("uim_remote_ind_cb: reset card.");
            }
            uim_remote_send_atr(card, atr, atrlen);
        }
    }
    break;
    }
}

int modem_check_softsim_slot(int slot)
{
    if (slot < UIM_REMOTE_SLOT_1_V01 || slot > UIM_REMOTE_SLOT_3_V01)
    {
        return -1;
    }
    return 0;
}

int modem_set_softsim_slot(int slot)
{
#if 0
    btsim_dms_qmi_init();
    if (slot != qmi_get_slot())
    {
        qmi_set_slot(slot);
        system("reboot");
        return 0;
    }
#endif
    card_slot = slot;
    return 0;
}

int modem_get_softsim_slot(void)
{
    return card_slot;
}

int init_modem_connection(void)
{
    uim_remote_service_object = uim_remote_get_service_object_v01();
    if (!uim_remote_service_object)
    {
        LOGI("uim_remote_get_service_object failed, verify user_identity_module_remote_v01.h and .c match.");
    }
    return 0;
}

int connect_modem(void)
{
    uint32_t num_services, num_entries = 0;
    int rc, service_connect;
    qmi_cci_os_signal_type os_params;
    qmi_service_info info[2];

    //os_params.ext_signal = (qurt_signal_t *)malloc(sizeof(qurt_signal_t));
    os_params.ext_signal = 0;
    os_params.sig = 0;
    os_params.timer_sig = 0; 
    rc = qmi_client_notifier_init(uim_remote_service_object, &os_params, &notifier);
    LOGI("qmi_client_notifier_init returned %d", rc);
    /* Check if the service is up, if not wait on a signal */
    while (1)
    {
        rc = qmi_client_get_service_list(uim_remote_service_object, NULL, NULL, &num_services);
        LOGI("qmi_client_get_service_list() returned %d num_services = %d", rc, num_services);
        if (rc == QMI_NO_ERR)
            break;
        /* wait for server to come up */
        QMI_CCI_OS_SIGNAL_WAIT(&os_params, 0);
    };
    rc = qmi_client_release(notifier);
    LOGI("qmi_client_release of notifier returned %d", rc);

    num_entries = num_services;
    /* The server has come up, store the information in info variable */
    rc = qmi_client_get_service_list(uim_remote_service_object, info, &num_entries, &num_services);
    LOGI("qmi_client_get_service_list() returned %d num_entries = %d num_services = %d", rc, num_entries, num_services);
    service_connect = 0;

    rc = qmi_client_init(&info[service_connect], uim_remote_service_object, uim_remote_ind_cb, &card, NULL, &clnt);

    LOGI("qmi_client_init returned %d", rc);
    if (rc != QMI_NO_ERR)
    {
        //system_report_event(EVENT_RUN_ERROR, EVENT_SUB_RUNERROR_MDM_NOCONNECT, NULL);
    }

    card.clnt = &clnt;
    card.state = UIM_REMOTE_CARD_DISCONNECTED;
    //card.slot = uim_remote_slot_type_enum_v01[card_slot];
    if(card_slot == 1)
    {
        card.slot = UIM_REMOTE_SLOT_1_V01;
    }
    else if(card_slot == 2)
    {
        card.slot = UIM_REMOTE_SLOT_2_V01;
    }
    else if(card_slot == 3)
    {
        card.slot = UIM_REMOTE_SLOT_3_V01;
    }
    else
    {
        card.slot = UIM_REMOTE_SLOT_NOT_APPLICABLE_V01;
    }
    

    rc = uim_remote_connect_card(&card);
    rc = uim_remote_event(&card, UIM_REMOTE_CARD_INSERTED_V01, card.slot, atr, sizeof(atr), 0, UIM_REMOTE_CARD_ERROR_TYPE_ENUM_MIN_ENUM_VAL_V01);
    return rc;
}

int disconnect_modem(void)
{
    int rc;
    if (card.state == UIM_REMOTE_CARD_POWERED)
    {
        stop_cos_thread_nosave();
        card.state = UIM_REMOTE_CARD_CONNECTED;
    }

    rc = uim_remote_event(&card, UIM_REMOTE_CARD_REMOVED_V01, card.slot, NULL, 0, 0, UIM_REMOTE_CARD_ERROR_TYPE_ENUM_MIN_ENUM_VAL_V01);
    rc = uim_remote_disconnect_card(&card);

    rc = qmi_client_release(clnt);
    LOGI("qmi_client_release of clnt returned %d", rc);

    return rc;
}

int deinit_modem_connection(void)
{
    return 0;
}

int modem_is_cos_connected(void)
{
    return card.state == UIM_REMOTE_CARD_POWERED;
}

int modem_replug_card(void)
{
    return uim_remote_replug_card(&card);
}

#endif
