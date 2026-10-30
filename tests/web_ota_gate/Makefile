CC ?= cc
REPO_ROOT := ../..

INC := -I$(REPO_ROOT)/tests/web_ota_gate \
       -I$(REPO_ROOT)/Custom/Services/OTA \
       -I$(REPO_ROOT)/Custom/Common/Lib/pp \
       -I$(REPO_ROOT)/Custom/Common/Lib/mongoose \
       -I$(REPO_ROOT)/tests/storage_gate/shim \
       -I$(REPO_ROOT)/Custom/Common/Lib/MbedTLS/include \
       -I$(REPO_ROOT)/Custom/Common/Lib/MbedTLS/configs \
       -I$(REPO_ROOT)/tests/sign_spike \
       -I$(REPO_ROOT)/Custom/Core/System \
       -I$(REPO_ROOT)/Custom/Services/Web/api \
       -I$(REPO_ROOT)/Custom/Services/Web \
       -I$(REPO_ROOT)/Custom/Core/Data \
       -I$(REPO_ROOT)/Custom/Common/Utils \
       -I$(REPO_ROOT)/Custom/Hal \
       -I$(REPO_ROOT)/Custom/Services/MQTT \
       -I$(REPO_ROOT)/Custom/Services/System \
       -I$(REPO_ROOT)/Custom/Services/Device \
       -I$(REPO_ROOT)/Custom/Services/AI \
       -I$(REPO_ROOT)/Custom/Services/AppHost \
       -I$(REPO_ROOT)/Custom/Core/Video \
       -I$(REPO_ROOT)/Custom/Common/Inc \
       -I$(REPO_ROOT)/Custom/Common/Lib/cJSON \
       -I$(REPO_ROOT)/Custom/Common/Lib/nvs \
       -I$(REPO_ROOT)/Custom/Core/Log \
       -I$(REPO_ROOT)/Middlewares/ST/STM32_ISP_Library/isp/Inc

DEFS := -DMBEDTLS_CONFIG_FILE='"host_user_config.h"' -D_DEFAULT_SOURCE -D'__packed=__attribute__((packed))'
MODULE_CFLAGS := -std=c11 -O1 -g -w $(INC) $(DEFS)
TEST_CFLAGS := -std=c11 -O1 -g -Wall -Wextra -Werror $(INC) $(DEFS)
CORE_OBJS := api_ota_module.o mongoose.o cJSON.o

.PHONY: test clean FORCE
test: test_web_ota_gate
	./test_web_ota_gate

test_web_ota_gate: test_web_ota_gate.o $(CORE_OBJS) gen_stubs.o
	$(CC) $(TEST_CFLAGS) -o $@ $^

gen_stubs.c: test_web_ota_gate.o $(CORE_OBJS) $(REPO_ROOT)/tests/storage_gate/gen_stubs.py FORCE
	@python3 $(REPO_ROOT)/tests/storage_gate/gen_stubs.py test_web_ota_gate.o $(CORE_OBJS)

test_web_ota_gate.o: test_web_ota_gate.c
	$(CC) $(TEST_CFLAGS) -c $< -o $@

api_ota_module.o: $(REPO_ROOT)/Custom/Services/Web/api/api_ota_module.c
	$(CC) $(MODULE_CFLAGS) -c $< -o $@

mongoose.o: $(REPO_ROOT)/Custom/Common/Lib/mongoose/mongoose.c
	$(CC) $(MODULE_CFLAGS) -c $< -o $@

cJSON.o: $(REPO_ROOT)/Custom/Common/Lib/cJSON/cJSON.c
	$(CC) $(MODULE_CFLAGS) -c $< -o $@

clean:
	rm -f test_web_ota_gate *.o gen_stubs.c

FORCE:
