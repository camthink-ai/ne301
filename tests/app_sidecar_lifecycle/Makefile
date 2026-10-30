CC ?= cc
REPO_ROOT := ../..
MBEDTLS := $(REPO_ROOT)/Custom/Common/Lib/MbedTLS
SIDECAR_BUILD := $(REPO_ROOT)/tests/app_sidecar/build

INC := -I$(REPO_ROOT)/tests/sign_spike/shim \
       -I$(REPO_ROOT)/tests/storage_gate/shim \
       -I$(REPO_ROOT)/tests/sign_spike \
       -I. \
       -I$(REPO_ROOT)/tests/app_installer \
       -I$(REPO_ROOT)/Custom/Hal \
       -I$(REPO_ROOT)/Custom/Services/AppHost \
       -I$(REPO_ROOT)/Custom/Services/Web \
       -I$(REPO_ROOT)/Custom/Common/Inc \
       -I$(REPO_ROOT)/Custom/Common/Lib \
       -I$(REPO_ROOT)/Custom/Common/Lib/nvs \
       -I$(REPO_ROOT)/Custom/Common/Lib/cJSON \
       -I$(REPO_ROOT)/Custom/Common/Utils \
       -I$(MBEDTLS)/include \
       -I$(MBEDTLS)/configs \
       -I$(SIDECAR_BUILD)

DEFS := -DMBEDTLS_CONFIG_FILE='"host_user_config.h"' -DMBEDTLS_ALLOW_PRIVATE_ACCESS
STRICT_CFLAGS := -std=c11 -O1 -g -Wall -Wextra -Werror $(INC) $(DEFS)
LIB_SRCS := $(wildcard $(MBEDTLS)/library/*.c)
MBOBJS := $(addprefix build/,$(notdir $(LIB_SRCS:.c=.o)))
CORE_OBJS := app_installer.o app_state_store.o app_admission.o app_trust_anchor.o generic_math.o

.PHONY: test clean vectors
test:
	$(MAKE) -C ../app_unsigned test

vectors:
	$(MAKE) -C $(REPO_ROOT)/tests/app_sidecar build/vectors
	python3 gen_lifecycle_fixtures.py --output build
	cp golden.neui golden.neapp.v3 test-publisher.spki.der build/

$(SIDECAR_BUILD)/sidecar_vectors.h:
	$(MAKE) -C $(REPO_ROOT)/tests/app_sidecar build/vectors

test_app_sidecar_lifecycle: test_app_sidecar_lifecycle.o $(CORE_OBJS) $(MBOBJS)
	$(CC) $(STRICT_CFLAGS) -o $@ $^

test_app_sidecar_lifecycle.o: test_app_sidecar_lifecycle.c mem_sidecar_backend.h $(SIDECAR_BUILD)/sidecar_vectors.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_installer.o: $(REPO_ROOT)/Custom/Services/AppHost/app_installer.c $(REPO_ROOT)/Custom/Services/AppHost/app_sidecar_verify.c $(REPO_ROOT)/Custom/Services/AppHost/app_sidecar_lifecycle.c $(REPO_ROOT)/Custom/Services/AppHost/app_pair_installer.c $(REPO_ROOT)/Custom/Services/AppHost/app_pair_installer.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_state_store.o: $(REPO_ROOT)/Custom/Services/AppHost/app_state_store.c
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_admission.o: $(REPO_ROOT)/Custom/Services/AppHost/app_admission.c
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_trust_anchor.o: $(REPO_ROOT)/Custom/Services/AppHost/app_trust_anchor.c
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

generic_math.o: $(REPO_ROOT)/Custom/Common/Utils/generic_math.c
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

build:
	mkdir -p build

build/%.o: $(MBEDTLS)/library/%.c | build
	$(CC) -std=c11 -O1 -g -w $(INC) $(DEFS) -c $< -o $@

clean:
	rm -rf build test_app_sidecar_lifecycle *.o
