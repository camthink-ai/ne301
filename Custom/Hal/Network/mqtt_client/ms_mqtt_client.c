#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Log/debug.h"
#include "Hal/mem.h"
#include "MQTTPacket.h"
#include "ms_network.h"
#include "ms_mqtt_client_priv.h"
#include "ms_mqtt_client.h"

#define MS_MQTT_MIN(a, b)                       ((a) < (b) ? (a) : (b))
#define MS_MQTT_MAX(a, b)                       ((a) > (b) ? (a) : (b))
#define MS_MQTT_MSG_ID(client)                  ((++(client->msg_id)) ? (client->msg_id) : (++(client->msg_id)))
#define MS_MQTT_CHECK_MALLOC(buf, target)       if (buf == NULL) { LOG_LIB_ERROR("MQTT client malloc failed(line: %d)!", __LINE__); goto target; }
#define MS_MQTT_CLIENT_LOCK(client)             xSemaphoreTake(client->lock, portMAX_DELAY)
#define MS_MQTT_CLIENT_UNLOCK(client)           xSemaphoreGive(client->lock)
const static int STOPPED_BIT = (1 << 0);
const static int RECONNECT_BIT = (1 << 1);
const static int DISCONNECT_BIT = (1 << 2);

#if MS_MQTT_CLIENT_IS_DEBUG
static const char *mqtt_error_str_list[] = {
    "SUCCESS",
    "ERR_FAILED",
    "ERR_INVALID_ARG",
    "ERR_INVALID_STATE",
    "ERR_TIMEOUT",
    "ERR_DNS",
    "ERR_SOCKET",
    "ERR_SELECT",
    "ERR_CONN",
    "ERR_SEND",
    "ERR_RECV",
    "ERR_TLS",
    "ERR_TLS_AUTH",
    "ERR_TLS_HANDSHAKE",
    "ERR_TLS_ALERT",
    "ERR_MEM",
    "ERR_SERIAL",
    "ERR_DESERIAL",
    "ERR_SIZE",
    "ERR_RESPONSE",
    "ERR_LIMIT",
    "ERR_FILE",
    "ERR_NETIF",
    "ERR_UNKNOWN",
};
#define MS_MQTT_PRINTF(fmt, ...)        printf("[%s, %d] " fmt "\r\n", __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define MQTT_PRINTF_ERROR_CODE(ret)     printf("[%s, %d] execute result: %s (%d)\r\n", __FUNCTION__, __LINE__, (ret >= 0) ? mqtt_error_str_list[0] : ((ret < -(sizeof(mqtt_error_str_list) / sizeof(mqtt_error_str_list[0]))) ? "UNKNOWN" : mqtt_error_str_list[-ret]), ret)
#else
#define MS_MQTT_PRINTF(fmt, ...)        
#define MQTT_PRINTF_ERROR_CODE(ret)     
#endif

char *ms_strdup(const char *s)
{
    char *p = NULL;
    size_t len = 0;

    if (s == NULL) return NULL;
    len = strlen(s) + 1;
	p = (char *)hal_mem_alloc_large(len);
	if (p == NULL) return NULL;
	memcpy(p, s, len);
    
	return p;
}

static void ms_mqtt_client_send_event(ms_mqtt_client_handle_t client)
{
    int i = 0;
    ms_mqtt_client_event_handler_t event_handler = NULL;

    for (i = 0; i < MS_MQTT_CLIENT_MAX_EVENT_FUNC_SIZE; i++) {
        event_handler = client->event_handler_list[i];
        if (event_handler != NULL) {
            event_handler(&client->event, client->event_user_data[i]);
        }
    }
}

static void ms_mqtt_client_delete_expired_messages(ms_mqtt_client_handle_t client)
{
    int msg_id = 0;

    MS_MQTT_CLIENT_LOCK(client);
#if 1
    while ((msg_id = outbox_delete_single_expired(client->outbox, xTaskGetTickCount(), client->config->network.outbox_expired_timeout)) > 0) {
        client->event.event_id = MQTT_EVENT_DELETED;
        client->event.msg_id = msg_id;
        MS_MQTT_CLIENT_UNLOCK(client);
        ms_mqtt_client_send_event(client);
        MS_MQTT_CLIENT_LOCK(client);
    }
#else
    (void)msg_id;
    outbox_delete_expired(client->outbox, xTaskGetTickCount(), client->config->network.outbox_expired_timeout);
#endif
    MS_MQTT_CLIENT_UNLOCK(client);
}

int ms_mqtt_client_get_outbox_size(ms_mqtt_client_handle_t client)
{
    int size = 0;
    if (client == NULL) return 0;

    MS_MQTT_CLIENT_LOCK(client);
    size = outbox_get_num(client->outbox);
    MS_MQTT_CLIENT_UNLOCK(client);
    return size;
}

static int ms_mqtt_client_delete_outbox(ms_mqtt_client_handle_t client, int msg_id, int msg_type)
{
    int ret = 0;
    
    MS_MQTT_CLIENT_LOCK(client);
    ret = outbox_delete(client->outbox, msg_id, msg_type);
    MS_MQTT_CLIENT_UNLOCK(client);
    return ret;
}

static int ms_mqtt_client_check_header(MQTTHeader *header)
{
    switch (header->bits.type) {
        case CONNECT:
        case CONNACK:
        case PUBACK:
        case PUBREC:
        case PUBCOMP:
        case SUBACK:
        case UNSUBACK:
        case PINGREQ:
        case PINGRESP:
        case DISCONNECT:
            return (header->byte & 0x0f) == 0;  /* all flag bits are 0 */
        case PUBREL:
        case SUBSCRIBE:
        case UNSUBSCRIBE:
            return (header->byte & 0x0f) == 0x02;  /* only bit 1 is set */
        case PUBLISH:
            /*
            * there is no qos=3  [MQTT-3.3.1-4]
            * dup flag must be set to 0 for all qos=0 messages [MQTT-3.3.1-2]
            */
            return (header->bits.qos < 3) && ((header->bits.qos > 0) || (header->bits.dup == 0));
        default:
            return 0;
    }
}

static uint16_t ms_mqtt_client_get_message_id(uint8_t *buffer, size_t length)
{
	MQTTHeader header = {0};
    if (length < 1) {
        return 0;
    }

    header.byte = buffer[0];
    switch (header.bits.type) {
        case PUBLISH: {
            int i;
            int topiclen;

            for (i = 1; i < length; ++i) {
                if ((buffer[i] & 0x80) == 0) {
                    ++i;
                    break;
                }
            }

            if (i + 2 >= length) {
                return 0;
            }
            topiclen = buffer[i++] << 8;
            topiclen |= buffer[i++];

            if (i + topiclen > length) {
                return 0;
            }
            i += topiclen;

            if (header.bits.qos > 0) {
                if (i + 2 > length) {
                    return 0;
                }
                //i += 2;
            } else {
                return 0;
            }

            return (buffer[i] << 8) | buffer[i + 1];
        }
        case PUBACK:
        case PUBREC:
        case PUBREL:
        case PUBCOMP:
        case SUBACK:
        case UNSUBACK:
        case SUBSCRIBE:
        case UNSUBSCRIBE: {
            // This requires the remaining length to be encoded in 1 byte,
            // which it should be.
            if (length >= 4 && (buffer[1] & 0x80) == 0) {
                return (buffer[2] << 8) | buffer[3];
            } else {
                return 0;
            }
        }

        default:
            return 0;
    }
}

