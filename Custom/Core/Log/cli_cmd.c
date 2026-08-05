#include "cli_cmd.h"
#include "debug.h"
#include "generic_file.h"
#include "lfs.h"
#include "storage.h"
#include "sd_file.h"
#include "json_config_mgr.h"
#include "aicam_types.h"
#include "aicam_error.h"
#include "drtc.h"
#include "misc.h"
#include "ai_service.h"
#include "communication_service.h"
#include "ai_draw_service.h"
#include "upgrade_manager.h"
#include "mqtt_service.h"
#include "service_init.h"
#include "ota_header.h"
#include "storage.h"
#include "video_pipeline.h"
#include "websocket_stream_server.h"
#include "mongoose.h"
#include "version.h"
#include "factory_test.h"
#include "rtmp_service.h"
#include "system_service.h"


static int cat_cmd(int argc, char* argv[]) 
{
    if (argc < 2) {
        LOG_SIMPLE("Usage: cat <filename>\r\n");
        return -1;
    }
    char local_filename[MAX_FILENAME_LEN];
    strncpy(local_filename, argv[1], MAX_FILENAME_LEN - 1);
    local_filename[MAX_FILENAME_LEN - 1] = '\0';

    void *fd = file_fopen(local_filename, "r");
    if (!fd) {
        LOG_SIMPLE("cat: cannot open %s\r\n", local_filename);
        return -1;
    }
    char buf[1024];
    int n;
    while ((n = file_fread(fd, buf, sizeof(buf))) > 0) {
        fwrite(buf, 1, n, stdout);
    }
    LOG_SIMPLE("\r\n");
    file_fclose(fd);
    return 0;
}

static int ls_cmd(int argc, char* argv[]) 
{
    char local_path[MAX_FILENAME_LEN];
    if (argc > 1) {
        strncpy(local_path, argv[1], MAX_FILENAME_LEN - 1);
        local_path[MAX_FILENAME_LEN - 1] = '\0';
    } else {
        strncpy(local_path, "/", MAX_FILENAME_LEN - 1);
        local_path[MAX_FILENAME_LEN - 1] = '\0';
    }
    void *dd = file_opendir(local_path);
    if (!dd) {
        LOG_SIMPLE("ls: cannot open directory %s\r\n", local_path);
        return -1;
    }
    // Union large enough for both lfs_info (264B) and sd_info (~282B)
    union { struct lfs_info lfs; char _pad[300]; } entry;
    int ret;
    LOG_SIMPLE("\r\n");
    while ((ret = file_readdir(dd, (char*)&entry)) == 1) {
        if (entry.lfs.type == LFS_TYPE_DIR) {
            LOG_SIMPLE("%-20s <DIR>\r\n", entry.lfs.name);
        } else {
            LOG_SIMPLE("%-20s %10lu bytes\r\n", entry.lfs.name, (unsigned long)entry.lfs.size);
        }
    }
    if (ret < 0) {
        LOG_SIMPLE("ls: readdir error\r\n");
    }
    file_closedir(dd);
    return 0;
}

static int cp_cmd(int argc, char* argv[]) {
    if (argc < 3) {
        LOG_SIMPLE("Usage: cp <src> <dst>\r\n");
        return -1;
    }
    char local_src[MAX_FILENAME_LEN];
    char local_dst[MAX_FILENAME_LEN];
    strncpy(local_src, argv[1], MAX_FILENAME_LEN - 1);
    local_src[MAX_FILENAME_LEN - 1] = '\0';
    strncpy(local_dst, argv[2], MAX_FILENAME_LEN - 1);
    local_dst[MAX_FILENAME_LEN - 1] = '\0';

    void *fd_src = file_fopen(local_src, "r");
    if (!fd_src) {
        LOG_SIMPLE("cp: cannot open %s\r\n", local_src);
        return -1;
    }
    void *fd_dst = file_fopen(local_dst, "w");
    if (!fd_dst) {
        LOG_SIMPLE("cp: cannot create %s\r\n", local_dst);
        file_fclose(fd_src);
        return -1;
    }
    char buf[1024];
    int n;
    while ((n = file_fread(fd_src, buf, sizeof(buf))) > 0) {
        if (file_fwrite(fd_dst, buf, n) != n) {
            LOG_SIMPLE("cp: write error\r\n");
            file_fclose(fd_src);
            file_fclose(fd_dst);
            return -1;
        }
    }
    file_fclose(fd_src);
    file_fclose(fd_dst);
    return 0;
}

static int mv_cmd(int argc, char* argv[]) {
    if (argc < 3) {
        LOG_SIMPLE("Usage: mv <src> <dst>\r\n");
        return -1;
    }
    char local_src[MAX_FILENAME_LEN];
    char local_dst[MAX_FILENAME_LEN];
    strncpy(local_src, argv[1], MAX_FILENAME_LEN - 1);
    local_src[MAX_FILENAME_LEN - 1] = '\0';
    strncpy(local_dst, argv[2], MAX_FILENAME_LEN - 1);
    local_dst[MAX_FILENAME_LEN - 1] = '\0';

    if (file_rename(local_src, local_dst) != 0) {
        LOG_SIMPLE("mv: cannot move %s to %s\r\n", local_src, local_dst);
        return -1;
    }
    return 0;
}

static int rm_cmd(int argc, char* argv[]) {
    if (argc < 2) {
        LOG_SIMPLE("Usage: rm <file>\r\n");
        return -1;
    }
    char local_filename[MAX_FILENAME_LEN];
    strncpy(local_filename, argv[1], MAX_FILENAME_LEN - 1);
    local_filename[MAX_FILENAME_LEN - 1] = '\0';

    if (file_remove(local_filename) != 0) {
        LOG_SIMPLE("rm: cannot remove %s\r\n", local_filename);
        return -1;
    }
    return 0;
}

