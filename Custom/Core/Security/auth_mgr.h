/**
 * @file auth_mgr.h
 * @brief Authentication Manager Header
 * @details Simple single admin authentication system for AI Camera
 */

#ifndef AUTH_MGR_H
#define AUTH_MGR_H

#include "aicam_types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== Constants ==================== */

#define AUTH_MAX_PASSWORD_LEN       64      // Maximum password length  
#define AUTH_MAX_SESSIONS          4       // Maximum concurrent sessions
#define AUTH_SESSION_TIMEOUT_MS    (30 * 60 * 1000)  // 30 minutes session timeout
#define AUTH_PASSWORD_HASH_LEN     32      // Password hash length
#define AUTH_MGR_MAGIC_NUMBER       0xABCDEF01
#define AUTH_ADMIN_USERNAME         "admin"
#define AUTH_SESSION_ID_MIN         1000
#define AUTH_SESSION_ID_MAX         9999
#define AUTH_DEFAULT_PASSWORD       "hicamthink"
#define AUTH_CREDENTIAL_RECORD_MAX  128
#define AUTH_CREDENTIAL_SALT_LEN    16
#define AUTH_CREDENTIAL_KEY_LEN     32
#define AUTH_PBKDF2_ITERATIONS      4096u
#define AUTH_CREDENTIAL_RECORD_PREFIX "A1:"
#define AUTH_BASIC_DECODE_MAX_LEN   AUTH_MAX_PASSWORD_LEN
#define AUTH_BASIC_DECODE_BUF_LEN   (AUTH_BASIC_DECODE_MAX_LEN + 1)

/* ==================== Types and Enums ==================== */

/**
 * @brief Authentication result codes
 */
typedef enum {
    AUTH_RESULT_SUCCESS = 0,           // Authentication successful
    AUTH_RESULT_INVALID_CREDENTIALS,   // Invalid username or password
    AUTH_RESULT_SESSION_EXPIRED,       // Session has expired
    AUTH_RESULT_SESSION_NOT_FOUND,     // Session not found
    AUTH_RESULT_MAX_SESSIONS_REACHED,  // Maximum sessions reached
    AUTH_RESULT_INVALID_PARAM,         // Invalid parameter
    AUTH_RESULT_INTERNAL_ERROR         // Internal error
} auth_result_t;

/**
 * @brief Authentication session information
 */
typedef struct {
    uint32_t session_id;                               // Unique session ID
    uint64_t login_time;                               // Login timestamp
    uint64_t last_activity;                            // Last activity timestamp
    uint64_t expires_at;                               // Session expiration time
    aicam_bool_t is_active;                            // Session active status
} auth_session_t;

typedef struct {
    aicam_bool_t authenticated;
    aicam_bool_t default_password_active;
    aicam_bool_t high_privilege_allowed;
} auth_verdict_t;

/* ==================== Configuration Macros ==================== */

#define AUTH_MGR_CONFIG_DEFAULT() \
    { \
        .session_timeout_ms = AUTH_SESSION_TIMEOUT_MS, \
        .enable_session_timeout = AICAM_TRUE, \
        .admin_password = AUTH_DEFAULT_PASSWORD \
    }

/* ==================== Public API Functions ==================== */

/**
 * @brief Initialize authentication manager
 * @param config Configuration parameters (NULL for default)
 * @return aicam_result_t Operation result
 */
aicam_result_t auth_mgr_init();

/**
 * @brief Deinitialize authentication manager
 * @return aicam_result_t Operation result
 */
aicam_result_t auth_mgr_deinit(void);

/**
 * @brief Verify password
 * @param password Password string
 * @return aicam_bool_t Result
 */
aicam_bool_t auth_mgr_verify_password(const char *password);

/**
 * @brief Admin login with username and password
 * @param username Username string (must be "admin")
 * @param password Password string
 * @param session_id Output session ID
 * @return auth_result_t Authentication result
 */
auth_result_t auth_mgr_login(const char *username, 
                            const char *password,
                            uint32_t *session_id);

/**
 * @brief Admin logout
 * @param session_id Session ID to logout
 * @return auth_result_t Operation result
 */
auth_result_t auth_mgr_logout(uint32_t session_id);

/**
 * @brief Validate session
 * @param session_id Session ID to validate
 * @param session Output session information (optional)
 * @return auth_result_t Validation result
 */
auth_result_t auth_mgr_validate_session(uint32_t session_id, auth_session_t *session);

/**
 * @brief Change admin password
 * @param session_id Valid session ID
 * @param old_password Current password
 * @param new_password New password
 * @return auth_result_t Operation result
 */
auth_result_t auth_mgr_change_password(const char *password);

void auth_mgr_revoke_all_sessions(void);

aicam_bool_t auth_mgr_credential_is_record(const char *value);

aicam_result_t auth_mgr_credential_record_encode(const char *password,
                                                 char *record,
                                                 size_t record_size);

aicam_bool_t auth_mgr_credential_record_verify(const char *record,
                                               const char *password);

aicam_bool_t auth_mgr_default_password_active(void);

uint32_t auth_mgr_credential_epoch(void);

auth_verdict_t auth_mgr_authorize(const char *username, const char *password);

/* ==================== Utility Functions ==================== */

/**
 * @brief Convert auth_result_t to string
 * @param result Authentication result code
 * @return const char* Result description string
 */
const char* auth_result_to_string(auth_result_t result);

#ifdef __cplusplus
}
#endif

#endif // AUTH_MGR_H 
