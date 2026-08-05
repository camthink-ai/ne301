#include "ms_modem.h"
#include "ms_modem_at.h"
#include "ms_modem_at_cmd.h"
#include "common_utils.h"
#include "cmsis_os2.h"
#include "Log/debug.h"
#include "Hal/mem.h"
#include "usart.h"
#include "pwr.h"

/// @brief UART related configuration
#define MODULE_DEAL_MAX_TIME_MS         (10)        // Maximum processing time for module
#define MODEM_NET_RECV_BUF_SIZE         (1536)      // Receive buffer size
#define MODEM_NET_RECV_BUF_NUM          (16)        // Number of receive buffers
#define MODEM_NET_RECV_QUEUE_NUM        (128)       // Number of receive queues
#define MODEM_NET_SEND_BUF_SIZE         (1536)      // Send buffer size
#define MODEM_NET_SEND_BUF_NUM          (0)         // Number of send buffers
/// @brief MODEM UART baud rate list
static const uint32_t modem_baud_rate_list[] = {115200, 230400, 460800, 921600};
/// @brief Send completion semaphore
static SemaphoreHandle_t u7_send_semphr = NULL;
/// @brief Receive buffer
static uint8_t modem_rbufs[MODEM_NET_RECV_BUF_NUM][MODEM_NET_RECV_BUF_SIZE] ALIGN_32 UNCACHED = {0};
/// @brief Receive buffer start pointer
#define MODEM_NET_RECV_BUF_START_PTR                &modem_rbufs[0][0]
/// @brief Receive buffer end pointer
#define MODEM_NET_RECV_BUF_END_PTR                  (&modem_rbufs[MODEM_NET_RECV_BUF_NUM - 1][MODEM_NET_RECV_BUF_SIZE - 1])
/// @brief Receive buffer max pointer
#define MODEM_NET_RECV_BUF_MAX_LOAD_PTR             (&modem_rbufs[MODEM_NET_RECV_BUF_NUM - 1][0])
/// @brief Receive buffer max length
#define MODEM_NET_RECV_BUF_MAX_LOAD_LEN             (MODEM_NET_RECV_BUF_SIZE * (MODEM_NET_RECV_BUF_NUM - 1))
/// @brief Receive buffer load length
static volatile uint32_t modem_rbuf_load_len UNCACHED = 0, modem_now_baudrate = 0;
/// @brief Receive data struct
typedef struct 
{
    uint8_t *buf;
    int len;
} ms_modem_rdata_t;
/// @brief Receive queue
static QueueHandle_t u7_recv_queue = NULL;
/// @brief Receive pointer lock
static SemaphoreHandle_t recv_mutex = NULL;
/// @brief Send lock
static SemaphoreHandle_t send_mutex = NULL;
/// @brief PPP receive callback function
static modem_net_ppp_callback_t ppp_recv_callback = NULL;
/// @brief Task handle
osThreadId_t modem_rx_task_handle = NULL, modem_tx_task_handle = NULL;
/// @brief Task stack size
static uint8_t modem_rx_tread_stack[MODEM_RX_TASK_STACK_SIZE] ALIGN_32 IN_PSRAM = {0};
/// @brief Task attributes
const osThreadAttr_t modemRxTask_attributes = {
    .name = "modemRxTask",
    .priority = MODEM_RX_TASK_PRIORITY,
    .stack_mem = modem_rx_tread_stack,
    .stack_size = sizeof(modem_rx_tread_stack),
};
/// @brief MODEM state
modem_state_t modem_state = MODEM_STATE_UNINIT;
/// @brief MODEM AT handle
modem_at_handle_t modem_at_handle = {0};
/// @brief MODEM configuration parameters
modem_config_t g_modem_config = {0};

/// @brief Send completion interrupt function
void HAL_UART7_TxCpltCallback(UART_HandleTypeDef *huart)
{
    xSemaphoreGiveFromISR(u7_send_semphr, NULL);
}

static ms_modem_rdata_t isr_rdata UNCACHED = {0};
static uint8_t *isr_new_rbuf UNCACHED = NULL, *isr_old_rbuf UNCACHED = NULL;
void HAL_UART7_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->RxEventType == HAL_UART_RXEVENT_HT) return;

    isr_old_rbuf = huart->pRxBuffPtr;
    isr_new_rbuf = isr_old_rbuf + Size;
    if (isr_new_rbuf > MODEM_NET_RECV_BUF_MAX_LOAD_PTR) isr_new_rbuf = MODEM_NET_RECV_BUF_START_PTR;
    if (modem_rbuf_load_len < MODEM_NET_RECV_BUF_MAX_LOAD_LEN) {
        if (HAL_UARTEx_ReceiveToIdle_DMA(&huart7, isr_new_rbuf, MODEM_NET_RECV_BUF_SIZE) != HAL_OK) {
            isr_rdata.buf = isr_new_rbuf;
            isr_rdata.len = -1;
            xQueueSendFromISR(u7_recv_queue, &isr_rdata, NULL);
        } else modem_rbuf_load_len += Size;
    } else {
        printf("modem rbuf overflow\r\n");
        isr_rdata.buf = isr_new_rbuf;
        isr_rdata.len = -1;
        xQueueSendFromISR(u7_recv_queue, &isr_rdata, NULL);
    }
    isr_rdata.buf = isr_old_rbuf;
    isr_rdata.len = Size;
    xQueueSendFromISR(u7_recv_queue, &isr_rdata, NULL);
}

/// @brief Error interrupt function
void HAL_UART7_ErrorCallback(UART_HandleTypeDef *huart)
{
    isr_rdata.buf = huart->pRxBuffPtr;
    isr_rdata.len = -1;
    xQueueSendToFrontFromISR(u7_recv_queue, &isr_rdata, NULL);
}

void HAL_UART7_ReInit(uint32_t baudrate)
{
    if (huart7.gState != HAL_UART_STATE_RESET) {
        HAL_UART_Abort_IT(&huart7);
        HAL_UART_DeInit(&huart7);
    }
    MX_UART7_Init(baudrate);
    xSemaphoreTake(recv_mutex, portMAX_DELAY);
    modem_rbuf_load_len = 0;
    modem_now_baudrate = baudrate;
    xSemaphoreGive(recv_mutex);
}

int HAL_UART7_GPIO_IsReady(void)
{
    int ret = 0;
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    /**UART7 GPIO Configuration
    PA15(JTDI)     ------> UART7_TX
    PG11     ------> UART7_RX
    */
    GPIO_InitStruct.Pin = GPIO_PIN_15;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_11;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

    osDelay(10);
    if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_15) == GPIO_PIN_SET || HAL_GPIO_ReadPin(GPIOG, GPIO_PIN_11) == GPIO_PIN_SET) {
        osDelay(10);
        if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_15) == GPIO_PIN_SET || HAL_GPIO_ReadPin(GPIOG, GPIO_PIN_11) == GPIO_PIN_SET) {
            ret = 1;
        }
    }
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_15);
    HAL_GPIO_DeInit(GPIOG, GPIO_PIN_11);
    return ret;
}

/// @brief Send data to MODEM
/// @param p_data Data
/// @param len Length
/// @param timeout Timeout duration
/// @return 0: Send timeout, -1: Send failed, length: Actual bytes sent
static int modem_net_uart_send(const uint8_t *p_data, uint16_t len, uint32_t timeout)
{
    int recode = 0;
    uint32_t start_tick = 0, now_tick = 0, diff_tick = 0;

    if (xSemaphoreTake(send_mutex, timeout) == pdFALSE) return 0;
    xSemaphoreTake(u7_send_semphr, 0);
    start_tick = HAL_GetTick();
    SCB_CleanDCache_by_Addr((uint32_t*)p_data, len);
    recode = HAL_UART_Transmit_DMA(&huart7, p_data, len);
    if (recode == HAL_OK) {
        if (xSemaphoreTake(u7_send_semphr, pdMS_TO_TICKS(timeout)) != pdPASS) {
            HAL_UART_AbortTransmit_IT(&huart7);
            xSemaphoreGive(send_mutex);
            MODEM_LOGE("U7 send timeout.");
            return -1;
        } else {
            do {
                now_tick = HAL_GetTick();
                diff_tick = (now_tick >= start_tick) ? (now_tick - start_tick) : (0xFFFFFFFFUL - start_tick + now_tick);
                if (diff_tick > timeout) {
                    HAL_UART_AbortTransmit_IT(&huart7);
                    xSemaphoreGive(send_mutex);
                    MODEM_LOGE("U7 state timeout.");
                    return -1;
                }
                osDelay(1);
            } while (huart7.gState != HAL_UART_STATE_READY);
        }
    } else {
        xSemaphoreGive(send_mutex);
        MODEM_LOGE("U7 send failed(err = %d)!", recode);
        return -1;
    }
    xSemaphoreGive(send_mutex);
    return len;
}

int modem_net_ppp_send(uint8_t *p_data, uint16_t len, uint32_t timeout)
{
    // MODEM_LOGD("PPP SEND: %d bytes.", len);
    return modem_net_uart_send(p_data, len, timeout);
}

