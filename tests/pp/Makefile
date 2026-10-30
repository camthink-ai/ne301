CC ?= cc
CFLAGS := -std=c11 -O1 -Wall -Wextra -Werror -I../../Custom/Common/Lib/pp

.PHONY: test clean

test: test_pp_name_type
	./test_pp_name_type

test_pp_name_type: test_pp_name_type.c ../../Custom/Common/Lib/pp/pp.c ../../Custom/Common/Lib/pp/pp.h
	$(CC) $(CFLAGS) -o $@ test_pp_name_type.c ../../Custom/Common/Lib/pp/pp.c

clean:
	rm -f test_pp_name_type