static int touch_cmd(int argc, char* argv[]) {
    if (argc < 2) {
        LOG_SIMPLE("Usage: touch <file>\r\n");
        return -1;
    }
    char local_filename[MAX_FILENAME_LEN];
    strncpy(local_filename, argv[1], MAX_FILENAME_LEN - 1);
    local_filename[MAX_FILENAME_LEN - 1] = '\0';

    void *fd = file_fopen(local_filename, "a");
    if (!fd) {
        LOG_SIMPLE("touch: cannot touch %s\r\n", local_filename);
        return -1;
    }
    file_fclose(fd);
    return 0;
}

static int write_cmd(int argc, char* argv[]) 
{
    if (argc < 3) {
        LOG_SIMPLE("Usage: write <filename> <content>\n");
        return -1;
    }
    char local_filename[MAX_FILENAME_LEN];
    strncpy(local_filename, argv[1], MAX_FILENAME_LEN - 1);
    local_filename[MAX_FILENAME_LEN - 1] = '\0';

    // Concatenate all subsequent parameters as content (supports multiple spaces)
    char content[1024] = {0};
    int offset = 0;
    for (int i = 2; i < argc; i++) {
        int n = snprintf(content + offset, sizeof(content) - offset, "%s%s", (i == 2) ? "" : " ", argv[i]);
        if (n < 0 || n >= (int)(sizeof(content) - offset)) {
            LOG_SIMPLE("write: content too long\r\n");
            return -1;
        }
        offset += n;
    }

    void *fd = file_fopen(local_filename, "w");
    if (!fd) {
        LOG_SIMPLE("write: cannot open %s\r\n", local_filename);
        return -1;
    }
    if (file_fwrite(fd, content, strlen(content)) != (int)strlen(content)) {
        LOG_SIMPLE("write: write error\r\n");
        file_fclose(fd);
        return -1;
    }
    file_fclose(fd);
    return 0;
}

static int seektest_cmd(int argc, char* argv[])
{
    if (argc < 3) {
        LOG_SIMPLE("Usage: seektest <filename> <offset> [write_str]\r\n");
        return -1;
    }
    char local_filename[MAX_FILENAME_LEN];
    strncpy(local_filename, argv[1], MAX_FILENAME_LEN - 1);
    local_filename[MAX_FILENAME_LEN - 1] = '\0';

    int offset = atoi(argv[2]);
    if (offset < 0) {
        LOG_SIMPLE("seektest: offset must be >= 0\r\n");
        return -1;
    }

    // If write parameter exists, open mode should allow writing
    void *fd = file_fopen(local_filename, (argc > 3) ? "r+" : "r");
    if (!fd) {
        LOG_SIMPLE("seektest: cannot open %s\r\n", local_filename);
        return -1;
    }

    if (file_fseek(fd, offset, SEEK_SET) < 0) {
        LOG_SIMPLE("seektest: seek failed\r\n");
        file_fclose(fd);
        return -1;
    }

    if (argc > 3) {
        // Write operation
        const char *str = argv[3];
        int wn = file_fwrite(fd, str, strlen(str));
        if (wn > 0) {
            LOG_SIMPLE("seektest: wrote '%s' at offset %d, bytes=%d\r\n", str, offset, wn);
        } else {
            LOG_SIMPLE("seektest: write failed at offset %d\r\n", offset);
        }
        // Reposition to offset to read content after write
        file_fseek(fd, offset, SEEK_SET);
    }

    // Read operation
    char buf[128] = {0};
    int n = file_fread(fd, buf, sizeof(buf)-1);
    if (n > 0) {
        buf[n] = '\0'; // Ensure string termination
        LOG_SIMPLE("seektest: content from offset %d:\r\n%s\r\n", offset, buf);
    } else {
        LOG_SIMPLE("seektest: nothing read from offset %d\r\n", offset);
    }
    file_fclose(fd);
    return 0;
}

static int format_cmd(int argc, char* argv[]) 
{
    LOG_SIMPLE("The file system is being formatted...\r\n");
    storage_format();
    LOG_SIMPLE("The file system formatting is complete.\r\n");
    return 0;
}

static int sdfile_cmd(int argc, char* argv[]) 
{
    sd_file_ops_switch();
    return 0;
}

static int flashfile_cmd(int argc, char* argv[]) 
{
    storage_file_ops_switch();
    return 0;
}


static int mem_cmd(int argc, char* argv[]) 
{
    if (argc < 4) {
        LOG_SIMPLE("Usage: mem r <address> <length>\r\n");
        LOG_SIMPLE("       mem w <address> <value>\r\n");
        return -1;
    }

    if (strcmp(argv[1], "r") == 0) {
        unsigned int addr = strtoul(argv[2], NULL, 0);
        int len = atoi(argv[3]);
        unsigned char *p = (unsigned char*)addr;
        LOG_SIMPLE("Read memory at 0x%08X:\r\n", addr);
        for (int i = 0; i < len; i++) {
            LOG_SIMPLE("%02X ", p[i]);
            if ((i+1)%16 == 0) LOG_SIMPLE("\r\n");
        }
        LOG_SIMPLE("\n");
    } else if (strcmp(argv[1], "w") == 0) {
        unsigned int addr = strtoul(argv[2], NULL, 0);
        unsigned int value = strtoul(argv[3], NULL, 0);
        unsigned int *p = (unsigned int*)addr;
        *p = value;
        LOG_SIMPLE("Write 0x%08X to 0x%08X\r\n", value, addr);
    } else {
        LOG_SIMPLE("Unknown mem subcommand: %s\r\n", argv[1]);
        return -1;
    }
    return 0;
}