static int ms_mqtt_client_read_message(ms_mqtt_client_handle_t client, uint8_t *buffer, int size, uint32_t timeout_ms)
{
    int ret = 0, rlen = 0, msg_len = 0, msg_rlen = 0;
    int multiplier = 1;
    uint8_t tmp_value = 0;
    MQTTHeader header = {0};

    if (size < 2) return MQTT_ERR_INVALID_ARG;
    ret = ms_network_recv((ms_network_handle_t)(client->network_handle), buffer, 1, timeout_ms);
    if (ret < 0) return ret;
    if (ret != 1) return MQTT_ERR_SIZE;
    header.byte = *buffer;
    rlen += ret;
    buffer += ret;
    if (ms_mqtt_client_check_header(&header) == 0) return MQTT_ERR_RESPONSE;

    do {
        if (rlen >= size) return MQTT_ERR_MEM;
        ret = ms_network_recv((ms_network_handle_t)(client->network_handle), &tmp_value, 1, client->config->network.timeout_ms);
        if (ret < 0) return ret;
        if (ret != 1) return MQTT_ERR_SIZE;
        *buffer = tmp_value;
        rlen += ret;
        buffer += ret;

        msg_len += (tmp_value & 127) * multiplier;
        multiplier *= 128;
    } while (rlen < 5 && (tmp_value & 128) != 0);

    if (msg_len + rlen > size) return MQTT_ERR_MEM;
    if (msg_len > 0) {
        do {
            ret = ms_network_recv((ms_network_handle_t)(client->network_handle), buffer + msg_rlen, msg_len - msg_rlen, client->config->network.timeout_ms);
            if (ret < 0) return ret;
            msg_rlen += ret;
        } while (msg_rlen != msg_len);
        if (msg_rlen != msg_len) return MQTT_ERR_SIZE;
        rlen += msg_rlen;
    }

    return rlen;
}

static int ms_mqtt_client_connect(ms_mqtt_client_handle_t client)
{
    int slen = 0, rlen = 0, ret = 0;
    uint8_t *buffer = NULL;
    uint32_t buf_len = 32;
    MQTTPacket_connectData options = MQTTPacket_connectData_initializer;
    options.MQTTVersion = client->config->base.protocol_ver;
    options.clientID.cstring = client->config->base.client_id;
    options.keepAliveInterval = client->config->base.keepalive;
    options.cleansession = client->config->base.clean_session;
    if (client->config->last_will.topic != NULL) {
        options.willFlag = 1;
        options.will.qos = client->config->last_will.qos;
        options.will.retained = client->config->last_will.retain;
        options.will.topicName.cstring = client->config->last_will.topic;
        if (client->config->last_will.msg_len == 0) options.will.message.cstring = client->config->last_will.msg;
        else {
            options.will.message.lenstring.data = client->config->last_will.msg;
            options.will.message.lenstring.len = client->config->last_will.msg_len;
        }
    }
    options.username.cstring = client->config->authentication.username;
    options.password.cstring = client->config->authentication.password;

    buf_len += MQTTstrlen(options.clientID);
    if (options.willFlag) {
        buf_len += MQTTstrlen(options.will.topicName);
        buf_len += MQTTstrlen(options.will.message);
    }
    if (options.username.cstring) buf_len += MQTTstrlen(options.username);
    if (options.password.cstring) buf_len += MQTTstrlen(options.password);

    if (buf_len > MS_MQTT_MAX(client->config->network.tx_buf_size, client->config->network.rx_buf_size)) {
        LOG_LIB_ERROR("MQTT connect buffer size is too large(%ld > %ld)!", buf_len, MS_MQTT_MAX(client->config->network.tx_buf_size, client->config->network.rx_buf_size));
        return MQTT_ERR_MEM;
    }

    buffer = (uint8_t *)hal_mem_alloc_large(buf_len);
    if (buffer == NULL) {
        LOG_LIB_ERROR("MQTT connect buffer alloc failed(buf_len = %ld)!", buf_len);
        return MQTT_ERR_MEM;
    }

    slen = MQTTSerialize_connect(buffer, buf_len, &options);
    if (slen <= 0) {
        hal_mem_free(buffer);
        return MQTT_ERR_SERIAL;
    }

    ret = ms_network_send((ms_network_handle_t)(client->network_handle), buffer, slen, client->config->network.timeout_ms);
    if (ret >= 0 && ret != slen) ret = MQTT_ERR_SIZE;
    if (ret < 0) {
        hal_mem_free(buffer);
        return ret;
    }

    ret = ms_mqtt_client_read_message(client, buffer, buf_len, client->config->network.timeout_ms);
    if (ret < 0) {
        hal_mem_free(buffer);
        return ret;
    }
    rlen = ret;

    if (MQTTDeserialize_connack(&client->event.session_present, &client->event.connect_rsp_code, buffer, rlen) != 1) {
        hal_mem_free(buffer);
        return MQTT_ERR_DESERIAL;
    }

    if (client->event.connect_rsp_code != 0) {
        /*
        0x00 Connection Accepted: Connection successful.
        0x01 Connection Refused, unacceptable protocol version: Server does not support the MQTT protocol version requested by the device.
        0x02 Connection Refused, identifier rejected: clientId parameter format error.
        0x03 Connection Refused, Server unavailable: Network connection established successfully, but MQTT service is unavailable.
        0x04 Connection Refused, bad user name or password: username or password format error.
        0x05 Connection Refused, not authorized: Device is not authorized.
        */
        LOG_LIB_ERROR("MQTT connect failed, connect_rsp_code: %d.", client->event.connect_rsp_code);
        hal_mem_free(buffer);
        return MQTT_ERR_CONN;
    }

    hal_mem_free(buffer);
    return MQTT_ERR_OK;
}

static void ms_mqtt_client_send_disconnect_msg(ms_mqtt_client_handle_t client)
{
    int len = 0;
    uint8_t buf[8] = {0};

    len = MQTTSerialize_disconnect(buf, 8);
    if (len > 0) ms_network_send((ms_network_handle_t)(client->network_handle), buf, len, client->config->network.timeout_ms);
}

static int ms_mqtt_client_send_ping_msg(ms_mqtt_client_handle_t client)
{
    int slen = 0, ret = 0;
    uint8_t buf[8] = {0};

    slen = MQTTSerialize_pingreq(buf, 8);
    if (slen <= 0) return MQTT_ERR_SERIAL;

    ret = ms_network_send((ms_network_handle_t)(client->network_handle), buf, slen, client->config->network.timeout_ms);
    if (ret == slen) ret = MQTT_ERR_OK;
    else if (ret >= 0) ret = MQTT_ERR_SIZE;
    
    MS_MQTT_PRINTF("send ping, ret: %d.", ret);
    return ret;
}

static int ms_mqtt_client_keepalive_process(ms_mqtt_client_handle_t client)
{
    const uint32_t keepalive_ms = client->config->base.keepalive * 1000;
    uint32_t now_tick = 0, diff_tick = 0;

    if (keepalive_ms > 0) {
        now_tick = xTaskGetTickCount();
        if (client->wait_for_ping_resp) {
            diff_tick = now_tick < client->last_ping_tick ? ((portMAX_DELAY - client->last_ping_tick) + now_tick) : (now_tick - client->last_ping_tick);
            if (pdTICKS_TO_MS(diff_tick) >= client->config->network.timeout_ms) {
                if (client->wait_for_ping_resp < MS_MQTT_CLIENT_PING_TRY_COUNT) {
                    client->last_ping_tick = now_tick;
                    client->wait_for_ping_resp ++;
                    return ms_mqtt_client_send_ping_msg(client);
                } else return MQTT_ERR_TIMEOUT;
            }
        } else {
            diff_tick = now_tick < client->keepalive_tick ? ((portMAX_DELAY - client->keepalive_tick) + now_tick) : (now_tick - client->keepalive_tick);
            if (pdTICKS_TO_MS(diff_tick) >= keepalive_ms / 2) {
                client->last_ping_tick = now_tick;
                client->wait_for_ping_resp = 1;
                return ms_mqtt_client_send_ping_msg(client);
            }
        }
    }
    return MQTT_ERR_OK;
}

