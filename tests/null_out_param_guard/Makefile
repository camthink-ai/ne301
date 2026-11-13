CC ?= cc
REPO_ROOT = ../..
SG_DIR = $(REPO_ROOT)/tests/storage_gate

# --- binary 1: fs_type overflow proof against the REAL Custom/Hal/storage.h ---
# shim/ supplies only the headers storage.h includes but does not need for
# storage_disk_info_t; everything else resolves to the real headers.
NSG_INC = -Ishim \
    -I$(REPO_ROOT)/Custom/Hal \
    -I$(REPO_ROOT)/Custom \
    -I$(REPO_ROOT)/Custom/Common/Lib/littlefs \
    -I$(REPO_ROOT)/Custom/Common/Lib/nvs \
    -I$(REPO_ROOT)/Custom/Common/Lib/mpool \
    -I$(REPO_ROOT)/Custom/Common/Inc

# --- binary 2: NULL out-param semantics against REAL json_config_mgr.c ---
# Same shim world as tests/storage_gate/test_storage_e2e.
E2E_INC = -I$(SG_DIR) \
    -I$(SG_DIR)/shim \
    -I$(REPO_ROOT)/Custom/Common/Lib/MbedTLS/include \
    -I$(REPO_ROOT)/Custom/Common/Lib/littlefs \
    -I$(REPO_ROOT)/Custom/Common/Lib/nvs \
    -I$(REPO_ROOT)/Custom/Common/Lib/cJSON \
    -I$(REPO_ROOT)/Custom/Hal \
    -I$(REPO_ROOT)/Custom/Core/System \
    -I$(REPO_ROOT)/Custom/Core/Security \
    -I$(REPO_ROOT)/Custom/Core/Data \
    -I$(REPO_ROOT)/Custom/Services/Web \
    -I$(REPO_ROOT)/Custom/Services/Web/api \
    -I$(REPO_ROOT)/Custom/Services/Device \
    -I$(REPO_ROOT)/Custom/Services/OTA \
    -I$(REPO_ROOT)/Custom/Services/Communication \
    -I$(REPO_ROOT)/Custom/Common/Inc \
    -I$(REPO_ROOT)/Custom/Common/Utils \
    -I$(REPO_ROOT)/Middlewares/ST/STM32_ISP_Library/isp/Inc

STRICT_CFLAGS = -Wall -Wextra -Werror -std=c11 -O1 -g -D_DEFAULT_SOURCE -D'__packed=__attribute__((packed))'
LEGACY_CFLAGS = -std=c11 -O1 -g -Wno-everything -DSTM32N657xx=1 -D'__packed=__attribute__((packed))'
THIRD_PARTY_CFLAGS = -Wall -Wno-sign-compare -std=c11 -O1 -g -include stddef.h -D'__packed=__attribute__((packed))'
MBEDTLS_MIN_FLAGS = -DMBEDTLS_CONFIG_FILE='"auth_mbedtls_config.h"'
MONGOOSE_INC = -I$(REPO_ROOT)/Custom/Common/Lib
MONGOOSE_CFLAGS = -DMG_TLS=0

E2E_REAL_OBJS = e2e_backend.o json_config_nvs.o json_config_json.o json_config_mgr.o json_config_utils.o json_config_boot_gate.o auth_mgr.o api_device_module.o api_auth_module.o web_api.o cJSON.o
E2E_LINK_OBJS = fake_flash.o storage_media_gate.o storage_nvs_session.o nvs.o lfs.o lfs_util.o mongoose.o gen_stubs.o mbedtls_sha256.o mbedtls_md.o mbedtls_platform_util.o
# The test object is deliberately NOT a stub input: gen_stubs must not stub
# libc symbols the fork/waitpid fault-detection helper needs.
E2E_STUB_INPUT_OBJS = $(E2E_REAL_OBJS) fake_flash.o storage_media_gate.o storage_nvs_session.o nvs.o lfs.o lfs_util.o mongoose.o mbedtls_sha256.o mbedtls_md.o mbedtls_platform_util.o

# --- binary 3: flash_fs_type truncation proof against the REAL device_service.h ---
# shim_devsvc/ supplies only the headers device_service.h includes but does
# not need for storage_info_t.
DSVC_INC = -Ishim_devsvc \
    -I$(REPO_ROOT)/Custom/Services/Device

.PHONY: test clean FORCE

test: test_fs_type_overflow test_getter_null_semantics test_flash_fs_type_truncation
	./test_fs_type_overflow
	./test_getter_null_semantics
	./test_flash_fs_type_truncation

test_fs_type_overflow: test_fs_type_overflow.o
	$(CC) $(STRICT_CFLAGS) -o $@ $^

test_fs_type_overflow.o: test_fs_type_overflow.c \
    shim/cmsis_os2.h shim/dev_manager.h shim/fsbl_app_common.h shim/generic_file.h \
    $(REPO_ROOT)/Custom/Hal/storage.h
	$(CC) $(STRICT_CFLAGS) $(NSG_INC) -c $< -o $@

test_flash_fs_type_truncation: test_flash_fs_type_truncation.o
	$(CC) $(STRICT_CFLAGS) -o $@ $^

test_flash_fs_type_truncation.o: test_flash_fs_type_truncation.c \
    shim_devsvc/aicam_types.h shim_devsvc/service_interfaces.h shim_devsvc/misc.h \
    shim_devsvc/json_config_mgr.h shim_devsvc/nn.h shim_devsvc/jpegc.h \
    $(REPO_ROOT)/Custom/Services/Device/device_service.h
	$(CC) $(STRICT_CFLAGS) $(DSVC_INC) -c $< -o $@

