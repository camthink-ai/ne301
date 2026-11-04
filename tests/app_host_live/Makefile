CC ?= cc
REPO_ROOT := ../..

INC := -I$(REPO_ROOT)/Custom/Services/AppHost \
       -I$(REPO_ROOT)/Custom/Common/Inc \
       -I.

CFLAGS := -std=c11 -O1 -g -Wall -Wextra -Werror $(INC) -pthread

CORE_OBJS := test_app_host_live.o app_host_live.o
JOINT_OBJS := test_app_host_live_joint.o app_host_live.o

.PHONY: test clean
test: test_app_host_live test_app_host_live_joint
	./test_app_host_live
	./test_app_host_live_joint

test_app_host_live: $(CORE_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

test_app_host_live_joint: $(JOINT_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

test_app_host_live.o: test_app_host_live.c $(REPO_ROOT)/Custom/Services/AppHost/app_host_live.h
	$(CC) $(CFLAGS) -c $< -o $@

test_app_host_live_joint.o: test_app_host_live_joint.c app27_live_fixtures.h $(REPO_ROOT)/Custom/Services/AppHost/app_host_live.h
	$(CC) $(CFLAGS) -c $< -o $@

app_host_live.o: $(REPO_ROOT)/Custom/Services/AppHost/app_host_live.c $(REPO_ROOT)/Custom/Services/AppHost/app_host_live.h $(REPO_ROOT)/Custom/Services/AppHost/app_admission.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f test_app_host_live test_app_host_live_joint *.o