static int ms_mqtt_client_receive_process(ms_mqtt_client_handle_t client)
{
    int slen = 0, rlen = 0, ret = 0;
    uint8_t *buffer = NULL;
    uint16_t msg_id = 0;
    MQTTHeader header = {0};
    MQTTString topicName = {0};
    
    buffer = (uint8_t *)hal_mem_alloc_large(client->config->network.rx_buf_size);
    if (buffer == NULL) return MQTT_ERR_MEM;

    ret = ms_mqtt_client_read_message(client, buffer, client->config->network.rx_buf_size, pdTICKS_TO_MS(MS_MQTT_CLIENT_TASK_BLOCK_TICK));
    if (ret < 0) {
        hal_mem_free(buffer);
        if (ret == NET_ERR_TIMEOUT) ret = MQTT_ERR_OK;
        return ret;
    }
    rlen = ret;
    ret = MQTT_ERR_OK;

    header.byte = buffer[0];
    msg_id = ms_mqtt_client_get_message_id(buffer, rlen);
    client->event.msg_id = msg_id;
    MS_MQTT_PRINTF("receive, type: %d, msg_id: %d.", header.bits.type, msg_id);
    switch (header.bits.type) {
        case SUBACK:
            if (ms_mqtt_client_delete_outbox(client, msg_id, SUBSCRIBE) == 0) {
                client->event.data = (rlen > 4) ? (buffer + 4) : NULL;
                client->event.data_len = (rlen > 4) ? (rlen - 4) : 0;
                client->event.event_id = MQTT_EVENT_SUBSCRIBED;
                ms_mqtt_client_send_event(client);
            }
            break;
        case UNSUBACK:
            if (ms_mqtt_client_delete_outbox(client, msg_id, UNSUBSCRIBE) == 0) {
                client->event.event_id = MQTT_EVENT_UNSUBSCRIBED;
                ms_mqtt_client_send_event(client);
            }
            break;
        case PUBLISH:
            ret = MQTTDeserialize_publish(&client->event.dup, &client->event.qos, &client->event.retain, &client->event.msg_id, 
                                        &topicName, &client->event.data, &client->event.data_len, buffer, rlen);
            if (ret != 1) {
                ret = MQTT_ERR_DESERIAL;
                break;
            }
            ret = MQTT_ERR_OK;

            client->event.topic = (uint8_t *)topicName.lenstring.data;
            client->event.topic_len = topicName.lenstring.len;
            client->event.event_id = MQTT_EVENT_DATA;
            ms_mqtt_client_send_event(client);

            if (header.bits.qos != 0) {
                if (header.bits.qos == 1) {
                    slen = MQTTSerialize_ack(buffer, client->config->network.rx_buf_size, PUBACK, 0, msg_id);
                } else if (header.bits.qos == 2) {
                    slen = MQTTSerialize_ack(buffer, client->config->network.rx_buf_size, PUBREC, 0, msg_id);
                } else {
                    ret = MQTT_ERR_RESPONSE;
                    break;
                }
                if (slen <= 0) {
                    ret = MQTT_ERR_SERIAL;
                    break;
                }
                ret = ms_network_send((ms_network_handle_t)(client->network_handle), buffer, slen, client->config->network.timeout_ms);
                if (ret == slen) ret = MQTT_ERR_OK;
                else if (ret >= 0) ret = MQTT_ERR_SIZE;
                else break;
            }
            
            break;
        case PUBACK:
            if (ms_mqtt_client_delete_outbox(client, msg_id, PUBLISH) == 0) {
                client->event.event_id = MQTT_EVENT_PUBLISHED;
                ms_mqtt_client_send_event(client);
            }
            break;
        case PUBREC:
            MS_MQTT_CLIENT_LOCK(client);
            outbox_set_pending(client->outbox, msg_id, ACKNOWLEDGED);
            MS_MQTT_CLIENT_UNLOCK(client);
            slen = MQTTSerialize_ack(buffer, client->config->network.rx_buf_size, PUBREL, 0, msg_id);
            if (slen <= 0) {
                ret = MQTT_ERR_SERIAL;
                break;
            }
            ret = ms_network_send((ms_network_handle_t)(client->network_handle), buffer, slen, client->config->network.timeout_ms);
            if (ret == slen) ret = MQTT_ERR_OK;
            else if (ret >= 0) ret = MQTT_ERR_SIZE;
            else break;
            break;
        case PUBREL:
            slen = MQTTSerialize_ack(buffer, client->config->network.rx_buf_size, PUBCOMP, 0, msg_id);
            if (slen <= 0) {
                ret = MQTT_ERR_SERIAL;
                break;
            }
            ret = ms_network_send((ms_network_handle_t)(client->network_handle), buffer, slen, client->config->network.timeout_ms);
            if (ret == slen) ret = MQTT_ERR_OK;
            else if (ret >= 0) ret = MQTT_ERR_SIZE;
            else break;
            break;
        case PUBCOMP:
            if (ms_mqtt_client_delete_outbox(client, msg_id, PUBLISH) == 0) {
                client->event.event_id = MQTT_EVENT_PUBLISHED;
                ms_mqtt_client_send_event(client);
            }
            break;
        case PINGRESP:
            client->wait_for_ping_resp = 0;
            client->keepalive_tick = xTaskGetTickCount();
            break;
        default:
            break;
    }

    hal_mem_free(buffer);
    return ret;
}

static outbox_item_handle_t ms_mqtt_client_outbox_add(ms_mqtt_client_handle_t client, uint8_t *data, int len,int msg_id, int msg_qos, int msg_type)
{
    outbox_message_t msg = {0};
    outbox_item_handle_t item = NULL;

    msg.msg_id = msg_id;
    msg.msg_type = msg_type;
    msg.msg_qos = msg_qos;
    msg.data = data;
    msg.len = len;
    msg.remaining_data = NULL;
    msg.remaining_len = 0;

    item = outbox_enqueue(client->outbox, &msg, xTaskGetTickCount());
    return item;
}

static int ms_mqtt_client_outbox_resend(ms_mqtt_client_handle_t client, outbox_item_handle_t item, uint8_t *is_can_del)
{
    int msg_type = 0, qos = 0, ret = 0;
    size_t buf_len = 0;
    uint8_t *buffer = NULL;
    uint16_t msg_id = 0;
    
    buffer = outbox_item_get_data(item, &buf_len, &msg_id, &msg_type, &qos);
    if (buffer != NULL) {
        if (msg_type == PUBLISH && qos != 0 && (outbox_item_get_pending(item) == TRANSMITTED)) {
            // set duplicate flag for QoS-1 and QoS-2 messages
            buffer[0] |= 0x08;
        }
        ret = ms_network_send((ms_network_handle_t)(client->network_handle), buffer, buf_len, client->config->network.timeout_ms);
        if (ret == buf_len) {
            if (is_can_del != NULL && msg_type == PUBLISH && qos == 0) *is_can_del = 1;
            ret = MQTT_ERR_OK;
        } else if (ret >= 0) ret = MQTT_ERR_SIZE;
    } else ret = MQTT_ERR_RESPONSE;
    return ret;
}

static int ms_mqtt_client_outbox_process(ms_mqtt_client_handle_t client)
{
    int ret = 0;
    uint8_t is_can_del = 0;
    uint32_t now_tick = 0, diff_tick = 0, msg_tick = 0;
    outbox_item_handle_t item;

    if (client->last_retransmit_tick == 0) client->last_retransmit_tick = xTaskGetTickCount();
    
    item = outbox_dequeue(client->outbox, QUEUED, NULL);
    if (item) {
        ret = ms_mqtt_client_outbox_resend(client, item, &is_can_del);
        if (ret == MQTT_ERR_OK) {
            if (is_can_del) outbox_delete_item(client->outbox, item);
            else outbox_item_set_pending(item, TRANSMITTED);
        }
    } else {
        now_tick = xTaskGetTickCount();
        diff_tick = now_tick < client->last_retransmit_tick ? ((portMAX_DELAY - client->last_retransmit_tick) + now_tick) : (now_tick - client->last_retransmit_tick);
        if (pdTICKS_TO_MS(diff_tick) >= client->config->network.outbox_resend_interval_ms) {
            client->last_retransmit_tick = now_tick;
            item = outbox_dequeue(client->outbox, TRANSMITTED, &msg_tick);
            diff_tick = now_tick < msg_tick ? ((portMAX_DELAY - msg_tick) + now_tick) : (now_tick - msg_tick);
            if (item && pdTICKS_TO_MS(diff_tick) >= client->config->network.outbox_resend_interval_ms) {
                ret = ms_mqtt_client_outbox_resend(client, item, NULL);
            }
        }
    }

    return ret;
}

