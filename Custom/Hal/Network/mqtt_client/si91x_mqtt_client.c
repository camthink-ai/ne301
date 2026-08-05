#include "Log/debug.h"
#include "sl_net.h"
#include "sl_utility.h"
#include "sl_constants.h"
#include "sl_net_dns.h"
#include "sl_mqtt_client.h"
#include "sl_net_netif.h"
#include "ms_network.h"
#include "si91x_mqtt_client.h"

#define SI91X_MQTT_CLIENT_LOCK()     do {\
    if (si91x_mqtt_lock == NULL) si91x_mqtt_lock = xSemaphoreCreateMutex();\
    if (si91x_mqtt_lock == NULL) return MQTT_ERR_MEM;\
    xSemaphoreTake(si91x_mqtt_lock, portMAX_DELAY);\
} while (0)
#define SI91X_MQTT_CLIENT_UNLOCK()   xSemaphoreGive(si91x_mqtt_lock)

#define SI91X_MQTT_CLIENT_FUNC_START(is_init)  do {\
    SI91X_MQTT_CLIENT_LOCK();\
    if ((si91x_mqtt_client == NULL && !is_init) || (si91x_mqtt_client != NULL && is_init)) {\
        SI91X_MQTT_CLIENT_UNLOCK();\
        return MQTT_ERR_INVALID_STATE;\
    }\
} while (0)
#define SI91X_MQTT_CLIENT_FUNC_END()    SI91X_MQTT_CLIENT_UNLOCK()

/// @brief [SI91X MQTT]CLIENT STRUCT
typedef struct
{
    /// @brief MQTT CLIENT
    sl_mqtt_client_t *sl_mqtt_client;
    /// @brief MQTT CLIENT CONFIG
    sl_mqtt_client_configuration_t *sl_mqtt_client_configuration;
    /// @brief MQTT BROKER CONFIG
    sl_mqtt_broker_t *sl_mqtt_broker;
    /// @brief MQTT LAST WILL MESSAGE
    sl_mqtt_client_last_will_message_t *sl_mqtt_client_last_will_message;
    /// @brief MQTT CLIENT CREDENTIALS
    sl_mqtt_client_credentials_t *sl_mqtt_client_credentials;
    /// @brief MQTT CLIENT EVENT HANDLER
    ms_mqtt_client_event_handler_t event_handler;
    /// @brief MQTT CLIENT USER ARG
    void *user_arg;
    /// @brief MQTT MESSAGE ID
    uint16_t msg_id;
} si91x_mqtt_client_t;

/// @brief MQTT CLIENT LOCK
static SemaphoreHandle_t si91x_mqtt_lock = NULL;
/// @brief [SI91X MQTT]CLIENT
static si91x_mqtt_client_t *si91x_mqtt_client = NULL;

bool parse_ipv4_to_bytes(const char *ip_string, uint8_t octets[4]) {
	if (ip_string == NULL || octets == NULL) {
		return false;
	}

	// Skip leading whitespace
	while (*ip_string && isspace((unsigned char)*ip_string)) {
		ip_string++;
	}

	uint32_t value = 0;
	int digits_in_part = 0;
	int octet_index = 0;

	for (const char *p = ip_string; ; ++p) {
		char ch = *p;

		if (ch >= '0' && ch <= '9') {
			// Build value, limit <= 255
			value = value * 10u + (uint32_t)(ch - '0');
			if (value > 255u) {
				return false;
			}
			digits_in_part++;
			// Each segment has at most 3 digits
			if (digits_in_part > 3) {
				return false;
			}
		} else if (ch == '.' || ch == '\0') {
			// Each segment must have at least one digit
			if (digits_in_part == 0) {
				return false;
			}
			// Write current segment
			if (octet_index >= 4) {
				return false; // Too many segments
			}
			octets[octet_index++] = (uint8_t)value;

			// Encounter terminator, check if exactly 4 segments
			if (ch == '\0') {
				break;
			}

			// Reset, prepare for next segment
			value = 0;
			digits_in_part = 0;
		} else if (isspace((unsigned char)ch)) {
			// Trailing whitespace allowed, but no non-whitespace characters after whitespace
			const char *q = p;
			while (*q && isspace((unsigned char)*q)) {
				q++;
			}
			if (*q != '\0') {
				return false; // Characters after whitespace
			}
			// Finish current segment (equivalent to encountering '\0')
			if (digits_in_part == 0) {
				return false;
			}
			if (octet_index >= 4) {
				return false;
			}
			octets[octet_index++] = (uint8_t)value;
			break;
		} else {
			// Invalid character
			return false;
		}
	}

	// Must be exactly 4 segments
	return (octet_index == 4);
}

static void si91x_mqtt_client_send_event(ms_mqtt_event_data_t *event_data)
{
    if (si91x_mqtt_client->event_handler != NULL) si91x_mqtt_client->event_handler(event_data, si91x_mqtt_client->user_arg);
}

