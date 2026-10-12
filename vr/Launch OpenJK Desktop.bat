@echo off
rem ------------------------------------------------------------------
rem  OpenJK VR (multiplayer) - the same game without the headset
rem
rem  For friends without VR (they see VR players' real arms, heads and
rem  blades too), or to watch yourself: start the VR game first and
rem  host, then run this and in its console (Shift+~) type
rem     connect 127.0.0.1
rem  (see "Watching yourself" in README-VR.txt).
rem
rem  It keeps its own settings and logs in
rem     Documents\My Games\OpenJK-VR-Desktop
rem  so it never changes your VR setup. Extra arguments are passed on,
rem  e.g.  "Launch OpenJK Desktop.bat" +connect 1.2.3.4
rem ------------------------------------------------------------------
setlocal
set "HERE=%~dp0"
for %%I in ("%~dp0..") do set "GAMEDATA=%%~fI"

if not exist "%GAMEDATA%\base\assets0.pk3" (
	echo Could not find "%GAMEDATA%\base\assets0.pk3".
	echo Put the OpenJK-VR folder inside Jedi Academy's GameData folder.
	pause
	exit /b 1
)

cd /d "%HERE%"
start "" "%HERE%openjk.x86_64.exe" +set fs_cdpath "%GAMEDATA%" +set com_homepath "OpenJK-VR-Desktop" +set vr_enabled 0 %*
