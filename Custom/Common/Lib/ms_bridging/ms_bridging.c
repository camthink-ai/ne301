#include <string.h>
#include "ms_bridging.h"

// CRC16-CCITT calculation (polynomial 0x1021)
static uint16_t ms_bridging_crc16(uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    uint16_t i = 0;
    uint8_t j = 0;

    for (i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc = crc << 1;
            }
        }
    }
    return crc;
}

// Find empty slot in ack frame buffer
static int ms_bridging_find_empty_ack_slot(ms_bridging_handler_t *handler)
{
    int i = 0;
    
    for (; i < MS_BR_FRAME_BUF_NUM; i++) {
        if (handler->ack_frame[i].is_valid == 0) {
            return i;
        }
    }
    return MS_BR_ERR_NO_FOUND;
}

// Find empty slot in notify frame buffer
static int ms_bridging_find_empty_notify_slot(ms_bridging_handler_t *handler)
{
    int i = 0;

    for (; i < MS_BR_FRAME_BUF_NUM; i++) {
        if (handler->notify_frame[i].is_valid == 0) {
            return i;
        }
    }
    return MS_BR_ERR_NO_FOUND;
}

// Wait for ack
static int ms_bridging_wait_for_ack(ms_bridging_handler_t *handler, ms_bridging_frame_type_t frame_type, ms_bridging_frame_cmd_t frame_cmd, uint16_t frame_id, ms_bridging_frame_t *ack_frame)
{
    int i = 0;
    uint32_t start_tick = 0, now_tick = 0;

    start_tick = MS_BR_GET_TICK_MS();
    do {
    #if defined(MS_BR_SEM_CREATE) && defined(MS_BR_SEM_DELETE) && defined(MS_BR_SEM_WAIT) && defined(MS_BR_SEM_POST)
        MS_BR_SEM_WAIT(handler->ack_sem, MS_BR_WAIT_ACK_DELAY_MS);
    #else
        MS_BR_DELAY_MS(MS_BR_WAIT_ACK_DELAY_MS);
    #endif
        if (handler->is_ready == 0) return MS_BR_ERR_INVALID_STATE;
        for (i = 0; i < MS_BR_FRAME_BUF_NUM; i++) {
            if (handler->ack_frame[i].is_valid == 1) {
                if (handler->ack_frame[i].header.id == frame_id && handler->ack_frame[i].header.cmd == frame_cmd && handler->ack_frame[i].header.type == frame_type) {
                    memcpy(ack_frame, &handler->ack_frame[i], sizeof(ms_bridging_frame_t));
                    handler->ack_frame[i].data = NULL;
                    handler->ack_frame[i].is_valid = 0;
                    return MS_BR_OK;
                }
            }
        }
        now_tick = MS_BR_GET_TICK_MS();
    } while (MS_BR_TICK_DIFF_MS(start_tick, now_tick) < MS_BR_WAIT_ACK_TIMEOUT_MS);
    
    return MS_BR_ERR_TIMEOUT;
}

// Add ack frame
static int ms_bridging_add_ack_frame(ms_bridging_handler_t *handler, ms_bridging_frame_t *frame)
{
    int empty_slot = -1;

    empty_slot = ms_bridging_find_empty_ack_slot(handler);
    if (empty_slot < 0) return MS_BR_ERR_NO_MEM;

    memcpy(&handler->ack_frame[empty_slot], frame, sizeof(ms_bridging_frame_t));
    handler->ack_frame_received_tick[empty_slot] = MS_BR_GET_TICK_MS();
    frame->data = NULL;

#if defined(MS_BR_SEM_CREATE) && defined(MS_BR_SEM_DELETE) && defined(MS_BR_SEM_WAIT) && defined(MS_BR_SEM_POST)
    MS_BR_SEM_POST(handler->ack_sem);
#endif
    return MS_BR_OK;
}

