CC      ?= cc
CFLAGS  ?= -O2 -g -Wall -Wextra -Wno-unused-parameter -std=c11 -D_DARWIN_C_SOURCE
SRC      = dsnet_link.c dsnet_server.c dsnet_client.c
OBJ      = $(SRC:%.c=build/%.o) build/ds_i18n.o
I18N     = ../ds_i18n.c

all: build/libdsnet.a build/test_dsnet build/test_link build/test_srv build/test_sim build/test_modcheck

build/%.o: %.c dsnet.h dsnet_internal.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

# i18n (TR, UTF-8 cuts): the one DSI_IMPLEMENTATION of the mod, also in every test here
build/ds_i18n.o: ../ds_i18n.c ../ds_i18n.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/libdsnet.a: $(OBJ)
	ar rcs $@ $^

build/test_dsnet: test/test_dsnet.c build/libdsnet.a
	$(CC) $(CFLAGS) $< build/libdsnet.a -lpthread -o $@

build/test_link: test/test_link.c build/libdsnet.a
	$(CC) $(CFLAGS) $< build/libdsnet.a -lpthread -o $@

# the server / client source compiled into the test (static functions fed exact bytes)
build/test_srv: test/test_srv.c dsnet_server.c build/dsnet_link.o build/dsnet_client.o build/ds_i18n.o
	$(CC) $(CFLAGS) $< build/dsnet_link.o build/dsnet_client.o build/ds_i18n.o -lpthread -o $@
build/test_sim: test/test_sim.c dsnet_client.c build/dsnet_link.o build/dsnet_server.o build/ds_i18n.o
	$(CC) $(CFLAGS) $< build/dsnet_link.o build/dsnet_server.o build/ds_i18n.o -lpthread -o $@

# ds_modcheck.h (header only): settings suffix, DSMP_REJOIN line, the loader's hostmods plan JSON
build/test_modcheck: test/test_modcheck.c ds_modcheck.h
	@mkdir -p build
	$(CC) $(CFLAGS) -fsanitize=address,undefined $< -o $@

test: build/test_dsnet build/test_link build/test_srv build/test_sim build/test_modcheck
	nice -n 19 ./build/test_modcheck
	nice -n 19 ./build/test_link
	nice -n 19 ./build/test_srv
	nice -n 19 ./build/test_sim
	nice -n 19 ./build/test_dsnet

# AddressSanitizer build of the library + tests (memory errors abort with a report)
asan:
	@mkdir -p build/asan
	$(CC) $(CFLAGS) -O1 -fsanitize=address -fno-omit-frame-pointer $(SRC) $(I18N) test/test_dsnet.c -lpthread -o build/asan/test_dsnet
	$(CC) $(CFLAGS) -O1 -fsanitize=address -fno-omit-frame-pointer $(SRC) $(I18N) test/test_link.c -lpthread -o build/asan/test_link
	$(CC) $(CFLAGS) -O1 -fsanitize=address -fno-omit-frame-pointer test/test_srv.c dsnet_link.c dsnet_client.c $(I18N) -lpthread -o build/asan/test_srv
	$(CC) $(CFLAGS) -O1 -fsanitize=address -fno-omit-frame-pointer test/test_sim.c dsnet_link.c dsnet_server.c $(I18N) -lpthread -o build/asan/test_sim
	./build/asan/test_link
	./build/asan/test_srv
	./build/asan/test_sim
	./build/asan/test_dsnet --only big_cmd
	./build/asan/test_dsnet --only hello_max
	./build/asan/test_dsnet --only full_big

clean:
	rm -rf build

.PHONY: all test clean asan
