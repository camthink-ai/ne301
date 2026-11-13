CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Werror -std=c11

.PHONY: test

test: enc_force_idr_harness
	./enc_force_idr_harness
	python3 source_contract.py

enc_force_idr_harness: enc_force_idr_harness.c
	$(CC) $(CFLAGS) -o $@ $<