// Add notify frame
static int ms_bridging_add_notify_frame(ms_bridging_handler_t *handler, ms_bridging_frame_t *frame)
{
    int empty_slot = -1;

    empty_slot = ms_bridging_find_empty_notify_slot(handler);
    if (empty_slot < 0) return MS_BR_ERR_NO_MEM;

    memcpy(&handler->notify_frame[empty_slot], frame, sizeof(ms_bridging_frame_t));
    frame->data = NULL;

#if defined(MS_BR_SEM_CREATE) && defined(MS_BR_SEM_DELETE) && defined(MS_BR_SEM_WAIT) && defined(MS_BR_SEM_POST)
    MS_BR_SEM_POST(handler->notify_sem);
#endif
    return MS_BR_OK;
}

// Check frame crc
static int ms_bridging_check_frame_crc(ms_bridging_frame_t *frame)
{
    uint16_t crc = 0;
    
    crc = ms_bridging_crc16((uint8_t *)&(frame->header), MS_BR_FRAME_HEADER_LEN - 2);
    if (crc != frame->header.crc) {
        return MS_BR_ERR_CRC_CHECK;
    }

    if (frame->header.len > 0 && frame->data != NULL) {
        crc = ms_bridging_crc16(frame->data, frame->header.len);
        if (crc != frame->data_crc) {
            return MS_BR_ERR_CRC_CHECK;
        }
    }

    return MS_BR_OK;
}

// Calculate frame crc
static int ms_bridging_calculate_frame_crc(ms_bridging_frame_t *frame)
{
    frame->header.crc = ms_bridging_crc16((uint8_t *)&(frame->header), MS_BR_FRAME_HEADER_LEN - 2);
    if (frame->header.len > 0 && frame->data != NULL) {
        frame->data_crc = ms_bridging_crc16(frame->data, frame->header.len);
    }
    return MS_BR_OK;
}

// Build frame
static int ms_bridging_build_frame(ms_bridging_handler_t *handler, ms_bridging_frame_t *frame, ms_bridging_frame_type_t type, ms_bridging_frame_cmd_t cmd, uint8_t *data, uint16_t len)
{
    frame->header.sof = MS_BR_FRAME_SOF;
    frame->header.id = handler->global_frame_id++;
    frame->header.len = len;
    frame->header.type = type;
    frame->header.cmd = cmd;
    frame->data = data;
    ms_bridging_calculate_frame_crc(frame);
    frame->is_valid = 1;
    return MS_BR_OK;
}

// Send frame
static int ms_bridging_send_frame(ms_bridging_handler_t *handler, ms_bridging_frame_t *frame)
{
    int ret = MS_BR_OK;
    uint8_t *buf = NULL;
    uint16_t len = 0;

    len = MS_BR_FRAME_HEADER_LEN;
    if (frame->header.len > 0 && frame->data != NULL) {
        len += (frame->header.len + 2);
    }
    buf = (uint8_t *)MS_BR_MALLOC(len);
    if (buf == NULL) return MS_BR_ERR_NO_MEM;
    memcpy(buf, (uint8_t *)&(frame->header), MS_BR_FRAME_HEADER_LEN);
    if (len > MS_BR_FRAME_HEADER_LEN) {
        memcpy(buf + MS_BR_FRAME_HEADER_LEN, (uint8_t *)frame->data, frame->header.len);
        memcpy(buf + MS_BR_FRAME_HEADER_LEN + frame->header.len, (uint8_t *)&(frame->data_crc), 2);
    }
    ret = handler->send_func(buf, len, MS_BR_FRAME_SEND_TIMEOUT_MS);
    MS_BR_FREE(buf);
    return ret;
}

ms_bridging_handler_t *ms_bridging_init(ms_bridging_send_func_t send_func, ms_bridging_notify_cb_t event_cb)
{
    ms_bridging_handler_t *handler = NULL;
    if (send_func == NULL || event_cb == NULL) return NULL;
    
    handler = (ms_bridging_handler_t *)MS_BR_MALLOC(sizeof(ms_bridging_handler_t));
    if (handler == NULL) return NULL;
    memset(handler, 0, sizeof(ms_bridging_handler_t));
#if defined(MS_BR_SEM_CREATE) && defined(MS_BR_SEM_DELETE) && defined(MS_BR_SEM_WAIT) && defined(MS_BR_SEM_POST)
    handler->ack_sem = MS_BR_SEM_CREATE();
    if (handler->ack_sem == NULL) {
        MS_BR_FREE(handler);
        return NULL;
    }
    handler->notify_sem = MS_BR_SEM_CREATE();
    if (handler->notify_sem == NULL) {
        MS_BR_SEM_DELETE(handler->ack_sem);
        MS_BR_FREE(handler);
        return NULL;
    }
#endif
    handler->send_func = send_func;
    handler->notify_cb = event_cb;
    handler->is_ready = 1;
    return handler;
}

