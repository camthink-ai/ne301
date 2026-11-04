CC ?= cc
REPO_ROOT = ../..

INC = -Ishim \
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

STRICT_CFLAGS = -Wall -Wextra -Werror -std=c11 -O1 -g -D'__packed=__attribute__((packed))'
THIRD_PARTY_CFLAGS = -Wall -Wno-sign-compare -std=c11 -O1 -include stddef.h -D'__packed=__attribute__((packed))'
LEGACY_CFLAGS = -std=c11 -O1 -g -Wno-everything -DSTM32N657xx=1 -D'__packed=__attribute__((packed))'

FIRST_PARTY_OBJS = test_storage_gate.o fake_flash.o storage_media_gate.o json_config_boot_gate.o storage_nvs_session.o
THIRD_PARTY_OBJS = lfs.o lfs_util.o nvs.o

MBEDTLS_MIN_FLAGS = -DMBEDTLS_CONFIG_FILE='"auth_mbedtls_config.h"'
MBEDTLS_HOST_OBJS = mbedtls_sha256.o mbedtls_md.o mbedtls_platform_util.o

E2E_REAL_OBJS = e2e_storage_boot.o e2e_backend.o json_config_nvs.o json_config_json.o json_config_mgr.o json_config_utils.o json_config_boot_gate.o auth_mgr.o api_device_module.o api_auth_module.o web_api.o cJSON.o
E2E_LINK_OBJS = fake_flash.o storage_media_gate.o storage_nvs_session.o nvs.o lfs.o lfs_util.o mongoose.o gen_stubs.o $(MBEDTLS_HOST_OBJS)
E2E_STUB_INPUT_OBJS = $(E2E_REAL_OBJS) fake_flash.o storage_media_gate.o storage_nvs_session.o nvs.o lfs.o lfs_util.o mongoose.o $(MBEDTLS_HOST_OBJS)
E2E_OBJS = $(E2E_REAL_OBJS) $(E2E_LINK_OBJS)

ASAN_CFLAGS = -fsanitize=address
MONGOOSE_INC = -I$(REPO_ROOT)/Custom/Common/Lib
MONGOOSE_CFLAGS = -DMG_TLS=0
ADMIN_CORE_OBJS = e2e_backend.o a_auth_mgr.o json_config_nvs.o json_config_json.o json_config_mgr.o json_config_utils.o json_config_boot_gate.o api_device_module.o a_api_auth_module.o web_api.o cJSON.o
ADMIN_LINK_OBJS = fake_flash.o storage_media_gate.o storage_nvs_session.o nvs.o lfs.o lfs_util.o mongoose.o gen_stubs.o $(MBEDTLS_HOST_OBJS)
ADMIN_STUB_INPUT_OBJS = test_admin_auth.o test_auth_boundary.o $(ADMIN_CORE_OBJS) fake_flash.o storage_media_gate.o storage_nvs_session.o nvs.o lfs.o lfs_util.o mongoose.o $(MBEDTLS_HOST_OBJS)

.PHONY: test clean
test: test_storage_gate test_storage_e2e test_admin_auth test_auth_boundary
	./test_storage_gate
	./test_storage_e2e
	ASAN_OPTIONS=detect_leaks=0 ./test_admin_auth
	ASAN_OPTIONS=detect_leaks=0 ./test_auth_boundary

test_storage_gate: $(FIRST_PARTY_OBJS) $(THIRD_PARTY_OBJS)
	$(CC) $(STRICT_CFLAGS) -o $@ $(FIRST_PARTY_OBJS) $(THIRD_PARTY_OBJS)

test_storage_e2e: $(E2E_OBJS)
	$(CC) $(STRICT_CFLAGS) -o $@ $(E2E_OBJS)

test_admin_auth: test_admin_auth.o $(ADMIN_CORE_OBJS) $(ADMIN_LINK_OBJS)
	$(CC) $(STRICT_CFLAGS) $(ASAN_CFLAGS) -o $@ test_admin_auth.o $(ADMIN_CORE_OBJS) $(ADMIN_LINK_OBJS)

test_auth_boundary: test_auth_boundary.o $(ADMIN_CORE_OBJS) $(ADMIN_LINK_OBJS)
	$(CC) $(STRICT_CFLAGS) $(ASAN_CFLAGS) -o $@ test_auth_boundary.o $(ADMIN_CORE_OBJS) $(ADMIN_LINK_OBJS)

