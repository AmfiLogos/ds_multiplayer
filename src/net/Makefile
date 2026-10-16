CC      ?= cc
CFLAGS  ?= -O2 -g -Wall -Wextra -Wno-unused-parameter -std=c11 -D_DARWIN_C_SOURCE
SRC      = dsnet_link.c dsnet_server.c dsnet_client.c
OBJ      = $(SRC:%.c=build/%.o)

all: build/libdsnet.a build/test_dsnet build/test_link

build/%.o: %.c dsnet.h dsnet_internal.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/libdsnet.a: $(OBJ)
	ar rcs $@ $^

build/test_dsnet: test/test_dsnet.c build/libdsnet.a
	$(CC) $(CFLAGS) $< build/libdsnet.a -lpthread -o $@

build/test_link: test/test_link.c build/libdsnet.a
	$(CC) $(CFLAGS) $< build/libdsnet.a -lpthread -o $@

test: build/test_dsnet build/test_link
	nice -n 19 ./build/test_link
	nice -n 19 ./build/test_dsnet

# AddressSanitizer build of the library + tests (memory errors abort with a report)
asan:
	@mkdir -p build/asan
	$(CC) $(CFLAGS) -O1 -fsanitize=address -fno-omit-frame-pointer $(SRC) test/test_dsnet.c -lpthread -o build/asan/test_dsnet
	$(CC) $(CFLAGS) -O1 -fsanitize=address -fno-omit-frame-pointer $(SRC) test/test_link.c -lpthread -o build/asan/test_link
	./build/asan/test_link
	./build/asan/test_dsnet --only big_cmd

clean:
	rm -rf build

.PHONY: all test clean asan