void ms_bridging_deinit(ms_bridging_handler_t *handler)
{
    int i = 0;
    if (handler == NULL) return;

    handler->is_ready = 0;
#if defined(MS_BR_SEM_CREATE) && defined(MS_BR_SEM_DELETE) && defined(MS_BR_SEM_WAIT) && defined(MS_BR_SEM_POST)
    MS_BR_SEM_POST(handler->ack_sem);
    MS_BR_SEM_POST(handler->notify_sem);
#endif
    for (; i < MS_BR_FRAME_BUF_NUM; i++) {
        if (handler->ack_frame[i].data != NULL) {
            MS_BR_FREE(handler->ack_frame[i].data);
            handler->ack_frame[i].data = NULL;
            handler->ack_frame[i].is_valid = 0;
        }
        if (handler->notify_frame[i].data != NULL) {
            MS_BR_FREE(handler->notify_frame[i].data);
            handler->notify_frame[i].data = NULL;
            handler->notify_frame[i].is_valid = 0;
        }
    }
    if (handler->input_frame.data != NULL) {
        MS_BR_FREE(handler->input_frame.data);
        handler->input_frame.data = NULL;
        handler->input_frame.is_valid = 0;
    }
#if defined(MS_BR_SEM_CREATE) && defined(MS_BR_SEM_DELETE) && defined(MS_BR_SEM_WAIT) && defined(MS_BR_SEM_POST)
    if (handler->ack_sem != NULL) {
        MS_BR_SEM_DELETE(handler->ack_sem);
        handler->ack_sem = NULL;
    }
    if (handler->notify_sem != NULL) {
        MS_BR_SEM_DELETE(handler->notify_sem);
        handler->notify_sem = NULL;
    }
#endif
    MS_BR_FREE(handler);
}

static void ms_bridging_deal_input_frame(ms_bridging_handler_t *handler)
{
    int ret = MS_BR_OK;

    ret = ms_bridging_check_frame_crc(&handler->input_frame);
    if (ret == MS_BR_OK) {
        handler->input_frame.is_valid = 1;
        if (handler->input_frame.header.type == MS_BR_FRAME_TYPE_EVENT || handler->input_frame.header.type == MS_BR_FRAME_TYPE_REQUEST) {
            ret = ms_bridging_add_notify_frame(handler, &handler->input_frame);
        } else if (handler->input_frame.header.type == MS_BR_FRAME_TYPE_EVENT_ACK || handler->input_frame.header.type == MS_BR_FRAME_TYPE_RESPONSE) {
            ret = ms_bridging_add_ack_frame(handler, &handler->input_frame);
        } else {
            ret = MS_BR_ERR_UNKNOW;
        }
    }

    if (ret != MS_BR_OK && handler->input_frame.data != NULL) {
        MS_BR_FREE(handler->input_frame.data);
        handler->input_frame.data = NULL;
    }
    handler->input_frame_len = 0;
    handler->input_frame.is_valid = 0;
}

