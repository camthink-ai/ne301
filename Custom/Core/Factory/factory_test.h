/**
 * @file factory_test.h
 * @brief NE301 Factory Test Module
 * @details Hardware self-test and factory configuration interface
 */

#ifndef _FACTORY_TEST_H_
#define _FACTORY_TEST_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== Definitions ==================== */

#define FACTORY_TEST_VERSION    "1.0.0"

/* Test result codes */
typedef enum {
    FACTORY_TEST_PASS = 0,
    FACTORY_TEST_FAIL = -1,
    FACTORY_TEST_SKIP = -2,
    FACTORY_TEST_TIMEOUT = -3,
    FACTORY_TEST_NOT_SUPPORTED = -4
} factory_test_result_t;

/* Test item flags */
typedef enum {
    FACTORY_TEST_PSRAM      = (1 << 0),
    FACTORY_TEST_FLASH      = (1 << 1),
    FACTORY_TEST_NVS        = (1 << 2),
    FACTORY_TEST_CAMERA     = (1 << 3),
    FACTORY_TEST_WIFI       = (1 << 4),
    FACTORY_TEST_ETHERNET   = (1 << 5),
    FACTORY_TEST_PIR        = (1 << 6),
    FACTORY_TEST_RTC        = (1 << 7),
    FACTORY_TEST_LED        = (1 << 8),
    FACTORY_TEST_U0_MODULE  = (1 << 9),
    FACTORY_TEST_NPU        = (1 << 10),
    FACTORY_TEST_ALL        = 0xFFFFFFFF
} factory_test_item_t;

/* Factory configuration structure */
typedef struct {
    char serial_number[32];     // Device serial number
    char hw_version[16];        // Hardware version
    char mfg_date[16];          // Manufacturing date YYYY-MM-DD
    char factory_fw_ver[32];    // Factory firmware version
    uint8_t mac_address[6];     // MAC address
    uint32_t mfg_timestamp;     // Manufacturing timestamp
    uint8_t test_passed;        // Factory test passed flag
    uint8_t reserved[3];
} factory_config_t;

/* Test report structure */
typedef struct {
    uint32_t test_mask;         // Tests performed
    uint32_t pass_mask;         // Tests passed
    uint32_t fail_mask;         // Tests failed
    uint32_t skip_mask;         // Tests skipped
    uint32_t test_duration_ms;  // Total test duration
    char error_message[128];    // Last error message
} factory_test_report_t;

/* ==================== Factory Test API ==================== */

/**
 * @brief Initialize factory test module
 * @return 0 on success, negative on error
 */
int factory_test_init(void);

/**
 * @brief Run factory tests
 * @param test_mask Bitmask of tests to run (FACTORY_TEST_xxx)
 * @param report Pointer to store test report
 * @return 0 if all tests pass, negative if any fail
 */
int factory_test_run(uint32_t test_mask, factory_test_report_t *report);

/**
 * @brief Run single test item
 * @param item Test item to run
 * @return Test result code
 */
factory_test_result_t factory_test_single(factory_test_item_t item);

/**
 * @brief Run full hardware self-test
 * @param report Pointer to store test report (can be NULL)
 * @return 0 if all tests pass, negative if any fail
 */
int factory_test_full(factory_test_report_t *report);

/**
 * @brief Print test report
 * @param report Pointer to test report
 */
void factory_test_print_report(const factory_test_report_t *report);

/* ==================== Factory Configuration API ==================== */

/**
 * @brief Read factory configuration from NVS
 * @param config Pointer to store configuration
 * @return 0 on success, negative on error
 */
int factory_config_read(factory_config_t *config);

/**
 * @brief Write factory configuration to NVS
 * @param config Pointer to configuration
 * @return 0 on success, negative on error
 */
int factory_config_write(const factory_config_t *config);

/**
 * @brief Check if device has valid factory configuration
 * @return true if valid, false otherwise
 */
bool factory_config_is_valid(void);

/**
 * @brief Generate serial number from MCU UID
 * @param buffer Output buffer
 * @param size Buffer size
 * @return 0 on success, negative on error
 */
int factory_generate_serial_number(char *buffer, size_t size);

/**
 * @brief Generate MAC address from MCU UID
 * @param mac Output MAC address (6 bytes)
 * @return 0 on success, negative on error
 */
int factory_generate_mac_address(uint8_t *mac);

/**
 * @brief Get the factory-burned WiFi MAC address
 * @param mac Output buffer (6 bytes)
 * @return 0 if a valid unicast MAC is burned in the FACTORY partition,
 *         -1 if not burned (system falls back to the chip-reported MAC)
 */
int factory_mac_get_burned(uint8_t *mac);

/**
 * @brief Mark factory test as passed
 * @return 0 on success, negative on error
 */
int factory_test_mark_passed(void);

/**
 * @brief Check if factory test has passed
 * @return true if passed, false otherwise
 */
bool factory_test_has_passed(void);

/* ==================== CLI Command Registration ==================== */

/**
 * @brief Register factory test CLI commands
 */
void factory_test_register_commands(void);

#ifdef __cplusplus
}
#endif

#endif /* _FACTORY_TEST_H_ */

