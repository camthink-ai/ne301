#include <string.h>
#include "ms_modem_at.h"

/// @brief Parse response content after AT test command
/// @param handle MODEM module handle
/// @param res AT send data error code
/// @param rsp_list Response list
/// @param rsp_num Number
/// @param user_data User data (pointer to uint8_t indicating whether echo is enabled)
/// @return Error code
static int paser_at_test_rsp_handler(void *_handle_, int res, char **rsp_list, uint16_t rsp_num, void *user_data)
{
    int i = 0;
    uint8_t *is_ate1 = NULL;
    char *back_rsp_data = NULL;
    modem_at_handle_t *handle = (modem_at_handle_t *)_handle_;

    is_ate1 = (uint8_t *)user_data;
    if (is_ate1) *is_ate1 = 0;
    if (rsp_num > 0) {
        if (strstr(rsp_list[0], "OK")) {
            res = MODEM_OK;
            // Return remaining responses to response queue (without error checking)
            for (i = 1; i < rsp_num; i++) {
                MODEM_LOGD("BACK RSP: %s", rsp_list[i]);
                back_rsp_data = (char *)pvPortMalloc(strlen(rsp_list[i]) + 1);
                if (back_rsp_data != NULL) {
                    strcpy(back_rsp_data, rsp_list[1]);
                    if (xQueueSendToFront(handle->rsp_queue, &back_rsp_data, 0) == pdFAIL) {
                        vPortFree(back_rsp_data);
                    }
                }
            }
        } else /*if (strstr(rsp_list[0], "AT"))*/ {
            if (is_ate1) *is_ate1 = 1;
            if (rsp_num > 1) {
                if (strstr(rsp_list[1], "OK")) res = MODEM_OK;
                else res = MODEM_ERR_FAILED;
            }
        }// else res = MODEM_ERR_FAILED;
    }
    return res;
}

/// @brief Parse response content after AT wait response command
/// @param handle MODEM module handle
/// @param res AT send data error code
/// @param rsp_list Response list
/// @param rsp_num Number
/// @param user_data User data (pointer to char ** response list)
/// @return Error code or number of response lines
static int paser_at_wait_rsp_handler(void *_handle_, int res, char **rsp_list, uint16_t rsp_num, void *user_data)
{
    int i = 0;
    char **wait_rsp_list = (char **)user_data;
    // modem_at_handle_t *handle = (modem_at_handle_t *)_handle_;
    if (res != MODEM_OK && rsp_num == 0) return res;

    if (wait_rsp_list != NULL && rsp_list != NULL) {
        for (i = 0; i < rsp_num; i++) {
            if (wait_rsp_list[i] != NULL && rsp_list[i] != NULL) {
                strcpy(wait_rsp_list[i], rsp_list[i]);
            }
        }
    }
    return (int)rsp_num;
}

