/**
 * @file    usb_glue_stm32n6.c
 * @brief   CherryUSB DWC2 glue for STM32N657 (NE301 board).
 *
 * Upstream usb_glue_st.c has no N6 variant, so this file provides everything
 * the DWC2 host/device drivers expect from a glue layer:
 *   - dwc2_get_user_params()  : per-controller FIFO/GCCFG parameters
 *   - usb_hc_low_level_init/deinit() : USB2_OTG_HS host power/clocks/PHY/NVIC
 *   - usb_dc_low_level_init/deinit() : USB1_OTG_HS device power/clocks/PHY/NVIC
 *   - USB{1,2}_OTG_HS_IRQHandler()   : N6 vector names -> CherryUSB ISRs
 *   - usb_dcache_*()          : D-cache maintenance (App runs with D-cache on)
 *
 * The PHY/clock bring-up sequence is ported verbatim from the previous
 * USBX HAL_HCD_MspInit() implementation (Appli/Core/Src/usb_otg.c, removed
 * with the USBX stack) - that sequence was the only N6-validated one.
 *
 * Note: upstream still has no isochronous support in the DWC2 *host* driver
 * (ISO URBs time out), so host UVC must wait for upstream or an in-tree
 * implementation. Device-side ISO works.
 */
#include <stdint.h>
#include <string.h>
#include "stm32n6xx_hal.h"
#include "usbh_core.h"
#include "usbd_core.h"
#include "usb_dwc2_param.h"

/* Vector handlers exported by the DWC2 drivers. */
extern void USBH_IRQHandler(uint8_t busid);
extern void USBD_IRQHandler(uint8_t busid);
/* osal bootstrap (ThreadX-specific, not declared in usb_osal.h). */
extern void usb_osal_init(uint8_t *mem, uint32_t mem_size);

/* Set while the corresponding low-level init has run; keeps the N6 vectors
 * from calling into an un-initialized controller (e.g. spurious IRQ). */
static volatile uint8_t g_host2_inited;
static volatile uint8_t g_dc1_inited;

void dwc2_get_user_params(uint32_t reg_base, struct dwc2_user_params *params)
{
    if (reg_base == (uint32_t)USB1_OTG_HS_BASE) {
        /* USB1: device role (composite RNDIS NIC + CDC-ACM console).
         * UTMI 16-bit internal HS PHY, internal buffer DMA. */
        params->phy_type = DWC2_PHY_TYPE_PARAM_UTMI;
        params->phy_utmi_width = 16;
        params->device_dma_enable = true;
        params->device_dma_desc_enable = false;
        /* HS requires rx >= (5 + 8 + 512/4 + 1 + 2*8 + 1) = 159 dwords
         * (usb_dc_dwc2.c assert). N6 DFIFO depth = 952 dwords (GHWCFG3).
         * TX FIFOs: EP0 16, RNDIS data-in 0x81 = 128 (512B), RNDIS notify
         * 0x83 = 16, CDC data-in 0x85 = 128, CDC notify 0x86 = 16. */
        params->device_rx_fifo_size = 176;
        params->device_tx_fifo_size[0] = 16;  /* 64 byte, EP0 IN   */
        params->device_tx_fifo_size[1] = 128; /* 512 byte, EP 0x81 */
        params->device_tx_fifo_size[2] = 0;
        params->device_tx_fifo_size[3] = 16;  /* 64 byte,  EP 0x83 */
        params->device_tx_fifo_size[4] = 0;
        params->device_tx_fifo_size[5] = 128; /* 512 byte, EP 0x85 */
        params->device_tx_fifo_size[6] = 16;  /* 64 byte,  EP 0x86 */
        params->device_gccfg = ((1 << 23) | (1 << 24)); /* VBVALOVAL | VBVALEXTOEN */
        params->host_dma_desc_enable = false;
        params->host_rx_fifo_size = 0;
        params->host_nperio_tx_fifo_size = 0;
        params->host_perio_tx_fifo_size = 0;
        params->host_gccfg = 0;
        params->b_session_valid_override = false;
        params->total_fifo_size = 0; /* autodetect from GHWCFG3 */
    } else {
        /* USB2: host role (4G modem uplink). Same IP generation as H7RS:
         * UTMI 16-bit internal HS PHY, internal DMA, host pulldowns on. */
        params->phy_type = DWC2_PHY_TYPE_PARAM_UTMI;
        params->phy_utmi_width = 16;
        params->device_dma_enable = true;
        params->device_dma_desc_enable = false;
        params->device_rx_fifo_size = 0;
        memset(params->device_tx_fifo_size, 0, sizeof(params->device_tx_fifo_size));
        params->device_gccfg = 0;
        params->host_dma_desc_enable = false;
        /* 896 dwords total; must stay <= GHWCFG3 DFIFO depth (N6: 1024). */
        params->host_rx_fifo_size = 512;
        params->host_nperio_tx_fifo_size = 128; /* 512 byte */
        params->host_perio_tx_fifo_size = 256;  /* 1024 byte */
        params->host_gccfg = (1 << 25);         /* USB_OTG_GCCFG_PULLDOWNEN */
        params->b_session_valid_override = false;
        params->total_fifo_size = 0; /* autodetect from GHWCFG3 */
    }
}