static void ms_mqtt_client_task(void *param)
{
    int ret = 0;
    uint32_t now_tick = 0, diff_tick = 0, tmp_tick = 0;
    EventBits_t event_bits = 0;
    ms_mqtt_state_t state;
    ms_mqtt_client_handle_t client = (ms_mqtt_client_handle_t)param;

    MS_MQTT_CLIENT_LOCK(client);
    client->run = 1;
    client->state = MQTT_STATE_STARTING;
    MS_MQTT_CLIENT_UNLOCK(client);
    xEventGroupClearBits(client->status_bits, STOPPED_BIT);
    client->event.event_id = MQTT_EVENT_STARTED;
    ms_mqtt_client_send_event(client);
    while (client->run) {
        MS_MQTT_CLIENT_LOCK(client);
        memset(&client->event, 0, sizeof(ms_mqtt_event_data_t));
        client->event.client = client;
        state = client->state;
        MS_MQTT_CLIENT_UNLOCK(client);
        ms_mqtt_client_delete_expired_messages(client);
        switch (state) {
            case MQTT_STATE_STARTING:
                xEventGroupClearBits(client->status_bits, RECONNECT_BIT | DISCONNECT_BIT);
                client->event.event_id = MQTT_EVENT_BEFORE_CONNECT;
                ms_mqtt_client_send_event(client);
                // Connect to MQTT server
                ret = ms_network_connect((ms_network_handle_t)(client->network_handle), client->config->base.hostname, client->config->base.port, client->config->network.timeout_ms);
                if (ret != MQTT_ERR_OK) {
                    client->state = MQTT_STATE_WAIT_RECONNECT;
                    client->event.event_id = MQTT_EVENT_ERROR;
                    client->event.error_code = ret;
                    ms_mqtt_client_send_event(client);
                    LOG_LIB_ERROR("MQTT network connect failed, error code: %d", ret);
                    MQTT_PRINTF_ERROR_CODE(ret);
                    break;
                }
                // Connection handshake
                ret = ms_mqtt_client_connect(client);
                if (ret != MQTT_ERR_OK) {
                    client->state = MQTT_STATE_WAIT_RECONNECT;
                    client->event.event_id = MQTT_EVENT_ERROR;
                    client->event.error_code = ret;
                    ms_mqtt_client_send_event(client);
                    LOG_LIB_ERROR("MQTT client connect failed, error code: %d", ret);
                    MQTT_PRINTF_ERROR_CODE(ret);
                    break;
                }
                client->state = MQTT_STATE_CONNECTED;
                client->wait_for_ping_resp = 0;
                client->reconnect_tick = xTaskGetTickCount();
                client->keepalive_tick = xTaskGetTickCount();
                client->event.event_id = MQTT_EVENT_CONNECTED;
                ms_mqtt_client_send_event(client);
                break;
            case MQTT_STATE_CONNECTED:
                if (xEventGroupWaitBits(client->status_bits, DISCONNECT_BIT, true, true, 0) & DISCONNECT_BIT) {
                    // Disconnect from MQTT server
                    ms_mqtt_client_send_disconnect_msg(client);
                    ms_network_close((ms_network_handle_t)(client->network_handle));
                    client->state = MQTT_STATE_DISCONNECTED;
                    client->event.event_id = MQTT_EVENT_DISCONNECTED;
                    ms_mqtt_client_send_event(client);
                    break;
                }
                // Process received messages
                ret = ms_mqtt_client_receive_process(client);
                if (ret != MQTT_ERR_OK) {
                    ms_network_close((ms_network_handle_t)(client->network_handle));
                    client->state = MQTT_STATE_WAIT_RECONNECT;
                    client->event.event_id = MQTT_EVENT_DISCONNECTED;
                    client->event.error_code = ret;
                    ms_mqtt_client_send_event(client);
                    LOG_LIB_ERROR("MQTT receive process failed, error code: %d", ret);
                    MQTT_PRINTF_ERROR_CODE(ret);
                    break;
                }
                // Send queued messages
                MS_MQTT_CLIENT_LOCK(client);
                ret = ms_mqtt_client_outbox_process(client);
                MS_MQTT_CLIENT_UNLOCK(client);
                if (ret != MQTT_ERR_OK) {
                    ms_network_close((ms_network_handle_t)(client->network_handle));
                    client->state = MQTT_STATE_WAIT_RECONNECT;
                    client->event.event_id = MQTT_EVENT_DISCONNECTED;
                    client->event.error_code = ret;
                    ms_mqtt_client_send_event(client);
                    LOG_LIB_ERROR("MQTT outbox process failed, error code: %d", ret);
                    MQTT_PRINTF_ERROR_CODE(ret);
                    break;
                }
                // Heartbeat processing
                ret = ms_mqtt_client_keepalive_process(client);
                if (ret != MQTT_ERR_OK) {
                    ms_network_close((ms_network_handle_t)(client->network_handle));
                    client->state = MQTT_STATE_WAIT_RECONNECT;
                    client->event.event_id = MQTT_EVENT_DISCONNECTED;
                    client->event.error_code = ret;
                    ms_mqtt_client_send_event(client);
                    LOG_LIB_ERROR("MQTT keep alive process failed, error code: %d", ret);
                    MQTT_PRINTF_ERROR_CODE(ret);
                    break;
                }
                break;
            case MQTT_STATE_WAIT_RECONNECT:
                if (client->config->network.disable_auto_reconnect) {
                    client->state = MQTT_STATE_DISCONNECTED;
                    break;
                }
                now_tick = xTaskGetTickCount();
                tmp_tick = pdMS_TO_TICKS(client->config->network.reconnect_interval_ms);
                diff_tick = now_tick < client->reconnect_tick ? ((portMAX_DELAY - client->reconnect_tick) + now_tick) : (now_tick - client->reconnect_tick);
                event_bits = 0;
                if (diff_tick > tmp_tick || (event_bits = xEventGroupWaitBits(client->status_bits, RECONNECT_BIT | DISCONNECT_BIT, true, false, MS_MQTT_MIN(diff_tick, MS_MQTT_CLIENT_TASK_BLOCK_TICK))) != 0) {
                    if (event_bits == 0 || (event_bits & RECONNECT_BIT)) {
                        client->state = MQTT_STATE_STARTING;
                        client->reconnect_tick = xTaskGetTickCount();
                        MS_MQTT_PRINTF("reconnecting...");
                    } else {
                        client->state = MQTT_STATE_DISCONNECTED;
                        MS_MQTT_PRINTF("into disconnected state.");
                    }
                }
                break;
            case MQTT_STATE_DISCONNECTED:
                if (xEventGroupWaitBits(client->status_bits, RECONNECT_BIT, true, true, MS_MQTT_CLIENT_TASK_BLOCK_TICK) & RECONNECT_BIT) {
                    client->state = MQTT_STATE_STARTING;
                    client->reconnect_tick = xTaskGetTickCount();
                    MS_MQTT_PRINTF("reconnecting...");
                }
                break;
            default:
                MS_MQTT_PRINTF("state(%d) error!", state);
                osDelay(MS_MQTT_CLIENT_TASK_BLOCK_TICK);
                break;
        }
    }
    client->event.event_id = MQTT_EVENT_STOPPED;
    ms_mqtt_client_send_event(client);
    MS_MQTT_CLIENT_LOCK(client);
    outbox_delete_all_items(client->outbox);
    client->state = MQTT_STATE_STOPPED;
    // client->task_handle = NULL;
    MS_MQTT_CLIENT_UNLOCK(client);
    xEventGroupSetBits(client->status_bits, STOPPED_BIT);
    // vTaskDelete(NULL);
}

#include "storage.h"
int ms_mqtt_client_get_cert_from_file(const char *cert_path, uint8_t **cert_data, uint32_t *cert_len)
{
    int ret = 0;
    void *fd = NULL;
    struct stat st = {0};
    if (cert_path == NULL) return MQTT_ERR_INVALID_ARG;
    if (cert_data == NULL) return MQTT_ERR_INVALID_ARG;
    if (cert_len == NULL) return MQTT_ERR_INVALID_ARG;

    fd = flash_lfs_fopen(cert_path, "r");
    if (fd == NULL) return MQTT_ERR_FILE;

    ret = flash_lfs_stat(cert_path, &st);
    if (ret != 0) {
        ret = MQTT_ERR_FILE;
        goto ms_mqtt_client_get_cert_from_file_failed;
    }
    if (st.st_size > MS_MQTT_CLIENT_MAX_CERT_DATA_SIZE) {
        ret = MQTT_ERR_FILE;
        goto ms_mqtt_client_get_cert_from_file_failed;
    }

    *cert_len = st.st_size;
    *cert_data = hal_mem_alloc_large(*cert_len);
    if (*cert_data == NULL) {
        ret = MQTT_ERR_MEM;
        goto ms_mqtt_client_get_cert_from_file_failed;
    }

    ret = flash_lfs_fread(fd, *cert_data, *cert_len);
    if (ret != *cert_len) {
        ret = MQTT_ERR_FILE;
        goto ms_mqtt_client_get_cert_from_file_failed;
    }

    flash_lfs_fclose(fd);
    fd = NULL;
    return MQTT_ERR_OK;

ms_mqtt_client_get_cert_from_file_failed:
    if (*cert_data != NULL) hal_mem_free(*cert_data);
    if (fd != NULL) flash_lfs_fclose(fd);
    MQTT_PRINTF_ERROR_CODE(ret);
    return ret;
}