static void si91x_mqtt_client_message_handler(void *client, sl_mqtt_client_message_t *message, void *context)
{
    ms_mqtt_event_data_t mqtt_event_data = {0};

    mqtt_event_data.event_id = MQTT_EVENT_DATA;
    mqtt_event_data.msg_id = message->packet_identifier;
    mqtt_event_data.dup = message->is_duplicate_message;
    mqtt_event_data.qos = message->qos_level;
    mqtt_event_data.retain = message->is_retained;
    mqtt_event_data.topic_len = message->topic_length;
    mqtt_event_data.topic = message->topic;
    mqtt_event_data.data_len = message->content_length;
    mqtt_event_data.data = message->content;

    si91x_mqtt_client_send_event(&mqtt_event_data);
}

static void si91x_mqtt_client_event_handler(void *client, sl_mqtt_client_event_t event, void *event_data, void *context)
{
    ms_mqtt_event_data_t mqtt_event_data = {0};

    switch (event) {
        case SL_MQTT_CLIENT_CONNECTED_EVENT:
            mqtt_event_data.event_id = MQTT_EVENT_CONNECTED;
            break;
        
        case SL_MQTT_CLIENT_MESSAGE_PUBLISHED_EVENT:
            mqtt_event_data.event_id = MQTT_EVENT_PUBLISHED;
            mqtt_event_data.topic_len = strlen((char *)context);
            mqtt_event_data.topic = (uint8_t *)context;
            break;

        case SL_MQTT_CLIENT_SUBSCRIBED_EVENT:
            mqtt_event_data.event_id = MQTT_EVENT_SUBSCRIBED;
            mqtt_event_data.topic_len = strlen((char *)context);
            mqtt_event_data.topic = (uint8_t *)context;
            break;
        
        case SL_MQTT_CLIENT_UNSUBSCRIBED_EVENT: 
            mqtt_event_data.event_id = MQTT_EVENT_UNSUBSCRIBED;
            mqtt_event_data.topic_len = strlen((char *)context);
            mqtt_event_data.topic = (uint8_t *)context;
            break;

        case SL_MQTT_CLIENT_DISCONNECTED_EVENT: 
            mqtt_event_data.event_id = MQTT_EVENT_DISCONNECTED;
            break;

        case SL_MQTT_CLIENT_ERROR_EVENT:
            mqtt_event_data.event_id = MQTT_EVENT_ERROR;
            mqtt_event_data.error_code = *(sl_mqtt_client_error_status_t *)event_data;
            break;

        default:
            mqtt_event_data.error_code = MQTT_ERR_UNKNOWN;
            break;
    }
    si91x_mqtt_client_send_event(&mqtt_event_data);
}