/// @brief Reset response data queue
/// @param queue Data queue
static void reset_rsp_data_queue(QueueHandle_t queue)
{
    char *rsp_data = NULL;
    
    while (xQueueReceive(queue, &rsp_data, 0) == pdPASS) {
        if (rsp_data) {
            MODEM_LOGD("RSP DEL: %s", rsp_data);
            vPortFree(rsp_data);
        }
    }
}
/// @brief Deinitialize MODEM module
/// @param handle MODEM module handle
void modem_at_deinit(modem_at_handle_t *handle)
{
    handle->state = MODEM_AT_STATE_UNINIT;
    memset(&handle->rx_parser, 0x00, sizeof(handle->rx_parser));

    if (handle->rsp_queue != NULL) {
        reset_rsp_data_queue(handle->rsp_queue);
        vQueueDelete(handle->rsp_queue);
        handle->rsp_queue = NULL;
    }

    handle->uart_tx_func = NULL;
    if (handle->uart_tx_mutex != NULL) {
        vSemaphoreDelete(handle->uart_tx_mutex);
        handle->uart_tx_mutex = NULL;
    }
}
/// @brief Initialize MODEM module
/// @param handle MODEM module handle
/// @param uart_tx_func Low-level UART send function
/// @return Error code
int modem_at_init(modem_at_handle_t *handle, at_uart_tx_func_t uart_tx_func)
{
    int recode = MODEM_OK;
    if (uart_tx_func == NULL) return MODEM_ERR_INVALID_ARG;
    if (handle->state >= MODEM_AT_STATE_INIT) return MODEM_ERR_INVALID_STATE;

    memset(&handle->rx_parser, 0x00, sizeof(handle->rx_parser));
    
    handle->rsp_queue = xQueueCreate(MODEM_AT_RSP_DATA_QUEUE_DEPTH, sizeof(char *));
    if (handle->rsp_queue == NULL) {
        recode = MODEM_ERR_MEM;
        goto modem_at_init_failed;
    }

    handle->uart_tx_func = uart_tx_func;
    handle->uart_tx_mutex = xSemaphoreCreateMutex();
    if (handle->uart_tx_mutex == NULL) {
        recode = MODEM_ERR_MEM;
        goto modem_at_init_failed;
    }

    handle->state = MODEM_AT_STATE_INIT;
    return MODEM_OK;

modem_at_init_failed:
    modem_at_deinit(handle);
    return recode;
}
/// @brief MODEM module execute AT command (with configuration options)
/// @param handle MODEM module handle
/// @param cmd_item AT command
/// @param is_lock Whether to take mutex lock
/// @param is_release_lock Whether to release mutex lock
/// @return Error code
int modem_at_cmd_exec_with_opt(modem_at_handle_t *handle, const at_cmd_item_t *cmd_item, uint8_t is_lock, uint8_t is_release_lock)
{
    int i = 0, recode = MODEM_OK;
    char *rsp_list[MODEM_AT_RSP_MAX_LINE_NUM] = {0};
    char *exp_rsp_content = NULL, *exp_rsp_end_ptr = NULL;
    char *or_flag_ptr = NULL, *r_ptr = NULL;
    size_t exp_rsp_len = 0;
    uint16_t send_len = 0, rsp_num = 0, retry_times = 0;
    if (handle == NULL || cmd_item == NULL || cmd_item->cmd == NULL) return MODEM_ERR_INVALID_ARG;
    if (handle->state < MODEM_AT_STATE_INIT) return MODEM_ERR_INVALID_STATE;
    
    if (is_lock == 0 || xSemaphoreTake(handle->uart_tx_mutex, pdMS_TO_TICKS(MODEM_AT_TX_MUTEX_TAKE_TIMEOUT)) == pdPASS) {
        // Calculate send length before sending
        if (cmd_item->cmd_len == 0) send_len = strlen(cmd_item->cmd);
        else send_len = cmd_item->cmd_len;
        do {
            // Reset response queue
            reset_rsp_data_queue(handle->rsp_queue);
            // Send
            MODEM_LOGD("TX => (%d): %-32s", send_len, cmd_item->cmd);
            recode = handle->uart_tx_func((const uint8_t *)cmd_item->cmd, send_len, MODEM_AT_TX_TIMEOUT_DEFAULT);
            if (recode != send_len) {
                MODEM_LOGE("Uart send failed(recode = %d)!", recode);
                if (recode < 0) recode = MODEM_ERR_UART_FAILED;
                else if (recode == 0) recode = MODEM_ERR_TIMEOUT;
                else recode = MODEM_ERR_INVALID_SIZE;
            } else recode = MODEM_OK;
            // Check if need to wait for response
            if (recode == MODEM_OK && cmd_item->expect_rsp_line > 0) {
                while (rsp_num < cmd_item->expect_rsp_line && (xQueueReceive(handle->rsp_queue, &rsp_list[rsp_num], pdMS_TO_TICKS(cmd_item->timeout_ms)) == pdPASS)) {
                    if (handle->is_filter_echo) {
                        if (strstr(cmd_item->cmd, "\r\n") != NULL && send_len > 2 && strncmp(rsp_list[rsp_num], cmd_item->cmd, send_len - 2) == 0) {
                            MODEM_LOGD("ECHO SKIP: %s", rsp_list[rsp_num]);
                            continue; // Skip echo command
                        }
                    }
                    MODEM_LOGD("RX <= (%d): %s", strlen(rsp_list[rsp_num]), rsp_list[rsp_num]);
                    rsp_num++;
                    if (rsp_num == 1 && cmd_item->expect_rsp_line > 1 && strstr(rsp_list[0], "+CME ERROR:")) {
                        MODEM_LOGE("CME ERROR: %s", rsp_list[0]);
                        recode = MODEM_ERR_FAILED;
                        break;
                    }
                }
                // Confirm response content
                if (rsp_num < cmd_item->expect_rsp_line) {
                    if (recode == MODEM_OK) recode = MODEM_ERR_TIMEOUT;
                } else {
                    recode = MODEM_OK;
                    for (i = 0; i < rsp_num; i++) {
                        if (cmd_item->expect_rsp[i] != NULL) {
                            or_flag_ptr = strchr(cmd_item->expect_rsp[i], '|');
                            if (or_flag_ptr != NULL && or_flag_ptr > cmd_item->expect_rsp[i]) {
                                recode = MODEM_ERR_FAILED;
                                r_ptr = cmd_item->expect_rsp[i];
                                exp_rsp_end_ptr = cmd_item->expect_rsp[i] + strlen(cmd_item->expect_rsp[i]);
                                do {
                                    exp_rsp_len = or_flag_ptr - r_ptr;
                                    exp_rsp_content = (char *)pvPortMalloc(exp_rsp_len + 1);
                                    if (exp_rsp_content == NULL) {
                                        recode = MODEM_ERR_MEM;
                                        break;
                                    }
                                    memcpy(exp_rsp_content, r_ptr, exp_rsp_len);
                                    exp_rsp_content[exp_rsp_len] = '\0';
                                    if (strstr(rsp_list[i], exp_rsp_content)) {
                                        vPortFree(exp_rsp_content);
                                        recode = MODEM_OK;
                                        break;
                                    } else {
                                        vPortFree(exp_rsp_content);
                                        r_ptr = or_flag_ptr + 1;
                                        if (r_ptr >= exp_rsp_end_ptr) break;
                                        or_flag_ptr = strchr(r_ptr, '|');
                                        if (or_flag_ptr == NULL) or_flag_ptr = exp_rsp_end_ptr;
                                    }
                                } while (or_flag_ptr > r_ptr);
                            } else if (strstr(rsp_list[i], cmd_item->expect_rsp[i]) == NULL) {
                                recode = MODEM_ERR_FAILED;
                            }
                            if (recode != MODEM_OK) {
                                MODEM_LOGE("CHECK RSP%d: \"%s\" not in \"%s\"", i, cmd_item->expect_rsp[i], rsp_list[i]);
                                break;
                            }
                        } else {
                            // MODEM_LOGD("NO CHECK(%d): \"%s\"", i, rsp_list[i]);
                        }
                    }
                }
            }
        } while ((retry_times++ < MODEM_AT_CMD_RETRY_TIME) && (rsp_num == 0) && (cmd_item->expect_rsp_line > 0));
    #if MODEM_AT_CMD_INTERVAL_DELAY > 0
        // Delay between commands
        osDelay(MODEM_AT_CMD_INTERVAL_DELAY);
    #endif
        // Release mutex
        if (is_release_lock) xSemaphoreGive(handle->uart_tx_mutex);
    } else recode = MODEM_ERR_MUTEX;
    // Check if callback function is defined, execute if exists
    if (cmd_item->handler != NULL) recode = cmd_item->handler(handle, recode, rsp_list, rsp_num, cmd_item->user_data);
    // Release resources
    for (i = 0; i < rsp_num; i++) if (rsp_list[i]) vPortFree(rsp_list[i]);
    return recode;
}
/// @brief MODEM module execute AT command
/// @param handle MODEM module handle
/// @param cmd_item AT command
/// @return Error code
int modem_at_cmd_exec(modem_at_handle_t *handle, const at_cmd_item_t *cmd_item)
{
    return modem_at_cmd_exec_with_opt(handle, cmd_item, 1, 1);
}
/// @brief MODEM module execute AT command list (not recommended)
/// @param handle MODEM module handle
/// @param cmd_list AT command list
/// @param cmd_num Number of commands
/// @return Error code
int modem_at_cmd_list_exec(modem_at_handle_t *handle, const at_cmd_item_t *cmd_list, uint16_t cmd_num)
{
    int i = 0, recode = MODEM_OK, res = MODEM_OK;
    if (cmd_list == NULL || cmd_num == 0) return MODEM_ERR_INVALID_ARG;
    if (handle->state < MODEM_AT_STATE_INIT) return MODEM_ERR_INVALID_STATE;

    for (i = 0; i < cmd_num; i++) {
        res = modem_at_cmd_exec(handle, &cmd_list[i]);
        if (res != MODEM_OK) MODEM_LOGE("modem_at_cmd_list_exec(%d) failed(recode = %d)!", i, res);
        recode |= res;
    }
    return recode;
}
/// @brief URC data processing and filtering
/// @param handle MODEM module handle
/// @param urc_data URC data
/// @return MODEM_OK: Processed/filtered, MODEM_ERR_NOT_SUPPORT: Not urc_data, others: Error code
int modem_at_urc_data_filter(modem_at_handle_t *handle, const char *urc_data)
{   
    if (strcmp("RDY", urc_data) == 0) return MODEM_OK;
    return MODEM_ERR_NOT_SUPPORT;
}
/// @brief UART receive data processing function definition
/// @param handle MODEM module handle
/// @param p_data Received data
/// @param len Data length
/// @param timeout Processing timeout
/// @return Error code
int modem_at_rx_deal_handler(modem_at_handle_t *handle, const uint8_t *p_data, uint16_t len, uint32_t timeout_ms)
{
    int recode = MODEM_OK, res = MODEM_OK;
    char *rsp_data = NULL;
    if (p_data == NULL || len == 0) return MODEM_ERR_INVALID_ARG;
    if (handle->state < MODEM_AT_STATE_INIT) return MODEM_ERR_INVALID_STATE;

    do {
        if (*p_data == '\r') handle->rx_parser.cr_flag = 1;
        else {
            if (*p_data == '\n') {
                if (handle->rx_parser.cr_flag) {      // Received newline character (\r\n)
                    if (handle->rx_parser.rsp_len > 0) {
                        handle->rx_parser.rsp_buf[handle->rx_parser.rsp_len] = '\0';
                        res = modem_at_urc_data_filter(handle, (const char *)handle->rx_parser.rsp_buf);
                        if (res == MODEM_ERR_NOT_SUPPORT) {
                            rsp_data = (char *)pvPortMalloc(handle->rx_parser.rsp_len + 1);
                            if (rsp_data != NULL) {
                                memcpy(rsp_data, handle->rx_parser.rsp_buf, handle->rx_parser.rsp_len + 1);
                                if (xQueueSend(handle->rsp_queue, &rsp_data, pdMS_TO_TICKS(timeout_ms)) == pdFAIL) {
                                    vPortFree(rsp_data);
                                    recode |= MODEM_ERR_FAILED;
                                }
                            } else recode |= MODEM_ERR_MEM;
                        } else if (res != MODEM_OK) recode |= res;
                        handle->rx_parser.rsp_len = 0;
                    }
                } else {                                // Not a complete newline (\r\n), save to receive buffer
                    if (handle->rx_parser.rsp_len < (MODEM_AT_RSP_LEN_MAXIMUM - 1)) {
                        handle->rx_parser.rsp_buf[handle->rx_parser.rsp_len] = *p_data;
                        handle->rx_parser.rsp_len++;
                    } else recode |= MODEM_ERR_INVALID_SIZE;
                }
            } else {
                if (handle->rx_parser.cr_flag) {
                    if (handle->rx_parser.rsp_len < (MODEM_AT_RSP_LEN_MAXIMUM - 1)) {
                        handle->rx_parser.rsp_buf[handle->rx_parser.rsp_len] = '\r';
                        handle->rx_parser.rsp_len++;
                    } else recode |= MODEM_ERR_INVALID_SIZE;
                }
                if (handle->rx_parser.rsp_len < (MODEM_AT_RSP_LEN_MAXIMUM - 1)) {
                    handle->rx_parser.rsp_buf[handle->rx_parser.rsp_len] = *p_data;
                    handle->rx_parser.rsp_len++;
                } else recode |= MODEM_ERR_INVALID_SIZE;
            }
            handle->rx_parser.cr_flag = 0;
        }
        p_data++;
		len--;
    } while (len > 0);

    return recode;
}

