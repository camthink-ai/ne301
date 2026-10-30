CC ?= cc
REPO_ROOT := ../..
MBEDTLS := $(REPO_ROOT)/Custom/Common/Lib/MbedTLS
BUILD := build
KEY := $(BUILD)/test-v4-key.pem
FOREIGN_KEY := $(BUILD)/test-v4-foreign-key.pem
V3_VECTORS := $(BUILD)/v3_vectors.h
V4_VECTORS := $(BUILD)/v4_vectors.h

INC := -I$(REPO_ROOT)/tests/sign_spike/shim \
       -I$(REPO_ROOT)/tests/storage_gate/shim \
       -I$(REPO_ROOT)/tests/app_installer \
       -I$(REPO_ROOT)/tests/app_admission \
       -I$(REPO_ROOT)/tests/sign_spike \
       -I$(REPO_ROOT)/Custom/Services/AppHost \
       -I$(REPO_ROOT)/Custom/Common/Inc \
       -I$(REPO_ROOT)/Custom/Common/Utils \
       -I$(REPO_ROOT)/Custom/Common/Lib \
       -I$(MBEDTLS)/include \
       -I$(MBEDTLS)/configs \
       -I$(BUILD)

DEFS := -DMBEDTLS_CONFIG_FILE='"host_user_config.h"' -DMBEDTLS_ALLOW_PRIVATE_ACCESS
CFLAGS := -std=c11 -O1 -g -Wall -Wextra -Werror $(INC) $(DEFS)
LIB_SRCS := $(wildcard $(MBEDTLS)/library/[a-z]*.c)
MBOBJS := $(addprefix $(BUILD)/,$(notdir $(LIB_SRCS:.c=.o)))
CORE_OBJS := test_app_native_vnext.o app_installer.o app_state_store.o app_admission.o app_trust_anchor.o generic_math.o

.PHONY: test clean
test:
	$(MAKE) -C ../app_unsigned test

test_app_native_vnext: $(CORE_OBJS) $(MBOBJS)
	$(CC) $(CFLAGS) -o $@ $^

test_app_native_vnext.o: test_app_native_vnext.c $(REPO_ROOT)/tests/app_installer/mem_io_backend.h $(REPO_ROOT)/Custom/Services/AppHost/app_native_vnext_floor.h $(V3_VECTORS) $(V4_VECTORS)
	$(CC) $(CFLAGS) -c $< -o $@

app_installer.o: $(REPO_ROOT)/Custom/Services/AppHost/app_installer.c $(REPO_ROOT)/Custom/Services/AppHost/app_sidecar_lifecycle.c $(REPO_ROOT)/Custom/Services/AppHost/app_native_vnext_floor.h
	$(CC) $(CFLAGS) -c $< -o $@

app_state_store.o: $(REPO_ROOT)/Custom/Services/AppHost/app_state_store.c
	$(CC) $(CFLAGS) -c $< -o $@

app_admission.o: $(REPO_ROOT)/Custom/Services/AppHost/app_admission.c
	$(CC) $(CFLAGS) -c $< -o $@

app_trust_anchor.o: $(REPO_ROOT)/Custom/Services/AppHost/app_trust_anchor.c
	$(CC) $(CFLAGS) -c $< -o $@

generic_math.o: $(REPO_ROOT)/Custom/Common/Utils/generic_math.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KEY): $(REPO_ROOT)/tests/sign_spike/vectors/ecdsa_p256_spike_testkey1.pem | $(BUILD)
	cp $< $@
	chmod 600 $@

$(FOREIGN_KEY): $(REPO_ROOT)/tests/sign_spike/vectors/ecdsa_p256_spike_testkey2.pem | $(BUILD)
	cp $< $@
	chmod 600 $@

$(V3_VECTORS): $(REPO_ROOT)/tests/app_admission/gen_v3_vectors.py $(REPO_ROOT)/tests/app_admission/anchor_keygen.py $(KEY) $(FOREIGN_KEY) | $(BUILD)
	python3 $(REPO_ROOT)/tests/app_admission/gen_v3_vectors.py --key "$(KEY)" --foreign-key "$(FOREIGN_KEY)" --output $@

$(V4_VECTORS): $(REPO_ROOT)/tests/app_admission/gen_v4_vectors.py $(REPO_ROOT)/tests/app_admission/anchor_keygen.py $(KEY) $(FOREIGN_KEY) | $(BUILD)
	python3 $(REPO_ROOT)/tests/app_admission/gen_v4_vectors.py --key "$(KEY)" --foreign-key "$(FOREIGN_KEY)" --output $@

$(BUILD):
	mkdir -p $@

$(BUILD)/%.o: $(MBEDTLS)/library/%.c | $(BUILD)
	$(CC) $(CFLAGS) -w -c $< -o $@

clean:
	rm -rf $(BUILD) test_app_native_vnext *.o
