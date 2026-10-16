# My Units, wiki links and the Compact Plus card: tests with no game. Binaries go to build/units (ignored).
#   make -C custom/units/test test        unit tests (store, links, one click, folding, card)
#   NET=1 make -C custom/units/test test  + the live short links on dungeonsettlers.wiki and the 5 s limit
#   make -C custom/units/test builder     C reader + card vs the wiki builder itself (needs node + ~/dungeonsettlersdb)
#   make -C custom/units/test tables      regenerate ../mu_card_tables.h after a wiki data update
U = ..
C = ../..
O = ../../../build/units
CF = -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -fsanitize=address,undefined -I$(U) -I$(C) -I$(C)/fe
LINK = $(U)/mu_card.c $(U)/mu_link.c $(U)/mu_clip.c $(U)/myunits.c $(U)/wk_link.c $(U)/wk_http.c $(U)/pvp_wiki.c $(U)/pvp_build.c \
  $(C)/fe/fe_json.c $(C)/fe/fe_save.c -framework CoreFoundation -framework CFNetwork -framework AppKit -lz -lm
N ?= 1500
test:
	mkdir -p $(O) tmp old
	nice -n 5 cc $(CF) -o $(O)/mu_test mu_test.c $(LINK)
	[ ! -d tmp/run ] || mv tmp/run old/run_$$(date +%s)
	mkdir -p tmp/run
	node node/make_team.js 3 5 > tmp/run/team3.txt
	MU_TEAM_LINK="$$(cat tmp/run/team3.txt)" $(O)/mu_test data tmp/run
builder:
	mkdir -p $(O)
	nice -n 5 cc $(CF) -o $(O)/card_tool card_tool.c $(LINK)
	nice -n 10 node builder_vs_c.js $(O)/card_tool $(N) 11
	nice -n 10 node builder_vs_c.js $(O)/card_tool $(N) 424242
tables:
	node ../tools/gen_card_tables.js > ../mu_card_tables.h.new && mv ../mu_card_tables.h.new ../mu_card_tables.h
	node ../tools/gen_wiki_tables.js > $(U)/pvp_wiki_tables.h.new && mv $(U)/pvp_wiki_tables.h.new $(U)/pvp_wiki_tables.h
.PHONY: test builder tables
