/**
 * @file auth_mgr.c
 * @brief Authentication Manager Implementation
 * @details Simple single admin authentication system implementation
 */

#include "auth_mgr.h"
#include "debug.h"
#include "json_config_mgr.h"
#include <string.h>
#include <stdlib.h>
#include "mbedtls/md.h"
#include "mbedtls/sha256.h"
#include "cmsis_os2.h"

/* ==================== Internal Data Structures ==================== */

typedef struct {
    uint32_t magic_number;
    aicam_bool_t is_initialized;
    aicam_bool_t credential_trusted;
    aicam_bool_t has_credential_record;
    aicam_bool_t legacy_upgrade_attempted;
    aicam_bool_t verify_cache_valid;
    uint8_t verify_cache_digest[AUTH_CREDENTIAL_KEY_LEN];
    aicam_bool_t default_active_valid;
    aicam_bool_t default_active_result;
    uint32_t credential_epoch;
    auth_mgr_config_t config;
    auth_session_t sessions[AUTH_MAX_SESSIONS];
    uint32_t session_count;
    uint32_t next_session_id;
    void *mutex;
} auth_mgr_state_t;

/* ==================== Global Variables ==================== */

static auth_mgr_state_t g_auth_mgr = {0};

/* ==================== Thread Safety Macros ==================== */
#define AUTH_MUTEX_CREATE()         osMutexNew(NULL)
#define AUTH_MUTEX_DESTROY(mutex)   osMutexDelete(mutex)
#define AUTH_MUTEX_LOCK(mutex)      osMutexAcquire(mutex, osWaitForever)
#define AUTH_MUTEX_UNLOCK(mutex)    osMutexRelease(mutex)


/* ==================== Internal Function Declarations ==================== */

static auth_session_t* auth_mgr_find_session(uint32_t session_id);
static auth_session_t* auth_mgr_allocate_session(void);
static void auth_mgr_cleanup_expired_sessions(void);
static uint32_t auth_mgr_generate_session_id(void);
static uint64_t auth_mgr_get_timestamp(void);
static aicam_bool_t auth_mgr_verify_locked(const char *password);
static void auth_mgr_password_digest(const char *password, uint8_t out[AUTH_CREDENTIAL_KEY_LEN]);
static void auth_mgr_sync_credential_state(const char *password);
static void auth_mgr_upgrade_legacy_credential(void);
static int auth_mgr_fill_salt(uint8_t *salt, size_t salt_len);
static int auth_mgr_constant_time_compare(const uint8_t *a, const uint8_t *b, size_t len);

/* ==================== Public API Implementation ==================== */

aicam_result_t auth_mgr_init()
{
    if (g_auth_mgr.is_initialized) {
        LOG_CORE_WARN("Authentication manager already initialized");
        return AICAM_OK;
    }
    
    LOG_CORE_INFO("Initializing Authentication Manager");

    // Initialize state structure
    memset(&g_auth_mgr, 0, sizeof(auth_mgr_state_t));
    g_auth_mgr.magic_number = AUTH_MGR_MAGIC_NUMBER;

    // Set configuration and load password from json_config_mgr
    auth_mgr_config_t default_config = AUTH_MGR_CONFIG_DEFAULT();
    memcpy(&g_auth_mgr.config, &default_config, sizeof(auth_mgr_config_t));

    char config_credential[AUTH_CREDENTIAL_RECORD_MAX];
    aicam_result_t config_result = json_config_get_device_credential(config_credential, sizeof(config_credential));
    if (config_result == AICAM_OK &&
        strncmp(config_credential, AUTH_CREDENTIAL_RECORD_PREFIX, strlen(AUTH_CREDENTIAL_RECORD_PREFIX)) == 0 &&
        auth_mgr_credential_is_record(config_credential) != AICAM_TRUE)
    {
        LOG_CORE_ERROR("Admin credential looks like a record but failed to parse, fail-closed: login and password change disabled, no default fallback");
        config_result = AICAM_ERROR;
    }
    if (config_result == AICAM_OK && auth_mgr_credential_is_record(config_credential) == AICAM_TRUE) {
        strncpy(g_auth_mgr.config.admin_credential, config_credential, sizeof(g_auth_mgr.config.admin_credential) - 1);
        g_auth_mgr.config.admin_credential[sizeof(g_auth_mgr.config.admin_credential) - 1] = '\0';
        memset(g_auth_mgr.config.admin_password, 0, sizeof(g_auth_mgr.config.admin_password));
        g_auth_mgr.has_credential_record = AICAM_TRUE;
        g_auth_mgr.credential_trusted = AICAM_TRUE;
        LOG_CORE_INFO("Admin credential record loaded from configuration manager");
    } else if (config_result == AICAM_OK) {
        strncpy(g_auth_mgr.config.admin_password, config_credential, sizeof(g_auth_mgr.config.admin_password) - 1);
        g_auth_mgr.config.admin_password[sizeof(g_auth_mgr.config.admin_password) - 1] = '\0';
        memset(g_auth_mgr.config.admin_credential, 0, sizeof(g_auth_mgr.config.admin_credential));
        g_auth_mgr.has_credential_record = AICAM_FALSE;
        g_auth_mgr.credential_trusted = AICAM_TRUE;
        LOG_CORE_INFO("Legacy admin credential loaded from configuration manager, record upgrade pending first successful authentication");
    } else {
        memset(g_auth_mgr.config.admin_password, 0, sizeof(g_auth_mgr.config.admin_password));
        memset(g_auth_mgr.config.admin_credential, 0, sizeof(g_auth_mgr.config.admin_credential));
        g_auth_mgr.has_credential_record = AICAM_FALSE;
        g_auth_mgr.credential_trusted = AICAM_FALSE;
        LOG_CORE_ERROR("Admin credential not loadable from config manager(rc=%d), auth fail-closed: login and password change disabled, public default not used", config_result);
    }
    memset(config_credential, 0, sizeof(config_credential));
    // Create mutex for thread safety
    g_auth_mgr.mutex = AUTH_MUTEX_CREATE();
    if (!g_auth_mgr.mutex) {
        LOG_CORE_ERROR("Failed to create authentication manager mutex");
        return AICAM_ERROR_NO_MEMORY;
    }
    
    // Initialize session ID counter
    g_auth_mgr.next_session_id = AUTH_SESSION_ID_MIN;

    LOG_CORE_INFO("Authentication manager mutex created successfully");

    g_auth_mgr.is_initialized = AICAM_TRUE;

    LOG_CORE_INFO("Authentication Manager initialized successfully");
    LOG_CORE_INFO("Admin username: '%s'", AUTH_ADMIN_USERNAME);
    if (g_auth_mgr.credential_trusted == AICAM_TRUE && g_auth_mgr.has_credential_record != AICAM_TRUE) {
        auth_mgr_upgrade_legacy_credential();
    }

    return AICAM_OK;
}

