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
    storage_lock();
    const asset_bin_header_t* header = (const asset_bin_header_t*)asset_flash;
    if (strncmp(header->magic, "WEBASSETS", 8) != 0) {
        storage_unlock();
        return AICAM_ERROR_INVALID_DATA;
    }

    uint32_t file_count = header->file_count;
    uint32_t data_total_size = header->total_size;
    size_t asset_file_total_size =
        sizeof(asset_bin_header_t) + ((size_t)file_count * sizeof(asset_file_index_t)) + (size_t)data_total_size;

    // Copy the whole asset.bin payload into RAM so later accesses don't directly dereference flash-mapped addresses.
    g_asset_blob = (uint8_t*)buffer_calloc(1, asset_file_total_size);
    if (g_asset_blob == NULL) {
        storage_unlock();
        return AICAM_ERROR_NO_MEMORY;
    }
    g_asset_blob_size = asset_file_total_size;
    memcpy(g_asset_blob, asset_flash, asset_file_total_size);
    storage_unlock();

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