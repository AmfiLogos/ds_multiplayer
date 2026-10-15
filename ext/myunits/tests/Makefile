# My Units + wiki link reader tests (no game). NET=1 make test = also the live short-link checks (dungeonsettlers.wiki).
S = ../../../src
test:
	mkdir -p tmp old
	nice -n 5 cc -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -I$(S) -o ./mu_test_bin mu_test.c \
	  $(S)/myunits.c $(S)/wk_link.c $(S)/wk_http.c $(S)/pvp_wiki.c $(S)/pvp_build.c $(S)/fe/fe_json.c $(S)/fe/fe_save.c \
	  -framework CoreFoundation -framework CFNetwork -lz -lm
	[ ! -d tmp/lib ] || mv tmp/lib old/lib_$$(date +%s)
	MU_TEAM_LINK="$$(cat ../fixtures/team_RzTjHe.txt)" ./mu_test_bin ../fixtures ./tmp