aicam_result_t auth_mgr_deinit(void)
{
    if (!g_auth_mgr.is_initialized) {
        return AICAM_OK;
    }
    
    LOG_CORE_INFO("Deinitializing Authentication Manager");
    
    AUTH_MUTEX_LOCK(g_auth_mgr.mutex);
    
    // Logout all active sessions
    for (uint32_t i = 0; i < g_auth_mgr.session_count; i++) {
        if (g_auth_mgr.sessions[i].is_active) {
            g_auth_mgr.sessions[i].is_active = AICAM_FALSE;
        }
    }
    
    g_auth_mgr.is_initialized = AICAM_FALSE;
    
    AUTH_MUTEX_UNLOCK(g_auth_mgr.mutex);
    
    // Destroy mutex
    AUTH_MUTEX_DESTROY(g_auth_mgr.mutex);
    
    // Clear sensitive data
    memset(&g_auth_mgr, 0, sizeof(auth_mgr_state_t));
    
    LOG_CORE_INFO("Authentication Manager deinitialized");
    return AICAM_OK;
}

aicam_bool_t auth_mgr_verify_password(const char *password)
{
    if (!password) {
        return AICAM_FALSE;
    }

    if (g_auth_mgr.is_initialized != AICAM_TRUE || g_auth_mgr.credential_trusted != AICAM_TRUE) {
        return AICAM_FALSE;
    }

    if (strlen(password) > AUTH_MAX_PASSWORD_LEN) {
        return AICAM_FALSE;
    }

    AUTH_MUTEX_LOCK(g_auth_mgr.mutex);

    aicam_bool_t result = auth_mgr_verify_locked(password);
    aicam_bool_t legacy_verified = (result == AICAM_TRUE && g_auth_mgr.has_credential_record != AICAM_TRUE);

    AUTH_MUTEX_UNLOCK(g_auth_mgr.mutex);

    if (legacy_verified == AICAM_TRUE) {
        auth_mgr_upgrade_legacy_credential();
    }

    return result;
}

auth_result_t auth_mgr_login(const char *username, 
                            const char *password,
                            uint32_t *session_id)
{
    if (!g_auth_mgr.is_initialized) {
        return AUTH_RESULT_INTERNAL_ERROR;
    }
    
    if (!username || !password || !session_id) {
        return AUTH_RESULT_INVALID_PARAM;
    }
    
    if (strlen(password) > AUTH_MAX_PASSWORD_LEN) {
        return AUTH_RESULT_INVALID_PARAM;
    }
    
    // Check if username is "admin"
    if (strcmp(username, AUTH_ADMIN_USERNAME) != 0) {
        LOG_CORE_WARN("Login failed: invalid username '%s'", username);
        return AUTH_RESULT_INVALID_CREDENTIALS;
    }
    
    AUTH_MUTEX_LOCK(g_auth_mgr.mutex);

    // Cleanup expired sessions first
    auth_mgr_cleanup_expired_sessions();

    if (!auth_mgr_verify_locked(password)) {
        AUTH_MUTEX_UNLOCK(g_auth_mgr.mutex);
        LOG_CORE_WARN("Login failed: invalid password for admin");
        return AUTH_RESULT_INVALID_CREDENTIALS;
    }
    aicam_bool_t legacy_verified = (g_auth_mgr.has_credential_record != AICAM_TRUE);
    
    // Check if maximum sessions reached
    uint32_t active_sessions = 0;
    for (uint32_t i = 0; i < g_auth_mgr.session_count; i++) {
        if (g_auth_mgr.sessions[i].is_active) {
            active_sessions++;
        }
    }
    
    if (active_sessions >= AUTH_MAX_SESSIONS) {
        AUTH_MUTEX_UNLOCK(g_auth_mgr.mutex);
        LOG_CORE_ERROR("Login failed: maximum sessions reached");
        return AUTH_RESULT_MAX_SESSIONS_REACHED;
    }
    
    // Create new session
    auth_session_t *session = auth_mgr_allocate_session();
    if (!session) {
        AUTH_MUTEX_UNLOCK(g_auth_mgr.mutex);
        LOG_CORE_ERROR("Login failed: cannot allocate session");
        return AUTH_RESULT_INTERNAL_ERROR;
    }
    
    // Initialize session
    uint64_t current_time = auth_mgr_get_timestamp();
    session->session_id = auth_mgr_generate_session_id();
    session->login_time = current_time;
    session->last_activity = current_time;
    session->expires_at = current_time + g_auth_mgr.config.session_timeout_ms;
    session->is_active = AICAM_TRUE;
    
    *session_id = session->session_id;

    AUTH_MUTEX_UNLOCK(g_auth_mgr.mutex);

    LOG_CORE_INFO("Admin logged in successfully (session: %u)", *session_id);

    if (legacy_verified == AICAM_TRUE) {
        auth_mgr_upgrade_legacy_credential();
    }

    return AUTH_RESULT_SUCCESS;
}