static int fget_cmd(int argc, char* argv[])
{
    if (argc == 1) {
        // No parameters, dump all data
        LOG_SIMPLE("Dump NVS_FACTORY:\r\n");
        storage_nvs_dump(NVS_FACTORY);
        LOG_SIMPLE("Dump NVS_USER:\r\n");
        storage_nvs_dump(NVS_USER);
        return 0;
    }

    if (argc == 2) {
        // With key parameter, read key
        char *key = argv[1];
        char value[128] = {0};
        int ret_factory = storage_nvs_read(NVS_FACTORY, key, value, sizeof(value)-1);
        if (ret_factory > 0) {
            LOG_SIMPLE("[FACTORY] Key: %s, Value: %s\r\n", key, value);
        } else {
            LOG_SIMPLE("[FACTORY] Key: %s not found\r\n", key);
        }
        int ret_user = storage_nvs_read(NVS_USER, key, value, sizeof(value)-1);
        if (ret_user > 0) {
            LOG_SIMPLE("[USER]    Key: %s, Value: %s\r\n", key, value);
        } else {
            LOG_SIMPLE("[USER]    Key: %s not found\r\n", key);
        }
        return 0;
    }

    LOG_SIMPLE("Usage: fget [key]\r\n");
    return -1;
}

static int fset_cmd(int argc, char* argv[])
{
    if (argc == 2) {
        char *key = argv[1];
        storage_nvs_delete(NVS_USER, key);
        return 0;
    }

    if (argc == 3) {
        char *key = argv[1];
        char *value = argv[2];
        storage_nvs_write(NVS_USER, key, value, strlen(value) + 1);
        return 0;
    }

    LOG_SIMPLE("Usage: fset <key> [value]\r\n");
    return -1;
}

static int standby_cmd(int argc, char* argv[])
{
#if ENABLE_U0_MODULE
    uint32_t wakeup_flags = PWR_WAKEUP_FLAG_RTC_TIMING | PWR_WAKEUP_FLAG_CONFIG_KEY;
    uint32_t sleep_second = 0;
    if (argc > 1) sleep_second = (uint32_t)(atoi(argv[1]));
    u0_module_enter_sleep_mode(wakeup_flags, 0, sleep_second);
#else
    if (argc > 1)
    {
        char* endptr;
        uint64_t wake_time = strtoull(argv[1], &endptr, 10);
        if (*endptr != '\0')
        {
            LOG_SIMPLE("Invalid standby time: %s\n", argv[1]);
            return -1;
        }
        usr_set_rtc_alarm(wake_time);
    }
    pwr_enter_standby_mode();
#endif

    return 0;
}

/* ==================== Configuration Management Commands ==================== */

/**
 * @brief Show current configuration
 */
static int config_show_cmd(int argc, char* argv[])
{
    LOG_SIMPLE("=== Current Configuration ===\r\n");
    
    //print current json config
    aicam_global_config_t *config = NULL;
    aicam_result_t result;
    config = (aicam_global_config_t*)buffer_calloc(1, sizeof(aicam_global_config_t));
    if (!config) {
        LOG_SIMPLE("Failed to allocate memory for config\r\n");
        return -1;
    }
    result = json_config_load_from_file(NULL, config);
    // show in json format
    char* json_buffer = (char*)buffer_calloc(1, JSON_CONFIG_MAX_BUFFER_SIZE);
    if (!json_buffer) {
        LOG_SIMPLE("Failed to allocate memory for json buffer\r\n");
        return -1;
    }
    result = json_config_serialize_to_string(config, json_buffer, JSON_CONFIG_MAX_BUFFER_SIZE);
    if (result != AICAM_OK) {
        LOG_SIMPLE("Failed to serialize config to string\r\n");
        buffer_free(json_buffer);
        return -1;
    }
    printf("%s\r\n", json_buffer);
    buffer_free(json_buffer);
    buffer_free(config);
    return 0;
}

/**
 * @brief Set configuration value
 */
static int config_set_cmd(int argc, char* argv[])
{
    //TODO: to implement
    return 0;
}

/* ==================== Utility Commands ==================== */

/**
 * @brief Show system version
 */
static int version_cmd(int argc, char* argv[])
{
    LOG_SIMPLE("=== AICAM System Version ===\r\n");
    LOG_SIMPLE("Firmware Version: %s\r\n", FW_VERSION_STRING);
    LOG_SIMPLE("Build Date: %s %s\r\n", FW_BUILD_DATE, FW_BUILD_TIME);
    LOG_SIMPLE("Git Hash: %s\r\n", FW_GIT_COMMIT);
    LOG_SIMPLE("Git Branch: %s\r\n", FW_GIT_BRANCH);
    LOG_SIMPLE("Core System: JSON Config + Event Bus\r\n");
    return 0;
}

/**
 * @brief Echo command for testing
 */
static int echo_cmd(int argc, char* argv[])
{
    LOG_SIMPLE("Echo: ");
    for (int i = 1; i < argc; i++) {
        LOG_SIMPLE("%s ", argv[i]);
    }
    LOG_SIMPLE("\r\n");
    return 0;
}

static int battery_cmd(int argc, char* argv[]) 
{
    uint8_t rate = 0;
    int ret;
    if (argc > 2) {
        LOG_SIMPLE("Usage: battery\n");
        return -1;
    }
    device_t *misc = device_find_pattern(BATTERY_DEVICE_NAME, DEV_TYPE_MISC);
    if(misc == NULL){
        return -1;
    }

    ret = device_ioctl(misc, MISC_CMD_ADC_GET_PERCENT, (uint8_t *)&rate, 0);
    if(!ret){
        LOG_SIMPLE("battery rate: %d \r\n", rate);
    }else{
        LOG_SIMPLE("get battery rate failed \r\n");
    }
    
    return 0;
}

