/**
 * @file web_assets.c
 * @brief Web assets implementation
 * This module parses the asset.bin file and provides the web_asset_t array interface
 */

 #include "web_assets.h"
 #include <string.h>
 #include <stdio.h>
 #include <stdlib.h>
 #include "debug.h"
 #include "buffer_mgr.h"
 #include "storage.h"
 #ifdef NE301_APP_HOST_POC
 #include "app_installer.h"
 #include "app_sidecar_lifecycle.h"
 #include "app_pair_installer.h"
 #include "mbedtls/sha256.h"
 #endif
 

#pragma pack(push, 1) 
typedef struct {
    char     magic[8];
    uint32_t version;
    uint32_t file_count;
    uint32_t total_size;
    uint32_t compressed_size;
    uint32_t timestamp;
    uint8_t  reserved[36];
} asset_bin_header_t;

typedef struct {
    char     path[56];
    uint32_t offset;
    uint32_t size;
} asset_file_index_t;
#pragma pack(pop)
 

 
typedef struct {
    const char *ext;
    const char *mime_type;
} mime_type_map_t;

mime_type_map_t mime_type_map[] = {
    {".html", "text/html;charset=utf-8"},
    {".htm",  "text/html;charset=utf-8"},
    {".css",  "text/css;charset=utf-8"},
    {".js",   "application/javascript;charset=utf-8"},
    {".json", "application/json;charset=utf-8"},
    {".png",  "image/png"},
    {".jpg",  "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".gif",  "image/gif"},
    {".svg",  "image/svg+xml"},
    {".xml",  "application/xml"},
    {".pdf",  "application/pdf"},
    {".mp3",  "audio/mpeg"},
    {".mp4",  "video/mp4"},
    {".zip",  "application/zip"},
    {".tar",  "application/x-tar"},
    {".gz",   "application/gzip"},
    {".txt",  "text/plain"},
    {".ico",  "image/x-icon"},
    {".woff", "font/woff"},
    {".woff2","font/woff2"},
    {".eot",  "application/vnd.ms-fontobject"},
    {".otf",  "font/otf"},
    {".ttf",  "font/ttf"},
    {".webp", "image/webp"},
    {".csv",  "text/csv"},
    {".yaml", "application/x-yaml"},
    {".yml",  "application/x-yaml"},
    {".md",   "text/markdown"},
    {NULL, NULL}  
};

web_asset_t* web_assets = NULL;
static uint32_t g_asset_count = 0;
static uint8_t *g_asset_blob = NULL;
static size_t g_asset_blob_size = 0;

static const char *get_mime_type(const char *filename) {
    const char *ext = strrchr(filename, '.');
    if (ext == NULL) {
        return "application/octet-stream";  
    }

    for (int i = 0; mime_type_map[i].ext != NULL; i++) {
        if (strcmp(ext, mime_type_map[i].ext) == 0) {
            return mime_type_map[i].mime_type;
        }
    }

    return "application/octet-stream";  
}
 
 aicam_result_t web_asset_adapter_init(const uint8_t* asset_data) {
    if (!asset_data) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    // Re-init safe
    web_asset_adapter_deinit();

    // Offset 1k from the beginning of the asset_data to skip the boot/header region
    const uint8_t *asset_flash = asset_data + 1024;

    // Validate header (while flash is locked against mode switches / concurrent writers)
    storage_lock_ext();
    const asset_bin_header_t* header = (const asset_bin_header_t*)asset_flash;
    if (strncmp(header->magic, "WEBASSETS", 8) != 0) {
        storage_unlock_ext();
        return AICAM_ERROR_INVALID_DATA;
    }

    uint32_t file_count = header->file_count;
    uint32_t data_total_size = header->total_size;
    size_t asset_file_total_size =
        sizeof(asset_bin_header_t) + ((size_t)file_count * sizeof(asset_file_index_t)) + (size_t)data_total_size;

    // Copy the whole asset.bin payload into RAM so later accesses don't directly dereference flash-mapped addresses.
    g_asset_blob = (uint8_t*)buffer_calloc(1, asset_file_total_size);
    if (g_asset_blob == NULL) {
        storage_unlock_ext();
        return AICAM_ERROR_NO_MEMORY;
    }
    g_asset_blob_size = asset_file_total_size;
    memcpy(g_asset_blob, asset_flash, asset_file_total_size);
    storage_unlock_ext();

    g_asset_count = file_count;
    web_assets = (web_asset_t*)buffer_calloc(g_asset_count, sizeof(web_asset_t));
    if (web_assets == NULL) {
        web_asset_adapter_deinit();
        return AICAM_ERROR_NO_MEMORY;
    }

    const asset_file_index_t* index_table =
        (const asset_file_index_t*)(g_asset_blob + sizeof(asset_bin_header_t));
    const uint8_t* data_section = g_asset_blob;

    for (uint32_t i = 0; i < g_asset_count; i++) {
        web_assets[i].path = index_table[i].path;
        web_assets[i].size = index_table[i].size;
        web_assets[i].data = data_section + index_table[i].offset;

        // check if the data is compressed
        if(web_assets[i].size > 2 && web_assets[i].data[0] == 0x1f && web_assets[i].data[1] == 0x8b)
        {
            web_assets[i].is_compressed = AICAM_TRUE;
        }
        else
        {
            web_assets[i].is_compressed = AICAM_FALSE;
        }

        //print the path and size
        LOG_SVC_INFO("[ASSETS] Path: %s, Size: %u bytes\n", web_assets[i].path, web_assets[i].size);

        web_assets[i].mime_type = get_mime_type(web_assets[i].path);
        web_assets[i].compression_ratio = 1.0f;
        web_assets[i].hash = 0; 
    }
     
    LOG_SVC_INFO("[ASSETS] Asset adapter initialized, %lu files loaded, total size: %u bytes.\n",
                 g_asset_count, (unsigned long)g_asset_blob_size);
    return AICAM_OK;
 }
 
 void web_asset_adapter_deinit(void) {
     if (web_assets != NULL) {
         buffer_free(web_assets);
         web_assets = NULL;
         g_asset_count = 0;
     }

     if (g_asset_blob != NULL) {
         buffer_free(g_asset_blob);
         g_asset_blob = NULL;
         g_asset_blob_size = 0;
     }
 }
 
 const web_asset_t* web_asset_find(const char* path) {
     if (!path || !web_assets) return NULL;
     
     for (uint32_t i = 0; i < g_asset_count; i++) {
         char asset_path_normalized[32];
         strncpy(asset_path_normalized, web_assets[i].path, sizeof(asset_path_normalized) - 1);
         for(char *p = asset_path_normalized; *p; ++p) {
             if(*p == '\\') *p = '/';
         }
         
         //ignore the leading '/'
         const char* p_path = (path[0] == '/') ? path + 1 : path;
         
         if (strncmp(asset_path_normalized, p_path, strlen(asset_path_normalized)) == 0) {
             return &web_assets[i];
         }
     }

    // not found,default to index.html
    return web_asset_find("index.html");
}

uint32_t web_asset_get_count(void) {
    return g_asset_count;
}

#ifdef NE301_APP_HOST_POC
#define WEB_SIDECAR_ROUTE_PREFIX "/appsite/g"
#define WEB_SIDECAR_SHA_HEX_LEN (APP_SIDECAR_SHA256_LEN * 2u)

typedef struct {
    aicam_bool_t valid;
    char publisher_id[APP_SIDECAR_VERDICT_ID_SLOT];
    char app_id[APP_SIDECAR_VERDICT_APP_SLOT];
    uint8_t publisher_key_sha256[APP_SIDECAR_SHA256_LEN];
    uint8_t native_tbs_sha256[APP_SIDECAR_SHA256_LEN];
    uint8_t ui_tbs_sha256[APP_SIDECAR_SHA256_LEN];
    uint16_t version_major;
    uint16_t version_minor;
    uint16_t version_patch;
    uint32_t host_ui_abi;
    uint64_t service_generation;
    uint16_t file_count;
    app_sidecar_asset_t files[APP_SIDECAR_FILE_COUNT_MAX];
} web_sidecar_projection_t;

static web_sidecar_projection_t g_web_sidecar = {0};

void web_sidecar_projection_reset(void) {
    memset(&g_web_sidecar, 0, sizeof(g_web_sidecar));
}

static aicam_bool_t web_sidecar_fill_trust(const app_install_record_t *native,
                                           app_sidecar_trust_t *trust) {
    if (native == NULL || trust == NULL ||
        strcmp(native->publisher_id, APP_ADMISSION_ADMIN_PUBLISHER_ID) != 0 ||
        memcmp(native->publisher_key_sha256,
               (const uint8_t[APP_INSTALL_SHA256_LEN]){0},
               APP_INSTALL_SHA256_LEN) != 0) {
        return AICAM_FALSE;
    }
    memset(trust, 0, sizeof(*trust));
    trust->publisher_id = APP_ADMISSION_ADMIN_PUBLISHER_ID;
    trust->publisher_id_len = strlen(APP_ADMISSION_ADMIN_PUBLISHER_ID);
    trust->app_id = native->app_id;
    trust->app_id_len = strlen(native->app_id);
    trust->native_tbs_sha256 = native->tbs_sha256;
    return AICAM_TRUE;
}

static aicam_bool_t web_sidecar_path_registered(const char *path) {
    size_t i;
    size_t len;

    if (path == NULL) {
        return AICAM_FALSE;
    }
    len = strlen(path);
    if (len == 0u || len > APP_SIDECAR_PATH_MAX) {
        return AICAM_FALSE;
    }
    if (path[0] == '/' || path[0] == '\\') {
        return AICAM_FALSE;
    }
    for (i = 0u; i < len; i++) {
        uint8_t c = (uint8_t)path[i];
        if (c == '\\' || c == '%' || c == ':' || c < 0x20u || c == 0x7fu) {
            return AICAM_FALSE;
        }
        if (!((c >= (uint8_t)'a' && c <= (uint8_t)'z') ||
              (c >= (uint8_t)'A' && c <= (uint8_t)'Z') ||
              (c >= (uint8_t)'0' && c <= (uint8_t)'9') ||
              c == (uint8_t)'.' || c == (uint8_t)'/' || c == (uint8_t)'_' ||
              c == (uint8_t)'-')) {
            return AICAM_FALSE;
        }
    }
    {
        size_t start = 0u;
        for (i = 0u; i <= len; i++) {
            size_t seg_len;
            if (i == len || path[i] == '/') {
                seg_len = i - start;
                if (seg_len == 0u) {
                    return AICAM_FALSE;
                }
                if ((seg_len == 1u && path[start] == '.') ||
                    (seg_len == 2u && path[start] == '.' && path[start + 1u] == '.')) {
                    return AICAM_FALSE;
                }
                start = i + 1u;
            }
        }
    }
    return AICAM_TRUE;
}

aicam_bool_t web_sidecar_projection_refresh(void) {
    const app_install_io_t *io;
    app_install_record_t native;
    app_sidecar_trust_t trust;
    app_sidecar_descriptor_t descriptor;
    app_sidecar_lifecycle_state_t state;
    app_sidecar_lifecycle_result_t result;
    web_sidecar_projection_t shadow;
    aicam_bool_t present = AICAM_FALSE;
    uint32_t payload_total = 0u;
    uint16_t i;

    io = app_installer_device_io();
    if (io == NULL) {
        return g_web_sidecar.valid;
    }
    if (app_install_query_committed(io, &native, &present) != APP_INSTALL_OK) {
        return g_web_sidecar.valid;
    }
    if (present != AICAM_TRUE) {
        web_sidecar_projection_reset();
        return AICAM_FALSE;
    }
    if (web_sidecar_fill_trust(&native, &trust) != AICAM_TRUE) {
        web_sidecar_projection_reset();
        return AICAM_FALSE;
    }
    memset(&descriptor, 0, sizeof(descriptor));
    result = app_sidecar_lifecycle_query(io, &trust, &native, &descriptor, &state);
    if (result != APP_SIDECAR_LIFECYCLE_OK ||
        state == APP_SIDECAR_LIFECYCLE_UNKNOWN) {
        return g_web_sidecar.valid;
    }
    /* An explicit UNAVAILABLE/NONE state revokes cached UI assets. */
    if (state != APP_SIDECAR_LIFECYCLE_ACTIVE) {
        web_sidecar_projection_reset();
        return AICAM_FALSE;
    }
    if (app_pair_is_authorized(io, &native, &descriptor) != AICAM_TRUE) {
        return g_web_sidecar.valid;
    }
    if (descriptor.file_count == 0u || descriptor.file_count > APP_SIDECAR_FILE_COUNT_MAX) {
        return g_web_sidecar.valid;
    }
    for (i = 0u; i < descriptor.file_count; i++) {
        if (!web_sidecar_path_registered(descriptor.files[i].path)) {
            return g_web_sidecar.valid;
        }
        if (descriptor.files[i].file_len == 0u ||
            descriptor.files[i].file_len > APP_SIDECAR_FILE_LEN_MAX) {
            return g_web_sidecar.valid;
        }
        payload_total += descriptor.files[i].file_len;
    }
    if (payload_total != descriptor.asset_payload_len) {
        return g_web_sidecar.valid;
    }
    memset(&shadow, 0, sizeof(shadow));
    shadow.valid = AICAM_TRUE;
    snprintf(shadow.publisher_id, sizeof(shadow.publisher_id),
             "%s", descriptor.publisher_id);
    snprintf(shadow.app_id, sizeof(shadow.app_id), "%s", descriptor.app_id);
    memcpy(shadow.publisher_key_sha256, descriptor.publisher_key_sha256,
           APP_SIDECAR_SHA256_LEN);
    memcpy(shadow.native_tbs_sha256, descriptor.native_tbs_sha256,
           APP_SIDECAR_SHA256_LEN);
    memcpy(shadow.ui_tbs_sha256, descriptor.ui_tbs_sha256, APP_SIDECAR_SHA256_LEN);
    shadow.version_major = descriptor.version_major;
    shadow.version_minor = descriptor.version_minor;
    shadow.version_patch = descriptor.version_patch;
    shadow.host_ui_abi = descriptor.host_ui_abi;
    shadow.service_generation = descriptor.service_generation;
    shadow.file_count = descriptor.file_count;
    for (i = 0u; i < descriptor.file_count; i++) {
        shadow.files[i] = descriptor.files[i];
    }
    g_web_sidecar = shadow;
    return AICAM_TRUE;
}

aicam_bool_t web_sidecar_projection_valid(void) {
    return g_web_sidecar.valid;
}

aicam_bool_t web_sidecar_projection_same_origin_allowed(void) {
    if (g_web_sidecar.valid == AICAM_TRUE &&
        strcmp(g_web_sidecar.publisher_id, APP_ADMISSION_ADMIN_PUBLISHER_ID) == 0) {
        return AICAM_TRUE;
    }
    if (web_sidecar_projection_refresh() == AICAM_TRUE &&
        g_web_sidecar.valid == AICAM_TRUE &&
        strcmp(g_web_sidecar.publisher_id, APP_ADMISSION_ADMIN_PUBLISHER_ID) == 0) {
        return AICAM_TRUE;
    }
    return AICAM_FALSE;
}

uint64_t web_sidecar_projection_generation(void) {
    return g_web_sidecar.service_generation;
}

uint16_t web_sidecar_projection_file_count(void) {
    return g_web_sidecar.file_count;
}

aicam_bool_t web_sidecar_route_parse(const char *uri, uint64_t *generation,
                                     char *path_out, size_t cap) {
    const char *cursor;
    char *end = NULL;
    unsigned long long value;

    if (uri == NULL || generation == NULL || path_out == NULL || cap == 0u) {
        return AICAM_FALSE;
    }
    if (strncmp(uri, WEB_SIDECAR_ROUTE_PREFIX, strlen(WEB_SIDECAR_ROUTE_PREFIX)) != 0) {
        return AICAM_FALSE;
    }
    cursor = uri + strlen(WEB_SIDECAR_ROUTE_PREFIX);
    if (cursor[0] == '\0' || cursor[0] == '/') {
        return AICAM_FALSE;
    }
    value = strtoull(cursor, &end, 10);
    if (end == cursor || value == 0ULL) {
        return AICAM_FALSE;
    }
    if (end[0] != '/') {
        return AICAM_FALSE;
    }
    if (strlen(end + 1) == 0u || strlen(end + 1) >= cap) {
        return AICAM_FALSE;
    }
    snprintf(path_out, cap, "%s", end + 1);
    *generation = (uint64_t)value;
    return AICAM_TRUE;
}

aicam_bool_t web_sidecar_asset_resolve(const char *path, uint32_t *out_index) {
    uint16_t i;

    if (!g_web_sidecar.valid || path == NULL || out_index == NULL) {
        return AICAM_FALSE;
    }
    if (!web_sidecar_path_registered(path)) {
        return AICAM_FALSE;
    }
    for (i = 0u; i < g_web_sidecar.file_count; i++) {
        if (strcmp(g_web_sidecar.files[i].path, path) == 0) {
            *out_index = (uint32_t)i;
            return AICAM_TRUE;
        }
    }
    return AICAM_FALSE;
}

uint32_t web_sidecar_asset_file_len(uint32_t index) {
    if (!g_web_sidecar.valid || index >= (uint32_t)g_web_sidecar.file_count) {
        return 0u;
    }
    return g_web_sidecar.files[index].file_len;
}

aicam_bool_t web_sidecar_asset_load(uint32_t index, uint8_t *buf, size_t cap,
                                    size_t *len) {
    const app_install_io_t *io;
    uint8_t digest[APP_SIDECAR_SHA256_LEN];
    size_t got = 0u;
    uint16_t i;

    if (!g_web_sidecar.valid || buf == NULL || len == NULL ||
        index >= (uint32_t)g_web_sidecar.file_count) {
        return AICAM_FALSE;
    }
    if (cap < (size_t)g_web_sidecar.files[index].file_len) {
        return AICAM_FALSE;
    }
    io = app_installer_device_io();
    if (io == NULL || io->file_read == NULL) {
        return AICAM_FALSE;
    }
    if (io->file_read(io->ctx, g_web_sidecar.files[index].storage_path, buf,
                      cap, &got) != APP_INSTALL_IO_OK) {
        return AICAM_FALSE;
    }
    if (got != (size_t)g_web_sidecar.files[index].file_len) {
        return AICAM_FALSE;
    }
    mbedtls_sha256(buf, got, digest, 0);
    for (i = 0u; i < APP_SIDECAR_SHA256_LEN; i++) {
        if (digest[i] != g_web_sidecar.files[index].sha256[i]) {
            return AICAM_FALSE;
        }
    }
    *len = got;
    return AICAM_TRUE;
}

const char *web_sidecar_asset_mime(const char *path) {
    return get_mime_type(path);
}
#endif
