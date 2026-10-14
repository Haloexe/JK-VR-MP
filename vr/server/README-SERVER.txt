OpenJK VR - dedicated server
============================

A game server that runs on its own - no game window, no headset - for VR
players and players without VR to join. Everything works on it as in a
game hosted from the menu: the VR saber combat (real blades cut, block and
clash, lag compensated), everyone seeing VR players as they really move,
proximity voice chat, and roleplay. Players join with OpenJK VR (in a headset or its
desktop copy, "Launch OpenJK Desktop.bat") or with the normal game, which
fights the normal way. Give your players the same OpenJK VR version as
the server, and update both together.


WINDOWS
-------
It is in the OpenJK-VR folder (inside Jedi Academy's GameData folder).

1. Double-click "Launch OpenJK VR Server.bat". A window opens with the
   server's console; once it says a map has loaded, the server is up.
   The first time, Windows asks whether openjkded.x86_64.exe may use the
   network: allow it (Private networks for your own network; Public too
   if Windows thinks yours is public).
2. Start it before the game: whichever starts first gets UDP port 29070,
   the one players connect to. (The game works on any port; if the game
   took it, the launcher says so.)
3. Join it from the VR game on the same PC: it is in the game's server
   list under Local - or in the console (Shift+~) type
     connect 127.0.0.1
4. Players on other PCs in your house: the server list under Local as
   well, or
     connect <this PC's address>       (ipconfig shows it, e.g. 192.168.1.20)
5. Players over the internet: on your router, forward UDP port 29070 to
   this PC. They then connect to your public address (search "what is my
   IP"), or find the server in the server list under Internet.

Its settings: base\server.cfg in the OpenJK-VR folder. Its own files - the
log games.log and the settings it remembers - are in
Documents\My Games\OpenJK-VR-Server\base.
Close it by typing  quit  (or closing the window).


LINUX
-----
For a rented server or a Linux PC: OpenJK-VR-Server-linux.tar.gz runs on
x86_64 Linux with glibc 2.34 or newer (Ubuntu 22.04, Debian 12, Rocky /
RHEL 9, or newer). Nothing else is needed.

1. Unpack it:  tar xzf OpenJK-VR-Server-linux.tar.gz
2. Copy assets0.pk3, assets1.pk3, assets2.pk3 and assets3.pk3 from your
   own game (Jedi Academy\GameData\base) into OpenJK-VR-Server-linux/base/
   - the game's own files can't be handed out with the server. Custom
   maps' .pk3 files go there too.
3. cd OpenJK-VR-Server-linux
   ./start-server.sh
   To keep it running after you log out, start it in screen or tmux:
     screen -S jka ./start-server.sh      (Ctrl+A then D leaves it running,
                                           screen -r jka goes back to it)
4. Let UDP port 29070 in, e.g.  sudo ufw allow 29070/udp  (and in your
   hosting company's firewall, if it has one).

Its settings: base/server.cfg. Its own files: home/base.
For a server only on your own network:  ./start-server.sh +set dedicated 1


SETTINGS
--------
base\server.cfg (Notepad on Windows) explains each one: the server's name,
password, how many players, the game type (Free For All, Duel, Team FFA,
Siege, Capture the Flag), limits, sabers only or guns too, the VR saber and
voice chat settings, bots, and the maps it plays in turn. Change it, save,
and restart the server.

An internet server is listed in the game's server browser (dedicated 2).
A server only for your own network: in "Launch OpenJK VR Server.bat" set
DEDICATED=1 (open it in Notepad), or on Linux +set dedicated 1 as above.

A password-protected server: g_password in server.cfg. Players type
  password <it>
in the console (Shift+~) before joining.


COMMANDS
--------
Type these in the server's window (on Linux, its terminal):
  status                    who is on: client numbers, names, pings
  map mp/ffa3               change map now
  map_restart               restart the map
  g_gametype 3              Duel from the next map command (0 Free For
                            All, 6 Team FFA, 7 Siege, 8 Capture the Flag),
                            e.g.  g_gametype 3  then  map mp/duel1
  svsay <text>              talk to everyone
  kick <name>               or  clientkick <client number>
  sv_banaddr <number>       ban that client's address (or an address);
                            sv_listbans lists bans, sv_bandel <n> lifts one
  addbot <name> <1-5>       add a bot (botlist lists them), kickbots
  g_vrSaberDebug 1          print every VR cut and clash
  quit                      stop the server

From your game instead (with rconpassword set in server.cfg):
  rconpassword <password>
  rcon status               any of the commands above after "rcon"


ROLEPLAY
--------
  g_rp 1                    roleplay on (0: off) - at any time, no map
                            change; nothing is lost while it's off
With it on, players log in to an account of theirs on this server (in the
Roleplay menu, or /new and /login) and play characters of their own: a
name no one else here has, a faction (Heroes, Villains, Neutral), a
description and a bio. Everyone sees the character's name, and what a
player wears while playing it stays with it. Talk reaches only the players
near (g_rpTalkRange), /me too; the team chat key and /ooc go to everyone,
out of character. g_rpCharacters: characters per account (5; up to 10).
Players on the normal game can't log in, but see the characters' names
and the chat. Players need this build: an older OpenJK VR is told to
update.
  g_rpDowned 1              down, not dead (0: dying as ever) - in Free
                            For All and Team FFA, a player playing a
                            character whose health runs out goes down,
                            until someone of their faction gets them up
                            (3 s holding use on them) or they give up
                            (attack held 1.5 s, the wheel, /kill: the kill
                            is whoever put them down's); a map's hazards
                            still kill; bots die as ever
NPCs take sides by the players' factions: the light side's are with the
Heroes, the dark side's with the Villains; Neutral are left alone until
they attack one. Nobody's NPCs, turrets or sentries harm someone down.
The log says who went down, by whom, who got whom up and who gave up
(lines starting "Roleplay:").
  rp list                   who plays which character, on which account
  rp rename <player> <name> give someone's character another name
  rp delete <account> <character>   delete a character (online or not)
  rp password <account> <temporary> for a player who forgot theirs (they
                            change it in the Roleplay menu after)
Passwords never reach the server: it keeps a key made from each, over
this server's salt. The accounts are in rp/accounts and the salt in
rp/server.txt, in the server's own files (Linux: home/base/rp; Windows:
Documents\My Games\OpenJK-VR-Server\base\rp). Back up rp/server.txt:
without it every password stops working. If it can't be read, roleplay
stays off and the console says so (mend it or put the backup back;
deleting it starts over without the old passwords).


GOOD TO KNOW
------------
* Bots: bot_minplayers in server.cfg keeps bots in the game until enough
  people join (2 by default, so there is always someone to fight).
* Private duels (Free For All): any number at once, each pair on its own.
  A challenge stands for 10 seconds; bots accept the ones put to them
  (bot_acceptDuels 0: they don't). g_privateDuel 0 turns them off (VR
  players' salutes too).
* Poses (sit, sit cross-legged, kneel, lie down) need nothing set up:
  players take them with the pose wheel, or the pose command.
* The log games.log keeps every VR swing, cut and clash (g_vrSaberLog). It
  only grows: delete it now and then, or set g_vrSaberLog 0.
* A VR player's blade is judged as they saw the others (lag compensation,
  g_vrSaberUnlag, up to a quarter of a second back), so players further
  away still meet what they swing at; a server close to its players still
  plays best.
* Running the server and playing on the same PC is fine: the server takes
  little of the PC. Hosting from home, it takes roughly 2-3 Mbit/s of your
  upload with 8 players (VR bodies and voices included).


CREDITS
-------
Built on OpenJK (the JACoders and its contributors, GPLv2), the Jedi Academy
source code Raven Software released, and id Software's id Tech 3. The VR
side started from Team Beef's JKXR (DrBeef, Baggyg, Bummser). Full credits:
README-VR.txt, or the project's page on GitHub.