static int light_cmd(int argc, char* argv[]) 
{
    uint8_t rate = 0;
    int ret;
    if (argc > 2) {
        LOG_SIMPLE("Usage: light\r\n");
        return -1;
    }

    device_t *misc = device_find_pattern(LIGHT_DEVICE_NAME, DEV_TYPE_MISC);
    if(misc == NULL){
        return -1;
    }

    ret = device_ioctl(misc, MISC_CMD_ADC_GET_PERCENT, (uint8_t *)&rate, 0);
    if(!ret){
        LOG_SIMPLE("light rate: %d \r\n", rate);
    }else{
        LOG_SIMPLE("get light rate failed \r\n");
    }
    
    return 0;
}

static int led_cmd(int argc, char* argv[]) 
{
    blink_params_t blink_params;
    int led_index = 0;
    if (argc < 3) {
        LOG_SIMPLE("Usage: led <index> <on/off/blink> [blink_times interval_ms]\r\n");
        return -1;
    }

    sscanf(argv[1], "%d", &led_index);
    if(led_index < 0 || led_index > 1){
        LOG_SIMPLE("Invalid led index: %d\r\n", led_index);
        return -1;
    }
    device_t *misc;

    if(led_index == 0){
        misc = device_find_pattern(IND_DEVICE_NAME, DEV_TYPE_MISC);
    }else if(led_index == 1){
        misc = device_find_pattern(IND_EXT_DEVICE_NAME, DEV_TYPE_MISC);
    }

    if(misc == NULL){
        return -1;
    }

    if (strcmp(argv[2], "on") == 0){
        device_ioctl(misc, MISC_CMD_LED_ON, 0, 0);
    }else if(strcmp(argv[2], "off") == 0){
        device_ioctl(misc, MISC_CMD_LED_OFF, 0, 0);
    }else if(strcmp(argv[2], "blink") == 0){
        if (argc < 4)
            return -1; 
        sscanf(argv[3], "%d", &blink_params.blink_times);
        sscanf(argv[4], "%d", &blink_params.interval_ms);
        device_ioctl(misc, MISC_CMD_LED_SET_BLINK, (uint8_t *)&blink_params, 0);
    }
    return 0;
}

static int flash_cmd(int argc, char* argv[]) 
{
    int duty;
    blink_params_t blink_params;
    if (argc < 2) {
        LOG_SIMPLE("Usage: flash <on/off/duty/blink>\r\n");
        return -1;
    }

    device_t *misc = device_find_pattern(FLASH_DEVICE_NAME, DEV_TYPE_MISC);
    if(misc == NULL){
        return -1;
    }

    if (strcmp(argv[1], "on") == 0){
        device_ioctl(misc, MISC_CMD_PWM_ON, 0, 0);
    }else if(strcmp(argv[1], "off") == 0){
        device_ioctl(misc, MISC_CMD_PWM_OFF, 0, 0);
    }else if(strcmp(argv[1], "duty") == 0){
        if (argc < 3)
            return -1; 
        sscanf(argv[2], "%d", &duty);
        uint8_t data = (uint8_t)duty;
        device_ioctl(misc, MISC_CMD_PWM_SET_DUTY, (uint8_t *)&data, 0);
        device_ioctl(misc, MISC_CMD_PWM_ON, 0, 0);
    }else if(strcmp(argv[1], "blink") == 0){
        if (argc < 4)
            return -1;
        sscanf(argv[2], "%d", &blink_params.blink_times);
        sscanf(argv[3], "%d", &blink_params.interval_ms);
        device_ioctl(misc, MISC_CMD_PWM_SET_BLINK, (uint8_t *)&blink_params, 0);
    }
    return 0;
}

static void button_short_press(void)
{
    LOG_SIMPLE("button short press ....\r\n");
}

static int button_cmd(int argc, char* argv[]) 
{
    if (argc > 2) {
        return -1;
    }

    device_t *misc = device_find_pattern(KEY_DEVICE_NAME, DEV_TYPE_MISC);
    if(misc == NULL){
        return -1;
    }

    device_ioctl(misc, MISC_CMD_BUTTON_SET_SP_CB, (uint8_t *)button_short_press, 0);
    return 0;
}

static int sdformat_cmd(int argc, char* argv[]) 
{
    sd_format();
    return 0;
}

static int sdinfo_cmd(int argc, char* argv[]) 
{
    sd_disk_info_t info;
    if (sd_get_disk_info(&info) == 0) {
        LOG_SIMPLE("sd_get_disk_info: mode %d, fs_type:%s, total: %ld Kbytes, free: %ld Kbytes\r\n", info.mode, info.fs_type, info.total_KBytes, info.free_KBytes);
    }
    return 0;
}