int si91x_mqtt_client_init(const ms_mqtt_config_t *config)
{
    int ret = 0;
    char *cert_data = NULL;
    uint32_t cert_len = 0;
    uint32_t credentials_size = 0, user_len = 0, pass_len = 0;
    sl_ip_address_t ip_address = {0};
    sl_status_t status = SL_STATUS_OK;
    if (config == NULL) return MQTT_ERR_INVALID_ARG;
    SI91X_MQTT_CLIENT_FUNC_START(true);

    if (sl_net_client_netif_state() != NETIF_STATE_UP && sl_net_netif_get_wakeup_mode() != WAKEUP_MODE_WIFI) {
        ret = MQTT_ERR_NETIF;
        goto si91x_mqtt_client_init_end;
    }
    si91x_mqtt_client = (si91x_mqtt_client_t *)hal_mem_alloc_large(sizeof(si91x_mqtt_client_t));
    if (si91x_mqtt_client == NULL) {
        ret = MQTT_ERR_MEM;
        goto si91x_mqtt_client_init_end;
    }
    memset(si91x_mqtt_client, 0, sizeof(si91x_mqtt_client_t));

    si91x_mqtt_client->sl_mqtt_client = (sl_mqtt_client_t *)hal_mem_alloc_large(sizeof(sl_mqtt_client_t));
    if (si91x_mqtt_client->sl_mqtt_client == NULL) {
        ret = MQTT_ERR_MEM;
        goto si91x_mqtt_client_init_end;
    }
    memset(si91x_mqtt_client->sl_mqtt_client, 0, sizeof(sl_mqtt_client_t));

    si91x_mqtt_client->sl_mqtt_client_configuration = (sl_mqtt_client_configuration_t *)hal_mem_alloc_large(sizeof(sl_mqtt_client_configuration_t));
    if (si91x_mqtt_client->sl_mqtt_client_configuration == NULL) {
        ret = MQTT_ERR_MEM;
        goto si91x_mqtt_client_init_end;
    }
    memset(si91x_mqtt_client->sl_mqtt_client_configuration, 0, sizeof(sl_mqtt_client_configuration_t));
    si91x_mqtt_client->sl_mqtt_client_configuration->credential_id = SL_NET_NO_CREDENTIAL_ID;

    si91x_mqtt_client->sl_mqtt_broker = (sl_mqtt_broker_t *)hal_mem_alloc_large(sizeof(sl_mqtt_broker_t));
    if (si91x_mqtt_client->sl_mqtt_broker == NULL) {
        ret = MQTT_ERR_MEM;
        goto si91x_mqtt_client_init_end;
    }
    memset(si91x_mqtt_client->sl_mqtt_broker, 0, sizeof(sl_mqtt_broker_t));

    si91x_mqtt_client->sl_mqtt_client_last_will_message = (sl_mqtt_client_last_will_message_t *)hal_mem_alloc_large(sizeof(sl_mqtt_client_last_will_message_t));
    if (si91x_mqtt_client->sl_mqtt_client_last_will_message == NULL) {
        ret = MQTT_ERR_MEM;
        goto si91x_mqtt_client_init_end;
    }
    memset(si91x_mqtt_client->sl_mqtt_client_last_will_message, 0, sizeof(sl_mqtt_client_last_will_message_t));

    // base config
    if (sl_net_netif_get_wakeup_mode() == WAKEUP_MODE_WIFI) {
        if (parse_ipv4_to_bytes(config->base.hostname, ip_address.ip.v4.bytes) == false) {
            status = sl_net_dns_resolve_hostname(config->base.hostname, config->network.timeout_ms, SL_NET_DNS_TYPE_IPV4, &ip_address);
            if (status != SL_STATUS_OK) {
                LOG_DRV_ERROR("[SI91X MQTT]DNS resolve hostname failed: 0x%08X\r\n", status);
                ret = MQTT_ERR_RESPONSE;
                goto si91x_mqtt_client_init_end;
            }
        } else {
            ip_address.type = SL_IPV4;
        }
    } else {
        ret = ms_network_dns_parse(config->base.hostname, ip_address.ip.v4.bytes);
        if (ret != 0) goto si91x_mqtt_client_init_end;
        ip_address.type = SL_IPV4;
    }
    memcpy(&(si91x_mqtt_client->sl_mqtt_broker->ip), &ip_address, sizeof(sl_ip_address_t));
    si91x_mqtt_client->sl_mqtt_broker->port = config->base.port;
    si91x_mqtt_client->sl_mqtt_broker->is_connection_encrypted = (config->authentication.ca_data != NULL || config->authentication.ca_path != NULL) ? true : false;
    si91x_mqtt_client->sl_mqtt_broker->connect_timeout = config->network.timeout_ms;
    si91x_mqtt_client->sl_mqtt_broker->keep_alive_interval = config->base.keepalive;
    si91x_mqtt_client->sl_mqtt_broker->keep_alive_retries = 3;
    // client config
    si91x_mqtt_client->sl_mqtt_client_configuration->auto_reconnect = (config->network.disable_auto_reconnect == 0) ? true : false;
    si91x_mqtt_client->sl_mqtt_client_configuration->retry_count = 255;
    si91x_mqtt_client->sl_mqtt_client_configuration->minimum_back_off_time = config->network.reconnect_interval_ms / 1000;
    si91x_mqtt_client->sl_mqtt_client_configuration->maximum_back_off_time = config->network.reconnect_interval_ms / 1000;
    si91x_mqtt_client->sl_mqtt_client_configuration->is_clean_session = (config->base.clean_session != 0) ? true : false;
    si91x_mqtt_client->sl_mqtt_client_configuration->mqt_version = (config->base.protocol_ver == 3) ? SL_MQTT_VERSION_3 : SL_MQTT_VERSION_3_1;
    si91x_mqtt_client->sl_mqtt_client_configuration->client_port = 10086;
    si91x_mqtt_client->sl_mqtt_client_configuration->client_id_length = strlen(config->base.client_id);
    si91x_mqtt_client->sl_mqtt_client_configuration->client_id = hal_mem_alloc_large(si91x_mqtt_client->sl_mqtt_client_configuration->client_id_length + 1);
    if (si91x_mqtt_client->sl_mqtt_client_configuration->client_id == NULL) {
        ret = MQTT_ERR_MEM;
        goto si91x_mqtt_client_init_end;
    }
    memcpy(si91x_mqtt_client->sl_mqtt_client_configuration->client_id, config->base.client_id, si91x_mqtt_client->sl_mqtt_client_configuration->client_id_length);
    if (config->authentication.ca_data != NULL || config->authentication.ca_path != NULL) {
        si91x_mqtt_client->sl_mqtt_client_configuration->tls_flags = SL_MQTT_TLS_ENABLE | SL_MQTT_TLS_TLSV_1_2 | SL_MQTT_TLS_CERT_INDEX_1;
    }
    // last will
    if (config->last_will.topic != NULL && config->last_will.msg != NULL) {
        si91x_mqtt_client->sl_mqtt_client_last_will_message->is_retained = (config->last_will.retain != 0) ? true : false;
        si91x_mqtt_client->sl_mqtt_client_last_will_message->will_qos_level = config->last_will.qos;
        if (si91x_mqtt_client->sl_mqtt_client_last_will_message->will_qos_level >= 2) si91x_mqtt_client->sl_mqtt_client_last_will_message->will_qos_level = 1; // max qos 1
        si91x_mqtt_client->sl_mqtt_client_last_will_message->will_topic_length = strlen(config->last_will.topic);
        si91x_mqtt_client->sl_mqtt_client_last_will_message->will_topic = hal_mem_alloc_large(si91x_mqtt_client->sl_mqtt_client_last_will_message->will_topic_length + 1);
        if (si91x_mqtt_client->sl_mqtt_client_last_will_message->will_topic == NULL) {
            ret = MQTT_ERR_MEM;
            goto si91x_mqtt_client_init_end;
        }
        memcpy(si91x_mqtt_client->sl_mqtt_client_last_will_message->will_topic, config->last_will.topic, si91x_mqtt_client->sl_mqtt_client_last_will_message->will_topic_length);
        si91x_mqtt_client->sl_mqtt_client_last_will_message->will_message_length = (config->last_will.msg_len == 0) ? strlen(config->last_will.msg) : config->last_will.msg_len;
        si91x_mqtt_client->sl_mqtt_client_last_will_message->will_message = hal_mem_alloc_large(si91x_mqtt_client->sl_mqtt_client_last_will_message->will_message_length + 1);
        if (si91x_mqtt_client->sl_mqtt_client_last_will_message->will_message == NULL) {
            ret = MQTT_ERR_MEM;
            goto si91x_mqtt_client_init_end;
        }
        memcpy(si91x_mqtt_client->sl_mqtt_client_last_will_message->will_message, config->last_will.msg, si91x_mqtt_client->sl_mqtt_client_last_will_message->will_message_length);
    }
    // credentials
    if (config->authentication.username != NULL) {
        user_len = strlen(config->authentication.username);
        if (config->authentication.password != NULL) {
            pass_len = strlen(config->authentication.password);
        }
        credentials_size = sizeof(sl_mqtt_client_credentials_t) + user_len + pass_len;
        si91x_mqtt_client->sl_mqtt_client_credentials = (sl_mqtt_client_credentials_t *)hal_mem_alloc_large(credentials_size);
        if (si91x_mqtt_client->sl_mqtt_client_credentials == NULL) {
            ret = MQTT_ERR_MEM;
            goto si91x_mqtt_client_init_end;
        }
        memset(si91x_mqtt_client->sl_mqtt_client_credentials, 0, credentials_size);

        si91x_mqtt_client->sl_mqtt_client_credentials->username_length = user_len;
        si91x_mqtt_client->sl_mqtt_client_credentials->password_length = pass_len;
        memcpy(si91x_mqtt_client->sl_mqtt_client_credentials->data, config->authentication.username, si91x_mqtt_client->sl_mqtt_client_credentials->username_length);
        si91x_mqtt_client->sl_mqtt_client_configuration->credential_id = SL_NET_MQTT_CLIENT_CREDENTIAL_ID(0);
        if (si91x_mqtt_client->sl_mqtt_client_credentials->password_length > 0) {
            memcpy(si91x_mqtt_client->sl_mqtt_client_credentials->data + si91x_mqtt_client->sl_mqtt_client_credentials->username_length, config->authentication.password, si91x_mqtt_client->sl_mqtt_client_credentials->password_length);
        }

        status = sl_net_set_credential(si91x_mqtt_client->sl_mqtt_client_configuration->credential_id, SL_NET_MQTT_CLIENT_CREDENTIAL, si91x_mqtt_client->sl_mqtt_client_credentials, credentials_size);
        if (status != SL_STATUS_OK) {
            LOG_DRV_ERROR("[SI91X MQTT]Set MQTT client credential failed: 0x%08X\r\n", status);
            ret = MQTT_ERR_RESPONSE;
            goto si91x_mqtt_client_init_end;
        }
    }

    // Configure SSL certificates
    if (config->authentication.ca_data != NULL || config->authentication.ca_path != NULL) {
        if (config->authentication.ca_path != NULL) {
            ret = ms_mqtt_client_get_cert_from_file(config->authentication.ca_path, (uint8_t **)&cert_data, &cert_len);
            if (ret != 0) goto si91x_mqtt_client_init_end;
        } else {
            cert_data = config->authentication.ca_data;
            cert_len = config->authentication.ca_len;
            if (cert_len == 0) cert_len = strlen(cert_data);
        }
        status = sl_net_set_credential(SL_NET_TLS_SERVER_CREDENTIAL_ID(1), SL_NET_SIGNING_CERTIFICATE, cert_data, cert_len);
        if (status != SL_STATUS_OK) {
            LOG_DRV_ERROR("[SI91X MQTT]Set CA certificate failed: 0x%08X\r\n", status);
            ret = MQTT_ERR_RESPONSE;
            goto si91x_mqtt_client_init_end;
        }
        if (config->authentication.ca_path != NULL) hal_mem_free(cert_data);
        cert_data = NULL;
        cert_len = 0;

        if ((config->authentication.client_cert_data != NULL || config->authentication.client_cert_path != NULL) && (config->authentication.client_key_data != NULL || config->authentication.client_key_path != NULL)) {
            if (config->authentication.client_cert_path != NULL) {
                ret = ms_mqtt_client_get_cert_from_file(config->authentication.client_cert_path, (uint8_t **)&cert_data, &cert_len);
                if (ret != 0) goto si91x_mqtt_client_init_end;
            } else {
                cert_data = config->authentication.client_cert_data;
                cert_len = config->authentication.client_cert_len;
                if (cert_len == 0) cert_len = strlen(cert_data);
            }
            status = sl_net_set_credential(SL_NET_TLS_CLIENT_CREDENTIAL_ID(1), SL_NET_CERTIFICATE, cert_data, cert_len);
            if (status != SL_STATUS_OK) {
                LOG_DRV_ERROR("[SI91X MQTT]Set client certificate failed: 0x%08X\r\n", status);
                ret = MQTT_ERR_RESPONSE;
                goto si91x_mqtt_client_init_end;
            }
            if (config->authentication.client_cert_path != NULL) hal_mem_free(cert_data);
            cert_data = NULL;
            cert_len = 0;
            
            if (config->authentication.client_key_path != NULL) {
                ret = ms_mqtt_client_get_cert_from_file(config->authentication.client_key_path, (uint8_t **)&cert_data, &cert_len);
                if (ret != 0) goto si91x_mqtt_client_init_end;
            } else {
                cert_data = config->authentication.client_key_data;
                cert_len = config->authentication.client_key_len;
                if (cert_len == 0) cert_len = strlen(cert_data);
            }
            status = sl_net_set_credential(SL_NET_TLS_CLIENT_CREDENTIAL_ID(1), SL_NET_PRIVATE_KEY, cert_data, cert_len);
            if (status != SL_STATUS_OK) {
                LOG_DRV_ERROR("[SI91X MQTT]Set client private key failed: 0x%08X\r\n", status);
                ret = MQTT_ERR_RESPONSE;
                goto si91x_mqtt_client_init_end;
            }
            if (config->authentication.client_key_path != NULL) hal_mem_free(cert_data);
            cert_data = NULL;
            cert_len = 0;
        }
    }

    status = sl_mqtt_client_init(si91x_mqtt_client->sl_mqtt_client, si91x_mqtt_client_event_handler);
    if (status != SL_STATUS_OK) {
        LOG_DRV_ERROR("[SI91X MQTT]client init failed: 0x%08X\r\n", status);
        ret = MQTT_ERR_RESPONSE;
        goto si91x_mqtt_client_init_end;
    }
    
si91x_mqtt_client_init_end:
    if (ret != 0) {
        if (si91x_mqtt_client != NULL) {
            if (si91x_mqtt_client->sl_mqtt_client != NULL) hal_mem_free(si91x_mqtt_client->sl_mqtt_client);
            if (si91x_mqtt_client->sl_mqtt_broker != NULL) {
                hal_mem_free(si91x_mqtt_client->sl_mqtt_broker);
            }
            if (si91x_mqtt_client->sl_mqtt_client_configuration != NULL) {
                if (si91x_mqtt_client->sl_mqtt_client_configuration->client_id != NULL) {
                    hal_mem_free(si91x_mqtt_client->sl_mqtt_client_configuration->client_id);
                }
                hal_mem_free(si91x_mqtt_client->sl_mqtt_client_configuration);
            }
            if (si91x_mqtt_client->sl_mqtt_client_last_will_message != NULL) {
                if (si91x_mqtt_client->sl_mqtt_client_last_will_message->will_topic != NULL) hal_mem_free(si91x_mqtt_client->sl_mqtt_client_last_will_message->will_topic);
                if (si91x_mqtt_client->sl_mqtt_client_last_will_message->will_message != NULL) hal_mem_free(si91x_mqtt_client->sl_mqtt_client_last_will_message->will_message);
                hal_mem_free(si91x_mqtt_client->sl_mqtt_client_last_will_message);
            }
            if (si91x_mqtt_client->sl_mqtt_client_credentials != NULL) hal_mem_free(si91x_mqtt_client->sl_mqtt_client_credentials);
            hal_mem_free(si91x_mqtt_client);
            si91x_mqtt_client = NULL;
        }
    }
    SI91X_MQTT_CLIENT_FUNC_END();
    return ret;
}

