# Owner feedback 2026-10-15 (afternoon, after real play) - source for the master plan

Owner quotes are verbatim (voice dictation, lightly trimmed). Screenshots: research/owner_shot_host_page_2026-10-15.png, research/owner_shot_bring_units_2026-10-15.png.

## Owner's process order
- "Please make a full-fledged plan with all of my feedback and then implement it thoroughly while taking consideration of everything rather than just building on top of each other things that kind of don't make sense. So just reorganize everything properly and then start building."
- "There are things that matter and things that don't matter, and right now you focus on things that don't matter." "You are inventing things. Did I ask you for any of this?" -> build only what he asked; no invented extras; no owner questions about internal choices.
- Reviews: only flag what breaks in production or loses data; "found nothing" is fine.

## Mod loader (NOT a launcher)
- "What I wanted was a mod loader, not a mod launcher. I wanted something more like tModLoader that boots the game and then inside of the booter you can do things."
- Launcher problems seen: "if I do play this world... it just launched a regular game version where I will still need to choose which game to launch"; "the world selection is kind of useless because I cannot start a world already"; "the UI at times is a bit bad".
- Keep: "the mods is fine and save these mods as group. I would want a tab just to control the mods. Unit builder is fine, my units".
- "No, I don't want a Windows version of the launcher. The only thing I want is a mod loader. There's no need for mod launcher. That was never what I asked." (Windows players still need the mod loader.)
- Applying mods: "we can do a soft restart in the back without reloading the game screens ... put a loading screen a bit the same way you would if people is entering in a dungeon and then put people back exactly where they were ... reload the game in the back, cut the fluff that takes time to load the game, so that you give the illusion that we are just reloading while actually we are relaunching the game."
- Logo: Mud Slogel idle sprite (decided).
- Signing key: "I don't care about the key." The mod list is open for anyone to change.

## Units, wiki links, gallery, fights
- "when I save some units and I can select bring to pvp for expedition I have no way to actually select them so it's a bit odd"
- "for the wiki link when I click it tells me to paste which is odd, I should just click and it should open the thing with the clipboard"
- "it doesn't work with the short links, it should ... do the effort and check the folding"
- "I just don't understand how the my units thing is supposed to work."
- "The custom units that are created cannot be inserted inside of your regular games. And also when you create a unit, make sure that it's saved inside of the game, so that if I want to use the same unit multiple times inside of a hub, I can. If I go to a hub with a custom unit, because it cannot be imported to a game, then it shouldn't be deleted but it shouldn't be saved either."
- "The custom fights and the boss fights should just follow the way it's done inside of the wiki so that people can do it directly inside of the game without having to download a save and put it inside of their files."
- "integrate the build gallery inside of the mod loader, so that people can decide to test some build and they can have the choice between their team or their units, or they can choose in the gallery build units or teams they want to select."
- Builder placement: "I asked you for the builder, but the builder was supposed to be something inside of the launcher, the special mod launcher. But if we want to have the hub and PvP in special modes, we want to allow people to create custom units inside of the multiplayer ... I'm not really sure how to organize it so think about it."

## Multiplayer menus and flows
- Host page screenshot: "The title is glitched" (World row arrows mis-encoded, text overflowing). "Multiplayer on the homepage shouldn't have a hover card."
- "A host game after you click multiplayer should have written right below it Your save, new save or foreign expedition as options so that it's pretty clear what can be done."
- Save lists: "I have some worlds that are duplicated in single player and multiplayer ... when I choose a world to play in single player, it should only show single player by default with maybe a toggle to show multiplayer worlds, and when I play multiplayer it should by default only show the worlds that are multiplayer, and a toggle can show me single worlds. So even if we make a multiplayer-only copy of a game version, in single player I don't see it and in multiplayer I only see the multiplayer version."
- Multiplayer tag: "add a tag multiplayer to a world ... as soon as another friend connects to the same world as you, and the tag you could deactivate it ... if I played the multiplayer once but I'm going to finish it on my own, you can remove it. And put the multiplayer tag next to the name of the save and then just put the players that connected on it."
- MP app should start Steam by itself so it can validate at launch.
- Install today needs Terminal (Mac); Windows kit uses install.bat.

## Foreign Expedition and PvP
- "the foreign expedition is like some kind of a neutral hub. So we load our characters and the character of someone else into some kind of a basic map. We have everything unlocked. ... right now it still asks me host from a save or host a new campaign"
- Bring Units: "you have some basic overflow of text and you don't have any icons for the units which there should be"
- "death is permanent also in the hub; once choosing a hub I wasn't able to choose the settings ... death is permanent, XP counts, dungeon loot comes back, 3 units each, and maybe that's not what I would want to do, and then what if I wanted to do PvP ... it's been kind of done in a bit of a lazy way. And death is permanent should be off by default."
- Unit display: "use something like we have in the wiki where we show the avatar, what the NPC looks like, a quick view of the items being worn and the skills in the skill bar, the branches being used, the passive, the inscriptions, so that we can have a very easy view. A bit like the Compact Plus view."
- "I wasn't able to test the PvP and the hub because they don't appear here" (he ran v10 then; v11 has them now).
- PLAN.md earlier owner intent: FE = "a fully kitted hub world"; units are COPIED (home keeps them).

## In-game multiplayer HUD
- Follow a friend's camera: that friend's view frame / markers constantly shown = annoying.
- "if my view is inside of the view of a friend, then I don't see the view of my friend at all. But I would want to see that they look at the same thing as me."
- "in the settings, I would want something to be able to hide the teams and all of this."
- "when I press on teams the players and settings bar disappears which it shouldn't"
- "I would want to be able to collapse both players and teams"
- "when you have no units left at the base, the player's window that shows you the connection and the settings for multiplayer kind of bugs out"
- See-through: "I want to be able to see them without any transparency" (Off step).

## Mods and sync
- Weapon-skill slot: host-only is fine ("it doesn't really matter").
- Faster late join: "put it as a toggle ... if we're not sure it's good yet".
- Players are never on different floors.
- GitHub public, MIT; download page for kits ("sure").

## Lanes / branches that exist (state when the plan starts)
Finished or running fixes (keep): seethrough, weaponslot, latejoin, steamstart (+ steamstart-win), winkit, uifix11 (World row encoding + Multiplayer tooltip done; host choices in progress), viewui (HUD items), sockpath, bepinex-bridge, softrestart (proof of hidden restart).
Stopped mid-work (worktrees on disk; reuse, do not lose): fehub (../ds_mp_fehub), myunits (../ds_mp_myunits), modsmenu (~/ds_launcher worktree + ../ds_mp_modsmenu), mptag (../ds_mp_mptag).
Launcher (not wanted): ~/ds_launcher branches ownerapp, winlauncher; reusable parts: game detection/copy/Steam start on Windows, Unit Builder logic, wiki decoder (Rust), modset cross-platform hash finding.
Plans: research/plan_modloader.md (corrected), research/plan_foreign_expedition_spectator.md, research/plan_pvp.md, research/plan_launcher.md (obsolete).