void ms_bridging_recv(ms_bridging_handler_t *handler, uint8_t *buf, uint16_t len)
{
    int i = 0;
    if (handler == NULL) return;
    
    for (; i < len; i++) {
        if (handler->is_ready == 0) return;
        if (handler->input_frame_len == 0 && buf[i] != MS_BR_FRAME_SOF) return;
        /* header.len is wire-controlled: a desynced/garbage stream can
         * claim 64KB - cap it the moment the header is complete so no
         * allocation or write bound trusts a wild value */
        if (handler->input_frame_len == MS_BR_FRAME_HEADER_LEN &&
            handler->input_frame.header.len > MS_BR_BUF_MAX_SIZE) {
            goto ms_bridging_recv_err;
        }
        if (handler->input_frame_len > MS_BR_BUF_MAX_SIZE) goto ms_bridging_recv_err;
        if (handler->input_frame_len < MS_BR_FRAME_HEADER_LEN) {
            ((uint8_t *)&(handler->input_frame.header))[handler->input_frame_len] = buf[i];
            handler->input_frame_len++;
        } else if (handler->input_frame_len < MS_BR_FRAME_HEADER_LEN + handler->input_frame.header.len) {
            if (handler->input_frame.data == NULL) {
                handler->input_frame.data = (uint8_t *)MS_BR_MALLOC(handler->input_frame.header.len);
                if (handler->input_frame.data == NULL) goto ms_bridging_recv_err;
            }
            ((uint8_t *)handler->input_frame.data)[handler->input_frame_len - MS_BR_FRAME_HEADER_LEN] = buf[i];
            handler->input_frame_len++;
        } else if (handler->input_frame_len < MS_BR_FRAME_ALL_LEN((&(handler->input_frame)))) {
            ((uint8_t *)&(handler->input_frame.data_crc))[handler->input_frame_len - MS_BR_FRAME_HEADER_LEN - handler->input_frame.header.len] = buf[i];
            handler->input_frame_len++;
        }

        if (handler->input_frame_len == MS_BR_FRAME_ALL_LEN((&(handler->input_frame)))) {
            ms_bridging_deal_input_frame(handler);
        }
    }

    return;
ms_bridging_recv_err:
    if (handler->input_frame.data) {
        MS_BR_FREE(handler->input_frame.data); 
        handler->input_frame.data = NULL;
    }
    handler->input_frame_len = 0;
    handler->input_frame.is_valid = 0;
}

void ms_bridging_polling(ms_bridging_handler_t *handler)
{
    int i = 0;
    uint32_t now_tick = 0;
    if (handler == NULL) goto ms_bridging_polling_end;

    for (; i < MS_BR_FRAME_BUF_NUM; i++) {
        if (handler->is_ready == 0) goto ms_bridging_polling_end;
        if (handler->notify_frame[i].is_valid == 1) {
            handler->notify_cb(handler, &handler->notify_frame[i]);
            if (handler->notify_frame[i].data != NULL) {
                MS_BR_FREE(handler->notify_frame[i].data);
                handler->notify_frame[i].data = NULL;
            }
            handler->notify_frame[i].is_valid = 0;
        }
        if (handler->is_ready == 0) goto ms_bridging_polling_end;
        if (handler->ack_frame[i].is_valid == 1) {
            now_tick = MS_BR_GET_TICK_MS();
            if (MS_BR_TICK_DIFF_MS(handler->ack_frame_received_tick[i], now_tick) >= MS_BR_WAIT_ACK_TIMEOUT_MS) {
                MS_BR_LOGD("Ack frame not received, id: %d cmd: %d type: %d", handler->ack_frame[i].header.id, handler->ack_frame[i].header.cmd, handler->ack_frame[i].header.type);
                if (handler->ack_frame[i].data != NULL) {
                    MS_BR_FREE(handler->ack_frame[i].data);
                    handler->ack_frame[i].data = NULL;
                }
                handler->ack_frame[i].is_valid = 0; 
            }
        }
    }

ms_bridging_polling_end:
#if defined(MS_BR_SEM_CREATE) && defined(MS_BR_SEM_DELETE) && defined(MS_BR_SEM_WAIT) && defined(MS_BR_SEM_POST)
    if (handler->is_ready) MS_BR_SEM_WAIT(handler->notify_sem, MS_BR_WAIT_ACK_DELAY_MS);
    else MS_BR_DELAY_MS(MS_BR_WAIT_ACK_DELAY_MS);
#else
    MS_BR_DELAY_MS(MS_BR_WAIT_ACK_DELAY_MS);
#endif
}