int si91x_mqtt_client_connnect(void)
{
    sl_status_t status = SL_STATUS_OK;

    SI91X_MQTT_CLIENT_FUNC_START(false);
    if (sl_net_client_netif_state() != NETIF_STATE_UP && sl_net_netif_get_wakeup_mode() != WAKEUP_MODE_WIFI) {
        SI91X_MQTT_CLIENT_FUNC_END();
        LOG_DRV_ERROR("[SI91X MQTT]connect refused: netif not up\r\n");
        return MQTT_ERR_NETIF;
    }
    status = sl_mqtt_client_connect(si91x_mqtt_client->sl_mqtt_client, si91x_mqtt_client->sl_mqtt_broker, si91x_mqtt_client->sl_mqtt_client_last_will_message, si91x_mqtt_client->sl_mqtt_client_configuration, 0);
    SI91X_MQTT_CLIENT_FUNC_END();
    if (status != SL_STATUS_IN_PROGRESS) {
        LOG_DRV_ERROR("[SI91X MQTT]client connect failed: 0x%08X\r\n", status);
        return MQTT_ERR_RESPONSE;
    }
    return MQTT_ERR_OK;
}

int si91x_mqtt_client_connnect_sync(uint32_t timeout_ms)
{
    sl_status_t status = SL_STATUS_OK;

    SI91X_MQTT_CLIENT_FUNC_START(false);
    if (sl_net_client_netif_state() != NETIF_STATE_UP && sl_net_netif_get_wakeup_mode() != WAKEUP_MODE_WIFI) {
        SI91X_MQTT_CLIENT_FUNC_END();
        LOG_DRV_ERROR("[SI91X MQTT]sync connect refused: netif not up\r\n");
        return MQTT_ERR_NETIF;
    }
    status = sl_mqtt_client_connect(si91x_mqtt_client->sl_mqtt_client, si91x_mqtt_client->sl_mqtt_broker, si91x_mqtt_client->sl_mqtt_client_last_will_message, si91x_mqtt_client->sl_mqtt_client_configuration, timeout_ms);
    SI91X_MQTT_CLIENT_FUNC_END();
    if (status != SL_STATUS_OK) {
        LOG_DRV_ERROR("[SI91X MQTT]client sync connect failed: 0x%08X\r\n", status);
        return MQTT_ERR_RESPONSE;
    }
    return MQTT_ERR_OK;
}

