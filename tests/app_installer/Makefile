CC ?= cc
REPO_ROOT := ../..
MBEDTLS := $(REPO_ROOT)/Custom/Common/Lib/MbedTLS

INC := -I$(REPO_ROOT)/Custom/Common/Lib/pp \
       -I$(REPO_ROOT)/tests/sign_spike/shim \
       -I$(REPO_ROOT)/tests/storage_gate/shim \
       -I$(REPO_ROOT)/Custom/Hal \
       -I$(REPO_ROOT)/Custom/Common/Lib/nvs \
       -I. \
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

CORE_OBJS := app_installer.o app_state_store.o app_admission.o app_trust_anchor.o generic_math.o
SESSION_OBJS := app_host_session.o app_host_live.o app_host_control.o app_report_service.o app_report_policy.o
SYNTHETIC_DIR := build/synthetic
SYNTHETIC_FIXTURES := $(SYNTHETIC_DIR)/fixtures.stamp

.PHONY: test test-unsigned-api clean
test: test-unsigned-api test_app_installer_storage
	./test_app_installer_storage

test-unsigned-api: test_app_install_gate
	python3 ../app_unsigned/gen_fixture.py
	./test_app_install_gate unsigned

test_app_installer: test_app_installer.o $(CORE_OBJS) $(MBOBJS)
	$(CC) $(STRICT_CFLAGS) -o $@ $^

test_app_installer_faults: test_app_installer_faults.o $(CORE_OBJS) $(MBOBJS)
	$(CC) $(STRICT_CFLAGS) -o $@ $^

test_app_install_gate: test_app_install_gate.o api_app_install_module.o app_installer.o app_state_store.o app_host_run.o $(SESSION_OBJS) app_admission.o app_host_validate.o generic_math.o cJSON.o web_api.o mongoose.o $(MBOBJS) gen_stubs.o
	$(CC) $(STRICT_CFLAGS) -o $@ test_app_install_gate.o api_app_install_module.o app_installer.o app_state_store.o app_host_run.o $(SESSION_OBJS) app_admission.o app_host_validate.o generic_math.o cJSON.o web_api.o mongoose.o $(MBOBJS) gen_stubs.o

test_app_installer_storage: test_app_installer_storage.o app_installer_storage.o $(CORE_OBJS) $(MBOBJS)
	$(CC) $(STRICT_CFLAGS) -o $@ test_app_installer_storage.o app_installer_storage.o $(CORE_OBJS) $(MBOBJS)

test_app_install_gate_synthetic: $(SYNTHETIC_DIR)/test_app_install_gate.o api_app_install_module.o app_installer.o app_state_store.o app_host_run.o $(SESSION_OBJS) app_admission.o app_host_validate.o generic_math.o cJSON.o web_api.o mongoose.o $(MBOBJS) $(SYNTHETIC_DIR)/app_trust_anchor.o $(SYNTHETIC_DIR)/gen_stubs.o
	$(CC) $(STRICT_CFLAGS) -o $@ $^

test_app_install_gate.o: test_app_install_gate.c installer_vectors.h mem_io_backend.h $(REPO_ROOT)/Custom/Services/AppHost/app_installer.h $(REPO_ROOT)/Custom/Services/AppHost/app_host_run.h
	$(CC) $(STRICT_CFLAGS) -DNEPK_CI_CANDIDATE_PATH='"$(NEPK_CI_CANDIDATE)"' -DNEPK_TEST_PAIR_SEQ3_PATH='"$(NEPK_TEST_PAIR_SEQ3)"' -DNEPK_TEST_PAIR_SEQ4_PATH='"$(NEPK_TEST_PAIR_SEQ4)"' -DNEPK_TEST_PAIR_SEQ4_CONFLICT_PATH='"$(NEPK_TEST_PAIR_SEQ4_CONFLICT)"' -DNEPK_TEST_PAIR_SEQ5_PATH='"$(NEPK_TEST_PAIR_SEQ5)"' -c $< -o $@

$(SYNTHETIC_FIXTURES): gen_synthetic_pair_fixtures.py $(REPO_ROOT)/tests/app_admission/synthetic_identity.py $(REPO_ROOT)/tests/app_admission/anchor_keygen.py $(REPO_ROOT)/tests/app_admission/gen_v4_vectors.py | build
	python3 gen_synthetic_pair_fixtures.py $(SYNTHETIC_DIR)