test_getter_null_semantics: test_getter_null_semantics.o $(E2E_REAL_OBJS) $(E2E_LINK_OBJS)
	$(CC) $(STRICT_CFLAGS) -o $@ $^

test_getter_null_semantics.o: test_getter_null_semantics.c \
    $(SG_DIR)/fake_flash.h $(REPO_ROOT)/Custom/Core/System/json_config_mgr.h
	$(CC) $(STRICT_CFLAGS) $(E2E_INC) -c $< -o $@

gen_stubs.c: $(E2E_STUB_INPUT_OBJS) $(SG_DIR)/gen_stubs.py FORCE
	@python3 $(SG_DIR)/gen_stubs.py $(E2E_STUB_INPUT_OBJS)

gen_stubs.o: gen_stubs.c
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

e2e_backend.o: $(SG_DIR)/e2e_backend.c
	$(CC) $(STRICT_CFLAGS) $(E2E_INC) -c $< -o $@

fake_flash.o: $(SG_DIR)/fake_flash.c
	$(CC) $(STRICT_CFLAGS) $(E2E_INC) -c $< -o $@

storage_media_gate.o: $(REPO_ROOT)/Custom/Hal/storage_media_gate.c
	$(CC) $(STRICT_CFLAGS) $(E2E_INC) -c $< -o $@

storage_nvs_session.o: $(REPO_ROOT)/Custom/Hal/storage_nvs_session.c
	$(CC) $(STRICT_CFLAGS) $(E2E_INC) -c $< -o $@

json_config_boot_gate.o: $(REPO_ROOT)/Custom/Core/System/json_config_boot_gate.c
	$(CC) $(STRICT_CFLAGS) $(E2E_INC) -c $< -o $@

json_config_nvs.o: $(REPO_ROOT)/Custom/Core/System/json_config_nvs.c
	$(CC) $(LEGACY_CFLAGS) $(E2E_INC) -c $< -o $@

json_config_json.o: $(REPO_ROOT)/Custom/Core/System/json_config_json.c
	$(CC) $(LEGACY_CFLAGS) $(E2E_INC) -c $< -o $@

json_config_mgr.o: $(REPO_ROOT)/Custom/Core/System/json_config_mgr.c
	$(CC) $(LEGACY_CFLAGS) $(E2E_INC) -c $< -o $@

json_config_utils.o: $(REPO_ROOT)/Custom/Core/System/json_config_utils.c
	$(CC) $(LEGACY_CFLAGS) $(E2E_INC) -c $< -o $@

auth_mgr.o: $(REPO_ROOT)/Custom/Core/Security/auth_mgr.c
	$(CC) $(LEGACY_CFLAGS) $(E2E_INC) -c $< -o $@

api_device_module.o: $(REPO_ROOT)/Custom/Services/Web/api/api_device_module.c
	$(CC) $(LEGACY_CFLAGS) $(E2E_INC) -c $< -o $@

api_auth_module.o: $(REPO_ROOT)/Custom/Services/Web/api/api_auth_module.c
	$(CC) $(LEGACY_CFLAGS) $(E2E_INC) -c $< -o $@

web_api.o: $(REPO_ROOT)/Custom/Services/Web/web_api.c
	$(CC) $(LEGACY_CFLAGS) $(E2E_INC) -c $< -o $@

cJSON.o: $(REPO_ROOT)/Custom/Common/Lib/cJSON/cJSON.c
	$(CC) $(LEGACY_CFLAGS) $(E2E_INC) -c $< -o $@

mongoose.o: $(REPO_ROOT)/Custom/Common/Lib/mongoose/mongoose.c
	$(CC) $(LEGACY_CFLAGS) $(E2E_INC) $(MONGOOSE_INC) $(MONGOOSE_CFLAGS) -c $< -o $@

mbedtls_sha256.o: $(REPO_ROOT)/Custom/Common/Lib/MbedTLS/library/sha256.c
	$(CC) $(LEGACY_CFLAGS) $(E2E_INC) $(MBEDTLS_MIN_FLAGS) -c $< -o $@

mbedtls_md.o: $(REPO_ROOT)/Custom/Common/Lib/MbedTLS/library/md.c
	$(CC) $(LEGACY_CFLAGS) $(E2E_INC) $(MBEDTLS_MIN_FLAGS) -c $< -o $@

mbedtls_platform_util.o: $(REPO_ROOT)/Custom/Common/Lib/MbedTLS/library/platform_util.c
	$(CC) $(LEGACY_CFLAGS) $(E2E_INC) $(MBEDTLS_MIN_FLAGS) -c $< -o $@

lfs.o: $(REPO_ROOT)/Custom/Common/Lib/littlefs/lfs.c
	$(CC) $(THIRD_PARTY_CFLAGS) $(E2E_INC) -c $< -o $@

lfs_util.o: $(REPO_ROOT)/Custom/Common/Lib/littlefs/lfs_util.c
	$(CC) $(THIRD_PARTY_CFLAGS) $(E2E_INC) -c $< -o $@

nvs.o: $(REPO_ROOT)/Custom/Common/Lib/nvs/nvs.c
	$(CC) $(THIRD_PARTY_CFLAGS) $(E2E_INC) -c $< -o $@

clean:
	rm -f test_fs_type_overflow test_getter_null_semantics test_flash_fs_type_truncation *.o gen_stubs.c

FORCE:
