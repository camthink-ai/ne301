CC ?= cc
REPO_ROOT := ../..
MBEDTLS := $(REPO_ROOT)/Custom/Common/Lib/MbedTLS

INC := -I$(REPO_ROOT)/tests/app_installer \
       -I$(REPO_ROOT)/tests/app_admission \
       -I$(REPO_ROOT)/tests/app_host_session \
       -I$(REPO_ROOT)/tests/sign_spike \
       -I$(REPO_ROOT)/Custom/Hal \
       -I$(REPO_ROOT)/Custom/Core/Video \
       -I$(REPO_ROOT)/Custom/Core/System \
       -I$(REPO_ROOT)/Custom/Core/Data \
       -I$(REPO_ROOT)/Custom/Services/Video \
       -I$(REPO_ROOT)/Custom/Services/AI \
       -I$(REPO_ROOT)/Custom/Common/Lib/nvs \
       -I$(REPO_ROOT)/Custom/Common/Lib/pp \
       -I$(REPO_ROOT)/tests/storage_gate/shim \
       -I$(REPO_ROOT)/Custom/Common/Lib/cJSON \
       -I$(REPO_ROOT)/Custom/Services/AppHost \
       -I$(REPO_ROOT)/Custom/Services/Web \
       -I$(REPO_ROOT)/Custom/Services/Web/api \
       -I$(REPO_ROOT)/Custom/Common/Inc \
       -I$(REPO_ROOT)/Custom/Common/Utils \
       -I$(REPO_ROOT)/Custom/Common/Lib \
       -I$(REPO_ROOT)/Custom/Common/Lib/mongoose \
       -I$(REPO_ROOT)/Middlewares/ST/STM32_ISP_Library/isp/Inc \
       -I$(REPO_ROOT)/BSP/Utilities/Fonts \
       -I. \
       -I$(MBEDTLS)/include \
       -I$(MBEDTLS)/configs

GATE_INC := -I$(REPO_ROOT)/tests/sign_spike/shim \
       -I$(REPO_ROOT)/tests/storage_gate/shim \
       $(INC)

DEFS := -DMBEDTLS_CONFIG_FILE='"host_user_config.h"' -DMBEDTLS_ALLOW_PRIVATE_ACCESS \
        -D_DEFAULT_SOURCE -D'__packed=__attribute__((packed))'

STRICT_CFLAGS := -std=c11 -O1 -g -Wall -Wextra -Werror $(INC) $(DEFS)
GATE_CFLAGS := -std=c11 -O1 -g -Wall -Wextra -Werror $(GATE_INC) $(DEFS)
LEGACY_CFLAGS := -std=c11 -O1 -g -Wno-everything $(GATE_INC) $(DEFS)