static int modem_check_baud_rate(uint32_t baudrate, uint8_t is_need_ate0, uint32_t timeout_ms) 
{
    int ret = MODEM_OK;
    uint8_t is_ate1 = 0;

    HAL_UART7_ReInit(baudrate);
    ret = HAL_UARTEx_ReceiveToIdle_DMA(&huart7, MODEM_NET_RECV_BUF_START_PTR, MODEM_NET_RECV_BUF_SIZE);
    if (ret != HAL_OK) return MODEM_ERR_UART_FAILED;
    ret = modem_at_test(&modem_at_handle, (is_need_ate0 ? &is_ate1 : NULL), timeout_ms);
    if (ret == MODEM_OK) {
        if (is_ate1 || is_need_ate0) {
            ret = modem_at_cmd_exec(&modem_at_handle, &at_cmd_ate0);
            if (ret != MODEM_OK) ret = modem_at_test(&modem_at_handle, NULL, timeout_ms);
            if (ret == MODEM_OK) modem_at_set_echo_filter(&modem_at_handle, 1);
        }
    }
    return ret;
}

static int modem_get_baud_rate(void)
{
    int ret = MODEM_OK;

    for (int i = 0; i < sizeof(modem_baud_rate_list) / sizeof(modem_baud_rate_list[0]); i++) {
        ret = modem_check_baud_rate(modem_baud_rate_list[i], 1, 500);
        if (ret == MODEM_OK) return (int)modem_baud_rate_list[i];
    }
    
    return MODEM_ERR_UNKNOW;
}

static int modem_set_baud_rate(uint32_t baudrate)
{
    char at_cmd[32] = {0};

    snprintf(at_cmd, sizeof(at_cmd), "AT+IPR=%lu\r\n", baudrate);
    return modem_at_cmd_wait_ok(&modem_at_handle, at_cmd, 500);
}

static int modem_check_and_set_baud_rate(uint32_t baudrate)
{
    int ret = MODEM_OK;
    
    // First detect target baud rate
    ret = modem_check_baud_rate(baudrate, 1, 1000);
    if (ret == MODEM_OK) return ret;
    
    ret = modem_get_baud_rate();
    if (ret < 0) return ret;
    MODEM_LOGD("Current Modem Baudrate: %d.", ret);
    if (ret != baudrate) {
        MODEM_LOGD("Set Modem Baudrate to %lu.", baudrate);
        ret = modem_set_baud_rate(baudrate);
        if (ret != MODEM_OK) return ret;
        osDelay(500);
        ret = modem_check_baud_rate(baudrate, 0, 500);
        if (ret == MODEM_OK) modem_at_cmd_wait_ok(&modem_at_handle, "AT&W\r\n", 500);   // Save config
    } else ret = MODEM_OK;

    return ret;
}

/// @brief Task to process data received from MODEM
/// @param argument Task parameter
void modem_rx_task(void *argument)
{
#define MAX_ERROR_TIMES 50
    int recode = MODEM_OK, error_times = 0;
    ms_modem_rdata_t rdata = {0};

    while (1) {
        if (xQueueReceive(u7_recv_queue, &rdata, portMAX_DELAY) == pdPASS) {
            if (rdata.len > 0) {
                // MODEM_LOGD("U7_RECV: %d + %d / %lu.", rdata.len, rdata.align, modem_rbuf_load_len);
                xSemaphoreTake(recv_mutex, portMAX_DELAY);
                if (ppp_recv_callback != NULL) {
                    recode = ppp_recv_callback(rdata.buf, rdata.len);
                } else {
                    recode = modem_at_rx_deal_handler(&modem_at_handle, rdata.buf, rdata.len, MODULE_DEAL_MAX_TIME_MS);
                }
                if (modem_rbuf_load_len >= (rdata.len/* + rdata.align*/)) modem_rbuf_load_len -= (rdata.len/* + rdata.align*/);
                else modem_rbuf_load_len = 0;
                xSemaphoreGive(recv_mutex);
                if (recode != MODEM_OK) MODEM_LOGE("U7_RECV: %d, RECODE: %d.", rdata.len, recode);
                error_times = 0;
            } else if (huart7.ErrorCode != HAL_UART_ERROR_NONE) {
                // MODEM_LOGE("U7 ERROR, ErrorCode = 0x%lx.", huart7.ErrorCode);
                if (modem_rbuf_load_len >= MODEM_NET_RECV_BUF_MAX_LOAD_LEN) {
                    xQueueSend(u7_recv_queue, &rdata, MODULE_DEAL_MAX_TIME_MS);
                } else {
                    if (error_times < MAX_ERROR_TIMES) {
                        error_times++;
                        if (huart7.RxState != HAL_UART_STATE_READY || huart7.ReceptionType != HAL_UART_RECEPTION_STANDARD) {
                             recode = HAL_UART_AbortReceive_IT(&huart7);
                            if (recode != HAL_OK) MODEM_LOGE("U7 Stop Recv failed(recode = %d)!", recode);
                        }
                        recode = HAL_UARTEx_ReceiveToIdle_DMA(&huart7, rdata.buf, MODEM_NET_RECV_BUF_SIZE);
                        if (recode != HAL_OK) {
                            xQueueSendToFront(u7_recv_queue, &rdata, MODULE_DEAL_MAX_TIME_MS);
                        }
                    } else {
                        MODEM_LOGE("U7 ERROR too many times, reinit modem.");
                        while (xQueueReceive(u7_recv_queue, &rdata, 0) == pdPASS);
                        HAL_UART7_ReInit(modem_now_baudrate);
                        recode = HAL_UARTEx_ReceiveToIdle_DMA(&huart7, MODEM_NET_RECV_BUF_START_PTR, MODEM_NET_RECV_BUF_SIZE);
                        if (recode != HAL_OK) {
                            rdata.buf = MODEM_NET_RECV_BUF_START_PTR;
                            rdata.len = -1;
                            xQueueSendToFront(u7_recv_queue, &rdata, MODULE_DEAL_MAX_TIME_MS);
                            // MODEM_LOGE("U7 Start Recv failed(recode = %d)!", recode);
                        }
                        error_times = 0;
                    }
                }
            }
        }
    }
}

int modem_device_init(void)
{
    int ret = MODEM_OK;
    uint32_t timeout_ms = 0;
    if (modem_state >= MODEM_STATE_INIT) return MODEM_ERR_INVALID_STATE;
    
    SCB_InvalidateDCache_by_Addr(MODEM_NET_RECV_BUF_START_PTR, MODEM_NET_RECV_BUF_SIZE * MODEM_NET_RECV_BUF_NUM);
    pwr_manager_release(pwr_manager_get_handle(PWR_CAT1_NAME));
    osDelay(MODEM_POWER_OFF_DELAY_MS);
    pwr_manager_acquire(pwr_manager_get_handle(PWR_CAT1_NAME));
    osDelay(MODEM_POWER_ON_DELAY_MS);

    do {
        osDelay(100);
        timeout_ms += 100;
        if (HAL_UART7_GPIO_IsReady() == 1) break;
    } while (timeout_ms < MODEM_GPIO_READY_TIMEOUT_MS);

    if (HAL_UART7_GPIO_IsReady() == 0) {
        MODEM_LOGE("UART7 GPIO not ready!");
        pwr_manager_release(pwr_manager_get_handle(PWR_CAT1_NAME));
        return MODEM_ERR_HW_NOT_CNT;
    }

    ret = modem_at_init(&modem_at_handle, modem_net_uart_send);
    if (ret != MODEM_OK) goto modem_device_init_failed;

    u7_recv_queue = xQueueCreate(MODEM_NET_RECV_QUEUE_NUM, sizeof(ms_modem_rdata_t));
    if (u7_recv_queue == NULL) {
        ret = MODEM_ERR_MEM;
        goto modem_device_init_failed;
    }

    u7_send_semphr = xSemaphoreCreateBinary();
    if (u7_send_semphr == NULL) {
        ret = MODEM_ERR_MEM;
        goto modem_device_init_failed;
    }

    recv_mutex = xSemaphoreCreateMutex();
    if (recv_mutex == NULL) {
        ret = MODEM_ERR_MEM;
        goto modem_device_init_failed;
    }

    send_mutex = xSemaphoreCreateMutex();
    if (send_mutex == NULL) {
        ret = MODEM_ERR_MEM;
        goto modem_device_init_failed;
    }

    modem_rx_task_handle = osThreadNew(modem_rx_task, NULL, &modemRxTask_attributes);
    if (modem_rx_task_handle == NULL) {
        ret = MODEM_ERR_MEM;
        goto modem_device_init_failed;
    }

    // CHECK AND SET BAUD RATE
    ret = modem_check_and_set_baud_rate(MODEM_UART_BAUDRATE);
    if (ret != MODEM_OK) goto modem_device_init_failed;

    modem_state = MODEM_STATE_INIT;
modem_device_init_failed:
    if (ret != MODEM_OK) {
        MODEM_LOGE("modem init failed(ret = %d)!", ret);
        modem_device_deinit();
    }
    return ret;
}

int modem_device_deinit(void)
{
    modem_device_exit_ppp(1);
    if (recv_mutex != NULL) xSemaphoreTake(recv_mutex, portMAX_DELAY);
    if (send_mutex != NULL) xSemaphoreTake(send_mutex, portMAX_DELAY);
    modem_state = MODEM_STATE_UNINIT;
    pwr_manager_release(pwr_manager_get_handle(PWR_CAT1_NAME));

    if (modem_rx_task_handle != NULL) {
        osThreadTerminate(modem_rx_task_handle);
        modem_rx_task_handle = NULL;
    }

    HAL_UART_AbortReceive_IT(&huart7);
    HAL_UART_AbortTransmit_IT(&huart7);
    HAL_UART_DeInit(&huart7);

    modem_at_deinit(&modem_at_handle);

    if (u7_send_semphr != NULL) {
        vSemaphoreDelete(u7_send_semphr);
        u7_send_semphr = NULL;
    }
    if (u7_recv_queue != NULL) {
        vQueueDelete(u7_recv_queue);
        u7_recv_queue = NULL;
    }
    if (recv_mutex != NULL) {
        vSemaphoreDelete(recv_mutex);
        recv_mutex = NULL;
    }
    if (send_mutex != NULL) {
        vSemaphoreDelete(send_mutex);
        send_mutex = NULL;
    }

    return MODEM_OK;
}

