@echo off
rem ------------------------------------------------------------------
rem  OpenJK VR dedicated server
rem
rem  Runs a server for VR players (and players without VR) to join -
rem  no game window, no headset, it keeps going while nobody plays.
rem  Settings: base\server.cfg in this folder (open it in Notepad).
rem  Type server commands in its window: status, map mp/ffa3,
rem  kick <name>, addbot <name>, svsay <text>, quit ...
rem  See README-SERVER.txt.
rem
rem  Start it before the game: whichever starts first gets UDP port
rem  29070, the one players connect to. Extra arguments are passed on,
rem  e.g.  "Launch OpenJK VR Server.bat" +set sv_maxclients 16
rem ------------------------------------------------------------------
setlocal
set "HERE=%~dp0"
for %%I in ("%~dp0..") do set "GAMEDATA=%%~fI"

rem 2: an internet server, also listed in the game's server browser
rem 1: only for your own network (LAN)
set "DEDICATED=2"
set "PORT=29070"

if not exist "%GAMEDATA%\base\assets0.pk3" (
	echo Could not find "%GAMEDATA%\base\assets0.pk3".
	echo Put the OpenJK-VR folder inside Jedi Academy's GameData folder.
	pause
	exit /b 1
)

netstat -an -p UDP | findstr /C:":%PORT% " >nul
if not errorlevel 1 (
	echo UDP port %PORT% is already in use - most likely by the game, which
	echo takes it when it starts. Close the game and start this first, then
	echo the game. Or press a key to start the server on the next free port
	echo ^(then players connect to your address with :29071 after it^).
	pause
)

title OpenJK VR Server
cd /d "%HERE%"
"%HERE%openjkded.x86_64.exe" +set fs_cdpath "%GAMEDATA%" +set com_homepath "OpenJK-VR-Server" +set dedicated %DEDICATED% +set net_port %PORT% +exec server.cfg %*
echo.
echo The server has stopped.
pause
