@echo off
rem ------------------------------------------------------------------
rem  OpenJK VR (multiplayer) launcher
rem
rem  1. Connect your headset and start SteamVR (or the Meta Quest Link /
rem     Oculus app) first.
rem  2. Run this file. The game reads the original Jedi Academy files from
rem     the GameData folder one level up; nothing there is modified.
rem
rem  Extra arguments are passed on, e.g.:
rem     "Launch OpenJK VR.bat" +connect 1.2.3.4
rem  Without the headset: "Launch OpenJK Desktop.bat" (its own settings).
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
start "" "%HERE%openjk.x86_64.exe" +set fs_cdpath "%GAMEDATA%" +set vr_enabled 1 %*
