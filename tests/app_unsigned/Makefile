CC ?= cc
REPO_ROOT := ../..
MBEDTLS := $(REPO_ROOT)/Custom/Common/Lib/MbedTLS
INC := -I$(REPO_ROOT)/tests/sign_spike/shim \
       -I$(REPO_ROOT)/tests/storage_gate/shim \
       -I$(REPO_ROOT)/tests/app_installer \
       -I$(REPO_ROOT)/tests/app_sidecar_lifecycle \
       -I$(REPO_ROOT)/Custom/Services/AppHost \
       -I$(REPO_ROOT)/Custom/Common/Inc \
       -I$(REPO_ROOT)/Custom/Common/Utils \
       -I$(REPO_ROOT)/Custom/Core/Data \
       -I$(REPO_ROOT)/Custom/Common/Lib \
       -I$(REPO_ROOT)/tests/sign_spike \
       -I$(MBEDTLS)/include \
       -I$(MBEDTLS)/configs
DEFS := -D_GNU_SOURCE -DMBEDTLS_CONFIG_FILE='"host_user_config.h"' -DMBEDTLS_ALLOW_PRIVATE_ACCESS
CFLAGS := -std=c11 -O1 -g -Wall -Wextra -Werror -Wno-unused-function -ffunction-sections -fdata-sections $(INC) $(DEFS)
LIB_SRCS := $(wildcard $(MBEDTLS)/library/*.c)

# Apple ld uses -dead_strip; GNU ld uses --gc-sections.
ifeq ($(shell uname -s),Darwin)
GC_SECTIONS_FLAG := -Wl,-dead_strip
else
GC_SECTIONS_FLAG := -Wl,--gc-sections
endif
CORE_SRCS := $(REPO_ROOT)/Custom/Services/AppHost/app_admission.c \
             $(REPO_ROOT)/Custom/Services/AppHost/app_installer.c \
             $(REPO_ROOT)/Custom/Services/AppHost/app_state_store.c \
             $(REPO_ROOT)/Custom/Common/Utils/generic_math.c

.PHONY: test clean
test: test_unsigned_admission
	python3 gen_fixture.py
	python3 verify_fixture.py single-file-v5.neapp
	python3 test_version_boundaries.py
	./test_unsigned_admission

test_unsigned_admission: test_unsigned_admission.c $(CORE_SRCS) \
                         $(REPO_ROOT)/Custom/Services/AppHost/app_sidecar_verify.c \
                         $(REPO_ROOT)/Custom/Services/AppHost/app_sidecar_lifecycle.c \
                         $(REPO_ROOT)/Custom/Services/AppHost/app_pair_installer.c
	$(CC) $(CFLAGS) $(GC_SECTIONS_FLAG) -w -o $@ test_unsigned_admission.c $(CORE_SRCS) $(LIB_SRCS)

clean:
	rm -rf test_unsigned_admission test_unsigned_admission.dSYM *.neapp *.neui
