CC ?= cc
REPO_ROOT = ../..
SG = ../storage_gate

INC = -I. -I$(SG) -I$(SG)/shim \
      -I$(REPO_ROOT)/Custom/Common/Lib/MbedTLS/include \
      -I$(REPO_ROOT)/Custom/Common/Lib/MbedTLS/port \
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

STRICT_CFLAGS = -Wall -Wextra -Werror -std=c11 -O1 -g -D'__packed=__attribute__((packed))'
THIRD_PARTY_CFLAGS = -Wall -Wno-sign-compare -std=c11 -O1 -include stddef.h -D'__packed=__attribute__((packed))'
LEGACY_CFLAGS = -std=c11 -O1 -g -Wno-everything -DSTM32N657xx=1 -D'__packed=__attribute__((packed))'
MONGOOSE_INC = -I$(REPO_ROOT)/Custom/Common/Lib
MBEDTLS_MIN_FLAGS = -DMBEDTLS_CONFIG_FILE='"threading_gate_config.h"'
MBEDTLS_LIB_INC = -I$(REPO_ROOT)/Custom/Common/Lib/MbedTLS/library

REAL_OBJS = test_threading_gate.o hal_hash_shim.o e2e_backend.o fake_flash.o \
            json_config_nvs.o json_config_json.o json_config_mgr.o json_config_utils.o \
            json_config_boot_gate.o auth_mgr.o api_device_module.o api_auth_module.o \
            web_api.o cJSON.o
LINK_OBJS = storage_media_gate.o storage_nvs_session.o nvs.o lfs.o lfs_util.o mongoose.o gen_stubs.o \
            mbedtls_sha256.o mbedtls_md.o mbedtls_threading.o mbedtls_platform_util.o \
            sha256_alt.o threading_alt.o
LINK_OBJS_NO_STUB = $(filter-out gen_stubs.o,$(LINK_OBJS))
STUB_INPUT_OBJS = $(REAL_OBJS) $(LINK_OBJS_NO_STUB)

.PHONY: test clean
test: test_threading_gate
	./test_threading_gate

test_threading_gate: $(REAL_OBJS) $(LINK_OBJS)
	$(CC) $(STRICT_CFLAGS) -o $@ $(REAL_OBJS) $(LINK_OBJS)

gen_stubs.c: $(STUB_INPUT_OBJS) $(SG)/gen_stubs.py FORCE
	@python3 $(SG)/gen_stubs.py $(STUB_INPUT_OBJS)

gen_stubs.o: gen_stubs.c
	$(CC) $(STRICT_CFLAGS) $(INC) -c $< -o $@

FORCE:

e2e_backend.o: $(SG)/e2e_backend.c
	$(CC) $(STRICT_CFLAGS) $(INC) -c $< -o $@

fake_flash.o: $(SG)/fake_flash.c
	$(CC) $(STRICT_CFLAGS) $(INC) -c $< -o $@

storage_media_gate.o: $(REPO_ROOT)/Custom/Hal/storage_media_gate.c
	$(CC) $(STRICT_CFLAGS) $(INC) -c $< -o $@

storage_nvs_session.o: $(REPO_ROOT)/Custom/Hal/storage_nvs_session.c
	$(CC) $(STRICT_CFLAGS) $(INC) -c $< -o $@

json_config_boot_gate.o: $(REPO_ROOT)/Custom/Core/System/json_config_boot_gate.c
	$(CC) $(STRICT_CFLAGS) $(INC) -c $< -o $@

json_config_nvs.o: $(REPO_ROOT)/Custom/Core/System/json_config_nvs.c
	$(CC) $(LEGACY_CFLAGS) $(INC) -c $< -o $@

json_config_json.o: $(REPO_ROOT)/Custom/Core/System/json_config_json.c
	$(CC) $(LEGACY_CFLAGS) $(INC) -c $< -o $@

json_config_mgr.o: $(REPO_ROOT)/Custom/Core/System/json_config_mgr.c
	$(CC) $(LEGACY_CFLAGS) $(INC) -c $< -o $@