static void modem_device_free_rsp_bufs(char **rsp_bufs, size_t num)
{
    if (rsp_bufs == NULL) return;
    for (int i = 0; i < num; i++) {
        if (rsp_bufs[i] != NULL) hal_mem_free(rsp_bufs[i]);
    }
    hal_mem_free(rsp_bufs);
}

static char **modem_device_malloc_rsp_bufs(size_t num)
{
    char **rsp_bufs = NULL;

    if (num == 0) return NULL;
    rsp_bufs = (char **)hal_mem_alloc_large(num * sizeof(char *));
    if (rsp_bufs == NULL) return NULL;

    for (size_t i = 0; i < num; i++) {
        rsp_bufs[i] = (char *)hal_mem_alloc_large(MODEM_AT_RSP_LEN_MAXIMUM);
        if (rsp_bufs[i] == NULL) {
            modem_device_free_rsp_bufs(rsp_bufs, i);
            return NULL;
        }
        memset(rsp_bufs[i], 0, MODEM_AT_RSP_LEN_MAXIMUM);
    }

    return rsp_bufs;
}

int modem_device_wait_sim_ready(uint32_t timeout_ms)
{
    char **rsp_bufs = NULL;
    char cmd_buf[MODEM_AT_CMD_LEN_MAXIMUM] = {0};
    int ret = MODEM_OK;
    uint32_t start_time = 0, end_time = 0, diff_time = 0, no_sim_times = 0;

    start_time = osKernelGetTickCount();
    rsp_bufs = modem_device_malloc_rsp_bufs(2);
    if (rsp_bufs == NULL) return MODEM_ERR_MEM;
    do {
        end_time = osKernelGetTickCount();
        diff_time = (end_time >= start_time) ? (end_time - start_time) : (UINT32_MAX - start_time + end_time);

        ret = modem_at_cmd_wait_rsp(&modem_at_handle, "AT+CPIN?\r\n", rsp_bufs, 2, 500);
        if (ret == 2) {
            if (strstr(rsp_bufs[0], "READY") != NULL) {
                modem_device_free_rsp_bufs(rsp_bufs, 2);
                return MODEM_OK;
            } else if (strstr(rsp_bufs[0], "SIM PIN") != NULL) {
                snprintf(cmd_buf, sizeof(cmd_buf), "AT+CPIN=%s\r\n", g_modem_config.pin);
                ret = modem_at_cmd_wait_ok(&modem_at_handle, cmd_buf, 500);
                if (ret != MODEM_OK) {
                    modem_device_free_rsp_bufs(rsp_bufs, 2);
                    return ret;
                }
            } else if (strstr(rsp_bufs[0], "SIM PUK") != NULL) {
                snprintf(cmd_buf, sizeof(cmd_buf), "AT+CPIN=\"%s\",\"%s\"\r\n", g_modem_config.puk, g_modem_config.pin);
                ret = modem_at_cmd_wait_ok(&modem_at_handle, cmd_buf, 500);
                if (ret != MODEM_OK) {
                    modem_device_free_rsp_bufs(rsp_bufs, 2);
                    return ret;
                }
            }
        } else if (ret == 1 && strstr(rsp_bufs[0], "+CME ERROR: 10") != NULL && (++no_sim_times > 10)) {
            modem_device_free_rsp_bufs(rsp_bufs, 2);
            return MODEM_ERR_INVALID_STATE;
        }
        osDelay(100);
    } while (diff_time < timeout_ms);

    modem_device_free_rsp_bufs(rsp_bufs, 2);
    return MODEM_ERR_TIMEOUT;
}

modem_operator_t modem_device_get_operator(void)
{
    char **rsp_bufs = NULL;
    char *value_ptr = NULL;
    char *value_ptr2 = NULL;
    char plmn_str[8] = {0};
    char operator_str[32] = {0};
    int ret = MODEM_OK;
    modem_operator_t op = MODEM_OPERATOR_UNKNOWN;
    size_t len = 0;

    /* 0. If user has set a manual PLMN, determine operator directly from it */
    if (g_modem_config.plmn[0] != '\0') {
        len = strlen(g_modem_config.plmn);
        if (len >= 5 && len <= 6) {
            if (len == 6) {
                if (strncmp(g_modem_config.plmn, "310004", 6) == 0 ||
                    strncmp(g_modem_config.plmn, "311480", 6) == 0 ||
                    strncmp(g_modem_config.plmn, "311270", 6) == 0 ||
                    strncmp(g_modem_config.plmn, "311271", 6) == 0 ||
                    strncmp(g_modem_config.plmn, "311272", 6) == 0) {
                    return MODEM_OPERATOR_AMERICAN_VERIZON;
                }
            }
            if (strncmp(g_modem_config.plmn, "46000", 5) == 0 ||
                strncmp(g_modem_config.plmn, "46002", 5) == 0 ||
                strncmp(g_modem_config.plmn, "46007", 5) == 0 ||
                strncmp(g_modem_config.plmn, "46008", 5) == 0) {
                return MODEM_OPERATOR_CHINA_MOBILE;
            }
            if (strncmp(g_modem_config.plmn, "46001", 5) == 0 ||
                strncmp(g_modem_config.plmn, "46006", 5) == 0 ||
                strncmp(g_modem_config.plmn, "46009", 5) == 0) {
                return MODEM_OPERATOR_CHINA_UNICOM;
            }
            if (strncmp(g_modem_config.plmn, "46003", 5) == 0 ||
                strncmp(g_modem_config.plmn, "46005", 5) == 0 ||
                strncmp(g_modem_config.plmn, "46011", 5) == 0) {
                return MODEM_OPERATOR_CHINA_TELECOM;
            }
        }
        /* PLMN set but unrecognized — still return UNKNOWN so caller falls through */
        return MODEM_OPERATOR_UNKNOWN;
    }

    /* 1. Prefer AT+CIMI to read IMSI and use the first 5/6 digits (MCC+MNC) to determine the operator */
    rsp_bufs = modem_device_malloc_rsp_bufs(2);
    if (rsp_bufs != NULL) {
        ret = modem_at_cmd_wait_rsp(&modem_at_handle, "AT+CIMI\r\n", rsp_bufs, 2, 500);
        if (ret == 2 && strstr(rsp_bufs[1], "OK") != NULL) {
            len = strlen(rsp_bufs[0]);
            if (len >= 5) {
                /* First try to match the first 6 digits for Verizon and other 3‑digit MNCs (for example 311480, 310004) */
                if (len >= 6) {
                    memcpy(plmn_str, rsp_bufs[0], 6);
                    plmn_str[6] = '\0';
                    if (strncmp(plmn_str, "310004", 6) == 0 ||
                        strncmp(plmn_str, "311480", 6) == 0 ||
                        strncmp(plmn_str, "311270", 6) == 0 ||
                        strncmp(plmn_str, "311271", 6) == 0 ||
                        strncmp(plmn_str, "311272", 6) == 0) {
                        op = MODEM_OPERATOR_AMERICAN_VERIZON;
                    }
                }

                /* If the operator is still unknown, use the first 5 digits to match Chinese operators by MCC+MNC (for example 46000, 46002) */
                if (op == MODEM_OPERATOR_UNKNOWN) {
                    memcpy(plmn_str, rsp_bufs[0], 5);
                    plmn_str[5] = '\0';
                    if (strncmp(plmn_str, "46000", 5) == 0 ||
                        strncmp(plmn_str, "46002", 5) == 0 ||
                        strncmp(plmn_str, "46007", 5) == 0 ||
                        strncmp(plmn_str, "46008", 5) == 0) {
                        op = MODEM_OPERATOR_CHINA_MOBILE;
                    } else if (strncmp(plmn_str, "46001", 5) == 0 ||
                               strncmp(plmn_str, "46006", 5) == 0 ||
                               strncmp(plmn_str, "46009", 5) == 0) {
                        op = MODEM_OPERATOR_CHINA_UNICOM;
                    } else if (strncmp(plmn_str, "46003", 5) == 0 ||
                               strncmp(plmn_str, "46005", 5) == 0 ||
                               strncmp(plmn_str, "46011", 5) == 0) {
                        op = MODEM_OPERATOR_CHINA_TELECOM;
                    }
                }

                if (op != MODEM_OPERATOR_UNKNOWN) {
                    modem_device_free_rsp_bufs(rsp_bufs, 2);
                    return op;
                }
            }
        }
        modem_device_free_rsp_bufs(rsp_bufs, 2);
        rsp_bufs = NULL;
    }

    /* 2. Fallback to AT+COPS? and determine the operator by PLMN or operator name */
    rsp_bufs = modem_device_malloc_rsp_bufs(2);
    if (rsp_bufs == NULL) return MODEM_OPERATOR_UNKNOWN;

    ret = modem_at_cmd_wait_rsp(&modem_at_handle, "AT+COPS?\r\n", rsp_bufs, 2, 500);
    if (ret != 2 || strstr(rsp_bufs[1], "OK") == NULL) {
        modem_device_free_rsp_bufs(rsp_bufs, 2);
        return MODEM_OPERATOR_UNKNOWN;
    }

    value_ptr = strstr(rsp_bufs[0], ",\"");
    if (value_ptr == NULL) {
        modem_device_free_rsp_bufs(rsp_bufs, 2);
        return MODEM_OPERATOR_UNKNOWN;
    }
    value_ptr2 = strstr(value_ptr + 2, "\",");
    if (value_ptr2 == NULL) {
        modem_device_free_rsp_bufs(rsp_bufs, 2);
        return MODEM_OPERATOR_UNKNOWN;
    }

    len = (size_t)((uint32_t)value_ptr2 - (uint32_t)value_ptr - 2);
    if (len >= sizeof(operator_str)) len = sizeof(operator_str) - 1;
    memcpy(operator_str, value_ptr + 2, len);
    operator_str[len] = '\0';

    modem_device_free_rsp_bufs(rsp_bufs, 2);

    /* 2.1 If the PLMN is numeric (for example "46000", "311480"), map it again by MCC/MNC using string comparison */
    if (len >= 5 && len <= 6) {
        if (len == 6) {
            /* First check whether it is Verizon or other operators that use a 3‑digit MNC */
            if (strncmp(operator_str, "310004", 6) == 0 ||
                strncmp(operator_str, "311480", 6) == 0 ||
                strncmp(operator_str, "311270", 6) == 0 ||
                strncmp(operator_str, "311271", 6) == 0 ||
                strncmp(operator_str, "311272", 6) == 0) {
                return MODEM_OPERATOR_AMERICAN_VERIZON;
            }
        }

        /* Match the first 5 digits against the known Chinese operator list */
        if (strncmp(operator_str, "46000", 5) == 0 ||
            strncmp(operator_str, "46002", 5) == 0 ||
            strncmp(operator_str, "46007", 5) == 0 ||
            strncmp(operator_str, "46008", 5) == 0) {
            return MODEM_OPERATOR_CHINA_MOBILE;
        }
        if (strncmp(operator_str, "46001", 5) == 0 ||
            strncmp(operator_str, "46006", 5) == 0 ||
            strncmp(operator_str, "46009", 5) == 0) {
            return MODEM_OPERATOR_CHINA_UNICOM;
        }
        if (strncmp(operator_str, "46003", 5) == 0 ||
            strncmp(operator_str, "46005", 5) == 0 ||
            strncmp(operator_str, "46011", 5) == 0) {
            return MODEM_OPERATOR_CHINA_TELECOM;
        }

        /* If it has 6 digits but the first 5 digits do not match any Chinese operator, fall back to check Verizon again */
        if (len == 6 &&
            (strncmp(operator_str, "310004", 6) == 0 ||
             strncmp(operator_str, "311480", 6) == 0 ||
             strncmp(operator_str, "311270", 6) == 0 ||
             strncmp(operator_str, "311271", 6) == 0 ||
             strncmp(operator_str, "311272", 6) == 0)) {
            return MODEM_OPERATOR_AMERICAN_VERIZON;
        }
    }

    /* 2.2 Match by operator name (long or short name returned by COPS mode 0) */
    if (strstr(operator_str, "China Mobile") != NULL || strstr(operator_str, "CMCC") != NULL ||
        strstr(operator_str, "CHN-CMCC") != NULL || strstr(operator_str, "CHN-MOBILE") != NULL) {
        return MODEM_OPERATOR_CHINA_MOBILE;
    }
    if (strstr(operator_str, "China Unicom") != NULL || strstr(operator_str, "UNICOM") != NULL ||
        strstr(operator_str, "CHN-UNICOM") != NULL || strstr(operator_str, "CHN-CU") != NULL) {
        return MODEM_OPERATOR_CHINA_UNICOM;
    }
    if (strstr(operator_str, "China Telecom") != NULL || strstr(operator_str, "CTCC") != NULL ||
        strstr(operator_str, "CHN-CT") != NULL || strstr(operator_str, "CHN-TELECOM") != NULL) {
        return MODEM_OPERATOR_CHINA_TELECOM;
    }
    if (strstr(operator_str, "Verizon") != NULL) {
        return MODEM_OPERATOR_AMERICAN_VERIZON;
    }

    return MODEM_OPERATOR_UNKNOWN;
}