static int camera_cmd(int argc, char *argv[])
{
    if (argc < 2) {
        LOG_SIMPLE("Usage:");
        LOG_SIMPLE(" camera bri <val> | camera bri");
        LOG_SIMPLE(" camera con <val> | camera con");
        LOG_SIMPLE(" camera mir <val> | camera mir");
        LOG_SIMPLE(" camera aec <val> | camera aec");
        LOG_SIMPLE(" camera skip <val> | camera skip");
        return -1;
    }
    int val = 0, ret = 0, set_flag = 0;
    device_t *camera_dev = device_find_pattern(CAMERA_DEVICE_NAME, DEV_TYPE_VIDEO);
    if(camera_dev == NULL){
        LOG_SIMPLE("camera device not found\r\n");
        return -1;
    }
    sensor_params_t sensor_param;
    ret = device_ioctl(camera_dev, CAM_CMD_GET_SENSOR_PARAM, (uint8_t *)&sensor_param, sizeof(sensor_params_t));
    if(ret != AICAM_OK){
        LOG_SIMPLE("get sensor param failed\r\n");
        return -1;
    }
    if (strcmp(argv[1], "bri") == 0) {
        if (argc >= 3) {
            val = atoi(argv[2]);
            if (val < 0) val = 0;
            if (val > 100) val = 100;
            sensor_param.brightness = val;
            set_flag = 1;
        } else {
            LOG_SIMPLE("brightness: %d\r\n", sensor_param.brightness);
        }
    } else if (strcmp(argv[1], "con") == 0) {
        if (argc >= 3) {
            val = atoi(argv[2]);
            sensor_param.contrast = val;
            set_flag = 1;
        } else {
            LOG_SIMPLE("con: %d\r\n", sensor_param.contrast);
        }
    } else if (strcmp(argv[1], "mir") == 0) {
        if (argc >= 3) {
            val = atoi(argv[2]);
            sensor_param.mirror_flip = val;
            set_flag = 1;
        } else {
            LOG_SIMPLE("mir: %d\r\n", sensor_param.mirror_flip);
        }
    } else if (strcmp(argv[1], "aec") == 0) {
        if (argc >= 3) {
            val = atoi(argv[2]);
            sensor_param.aec = val;
            set_flag = 1;
        } else {
            LOG_SIMPLE("aec: %d\r\n", sensor_param.aec);
        }
    } else if (strcmp(argv[1], "skip") == 0) {
        // Handle startup skip frames - use ioctl directly, persist via image_config
        int skip_frames = 0;
        if (argc >= 3) {
            val = atoi(argv[2]);
            if (val < 1) val = 1;
            if (val > 300) val = 300;
            ret = device_ioctl(camera_dev, CAM_CMD_SET_STARTUP_SKIP_FRAMES, NULL, val);
            if (ret == AICAM_OK) {
                // Also save to config for persistence
                image_config_t image_config;
                if (json_config_get_device_service_image_config(&image_config) == AICAM_OK) {
                    image_config.startup_skip_frames = val;
                    json_config_set_device_service_image_config(&image_config);
                }
                LOG_SIMPLE("startup_skip_frames set to: %d\r\n", val);
            } else {
                LOG_SIMPLE("set startup_skip_frames failed: %d\r\n", ret);
                return -1;
            }
        } else {
            ret = device_ioctl(camera_dev, CAM_CMD_GET_STARTUP_SKIP_FRAMES, (uint8_t *)&skip_frames, 0);
            if (ret == AICAM_OK) {
                LOG_SIMPLE("startup_skip_frames: %d\r\n", skip_frames);
            } else {
                LOG_SIMPLE("get startup_skip_frames failed\r\n");
            }
        }
        return ret;
    } else {
        LOG_SIMPLE("Unknown camera command\r\n");
        return -1;
    }
    if(set_flag){
        ret = device_ioctl(camera_dev, CAM_CMD_SET_SENSOR_PARAM, (uint8_t *)&sensor_param, sizeof(sensor_params_t));
        if(ret != AICAM_OK){
            LOG_SIMPLE("set sensor param failed\r\n");
            return -1;
        }
    }
    if (ret != 0) LOG_SIMPLE("Camera command failed, ret=%d\r\n", ret);
    return ret;
}

static int upgrade_from_file_cmd(int argc, char* argv[])
{
    if (argc < 3) {
        LOG_SIMPLE("Usage: upgrade_from_file <firmware_type> <filename>\r\n");
        return -1;
    }
    int fw_type = atoi(argv[1]);
    if (fw_type < 0 || fw_type >= FIRMWARE_TYPE_COUNT) {
        LOG_SIMPLE("Invalid firmware type\r\n");
        return -1;
    }
    char local_filename[MAX_FILENAME_LEN];
    strncpy(local_filename, argv[2], MAX_FILENAME_LEN - 1);
    local_filename[MAX_FILENAME_LEN - 1] = '\0';

    void *fd = file_fopen(local_filename, "rb");
    if (!fd) {
        LOG_SIMPLE("Cannot open %s\r\n", local_filename);
        return -1;
    }

    firmware_header_t header;
    struct stat st;
    if (file_stat(local_filename, &st) != 0) {
        LOG_SIMPLE("Cannot stat %s\r\n", local_filename);
        file_fclose(fd);
        return -1;
    }
    header.file_size = st.st_size;
    memcpy(header.version, local_filename, sizeof(header.version));
    upgrade_handle_t handle = {0};
    if (upgrade_begin(&handle, fw_type, &header) != 0) {
        LOG_SIMPLE("upgrade_begin failed\r\n");
        file_fclose(fd);
        return -1;
    }

    LOG_SIMPLE("Firmware size: %d upgrade address: 0x%x\r\n", header.file_size, handle.base_offset);
    char buf[1024];
    uint32_t remain = header.file_size;
    while (remain > 0) {
        size_t chunk = remain > sizeof(buf) ? sizeof(buf) : remain;
        int n = file_fread(fd, buf, chunk);
        if (n <= 0) break;
        if (upgrade_write_chunk(&handle, buf, n) != 0) {
            LOG_SIMPLE("upgrade_write_chunk failed\r\n");
            file_fclose(fd);
            return -1;
        }
        remain -= n;
    }
    file_fclose(fd);

    if (remain != 0) {
        LOG_SIMPLE("Firmware file size mismatch\r\n");
        return -1;
    }

    if (upgrade_finish(&handle) != 0) {
        LOG_SIMPLE("upgrade_finish failed\r\n");
        return -1;
    }
    LOG_SIMPLE("Upgrade from file success!\r\n");
    return 0;
}