/*
 * VDDUSB is the shared PHY supply for BOTH OTG instances: powering it down
 * from one instance's deinit kills the other's PHY (seen as: ue (USB2 host)
 * deinit took the PC-facing USB1 device offline). Reference-count it.
 */
static uint8_t g_otg_vddusb_users;

static void usb_otg_vddusb_acquire(uint32_t otg_base)
{
    (void)otg_base;
    if (g_otg_vddusb_users++ != 0) {
        return; /* already powered for the other instance */
    }

    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWREx_EnableVddUSBVMEN();
    uint32_t ready_poll = 1000000;
    while (__HAL_PWR_GET_FLAG(PWR_FLAG_USB33RDY) == 0U) {
        if (--ready_poll == 0U) {
            printf("[USB] VDD33USB never became ready\r\n");
            g_otg_vddusb_users--; /* not acquired after all */
            return;
        }
    }
    HAL_PWREx_EnableVddUSB();
}

static void usb_otg_vddusb_release(uint32_t otg_base)
{
    (void)otg_base;
    if (g_otg_vddusb_users == 0) {
        return;
    }
    if (--g_otg_vddusb_users == 0) {
        HAL_PWREx_DisableVddUSB(); /* last user out: really power down */
    }
}

/**
 * @brief N6 OTG controller + internal HS PHY bring-up (per instance).
 *
 * Sequence (validated on N6 hardware with the former USBX stack):
 *  1. VDD33USB voltage monitor (VMEN) -> wait USB33RDY -> enable VDDUSB
 *  2. RCC periph clocks: OTG + PHY reference = HSE_DIRECT
 *  3. AHB5 GRSTCLRP/GRSTCLKP toggle plus OTG/PHY force reset, HSE div2 select
 *  4. USBPHYC_CR: PLL1 lock bypass | monocom mode | FSEL 24MHz | enable
 *  5. release PHY reset, release OTG reset, enable PHY clock
 *  6. NVIC priority 7 (same as the former USBX deployment)
 */
static int usb_otg_phy_clock_init(uint32_t otg_base, uint32_t ahb5_reset_msk)
{
    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

    usb_otg_vddusb_acquire(otg_base);

    if (otg_base == (uint32_t)USB1_OTG_HS_BASE) {
        PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USBOTGHS1;
        PeriphClkInitStruct.UsbOtgHs1ClockSelection = RCC_USBPHY1CLKSOURCE_HSE_DIRECT;
        if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK) {
            return -1;
        }
        PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USBPHY1;
        PeriphClkInitStruct.UsbPhy1ClockSelection = RCC_USBPHY1CLKSOURCE_HSE_DIRECT;
        if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK) {
            return -1;
        }
    } else {
        PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USBOTGHS2;
        PeriphClkInitStruct.UsbOtgHs2ClockSelection = RCC_USBPHY2CLKSOURCE_HSE_DIRECT;
        if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK) {
            return -1;
        }
        PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USBPHY2;
        PeriphClkInitStruct.UsbPhy2ClockSelection = RCC_USBPHY2CLKSOURCE_HSE_DIRECT;
        if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK) {
            return -1;
        }
    }

    __HAL_RCC_GPIOA_CLK_ENABLE();

    LL_AHB5_GRP1_ForceReset(ahb5_reset_msk);
    if (otg_base == (uint32_t)USB1_OTG_HS_BASE) {
        __HAL_RCC_USB1_OTG_HS_FORCE_RESET();
        __HAL_RCC_USB1_OTG_HS_PHY_FORCE_RESET();
    } else {
        __HAL_RCC_USB2_OTG_HS_FORCE_RESET();
        __HAL_RCC_USB2_OTG_HS_PHY_FORCE_RESET();
    }

    LL_RCC_HSE_SelectHSEDiv2AsDiv2Clock();
    LL_AHB5_GRP1_ReleaseReset(ahb5_reset_msk);

    if (otg_base == (uint32_t)USB1_OTG_HS_BASE) {
        __HAL_RCC_USB1_OTG_HS_CLK_ENABLE();
    } else {
        __HAL_RCC_USB2_OTG_HS_CLK_ENABLE();
    }

    /* A few clock cycles are required before touching USBPHYC registers */
    HAL_Delay(1);

    USB_HS_PHYC_GlobalTypeDef *phyc = (otg_base == (uint32_t)USB1_OTG_HS_BASE) ? USB1_HS_PHYC : USB2_HS_PHYC;
    phyc->USBPHYC_CR &= ~(0x7 << 0x4);
    phyc->USBPHYC_CR |= (0x1 << 16) | /* PLL1 lock bypass                       */
                        (0x2 << 4) |  /* FSEL: 24 MHz crystal                   */
                        (0x1 << 2) |  /* monocom output mode                    */
                        0x1U;         /* enable                                 */

    if (otg_base == (uint32_t)USB1_OTG_HS_BASE) {
        __HAL_RCC_USB1_OTG_HS_PHY_RELEASE_RESET();
    } else {
        __HAL_RCC_USB2_OTG_HS_PHY_RELEASE_RESET();
    }

    HAL_Delay(1);

    if (otg_base == (uint32_t)USB1_OTG_HS_BASE) {
        __HAL_RCC_USB1_OTG_HS_RELEASE_RESET();
        __HAL_RCC_USB1_OTG_HS_PHY_CLK_ENABLE();
        HAL_NVIC_SetPriority(USB1_OTG_HS_IRQn, 7U, 0U);
        HAL_NVIC_EnableIRQ(USB1_OTG_HS_IRQn);
    } else {
        __HAL_RCC_USB2_OTG_HS_RELEASE_RESET();
        __HAL_RCC_USB2_OTG_HS_PHY_CLK_ENABLE();
        /* Host ISO cadence is 125us and the channel re-arm happens in the
         * completion ISR: any same/lower-priority IRQ blocking it >125us
         * loses a microframe (mid-frame payload loss = torn image bands,
         * undetected by SOI/EOI checks). prio 4 preempts the GPDMA/HPDMA/
         * EXTI crowd (5..7) that fires heavily while Wi-Fi streams; the
         * host ISR is a few us, so it cannot hurt them back. */
        HAL_NVIC_SetPriority(USB2_OTG_HS_IRQn, 4U, 0U);
        HAL_NVIC_EnableIRQ(USB2_OTG_HS_IRQn);
    }
    return 0;
}