int modem_device_get_network_status(int *stat, char *tac, char *ci, int *act)
{
    int ret = MODEM_OK, result = 0;
    char **rsp_bufs = NULL;
    char *ptr1, *ptr2 = NULL;

    if (tac != NULL || ci != NULL || act != NULL) {
        ret = modem_at_cmd_wait_ok(&modem_at_handle, "AT+CEREG=2\r\n", 500);
        if (ret != MODEM_OK) return ret;
    }
    rsp_bufs = modem_device_malloc_rsp_bufs(2);
    if (rsp_bufs == NULL) return MODEM_ERR_MEM;
    ret = modem_at_cmd_wait_rsp(&modem_at_handle, "AT+CEREG?\r\n", rsp_bufs, 2, 500);
    if (ret < 0) {
        modem_device_free_rsp_bufs(rsp_bufs, 2);
        return ret;
    }
    if (ret == 2 && strstr(rsp_bufs[1], "OK") != NULL) {
        if (strstr(rsp_bufs[0], "+CEREG:") != NULL) {
            // Get Status
            ptr1 = strstr(rsp_bufs[0], ",");
            if (ptr1 != NULL && stat != NULL) {
                ptr1++;
                *stat = atoi(ptr1);
                result++;
            }
        }
        if (strstr(rsp_bufs[0], "+CEREG: 2") != NULL) {
            do {
                // Get TAC
                ptr1 = strstr(rsp_bufs[0], ",\"");
                if (ptr1 != NULL && tac != NULL) {
                    ptr1 += 2;
                    ptr2 = strstr(ptr1, "\",");
                    if (ptr2 != NULL) {
                        strncpy(tac, ptr1, ((uint32_t)ptr2 - (uint32_t)ptr1));
                        result++;
                    }
                }
                // Get CI
                if (ptr1 == NULL) break;
                ptr1 = strstr(ptr1, ",\"");
                if (ptr1 != NULL && ci != NULL) {
                    ptr1 += 2;
                    ptr2 = strstr(ptr1, "\",");
                    if (ptr2 != NULL) {
                        strncpy(ci, ptr1, ((uint32_t)ptr2 - (uint32_t)ptr1));
                        result++;
                    }
                }
                // Get ACT
                if (ptr1 == NULL) break;
                ptr1 = strstr(ptr1, ",\"");
                if (ptr1 != NULL && act != NULL) {
                    ptr1 += 2;
                    *act = atoi(ptr1);
                    result++;
                }
            } while (0);
        }
    }
    modem_device_free_rsp_bufs(rsp_bufs, 2);
    return result;
}

static int modem_device_activate_network(void)
{
    int ret = MODEM_OK, context_id = 1;
    char at_cmd[MODEM_AT_CMD_LEN_MAXIMUM] = {0};
#if MODEM_IS_CHECK_NETWORK_TYPE
    char **rsp_bufs = NULL;
    char *value_ptr = NULL;
#endif
    modem_operator_t operator = MODEM_OPERATOR_UNKNOWN;

    // Configure Modem Network (treat invalid/unknown as AUTO so we auto-detect)
    if (g_modem_config.isp_selected >= MODEM_OPERATOR_UNKNOWN) {
        operator = MODEM_OPERATOR_AUTO;
    } else {
        operator = (modem_operator_t)g_modem_config.isp_selected;
    }
    if (operator == MODEM_OPERATOR_AUTO) operator = modem_device_get_operator();

    if (operator == MODEM_OPERATOR_AMERICAN_VERIZON) {
        if (g_modem_config.apn_context_id != 1) {
            // Deactivate PDP context
            modem_at_cmd_wait_ok(&modem_at_handle, "AT+QIDEACT=1\r\n", 3000);
            // Clear PDP context
            ret = modem_at_cmd_wait_ok(&modem_at_handle, "AT+QICSGP=1,3,\"\",\"\",\"\",0\r\n", 500);
            if (ret != MODEM_OK) MODEM_LOGE("Unset Default PDP context 1 failed(ret = %d)!", ret);
        }
        g_modem_config.ppp_context_id = 3;
        context_id = 3;
    } else if (g_modem_config.ppp_context_id > 0) {
        context_id = g_modem_config.ppp_context_id;
    } else {
        context_id = 1;
    }

    // unset default PDP context
    if (g_modem_config.apn_context_id > 0 && g_modem_config.apn_context_id != context_id) {
        // Deactivate PDP context
        snprintf(at_cmd, sizeof(at_cmd), "AT+QIDEACT=%d\r\n", g_modem_config.apn_context_id);
        modem_at_cmd_wait_ok(&modem_at_handle, at_cmd, 3000);
        // Clear PDP context
        snprintf(at_cmd, sizeof(at_cmd), "AT+QICSGP=%d,3,\"\",\"\",\"\",0\r\n", g_modem_config.apn_context_id);
        ret = modem_at_cmd_wait_ok(&modem_at_handle, at_cmd, 500);
        if (ret != MODEM_OK) MODEM_LOGE("Unset Default PDP context %d failed(ret = %d)!", g_modem_config.apn_context_id, ret);
    }

    // Deactivate PDP context
    snprintf(at_cmd, sizeof(at_cmd), "AT+QIDEACT=%d\r\n", context_id);
    modem_at_cmd_wait_ok(&modem_at_handle, at_cmd, 3000);
    // Default PDP context
    snprintf(at_cmd, sizeof(at_cmd), "AT+QICSGP=%d,3,\"%s\",\"%s\",\"%s\",%d\r\n", context_id, g_modem_config.apn, g_modem_config.user, g_modem_config.passwd, g_modem_config.authentication);
    ret = modem_at_cmd_wait_ok(&modem_at_handle, at_cmd, 500);
    if (ret != MODEM_OK) return ret;
    g_modem_config.apn_context_id = context_id;

    // Send pre-AT commands
    for (int i = 0; i < sizeof(g_modem_config.ppp_pre_at_cmds) / sizeof(g_modem_config.ppp_pre_at_cmds[0]); i++) {
        if (g_modem_config.ppp_pre_at_cmds[i][0] != 0) {
            ret = modem_at_cmd_wait_ok(&modem_at_handle, g_modem_config.ppp_pre_at_cmds[i], 3000);
            if (ret != MODEM_OK) MODEM_LOGE("Send Pre-AT command %d: %s failed(ret = %d)!", i, g_modem_config.ppp_pre_at_cmds[i], ret);
        }
    }

    // Activate Modem Network
#if MODEM_IS_CHECK_NETWORK_TYPE
    rsp_bufs = modem_device_malloc_rsp_bufs(2);
    if (rsp_bufs == NULL) return MODEM_ERR_MEM;
    ret = modem_at_cmd_wait_rsp(&modem_at_handle, "AT+COPS?\r\n", rsp_bufs, 2, 500);
    if (ret == 2 && strstr(rsp_bufs[1], "OK") != NULL) {
        if (strstr(rsp_bufs[0], "+COPS:") != NULL) {
            value_ptr = strstr(rsp_bufs[0], "\",");
            if (value_ptr != NULL) {
                value_ptr += 2;
                MODEM_LOGD("Network type: %c.", *value_ptr);
                if (*value_ptr != '1' && *value_ptr != '7') {
                    // Not 4G network, need to activate network
#endif
                    snprintf(at_cmd, sizeof(at_cmd), "AT+CGACT=1,%d\r\n", context_id);
                    ret = modem_at_cmd_wait_ok(&modem_at_handle, at_cmd, 10000);
                    if (ret != MODEM_OK) MODEM_LOGE("Activate Network failed(ret = %d)!", ret);
#if MODEM_IS_CHECK_NETWORK_TYPE
                }
            }
        }
    } else if (ret >= 0) ret = MODEM_ERR_FAILED;
    modem_device_free_rsp_bufs(rsp_bufs, 2);
#endif

    return ret;
}

