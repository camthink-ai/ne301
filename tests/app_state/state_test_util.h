#ifndef STATE_TEST_UTIL_H
#define STATE_TEST_UTIL_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "app_state_store.h"
#include "app_installer.h"
#include "mem_io_backend.h"

static int g_total;
static int g_fail;

__attribute__((constructor)) static void state_test_unbuffered(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
}

static void CHECK(int cond)
{
    g_total++;
    if (!cond) {
        g_fail++;
        printf("FAIL state:%d\n", g_total);
    }
}

#define STATE_PUB_A "pub-alpha"
#define STATE_APP_A "line-counting"
#define STATE_PUB_B "pub-beta"
#define STATE_APP_B "other-app"

static app_install_io_t g_io_installer_io;

static void state_io_init(void)
{
    mem_reset();
    mem_io_init(&g_io_installer_io);
}

static void state_make_identity(app_state_identity_t *id, const char *pub,
                                const char *app, uint8_t seed)
{
    size_t i;
    memset(id, 0, sizeof(*id));
    for (i = 0; i < APP_STATE_SHA256_LEN; i++) {
        id->spki_sha256[i] = (uint8_t)(seed * 37u + i * 11u);
    }
    snprintf(id->publisher_id, sizeof(id->publisher_id), "%s", pub);
    snprintf(id->app_id, sizeof(id->app_id), "%s", app);
}

static void state_fill_blob(uint8_t *buf, uint32_t len, uint8_t seed)
{
    uint32_t i;
    for (i = 0; i < len; i++) {
        buf[i] = (uint8_t)(seed + i * 7u);
    }
}

static void state_install_identity(const app_state_identity_t *id, uint16_t major)
{
    app_install_record_t rec;
    uint8_t blob[APP_INSTALL_RECORD_BIN_SIZE];
    char path[APP_INSTALL_PATH_MAX];
    size_t pos = 0;
    size_t i;

    memset(&rec, 0, sizeof(rec));
    snprintf(rec.publisher_id, sizeof(rec.publisher_id), "%s", id->publisher_id);
    snprintf(rec.app_id, sizeof(rec.app_id), "%s", id->app_id);
    rec.version_major = major;
    rec.version_minor = 0;
    rec.version_patch = 0;
    rec.package_len = 128u;
    memcpy(rec.package_sha256, id->spki_sha256, APP_INSTALL_SHA256_LEN);
    memcpy(rec.publisher_key_sha256, id->spki_sha256, APP_INSTALL_SHA256_LEN);
    snprintf(path, sizeof(path), "%s", APP_INSTALL_PKG_DIR "/");
    pos = strlen(path);
    for (i = 0; i < APP_INSTALL_SHA256_LEN; i++) {
        snprintf(path + pos, sizeof(path) - pos, "%02x", rec.package_sha256[i]);
        pos += 2u;
    }
    snprintf(path + pos, sizeof(path) - pos, "%s", ".neapp");
    snprintf(rec.package_path, sizeof(rec.package_path), "%s", path);
    if (app_install_record_encode(&rec, blob, sizeof(blob)) != AICAM_TRUE) {
        printf("FATAL: record encode\n");
        exit(1);
    }
    mem_kv_write(&g_io, APP_INSTALL_KEY_RECORD, blob, sizeof(blob));
}

static int state_count_meta_keys(void)
{
    int n = 0;
    size_t i;
    (void)count_files;
    for (i = 0; i < MEM_MAX_KEYS; i++) {
        if (g_io.keys[i].present == AICAM_TRUE &&
            strncmp(g_io.keys[i].key, "app_state_", 10) == 0 &&
            !(g_io.keys[i].key[10] == 'p' && g_io.keys[i].key[11] == '_')) {
            n++;
        }
    }
    return n;
}

static int state_count_marker_keys(void)
{
    int n = 0;
    size_t i;
    for (i = 0; i < MEM_MAX_KEYS; i++) {
        if (g_io.keys[i].present == AICAM_TRUE &&
            strncmp(g_io.keys[i].key, "app_state_p_", 12) == 0) {
            n++;
        }
    }
    return n;
}

static int state_count_blob_files(void)
{
    int n = 0;
    size_t i;
    for (i = 0; i < MEM_MAX_FILES; i++) {
        if (g_io.files[i].present == AICAM_TRUE &&
            strncmp(g_io.files[i].path, "/apps/st_", 9) == 0) {
            n++;
        }
    }
    return n;
}

static mem_file_t *state_find_blob_file(void)
{
    size_t i;
    for (i = 0; i < MEM_MAX_FILES; i++) {
        if (g_io.files[i].present == AICAM_TRUE &&
            strncmp(g_io.files[i].path, "/apps/st_", 9) == 0) {
            return &g_io.files[i];
        }
    }
    return NULL;
}

static mem_kv_t *state_find_meta_kv(void)
{
    size_t i;
    for (i = 0; i < MEM_MAX_KEYS; i++) {
        if (g_io.keys[i].present == AICAM_TRUE &&
            strncmp(g_io.keys[i].key, "app_state_", 10) == 0 &&
            !(g_io.keys[i].key[10] == 'p' && g_io.keys[i].key[11] == '_')) {
            return &g_io.keys[i];
        }
    }
    return NULL;
}

#endif