ms_mqtt_client_handle_t ms_mqtt_client_init(const ms_mqtt_config_t *config)
{
    ms_mqtt_client_handle_t client = NULL;
    network_tls_config_t tls_config = {0};
    if (config == NULL) return NULL;

    // TODO: Check configuration
    
    // Create client
    client = (ms_mqtt_client_handle_t)hal_mem_alloc_large(sizeof(struct ms_mqtt_client));
    if (client == NULL) {
        LOG_LIB_ERROR("MQTT client handle malloc failed!");
        return NULL;
    }
    memset(client, 0, sizeof(struct ms_mqtt_client));

    // Copy configuration
    client->config = (ms_mqtt_config_t *)hal_mem_calloc_fast(1, sizeof(ms_mqtt_config_t));
    if (client->config == NULL) {
        LOG_LIB_ERROR("MQTT client config handle malloc failed!");
        goto ms_mqtt_client_init_failed;
    }
    memcpy(client->config, config, sizeof(ms_mqtt_config_t));
    client->config->base.hostname = NULL;
    client->config->base.client_id = NULL;
    client->config->authentication.username = NULL;
    client->config->authentication.password = NULL;
    client->config->authentication.ca_path = NULL;
    client->config->authentication.client_cert_path = NULL;
    client->config->authentication.client_key_path = NULL;
    client->config->authentication.ca_data = NULL;
    client->config->authentication.client_cert_data = NULL;
    client->config->authentication.client_key_data = NULL;
    client->config->last_will.topic = NULL;
    client->config->last_will.msg = NULL;
    if (client->config->network.tx_buf_size <= 0) client->config->network.tx_buf_size = client->config->network.buffer_size;
    if (client->config->network.rx_buf_size <= 0) client->config->network.rx_buf_size = client->config->network.buffer_size;
    client->config->base.hostname = ms_strdup(config->base.hostname);
    MS_MQTT_CHECK_MALLOC(client->config->base.hostname, ms_mqtt_client_init_failed);
    client->config->base.client_id = ms_strdup(config->base.client_id);
    MS_MQTT_CHECK_MALLOC(client->config->base.client_id, ms_mqtt_client_init_failed);
    if (config->authentication.username != NULL && config->authentication.password != NULL) {
        client->config->authentication.username = ms_strdup(config->authentication.username);
        MS_MQTT_CHECK_MALLOC(client->config->authentication.username, ms_mqtt_client_init_failed);
        client->config->authentication.password = ms_strdup(config->authentication.password);
        MS_MQTT_CHECK_MALLOC(client->config->authentication.password, ms_mqtt_client_init_failed);
    }

    if (config->authentication.ca_path != NULL) {
        client->config->authentication.ca_path = ms_strdup(config->authentication.ca_path);
        MS_MQTT_CHECK_MALLOC(client->config->authentication.ca_path, ms_mqtt_client_init_failed);
        uint32_t ca_len_uint32 = 0;
        if (ms_mqtt_client_get_cert_from_file(client->config->authentication.ca_path, (uint8_t **)&client->config->authentication.ca_data, &ca_len_uint32) != MQTT_ERR_OK) {
            client->config->authentication.ca_len = 0;
            LOG_LIB_ERROR("MQTT client get ca from file failed!");
            goto ms_mqtt_client_init_failed;
        }
        client->config->authentication.ca_len = (size_t)ca_len_uint32;
    } else if (config->authentication.ca_data != NULL) {
        if (config->authentication.ca_len == 0) {
            client->config->authentication.ca_data = ms_strdup(config->authentication.ca_data);
            MS_MQTT_CHECK_MALLOC(client->config->authentication.ca_data, ms_mqtt_client_init_failed);
            client->config->authentication.ca_len = strlen(config->authentication.ca_data);
        } else {
            client->config->authentication.ca_data = hal_mem_alloc_large(config->authentication.ca_len);
            MS_MQTT_CHECK_MALLOC(client->config->authentication.ca_data, ms_mqtt_client_init_failed);
            memcpy(client->config->authentication.ca_data, config->authentication.ca_data, config->authentication.ca_len);
            client->config->authentication.ca_len = config->authentication.ca_len;
        }
    } else {
        client->config->authentication.ca_len = 0;
    }

    if (config->authentication.client_cert_path != NULL) {
        client->config->authentication.client_cert_path = ms_strdup(config->authentication.client_cert_path);
        MS_MQTT_CHECK_MALLOC(client->config->authentication.client_cert_path, ms_mqtt_client_init_failed);
        uint32_t client_cert_len_uint32 = 0;
        if (ms_mqtt_client_get_cert_from_file(client->config->authentication.client_cert_path, (uint8_t **)&client->config->authentication.client_cert_data, &client_cert_len_uint32) != MQTT_ERR_OK) {
            client->config->authentication.client_cert_len = 0;
            LOG_LIB_ERROR("MQTT client get client cert from file failed!");
            goto ms_mqtt_client_init_failed;
        }
        client->config->authentication.client_cert_len = (size_t)client_cert_len_uint32;
    } else if (config->authentication.client_cert_data != NULL) {
        if (config->authentication.client_cert_len == 0) {
            client->config->authentication.client_cert_data = ms_strdup(config->authentication.client_cert_data);
            MS_MQTT_CHECK_MALLOC(client->config->authentication.client_cert_data, ms_mqtt_client_init_failed);
            client->config->authentication.client_cert_len = strlen(config->authentication.client_cert_data);
        } else {
            client->config->authentication.client_cert_data = hal_mem_alloc_large(config->authentication.client_cert_len);
            MS_MQTT_CHECK_MALLOC(client->config->authentication.client_cert_data, ms_mqtt_client_init_failed);
            memcpy(client->config->authentication.client_cert_data, config->authentication.client_cert_data, config->authentication.client_cert_len);
            client->config->authentication.client_cert_len = config->authentication.client_cert_len;
        }
    } else {
        client->config->authentication.client_cert_len = 0;
    }

    if (config->authentication.client_key_path != NULL) {
        client->config->authentication.client_key_path = ms_strdup(config->authentication.client_key_path);
        MS_MQTT_CHECK_MALLOC(client->config->authentication.client_key_path, ms_mqtt_client_init_failed);
        uint32_t client_key_len_uint32 = 0;
        if (ms_mqtt_client_get_cert_from_file(client->config->authentication.client_key_path, (uint8_t **)&client->config->authentication.client_key_data, &client_key_len_uint32) != MQTT_ERR_OK) {
            client->config->authentication.client_key_len = 0;
            LOG_LIB_ERROR("MQTT client get client key from file failed!");
            goto ms_mqtt_client_init_failed;
        }
        client->config->authentication.client_key_len = (size_t)client_key_len_uint32;
    } else if (config->authentication.client_key_data != NULL) {
        if (config->authentication.client_key_len == 0) {
            client->config->authentication.client_key_data = ms_strdup(config->authentication.client_key_data);
            MS_MQTT_CHECK_MALLOC(client->config->authentication.client_key_data, ms_mqtt_client_init_failed);
            client->config->authentication.client_key_len = strlen(config->authentication.client_key_data);
        } else {
            client->config->authentication.client_key_data = hal_mem_alloc_large(config->authentication.client_key_len);
            MS_MQTT_CHECK_MALLOC(client->config->authentication.client_key_data, ms_mqtt_client_init_failed);
            memcpy(client->config->authentication.client_key_data, config->authentication.client_key_data, config->authentication.client_key_len);
            client->config->authentication.client_key_len = config->authentication.client_key_len;
        }
    } else {
        client->config->authentication.client_key_len = 0;
    }

    if (config->last_will.topic != NULL) {
        client->config->last_will.topic = ms_strdup(config->last_will.topic);
        MS_MQTT_CHECK_MALLOC(client->config->last_will.topic, ms_mqtt_client_init_failed);

        if (config->last_will.msg != NULL) {
            if (config->last_will.msg_len == 0) {
                client->config->last_will.msg = ms_strdup(config->last_will.msg);
                MS_MQTT_CHECK_MALLOC(client->config->last_will.msg, ms_mqtt_client_init_failed);
            } else {
                client->config->last_will.msg = hal_mem_alloc_large(config->last_will.msg_len);
                MS_MQTT_CHECK_MALLOC(client->config->last_will.msg, ms_mqtt_client_init_failed);
                memcpy(client->config->last_will.msg, config->last_will.msg, config->last_will.msg_len);
                client->config->last_will.msg_len = config->last_will.msg_len;
            }
        }
    }

    //print key data, every line end with \n
    // printf("Client Key Data:\r\n");
    // for (int i = 0; i < client->config->authentication.client_key_len; i++) {
    //     if('\n' == client->config->authentication.client_key_data[i]) {
    //         printf("\r\n");
    //     }
    //     else
    //     printf("%c", client->config->authentication.client_key_data[i]);
    // }
    // printf("Client Cert Data:\r\n");
    // for (int i = 0; i < client->config->authentication.client_cert_len; i++) {
    //     if('\n' == client->config->authentication.client_cert_data[i]) {
    //         printf("\r\n");
    //     }
    //     else
    //     printf("%c", client->config->authentication.client_cert_data[i]);
    // }
    // printf("CA Data:\r\n");
    // for (int i = 0; i < client->config->authentication.ca_len; i++) {
    //     if('\n' == client->config->authentication.ca_data[i]) {
    //         printf("\r\n");
    //     }
    //     else
    //     printf("%c", client->config->authentication.ca_data[i]);
    // }
    // printf("\r\n");

    // Initialize variables
    client->state = MQTT_STATE_STOPPED;
    client->run = 0;
    client->wait_for_ping_resp = 0;
    client->msg_id = 0;
    client->last_retransmit_tick = 0;
    client->last_ping_tick = 0;
    client->keepalive_tick = xTaskGetTickCount();
    client->reconnect_tick = xTaskGetTickCount();
    client->lock = xSemaphoreCreateMutex();
    if (client->lock == NULL) {
        LOG_LIB_ERROR("MQTT client lock handle malloc failed!");
        goto ms_mqtt_client_init_failed;
    }
    client->status_bits = xEventGroupCreate();
    if (client->status_bits == NULL) {
        LOG_LIB_ERROR("MQTT client event group handle malloc failed!");
        goto ms_mqtt_client_init_failed;
    }
    client->outbox = outbox_init();
    if (client->outbox == NULL) {
        LOG_LIB_ERROR("MQTT client outbox handle malloc failed!");
        goto ms_mqtt_client_init_failed;
    }

    // Initialize network
    tls_config.ca_data = client->config->authentication.ca_data;
    tls_config.ca_len = client->config->authentication.ca_len;
    tls_config.client_cert_data = client->config->authentication.client_cert_data;
    tls_config.client_cert_len = client->config->authentication.client_cert_len;
    tls_config.client_key_data = client->config->authentication.client_key_data;
    tls_config.client_key_len = client->config->authentication.client_key_len;
    tls_config.is_verify_hostname = client->config->authentication.is_verify_hostname;
    client->network_handle = (void *)ms_network_init(&tls_config);
    if ((ms_network_handle_t)(client->network_handle) == NULL) {
        LOG_LIB_ERROR("MQTT client network handle init failed!");
        goto ms_mqtt_client_init_failed;
    }

    return client;
ms_mqtt_client_init_failed:
    ms_mqtt_client_destroy(client);
    MQTT_PRINTF_ERROR_CODE(MQTT_ERR_FAILED);
    return NULL;
}