#if MODEM_IS_ENABLE_NETWORK_READY
int modem_device_wait_network_ready(uint32_t timeout_ms)
{
    int ret = MODEM_OK, net_status = 0;
    uint32_t start_time = 0, end_time = 0, diff_time = 0;

    start_time = osKernelGetTickCount();
    // Wait for SIM ready
    ret = modem_device_wait_sim_ready(timeout_ms);
    if (ret != MODEM_OK) return ret;
    // Wait for Network ready
    do {
        osDelay(10);
        ret = modem_device_get_network_status(&net_status, NULL, NULL, NULL);
        if (ret == 1 && (net_status == 1 || net_status == 5)) return MODEM_OK;
        end_time = osKernelGetTickCount();
        diff_time = (end_time >= start_time) ? (end_time - start_time) : (UINT32_MAX - start_time + end_time);
    } while (diff_time < timeout_ms);
    
    return MODEM_ERR_TIMEOUT;
}
#endif

int modem_device_check_and_enable_ecm(void)
{
    int ret = MODEM_OK;
    char **rsp_bufs = NULL;

    rsp_bufs = modem_device_malloc_rsp_bufs(2);
    if (rsp_bufs == NULL) return MODEM_ERR_MEM;
    ret = modem_at_cmd_wait_rsp(&modem_at_handle, "AT+QCFG=\"usbnet\"\r\n", rsp_bufs, 2, 500);
    if (ret == 2 && strstr(rsp_bufs[0], "+QCFG: \"usbnet\",1") != NULL) {
        modem_device_free_rsp_bufs(rsp_bufs, 2);
        return MODEM_OK;
    }
    ret = modem_at_cmd_wait_ok(&modem_at_handle, "AT+QCFG=\"usbnet\",1\r\n", 500);

    modem_device_free_rsp_bufs(rsp_bufs, 2);
    return ret;
}

int modem_device_get_info(modem_info_t *info, uint8_t is_update_all)
{
    char **rsp_bufs = NULL;
    char *value_ptr = NULL, *value_ptr2 = NULL;
    int csq = 0, ber = 0, dBm = 0;
    int ret = MODEM_OK, net_status = 0;
    if (info == NULL) return MODEM_ERR_INVALID_ARG;
    if (modem_state != MODEM_STATE_INIT) return MODEM_ERR_INVALID_STATE;

    rsp_bufs = modem_device_malloc_rsp_bufs(2);
    if (rsp_bufs == NULL) return MODEM_ERR_MEM;
    if (is_update_all) {
        // Get Model Name, IMEI, Version
        ret = modem_at_cmd_wait_rsp(&modem_at_handle, "AT+CGMM\r\n", rsp_bufs, 2, 500);
        if (ret == 2) {
            ret = MODEM_OK;
            if (strstr(rsp_bufs[1], "OK") == NULL) ret = MODEM_ERR_FAILED;
            else strncpy(info->model_name, rsp_bufs[0], sizeof(info->model_name) - 1);
        }
        if (ret != MODEM_OK) {
            strncpy(info->model_name, "-", sizeof(info->model_name) - 1);
            MODEM_LOGE("Get Model Name failed(ret = %d)!", ret);
        }
        
        ret = modem_at_cmd_wait_rsp(&modem_at_handle, "AT+GSN\r\n", rsp_bufs, 2, 500);
        if (ret == 2) {
            ret = MODEM_OK;
            if (strstr(rsp_bufs[1], "OK") == NULL) ret = MODEM_ERR_FAILED;
            else strncpy(info->imei, rsp_bufs[0], sizeof(info->imei) - 1);
        }
        if (ret != MODEM_OK) {
            strncpy(info->imei, "-", sizeof(info->imei) - 1);
            MODEM_LOGE("Get IMEI failed(ret = %d)!", ret);
        }

        ret = modem_at_cmd_wait_rsp(&modem_at_handle, "AT+CGMR\r\n", rsp_bufs, 2, 500);
        if (ret == 2) {
            ret = MODEM_OK;
            if (strstr(rsp_bufs[1], "OK") == NULL) ret = MODEM_ERR_FAILED;
            else strncpy(info->version, rsp_bufs[0], sizeof(info->version) - 1);
        }
        if (ret != MODEM_OK) {
            strncpy(info->version, "-", sizeof(info->version) - 1);
            MODEM_LOGE("Get Version failed(ret = %d)!", ret);
        }
    }

    // Get SIM Status, IMSI, ICCID, PLMN ID
    ret = modem_at_cmd_wait_rsp(&modem_at_handle, "AT+CPIN?\r\n", rsp_bufs, 2, 500);
    if (ret == 1 && strstr(rsp_bufs[0], "+CME ERROR") != NULL) {
        sscanf(rsp_bufs[0], "+CME ERROR: %d", &ret);
        if (ret == 10) {
            strncpy(info->sim_status, "No SIM Card", sizeof(info->sim_status) - 1);
        } else {
            strncpy(info->sim_status, rsp_bufs[0], sizeof(info->sim_status) - 1);
        }
    } else if (ret == 2) {
        if (strstr(rsp_bufs[1], "OK") == NULL) {
            modem_device_free_rsp_bufs(rsp_bufs, 2);
            return MODEM_ERR_FAILED;
        }
        value_ptr = strstr(rsp_bufs[0], "+CPIN:");
        if (value_ptr == NULL) {
            modem_device_free_rsp_bufs(rsp_bufs, 2);
            return MODEM_ERR_FAILED;
        }
        strncpy(info->sim_status, value_ptr + strlen("+CPIN:"), sizeof(info->sim_status) - 1);
    } else strncpy(info->sim_status, "Unknown", sizeof(info->sim_status) - 1);

    if (strstr(info->sim_status, "READY") != NULL) {
        ret = modem_at_cmd_wait_rsp(&modem_at_handle, "AT+CIMI\r\n", rsp_bufs, 2, 500);
        if (ret == 2) {
            ret = MODEM_OK;
            if (strstr(rsp_bufs[1], "OK") == NULL) ret = MODEM_ERR_FAILED;
            else strncpy(info->imsi, rsp_bufs[0], sizeof(info->imsi) - 1);
        }
        if (ret != MODEM_OK) {
            strncpy(info->imsi, "-", sizeof(info->imsi) - 1);
            MODEM_LOGE("Get IMSI failed(ret = %d)!", ret);
        }
        if (strlen(info->imsi) < 5) strncpy(info->plmn_id, "-", sizeof(info->plmn_id) - 1);
        else strncpy(info->plmn_id, info->imsi, 5);

        ret = modem_at_cmd_wait_rsp(&modem_at_handle, "AT+QCCID\r\n", rsp_bufs, 2, 500);
        if (ret == 2) {
            ret = MODEM_OK;
            if (strstr(rsp_bufs[1], "OK") == NULL) ret = MODEM_ERR_FAILED;
            value_ptr = strstr(rsp_bufs[0], "+QCCID:");
            if (value_ptr == NULL) strncpy(info->iccid, rsp_bufs[0], sizeof(info->iccid) - 1);
            else strncpy(info->iccid, value_ptr + strlen("+QCCID:"), sizeof(info->iccid) - 1);
        }
        if (ret != MODEM_OK) {
            strncpy(info->iccid, "-", sizeof(info->iccid) - 1);
            MODEM_LOGE("Get ICCID failed(ret = %d)!", ret);
        }
    }
    
    // Get Signal Quality
    ret = modem_at_cmd_wait_rsp(&modem_at_handle, "AT+CSQ\r\n", rsp_bufs, 2, 500);
    if (ret == 2) {
        ret = MODEM_OK;
        if (strstr(rsp_bufs[1], "OK") == NULL) ret = MODEM_ERR_FAILED;
        else if (sscanf(rsp_bufs[0], "+CSQ: %d,%d", &csq, &ber) != 2) ret = MODEM_ERR_FMT;
        else {
            if (csq >= 0 && csq <= 31) {
                dBm = -113 + 2 * csq;
                info->csq_value = csq;
                info->ber_value = ber;
                info->rssi = dBm;
                if (dBm >= -53) info->csq_level = 5;
                else if (dBm >= -63) info->csq_level = 4;
                else if (dBm >= -73) info->csq_level = 3;
                else if (dBm >= -83) info->csq_level = 2;
                else if (dBm >= -93) info->csq_level = 1;
                else info->csq_level = 0;
            } else info->csq_level = 0;
        }
    }
    if (ret != MODEM_OK) {
        info->csq_value = 99;
        info->ber_value = 99;
        info->rssi = -113;
        info->csq_level = 0;
        MODEM_LOGE("Get CSQ failed(ret = %d)!", ret);
    }

    // Get Operator
    ret = modem_at_cmd_wait_rsp(&modem_at_handle, "AT+COPS?\r\n", rsp_bufs, 2, 500);
    if (ret == 2) {
        ret = MODEM_OK;
        if (strstr(rsp_bufs[1], "OK") == NULL) ret = MODEM_ERR_FAILED;
        else {
            value_ptr = strstr(rsp_bufs[0], ",\"");
            if (value_ptr == NULL) ret = MODEM_ERR_FAILED;
            else {
                value_ptr2 = strstr(value_ptr + 2, "\",");
                if (value_ptr2 == NULL) ret = MODEM_ERR_FMT;
                else if (((uint32_t)value_ptr2 - (uint32_t)value_ptr - 2) >= sizeof(info->operator)) ret = MODEM_ERR_INVALID_SIZE;
                else {
                    memcpy(info->operator, value_ptr + 2, ((uint32_t)value_ptr2 - (uint32_t)value_ptr - 2));
                    info->operator[((uint32_t)value_ptr2 - (uint32_t)value_ptr - 2)] = '\0';
                }
            }
        }
    }
    if (ret != MODEM_OK) {
        strncpy(info->operator, "-", sizeof(info->operator) - 1);
        MODEM_LOGE("Get Operator failed(ret = %d)!", ret);
    }

    // Get Network Info (+QNWINFO: "FDD LTE","46011","LTE BAND 5",2452)
    ret = modem_at_cmd_wait_rsp(&modem_at_handle, "AT+QNWINFO\r\n", rsp_bufs, 2, 1000);
    if (ret == 2) {
        ret = MODEM_OK;
        if (strstr(rsp_bufs[1], "OK") == NULL) ret = MODEM_ERR_FAILED;
        else {
            value_ptr = strstr(rsp_bufs[0], "+QNWINFO: \"");
            if (value_ptr == NULL) ret = MODEM_ERR_FAILED;
            else {
                value_ptr += strlen("+QNWINFO: \"");
                value_ptr2 = strstr(value_ptr, "\",\"");
                if (value_ptr2 == NULL) ret = MODEM_ERR_FMT;
                else if (((uint32_t)value_ptr2 - (uint32_t)value_ptr) >= sizeof(info->network_type)) ret = MODEM_ERR_INVALID_SIZE;
                else {
                    memcpy(info->network_type, value_ptr, ((uint32_t)value_ptr2 - (uint32_t)value_ptr));
                    info->network_type[((uint32_t)value_ptr2 - (uint32_t)value_ptr)] = '\0';
                }
            }
        }
    }
    if (ret != MODEM_OK) {
        strncpy(info->network_type, "-", sizeof(info->network_type) - 1);
        MODEM_LOGE("Get Network Type failed(ret = %d)!", ret);
    }

    // SIM card registration status、LAC、Cell ID, (+CREG: 2,1,"5F0C","E0B70B",7)
    strncpy(info->lac, "-", sizeof(info->lac) - 1);
    strncpy(info->cell_id, "-", sizeof(info->cell_id) - 1);
    strncpy(info->registration_status, "Unknown", sizeof(info->registration_status) - 1);

    ret = modem_device_get_network_status(&net_status, info->lac, info->cell_id, NULL);
    if (ret == 3) {
        if (net_status == 0) strncpy(info->registration_status, "Not registered", sizeof(info->registration_status) - 1);
        else if (net_status == 1) strncpy(info->registration_status, "Registered (Home network)", sizeof(info->registration_status) - 1);
        else if (net_status == 2) strncpy(info->registration_status, "Searching", sizeof(info->registration_status) - 1);
        else if (net_status == 3) strncpy(info->registration_status, "Registration denied", sizeof(info->registration_status) - 1);
        else if (net_status == 4) strncpy(info->registration_status, "Unknown", sizeof(info->registration_status) - 1);
        else if (net_status == 5) strncpy(info->registration_status, "Registered (Roaming)", sizeof(info->registration_status) - 1);
        else sprintf(info->registration_status, "Unknown(%d)", net_status);
    } else MODEM_LOGE("Get CREG info failed(ret = %d)!", ret);

    modem_device_free_rsp_bufs(rsp_bufs, 2);
    return MODEM_OK;
}