a_auth_mgr.o: $(REPO_ROOT)/Custom/Core/Security/auth_mgr.c
	$(CC) $(LEGACY_CFLAGS) $(ASAN_CFLAGS) $(INC) -c $< -o $@

a_api_auth_module.o: $(REPO_ROOT)/Custom/Services/Web/api/api_auth_module.c
	$(CC) $(LEGACY_CFLAGS) $(ASAN_CFLAGS) $(INC) -c $< -o $@

test_admin_auth.o: test_admin_auth.c
	$(CC) $(STRICT_CFLAGS) $(ASAN_CFLAGS) $(INC) -c $< -o $@

test_auth_boundary.o: test_auth_boundary.c
	$(CC) $(STRICT_CFLAGS) $(ASAN_CFLAGS) $(INC) $(MONGOOSE_INC) $(MONGOOSE_CFLAGS) -c $< -o $@

mongoose.o: $(REPO_ROOT)/Custom/Common/Lib/mongoose/mongoose.c
	$(CC) $(LEGACY_CFLAGS) $(INC) $(MONGOOSE_INC) $(MONGOOSE_CFLAGS) -c $< -o $@

%.o: %.c
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

auth_mgr.o: $(REPO_ROOT)/Custom/Core/Security/auth_mgr.c
	$(CC) $(LEGACY_CFLAGS) $(INC) -c $< -o $@

api_device_module.o: $(REPO_ROOT)/Custom/Services/Web/api/api_device_module.c
	$(CC) $(LEGACY_CFLAGS) $(INC) -c $< -o $@

api_auth_module.o: $(REPO_ROOT)/Custom/Services/Web/api/api_auth_module.c
	$(CC) $(LEGACY_CFLAGS) $(INC) -c $< -o $@

web_api.o: $(REPO_ROOT)/Custom/Services/Web/web_api.c
	$(CC) $(LEGACY_CFLAGS) $(INC) -c $< -o $@

mbedtls_sha256.o: $(REPO_ROOT)/Custom/Common/Lib/MbedTLS/library/sha256.c
	$(CC) $(LEGACY_CFLAGS) $(INC) $(MBEDTLS_MIN_FLAGS) -c $< -o $@

mbedtls_md.o: $(REPO_ROOT)/Custom/Common/Lib/MbedTLS/library/md.c
	$(CC) $(LEGACY_CFLAGS) $(INC) $(MBEDTLS_MIN_FLAGS) -c $< -o $@

mbedtls_platform_util.o: $(REPO_ROOT)/Custom/Common/Lib/MbedTLS/library/platform_util.c
	$(CC) $(LEGACY_CFLAGS) $(INC) $(MBEDTLS_MIN_FLAGS) -c $< -o $@

cJSON.o: $(REPO_ROOT)/Custom/Common/Lib/cJSON/cJSON.c
	$(CC) $(LEGACY_CFLAGS) $(INC) -c $< -o $@

json_config_utils.o: $(REPO_ROOT)/Custom/Core/System/json_config_utils.c
	$(CC) $(LEGACY_CFLAGS) $(INC) -c $< -o $@

lfs.o: $(REPO_ROOT)/Custom/Common/Lib/littlefs/lfs.c
	$(CC) $(THIRD_PARTY_CFLAGS) $(INC) -c $< -o $@

lfs_util.o: $(REPO_ROOT)/Custom/Common/Lib/littlefs/lfs_util.c
	$(CC) $(THIRD_PARTY_CFLAGS) $(INC) -c $< -o $@

nvs.o: $(REPO_ROOT)/Custom/Common/Lib/nvs/nvs.c
	$(CC) $(THIRD_PARTY_CFLAGS) $(INC) -c $< -o $@

gen_stubs.c: $(E2E_STUB_INPUT_OBJS) $(ADMIN_STUB_INPUT_OBJS) gen_stubs.py FORCE
	@python3 gen_stubs.py $(E2E_STUB_INPUT_OBJS) $(ADMIN_STUB_INPUT_OBJS)

FORCE:

gen_stubs.o: gen_stubs.c
	$(CC) $(STRICT_CFLAGS) $(INC) -c $< -o $@

clean:
	rm -f test_storage_gate test_storage_e2e test_admin_auth test_auth_boundary *.o gen_stubs.c