int si91x_mqtt_client_publish(const char *topic, const char *data, int data_len, int qos, int retain)
{
    sl_status_t status = SL_STATUS_OK;
    sl_mqtt_client_message_t message_to_be_published = {0};

    SI91X_MQTT_CLIENT_FUNC_START(false);
    message_to_be_published.topic = (uint8_t *)topic;
    message_to_be_published.topic_length = strlen(topic);
    message_to_be_published.content = (uint8_t *)data;
    message_to_be_published.content_length = data_len;
    message_to_be_published.is_retained = (retain != 0) ? true : false;
    message_to_be_published.is_duplicate_message = false;
    if (qos > 0) {
        if (qos >= 2) qos = 1;  // SI91X MQTT only support QoS 0 and 1
        message_to_be_published.qos_level = qos;
        message_to_be_published.packet_identifier = ++si91x_mqtt_client->msg_id;
        if (message_to_be_published.packet_identifier == 0) message_to_be_published.packet_identifier = 1;
    }
    status = sl_mqtt_client_publish(si91x_mqtt_client->sl_mqtt_client, &message_to_be_published, 0, (void *)topic);
    SI91X_MQTT_CLIENT_FUNC_END();
    if (status != SL_STATUS_IN_PROGRESS && status != SL_STATUS_OK) {
        LOG_DRV_ERROR("[SI91X MQTT]client publish failed: 0x%08X\r\n", status);
        return MQTT_ERR_RESPONSE;
    }
    return MQTT_ERR_OK;
}

