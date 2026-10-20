test:
	nice -n 5 cc -O1 -Wall -Wextra -D_DARWIN_C_SOURCE -fsanitize=address,undefined -I.. -o ./extaddr_test_bin extaddr_test.c ../extaddr.c && ./extaddr_test_bin