int ms_bridging_request(ms_bridging_handler_t *handler, ms_bridging_frame_cmd_t cmd, void *data, uint16_t len, void **data_out, uint16_t *len_out)
{
    int ret = MS_BR_OK, retry_times = 0;
    ms_bridging_frame_t frame = {0};
    if (handler == NULL) return MS_BR_ERR_INVALID_ARG;

    ret = ms_bridging_build_frame(handler, &frame, MS_BR_FRAME_TYPE_REQUEST, cmd, (uint8_t *)data, len);
    if (ret != MS_BR_OK) return ret;

    do {
        if (handler->is_ready == 0) return MS_BR_ERR_INVALID_STATE;

        ret = ms_bridging_send_frame(handler, &frame);
        if (ret != MS_BR_OK) return ret;

        ret = ms_bridging_wait_for_ack(handler, MS_BR_FRAME_TYPE_RESPONSE, cmd, frame.header.id, &frame);
        if (ret == MS_BR_OK && frame.header.len > 0 && frame.data != NULL && data_out != NULL && len_out != NULL) {
            *data_out = MS_BR_MALLOC(frame.header.len);
            if (*data_out == NULL) {
                MS_BR_FREE(frame.data);
                return MS_BR_ERR_NO_MEM;
            }
            memcpy(*data_out, frame.data, frame.header.len);
            *len_out = frame.header.len;
            MS_BR_FREE(frame.data);
        } else if (ret != MS_BR_OK) {
            MS_BR_LOGE("ms_bridging_request failed: %d", ret);
        }
    } while (retry_times++ < MS_BR_RETRY_TIMES && ret != MS_BR_OK);
    
    return ret;
}

int ms_bridging_response(ms_bridging_handler_t *handler, ms_bridging_frame_t *req_frame, void *data, uint16_t len)
{
    ms_bridging_frame_t frame = {0};
    if (handler == NULL || req_frame == NULL) return MS_BR_ERR_INVALID_ARG;
    
    frame.header.sof = MS_BR_FRAME_SOF;
    frame.header.id = req_frame->header.id;
    frame.header.len = len;
    frame.header.type = MS_BR_FRAME_TYPE_RESPONSE;
    frame.header.cmd = req_frame->header.cmd;
    frame.data = data;
    ms_bridging_calculate_frame_crc(&frame);
    
    return ms_bridging_send_frame(handler, &frame);
}

int ms_bridging_send_event(ms_bridging_handler_t *handler, ms_bridging_frame_cmd_t cmd, void *data, uint16_t len)
{
    int ret = MS_BR_OK, retry_times = 0;
    ms_bridging_frame_t frame = {0};
    if (handler == NULL) return MS_BR_ERR_INVALID_ARG;

    ret = ms_bridging_build_frame(handler, &frame, MS_BR_FRAME_TYPE_EVENT, cmd, (uint8_t *)data, len);
    if (ret != MS_BR_OK) return ret;

    do {
        if (handler->is_ready == 0) return MS_BR_ERR_INVALID_STATE;

        ret = ms_bridging_send_frame(handler, &frame);
        if (ret != MS_BR_OK) return ret;

        ret = ms_bridging_wait_for_ack(handler, MS_BR_FRAME_TYPE_EVENT_ACK, cmd, frame.header.id, &frame);
    } while (retry_times++ < MS_BR_RETRY_TIMES && ret != MS_BR_OK);
    
    return ret;
}

int ms_bridging_event_ack(ms_bridging_handler_t *handler, ms_bridging_frame_t *event_frame)
{
    ms_bridging_frame_t frame = {0};
    if (handler == NULL || event_frame == NULL) return MS_BR_ERR_INVALID_ARG;
    
    frame.header.sof = MS_BR_FRAME_SOF;
    frame.header.id = event_frame->header.id;
    frame.header.len = 0;
    frame.header.type = MS_BR_FRAME_TYPE_EVENT_ACK;
    frame.header.cmd = event_frame->header.cmd;
    ms_bridging_calculate_frame_crc(&frame);
    
    return ms_bridging_send_frame(handler, &frame);
}

// Convenience functions for common operations
int ms_bridging_request_keep_alive(ms_bridging_handler_t *handler)
{
    return ms_bridging_request(handler, MS_BR_FRAME_CMD_KEEPLIVE, NULL, 0, NULL, NULL);
}

