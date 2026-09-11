/**
 * @file board_hw.h
 * @brief Board hardware revision band detection.
 *
 * The PE9 strap separates revision bands, not exact revisions: boards
 * v1.3 or newer strap PE9 high, boards v1.2 or older leave it pulled low.
 * The band decides the HaLow WAKE pin (<= v1.2: PB2, >= v1.3: PD9).
 */
#ifndef _BOARD_HW_H_
#define _BOARD_HW_H_

#include "stm32n6xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BOARD_HW_V1_2_OR_OLDER = 0,
    BOARD_HW_V1_3_OR_NEWER = 1,
} board_hw_version_t;

/**
 * Sample the PE9 revision strap (pull-down input, debounced, osDelay-based)
 * exactly once and release the pin — boards v1.3 or newer use PE9 for
 * another module, so it must never be re-sampled afterwards. Called from
 * driver_core_init(); thread context required.
 */
void board_hw_init(void);

board_hw_version_t board_hw_version(void);

/** "1.3.0" / "1.2.0" for logs (strap bands, reported as 3-part numbers to
 * distinguish from the factory-written exact revision). */
const char *board_hw_version_str(void);

/** HaLow WAKE pin for the detected band (<= v1.2: PB2, >= v1.3: PD9). */
GPIO_TypeDef *board_hw_halow_wake_port(void);
uint16_t board_hw_halow_wake_pin(void);

#ifdef __cplusplus
}
#endif

#endif /* _BOARD_HW_H_ */