int ms_mqtt_client_destroy(ms_mqtt_client_handle_t client)
{
    if (client == NULL) return MQTT_ERR_INVALID_ARG;

    if (client->run) {
        ms_mqtt_client_stop(client);
    }

    if (client->config != NULL) {
        if (client->config->base.hostname != NULL) hal_mem_free(client->config->base.hostname);
        if (client->config->base.client_id != NULL) hal_mem_free(client->config->base.client_id);
        if (client->config->authentication.username != NULL) hal_mem_free(client->config->authentication.username);
        if (client->config->authentication.password != NULL) hal_mem_free(client->config->authentication.password);
        if (client->config->authentication.ca_path != NULL) hal_mem_free(client->config->authentication.ca_path);
        if (client->config->authentication.client_cert_path != NULL) hal_mem_free(client->config->authentication.client_cert_path);
        if (client->config->authentication.client_key_path != NULL) hal_mem_free(client->config->authentication.client_key_path);
        if (client->config->authentication.ca_data != NULL) hal_mem_free(client->config->authentication.ca_data);
        if (client->config->authentication.client_cert_data != NULL) hal_mem_free(client->config->authentication.client_cert_data);
        if (client->config->authentication.client_key_data != NULL) hal_mem_free(client->config->authentication.client_key_data);
        if (client->config->last_will.topic != NULL) hal_mem_free(client->config->last_will.topic);
        if (client->config->last_will.msg != NULL) hal_mem_free(client->config->last_will.msg);
        hal_mem_free(client->config);
    }

    if ((ms_network_handle_t)(client->network_handle) != NULL) {
    	ms_network_deinit((ms_network_handle_t)(client->network_handle));
    }

    if (client->outbox != NULL) {
        outbox_destroy(client->outbox);
    }

    if (client->status_bits != NULL) {
        vEventGroupDelete(client->status_bits);
    }

    if (client->lock != NULL) {
        vSemaphoreDelete(client->lock);
    }

    hal_mem_free(client);
    MQTT_PRINTF_ERROR_CODE(MQTT_ERR_OK);
    return MQTT_ERR_OK;
}

int ms_mqtt_client_start(ms_mqtt_client_handle_t client)
{
    if (client == NULL) return MQTT_ERR_INVALID_ARG;

    MS_MQTT_CLIENT_LOCK(client);
    if (client->task_handle != NULL) {
        LOG_LIB_ERROR("MQTT client task already exists!");
        MS_MQTT_CLIENT_UNLOCK(client); 
        // return MQTT_ERR_INVALID_STATE;
        return MQTT_ERR_OK;
    }

    if (xTaskCreate(ms_mqtt_client_task, "mqtt_task", client->config->task.stack_size, client, client->config->task.priority, &client->task_handle) != pdTRUE) {
        LOG_LIB_ERROR("MQTT client task create failed!");
        MS_MQTT_CLIENT_UNLOCK(client); 
        return MQTT_ERR_MEM;
    }

    MS_MQTT_CLIENT_UNLOCK(client); 
    MQTT_PRINTF_ERROR_CODE(MQTT_ERR_OK);
    return MQTT_ERR_OK;
}

int ms_mqtt_client_reconnect(ms_mqtt_client_handle_t client)
{
    if (client == NULL) return MQTT_ERR_INVALID_ARG;
    if (client->state == MQTT_STATE_STARTING || client->state == MQTT_STATE_CONNECTED) return MQTT_ERR_OK;
    // if (client->state != MQTT_STATE_WAIT_RECONNECT && client->state != MQTT_STATE_DISCONNECTED) {
    if (client->state == MQTT_STATE_STOPPED) {
        MQTT_PRINTF_ERROR_CODE(MQTT_ERR_INVALID_STATE);
        return MQTT_ERR_INVALID_STATE;
    }

    xEventGroupSetBits(client->status_bits, RECONNECT_BIT);
    MQTT_PRINTF_ERROR_CODE(MQTT_ERR_OK);
    return MQTT_ERR_OK;
}