auth_result_t auth_mgr_logout(uint32_t session_id)
{
    if (!g_auth_mgr.is_initialized) {
        return AUTH_RESULT_INTERNAL_ERROR;
    }
    
    AUTH_MUTEX_LOCK(g_auth_mgr.mutex);
    
    auth_session_t *session = auth_mgr_find_session(session_id);
    if (!session || !session->is_active) {
        AUTH_MUTEX_UNLOCK(g_auth_mgr.mutex);
        return AUTH_RESULT_SESSION_NOT_FOUND;
    }
    
    LOG_CORE_INFO("Admin logging out (session: %u)", session_id);
    
    // Deactivate session
    session->is_active = AICAM_FALSE;
    
    AUTH_MUTEX_UNLOCK(g_auth_mgr.mutex);
    
    return AUTH_RESULT_SUCCESS;
}

auth_result_t auth_mgr_validate_session(uint32_t session_id, auth_session_t *session)
{
    if (!g_auth_mgr.is_initialized) {
        return AUTH_RESULT_INTERNAL_ERROR;
    }
    
    AUTH_MUTEX_LOCK(g_auth_mgr.mutex);
    
    // Cleanup expired sessions
    auth_mgr_cleanup_expired_sessions();
    
    auth_session_t *found_session = auth_mgr_find_session(session_id);
    if (!found_session || !found_session->is_active) {
        AUTH_MUTEX_UNLOCK(g_auth_mgr.mutex);
        return AUTH_RESULT_SESSION_NOT_FOUND;
    }
    
    // Check if session has expired
    uint64_t current_time = auth_mgr_get_timestamp();
    if (g_auth_mgr.config.enable_session_timeout && 
        current_time > found_session->expires_at) {
        found_session->is_active = AICAM_FALSE;
        AUTH_MUTEX_UNLOCK(g_auth_mgr.mutex);
        LOG_CORE_INFO("Session %u expired", session_id);
        return AUTH_RESULT_SESSION_EXPIRED;
    }
    
    // Update last activity and extend expiration
    found_session->last_activity = current_time;
    found_session->expires_at = current_time + g_auth_mgr.config.session_timeout_ms;
    
    // Copy session info if requested
    if (session) {
        memcpy(session, found_session, sizeof(auth_session_t));
    }
    
    AUTH_MUTEX_UNLOCK(g_auth_mgr.mutex);
    
    return AUTH_RESULT_SUCCESS;
}

auth_result_t auth_mgr_change_password(const char *password)
{
    if (!g_auth_mgr.is_initialized) {
        return AUTH_RESULT_INTERNAL_ERROR;
    }

    if (!password) {
        return AUTH_RESULT_INVALID_PARAM;
    }

    if (strlen(password) > AUTH_MAX_PASSWORD_LEN) {
        return AUTH_RESULT_INVALID_PARAM;
    }

    if (g_auth_mgr.credential_trusted != AICAM_TRUE) {
        LOG_CORE_ERROR("Password change refused: session credential untrusted, fail-closed");
        return AUTH_RESULT_INTERNAL_ERROR;
    }

    aicam_result_t config_result = json_config_set_device_password(password);
    if (config_result != AICAM_OK) {
        LOG_CORE_ERROR("Failed to persist admin credential(rc=%d), change not applied, previous credential kept", config_result);
        if (json_config_nvs_credential_undecided() == AICAM_TRUE) {
            AUTH_MUTEX_LOCK(g_auth_mgr.mutex);
            memset(g_auth_mgr.config.admin_password, 0, sizeof(g_auth_mgr.config.admin_password));
            memset(g_auth_mgr.config.admin_credential, 0, sizeof(g_auth_mgr.config.admin_credential));
            g_auth_mgr.has_credential_record = AICAM_FALSE;
            g_auth_mgr.verify_cache_valid = AICAM_FALSE;
            memset(g_auth_mgr.verify_cache_digest, 0, sizeof(g_auth_mgr.verify_cache_digest));
            g_auth_mgr.default_active_valid = AICAM_FALSE;
            g_auth_mgr.credential_epoch++;
            g_auth_mgr.credential_trusted = AICAM_FALSE;
            for (uint32_t i = 0; i < g_auth_mgr.session_count; i++) {
                if (g_auth_mgr.sessions[i].is_active) {
                    g_auth_mgr.sessions[i].is_active = AICAM_FALSE;
                }
            }
            AUTH_MUTEX_UNLOCK(g_auth_mgr.mutex);
            LOG_CORE_ERROR("Credential state undecided, auth manager fail-closed, all sessions revoked until reset-to-default");
        }
        return AUTH_RESULT_INTERNAL_ERROR;
    }

    auth_mgr_sync_credential_state(password);
    auth_mgr_revoke_all_sessions();

    LOG_CORE_INFO("Password saved to configuration manager successfully");

    LOG_CORE_INFO("Admin password changed successfully, active sessions revoked");

    return AUTH_RESULT_SUCCESS;
}

