CC ?= cc
REPO_ROOT := ../..

INC := -I$(REPO_ROOT)/tests/mem_pool_guard \
       -I$(REPO_ROOT)/Custom/Hal \
       -I$(REPO_ROOT)/Custom/Common/Lib/mpool \
       -I$(REPO_ROOT)/Custom/Common/Inc \
       -I$(REPO_ROOT)/Custom/Core/System \
       -I$(REPO_ROOT)/tests/app_host_session/devshim \
       -I$(REPO_ROOT)/tests/storage_gate/shim

DEFS := -D_DEFAULT_SOURCE
MODULE_CFLAGS := -std=c11 -O1 -g -w $(INC) $(DEFS)
TEST_CFLAGS := -std=c11 -O1 -g -Wall -Wextra -Werror $(INC) $(DEFS)
CORE_OBJS := mem.o

.PHONY: test clean FORCE
test: test_mem_pool_guard
	./test_mem_pool_guard

test_mem_pool_guard: test_mem_pool_guard.o $(CORE_OBJS) gen_stubs.o
	$(CC) $(TEST_CFLAGS) -o $@ $^

gen_stubs.c: test_mem_pool_guard.o $(CORE_OBJS) $(REPO_ROOT)/tests/storage_gate/gen_stubs.py FORCE
	@python3 $(REPO_ROOT)/tests/storage_gate/gen_stubs.py test_mem_pool_guard.o $(CORE_OBJS)

test_mem_pool_guard.o: test_mem_pool_guard.c
	$(CC) $(TEST_CFLAGS) -c $< -o $@

mem.o: $(REPO_ROOT)/Custom/Hal/mem.c
	$(CC) $(MODULE_CFLAGS) -c $< -o $@

clean:
	rm -f test_mem_pool_guard *.o gen_stubs.c

FORCE:
