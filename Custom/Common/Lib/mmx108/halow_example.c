#include <stdio.h>

#include "stm32n6xx_hal.h"
#include "cmsis_os2.h"


#include "mmipal.h"
#include "mmwlan.h"
#include "mmhal.h"
#include "main.h"
#include "lwip/netif.h"
#if !defined(MMHAL_WLAN_USE_SOFT_SPI)
#include "spi.h"
#endif /* HAL SPI6 (default) or soft SPI */

/*******************************************************************************
 * Definitions
 ******************************************************************************/


#define MORSE_DEFAULT_IP "192.168.1.199"
#define MORSE_DEFAULT_NETMASK "255.255.255.0"
#define MORSE_DEFAULT_GW "192.168.1.1"

#define MORSE_DEFAULT_SSID "Gateway_FC209B_HaLow"
#define MORSE_DEFAULT_PASSPHRASE ""

#if !defined(MMHAL_WLAN_USE_SOFT_SPI)
extern SPI_HandleTypeDef hspi6;
#endif

extern const struct mmwlan_s1g_channel_list s1g_channel_list_US;

struct mmosal_semb *g_wifi_link_established = NULL;

extern struct netif *mmipal_get_lwip_netif(void);

/*******************************************************************************
 * Code
 ******************************************************************************/
static void mm_halow_link_status_callback(const struct mmipal_link_status *link_status)
{
    if (link_status->link_state == MMIPAL_LINK_UP)
    {
        printf("Link is up!\r\nIP: %s, Netmask: %s, Gateway: %s\r\n", link_status->ip_addr, link_status->netmask, link_status->gateway);

        struct netif *netif = mmipal_get_lwip_netif();
        netif_set_default(netif);

        mmosal_semb_give(g_wifi_link_established);
    }
    else
    {		
        printf("Link is down!\r\n");
    }
}

static void mm_halow_status_callback(enum mmwlan_sta_state sta_state)
{
    switch (sta_state)
    {
    case MMWLAN_STA_DISABLED:
        printf("\r\nWLAN STA disabled\r\n");
        break;

    case MMWLAN_STA_CONNECTING:
        printf("\r\nWLAN STA connecting\r\n");
        break;

    case MMWLAN_STA_CONNECTED:
        printf("\r\nWLAN STA connected\r\n");
        break;
    }
}

static void mm_halow_init()
{
	struct mmipal_init_args ip_init_args;
	
    MMOSAL_ASSERT(g_wifi_link_established == NULL);
    g_wifi_link_established = mmosal_semb_create("link_established");
   
    /* Initialize MMWLAN interface */
    mmwlan_init();
	
    mmwlan_set_channel_list(&s1g_channel_list_US);
    
    /* Boot the WLAN interface so that we can retrieve the firmware version. */
    struct mmwlan_boot_args boot_args = MMWLAN_BOOT_ARGS_INIT;
    (void)mmwlan_boot(&boot_args);
	
    /* Initialize IP stack. */
    memset(&ip_init_args, 0, sizeof(struct mmipal_init_args));
	ip_init_args.mode = MMIPAL_STATIC;
	strcpy(ip_init_args.ip_addr, MORSE_DEFAULT_IP);
	strcpy(ip_init_args.netmask, MORSE_DEFAULT_NETMASK);
	strcpy(ip_init_args.gateway_addr, MORSE_DEFAULT_GW);
    if (mmipal_init(&ip_init_args) != MMIPAL_SUCCESS)
    {
        printf("Error initializing network interface.\n");
        MMOSAL_ASSERT(false);
    }
	
    mmipal_set_link_status_callback(mm_halow_link_status_callback);
}

static void mm_halow_start()
{
    enum mmwlan_status status;
    struct mmwlan_sta_args sta_args = MMWLAN_STA_ARGS_INIT;

	strcpy((char *)sta_args.ssid, MORSE_DEFAULT_SSID);
	sta_args.ssid_len = strlen(MORSE_DEFAULT_SSID);
	sta_args.security_type = MMWLAN_OPEN;
	strcpy((char *)sta_args.passphrase, MORSE_DEFAULT_PASSPHRASE);
	sta_args.passphrase_len = strlen(MORSE_DEFAULT_PASSPHRASE); 
	
    mmwlan_set_power_save_mode(MMWLAN_PS_DISABLED);

    printf("\r\nAttempting to connect to %s ", sta_args.ssid);
    if (sta_args.security_type == MMWLAN_SAE)
    {
        printf("with passphrase %s", sta_args.passphrase);
    }
    printf("\r\n");

    status = mmwlan_sta_enable(&sta_args, mm_halow_status_callback);
    MMOSAL_ASSERT(status == MMWLAN_SUCCESS);

    /* Wait for link status callback.
    * Use a binary semaphore to block us until Link is up.
    */
    mmosal_semb_wait(g_wifi_link_established, UINT32_MAX);

    /* Wi-Fi link is now established, return to caller */
}

void mm_halow_gpios_init()
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	__HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
	
	/*Configure GPIO pins : MM_HALOW_RESET_Pin */
	GPIO_InitStruct.Pin = MM_HALOW_RESET_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_PULLUP;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
	HAL_GPIO_Init(MM_HALOW_RESET_GPIO_Port, &GPIO_InitStruct);
	//RESET output HIGH
	HAL_GPIO_WritePin(MM_HALOW_RESET_GPIO_Port, MM_HALOW_RESET_Pin, GPIO_PIN_RESET);

	/*Configure GPIO pins : MM_HALOW_WAKE_Pin */
	GPIO_InitStruct.Pin = MM_HALOW_WAKE_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_PULLUP;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
	HAL_GPIO_Init(MM_HALOW_WAKE_GPIO_Port, &GPIO_InitStruct);
	//WAKE output HIGH
	HAL_GPIO_WritePin(MM_HALOW_WAKE_GPIO_Port, MM_HALOW_WAKE_Pin, GPIO_PIN_SET);

	/*Configure GPIO pin : MM_HALOW_SPI_IRQ_Pin */
	GPIO_InitStruct.Pin = MM_HALOW_SPI_IRQ_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
	GPIO_InitStruct.Pull = GPIO_PULLUP;
	HAL_GPIO_Init(MM_HALOW_SPI_IRQ_GPIO_Port, &GPIO_InitStruct);
	HAL_GPIO_WritePin(MM_HALOW_SPI_IRQ_GPIO_Port, MM_HALOW_SPI_IRQ_Pin, GPIO_PIN_SET);
	/* EXTI interrupt init*/
	HAL_NVIC_SetPriority(EXTI4_IRQn, 5, 0);
	HAL_NVIC_EnableIRQ(EXTI4_IRQn);

	/*Configure GPIO pin : MM_HALOW_BUSY_Pin */
	GPIO_InitStruct.Pin = MM_HALOW_BUSY_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
	GPIO_InitStruct.Pull = GPIO_PULLDOWN;
	HAL_GPIO_Init(MM_HALOW_BUSY_GPIO_Port, &GPIO_InitStruct);
	HAL_GPIO_WritePin(MM_HALOW_BUSY_GPIO_Port, MM_HALOW_BUSY_Pin, GPIO_PIN_RESET);
	/* EXTI interrupt init*/
	HAL_NVIC_SetPriority(EXTI15_IRQn, 6, 0);
	HAL_NVIC_EnableIRQ(EXTI15_IRQn);
}

void mm_halow_component_start(void *arg)
{	
	mm_halow_gpios_init();

#if defined(MMHAL_WLAN_USE_SOFT_SPI)
	/* GPIO bit-bang SPI: pins configured in mmhal_wlan_init() -> mm_soft_spi_init() */
#else
	MX_SPI6_Init();
#endif

	mmhal_init();
	
	mm_halow_init();
	
	mm_halow_start();

	//return 0;
}

