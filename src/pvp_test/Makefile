test:
	nice -n 5 cc -O1 -g -Wall -Wextra -fsanitize=address,undefined -o ./pvp_test_bin pvp_test.c ../pvp_core.c ../pvp_modcheck.c ../fe/fe_json.c && ./pvp_test_bin
