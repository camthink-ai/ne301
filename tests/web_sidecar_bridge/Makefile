CC ?= cc
REPO_ROOT := ../..
MBEDTLS := $(REPO_ROOT)/Custom/Common/Lib/MbedTLS
SIDECAR_BUILD := $(REPO_ROOT)/tests/app_sidecar/build

INC := -I$(REPO_ROOT)/Custom/Common/Lib/pp \
       -I$(REPO_ROOT)/tests/sign_spike/shim \
       -I$(REPO_ROOT)/tests/storage_gate/shim \
       -I$(REPO_ROOT)/tests/sign_spike \
       -I. \
       -Ibuild \
       -I$(SIDECAR_BUILD) \
       -I$(REPO_ROOT)/tests/app_installer \
       -I$(REPO_ROOT)/tests/app_admission \
       -I$(REPO_ROOT)/tests/app_sidecar_lifecycle \
       -I$(REPO_ROOT)/Custom/Hal \
       -I$(REPO_ROOT)/Custom/Core/Video \
       -I$(REPO_ROOT)/Custom/Core/System \
       -I$(REPO_ROOT)/Custom/Core/Data \
       -I$(REPO_ROOT)/Custom/Services/Video \
       -I$(REPO_ROOT)/Custom/Services/AI \
       -I$(REPO_ROOT)/Custom/Services/AppHost \
       -I$(REPO_ROOT)/Custom/Services/Web \
       -I$(REPO_ROOT)/Custom/Services/Web/api \
       -I$(REPO_ROOT)/Custom/Common/Inc \
       -I$(REPO_ROOT)/Custom/Common/Lib \
       -I$(REPO_ROOT)/Custom/Common/Lib/nvs \
       -I$(REPO_ROOT)/Custom/Common/Lib/mmx108/mm_shims \
       -I$(REPO_ROOT)/Custom/Common/Lib/mmx108/morselib/include \
       -I$(REPO_ROOT)/Custom/Common/Lib/cJSON \
       -I$(REPO_ROOT)/Custom/Common/Lib/mongoose \
       -I$(REPO_ROOT)/Custom/Common/Utils \
       -I$(REPO_ROOT)/Middlewares/ST/STM32_ISP_Library/isp/Inc \
       -I$(REPO_ROOT)/BSP/Utilities/Fonts \
       -I$(MBEDTLS)/include \
       -I$(MBEDTLS)/configs

DEFS := -DMBEDTLS_CONFIG_FILE='"host_user_config.h"' -DMBEDTLS_ALLOW_PRIVATE_ACCESS -DNE301_APP_HOST_POC=1 -D_DEFAULT_SOURCE -D'__packed=__attribute__((packed))'
STRICT_CFLAGS := -std=c11 -O1 -g -Wall -Wextra -Werror $(INC) $(DEFS) -pthread
LIB_SRCS := $(wildcard $(MBEDTLS)/library/*.c)
MBOBJS := $(addprefix build/,$(notdir $(LIB_SRCS:.c=.o)))
CORE_OBJS := api_app_session_module.o web_assets.o app_installer.o app_state_store.o \
             app_admission.o app_host_control.o app_host_live.o app_report_policy.o \
             generic_math.o cJSON.o web_api.o mongoose.o

.PHONY: test clean vectors FORCE
test: vectors test_web_sidecar_bridge
	./test_web_sidecar_bridge

vectors:
	python3 $(REPO_ROOT)/tests/app_sidecar/gen_sidecar_fixtures.py --output $(SIDECAR_BUILD)
	python3 gen_serving_fixtures.py --output build

$(SIDECAR_BUILD)/sidecar_vectors.h:
	python3 $(REPO_ROOT)/tests/app_sidecar/gen_sidecar_fixtures.py --output $(SIDECAR_BUILD)

test_web_sidecar_bridge: test_web_sidecar_bridge.o $(CORE_OBJS) $(MBOBJS) gen_stubs.o
	$(CC) $(STRICT_CFLAGS) -o $@ $^

gen_stubs.c: test_web_sidecar_bridge.o $(CORE_OBJS) $(MBOBJS) $(REPO_ROOT)/tests/storage_gate/gen_stubs.py FORCE
	@python3 $(REPO_ROOT)/tests/storage_gate/gen_stubs.py test_web_sidecar_bridge.o $(CORE_OBJS) $(MBOBJS)

test_web_sidecar_bridge.o: test_web_sidecar_bridge.c $(SIDECAR_BUILD)/sidecar_vectors.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

api_app_session_module.o: $(REPO_ROOT)/Custom/Services/Web/api/api_app_session_module.c
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

web_assets.o: $(REPO_ROOT)/Custom/Services/Web/web_assets.c
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_installer.o: $(REPO_ROOT)/Custom/Services/AppHost/app_installer.c
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_state_store.o: $(REPO_ROOT)/Custom/Services/AppHost/app_state_store.c
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_admission.o: $(REPO_ROOT)/Custom/Services/AppHost/app_admission.c
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_host_control.o: $(REPO_ROOT)/Custom/Services/AppHost/app_host_control.c
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_host_live.o: $(REPO_ROOT)/Custom/Services/AppHost/app_host_live.c
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_report_policy.o: $(REPO_ROOT)/Custom/Services/AppHost/app_report_policy.c
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

generic_math.o: $(REPO_ROOT)/Custom/Common/Utils/generic_math.c
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

cJSON.o: $(REPO_ROOT)/Custom/Common/Lib/cJSON/cJSON.c
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

web_api.o: $(REPO_ROOT)/Custom/Services/Web/web_api.c
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

mongoose.o: $(REPO_ROOT)/Custom/Common/Lib/mongoose/mongoose.c
	$(CC) -std=c11 -O1 -g -w $(INC) $(DEFS) -c $< -o $@

build:
	mkdir -p build

build/%.o: $(MBEDTLS)/library/%.c | build
	$(CC) -std=c11 -O1 -g -w $(INC) $(DEFS) -c $< -o $@

clean:
	rm -rf build test_web_sidecar_bridge *.o gen_stubs.c
