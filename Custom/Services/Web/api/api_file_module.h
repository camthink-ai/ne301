/**
 * @file api_file_module.h
 * @brief File Management API Module Header
 * @details File browsing, upload, download, delete, edit, preview API declarations
 */

#ifndef API_FILE_MODULE_H
#define API_FILE_MODULE_H

#include "web_api.h"

struct mg_connection;  /* forward declaration for Mongoose */

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== API Function Declarations ==================== */

/**
 * @brief Register file management API module
 * @return Operation result
 */
aicam_result_t web_api_register_file_module(void);

/**
 * @brief Streaming file upload processor (hooked at MG_EV_HTTP_HDRS).
 *        Takes over the Mongoose connection and writes body chunks directly
 *        to the filesystem, keeping memory usage constant regardless of file size.
 *
 * @param c       Mongoose connection
 * @param ev      Mongoose event type
 * @param ev_data Event data (mg_http_message* for MG_EV_HTTP_HDRS)
 */
void file_upload_stream_processor(struct mg_connection *c, int ev,
                                  void *ev_data);

/**
 * @brief GET /api/v1/files/list - List directory contents
 *        Query: ?fs=flash|sd&path=/
 */
aicam_result_t file_list_handler(http_handler_context_t *ctx);

/**
 * @brief GET /api/v1/files/download - Download a file
 *        Query: ?fs=flash|sd&path=/file.txt
 */
aicam_result_t file_download_handler(http_handler_context_t *ctx);

/**
 * @brief POST /api/v1/files/upload - Upload a file
 *        Raw body contains file data
 *        Query: ?fs=flash|sd&path=/dir&filename=myfile.txt
 */
aicam_result_t file_upload_handler(http_handler_context_t *ctx);

/**
 * @brief DELETE /api/v1/files - Delete a file
 *        Query: ?fs=flash|sd&path=/file.txt
 */
aicam_result_t file_delete_handler(http_handler_context_t *ctx);

/**
 * @brief PUT /api/v1/files/rename - Rename a file
 *        Body: {"fs": "flash", "old_path": "/old.txt", "new_path": "/new.txt"}
 */
aicam_result_t file_rename_handler(http_handler_context_t *ctx);

/**
 * @brief GET /api/v1/files/preview - Preview a file
 *        Query: ?fs=flash|sd&path=/file.txt
 */
aicam_result_t file_preview_handler(http_handler_context_t *ctx);

/**
 * @brief PUT /api/v1/files/edit - Edit a text file
 *        Body: {"fs": "flash", "path": "/file.txt", "content": "new content"}
 */
aicam_result_t file_edit_handler(http_handler_context_t *ctx);

/**
 * @brief POST /api/v1/files/create - Create a directory or empty file
 *        Body: {"fs": "flash", "path": "/dir", "name": "newdir", "type": "dir|file"}
 */
aicam_result_t file_create_handler(http_handler_context_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* API_FILE_MODULE_H */