int modem_device_restart_network(void)
{
    int ret = MODEM_OK;
    if (modem_state != MODEM_STATE_INIT) return MODEM_ERR_INVALID_STATE;

    MODEM_LOGD("Restart Modem Network...");
    ret = modem_at_cmd_wait_ok(&modem_at_handle, "AT+CFUN=4\r\n", 3000);
    if (ret != MODEM_OK) return ret;

    ret = modem_at_cmd_wait_ok(&modem_at_handle, "AT+CFUN=1\r\n", 3000);
    if (ret != MODEM_OK) return ret;
    MODEM_LOGD("Modem Network Restarted.");

    return MODEM_OK;
}

int modem_device_set_config(const modem_config_t *config)
{
    int ret = MODEM_OK;
    char at_cmd[MODEM_AT_CMD_LEN_MAXIMUM] = {0};
    uint8_t is_need_restart_network = 0;
    if (config == NULL) return MODEM_ERR_INVALID_ARG;
    if (modem_state != MODEM_STATE_INIT) return MODEM_ERR_INVALID_STATE;
    
    if (config->apn[0] != 0 && strcmp(config->apn, g_modem_config.apn) != 0) {
        // Deactivate PDP context
        modem_at_cmd_wait_ok(&modem_at_handle, "AT+QIDEACT=1\r\n", 3000);
        // Set PDP context
        snprintf(at_cmd, sizeof(at_cmd), "AT+CGDCONT=1,\"IPV4V6\",\"%s\"\r\n", config->apn);
        ret = modem_at_cmd_wait_ok(&modem_at_handle, at_cmd, 500);
        if (ret != MODEM_OK) return ret;
        MODEM_LOGD("Set Modem APN: %s => %s.", g_modem_config.apn, config->apn);
        strncpy(g_modem_config.apn, config->apn, sizeof(g_modem_config.apn) - 1);
        is_need_restart_network = 1;
    }

    if (strcmp(config->apn, g_modem_config.apn) != 0 || strcmp(config->user, g_modem_config.user) != 0 || strcmp(config->passwd, g_modem_config.passwd) != 0 || config->authentication != g_modem_config.authentication) {
        // Deactivate PDP context
        modem_at_cmd_wait_ok(&modem_at_handle, "AT+QIDEACT=1\r\n", 3000);
        // Set PDP context
        snprintf(at_cmd, sizeof(at_cmd), "AT+QICSGP=1,3,\"%s\",\"%s\",\"%s\",%d\r\n", config->apn, config->user, config->passwd, config->authentication);
        ret = modem_at_cmd_wait_ok(&modem_at_handle, at_cmd, 500);
        if (ret != MODEM_OK) return ret;
        MODEM_LOGD("Set Modem QICSGP: %s, %s, %s, %d => %s, %s, %s, %d.", g_modem_config.apn, g_modem_config.user, g_modem_config.passwd, g_modem_config.authentication, config->apn, config->user, config->passwd, config->authentication);
        strncpy(g_modem_config.apn, config->apn, sizeof(g_modem_config.apn) - 1);
        strncpy(g_modem_config.user, config->user, sizeof(g_modem_config.user) - 1);
        strncpy(g_modem_config.passwd, config->passwd, sizeof(g_modem_config.passwd) - 1);
        g_modem_config.authentication = config->authentication;
        is_need_restart_network = 1;
    }

    if (strcmp(config->plmn, g_modem_config.plmn) != 0) {
        MODEM_LOGD("Set Modem PLMN: %s => %s.", g_modem_config.plmn, config->plmn);
        if (config->plmn[0] == 0) {
            ret = modem_at_cmd_wait_ok(&modem_at_handle, "AT+COPS=0\r\n", 500);
            if (ret != MODEM_OK) return ret;
        } else {
            snprintf(at_cmd, sizeof(at_cmd), "AT+COPS=1,2,\"%s\"\r\n", config->plmn);
            ret = modem_at_cmd_wait_ok(&modem_at_handle, at_cmd, 500);
            if (ret != MODEM_OK) return ret;
        }
        strncpy(g_modem_config.plmn, config->plmn, sizeof(g_modem_config.plmn) - 1);
        g_modem_config.plmn[sizeof(g_modem_config.plmn) - 1] = '\0';
        is_need_restart_network = 1;
    }

    if (config->is_enable_roam != g_modem_config.is_enable_roam) {
        snprintf(at_cmd, sizeof(at_cmd), "AT+QCFG=\"roamservice\",%d,1\r\n", (config->is_enable_roam ? 2 : 1));
        ret = modem_at_cmd_wait_ok(&modem_at_handle, at_cmd, 500);
        if (ret == MODEM_OK) {
            MODEM_LOGD("Set Modem Roaming: %d => %d.", g_modem_config.is_enable_roam, config->is_enable_roam);
            g_modem_config.is_enable_roam = config->is_enable_roam;
            is_need_restart_network = 1;
        }
    }

    if (is_need_restart_network) modem_device_restart_network();

    if (config->pin[0] != 0) {
        MODEM_LOGD("Set Modem PIN: %s => %s.", g_modem_config.pin, config->pin);
        strncpy(g_modem_config.pin, config->pin, sizeof(g_modem_config.pin) - 1);
    }
    
    if (config->puk[0] != 0) {
        MODEM_LOGD("Set Modem PUK: %s => %s.", g_modem_config.puk, config->puk);
        strncpy(g_modem_config.puk, config->puk, sizeof(g_modem_config.puk) - 1);
    }

    if (config->isp_selected != g_modem_config.isp_selected) {
        MODEM_LOGD("Set Modem ISP Selected: %d => %d.", g_modem_config.isp_selected, config->isp_selected);
        g_modem_config.isp_selected = config->isp_selected;
    }

    if (config->ppp_context_id != g_modem_config.ppp_context_id) {
        MODEM_LOGD("Set Modem PPP Context ID: %d => %d.", g_modem_config.ppp_context_id, config->ppp_context_id);
        g_modem_config.ppp_context_id = config->ppp_context_id;
    }
    memcpy(g_modem_config.ppp_pre_at_cmds, config->ppp_pre_at_cmds, sizeof(g_modem_config.ppp_pre_at_cmds));
    
    return MODEM_OK;
}