static void usb_otg_phy_clock_deinit(uint32_t otg_base)
{
    if (otg_base == (uint32_t)USB1_OTG_HS_BASE) {
        HAL_NVIC_DisableIRQ(USB1_OTG_HS_IRQn);
        __HAL_RCC_USB1_OTG_HS_CLK_DISABLE();
        __HAL_RCC_USB1_OTG_HS_PHY_CLK_DISABLE();
    } else {
        HAL_NVIC_DisableIRQ(USB2_OTG_HS_IRQn);
        __HAL_RCC_USB2_OTG_HS_CLK_DISABLE();
        __HAL_RCC_USB2_OTG_HS_PHY_CLK_DISABLE();
    }
    /* VDDUSB is shared: only the last instance out may power it down. */
    usb_otg_vddusb_release(otg_base);
}

/* ============================ Host (USB2) ============================ */

void usb_hc_low_level_init(struct usbh_bus *bus)
{
    if (usb_otg_phy_clock_init(bus->hcd.reg_base, 0x01000000U) != 0) {
        printf("[USB] USB2 host low-level init failed\r\n");
        return;
    }
    g_host2_inited = 1;
}

void usb_hc_low_level_deinit(struct usbh_bus *bus)
{
    g_host2_inited = 0;
    usb_otg_phy_clock_deinit(bus->hcd.reg_base);
}

/* =========================== Device (USB1) =========================== */

void usb_dc_low_level_init(uint8_t busid)
{
    if (usb_otg_phy_clock_init(g_usbdev_bus[busid].reg_base, 0x00800000U) != 0) {
        printf("[USB] USB1 device low-level init failed\r\n");
        return;
    }
    g_dc1_inited = 1;
}

void usb_dc_low_level_deinit(uint8_t busid)
{
    g_dc1_inited = 0;
    usb_otg_phy_clock_deinit(g_usbdev_bus[busid].reg_base);
}

/* ===================== IRQ vectors (N6 names) ======================= */

void USB2_OTG_HS_IRQHandler(void)
{
    if (g_host2_inited) {
        USBH_IRQHandler(0);
    }
}

void USB1_OTG_HS_IRQHandler(void)
{
    if (g_dc1_inited) {
        USBD_IRQHandler(0);
    }
}

/* ===================== D-cache maintenance ========================== */

#ifdef CONFIG_USB_DCACHE_ENABLE
void usb_dcache_clean(uintptr_t addr, size_t size)
{
    SCB_CleanDCache_by_Addr((void *)addr, size);
}

void usb_dcache_invalidate(uintptr_t addr, size_t size)
{
    SCB_InvalidateDCache_by_Addr((void *)addr, size);
}

void usb_dcache_flush(uintptr_t addr, size_t size)
{
    SCB_CleanInvalidateDCache_by_Addr((void *)addr, size);
}
#endif

/* Busy-wait ms delay used by the DWC2 device driver during core reset
 * (may run before the scheduler is available; mirrors upstream glue). */
extern uint32_t SystemCoreClock;
void usbd_dwc2_delay_ms(uint8_t ms)
{
    uint32_t count = SystemCoreClock / 1000 * ms;
    while (count--) {
        __asm volatile("nop");
    }
}

uint32_t usbd_dwc2_get_system_clock(void)
{
    return SystemCoreClock;
}