static int dump_firmware_cmd(int argc, char* argv[])
{
    if (argc < 4) {
        LOG_SIMPLE("Usage: dump_firmware <firmware_type> <slot> <filename>\r\n");
        return -1;
    }
    int fw_type = atoi(argv[1]);
    int slot_idx = atoi(argv[2]);
    if (fw_type < 0 || fw_type >= FIRMWARE_TYPE_COUNT) {
        LOG_SIMPLE("Invalid firmware type\r\n");
        return -1;
    }
    if (slot_idx != SLOT_A && slot_idx != SLOT_B) {
        LOG_SIMPLE("Invalid slot (must be 0 or 1)\r\n");
        return -1;
    }
    char local_filename[MAX_FILENAME_LEN];
    strncpy(local_filename, argv[3], MAX_FILENAME_LEN - 1);
    local_filename[MAX_FILENAME_LEN - 1] = '\0';

    void *fd = file_fopen(local_filename, "wb");
    if (!fd) {
        LOG_SIMPLE("Cannot open %s for write\r\n", local_filename);
        return -1;
    }

    upgrade_handle_t handle = {0};
    firmware_header_t header = {0};
    handle.header = &header;
    if (upgrade_read_begin(&handle, fw_type, slot_idx) != 0) {
        LOG_SIMPLE("upgrade_read_begin failed\r\n");
        file_fclose(fd);
        return -1;
    }

    char buf[1024];
    uint32_t remain = handle.total_size;
    while (remain > 0) {
        size_t chunk = remain > sizeof(buf) ? sizeof(buf) : remain;
        if (upgrade_read_chunk(&handle, buf, chunk) != chunk) break;
        file_fwrite(fd, buf, chunk);
        remain -= chunk;
    }
    file_fclose(fd);

    if (remain != 0) {
        LOG_SIMPLE("Firmware dump failed (size mismatch)\r\n");
        return -1;
    }

    LOG_SIMPLE("Firmware dumped to %s ,size=%d\r\n", local_filename, handle.total_size);
    return 0;
}

static int switch_slot_cmd(int argc, char* argv[])
{
    if (argc < 2) {
        LOG_SIMPLE("Usage: switch_slot <firmware_type>\r\n");
        return -1;
    }
    int fw_type = atoi(argv[1]);
    if (fw_type < 0 || fw_type >= FIRMWARE_TYPE_COUNT) {
        LOG_SIMPLE("Invalid firmware type\r\n");
        return -1;
    }
    SystemState *sys_state = get_system_state();
    if (switch_active_slot(fw_type) == 0) {
        LOG_SIMPLE("Switch slot success! Now active slot=%d\r\n", sys_state->active_slot[fw_type]);
        return 0;
    } else {
        LOG_SIMPLE("Switch slot failed. No valid slot to switch.\r\n");
        return -1;
    }
}

static const char* slot_status_str[] = {
    "IDLE",
    "PENDING_VERIFICATION",
    "ACTIVE",
    "UNBOOTABLE"
};

static int show_slot_status_cmd(int argc, char* argv[])
{
    SystemState *sys_state = get_system_state();

    int fw_start = 0;
    int fw_end = FIRMWARE_TYPE_COUNT;

    if (argc == 2) {
        int fw_type = atoi(argv[1]);
        if (fw_type < 0 || fw_type >= FIRMWARE_TYPE_COUNT) {
            LOG_SIMPLE("Invalid firmware type\r\n");
            return -1;
        }
        fw_start = fw_type;
        fw_end = fw_type + 1;
    }

    for (int fw = fw_start; fw < fw_end; fw++) {
        LOG_SIMPLE("------------------------------------------------------------\n");
        LOG_SIMPLE("Firmware %d | Active slot: %d\n", fw, sys_state->active_slot[fw]);
        LOG_SIMPLE("Slot | Status               | BootSuccess | TryCount | Version         | Size     | CRC32      \n");
        LOG_SIMPLE("-----+----------------------+-------------+----------+-----------------+----------+------------\n");
        for (int slot = 0; slot < SLOT_COUNT; slot++) {
            slot_info_t *info = &sys_state->slot[fw][slot];
            // Use OTA_VER_BUILD for 16-bit BUILD number support
            LOG_SIMPLE("%4d | %-20s | %11u | %8u | %d.%d.%d.%-5u | %8u | 0x%08X\n",
                slot,
                slot_status_str[info->status],
                info->boot_success,
                info->try_count,
                OTA_VER_MAJOR(info->version),
                OTA_VER_MINOR(info->version),
                OTA_VER_PATCH(info->version),
                OTA_VER_BUILD(info->version),
                info->firmware_size,
                info->crc32
            );
        }
    }
    return 0;
}

static int clean_slot_cmd(int argc, char* argv[])
{
    clean_system_state();
    return 0;
}