/// @brief Test AT command and determine if echo is enabled
/// @param handle MODEM module handle
/// @param is_ate1 Whether echo is enabled (NULL means no need to check)
/// @param timeout_ms Timeout
/// @return Error code
int modem_at_test(modem_at_handle_t *handle, uint8_t *is_ate1, uint32_t timeout_ms)
{
    at_cmd_item_t cmd_item = {
        .cmd = "AT\r\n",
		.cmd_len = 0,
        .timeout_ms = timeout_ms,
        .expect_rsp_line = ((is_ate1 == NULL) ? 1 : 2),
		.expect_rsp = {NULL, NULL},
		.handler = paser_at_test_rsp_handler,
        .user_data = (void *)is_ate1
    };

    return modem_at_cmd_exec(handle, &cmd_item);
}

int modem_at_set_echo_filter(modem_at_handle_t *handle, uint8_t is_filter)
{
    handle->is_filter_echo = is_filter ? 1 : 0;
    MODEM_LOGD("Echo filter: %d.", handle->is_filter_echo);
    return MODEM_OK;
}

int modem_at_cmd_wait_ok(modem_at_handle_t *handle, const char *cmd, uint32_t timeout_ms)
{
    at_cmd_item_t cmd_item = {
        .cmd = (char *)cmd,
        .cmd_len = 0,
        .timeout_ms = timeout_ms,
        .expect_rsp_line = 1,
        .expect_rsp = {"OK", NULL},
        .handler = NULL,
    };
    return modem_at_cmd_exec(handle, &cmd_item);
}

int modem_at_cmd_wait_str(modem_at_handle_t *handle, const char *cmd, const char *rsp_str, uint32_t timeout_ms)
{
    at_cmd_item_t cmd_item = {
        .cmd = (char *)cmd,
        .cmd_len = 0,
        .timeout_ms = timeout_ms,
        .expect_rsp_line = 1,
        .expect_rsp = {(char *)rsp_str, NULL},
        .handler = NULL,
    };
    return modem_at_cmd_exec(handle, &cmd_item);
}

int modem_at_cmd_wait_rsp(modem_at_handle_t *handle, const char *cmd, char **rsp_list, uint16_t rsp_num, uint32_t timeout_ms)
{
    at_cmd_item_t cmd_item = {
        .cmd = (char *)cmd,
        .cmd_len = 0,
        .timeout_ms = timeout_ms,
        .expect_rsp_line = rsp_num,
        .expect_rsp = {0},
        .handler = paser_at_wait_rsp_handler,
        .user_data = (void *)rsp_list
    };
    return modem_at_cmd_exec(handle, &cmd_item);
}