json_config_utils.o: $(REPO_ROOT)/Custom/Core/System/json_config_utils.c
	$(CC) $(LEGACY_CFLAGS) $(INC) -c $< -o $@

auth_mgr.o: $(REPO_ROOT)/Custom/Core/Security/auth_mgr.c
	$(CC) $(LEGACY_CFLAGS) $(INC) -c $< -o $@

api_device_module.o: $(REPO_ROOT)/Custom/Services/Web/api/api_device_module.c
	$(CC) $(LEGACY_CFLAGS) $(INC) -c $< -o $@

api_auth_module.o: $(REPO_ROOT)/Custom/Services/Web/api/api_auth_module.c
	$(CC) $(LEGACY_CFLAGS) $(INC) -c $< -o $@

web_api.o: $(REPO_ROOT)/Custom/Services/Web/web_api.c
	$(CC) $(LEGACY_CFLAGS) $(INC) -c $< -o $@

test_threading_gate.o: test_threading_gate.c
	$(CC) $(STRICT_CFLAGS) $(INC) $(MBEDTLS_MIN_FLAGS) -c $< -o $@

hal_hash_shim.o: hal_hash_shim.c
	$(CC) $(STRICT_CFLAGS) $(INC) -c $< -o $@

mbedtls_sha256.o: $(REPO_ROOT)/Custom/Common/Lib/MbedTLS/library/sha256.c
	$(CC) $(LEGACY_CFLAGS) $(INC) $(MBEDTLS_MIN_FLAGS) $(MBEDTLS_LIB_INC) -c $< -o $@

mbedtls_md.o: $(REPO_ROOT)/Custom/Common/Lib/MbedTLS/library/md.c
	$(CC) $(LEGACY_CFLAGS) $(INC) $(MBEDTLS_MIN_FLAGS) $(MBEDTLS_LIB_INC) -c $< -o $@

mbedtls_threading.o: $(REPO_ROOT)/Custom/Common/Lib/MbedTLS/library/threading.c
	$(CC) $(LEGACY_CFLAGS) $(INC) $(MBEDTLS_MIN_FLAGS) $(MBEDTLS_LIB_INC) -c $< -o $@

mbedtls_platform_util.o: $(REPO_ROOT)/Custom/Common/Lib/MbedTLS/library/platform_util.c
	$(CC) $(LEGACY_CFLAGS) $(INC) $(MBEDTLS_MIN_FLAGS) $(MBEDTLS_LIB_INC) -c $< -o $@

sha256_alt.o: $(REPO_ROOT)/Custom/Common/Lib/MbedTLS/port/sha256_alt.c
	$(CC) $(LEGACY_CFLAGS) $(INC) $(MBEDTLS_MIN_FLAGS) $(MBEDTLS_LIB_INC) -c $< -o $@

threading_alt.o: $(REPO_ROOT)/Custom/Common/Lib/MbedTLS/port/threading_alt.c
	$(CC) $(LEGACY_CFLAGS) $(INC) $(MBEDTLS_MIN_FLAGS) $(MBEDTLS_LIB_INC) -c $< -o $@

cJSON.o: $(REPO_ROOT)/Custom/Common/Lib/cJSON/cJSON.c
	$(CC) $(THIRD_PARTY_CFLAGS) $(INC) -c $< -o $@

nvs.o: $(REPO_ROOT)/Custom/Common/Lib/nvs/nvs.c
	$(CC) $(THIRD_PARTY_CFLAGS) $(INC) -c $< -o $@

lfs.o: $(REPO_ROOT)/Custom/Common/Lib/littlefs/lfs.c
	$(CC) $(THIRD_PARTY_CFLAGS) $(INC) -c $< -o $@

lfs_util.o: $(REPO_ROOT)/Custom/Common/Lib/littlefs/lfs_util.c
	$(CC) $(THIRD_PARTY_CFLAGS) $(INC) -c $< -o $@

mongoose.o: $(REPO_ROOT)/Custom/Common/Lib/mongoose/mongoose.c
	$(CC) $(LEGACY_CFLAGS) $(INC) $(MONGOOSE_INC) -DMG_TLS=0 -c $< -o $@

clean:
	rm -f *.o gen_stubs.c test_threading_gate
