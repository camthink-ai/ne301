CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=gnu11

BINS = ws_fifo_harness join_replay

all: $(BINS)

ws_fifo_harness: ws_fifo_harness.c
	$(CC) $(CFLAGS) -o $@ $< -lpthread

join_replay: join_replay.c
	$(CC) $(CFLAGS) -o $@ $< -lpthread

run: $(BINS)
	./ws_fifo_harness
	./join_replay

clean:
	rm -f $(BINS)

.PHONY: all run clean
