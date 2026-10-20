# fixfoc-mp (Foctopus report 2026-10-20, bugs 5 and 7)

Proof: `ext/fixfoc/foc_proof.py [A B C]` (leases a pair, uses its first copy, no mp.json = the owner's app).
New code ALL PASS (en, and C in de / ru / ja). origin/main da9705e + the same test commands: A1 A2 A4 B2 B3 C2 fail.

## Bug 7 "cancelling multiplayer doesn't actually cancel"
1. Host > New save > New campaign, then the new-campaign screen's own Cancel: the title view stays active under that
   screen and `picker_open` only knew the load list (popup 5), so the menu stayed in PICK_NEW forever. The next
   single-player load or new game became a hosted multiplayer lobby (world held until Start).
   Fix: `observe()` counts CreateCampaign (popup 7) too (src/mpmenu.c).
2. Host alone in a world, Leave multiplayer ("You keep playing it alone"): det stayed locked with no session to move
   its target, so the world stood still; trn kept the multiplayer autosave role (a guest that left kept role 0 = its
   own autosaves dropped until a restart). Fix: `end_session_c` calls `trn_clear_role()` + `rig_det_disarm()`.
3. "Saves on every input": not seen on Mac. Save() calls (hooked) and Saves/ file times stayed unchanged during 30-40 s
   of keys after the Cancel and after the Leave, old and new code. Windows not run (VM busy with the release lane).

## Bug 5 tutorial
Tutorial world = campaign dungeon region 7 (DungeonRegion.Tutorial): one unit, scripted pop-ups per player
(popup 14), its own story notices. A guest joined (pair AB) into a world with nothing to control. Choice: block.
Hosting a world in the tutorial does not start a session; the notice box says "Multiplayer could not start / The
tutorial is single-player only: you play it alone. To play together, start a campaign without the tutorial." (10 languages).
- The world notice waits until no stock pop-up is open (a tutorial / story pop-up drew over it).
- `fail()` kept 200 bytes of the reason: the Russian text was cut mid-word; now 640.

Test commands (control channel only): `mpm_newcampaign {tutorial:1}`, `{cancel:1}` (the screen's Cancel button),
`{probe:1}` (popup 7 open), `{popups:1}`, `{close:N}`.