int ms_mqtt_client_disconnect(ms_mqtt_client_handle_t client)
{
    if (client == NULL) return MQTT_ERR_INVALID_ARG;
    if (client->state == MQTT_STATE_DISCONNECTED) return MQTT_ERR_OK;
    // if (client->state != MQTT_STATE_WAIT_RECONNECT && client->state != MQTT_STATE_CONNECTED) {
    if (client->state == MQTT_STATE_STOPPED) {
        // MQTT_PRINTF_ERROR_CODE(MQTT_ERR_INVALID_STATE);
        // return MQTT_ERR_INVALID_STATE;
        return MQTT_ERR_OK;
    }

    xEventGroupSetBits(client->status_bits, DISCONNECT_BIT);
    MQTT_PRINTF_ERROR_CODE(MQTT_ERR_OK);
    return MQTT_ERR_OK;
}

int ms_mqtt_client_stop(ms_mqtt_client_handle_t client)
{
    if (client == NULL) return MQTT_ERR_INVALID_ARG;

    MS_MQTT_CLIENT_LOCK(client);
    if (client->task_handle == NULL) {
        MS_MQTT_CLIENT_UNLOCK(client); 
        // MQTT_PRINTF_ERROR_CODE(MQTT_ERR_INVALID_STATE);
        // return MQTT_ERR_INVALID_STATE;
        return MQTT_ERR_OK;
    }

    if (client->run) {
        if (client->state == MQTT_STATE_CONNECTED) {
            ms_mqtt_client_send_disconnect_msg(client);
            ms_network_close((ms_network_handle_t)(client->network_handle));
        }
        client->run = 0;
        client->state = MQTT_STATE_DISCONNECTED;
        MS_MQTT_CLIENT_UNLOCK(client); 
        xEventGroupWaitBits(client->status_bits, STOPPED_BIT, true, true, portMAX_DELAY);
        vTaskDelete(client->task_handle);
        client->task_handle = NULL;
    } else {
        MS_MQTT_CLIENT_UNLOCK(client);
    }

    MQTT_PRINTF_ERROR_CODE(MQTT_ERR_OK);
    return MQTT_ERR_OK;
}

int ms_mqtt_client_subscribe_single(ms_mqtt_client_handle_t client, char *topic, int qos)
{
    ms_mqtt_topic_t mqtt_topic = {0};

    mqtt_topic.filter = topic;
    mqtt_topic.qos = qos;
    return ms_mqtt_client_subscribe_multiple(client, &mqtt_topic, 1);
}

int ms_mqtt_client_subscribe_multiple(ms_mqtt_client_handle_t client, const ms_mqtt_topic_t *topic_list, int size)
{
    int slen = 0, ret = 0, i = 0;
    uint8_t *buffer = NULL;
    uint16_t msg_id = 0;
    uint32_t buf_len = 8;
    MQTTString *topics = NULL;
    int *qoss = NULL;
    if (client == NULL || topic_list == NULL || size <= 0) return MQTT_ERR_INVALID_ARG;
    if (client->state != MQTT_STATE_CONNECTED) {
        MQTT_PRINTF_ERROR_CODE(MQTT_ERR_INVALID_STATE);
        return MQTT_ERR_INVALID_STATE;
    }
    if (client->config->network.outbox_limit > 0 && ms_mqtt_client_get_outbox_size(client) > client->config->network.outbox_limit) {
        MQTT_PRINTF_ERROR_CODE(MQTT_ERR_LIMIT);
        return MQTT_ERR_LIMIT;
    }

    topics = (MQTTString *)hal_mem_alloc_large(sizeof(MQTTString) * size);
    if (topics == NULL) {
        ret = MQTT_ERR_MEM;
        goto ms_mqtt_client_subscribe_multiple_end;
    }

    qoss = (int *)hal_mem_alloc_large(sizeof(int) * size);
    if (qoss == NULL) {
        ret = MQTT_ERR_MEM;
        goto ms_mqtt_client_subscribe_multiple_end;
    }

    for (i = 0; i < size; i++) {
        topics[i].cstring = topic_list[i].filter;
        topics[i].lenstring.data = NULL;
        topics[i].lenstring.len = 0;
        qoss[i] = topic_list[i].qos;
        buf_len += (MQTTstrlen(topics[i]) + 4);
    }

    if (buf_len > client->config->network.tx_buf_size) {
        ret = MQTT_ERR_MEM;
        goto ms_mqtt_client_subscribe_multiple_end;
    }

    buffer = (uint8_t *)hal_mem_alloc_large(buf_len);
    if (buffer == NULL) {
        ret = MQTT_ERR_MEM;
        goto ms_mqtt_client_subscribe_multiple_end;
    }

    MS_MQTT_CLIENT_LOCK(client);
    msg_id = MS_MQTT_MSG_ID(client);
    MS_MQTT_CLIENT_UNLOCK(client);
    slen = MQTTSerialize_subscribe(buffer, buf_len, 0, msg_id, size, topics, qoss);
    if (slen <= 0) {
        ret = MQTT_ERR_SERIAL;
        goto ms_mqtt_client_subscribe_multiple_end;
    }

    ret = ms_network_send((ms_network_handle_t)(client->network_handle), buffer, slen, client->config->network.timeout_ms);
    if (ret == slen) ret = msg_id;
    else if (ret >= 0) {
        MS_MQTT_PRINTF("Actual send size: %d, expected send size: %d.", ret, slen);
        ret = MQTT_ERR_SIZE;
    }

    MS_MQTT_CLIENT_LOCK(client);
    if (ms_mqtt_client_outbox_add(client, buffer, slen, msg_id, 0, SUBSCRIBE) == NULL) {
        ret = MQTT_ERR_MEM;
        MS_MQTT_CLIENT_UNLOCK(client);
        goto ms_mqtt_client_subscribe_multiple_end;
    }
    outbox_set_pending(client->outbox, msg_id, TRANSMITTED);
    MS_MQTT_CLIENT_UNLOCK(client);

ms_mqtt_client_subscribe_multiple_end:
    if (buffer) hal_mem_free(buffer);
    if (topics) hal_mem_free(topics);
    if (qoss) hal_mem_free(qoss);
    MQTT_PRINTF_ERROR_CODE(ret);
    return ret;
}


int ms_mqtt_client_unsubscribe(ms_mqtt_client_handle_t client, char *topic)
{
    int slen = 0, ret = 0;
    uint8_t *buffer = NULL;
    uint16_t msg_id = 0;
    uint32_t buf_len = 8;
    MQTTString topic_str = MQTTString_initializer;
    if (client == NULL || topic == NULL) return MQTT_ERR_INVALID_ARG;
    if (client->state != MQTT_STATE_CONNECTED) {
        MQTT_PRINTF_ERROR_CODE(MQTT_ERR_INVALID_STATE);
        return MQTT_ERR_INVALID_STATE;
    }
    if (client->config->network.outbox_limit > 0 && ms_mqtt_client_get_outbox_size(client) > client->config->network.outbox_limit) {
        MQTT_PRINTF_ERROR_CODE(MQTT_ERR_LIMIT);
        return MQTT_ERR_LIMIT;
    }

    buf_len += (strlen(topic) + 4);
    if (buf_len > client->config->network.tx_buf_size) {
        ret = MQTT_ERR_MEM;
        goto ms_mqtt_client_unsubscribe_end;
    }

    buffer = (uint8_t *)hal_mem_alloc_large(buf_len);
    if (buffer == NULL) {
        ret = MQTT_ERR_MEM;
        goto ms_mqtt_client_unsubscribe_end;
    }

    MS_MQTT_CLIENT_LOCK(client);
    msg_id = MS_MQTT_MSG_ID(client);
    MS_MQTT_CLIENT_UNLOCK(client);
    topic_str.cstring = topic;
    slen = MQTTSerialize_unsubscribe(buffer, buf_len, 0, msg_id, 1, &topic_str);
    if (slen <= 0) {
        ret = MQTT_ERR_SERIAL;
        goto ms_mqtt_client_unsubscribe_end;
    }

    ret = ms_network_send((ms_network_handle_t)(client->network_handle), buffer, slen, client->config->network.timeout_ms);
    if (ret == slen) ret = msg_id;
    else if (ret >= 0) {
        MS_MQTT_PRINTF("Actual send size: %d, expected send size: %d.", ret, slen);
        ret = MQTT_ERR_SIZE;
    }

    MS_MQTT_CLIENT_LOCK(client);
    if (ms_mqtt_client_outbox_add(client, buffer, slen, msg_id, 0, UNSUBSCRIBE) == NULL) {
        ret = MQTT_ERR_MEM;
        MS_MQTT_CLIENT_UNLOCK(client);
        goto ms_mqtt_client_unsubscribe_end;
    }
    outbox_set_pending(client->outbox, msg_id, TRANSMITTED);
    MS_MQTT_CLIENT_UNLOCK(client);

ms_mqtt_client_unsubscribe_end:
    if (buffer) hal_mem_free(buffer);
    MQTT_PRINTF_ERROR_CODE(ret);
    return ret;
}

