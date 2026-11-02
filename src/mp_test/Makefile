# mp tests: mp_core + mp_config + dsnet against a mock game. `make test` (nice 19).
CC      ?= cc
CFLAGS  ?= -O2 -g -Wall -Wextra -Wno-unused-parameter -std=c11 -D_DARWIN_C_SOURCE
NET      = ../net/dsnet_link.c ../net/dsnet_server.c ../net/dsnet_client.c
MP       = ../mp_core.c ../mp_config.c ../ds_i18n.c
HDR      = ../mp.h ../net/dsnet.h ../net/dsnet_internal.h

all: build/test_mp

build/test_mp: test_mp.c $(MP) $(NET) $(HDR)
	@mkdir -p build
	nice -n 19 $(CC) $(CFLAGS) test_mp.c $(MP) $(NET) -lpthread -o $@

test: build/test_mp
	nice -n 19 ./build/test_mp

# the saved owner table that goes with a hosted world (../mp_owners_pick.h); no network, no game
build/owners_pick_test: owners_pick_test.c ../mp_owners_pick.h
	@mkdir -p build
	nice -n 19 $(CC) $(CFLAGS) owners_pick_test.c -o $@

owners: build/owners_pick_test
	./build/owners_pick_test

clean:
	rm -rf build

.PHONY: all test owners clean