int ms_bridging_request_get_time(ms_bridging_handler_t *handler, ms_bridging_time_t *time)
{
    void *data_out = NULL;
    uint16_t len_out = 0;
    int ret = ms_bridging_request(handler, MS_BR_FRAME_CMD_GET_TIME, NULL, 0, &data_out, &len_out);
    
    if (ret == MS_BR_OK && data_out != NULL && len_out == sizeof(ms_bridging_time_t)) {
        memcpy(time, data_out, sizeof(ms_bridging_time_t));
        MS_BR_FREE(data_out);
    } else if (data_out != NULL) {
        MS_BR_FREE(data_out);
    }
    
    return ret;
}

int ms_bridging_request_set_time(ms_bridging_handler_t *handler, ms_bridging_time_t *time)
{
    return ms_bridging_request(handler, MS_BR_FRAME_CMD_SET_TIME, time, sizeof(ms_bridging_time_t), NULL, NULL);
}

int ms_bridging_request_power_control(ms_bridging_handler_t *handler, ms_bridging_power_ctrl_t *power_ctrl)
{
    return ms_bridging_request(handler, MS_BR_FRAME_CMD_PWR_CTRL, power_ctrl, sizeof(ms_bridging_power_ctrl_t), NULL, NULL);
}

int ms_bridging_request_power_status(ms_bridging_handler_t *handler, uint32_t *switch_bits)
{
    void *data_out = NULL;
    uint16_t len_out = 0;
    int ret = ms_bridging_request(handler, MS_BR_FRAME_CMD_PWR_STATUS, NULL, 0, &data_out, &len_out);
    
    if (ret == MS_BR_OK && data_out != NULL && len_out == sizeof(uint32_t)) {
        memcpy(switch_bits, data_out, sizeof(uint32_t));
        MS_BR_FREE(data_out);
    } else if (data_out != NULL) {
        MS_BR_FREE(data_out);
    }
    
    return ret;
}

int ms_bridging_request_wakeup_flag(ms_bridging_handler_t *handler, uint32_t *wakeup_flag)
{
    void *data_out = NULL;
    uint16_t len_out = 0;
    int ret = ms_bridging_request(handler, MS_BR_FRAME_CMD_WKUP_FLAG, NULL, 0, &data_out, &len_out);
    
    if (ret == MS_BR_OK && data_out != NULL && len_out == sizeof(uint32_t)) {
        memcpy(wakeup_flag, data_out, sizeof(uint32_t));
        MS_BR_FREE(data_out);
    } else if (data_out != NULL) {
        MS_BR_FREE(data_out);
    }
    
    return ret;
}

int ms_bridging_request_clear_flag(ms_bridging_handler_t *handler)
{
    return ms_bridging_request(handler, MS_BR_FRAME_CMD_CLEAR_FLAG, NULL, 0, NULL, NULL);
}

int ms_bridging_request_reset_n6(ms_bridging_handler_t *handler)
{
    return ms_bridging_request(handler, MS_BR_FRAME_CMD_RST_N6, NULL, 0, NULL, NULL);
}

int ms_bridging_request_key_value(ms_bridging_handler_t *handler, uint32_t *key_value)
{
    void *data_out = NULL;
    uint16_t len_out = 0;
    int ret = ms_bridging_request(handler, MS_BR_FRAME_CMD_KEY_VALUE, NULL, 0, &data_out, &len_out);
    
    if (ret == MS_BR_OK && data_out != NULL && len_out == sizeof(uint32_t)) {
        memcpy(key_value, data_out, sizeof(uint32_t));
        MS_BR_FREE(data_out);
    } else if (data_out != NULL) {
        MS_BR_FREE(data_out);
    }
    
    return ret;
}

int ms_bridging_event_key_value(ms_bridging_handler_t *handler, uint32_t key_value)
{
    return ms_bridging_send_event(handler, MS_BR_FRAME_CMD_KEY_VALUE, &key_value, sizeof(uint32_t));
}

int ms_bridging_request_usb_vin_value(ms_bridging_handler_t *handler, uint32_t *usb_vin_value)
{
    void *data_out = NULL;
    uint16_t len_out = 0;
    int ret = ms_bridging_request(handler, MS_BR_FRAME_CMD_USB_VIN_VALUE, NULL, 0, &data_out, &len_out);
    
    if (ret == MS_BR_OK && data_out != NULL && len_out == sizeof(uint32_t)) {
        memcpy(usb_vin_value, data_out, sizeof(uint32_t));
        MS_BR_FREE(data_out);
    } else if (data_out != NULL) {
        MS_BR_FREE(data_out);
    }
    
    return ret;
}