int modem_device_get_config(modem_config_t *config)
{
    int index = 0, context_id = 0;
    char **rsp_bufs = NULL;
    char *value_ptr1 = NULL, *value_ptr2 = NULL;
    char cmd_str[64] = {0};
    int ret = MODEM_OK;
    if (config == NULL) return MODEM_ERR_INVALID_ARG;
    if (modem_state != MODEM_STATE_INIT && modem_state != MODEM_STATE_PPP) return MODEM_ERR_INVALID_STATE;
    if (modem_state == MODEM_STATE_PPP) {
        memcpy(config, &g_modem_config, sizeof(modem_config_t));
        return MODEM_OK;
    }

    memset(config, 0, sizeof(modem_config_t));
    rsp_bufs = modem_device_malloc_rsp_bufs(8);
    if (rsp_bufs == NULL) return MODEM_ERR_MEM;
    ret = modem_at_cmd_wait_rsp(&modem_at_handle, "AT+CGDCONT?\r\n", rsp_bufs, 8, 500);
    if (ret < 1) {
        modem_device_free_rsp_bufs(rsp_bufs, 8);
        return ret;
    }
    for (index = 0; index < ret; index++) {
        if (strstr(rsp_bufs[index], "+CGDCONT:") != NULL) {
            // Find the context ID
            value_ptr1 = NULL;
            do {
                // +CGDCONT: 1,"IP","",0
                value_ptr1 = strstr(rsp_bufs[index], ",\"IP\",\"");
                if (value_ptr1 != NULL) {
                    context_id = *(value_ptr1 - 1) - '0';
                    if (*(value_ptr1 - 2) >= '0' && *(value_ptr1 - 2) <= '9') context_id = (*(value_ptr1 - 2) - '0') * 10 + context_id;
                    value_ptr1 += strlen(",\"IP\",\"");
                    break;
                }
                // +CGDCONT: 1,"IPV4V6","",0
                value_ptr1 = strstr(rsp_bufs[index], ",\"IPV4V6\",\"");
                if (value_ptr1 != NULL) {
                    context_id = *(value_ptr1 - 1) - '0';
                    if (*(value_ptr1 - 2) >= '0' && *(value_ptr1 - 2) <= '9') context_id = (*(value_ptr1 - 2) - '0') * 10 + context_id;
                    value_ptr1 += strlen(",\"IPV4V6\",\"");
                    break;
                }
                // +CGDCONT: 1,"IPV6","",0
                value_ptr1 = strstr(rsp_bufs[index], ",\"IPV6\",\"");
                if (value_ptr1 != NULL) {
                    context_id = *(value_ptr1 - 1) - '0';
                    if (*(value_ptr1 - 2) >= '0' && *(value_ptr1 - 2) <= '9') context_id = (*(value_ptr1 - 2) - '0') * 10 + context_id;
                    value_ptr1 += strlen(",\"IPV6\",\"");
                    break;
                }
                // +CGDCONT: 1,"PPP","",0
                value_ptr1 = strstr(rsp_bufs[index], ",\"PPP\",\"");
                if (value_ptr1 != NULL) {
                    context_id = *(value_ptr1 - 1) - '0';
                    if (*(value_ptr1 - 2) >= '0' && *(value_ptr1 - 2) <= '9') context_id = (*(value_ptr1 - 2) - '0') * 10 + context_id;
                    value_ptr1 += strlen(",\"PPP\",\"");
                    break;
                }
            } while (0);
            // Get the APN
            if (value_ptr1 != NULL) {
                value_ptr2 = strchr(value_ptr1, '"');
                if (value_ptr2 == NULL) {
                    modem_device_free_rsp_bufs(rsp_bufs, 8);
                    return MODEM_ERR_FAILED;
                }
                if (value_ptr2 > value_ptr1) {
                    size_t apn_len = (size_t)(value_ptr2 - value_ptr1);
                    if (apn_len >= sizeof(config->apn)) apn_len = sizeof(config->apn) - 1;
                    memcpy(config->apn, value_ptr1, apn_len);
                    config->apn[apn_len] = '\0';
                    strncpy(g_modem_config.apn, config->apn, sizeof(g_modem_config.apn) - 1);
                    g_modem_config.apn[sizeof(g_modem_config.apn) - 1] = '\0';
                }
            }
        }
        if (config->apn[0] != 0) {
            config->apn_context_id = context_id;
            g_modem_config.apn_context_id = config->apn_context_id;
            break;
        }
    }
    
    if (config->apn_context_id == 0) {
        config->apn_context_id = 1;
        g_modem_config.apn_context_id = config->apn_context_id;
    }
    if (context_id > 0) {
        snprintf(cmd_str, sizeof(cmd_str), "AT+QICSGP=%d\r\n", context_id);
        ret = modem_at_cmd_wait_rsp(&modem_at_handle, cmd_str, rsp_bufs, 2, 500);
        if (ret < 1) {
            modem_device_free_rsp_bufs(rsp_bufs, 8);
            return ret;
        }
        if (strstr(rsp_bufs[0], "+QICSGP:") != NULL) {
            value_ptr1 = strstr(rsp_bufs[0], ",\"");
            if (value_ptr1 == NULL) {
                modem_device_free_rsp_bufs(rsp_bufs, 8);
                return MODEM_ERR_FAILED;
            }
            value_ptr1 += strlen(",\"");
            value_ptr2 = strchr(value_ptr1, '"');
            if (value_ptr2 == NULL) {
                modem_device_free_rsp_bufs(rsp_bufs, 8);
                return MODEM_ERR_FAILED;
            }
            if (value_ptr2 == value_ptr1) {
                config->apn[0] = 0;
                g_modem_config.apn[0] = 0;
            } else {
                size_t len_apn = (size_t)(value_ptr2 - value_ptr1);
                if (len_apn >= sizeof(config->apn)) len_apn = sizeof(config->apn) - 1;
                memcpy(config->apn, value_ptr1, len_apn);
                config->apn[len_apn] = '\0';
                strncpy(g_modem_config.apn, config->apn, sizeof(g_modem_config.apn) - 1);
                g_modem_config.apn[sizeof(g_modem_config.apn) - 1] = '\0';
            }
            value_ptr1 = strstr(value_ptr2, ",\"");
            if (value_ptr1 == NULL) {
                modem_device_free_rsp_bufs(rsp_bufs, 8);
                return MODEM_ERR_FAILED;
            }
            value_ptr1 += strlen(",\"");
            value_ptr2 = strchr(value_ptr1, '"');
            if (value_ptr2 == NULL) {
                modem_device_free_rsp_bufs(rsp_bufs, 8);
                return MODEM_ERR_FAILED;
            }
            if (value_ptr2 == value_ptr1) {
                config->user[0] = 0;
                g_modem_config.user[0] = 0;
            } else {
                size_t len_usr = (size_t)(value_ptr2 - value_ptr1);
                if (len_usr >= sizeof(config->user)) len_usr = sizeof(config->user) - 1;
                memcpy(config->user, value_ptr1, len_usr);
                config->user[len_usr] = '\0';
                strncpy(g_modem_config.user, config->user, sizeof(g_modem_config.user) - 1);
                g_modem_config.user[sizeof(g_modem_config.user) - 1] = '\0';
            }
            value_ptr1 = strstr(value_ptr2, ",\"");
            if (value_ptr1 == NULL) {
                modem_device_free_rsp_bufs(rsp_bufs, 8);
                return MODEM_ERR_FAILED;
            }
            value_ptr1 += strlen(",\"");
            value_ptr2 = strchr(value_ptr1, '"');
            if (value_ptr2 == NULL) {
                modem_device_free_rsp_bufs(rsp_bufs, 8);
                return MODEM_ERR_FAILED;
            }
            if (value_ptr2 == value_ptr1) {
                config->passwd[0] = 0;
                g_modem_config.passwd[0] = 0;
            } else {
                size_t len_pwd = (size_t)(value_ptr2 - value_ptr1);
                if (len_pwd >= sizeof(config->passwd)) len_pwd = sizeof(config->passwd) - 1;
                memcpy(config->passwd, value_ptr1, len_pwd);
                config->passwd[len_pwd] = '\0';
                strncpy(g_modem_config.passwd, config->passwd, sizeof(g_modem_config.passwd) - 1);
                g_modem_config.passwd[sizeof(g_modem_config.passwd) - 1] = '\0';
            }
            value_ptr1 = strstr(value_ptr2, ",");
            if (value_ptr1 == NULL) {
                modem_device_free_rsp_bufs(rsp_bufs, 8);
                return MODEM_ERR_FAILED;
            }
            value_ptr1 += strlen(",");
            config->authentication = atoi(value_ptr1);
            g_modem_config.authentication = config->authentication;
        }
    }

    ret = modem_at_cmd_wait_rsp(&modem_at_handle, "AT+QCFG=\"roamservice\"\r\n", rsp_bufs, 2, 500);
    if (ret < 1) {
        modem_device_free_rsp_bufs(rsp_bufs, 8);
        return ret;
    }
    if (strstr(rsp_bufs[0], "+QCFG:") != NULL) {
        value_ptr1 = strstr(rsp_bufs[0], "\",");
        if (value_ptr1 == NULL) {
            modem_device_free_rsp_bufs(rsp_bufs, 8);
            return MODEM_ERR_FAILED;
        }
        value_ptr1 += strlen("\",");
        {
            int roam_val = atoi(value_ptr1);
            config->is_enable_roam = (roam_val == 2) ? 1u : 0u;
            g_modem_config.is_enable_roam = config->is_enable_roam;
        }
    }

    memcpy(config->pin, g_modem_config.pin, sizeof(g_modem_config.pin));
    memcpy(config->puk, g_modem_config.puk, sizeof(g_modem_config.puk));
    config->isp_selected = g_modem_config.isp_selected;
    config->ppp_context_id = g_modem_config.ppp_context_id;
    memcpy(config->ppp_pre_at_cmds, g_modem_config.ppp_pre_at_cmds, sizeof(g_modem_config.ppp_pre_at_cmds));
    memcpy(config->plmn, g_modem_config.plmn, sizeof(g_modem_config.plmn));

    modem_device_free_rsp_bufs(rsp_bufs, 8);
    return MODEM_OK;
}

