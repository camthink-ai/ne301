CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=gnu11

BINS = ws_fifo_harness join_replay ownership_test

all: $(BINS)

ws_fifo_harness: ws_fifo_harness.c
	$(CC) $(CFLAGS) -o $@ $< -lpthread

join_replay: join_replay.c
	$(CC) $(CFLAGS) -o $@ $< -lpthread

ownership_test: ownership_test.c
	$(CC) $(CFLAGS) -o $@ $< -lpthread

run: $(BINS)
	./ws_fifo_harness
	./join_replay
	./ownership_test
	python3 source_contract.py

clean:
	rm -f $(BINS)

.PHONY: all run clean
