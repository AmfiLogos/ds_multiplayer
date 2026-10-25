test:
	nice -n 5 cc -O1 -g -Wall -Wextra -fsanitize=address,undefined -o ./pvpr_test_bin pvpr_test.c ../pvp_rules_core.c ../pvp_maps.c -I.. && ./pvpr_test_bin