__attribute__((unused)) static void ota_header_print(const ota_header_t *header)
{
    if (!header) {
        return;
    }
    
    printf("=== OTA Header Information ===\r\n");
    printf("Magic: 0x%08lX\r\n", header->magic);
    printf("Header Version: 0x%04X\r\n", header->header_version);
    printf("Header Size: %d bytes\r\n", header->header_size);
    printf("Header CRC32: 0x%08lX\r\n", header->header_crc32);
    
    const char* fw_type_names[] = {
        "Unknown", "FSBL", "APP", "WEB", "AI_MODEL", "CONFIG", "PATCH", "FULL"
    };
    printf("Firmware Type: %s (%d)\r\n", 
           (header->fw_type < sizeof(fw_type_names)/sizeof(fw_type_names[0])) ? 
           fw_type_names[header->fw_type] : "Unknown", header->fw_type);
    
    printf("Encryption Type: %d\r\n", header->encrypt_type);
    printf("Compression Type: %d\r\n", header->compress_type);
    RTC_TIME_S tm_utc;
    timeStamp_to_time(header->timestamp, &tm_utc);
    printf("Timestamp: %04d-%02d-%02d %02d:%02d:%02d\r\n", tm_utc.year + 1970, tm_utc.month, tm_utc.date, tm_utc.hour, tm_utc.minute, tm_utc.second);
    printf("Sequence: %lu\r\n", header->sequence);
    printf("Total Package Size: %lu bytes\r\n", header->total_package_size);
    
    printf("\n=== Firmware Information ===\r\n");
    printf("Firmware Name: %s\r\n", header->fw_name);
    printf("Firmware Description: %s\r\n", header->fw_desc);
    
    // Extract full version (including suffix) using unified interface
    char version_str[64] = {0};
    if (ota_header_get_full_version(header, version_str, sizeof(version_str)) == 0) {
        printf("Firmware Version: %s\r\n", version_str);
    } else {
        // Fallback to numeric version only
        printf("Firmware Version: %d.%d.%d.%d\r\n", 
               header->fw_ver[0], header->fw_ver[1],
               header->fw_ver[2], header->fw_ver[3] + (header->fw_ver[4] << 8));
    }
    printf("Minimum Compatible Version: %d.%d.%d.%d\r\n", 
           header->min_ver[0], header->min_ver[1],
           header->min_ver[2], header->min_ver[3] + (header->min_ver[4] << 8));
    printf("Firmware Size: %lu bytes\r\n", header->fw_size);
    printf("Compressed Size: %lu bytes\r\n", header->fw_size_compressed);
    printf("Firmware CRC32: 0x%08lX\n", header->fw_crc32);
    
    printf("\n=== Target Information ===\r\n");
    printf("Target Address: 0x%08lX\r\n", header->target_addr);
    printf("Target Size: %lu bytes\r\n", header->target_size);
    printf("Target Offset: 0x%08lX\r\n", header->target_offset);
    printf("Target Partition: %s\r\n", header->target_partition);
    printf("Hardware Version: 0x%08lX\r\n", header->hw_version);
    printf("Chip ID: 0x%08lX\r\n", header->chip_id);
    
    printf("========================\r\n");
}

// Helper to get version string from flash or system state
static void get_fw_version_str(FirmwareType fw_type, char *buf, size_t size)
{
    SystemState *sys_state = get_system_state();
    if (!sys_state) {
        snprintf(buf, size, "unknown");
        return;
    }
    
    int active_slot = sys_state->active_slot[fw_type];
    slot_info_t *slot_info = &sys_state->slot[fw_type][active_slot];
    
    // Try to read from flash header
    uint32_t partition = get_active_partition(fw_type);
    if (partition != 0) {
        ota_header_t header = {0};
        if (storage_flash_read(partition, &header, sizeof(ota_header_t)) == 0 && 
            ota_header_verify(&header) == 0) {
            if (ota_header_get_full_version(&header, buf, size) == 0) {
                return;
            }
        }
    }
    
    // Fallback to system state
    snprintf(buf, size, "%d.%d.%d.%u", 
             OTA_VER_MAJOR(slot_info->version), OTA_VER_MINOR(slot_info->version),
             OTA_VER_PATCH(slot_info->version), OTA_VER_BUILD(slot_info->version));
}

static int fw_version_cmd(int argc, char* argv[])
{
    char version_str[64];
    
    LOG_SIMPLE("=== Firmware Version Information ===\r\n\r\n");
    
    // FSBL
    get_fw_version_str(FIRMWARE_FSBL, version_str, sizeof(version_str));
    LOG_SIMPLE("FSBL:     %s\r\n", version_str);
    
    // APP
    get_fw_version_str(FIRMWARE_APP, version_str, sizeof(version_str));
    LOG_SIMPLE("APP:      %s\r\n", version_str);
    
    // WEB
    get_fw_version_str(FIRMWARE_WEB, version_str, sizeof(version_str));
    LOG_SIMPLE("WEB:      %s\r\n", version_str);
    
    // WAKECORE
#if ENABLE_U0_MODULE
    {
        ms_bridging_version_t wakecore_version = {0};
        if (u0_module_get_version(&wakecore_version) == 0) {
            LOG_SIMPLE("WAKECORE: %d.%d.%d.%d\r\n", wakecore_version.major, wakecore_version.minor, wakecore_version.patch, wakecore_version.build);
        } else {
            LOG_SIMPLE("WAKECORE: unknown\r\n");
        }
    }
#else
    LOG_SIMPLE("WAKECORE: N/A\r\n");
#endif
    
    // MODEL (check if AI_1 is active)
    FirmwareType model_type = json_config_get_ai_1_active() ? FIRMWARE_AI_2 : FIRMWARE_AI_1;
    get_fw_version_str(model_type, version_str, sizeof(version_str));
    LOG_SIMPLE("MODEL:    %s (%s)\r\n", version_str, 
               model_type == FIRMWARE_AI_2 ? "AI_2" : "AI_1");
    
    LOG_SIMPLE("\r\n====================================\r\n");
    
    return 0;
}

