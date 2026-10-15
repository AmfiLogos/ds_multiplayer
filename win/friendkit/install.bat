@echo off
setlocal EnableExtensions
title Dungeon Settlers Multiplayer - install
cd /d "%~dp0"
echo.
echo  Dungeon Settlers Multiplayer - install
echo  ======================================
echo.
if not exist "files\dsmp_baselib.dll" (
  echo  The kit files are missing.
  echo  Right-click the zip file, click "Extract All", then open the extracted folder
  echo  and double-click install.bat there.
  goto fail
)

call files\find_game.bat %1
if defined GAME goto found
echo  The game folder was not found automatically.
echo  1. Open Steam. Right-click Dungeon Settlers. Click Manage, then Browse local files.
echo  2. Click the address bar at the top of that folder window. Press Ctrl+C.
echo  3. Click this window. Press Ctrl+V, then press Enter.
echo.
set "ASK="
set /p "ASK=Game folder: "
if not defined ASK goto fail
set "ASK=%ASK:"=%"
call files\find_game.bat "%ASK%"
if not defined GAME (
  echo.
  echo  DungeonSettlers.exe is not in that folder.
  goto fail
)

:found
echo  Game folder: %GAME%
for %%F in ("%GAME%\GameAssembly.dll") do set "GASIZE=%%~zF"
if not "%GASIZE%"=="90742272" (
  echo.
  echo  Your game version is not the version this kit is for ^(Steam build 25846389^).
  echo  Nothing was changed. Look for a newer kit on the download page.
  goto fail
)
tasklist /fi "imagename eq DungeonSettlers.exe" 2>nul | find /i "DungeonSettlers.exe" >nul
if not errorlevel 1 (
  echo.
  echo  Dungeon Settlers is running. Close the game, then run install.bat again.
  goto fail
)

rem keep the game's own baselib.dll as baselib_orig.dll (not again if ours is already in place)
rem (the game's baselib.dll is about 230 KB; ours is over 1 MB)
set "BLSIZE=0"
for %%F in ("%GAME%\baselib.dll") do if exist "%%~fF" set "BLSIZE=%%~zF"
if %BLSIZE% GTR 0 if %BLSIZE% LSS 1000000 (
  copy /y "%GAME%\baselib.dll" "%GAME%\baselib_orig.dll" >nul
  if errorlevel 1 goto copyfail
)
if not exist "%GAME%\baselib_orig.dll" (
  echo.
  echo  The game file baselib.dll is missing. In Steam, right-click Dungeon Settlers,
  echo  click Properties, Installed Files, Verify integrity of game files. Then run install.bat again.
  goto fail
)
copy /y "files\dsmp_baselib.dll" "%GAME%\baselib.dll" >nul || goto copyfail
copy /y "files\det_ranges_win.tsv" "%GAME%\det_ranges_win.tsv" >nul || goto copyfail
copy /y "files\DSMP_Play.bat" "%GAME%\DSMP_Play.bat" >nul || goto copyfail
copy /y "files\find_game.bat" "%GAME%\DSMP_find_game.bat" >nul
copy /y "uninstall.bat" "%GAME%\DSMP_Uninstall.bat" >nul

rem desktop shortcut "Dungeon Settlers Multiplayer"
set "DESK="
for /f "tokens=2,*" %%a in ('reg query "HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\Shell Folders" /v Desktop 2^>nul') do set "DESK=%%b"
if not defined DESK set "DESK=%USERPROFILE%\Desktop"
powershell -NoProfile -Command "$s=(New-Object -ComObject WScript.Shell).CreateShortcut($env:DESK+'\Dungeon Settlers Multiplayer.lnk'); $s.TargetPath=$env:GAME+'\DSMP_Play.bat'; $s.WorkingDirectory=$env:GAME; $s.IconLocation=$env:GAME+'\DungeonSettlers.exe,0'; $s.WindowStyle=7; $s.Save()" >nul 2>&1
if not exist "%DESK%\Dungeon Settlers Multiplayer.lnk" (
  > "%DESK%\Dungeon Settlers Multiplayer.bat" echo @call "%GAME%\DSMP_Play.bat"
)

echo.
echo  Done. Multiplayer is installed.
echo.
echo  To play multiplayer: double-click "Dungeon Settlers Multiplayer" on your desktop.
echo  To play the normal game: start it from Steam as usual. Steam starts it without the mod.
echo.
pause
exit /b 0

:copyfail
echo.
echo  A file could not be copied into the game folder.
echo  Close the game. Then right-click install.bat and click "Run as administrator".
:fail
echo.
pause
exit /b 1