int si91x_mqtt_client_publish_sync(const char *topic, const char *data, int data_len, int qos, int retain, uint32_t timeout_ms)
{
    sl_status_t status = SL_STATUS_OK;
    sl_mqtt_client_message_t message_to_be_published = {0};

    SI91X_MQTT_CLIENT_FUNC_START(false);
    message_to_be_published.topic = (uint8_t *)topic;
    message_to_be_published.topic_length = strlen(topic);
    message_to_be_published.content = (uint8_t *)data;
    message_to_be_published.content_length = data_len;
    message_to_be_published.is_retained = (retain != 0) ? true : false;
    message_to_be_published.is_duplicate_message = false;
    if (qos > 0) {
        if (qos >= 2) qos = 1;  // SI91X MQTT only support QoS 0 and 1
        message_to_be_published.qos_level = qos;
        message_to_be_published.packet_identifier = ++si91x_mqtt_client->msg_id;
        if (message_to_be_published.packet_identifier == 0) message_to_be_published.packet_identifier = 1;
    }
    status = sl_mqtt_client_publish(si91x_mqtt_client->sl_mqtt_client, &message_to_be_published, timeout_ms, (void *)topic);
    SI91X_MQTT_CLIENT_FUNC_END();
    if (status != SL_STATUS_OK) {
        LOG_DRV_ERROR("[SI91X MQTT]client sync publish failed: 0x%08X\r\n", status);
        return MQTT_ERR_RESPONSE;
    }
    return MQTT_ERR_OK;
}