static int mg_log_level_cmd(int argc, char* argv[])
{
    if (argc == 1) {
        // No parameter, show current log level
        const char* level_names[] = {
            "NONE", "ERROR", "INFO", "DEBUG", "VERBOSE"
        };
        int current_level = mg_log_level;
        if (current_level >= 0 && current_level < (int)(sizeof(level_names)/sizeof(level_names[0]))) {
            LOG_SIMPLE("Current mongoose log level: %s (%d)\r\n", level_names[current_level], current_level);
        } else {
            LOG_SIMPLE("Current mongoose log level: %d\r\n", current_level);
        }
        return 0;
    }
    
    if (argc == 2) {
        int level = -1;
        
        // Try to parse as number first
        char* endptr;
        level = (int)strtol(argv[1], &endptr, 10);
        if (*endptr == '\0' && level >= 0 && level <= MG_LL_VERBOSE) {
            mg_log_set(level);
            const char* level_names[] = {
                "NONE", "ERROR", "INFO", "DEBUG", "VERBOSE"
            };
            LOG_SIMPLE("Mongoose log level set to: %s (%d)\r\n", level_names[level], level);
            return 0;
        }
        
        // Try to parse as string
        if (strcmp(argv[1], "none") == 0 || strcmp(argv[1], "NONE") == 0) {
            level = MG_LL_NONE;
        } else if (strcmp(argv[1], "error") == 0 || strcmp(argv[1], "ERROR") == 0) {
            level = MG_LL_ERROR;
        } else if (strcmp(argv[1], "info") == 0 || strcmp(argv[1], "INFO") == 0) {
            level = MG_LL_INFO;
        } else if (strcmp(argv[1], "debug") == 0 || strcmp(argv[1], "DEBUG") == 0) {
            level = MG_LL_DEBUG;
        } else if (strcmp(argv[1], "verbose") == 0 || strcmp(argv[1], "VERBOSE") == 0) {
            level = MG_LL_VERBOSE;
        }
        
        if (level >= 0) {
            mg_log_set(level);
            const char* level_names[] = {
                "NONE", "ERROR", "INFO", "DEBUG", "VERBOSE"
            };
            LOG_SIMPLE("Mongoose log level set to: %s (%d)\r\n", level_names[level], level);
            return 0;
        }
    }
    
    LOG_SIMPLE("Usage: mg_log_level [level]\r\n");
    LOG_SIMPLE("  level: 0=NONE, 1=ERROR, 2=INFO, 3=DEBUG, 4=VERBOSE\r\n");
    LOG_SIMPLE("        or: none, error, info, debug, verbose\r\n");
    LOG_SIMPLE("  If no level is specified, shows current log level\r\n");
    return -1;
}

debug_cmd_reg_t file_cmd_table[] = {
    {"cat",   "Display file contents",    cat_cmd},
    {"ls",    "List directory contents",  ls_cmd},
    {"cp",    "Copy file",                cp_cmd},
    {"mv",    "Move/rename file",         mv_cmd},
    {"rm",    "Remove file",              rm_cmd},
    {"touch", "Create empty file",        touch_cmd},
    {"write", "write file",               write_cmd},
    {"format", "File system formatting",  format_cmd},
    {"sdformat", "SD card formatting",    sdformat_cmd},
    {"sdinfo", "Show SD card info",      sdinfo_cmd},
    {"seektest", "Test file seek", seektest_cmd},
    {"sdfile", "Switch to sd filesystem", sdfile_cmd},
    {"flashfile", "Switch to flash filesystem", flashfile_cmd},
    {"mem", "Memory read/write. r addr len | w addr value", mem_cmd},
    {"fget", "NVS get. fget [key]", fget_cmd},
    {"fset", "NVS set/delete. fset <key> [value]", fset_cmd},
    {"standby", "standby mode", standby_cmd},
    {"config_show", "Show current configuration", config_show_cmd},
    {"config_set", "Set configuration value. config_set <key> <value>", config_set_cmd},
    {"version", "Show system version", version_cmd},
    {"echo", "Echo command for testing", echo_cmd},
    // {"mkdir", "Create directory",         mkdir_cmd},
    // {"rmdir", "Remove directory",         rmdir_cmd},
    {"led",      "System led control",      led_cmd},
    {"flash",    "Flash control",           flash_cmd},
    {"battery",  "Battery rate",      battery_cmd},
    {"light",    "Light rate",           light_cmd},
    {"button",   "Button short press cb test",        button_cmd},
    {"camera", "camera <bri|con|mir|aec> [val]", camera_cmd},
    {"upgrade_from_file", "upgrade firmware from file", upgrade_from_file_cmd },
    {"dump_firmware", "dump firmware to filesystem", dump_firmware_cmd },
    {"switch_slot", "switch slot", switch_slot_cmd },
    {"show_slot", "show slot", show_slot_status_cmd },
    {"clean_slot", "clean slot", clean_slot_cmd },
    {"fw_version", "Show all firmware versions (FSBL/APP/WEB/WAKECORE/MODEL)", fw_version_cmd },
    {"mg_log_level", "Set/show mongoose log level. mg_log_level [0-4|none|error|info|debug|verbose]", mg_log_level_cmd },
};


void register_cmds(void)
{
    // register util commands
    for (int i = 0; i < (int)(sizeof(file_cmd_table) / sizeof(file_cmd_table[0])); i++) {
        debug_register_commands(&file_cmd_table[i], 1);
    }


    //web_example_register_commands();
    //ai_service_register_commands();
    comm_cmd_register();
    mqtt_cmd_register();
    service_debug_register_commands();
    video_pipeline_register_commands();
    websocket_stream_server_register_commands();
    factory_test_register_commands();
    rtmp_cmd_register();
    system_service_pir_debug_register_commands();

    
    // LOG_SIMPLE("[CLI] All commands registered (%d util commands + driver commands)\r\n", 
    //            (int)(sizeof(file_cmd_table) / sizeof(file_cmd_table[0])));
}
