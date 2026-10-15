# Owner feedback on v12-mvp (2026-10-16) - collect only, no work until he says

1. Title screen "Mods" button: no tooltip (hover card).
2. Title screen: "Mods" goes below "Multiplayer".
3. Tooltips (hover cards): place them OUTSIDE the panel, to the right side, not right below the button: below, they cover the previous/next section and make reading impossible (shot: shots_v12/host_page.png).
4. Multiplayer page: no spacing between the buttons (Your save / New save / Foreign Expedition / Back are glued together).
5. Rules: the rules menu should be outside (its own panel), EXPANDED, not folded behind an extra click.
6. Host a world list: hovering the "Mods: ..." line must show ALL the world's mods (today one truncated line, card shows only "Global cooldown uses cooldown reduction") (shot: shots_v12/host_world.png).
7. (seen in shot, lead note) macOS notice "App Running in Background: DungeonSettlers is running in the background" appears top right when the app starts — side effect of the hidden helper/background flags; players should not see it.
8. (seen in shot, lead note) Foreign Expedition card still says "into a copy of your world" — the neutral hub is not built yet.
Owner: "the multiplayer works"; everything else comes from real testing later.

## Owner decisions 2026-10-16 ~00:30 (after reading the status page)
9. Mods per world: show each world's mods on the RIGHT side of each world row in the world lists, easy to see at a glance.
10. No mod restart inside a world. To change mods, go back to the title screen. (Apply only at the title.) Faster restart at the title if possible.
11. Our own mods (global cooldown, Hell difficulty, and every mod we make) must work on Windows too: "didn't we build a mod loader so it can just work on both?"
12. MelonLoader must work in our loader too (Mac and Windows): "can't we just fix it? If so, let's fix it."
13. PvP v1, very simple: a map (the starting map for now), players appear there, PvP is on. Research: can units fight at base, or only in a dungeon?
14. My Units / Unit Builder / Gallery / Test Fights: NOT in the Mods window. "Think seriously about how to do it."
15. Weapon slot host-only: do it.
16. No upstream pull requests. Extended Hotbar: leave its own check; incompatible mods are acceptable.
17. "Make a serious plan ... then let's go." Owner is low on tokens: plan must be solid and executable without him.
