@echo off
rem Dungeon Settlers Multiplayer: starts the game with the multiplayer mod on.
rem Multiplayer saves and settings: %LOCALAPPDATA%\DungeonSettlersMP (the first start copies your normal saves there).
setlocal EnableExtensions
cd /d "%~dp0"
rem our baselib.dll is over 1 MB; the game's own one is about 230 KB
set "BLSIZE=0"
for %%F in (baselib.dll) do if exist "%%~fF" set "BLSIZE=%%~zF"
if %BLSIZE% LSS 1000000 goto notinstalled
for %%F in (GameAssembly.dll) do if not "%%~zF"=="90742272" goto newgame

set "MP=%LOCALAPPDATA%\DungeonSettlersMP"
set "REAL=%USERPROFILE%\AppData\LocalLow\CanOpener\Dungeon Settlers"
if not exist "%MP%\data" mkdir "%MP%\data"
if exist "%MP%\data\copied_from_normal_game.txt" goto start
if exist "%REAL%\Saves" xcopy /e /i /q /y "%REAL%\Saves" "%MP%\data\Saves" >nul
for %%f in (OutGame.json UserSetting.json UserCampaignLog.json) do if exist "%REAL%\%%f" copy /y "%REAL%\%%f" "%MP%\data\%%f" >nul
> "%MP%\data\copied_from_normal_game.txt" echo Saves copied from "%REAL%" on %date% %time%

:start
set "DSRIG_DIR=%MP%"
set "DSRIG_LIVE=1"
set "DSRIG_FPS=60"
start "" "%~dp0DungeonSettlers.exe" -logFile "%MP%\player.log"
exit /b 0

:notinstalled
echo.
echo  The multiplayer mod is not in the game folder any more.
echo  A Steam update or "Verify integrity of game files" puts the original game file back.
echo  Run install.bat from the kit again.
echo.
pause
exit /b 1

:newgame
echo.
echo  The game was updated by Steam. This multiplayer kit is for an older game version.
echo  Look for a newer kit on the download page. The normal game still works from Steam.
echo.
pause
exit /b 1