LIB_SRCS := $(wildcard $(MBEDTLS)/library/*.c)
MBOBJS := $(addprefix build/,$(notdir $(LIB_SRCS:.c=.o)))

CORE_OBJS := app_host_session.o app_host_live.o app_host_control.o app_state_store.o \
             app_installer.o app_admission.o app_host_validate.o app_report_service.o app_report_policy.o generic_math.o

PTHREAD_FLAGS := -pthread

.PHONY: test clean
test: test_app_host_control test_app_control_gate
	./test_app_host_control
	./test_app_control_gate

test_app_host_control: test_app_host_control.o $(CORE_OBJS) $(MBOBJS)
	$(CC) $(STRICT_CFLAGS) $(PTHREAD_FLAGS) -o $@ $^

test_app_control_gate: test_app_control_gate.o api_app_control_module.o app_host_control.o cJSON.o web_api.o mongoose.o gen_stubs.o
	$(CC) $(GATE_CFLAGS) $(PTHREAD_FLAGS) -o $@ test_app_control_gate.o api_app_control_module.o app_host_control.o cJSON.o web_api.o mongoose.o gen_stubs.o

test_app_host_control.o: test_app_host_control.c $(REPO_ROOT)/tests/app_host_session/session_vectors.h $(REPO_ROOT)/tests/app_host_control/control_vectors.h $(REPO_ROOT)/tests/app_installer/mem_io_backend.h $(REPO_ROOT)/Custom/Services/AppHost/app_report_service.h $(REPO_ROOT)/Custom/Services/AppHost/app_report_service_device.h
	$(CC) $(STRICT_CFLAGS) $(PTHREAD_FLAGS) -c $< -o $@

test_app_control_gate.o: test_app_control_gate.c $(REPO_ROOT)/Custom/Services/AppHost/app_host_control.h
	$(CC) $(GATE_CFLAGS) $(PTHREAD_FLAGS) -c $< -o $@

api_app_control_module.o: $(REPO_ROOT)/Custom/Services/Web/api/api_app_control_module.c $(REPO_ROOT)/Custom/Services/Web/api/api_app_control_module.h
	$(CC) $(GATE_CFLAGS) -c $< -o $@

cJSON.o: $(REPO_ROOT)/Custom/Common/Lib/cJSON/cJSON.c
	$(CC) $(LEGACY_CFLAGS) -c $< -o $@

web_api.o: $(REPO_ROOT)/Custom/Services/Web/web_api.c
	$(CC) $(LEGACY_CFLAGS) -c $< -o $@

mongoose.o: $(REPO_ROOT)/Custom/Common/Lib/mongoose/mongoose.c
	$(CC) $(LEGACY_CFLAGS) -DMG_TLS=0 -c $< -o $@

gen_stubs.c: test_app_control_gate.o api_app_control_module.o app_host_control.o cJSON.o web_api.o mongoose.o $(REPO_ROOT)/tests/storage_gate/gen_stubs.py FORCE
	@python3 $(REPO_ROOT)/tests/storage_gate/gen_stubs.py test_app_control_gate.o api_app_control_module.o app_host_control.o cJSON.o web_api.o mongoose.o

gen_stubs.o: gen_stubs.c
	$(CC) $(GATE_CFLAGS) -w -c $< -o $@

FORCE:

app_host_session.o: $(REPO_ROOT)/Custom/Services/AppHost/app_host_session.c $(REPO_ROOT)/Custom/Services/AppHost/app_host_session.h $(REPO_ROOT)/Custom/Services/AppHost/app_report_service.h $(REPO_ROOT)/Custom/Services/AppHost/app_report_service_device.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_host_live.o: $(REPO_ROOT)/Custom/Services/AppHost/app_host_live.c $(REPO_ROOT)/Custom/Services/AppHost/app_host_live.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_host_control.o: $(REPO_ROOT)/Custom/Services/AppHost/app_host_control.c $(REPO_ROOT)/Custom/Services/AppHost/app_host_control.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_state_store.o: $(REPO_ROOT)/Custom/Services/AppHost/app_state_store.c $(REPO_ROOT)/Custom/Services/AppHost/app_state_store.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_installer.o: $(REPO_ROOT)/Custom/Services/AppHost/app_installer.c $(REPO_ROOT)/Custom/Services/AppHost/app_installer.h $(REPO_ROOT)/Custom/Services/AppHost/app_sidecar_lifecycle.c $(REPO_ROOT)/Custom/Services/AppHost/app_native_vnext_floor.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_admission.o: $(REPO_ROOT)/Custom/Services/AppHost/app_admission.c $(REPO_ROOT)/Custom/Services/AppHost/app_admission.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_host_validate.o: $(REPO_ROOT)/Custom/Services/AppHost/app_host_validate.c $(REPO_ROOT)/Custom/Services/AppHost/app_host_validate.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_report_service.o: $(REPO_ROOT)/Custom/Services/AppHost/app_report_service.c $(REPO_ROOT)/Custom/Services/AppHost/app_report_service.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_report_policy.o: $(REPO_ROOT)/Custom/Services/AppHost/app_report_policy.c $(REPO_ROOT)/Custom/Services/AppHost/app_report_policy.h $(REPO_ROOT)/Custom/Services/AppHost/app_report_service.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

generic_math.o: $(REPO_ROOT)/Custom/Common/Utils/generic_math.c
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

build:
	mkdir -p build

build/%.o: $(MBEDTLS)/library/%.c | build
	$(CC) $(STRICT_CFLAGS) -w -c $< -o $@

clean:
	rm -rf build test_app_host_control test_app_control_gate *.o gen_stubs.c
