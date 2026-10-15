Dungeon Settlers Multiplayer - Windows kit
==========================================

You need: Dungeon Settlers from Steam, installed and up to date (Steam build 25846389).
Your normal game, saves and settings do not change. Multiplayer uses a copy of your saves.


INSTALL (one time)
1. Quit Dungeon Settlers.
2. Right-click the zip file. Click "Extract All". Click "Extract".
3. Open the extracted folder. Double-click install.bat.
   If Windows shows "Windows protected your PC": click "More info", then click "Run anyway".
4. A black window opens. It finds the game by itself.
   If it asks for the game folder:
   4.1 In Steam, right-click Dungeon Settlers. Click Manage, then click Browse local files.
   4.2 Click the address bar at the top of that folder window. Press Ctrl+C.
   4.3 Click the black window. Press Ctrl+V. Press Enter.
5. When it says "Done", press any key. The window closes.
   You now have "Dungeon Settlers Multiplayer" on your desktop.


PLAY
1. Double-click "Dungeon Settlers Multiplayer" on your desktop. The game starts with multiplayer on.
2. On the title screen, click Multiplayer.
3. Click Join by address.
4. Type the address that the host gives you (for example 192.168.1.20:47777). Click Connect.
   (If there is no text box: copy the address with Ctrl+C, then click "Connect (address from the clipboard)".)
5. Wait until the host's world loads. The host clicks Start.

The host plays on a Mac with the Mac kit. A Windows player joins. (Hosting from Windows is not tested yet.)
Same home network: use the "Local" address that the host's lobby shows.
Different homes: the host must open TCP port 47777 on the router and send you the router's public
address. This is not tested yet.

To play the normal game, start it from Steam as usual. Steam starts it without the mod.


IF SOMETHING GOES WRONG
- "The multiplayer mod is not in the game folder any more": Steam put the original file back
  (after an update or a file check). Run install.bat again.
- "The game was updated by Steam": this kit is for an older game version. Get the newer kit.
- Antivirus removes baselib.dll: the mod file is not signed. Allow it in your antivirus, then run install.bat again.
- Send these files to the host: the folder %LOCALAPPDATA%\DungeonSettlersMP has rig.log and player.log.
  (Press Windows+R, paste %LOCALAPPDATA%\DungeonSettlersMP, press Enter.)


UNINSTALL
1. Quit Dungeon Settlers.
2. Double-click uninstall.bat in this kit folder.
   (Or: in Steam, right-click Dungeon Settlers, Manage, Browse local files, then double-click DSMP_Uninstall.bat.)
3. When it says "Done", the game is normal again.
Your multiplayer saves stay in %LOCALAPPDATA%\DungeonSettlersMP. Delete that folder if you do not need them.
Steam "Verify integrity of game files" also puts the original game file back.

Source code: https://github.com/AmfiLogos/ds_multiplayer