void auth_mgr_revoke_all_sessions(void)
{
    if (!g_auth_mgr.is_initialized) {
        return;
    }

    AUTH_MUTEX_LOCK(g_auth_mgr.mutex);

    for (uint32_t i = 0; i < g_auth_mgr.session_count; i++) {
        if (g_auth_mgr.sessions[i].is_active) {
            g_auth_mgr.sessions[i].is_active = AICAM_FALSE;
            LOG_CORE_INFO("Session %u revoked after credential change", g_auth_mgr.sessions[i].session_id);
        }
    }

    AUTH_MUTEX_UNLOCK(g_auth_mgr.mutex);
}

static void auth_mgr_sync_credential_state(const char *password)
{
    char record[AUTH_CREDENTIAL_RECORD_MAX];
    aicam_result_t credential_result = json_config_get_device_credential(record, sizeof(record));

    AUTH_MUTEX_LOCK(g_auth_mgr.mutex);

    if (password != NULL) {
        strncpy(g_auth_mgr.config.admin_password, password, sizeof(g_auth_mgr.config.admin_password) - 1);
        g_auth_mgr.config.admin_password[sizeof(g_auth_mgr.config.admin_password) - 1] = '\0';
    }

    if (credential_result == AICAM_OK && auth_mgr_credential_is_record(record) == AICAM_TRUE) {
        strncpy(g_auth_mgr.config.admin_credential, record, sizeof(g_auth_mgr.config.admin_credential) - 1);
        g_auth_mgr.config.admin_credential[sizeof(g_auth_mgr.config.admin_credential) - 1] = '\0';
        g_auth_mgr.has_credential_record = AICAM_TRUE;
    }

    g_auth_mgr.verify_cache_valid = AICAM_FALSE;
    memset(g_auth_mgr.verify_cache_digest, 0, sizeof(g_auth_mgr.verify_cache_digest));
    g_auth_mgr.default_active_valid = AICAM_FALSE;
    g_auth_mgr.credential_epoch++;

    AUTH_MUTEX_UNLOCK(g_auth_mgr.mutex);

    memset(record, 0, sizeof(record));
}

/* ==================== Utility Functions Implementation ==================== */

const char* auth_result_to_string(auth_result_t result)
{
    switch (result) {
        case AUTH_RESULT_SUCCESS:               return "Success";
        case AUTH_RESULT_INVALID_CREDENTIALS:   return "Invalid credentials";
        case AUTH_RESULT_SESSION_EXPIRED:       return "Session expired";
        case AUTH_RESULT_SESSION_NOT_FOUND:     return "Session not found";
        case AUTH_RESULT_MAX_SESSIONS_REACHED:  return "Maximum sessions reached";
        case AUTH_RESULT_INVALID_PARAM:         return "Invalid parameter";
        case AUTH_RESULT_INTERNAL_ERROR:        return "Internal error";
        default:                               return "Unknown error";
    }
}

/* ==================== Internal Functions Implementation ==================== */

static auth_session_t* auth_mgr_find_session(uint32_t session_id)
{
    for (uint32_t i = 0; i < g_auth_mgr.session_count; i++) {
        if (g_auth_mgr.sessions[i].session_id == session_id && 
            g_auth_mgr.sessions[i].is_active) {
            return &g_auth_mgr.sessions[i];
        }
    }
    return NULL;
}

static auth_session_t* auth_mgr_allocate_session(void)
{
    // First, try to find an inactive session slot
    for (uint32_t i = 0; i < AUTH_MAX_SESSIONS; i++) {
        if (!g_auth_mgr.sessions[i].is_active) {
            if (i >= g_auth_mgr.session_count) {
                g_auth_mgr.session_count = i + 1;
            }
            memset(&g_auth_mgr.sessions[i], 0, sizeof(auth_session_t));
            return &g_auth_mgr.sessions[i];
        }
    }
    
    return NULL;  // No available session slots
}

static void auth_mgr_cleanup_expired_sessions(void)
{
    if (!g_auth_mgr.config.enable_session_timeout) {
        return;
    }
    
    uint64_t current_time = auth_mgr_get_timestamp();
    
    for (uint32_t i = 0; i < g_auth_mgr.session_count; i++) {
        auth_session_t *session = &g_auth_mgr.sessions[i];
        if (session->is_active && current_time > session->expires_at) {
            LOG_CORE_INFO("Session %u expired", session->session_id);
            session->is_active = AICAM_FALSE;
        }
    }
}

static uint32_t auth_mgr_generate_session_id(void)
{
    uint32_t session_id = g_auth_mgr.next_session_id++;
    
    if (g_auth_mgr.next_session_id > AUTH_SESSION_ID_MAX) {
        g_auth_mgr.next_session_id = AUTH_SESSION_ID_MIN;
    }
    
    // Ensure uniqueness
    while (auth_mgr_find_session(session_id)) {
        session_id = g_auth_mgr.next_session_id++;
        if (g_auth_mgr.next_session_id > AUTH_SESSION_ID_MAX) {
            g_auth_mgr.next_session_id = AUTH_SESSION_ID_MIN;
        }
    }
    
    return session_id;
}

