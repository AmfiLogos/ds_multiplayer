# chatfix (v0.1.21 plan section A, owner feedback 2026-10-20)

Code: src/mpmenu.c chat block (chat_parent .. chat_frame), observe() Escape line, the Input.get_mouseScrollDelta hook in
mpmenu_install; src/rig_sock.c jstr decodes \uXXXX (tests send json.dumps text); win/win_map.tsv + 3 rows.

- A6 / A7: own overlay canvas "DSMP_Chat" (DontDestroyOnLoad, sorting 32030: above Teams 32000 / panel 32020, under
  tooltips + notice 32040+), shown whenever a dsnet client exists (connecting, guest waiting, lobby, loading, world).
  Enter opens it in the Esc menu too (the host lobby is that menu); not while another TMP_InputField has the focus.
  Escape while typing closes the chat only (o->escape is 0 then, so no title Back).
- A1 / A2: chat texts leave the text fit (no auto size), wrap at 410 (idle) / 446 (box); rows stacked bottom-up with
  the measured height (GetPreferredValues), cached per line. Box: whole lines only; scrolling stops at the oldest line.
- A3: margins 0, text centred in its plate (5 px padding at 960x540).
- A4: Input.get_mouseScrollDelta (managed) returns 0 while the pointer is over the chat. Callers: MouseIdleState
  DetectWheelInput (Mac) / HandleInput (Windows, DetectWheelInput inlined there), UI scroll, Unity debug UI.
  The chat itself reads the wheel through the icall, so it scrolls.
- A5: the received line was always UTF-8 clean on the wire; lines are now checked per font with the PUBLIC
  TMP_FontAsset.HasCharacter(char, fallbacks, add). Never HasCharacter_Internal: its static "searched" set is only reset
  by the public calls (null at the title = exception; stale = fallbacks skipped). Lora + its fallbacks already cover
  accents, Cyrillic, Korean, Japanese, Chinese; the game's JP / SC / TC fonts are the next candidates; an emoji (no game
  font) becomes '?'. The input field switches font the same way. The 200-byte wire limit is enforced in the field
  (cut on a whole character, caret to the end), so what is shown is what is sent (100 Cyrillic / 66 CJK letters).
- Release lane: run loader tools/hooks/hooklist.py gen (new hook mpmenu Input.get_mouseScrollDelta).

Test: ext/chatfix/chat_pair.py --dylib <fake-mouse build> --out DIR (21 checks; fails on the v0.1.20 code).
ext/chatfix/font_probe.py: which chat fonts have which characters, one copy at the title.