int si91x_mqtt_client_subscribe(const char *topic, int qos)
{
    sl_status_t status = SL_STATUS_OK;

    SI91X_MQTT_CLIENT_FUNC_START(false);
    if (qos >= 2) qos = 1;  // SI91X MQTT only support QoS 0 and 1
    status = sl_mqtt_client_subscribe(si91x_mqtt_client->sl_mqtt_client, (const uint8_t *)topic, strlen(topic), qos, 0, si91x_mqtt_client_message_handler, (void *)topic);
    SI91X_MQTT_CLIENT_FUNC_END();
    if (status != SL_STATUS_IN_PROGRESS && status != SL_STATUS_OK) {
        LOG_DRV_ERROR("[SI91X MQTT]client subscribe failed: 0x%08X\r\n", status);
        return MQTT_ERR_RESPONSE;
    }
    return MQTT_ERR_OK;
}

int si91x_mqtt_client_subscribe_sync(const char *topic, int qos, uint32_t timeout_ms)
{
    sl_status_t status = SL_STATUS_OK;

    SI91X_MQTT_CLIENT_FUNC_START(false);
    if (qos >= 2) qos = 1;  // SI91X MQTT only support QoS 0 and 1
    status = sl_mqtt_client_subscribe(si91x_mqtt_client->sl_mqtt_client, (const uint8_t *)topic, strlen(topic), qos, timeout_ms, si91x_mqtt_client_message_handler, (void *)topic);
    SI91X_MQTT_CLIENT_FUNC_END();
    if (status != SL_STATUS_OK) {
        LOG_DRV_ERROR("[SI91X MQTT]client sync subscribe failed: 0x%08X\r\n", status);
        return MQTT_ERR_RESPONSE;
    }
    return MQTT_ERR_OK;
}

int si91x_mqtt_client_unsubscribe(const char *topic)
{
    sl_status_t status = SL_STATUS_OK;
    
    SI91X_MQTT_CLIENT_FUNC_START(false);        
    status = sl_mqtt_client_unsubscribe(si91x_mqtt_client->sl_mqtt_client, (const uint8_t *)topic, strlen(topic), 0, (void *)topic);
    SI91X_MQTT_CLIENT_FUNC_END();
    if (status != SL_STATUS_IN_PROGRESS && status != SL_STATUS_OK) {
        LOG_DRV_ERROR("[SI91X MQTT]client unsubscribe failed: 0x%08X\r\n", status);
        return MQTT_ERR_RESPONSE;
    }
    return MQTT_ERR_OK;
}

int si91x_mqtt_client_unsubscribe_sync(const char *topic, uint32_t timeout_ms)
{
    sl_status_t status = SL_STATUS_OK;
    
    SI91X_MQTT_CLIENT_FUNC_START(false);        
    status = sl_mqtt_client_unsubscribe(si91x_mqtt_client->sl_mqtt_client, (const uint8_t *)topic, strlen(topic), timeout_ms, (void *)topic);
    SI91X_MQTT_CLIENT_FUNC_END();
    if (status != SL_STATUS_OK) {
        LOG_DRV_ERROR("[SI91X MQTT]client sync unsubscribe failed: 0x%08X\r\n", status);
        return MQTT_ERR_RESPONSE;
    }
    return MQTT_ERR_OK;
}

int si91x_mqtt_client_disconnect(void)
{
    int ret = MQTT_ERR_OK;
    sl_status_t status = SL_STATUS_OK;

    SI91X_MQTT_CLIENT_FUNC_START(false);
    status = sl_mqtt_client_disconnect(si91x_mqtt_client->sl_mqtt_client, 0);
    if (status != SL_STATUS_IN_PROGRESS && status != SL_STATUS_OK) {
        LOG_DRV_ERROR("[SI91X MQTT]client disconnect failed: 0x%08X\r\n", status);
        ret = MQTT_ERR_RESPONSE;
    }
    SI91X_MQTT_CLIENT_FUNC_END();
    return ret;
}

