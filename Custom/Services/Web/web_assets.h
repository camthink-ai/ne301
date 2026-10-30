/**
 * @file web_assets.h
 * @brief Web assets header file
 */

 #ifndef WEB_ASSETS_H
 #define WEB_ASSETS_H
 
 #include <stdint.h>
 #include <stddef.h>
 #include <stdbool.h>
 #include "aicam_types.h" 
 
 #ifdef __cplusplus
 extern "C" {
 #endif

 typedef struct {
     const char* path;
     const uint8_t* data;
     size_t size;
     const char* mime_type;
     uint32_t hash;
     aicam_bool_t is_compressed;
     float compression_ratio;
 } web_asset_t;
 

 #define WEB_ASSET_COUNT 4
 #define WEB_TOTAL_SIZE 88592
 #define WEB_COMPRESSED_SIZE 88592
 #define WEB_COMPRESSION_RATIO 1.000
 

 extern web_asset_t* web_assets;
 
 /**
  * @brief initialize the web asset adapter
  * @param asset_data pointer to the asset.bin binary data
  * @return operation result
  */
 aicam_result_t web_asset_adapter_init(const uint8_t* asset_data);
 
 /**
  * @brief deinitialize the web asset adapter, release memory
  */
void web_asset_adapter_deinit(void);

const web_asset_t* web_asset_find(const char* path);

/**
 * @brief Get the total number of web assets
 * @return Number of assets, or 0 if not initialized
 */
uint32_t web_asset_get_count(void);

#ifdef NE301_APP_HOST_POC
aicam_bool_t web_sidecar_projection_refresh(void);

aicam_bool_t web_sidecar_projection_valid(void);

uint64_t web_sidecar_projection_generation(void);
aicam_bool_t web_sidecar_projection_same_origin_allowed(void);

uint16_t web_sidecar_projection_file_count(void);

aicam_bool_t web_sidecar_route_parse(const char *uri, uint64_t *generation,
                                     char *path_out, size_t cap);

aicam_bool_t web_sidecar_asset_resolve(const char *path, uint32_t *out_index);

aicam_bool_t web_sidecar_asset_load(uint32_t index, uint8_t *buf, size_t cap,
                                    size_t *len);

const char *web_sidecar_asset_mime(const char *path);

uint32_t web_sidecar_asset_file_len(uint32_t index);

void web_sidecar_projection_reset(void);
#endif

 #ifdef __cplusplus
 }
 #endif

 #endif // WEB_ASSETS_H