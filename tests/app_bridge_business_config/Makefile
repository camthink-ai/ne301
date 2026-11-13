CC ?= cc
REPO_ROOT := ../..
MBEDTLS := $(REPO_ROOT)/Custom/Common/Lib/MbedTLS

INC := -I$(REPO_ROOT)/Custom/Common/Lib/pp \
       -I$(REPO_ROOT)/tests/storage_gate/shim \
       -I$(REPO_ROOT)/tests/sign_spike/shim \
       -I. \
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
       -I$(REPO_ROOT)/Custom/Common/Lib/mmx108/mm_shims \
       -I$(REPO_ROOT)/Custom/Common/Lib/mmx108/morselib/include \
       -I$(REPO_ROOT)/Custom/Common/Lib/cJSON \
       -I$(REPO_ROOT)/Custom/Common/Lib/mongoose \
       -I$(REPO_ROOT)/Custom/Common/Utils \
       -I$(REPO_ROOT)/Middlewares/ST/STM32_ISP_Library/isp/Inc \
       -I$(REPO_ROOT)/BSP/Utilities/Fonts \
       -I$(MBEDTLS)/include \
       -I$(MBEDTLS)/configs

DEFS := -D_DEFAULT_SOURCE -DNE301_APP_HOST_POC=1 -D'__packed=__attribute__((packed))'
STRICT_CFLAGS := -std=c11 -O1 -g -Wall -Wextra -Werror $(INC) $(DEFS)

.PHONY: test clean

test: test_app_bridge_business_config
	./test_app_bridge_business_config

test_app_bridge_business_config: test_app_bridge_business_config.o app_bridge_business_config.o cJSON.o
	$(CC) $(STRICT_CFLAGS) -o $@ $^

test_app_bridge_business_config.o: test_app_bridge_business_config.c $(REPO_ROOT)/Custom/Services/Web/api/app_bridge_business_config.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_bridge_business_config.o: $(REPO_ROOT)/Custom/Services/Web/api/app_bridge_business_config.c $(REPO_ROOT)/Custom/Services/Web/api/app_bridge_business_config.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

cJSON.o: $(REPO_ROOT)/Custom/Common/Lib/cJSON/cJSON.c
	$(CC) -std=c11 -O1 -g -w $(INC) $(DEFS) -c $< -o $@

clean:
	rm -f test_app_bridge_business_config *.o
