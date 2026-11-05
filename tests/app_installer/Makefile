CC ?= cc
REPO_ROOT := ../..
MBEDTLS := $(REPO_ROOT)/Custom/Common/Lib/MbedTLS

INC := -I$(REPO_ROOT)/Custom/Common/Lib/pp \
       -I$(REPO_ROOT)/tests/sign_spike/shim \
       -I$(REPO_ROOT)/tests/storage_gate/shim \
       -I$(REPO_ROOT)/Custom/Hal \
       -I$(REPO_ROOT)/Custom/Common/Lib/nvs \
       -I. \
       -I$(REPO_ROOT)/tests/app_host_session \
       -I$(REPO_ROOT)/Custom/Services/AppHost \
       -I$(REPO_ROOT)/Custom/Services/Web \
       -I$(REPO_ROOT)/Custom/Services/Web/api \
       -I$(REPO_ROOT)/Custom/Common/Inc \
       -I$(REPO_ROOT)/Custom/Common/Utils \
       -I$(REPO_ROOT)/Custom/Common/Lib \
       -I$(REPO_ROOT)/Custom/Common/Lib/cJSON \
       -I$(REPO_ROOT)/Custom/Common/Lib/pp \
       -I$(MBEDTLS)/include \
       -I$(MBEDTLS)/configs \
       -I$(REPO_ROOT)/tests/sign_spike

DEFS := -D_GNU_SOURCE '-D__packed=__attribute__((packed))' -DMBEDTLS_CONFIG_FILE='"host_user_config.h"' -DMBEDTLS_ALLOW_PRIVATE_ACCESS

STRICT_CFLAGS := -std=c11 -O1 -g -Wall -Wextra -Werror $(INC) $(DEFS)
LEGACY_CFLAGS := -std=c11 -O1 -g -Wno-everything $(INC) $(DEFS)

