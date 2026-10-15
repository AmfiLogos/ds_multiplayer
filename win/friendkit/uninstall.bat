@echo off
setlocal EnableExtensions
title Dungeon Settlers Multiplayer - uninstall
cd /d "%~dp0"
echo.
echo  Dungeon Settlers Multiplayer - uninstall
echo  ========================================
echo.
set "GAME="
if exist "%~dp0DungeonSettlers.exe" set "GAME=%~dp0."
if not defined GAME if exist "files\find_game.bat" call files\find_game.bat %1
if not defined GAME (
  echo  The game folder was not found.
  echo  Open the game folder ^(Steam: right-click Dungeon Settlers, Manage, Browse local files^)
  echo  and double-click DSMP_Uninstall.bat there.
  goto fail
)
for %%G in ("%GAME%") do set "GAME=%%~fG"
echo  Game folder: %GAME%
tasklist /fi "imagename eq DungeonSettlers.exe" 2>nul | find /i "DungeonSettlers.exe" >nul
if not errorlevel 1 (
  echo.
  echo  Dungeon Settlers is running. Close the game, then run the uninstall again.
  goto fail
)

rem our baselib.dll is over 1 MB; the game's own one is about 230 KB
set "BLSIZE=0"
for %%F in ("%GAME%\baselib.dll") do if exist "%%~fF" set "BLSIZE=%%~zF"
if %BLSIZE% LSS 1000000 goto restored
if not exist "%GAME%\baselib_orig.dll" (
  echo.
  echo  The original game file is not here. Steam can put it back:
  echo  in Steam, right-click Dungeon Settlers, click Properties, Installed Files,
  echo  then Verify integrity of game files.
  set "NEEDVERIFY=1"
  goto rest
)
copy /y "%GAME%\baselib_orig.dll" "%GAME%\baselib.dll" >nul
if errorlevel 1 (
  echo.
  echo  The game file could not be put back. Close the game. Then right-click this file
  echo  and click "Run as administrator".
  goto fail
)
:restored
if exist "%GAME%\baselib_orig.dll" del /q "%GAME%\baselib_orig.dll"

:rest
for %%f in (det_ranges_win.tsv DSMP_Play.bat DSMP_find_game.bat) do if exist "%GAME%\%%f" del /q "%GAME%\%%f"
set "DESK="
for /f "tokens=2,*" %%a in ('reg query "HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\Shell Folders" /v Desktop 2^>nul') do set "DESK=%%b"
if not defined DESK set "DESK=%USERPROFILE%\Desktop"
for %%f in (lnk bat) do if exist "%DESK%\Dungeon Settlers Multiplayer.%%f" del /q "%DESK%\Dungeon Settlers Multiplayer.%%f"

echo.
if defined NEEDVERIFY (echo  The mod files are removed. Do the Steam check above to finish.) else (echo  Done. The game is normal again.)
echo  Your multiplayer saves are kept in %LOCALAPPDATA%\DungeonSettlersMP
echo  You can delete that folder if you do not need them.
echo.
pause
if /i "%~nx0"=="DSMP_Uninstall.bat" (goto) 2>nul & del "%~f0"
exit /b 0

:fail
echo.
pause
exit /b 1
