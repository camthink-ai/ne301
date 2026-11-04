CC ?= cc
CFLAGS := -std=c11 -O1 -Wall -Wextra -Werror -I../../Custom/Services/Web -I../../Custom/Common/Inc

test_web_host_policy: test_web_host_policy.c ../../Custom/Services/Web/web_host_policy.c ../../Custom/Services/Web/web_host_policy.h
	$(CC) $(CFLAGS) -o $@ test_web_host_policy.c ../../Custom/Services/Web/web_host_policy.c

.PHONY: test clean
test: test_web_host_policy
	./test_web_host_policy

clean:
	rm -f test_web_host_policy