static aicam_bool_t auth_mgr_verify_locked(const char *password)
{
    if (g_auth_mgr.has_credential_record == AICAM_TRUE) {
        uint8_t digest[AUTH_CREDENTIAL_KEY_LEN];
        aicam_bool_t record_ok;

        if (g_auth_mgr.verify_cache_valid == AICAM_TRUE) {
            auth_mgr_password_digest(password, digest);
            if (auth_mgr_constant_time_compare(digest, g_auth_mgr.verify_cache_digest,
                                               AUTH_CREDENTIAL_KEY_LEN) == 0) {
                memset(digest, 0, sizeof(digest));
                return AICAM_TRUE;
            }
            memset(digest, 0, sizeof(digest));
        }

        record_ok = auth_mgr_credential_record_verify(g_auth_mgr.config.admin_credential, password);
        if (record_ok == AICAM_TRUE) {
            auth_mgr_password_digest(password, g_auth_mgr.verify_cache_digest);
            g_auth_mgr.verify_cache_valid = AICAM_TRUE;
        }
        return record_ok;
    }

    if (g_auth_mgr.config.admin_password[0] == '\0') {
        return AICAM_FALSE;
    }

    size_t password_len = strlen(password);
    size_t stored_len = strlen(g_auth_mgr.config.admin_password);
    if (password_len != stored_len) {
        return AICAM_FALSE;
    }

    return (auth_mgr_constant_time_compare((const uint8_t *)password,
                                           (const uint8_t *)g_auth_mgr.config.admin_password,
                                           stored_len) == 0) ? AICAM_TRUE : AICAM_FALSE;
}

static void auth_mgr_upgrade_legacy_credential(void)
{
    if (!g_auth_mgr.is_initialized) {
        return;
    }

    if (g_auth_mgr.legacy_upgrade_attempted == AICAM_TRUE ||
        g_auth_mgr.has_credential_record == AICAM_TRUE ||
        g_auth_mgr.credential_trusted != AICAM_TRUE) {
        return;
    }

    char password[AUTH_MAX_PASSWORD_LEN + 1];

    AUTH_MUTEX_LOCK(g_auth_mgr.mutex);
    g_auth_mgr.legacy_upgrade_attempted = AICAM_TRUE;
    strncpy(password, g_auth_mgr.config.admin_password, sizeof(password) - 1);
    password[sizeof(password) - 1] = '\0';
    AUTH_MUTEX_UNLOCK(g_auth_mgr.mutex);

    if (password[0] == '\0') {
        memset(password, 0, sizeof(password));
        return;
    }

    if (json_config_set_device_password(password) != AICAM_OK) {
        LOG_CORE_WARN("Legacy admin credential record upgrade deferred, verification keeps using the legacy credential");
        memset(password, 0, sizeof(password));
        return;
    }

    auth_mgr_sync_credential_state(password);
    memset(password, 0, sizeof(password));

    LOG_CORE_INFO("Legacy admin credential upgraded to salted record after verified NVS persistence");
}

aicam_bool_t auth_mgr_default_password_active(void)
{
    if (g_auth_mgr.is_initialized != AICAM_TRUE || g_auth_mgr.credential_trusted != AICAM_TRUE) {
        return AICAM_FALSE;
    }

    for (;;) {
        uint32_t epoch;
        aicam_bool_t active;

        AUTH_MUTEX_LOCK(g_auth_mgr.mutex);
        if (g_auth_mgr.default_active_valid == AICAM_TRUE) {
            active = g_auth_mgr.default_active_result;
            AUTH_MUTEX_UNLOCK(g_auth_mgr.mutex);
            return active;
        }
        epoch = g_auth_mgr.credential_epoch;
        AUTH_MUTEX_UNLOCK(g_auth_mgr.mutex);

        active = auth_mgr_verify_password(AUTH_DEFAULT_PASSWORD);

        AUTH_MUTEX_LOCK(g_auth_mgr.mutex);
        if (g_auth_mgr.credential_epoch != epoch) {
            AUTH_MUTEX_UNLOCK(g_auth_mgr.mutex);
            continue;
        }
        g_auth_mgr.default_active_result = active;
        g_auth_mgr.default_active_valid = AICAM_TRUE;
        AUTH_MUTEX_UNLOCK(g_auth_mgr.mutex);
        return active;
    }
}

uint32_t auth_mgr_credential_epoch(void)
{
    uint32_t epoch = 0u;

    if (g_auth_mgr.is_initialized != AICAM_TRUE) {
        return epoch;
    }

    AUTH_MUTEX_LOCK(g_auth_mgr.mutex);
    epoch = g_auth_mgr.credential_epoch;
    AUTH_MUTEX_UNLOCK(g_auth_mgr.mutex);
    return epoch;
}

auth_verdict_t auth_mgr_authorize(const char *username, const char *password)
{
    auth_verdict_t verdict = {AICAM_FALSE, AICAM_FALSE, AICAM_FALSE};
    uint32_t verify_epoch;

    if (!username || !password) {
        return verdict;
    }

    if (g_auth_mgr.is_initialized != AICAM_TRUE || g_auth_mgr.credential_trusted != AICAM_TRUE) {
        return verdict;
    }

    if (username[0] == '\0' || password[0] == '\0') {
        return verdict;
    }

    if (strlen(password) > AUTH_MAX_PASSWORD_LEN) {
        return verdict;
    }

    if (strcmp(username, AUTH_ADMIN_USERNAME) != 0) {
        return verdict;
    }

    verify_epoch = auth_mgr_credential_epoch();
    if (auth_mgr_verify_password(password) != AICAM_TRUE) {
        return verdict;
    }
    if (auth_mgr_credential_epoch() != verify_epoch) {
        return verdict;
    }

    verdict.authenticated = AICAM_TRUE;
    verdict.default_password_active = auth_mgr_default_password_active();
    if (auth_mgr_credential_epoch() != verify_epoch) {
        verdict.authenticated = AICAM_FALSE;
        verdict.default_password_active = AICAM_FALSE;
        return verdict;
    }
    verdict.high_privilege_allowed = (verdict.default_password_active != AICAM_TRUE) ? AICAM_TRUE : AICAM_FALSE;

    return verdict;
}