LIB_SRCS := $(wildcard $(MBEDTLS)/library/*.c)
MBOBJS := $(addprefix build/,$(notdir $(LIB_SRCS:.c=.o)))

CORE_OBJS := app_installer.o app_state_store.o app_admission.o generic_math.o
SESSION_OBJS := app_host_session.o app_host_live.o app_host_control.o app_report_service.o app_report_policy.o

.PHONY: test test-unsigned-api test-product-version clean
test: test-unsigned-api test_app_installer_storage test_app_installer test_app_installer_faults
	./test_app_installer_storage
	./test_app_installer
	./test_app_installer_faults
	./test_app_install_gate

test-unsigned-api: test_app_install_gate
	python3 ../app_unsigned/gen_fixture.py
	./test_app_install_gate unsigned

test-product-version: test_app_product_version
	python3 make_product_version_fixtures.py --output build/product-version
	./test_app_product_version

test_app_product_version: test_app_product_version.o $(CORE_OBJS) $(MBOBJS)
	$(CC) $(STRICT_CFLAGS) -o $@ $^

test_app_installer: test_app_installer.o $(CORE_OBJS) $(MBOBJS)
	$(CC) $(STRICT_CFLAGS) -o $@ $^

test_app_installer_faults: test_app_installer_faults.o $(CORE_OBJS) $(MBOBJS)
	$(CC) $(STRICT_CFLAGS) -o $@ $^

test_app_install_gate: test_app_install_gate.o api_app_install_module.o app_installer.o app_state_store.o app_host_run.o $(SESSION_OBJS) app_admission.o app_host_validate.o generic_math.o cJSON.o web_api.o mongoose.o $(MBOBJS) gen_stubs.o
	$(CC) $(STRICT_CFLAGS) -o $@ test_app_install_gate.o api_app_install_module.o app_installer.o app_state_store.o app_host_run.o $(SESSION_OBJS) app_admission.o app_host_validate.o generic_math.o cJSON.o web_api.o mongoose.o $(MBOBJS) gen_stubs.o

test_app_installer_storage: test_app_installer_storage.o app_installer_storage.o $(CORE_OBJS) $(MBOBJS)
	$(CC) $(STRICT_CFLAGS) -o $@ test_app_installer_storage.o app_installer_storage.o $(CORE_OBJS) $(MBOBJS)

test_app_install_gate.o: test_app_install_gate.c installer_vectors.h mem_io_backend.h $(REPO_ROOT)/Custom/Services/AppHost/app_installer.h $(REPO_ROOT)/Custom/Services/AppHost/app_host_run.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@


api_app_install_module.o: $(REPO_ROOT)/Custom/Services/Web/api/api_app_install_module.c
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_host_run.o: $(REPO_ROOT)/Custom/Services/AppHost/app_host_run.c $(REPO_ROOT)/Custom/Services/AppHost/app_host_run.h
	$(CC) $(STRICT_CFLAGS) -I$(REPO_ROOT)/tests/sign_spike -c $< -o $@

app_host_session.o: $(REPO_ROOT)/Custom/Services/AppHost/app_host_session.c $(REPO_ROOT)/Custom/Services/AppHost/app_host_session.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_host_live.o: $(REPO_ROOT)/Custom/Services/AppHost/app_host_live.c $(REPO_ROOT)/Custom/Services/AppHost/app_host_live.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_host_control.o: $(REPO_ROOT)/Custom/Services/AppHost/app_host_control.c $(REPO_ROOT)/Custom/Services/AppHost/app_host_control.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_report_service.o: $(REPO_ROOT)/Custom/Services/AppHost/app_report_service.c $(REPO_ROOT)/Custom/Services/AppHost/app_report_service.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_report_policy.o: $(REPO_ROOT)/Custom/Services/AppHost/app_report_policy.c $(REPO_ROOT)/Custom/Services/AppHost/app_report_policy.h $(REPO_ROOT)/Custom/Services/AppHost/app_report_service.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_host_validate.o: $(REPO_ROOT)/Custom/Services/AppHost/app_host_validate.c $(REPO_ROOT)/Custom/Services/AppHost/app_host_validate.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

cJSON.o: $(REPO_ROOT)/Custom/Common/Lib/cJSON/cJSON.c
	$(CC) $(LEGACY_CFLAGS) -c $< -o $@

web_api.o: $(REPO_ROOT)/Custom/Services/Web/web_api.c
	$(CC) $(LEGACY_CFLAGS) -c $< -o $@

mongoose.o: $(REPO_ROOT)/Custom/Common/Lib/mongoose/mongoose.c
	$(CC) $(LEGACY_CFLAGS) -DMG_TLS=0 -c $< -o $@

gen_stubs.c: test_app_install_gate.o api_app_install_module.o app_installer.o app_state_store.o app_host_run.o $(SESSION_OBJS) app_admission.o app_host_validate.o generic_math.o cJSON.o web_api.o mongoose.o $(MBOBJS) $(REPO_ROOT)/tests/storage_gate/gen_stubs.py FORCE
	@python3 $(REPO_ROOT)/tests/storage_gate/gen_stubs.py test_app_install_gate.o api_app_install_module.o app_installer.o app_state_store.o app_host_run.o $(SESSION_OBJS) app_admission.o app_host_validate.o generic_math.o cJSON.o web_api.o mongoose.o $(MBOBJS)

gen_stubs.o: gen_stubs.c
	$(CC) $(STRICT_CFLAGS) -w -c $< -o $@

FORCE:

test_app_installer.o: test_app_installer.c mem_io_backend.h $(REPO_ROOT)/tests/app_host_session/session_vectors.h $(REPO_ROOT)/Custom/Services/AppHost/app_installer.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

test_app_installer_faults.o: test_app_installer_faults.c mem_io_backend.h $(REPO_ROOT)/tests/app_host_session/session_vectors.h $(REPO_ROOT)/Custom/Services/AppHost/app_installer.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

test_app_product_version.o: test_app_product_version.c mem_io_backend.h $(REPO_ROOT)/Custom/Services/AppHost/app_pair_installer.h $(REPO_ROOT)/Custom/Services/AppHost/app_sidecar_lifecycle.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_installer.o: $(REPO_ROOT)/Custom/Services/AppHost/app_installer.c $(REPO_ROOT)/Custom/Services/AppHost/app_installer.h $(REPO_ROOT)/Custom/Services/AppHost/app_sidecar_verify.c $(REPO_ROOT)/Custom/Services/AppHost/app_sidecar_lifecycle.c $(REPO_ROOT)/Custom/Services/AppHost/app_pair_installer.c $(REPO_ROOT)/Custom/Services/AppHost/app_native_vnext_floor.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_state_store.o: $(REPO_ROOT)/Custom/Services/AppHost/app_state_store.c $(REPO_ROOT)/Custom/Services/AppHost/app_state_store.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_installer_storage.o: $(REPO_ROOT)/Custom/Services/AppHost/app_installer_storage.c $(REPO_ROOT)/Custom/Services/AppHost/app_installer.h $(REPO_ROOT)/tests/storage_gate/shim/storage.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

test_app_installer_storage.o: test_app_installer_storage.c installer_vectors.h $(REPO_ROOT)/Custom/Services/AppHost/app_installer.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_admission.o: $(REPO_ROOT)/Custom/Services/AppHost/app_admission.c $(REPO_ROOT)/Custom/Services/AppHost/app_admission.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

generic_math.o: $(REPO_ROOT)/Custom/Common/Utils/generic_math.c
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

build:
	mkdir -p build

build/%.o: $(MBEDTLS)/library/%.c | build
	$(CC) $(STRICT_CFLAGS) -w -c $< -o $@

clean:
	rm -rf build test_app_installer test_app_installer_faults test_app_install_gate test_app_install_gate_synthetic test_app_installer_storage test_app_product_version gen_stubs.c *.o