int ms_bridging_event_usb_vin_value(ms_bridging_handler_t *handler, uint32_t usb_vin_value)
{
    return ms_bridging_send_event(handler, MS_BR_FRAME_CMD_USB_VIN_VALUE, &usb_vin_value, sizeof(uint32_t));
}

int ms_bridging_request_pir_value(ms_bridging_handler_t *handler, uint32_t *pir_value)
{
    void *data_out = NULL;
    uint16_t len_out = 0;
    int ret = ms_bridging_request(handler, MS_BR_FRAME_CMD_PIR_VALUE, NULL, 0, &data_out, &len_out);
    
    if (ret == MS_BR_OK && data_out != NULL && len_out == sizeof(uint32_t)) {
        memcpy(pir_value, data_out, sizeof(uint32_t));
        MS_BR_FREE(data_out);
    } else if (data_out != NULL) {
        MS_BR_FREE(data_out);
    }
    
    return ret;
}

int ms_bridging_event_pir_value(ms_bridging_handler_t *handler, uint32_t pir_value)
{
    return ms_bridging_send_event(handler, MS_BR_FRAME_CMD_PIR_VALUE, &pir_value, sizeof(uint32_t));
}

int ms_bridging_request_pir_cfg(ms_bridging_handler_t *handler, ms_bridging_pir_cfg_t *pir_cfg)
{
    void *data_out = NULL;
    uint16_t len_out = 0;
    uint32_t cfg_result = 0;
    int ret = ms_bridging_request(handler, MS_BR_FRAME_CMD_PIR_CFG, pir_cfg, ((pir_cfg == NULL) ? 0 : sizeof(ms_bridging_pir_cfg_t)), &data_out, &len_out);
    
    if (ret == MS_BR_OK && data_out != NULL && len_out == sizeof(uint32_t)) {
        memcpy(&cfg_result, data_out, sizeof(uint32_t));
        MS_BR_FREE(data_out);
        ret = cfg_result;
    } else if (data_out != NULL) {
        MS_BR_FREE(data_out);
    }
    
    return ret;
}

int ms_bridging_request_version(ms_bridging_handler_t *handler, ms_bridging_version_t *version)
{
    void *data_out = NULL;
    uint16_t len_out = 0;
    int ret = ms_bridging_request(handler, MS_BR_FRAME_CMD_GET_VERSION, NULL, 0, &data_out, &len_out);
    
    if (ret == MS_BR_OK && data_out != NULL && version != NULL) {
        if (len_out == sizeof(ms_bridging_version_t)) {
            memcpy(version, data_out, sizeof(ms_bridging_version_t));
        }
        MS_BR_FREE(data_out);
    } else if (data_out != NULL) {
        MS_BR_FREE(data_out);
    }
    
    return ret;
}

int ms_bridging_request_usbin_value(ms_bridging_handler_t *handler, uint32_t *usbin_value)
{
    void *data_out = NULL;
    uint16_t len_out = 0;
    int ret = ms_bridging_request(handler, MS_BR_FRAME_CMD_USB_VIN_VALUE, NULL, 0, &data_out, &len_out);
    
    if (ret == MS_BR_OK && data_out != NULL && len_out == sizeof(uint32_t)) {
        memcpy(usbin_value, data_out, sizeof(uint32_t));
        MS_BR_FREE(data_out);
    } else if (data_out != NULL) {
        MS_BR_FREE(data_out);
    }
    
    return ret;
}

int ms_bridging_get_version_from_str(const char *version_str, ms_bridging_version_t *version)
{
    if (version_str == NULL || version == NULL) return MS_BR_ERR_INVALID_ARG;

    memset(version, 0, sizeof(ms_bridging_version_t));
    sscanf(version_str, "%d.%d.%d.%d", &version->major, &version->minor, &version->patch, &version->build);
    return MS_BR_OK;
}