$(SYNTHETIC_DIR)/test_app_install_gate.o: test_app_install_gate.c installer_vectors.h mem_io_backend.h $(SYNTHETIC_FIXTURES) $(REPO_ROOT)/Custom/Services/AppHost/app_installer.h $(REPO_ROOT)/Custom/Services/AppHost/app_host_run.h
	$(CC) $(STRICT_CFLAGS) -Wno-unused-function -DAPP_TRUST_SYNTHETIC_GATE -DNEPK_TEST_PAIR_SEQ3_PATH='"$(abspath $(SYNTHETIC_DIR)/seq3.bin)"' -DNEPK_TEST_PAIR_SEQ4_PATH='"$(abspath $(SYNTHETIC_DIR)/seq4.bin)"' -DNEPK_TEST_PAIR_SEQ4_CONFLICT_PATH='"$(abspath $(SYNTHETIC_DIR)/seq4-conflict.bin)"' -DNEPK_TEST_PAIR_SEQ5_PATH='"$(abspath $(SYNTHETIC_DIR)/seq5.bin)"' -DNEPK_TEST_PAIR_MIXED_PUBLISHER_PATH='"$(abspath $(SYNTHETIC_DIR)/mixed-publisher.bin)"' -DNEPK_TEST_PAIR_MIXED_SPKI_PATH='"$(abspath $(SYNTHETIC_DIR)/mixed-spki.bin)"' -DNEPK_TEST_PAIR_INBAND_ROOT_PATH='"$(abspath $(SYNTHETIC_DIR)/inband-root-replacement.bin)"' -DNEPK_TEST_PAIR_MALFORMED_PATH='"$(abspath $(SYNTHETIC_DIR)/malformed.bin)"' -c $< -o $@

$(SYNTHETIC_DIR)/app_trust_anchor.o: $(REPO_ROOT)/Custom/Services/AppHost/app_trust_anchor.c $(REPO_ROOT)/Custom/Services/AppHost/app_trust_anchor.h $(SYNTHETIC_FIXTURES)
	$(CC) $(STRICT_CFLAGS) -DAPP_TRUST_PROFILE=APP_TRUST_PROFILE_PRODUCTION -DAPP_TRUST_PROVISION_HEADER='"$(SYNTHETIC_DIR)/trust_config.h"' -c $< -o $@

$(SYNTHETIC_DIR)/gen_stubs.c: $(SYNTHETIC_DIR)/test_app_install_gate.o api_app_install_module.o app_installer.o app_state_store.o app_host_run.o $(SESSION_OBJS) app_admission.o app_host_validate.o generic_math.o cJSON.o web_api.o mongoose.o $(MBOBJS) $(SYNTHETIC_DIR)/app_trust_anchor.o $(REPO_ROOT)/tests/storage_gate/gen_stubs.py FORCE
	cd $(SYNTHETIC_DIR) && pwd && python3 ../../../../tests/storage_gate/gen_stubs.py test_app_install_gate.o ../../api_app_install_module.o ../../app_installer.o ../../app_state_store.o ../../app_host_run.o ../../app_host_session.o ../../app_host_live.o ../../app_host_control.o ../../app_report_service.o ../../app_report_policy.o ../../app_admission.o ../../app_host_validate.o ../../generic_math.o ../../cJSON.o ../../web_api.o ../../mongoose.o app_trust_anchor.o $(addprefix ../../,$(MBOBJS))

$(SYNTHETIC_DIR)/gen_stubs.o: $(SYNTHETIC_DIR)/gen_stubs.c
	$(CC) $(STRICT_CFLAGS) -w -c $< -o $@

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

test_app_installer.o: test_app_installer.c installer_vectors.h mem_io_backend.h $(REPO_ROOT)/Custom/Services/AppHost/app_installer.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

test_app_installer_faults.o: test_app_installer_faults.c installer_vectors.h mem_io_backend.h $(REPO_ROOT)/Custom/Services/AppHost/app_installer.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_installer.o: $(REPO_ROOT)/Custom/Services/AppHost/app_installer.c $(REPO_ROOT)/Custom/Services/AppHost/app_installer.h $(REPO_ROOT)/Custom/Services/AppHost/app_sidecar_lifecycle.c $(REPO_ROOT)/Custom/Services/AppHost/app_native_vnext_floor.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_state_store.o: $(REPO_ROOT)/Custom/Services/AppHost/app_state_store.c $(REPO_ROOT)/Custom/Services/AppHost/app_state_store.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_installer_storage.o: $(REPO_ROOT)/Custom/Services/AppHost/app_installer_storage.c $(REPO_ROOT)/Custom/Services/AppHost/app_installer.h $(REPO_ROOT)/tests/storage_gate/shim/storage.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

test_app_installer_storage.o: test_app_installer_storage.c installer_vectors.h $(REPO_ROOT)/Custom/Services/AppHost/app_installer.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_admission.o: $(REPO_ROOT)/Custom/Services/AppHost/app_admission.c $(REPO_ROOT)/Custom/Services/AppHost/app_admission.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_trust_anchor.o: $(REPO_ROOT)/Custom/Services/AppHost/app_trust_anchor.c $(REPO_ROOT)/Custom/Services/AppHost/app_trust_anchor.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

generic_math.o: $(REPO_ROOT)/Custom/Common/Utils/generic_math.c
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

build:
	mkdir -p build

build/%.o: $(MBEDTLS)/library/%.c | build
	$(CC) $(STRICT_CFLAGS) -w -c $< -o $@

clean:
	rm -rf build test_app_installer test_app_installer_faults test_app_install_gate test_app_install_gate_synthetic test_app_installer_storage gen_stubs.c *.o