int ms_mqtt_client_publish(ms_mqtt_client_handle_t client, char *topic, uint8_t *data, int len, int qos, int retain)
{
    int slen = 0, ret = 0;
    uint8_t *buffer = NULL;
    uint16_t msg_id = 0;
    uint32_t buf_len = 8;
    MQTTString topic_str = MQTTString_initializer;
    if (client == NULL || topic == NULL) return MQTT_ERR_INVALID_ARG;
    if (qos == 0 && client->state != MQTT_STATE_CONNECTED) {
        MQTT_PRINTF_ERROR_CODE(MQTT_ERR_INVALID_STATE);
        return MQTT_ERR_INVALID_STATE;
    }
    if (client->config->network.outbox_limit > 0 && qos > 0 && ms_mqtt_client_get_outbox_size(client) > client->config->network.outbox_limit) {
        MQTT_PRINTF_ERROR_CODE(MQTT_ERR_LIMIT);
        return MQTT_ERR_LIMIT;
    }

    buf_len += (strlen(topic) + 4);
    buf_len += len;
    if (buf_len > client->config->network.tx_buf_size) {
        ret = MQTT_ERR_MEM;
        goto ms_mqtt_client_publish_end;
    }

    buffer = (uint8_t *)hal_mem_alloc_large(buf_len);
    if (buffer == NULL) {
        ret = MQTT_ERR_MEM;
        goto ms_mqtt_client_publish_end;
    }

    if (qos > 0) {
        MS_MQTT_CLIENT_LOCK(client);
        msg_id = MS_MQTT_MSG_ID(client);
        MS_MQTT_CLIENT_UNLOCK(client);
    }
    topic_str.cstring = topic;
    slen = MQTTSerialize_publish(buffer, buf_len, 0, qos, retain, msg_id, topic_str, data, len);
    if (slen <= 0) {
        ret = MQTT_ERR_SERIAL;
        goto ms_mqtt_client_publish_end;
    }

    MS_MQTT_PRINTF("send publish, topic: %s, qos: %d, retain: %d, msg_id: %d.", topic, qos, retain, msg_id);
    MS_MQTT_PRINTF("publish data len: %d, timeout: %d.", len, client->config->network.timeout_ms);
    if (client->state == MQTT_STATE_CONNECTED) {
        ret = ms_network_send((ms_network_handle_t)(client->network_handle), buffer, slen, client->config->network.timeout_ms);
        if (ret == slen) ret = msg_id;
        else if (ret >= 0) {
            MS_MQTT_PRINTF("Actual send size: %d, expected send size: %d.", ret, slen);
            ret = MQTT_ERR_SIZE;
        }
    } else if (qos == 0) ret = MQTT_ERR_INVALID_STATE;
    else ret = msg_id;

    if (qos > 0) {
        MS_MQTT_CLIENT_LOCK(client);
        if (ms_mqtt_client_outbox_add(client, buffer, slen, msg_id, qos, PUBLISH) == NULL) {
            ret = MQTT_ERR_MEM;
            MS_MQTT_CLIENT_UNLOCK(client);
            goto ms_mqtt_client_publish_end;
        }
        if (ret == msg_id) outbox_set_pending(client->outbox, msg_id, TRANSMITTED);
        MS_MQTT_CLIENT_UNLOCK(client);
    }

ms_mqtt_client_publish_end:
    if (buffer) hal_mem_free(buffer);
    MQTT_PRINTF_ERROR_CODE(ret);
    return ret;
}

int ms_mqtt_client_enqueue(ms_mqtt_client_handle_t client, char *topic, uint8_t *data, int len, int qos, int retain)
{
    int slen = 0, ret = 0;
    uint8_t *buffer = NULL;
    uint16_t msg_id = 0;
    uint32_t buf_len = 8;
    MQTTString topic_str = MQTTString_initializer;
    if (client == NULL || topic == NULL) return MQTT_ERR_INVALID_ARG;
    if (client->config->network.outbox_limit > 0 && ms_mqtt_client_get_outbox_size(client) > client->config->network.outbox_limit) {
        MQTT_PRINTF_ERROR_CODE(MQTT_ERR_LIMIT);
        return MQTT_ERR_LIMIT;
    }

    buf_len += (strlen(topic) + 4);
    buf_len += len;
    if (buf_len > client->config->network.tx_buf_size) {
        ret = MQTT_ERR_MEM;
        goto ms_mqtt_client_enqueue_end;
    }

    buffer = (uint8_t *)hal_mem_alloc_large(buf_len);
    if (buffer == NULL) {
        ret = MQTT_ERR_MEM;
        goto ms_mqtt_client_enqueue_end;
    }

    if (qos > 0) {
        MS_MQTT_CLIENT_LOCK(client);
        msg_id = MS_MQTT_MSG_ID(client);
        MS_MQTT_CLIENT_UNLOCK(client);
    }
    topic_str.cstring = topic;
    slen = MQTTSerialize_publish(buffer, buf_len, 0, qos, retain, msg_id, topic_str, data, len);
    if (slen <= 0) {
        ret = MQTT_ERR_SERIAL;
        goto ms_mqtt_client_enqueue_end;
    }

    MS_MQTT_CLIENT_LOCK(client);
    if (ms_mqtt_client_outbox_add(client, buffer, slen, msg_id, qos, PUBLISH) == NULL) {
        ret = MQTT_ERR_MEM;
        MS_MQTT_CLIENT_UNLOCK(client);
        goto ms_mqtt_client_enqueue_end;
    }
    MS_MQTT_CLIENT_UNLOCK(client);
    ret = msg_id;

ms_mqtt_client_enqueue_end:
    if (buffer) hal_mem_free(buffer);
    MQTT_PRINTF_ERROR_CODE(ret);
    return ret;
}

int ms_mqtt_client_register_event(ms_mqtt_client_handle_t client, ms_mqtt_client_event_handler_t event_handler, void *user_arg)
{
    int i = 0, ret = MQTT_ERR_MEM;
    if (client == NULL || event_handler == NULL) return MQTT_ERR_INVALID_ARG;

    MS_MQTT_CLIENT_LOCK(client);
    for (i = 0; i < MS_MQTT_CLIENT_MAX_EVENT_FUNC_SIZE; i++) {
        if (client->event_handler_list[i] == NULL) {
            client->event_handler_list[i] = event_handler;
            client->event_user_data[i] = user_arg;
            ret = MQTT_ERR_OK;
        } else if (client->event_handler_list[i] == event_handler) {
            client->event_user_data[i] = user_arg;
            ret = MQTT_ERR_OK;
        }
        if (ret == MQTT_ERR_OK) break;
    }
    MS_MQTT_CLIENT_UNLOCK(client);

    MQTT_PRINTF_ERROR_CODE(ret);
    return ret;
}

int ms_mqtt_client_unregister_event(ms_mqtt_client_handle_t client, ms_mqtt_client_event_handler_t event_handler)
{
    int i = 0;
    if (client == NULL || event_handler == NULL) return MQTT_ERR_INVALID_ARG;

    MS_MQTT_CLIENT_LOCK(client);
    for (i = 0; i < MS_MQTT_CLIENT_MAX_EVENT_FUNC_SIZE; i++) {
        if (client->event_handler_list[i] == event_handler) {
            client->event_handler_list[i] = NULL;
            client->event_user_data[i] = NULL;
            break;
        }
    }
    MS_MQTT_CLIENT_UNLOCK(client);

    MQTT_PRINTF_ERROR_CODE(MQTT_ERR_OK);
    return MQTT_ERR_OK;
}

ms_mqtt_state_t ms_mqtt_client_get_state(ms_mqtt_client_handle_t client)
{
    if (client == NULL) return MQTT_STATE_MAX;
    return client->state;
}