static int auth_mgr_constant_time_compare(const uint8_t *a, const uint8_t *b, size_t len)
{
    volatile uint8_t diff = 0;
    for (size_t i = 0; i < len; i++) {
        diff |= (uint8_t)(a[i] ^ b[i]);
    }
    return diff;
}

static int auth_mgr_hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void auth_mgr_bytes_to_hex(const uint8_t *data, size_t len, char *out)
{
    static const char hex_chars[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[i * 2] = hex_chars[data[i] >> 4];
        out[i * 2 + 1] = hex_chars[data[i] & 0x0F];
    }
}

static aicam_bool_t auth_mgr_hex_to_bytes(const char *hex, uint8_t *out, size_t out_len)
{
    for (size_t i = 0; i < out_len; i++) {
        int hi = auth_mgr_hex_nibble(hex[i * 2]);
        int lo = auth_mgr_hex_nibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return AICAM_FALSE;
        }
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return AICAM_TRUE;
}

static int auth_mgr_fill_salt(uint8_t *salt, size_t salt_len)
{
    uint8_t pool[AUTH_CREDENTIAL_KEY_LEN];
    uint8_t digest[AUTH_CREDENTIAL_KEY_LEN];
    int sha_rc;
    static uint32_t salt_counter = 0x5A17A17Au;
    uint32_t tick = (uint32_t)osKernelGetTickCount();
    uintptr_t address_entropy = (uintptr_t)(void *)&g_auth_mgr;

    memset(pool, 0, sizeof(pool));
    memcpy(pool, &tick, sizeof(tick));
    salt_counter++;
    memcpy(pool + sizeof(tick), &salt_counter, sizeof(salt_counter));
    memcpy(pool + sizeof(tick) + sizeof(salt_counter), &address_entropy, sizeof(address_entropy));
    for (size_t i = 0; i < sizeof(pool); i++) {
        pool[i] = (uint8_t)(pool[i] ^ (uint8_t)(i * 31u + salt_counter));
    }

    sha_rc = mbedtls_sha256(pool, sizeof(pool), digest, 0);
    if (sha_rc != 0) {
        memset(pool, 0, sizeof(pool));
        memset(digest, 0, sizeof(digest));
        memset(salt, 0, salt_len);
        return sha_rc;
    }
    memcpy(salt, digest, salt_len < sizeof(digest) ? salt_len : sizeof(digest));

    memset(pool, 0, sizeof(pool));
    memset(digest, 0, sizeof(digest));
    return 0;
}

aicam_bool_t auth_mgr_credential_is_record(const char *value)
{
    if (!value) {
        return AICAM_FALSE;
    }

    size_t prefix_len = strlen(AUTH_CREDENTIAL_RECORD_PREFIX);
    size_t expected_len = prefix_len
                          + AUTH_CREDENTIAL_SALT_LEN * 2
                          + 1
                          + AUTH_CREDENTIAL_KEY_LEN * 2;
    if (strlen(value) != expected_len) {
        return AICAM_FALSE;
    }

    if (strncmp(value, AUTH_CREDENTIAL_RECORD_PREFIX, prefix_len) != 0) {
        return AICAM_FALSE;
    }

    const char *salt_hex = value + prefix_len;
    const char *separator = salt_hex + AUTH_CREDENTIAL_SALT_LEN * 2;
    const char *key_hex = separator + 1;

    if (*separator != ':') {
        return AICAM_FALSE;
    }

    uint8_t ignored_salt[AUTH_CREDENTIAL_SALT_LEN];
    uint8_t ignored_key[AUTH_CREDENTIAL_KEY_LEN];
    aicam_bool_t parseable = (auth_mgr_hex_to_bytes(salt_hex, ignored_salt, sizeof(ignored_salt)) == AICAM_TRUE &&
                              auth_mgr_hex_to_bytes(key_hex, ignored_key, sizeof(ignored_key)) == AICAM_TRUE) ?
                             AICAM_TRUE : AICAM_FALSE;
    memset(ignored_salt, 0, sizeof(ignored_salt));
    memset(ignored_key, 0, sizeof(ignored_key));
    return parseable;
}

typedef struct {
    uint32_t state[8];
    uint64_t bits;
    uint8_t block[64];
    size_t used;
} auth_sha256_ctx_t;

static const uint32_t auth_sha256_k[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

static uint32_t auth_sha256_ror(uint32_t v, unsigned n)
{
    return (v >> n) | (v << (32u - n));
}

static void auth_sha256_compress(uint32_t state[8], const uint8_t block[64])
{
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, h;
    uint32_t t1, t2;
    unsigned i;

    for (i = 0; i < 16u; i++) {
        w[i] = ((uint32_t)block[i * 4u] << 24) | ((uint32_t)block[i * 4u + 1u] << 16) |
               ((uint32_t)block[i * 4u + 2u] << 8) | (uint32_t)block[i * 4u + 3u];
    }
    for (i = 16u; i < 64u; i++) {
        uint32_t s0 = auth_sha256_ror(w[i - 15u], 7) ^ auth_sha256_ror(w[i - 15u], 18) ^ (w[i - 15u] >> 3);
        uint32_t s1 = auth_sha256_ror(w[i - 2u], 17) ^ auth_sha256_ror(w[i - 2u], 19) ^ (w[i - 2u] >> 10);
        w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
    }

    a = state[0]; b = state[1]; c = state[2]; d = state[3];
    e = state[4]; f = state[5]; g = state[6]; h = state[7];

    for (i = 0; i < 64u; i++) {
        uint32_t s1 = auth_sha256_ror(e, 6) ^ auth_sha256_ror(e, 11) ^ auth_sha256_ror(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        t1 = h + s1 + ch + auth_sha256_k[i] + w[i];
        uint32_t s0 = auth_sha256_ror(a, 2) ^ auth_sha256_ror(a, 13) ^ auth_sha256_ror(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        t2 = s0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

static void auth_sha256_init(auth_sha256_ctx_t *ctx)
{
    ctx->state[0] = 0x6a09e667u; ctx->state[1] = 0xbb67ae85u;
    ctx->state[2] = 0x3c6ef372u; ctx->state[3] = 0xa54ff53au;
    ctx->state[4] = 0x510e527fu; ctx->state[5] = 0x9b05688cu;
    ctx->state[6] = 0x1f83d9abu; ctx->state[7] = 0x5be0cd19u;
    ctx->bits = 0;
    ctx->used = 0;
}

static void auth_sha256_update(auth_sha256_ctx_t *ctx, const uint8_t *data, size_t len)
{
    ctx->bits += (uint64_t)len * 8u;
    while (len > 0u) {
        size_t take = 64u - ctx->used;
        if (take > len) {
            take = len;
        }
        memcpy(ctx->block + ctx->used, data, take);
        ctx->used += take;
        data += take;
        len -= take;
        if (ctx->used == 64u) {
            auth_sha256_compress(ctx->state, ctx->block);
            ctx->used = 0;
        }
    }
}

static void auth_sha256_final(auth_sha256_ctx_t *ctx, uint8_t out[32])
{
    uint64_t bits = ctx->bits;
    unsigned i;

    ctx->block[ctx->used++] = 0x80u;
    if (ctx->used > 56u) {
        memset(ctx->block + ctx->used, 0, 64u - ctx->used);
        auth_sha256_compress(ctx->state, ctx->block);
        ctx->used = 0;
    }
    memset(ctx->block + ctx->used, 0, 56u - ctx->used);
    for (i = 0; i < 8u; i++) {
        ctx->block[56u + i] = (uint8_t)(bits >> (56u - i * 8u));
    }
    auth_sha256_compress(ctx->state, ctx->block);

    for (i = 0; i < 8u; i++) {
        out[i * 4u] = (uint8_t)(ctx->state[i] >> 24);
        out[i * 4u + 1u] = (uint8_t)(ctx->state[i] >> 16);
        out[i * 4u + 2u] = (uint8_t)(ctx->state[i] >> 8);
        out[i * 4u + 3u] = (uint8_t)(ctx->state[i]);
    }
    memset(ctx, 0, sizeof(*ctx));
}

typedef struct {
    auth_sha256_ctx_t ipad_ctx;
    auth_sha256_ctx_t opad_ctx;
} auth_hmac_ctx_t;

static void auth_hmac_key(auth_hmac_ctx_t *hmac, const uint8_t *key, size_t key_len)
{
    uint8_t key_block[64];
    uint8_t pad[64];
    size_t i;

    memset(key_block, 0, sizeof(key_block));
    if (key_len > sizeof(key_block)) {
        auth_sha256_ctx_t key_hash;
        auth_sha256_init(&key_hash);
        auth_sha256_update(&key_hash, key, key_len);
        auth_sha256_final(&key_hash, key_block);
    } else {
        memcpy(key_block, key, key_len);
    }

    auth_sha256_init(&hmac->ipad_ctx);
    for (i = 0; i < sizeof(pad); i++) {
        pad[i] = key_block[i] ^ 0x36u;
    }
    auth_sha256_update(&hmac->ipad_ctx, pad, sizeof(pad));

    auth_sha256_init(&hmac->opad_ctx);
    for (i = 0; i < sizeof(pad); i++) {
        pad[i] = key_block[i] ^ 0x5cu;
    }
    auth_sha256_update(&hmac->opad_ctx, pad, sizeof(pad));

    memset(key_block, 0, sizeof(key_block));
    memset(pad, 0, sizeof(pad));
}

static void auth_hmac_mac(const auth_hmac_ctx_t *hmac, const uint8_t *msg, size_t msg_len,
                          uint8_t out[32])
{
    auth_sha256_ctx_t inner = hmac->ipad_ctx;
    uint8_t inner_digest[32];

    auth_sha256_update(&inner, msg, msg_len);
    auth_sha256_final(&inner, inner_digest);

    {
        auth_sha256_ctx_t outer = hmac->opad_ctx;
        auth_sha256_update(&outer, inner_digest, sizeof(inner_digest));
        auth_sha256_final(&outer, out);
    }
    memset(inner_digest, 0, sizeof(inner_digest));
}

static void auth_mgr_password_digest(const char *password, uint8_t out[AUTH_CREDENTIAL_KEY_LEN])
{
    auth_sha256_ctx_t ctx;

    auth_sha256_init(&ctx);
    auth_sha256_update(&ctx, (const uint8_t *)password, strlen(password));
    auth_sha256_final(&ctx, out);
}

static int auth_mgr_pbkdf2_hmac_sha256(const char *password,
                                       const uint8_t *salt,
                                       size_t salt_len,
                                       uint32_t iterations,
                                       uint8_t *dk,
                                       size_t dk_len)
{
    auth_hmac_ctx_t hmac_ctx;
    uint8_t u[AUTH_CREDENTIAL_KEY_LEN];
    uint8_t t[AUTH_CREDENTIAL_KEY_LEN];
    uint8_t salt_block[AUTH_CREDENTIAL_SALT_LEN + 4];
    uint32_t block_index = 1;
    size_t copied = 0;

    if (password == NULL || salt == NULL || dk == NULL || iterations == 0u || dk_len == 0u) {
        return -1;
    }

    auth_hmac_key(&hmac_ctx, (const uint8_t *)password, strlen(password));

    while (copied < dk_len) {
        size_t block_len = dk_len - copied;
        if (block_len > AUTH_CREDENTIAL_KEY_LEN) {
            block_len = AUTH_CREDENTIAL_KEY_LEN;
        }

        memcpy(salt_block, salt, salt_len);
        salt_block[salt_len] = (uint8_t)(block_index >> 24);
        salt_block[salt_len + 1] = (uint8_t)(block_index >> 16);
        salt_block[salt_len + 2] = (uint8_t)(block_index >> 8);
        salt_block[salt_len + 3] = (uint8_t)(block_index);

        auth_hmac_mac(&hmac_ctx, salt_block, sizeof(salt_block), u);
        memcpy(t, u, sizeof(t));

        for (uint32_t i = 1; i < iterations; i++) {
            auth_hmac_mac(&hmac_ctx, u, sizeof(u), u);
            for (size_t j = 0; j < sizeof(t); j++) {
                t[j] = (uint8_t)(t[j] ^ u[j]);
            }
        }

        memcpy(dk + copied, t, block_len);
        copied += block_len;
        block_index++;
    }

    memset(u, 0, sizeof(u));
    memset(t, 0, sizeof(t));
    memset(salt_block, 0, sizeof(salt_block));
    return 0;
}

aicam_result_t auth_mgr_credential_record_encode(const char *password,
                                                 char *record,
                                                 size_t record_size)
{
    if (!password || !record || record_size < AUTH_CREDENTIAL_RECORD_MAX) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    if (password[0] == '\0' || strlen(password) > AUTH_MAX_PASSWORD_LEN) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    uint8_t salt[AUTH_CREDENTIAL_SALT_LEN];
    uint8_t derived_key[AUTH_CREDENTIAL_KEY_LEN];
    int pbkdf2_rc;
    int salt_rc;
    size_t prefix_len = strlen(AUTH_CREDENTIAL_RECORD_PREFIX);

    memset(record, 0, record_size);
    salt_rc = auth_mgr_fill_salt(salt, sizeof(salt));
    if (salt_rc != 0) {
        memset(salt, 0, sizeof(salt));
        LOG_CORE_ERROR("Admin credential encode failed: salt digest rc=%d, no record produced", salt_rc);
        return AICAM_ERROR;
    }

    pbkdf2_rc = auth_mgr_pbkdf2_hmac_sha256(password, salt, sizeof(salt),
                                            AUTH_PBKDF2_ITERATIONS,
                                            derived_key, sizeof(derived_key));
    if (pbkdf2_rc != 0) {
        memset(salt, 0, sizeof(salt));
        LOG_CORE_ERROR("Admin credential encode failed: PBKDF2 rc=%d, no record produced", pbkdf2_rc);
        return AICAM_ERROR;
    }

    memcpy(record, AUTH_CREDENTIAL_RECORD_PREFIX, prefix_len);
    auth_mgr_bytes_to_hex(salt, sizeof(salt), record + prefix_len);
    record[prefix_len + AUTH_CREDENTIAL_SALT_LEN * 2] = ':';
    auth_mgr_bytes_to_hex(derived_key, sizeof(derived_key),
                          record + prefix_len + AUTH_CREDENTIAL_SALT_LEN * 2 + 1);
    record[AUTH_CREDENTIAL_RECORD_MAX - 1] = '\0';

    memset(salt, 0, sizeof(salt));
    memset(derived_key, 0, sizeof(derived_key));
    return AICAM_OK;
}

aicam_bool_t auth_mgr_credential_record_verify(const char *record,
                                               const char *password)
{
    if (!record || !password) {
        return AICAM_FALSE;
    }

    if (auth_mgr_credential_is_record(record) != AICAM_TRUE) {
        return AICAM_FALSE;
    }

    size_t prefix_len = strlen(AUTH_CREDENTIAL_RECORD_PREFIX);
    const char *salt_hex = record + prefix_len;
    const char *key_hex = salt_hex + AUTH_CREDENTIAL_SALT_LEN * 2 + 1;

    uint8_t salt[AUTH_CREDENTIAL_SALT_LEN];
    uint8_t stored_key[AUTH_CREDENTIAL_KEY_LEN];
    uint8_t computed_key[AUTH_CREDENTIAL_KEY_LEN];

    if (auth_mgr_hex_to_bytes(salt_hex, salt, sizeof(salt)) != AICAM_TRUE ||
        auth_mgr_hex_to_bytes(key_hex, stored_key, sizeof(stored_key)) != AICAM_TRUE) {
        return AICAM_FALSE;
    }

    if (auth_mgr_pbkdf2_hmac_sha256(password, salt, sizeof(salt),
                                    AUTH_PBKDF2_ITERATIONS,
                                    computed_key, sizeof(computed_key)) != 0) {
        memset(salt, 0, sizeof(salt));
        memset(stored_key, 0, sizeof(stored_key));
        memset(computed_key, 0, sizeof(computed_key));
        return AICAM_FALSE;
    }

    aicam_bool_t result = (auth_mgr_constant_time_compare(computed_key, stored_key, sizeof(stored_key)) == 0) ?
                          AICAM_TRUE : AICAM_FALSE;

    memset(salt, 0, sizeof(salt));
    memset(stored_key, 0, sizeof(stored_key));
    memset(computed_key, 0, sizeof(computed_key));
    return result;
}

static uint64_t auth_mgr_get_timestamp(void)
{
    // In real implementation, use system tick or RTC
    static uint64_t timestamp = 0;
    return ++timestamp * 1000;  // Simulate milliseconds
} 