int modem_device_into_ppp(modem_net_ppp_callback_t recv_callback)
{
    int ret = MODEM_OK;
    char at_cmd[32] = {0};
    if (recv_callback == NULL) return MODEM_ERR_INVALID_ARG;
    if (modem_state != MODEM_STATE_INIT) return MODEM_ERR_INVALID_STATE;

    ret = modem_device_activate_network();
    if (ret != MODEM_OK) return ret;

    if (g_modem_config.ppp_context_id == 0) {
        ret = modem_at_cmd_wait_str(&modem_at_handle, "ATD*99#\r", "CONNECT|OK", 500);
    } else {
        snprintf(at_cmd, sizeof(at_cmd), "ATD*99***%d#\r", g_modem_config.ppp_context_id);
        ret = modem_at_cmd_wait_str(&modem_at_handle, at_cmd, "CONNECT|OK", 500);
    }
    if (ret == MODEM_OK) {
        xSemaphoreTake(recv_mutex, portMAX_DELAY);
        modem_state = MODEM_STATE_PPP;
        ppp_recv_callback = recv_callback;
        xSemaphoreGive(recv_mutex);
    }
    return ret;
}

int modem_device_exit_ppp(uint8_t is_focre)
{
    int ret = MODEM_OK;
    if (modem_state != MODEM_STATE_PPP) return MODEM_ERR_INVALID_STATE;

    xSemaphoreTake(recv_mutex, portMAX_DELAY);
    ppp_recv_callback = NULL;
    xSemaphoreGive(recv_mutex);
    if (is_focre) {
        osDelay(2000);
        ret = modem_at_cmd_wait_str(&modem_at_handle, "+++", "OK|NO CARRIER", 2000);
    }
    xSemaphoreTake(recv_mutex, portMAX_DELAY);
    modem_state = MODEM_STATE_INIT;
    xSemaphoreGive(recv_mutex);
    
    return ret;
}

modem_state_t modem_device_get_state(void)
{
    return modem_state;
}

static int modem_net_ppp_recv_callback(uint8_t *p_data, uint16_t len)
{
    LOG_SIMPLE("ppp_recv_callback: %d.\r\n", len);
    return MODEM_OK;
}

static int modem_cmd_deal_cmd(int argc, char* argv[])
{
    int ret = MODEM_OK, i = 0;
    int rsp_num = 1;
    char at_cmd[MODEM_AT_CMD_LEN_MAXIMUM] = {0};
    char *rsp_list[MODEM_AT_RSP_MAX_LINE_NUM] = {0};
    uint32_t timeout = 500;

    if (argc < 2) {
        LOG_SIMPLE("Usage: modem <command>");
        return -1;
    }

    if (strcmp(argv[1], "init") == 0) {
        ret = modem_device_init();
        if (ret != MODEM_OK) {
            LOG_SIMPLE("modem init failed(ret = %d)!", ret);
            return -1;
        }
        LOG_SIMPLE("modem init success!");
        return 0;
    } else if (strcmp(argv[1], "deinit") == 0) {
        ret = modem_device_deinit();
        if (ret != MODEM_OK) {
            LOG_SIMPLE("modem deinit failed(ret = %d)!", ret);
            return -1;
        }
        LOG_SIMPLE("modem deinit success!");
        return 0;
    } else if (strcmp(argv[1], "into_ppp") == 0) {
        ret = modem_device_into_ppp(modem_net_ppp_recv_callback);
        if (ret != MODEM_OK) {
            LOG_SIMPLE("modem into ppp failed(ret = %d)!", ret);
            return -1;
        }
        LOG_SIMPLE("modem into ppp success!");
        return 0;
    } else if (strcmp(argv[1], "exit_ppp") == 0) {
        ret = modem_device_exit_ppp(1);
        if (ret != MODEM_OK) {
            LOG_SIMPLE("modem exit ppp failed(ret = %d)!", ret);
            return -1;
        }
        LOG_SIMPLE("modem exit ppp success!");
        return 0;
    } else if (strcmp(argv[1], "wait_sim_ready") == 0) {
        ret = modem_device_wait_sim_ready(5000);
        if (ret != MODEM_OK) {
            LOG_SIMPLE("modem wait sim ready failed(ret = %d)!", ret);
            return -1;
        }
        LOG_SIMPLE("modem wait sim ready success!");
        return 0;
    } else if (strstr(argv[1], "AT") != NULL) {
        if (modem_state != MODEM_STATE_INIT) {
            LOG_SIMPLE("modem is not in init state!");
            return -1;
        }
        for (i = 2; i < argc - 1; i++) {
            if (strcmp(argv[i], "-t") == 0) {
                timeout = atoi(argv[i + 1]);
                i++;
            } else if (strcmp(argv[i], "-r") == 0) {
                rsp_num = atoi(argv[i + 1]);
                i++;
            }
        }
        if (rsp_num < 1) rsp_num = 1;
        if (rsp_num > MODEM_AT_RSP_MAX_LINE_NUM) rsp_num = MODEM_AT_RSP_MAX_LINE_NUM;
        for (i = 0; i < rsp_num; i++) {
            rsp_list[i] = (char *)pvPortMalloc(MODEM_AT_RSP_LEN_MAXIMUM);
            if (rsp_list[i] == NULL) {
                LOG_SIMPLE("malloc rsp_list[%d] failed!", i);
                ret = MODEM_ERR_MEM;
                goto modem_at_cmd_end;
            }
        }
        snprintf(at_cmd, sizeof(at_cmd), "%s\r\n", argv[1]);
        ret = modem_at_cmd_wait_rsp(&modem_at_handle, at_cmd, rsp_list, rsp_num, timeout);
        if (ret < MODEM_OK) {
            LOG_SIMPLE("modem at failed(ret = %d)!", ret);
            goto modem_at_cmd_end;
        }
        for (i = 0; i < ret; i++) {
            LOG_SIMPLE("rsp[%d] = %s", i, rsp_list[i]);
        }
        ret = MODEM_OK;

modem_at_cmd_end:
        for (i = 0; i < rsp_num; i++) {
            if (rsp_list[i] != NULL) {
                vPortFree(rsp_list[i]);
                rsp_list[i] = NULL;
            }
        }
        return ret;
    } else if (strcmp(argv[1], "restart_network") == 0) {
        ret = modem_device_restart_network();
        if (ret != MODEM_OK) {
            LOG_SIMPLE("modem restart network failed(ret = %d)!", ret);
            return -1;
        }
        LOG_SIMPLE("modem restart network success!");
        return 0;
    }
    return -1;
}

debug_cmd_reg_t modem_cmd_table[] = {
    {"modem", "modem test", modem_cmd_deal_cmd},
};

void modem_device_register(void)
{
    debug_cmdline_register(modem_cmd_table, sizeof(modem_cmd_table) / sizeof(modem_cmd_table[0]));
}
