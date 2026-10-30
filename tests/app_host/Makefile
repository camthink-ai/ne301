CC ?= cc
CFLAGS = -Wall -Wextra -Werror -std=c11
REPO_ROOT = ../..
INC = -I$(REPO_ROOT)/Custom/Common/Inc -I$(REPO_ROOT)/Custom/Common/Utils -I$(REPO_ROOT)/Custom/Services/AppHost

.PHONY: test testapp clean
test: test_app_host_validate
	./test_app_host_validate

testapp:
	$(MAKE) -C testapp

test_app_host_validate: test_app_host_validate.c $(REPO_ROOT)/Custom/Services/AppHost/app_host_validate.c $(REPO_ROOT)/Custom/Common/Utils/generic_math.c
	$(CC) $(CFLAGS) $(INC) -o $@ $^

clean:
	rm -f test_app_host_validate
	$(MAKE) -C testapp clean
