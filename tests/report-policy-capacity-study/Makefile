CC ?= cc
REPO_ROOT := ../..
APPHOST := $(REPO_ROOT)/Custom/Services/AppHost
COMMON_INC := $(REPO_ROOT)/Custom/Common/Inc
MBEDTLS := $(REPO_ROOT)/Custom/Common/Lib/MbedTLS

INC := -I$(APPHOST) -I$(COMMON_INC) -I$(REPO_ROOT)/Custom/Common/Utils \
       -I$(MBEDTLS)/include -I$(MBEDTLS)/configs \
       -I$(REPO_ROOT)/tests/sign_spike/shim -I$(REPO_ROOT)/tests/sign_spike

DEFS := -DMBEDTLS_CONFIG_FILE='"host_user_config.h"' -DMBEDTLS_ALLOW_PRIVATE_ACCESS

CFLAGS := -std=c11 -O1 -g -Wall -Wextra -Werror $(INC) $(DEFS)

LIB_SRCS := $(wildcard $(MBEDTLS)/library/*.c)
MBOBJS := $(addprefix build/,$(notdir $(LIB_SRCS:.c=.o)))

.PHONY: test model clean
test: test_queue_capacity_layout
	./test_queue_capacity_layout

model:
	python3 capacity_model.py

test_queue_capacity_layout: test_queue_capacity_layout.c $(APPHOST)/app_report_service.c $(APPHOST)/app_report_service.h $(APPHOST)/app_report_policy.c $(APPHOST)/app_report_policy.h $(APPHOST)/app_installer.c $(APPHOST)/app_admission.c $(REPO_ROOT)/Custom/Common/Utils/generic_math.c $(MBOBJS)
	$(CC) $(CFLAGS) -o $@ test_queue_capacity_layout.c $(APPHOST)/app_report_policy.c $(APPHOST)/app_installer.c $(APPHOST)/app_admission.c $(REPO_ROOT)/Custom/Common/Utils/generic_math.c $(MBOBJS)

build:
	mkdir -p build

build/%.o: $(MBEDTLS)/library/%.c | build
	$(CC) $(CFLAGS) -w -c $< -o $@

clean:
	rm -rf build test_queue_capacity_layout
