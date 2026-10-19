# mp_api v2 tests: seeded random for mods + mod chat limits (no game). `make test`
test:
	@mkdir -p build
	nice -n 5 cc -O1 -g -Wall -Wextra -Wno-unused-parameter -std=gnu11 -D_DARWIN_C_SOURCE -fsanitize=address,undefined -I.. \
	  test_mp_api.c ../mp_rand.c ../mp_modchat.c -o build/test_mp_api && ./build/test_mp_api
.PHONY: test