int si91x_mqtt_client_disconnect_sync(uint32_t timeout_ms)
{
    sl_status_t status = SL_STATUS_OK;

    SI91X_MQTT_CLIENT_FUNC_START(false);
    status = sl_mqtt_client_disconnect(si91x_mqtt_client->sl_mqtt_client, timeout_ms);
    SI91X_MQTT_CLIENT_FUNC_END();
    if (status != SL_STATUS_OK) {
        LOG_DRV_ERROR("[SI91X MQTT]client sync disconnect failed: 0x%08X\r\n", status);
        return MQTT_ERR_RESPONSE;
    }
    return MQTT_ERR_OK;
}

int si91x_mqtt_client_deinit(void)
{
    SI91X_MQTT_CLIENT_FUNC_START(false);
    if (si91x_mqtt_client->sl_mqtt_client != NULL) {
        sl_mqtt_client_disconnect(si91x_mqtt_client->sl_mqtt_client, 5000);
        sl_mqtt_client_deinit(si91x_mqtt_client->sl_mqtt_client);
        hal_mem_free(si91x_mqtt_client->sl_mqtt_client);
    }
    if (si91x_mqtt_client->sl_mqtt_broker != NULL) hal_mem_free(si91x_mqtt_client->sl_mqtt_broker);
    if (si91x_mqtt_client->sl_mqtt_client_configuration != NULL) {
        if (si91x_mqtt_client->sl_mqtt_client_configuration->client_id != NULL) {
            hal_mem_free(si91x_mqtt_client->sl_mqtt_client_configuration->client_id);
        }
        hal_mem_free(si91x_mqtt_client->sl_mqtt_client_configuration);
    }
    if (si91x_mqtt_client->sl_mqtt_client_last_will_message != NULL) {
        if (si91x_mqtt_client->sl_mqtt_client_last_will_message->will_topic != NULL) hal_mem_free(si91x_mqtt_client->sl_mqtt_client_last_will_message->will_topic);
        if (si91x_mqtt_client->sl_mqtt_client_last_will_message->will_message != NULL) hal_mem_free(si91x_mqtt_client->sl_mqtt_client_last_will_message->will_message);
        hal_mem_free(si91x_mqtt_client->sl_mqtt_client_last_will_message);
    }
    if (si91x_mqtt_client->sl_mqtt_client_credentials != NULL) hal_mem_free(si91x_mqtt_client->sl_mqtt_client_credentials);
    hal_mem_free(si91x_mqtt_client);
    si91x_mqtt_client = NULL;
    SI91X_MQTT_CLIENT_FUNC_END();
    return MQTT_ERR_OK;
}

int si91x_mqtt_client_register_event(ms_mqtt_client_event_handler_t event_handler, void *user_arg)
{
    SI91X_MQTT_CLIENT_FUNC_START(false);
    si91x_mqtt_client->event_handler = event_handler;
    si91x_mqtt_client->user_arg = user_arg;
    SI91X_MQTT_CLIENT_FUNC_END();
    return MQTT_ERR_OK;
}

int si91x_mqtt_client_unregister_event(ms_mqtt_client_event_handler_t event_handler)
{
    SI91X_MQTT_CLIENT_FUNC_START(false);
    if (si91x_mqtt_client->event_handler != event_handler) {
        SI91X_MQTT_CLIENT_FUNC_END();
        return MQTT_ERR_INVALID_ARG;
    }
    si91x_mqtt_client->event_handler = NULL;
    si91x_mqtt_client->user_arg = NULL;
    SI91X_MQTT_CLIENT_FUNC_END();
    return MQTT_ERR_OK;
}

ms_mqtt_state_t si91x_mqtt_client_get_state(void)
{
    ms_mqtt_state_t state = MQTT_STATE_MAX;

    if (si91x_mqtt_lock == NULL) si91x_mqtt_lock = xSemaphoreCreateMutex(); 
    if (si91x_mqtt_lock == NULL) return MQTT_STATE_STOPPED; 
    xSemaphoreTake(si91x_mqtt_lock, portMAX_DELAY);
    if (si91x_mqtt_client == NULL) {
        xSemaphoreGive(si91x_mqtt_lock);
        return MQTT_STATE_STOPPED;
    }
    if (si91x_mqtt_client->sl_mqtt_client != NULL) {
        switch (si91x_mqtt_client->sl_mqtt_client->state) {
            case SL_MQTT_CLIENT_TA_INIT:
                state = MQTT_STATE_STARTING;
                break;
            case SL_MQTT_CLIENT_CONNECTED:
                state = MQTT_STATE_CONNECTED;
                break;
            default:
                state = MQTT_STATE_DISCONNECTED;
                break;
        }
    }
    xSemaphoreGive(si91x_mqtt_lock);
    return state;
}
