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

CAUTION: this is an early alpha whose code hasn't been reviewed by a
person. It was written with an AI assistant (Claude) and tested as it
was built, but nobody has checked its code by hand or audited it for
security, and a server takes in whatever its players' games send it. Run
it as an ordinary user - never root or Administrator - on a machine or
VPS with nothing sensitive on it, let in only its game port (UDP 29070),
keep the rcon password long and random, and back up its roleplay
accounts (ROLEPLAY, below). It comes with no warranty of any kind (see
LICENSE-OpenJK-GPLv2.txt).


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
Siege, Capture the Flag), limits, sabers only or guns too, the VR saber,
saber color and voice chat settings, bots, and the maps it plays in turn.
Change it, save, and restart the server.

An internet server is listed in the game's server browser (dedicated 2).
A server only for your own network: in "Launch OpenJK VR Server.bat" set
DEDICATED=1 (open it in Notepad), or on Linux +set dedicated 1 as above.

A password-protected server: g_password in server.cfg. Players type
  password <it>
in the console (Shift+~) before joining.

Flood protection: the server takes each player's commands - chat, what
the menus ask for - five at once, then one more a second, and drops any
that come sooner (the player is told). sv_floodProtectBurst is how many at
once (1: one a second, the old game's way, where a menu that sends a few
together loses all but the first); sv_floodProtect is the time (1: a
second; more: that many milliseconds; 0: no limit, so anyone could flood
the chat). With sv_floodProtectSlow 1, someone who keeps sending too fast
gets nothing through until they stop for a moment. Players on this build
ask the server for things by themselves (roleplay's cards, a remembered
login) only once what they sent before is made up, which leaves room for
what they type when the server takes five at once (with 1, a card asked
for as they look at someone can still take the place of a line said just
after it).


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
update. Logging in works with roleplay off too (it changes nothing else
then: admins keep their cheats, below) - the Roleplay menu opens then
too, from the in-game menu or the wheel.
  g_rpDowned 1              down, not dead (0: dying as ever) - in Free
                            For All and Team FFA, a player playing a
                            character whose health runs out goes down,
                            until someone of their faction gets them up
                            (3 s holding use on them) or they give up
                            (attack held 1.5 s, the wheel, /kill: the kill
                            is whoever put them down's; going to spectate
                            or another team is giving up); a map's hazards
                            still kill (a door's bump, a breath of water
                            borne); bots die as ever
NPCs take sides by the players' factions: the light side's are with the
Heroes, the dark side's with the Villains; Neutral are left alone until
they attack one. Nobody's NPCs, turrets or sentries harm someone down.
NPC packs: the server reads every NPC file it has (ext_data/NPCs, in any
pk3: 4 MB of them at most, 256 KB a file). The stock game read only as
many as 2 KB of their names held, and a pk3 named after its own
(assets1.pk3 - most packs start with z) comes before it - so with packs
installed, its own past that point were never read (no stormtroopers).
Saber and vehicle files likewise. A pack's file of the same name as one
of the game's (stormtrooper.npc) takes its place, and so does a pack's
NPC of the same name in any file. An NPC file broken so the game would
lose its way in every file after it (an {, a quote or a /* never closed,
a } too many, a name with nothing after it) is read up to there; a UTF-8
file's mark (Notepad's) is no part of its first NPC's name now. The
console says how many at each map - "NPC files: 148 of 148 read, 112 KB"
- and any left out or cut short, which; an NPC spawn that doesn't come
says why to whoever typed it. A pack's trooper of the Reborn's or the
Jedi's class with a gun its file gives it (a blaster, a repeater...)
fights as a gunner does, as in the original game - this engine had it
fight as a Jedi: firing less, and putting out a saber it hadn't ten
times a second, heard each time (loud with a few of them). One of the
Force alone keeps the Jedi's ways: fists, a baton or nothing, a gun its
file didn't give it (the game's, for want of one), or one it's not to
fire (the game's cultists of grip, lightning and drain) - decided as it
comes, whatever it holds later; with no saber in hand, no saber's heard
put out.
  g_rpCompanions 2          companions each player has out with them
                            (0 to 2; 0: none)
  g_rpCompanionsMax 16      companions out on the whole server at once
  g_rpCompanionModels 1     1: they may look like any player model the
                            server has; 0: only as their NPC types come
  g_rpGrey 0                Grey Jedi: 0 none; 1 any character may be
                            one; 2 the characters an admin picks (and
                            Force Masters)
  g_rpForceMaster 0         Force Masters: 0 none; 1 the characters an
                            admin picks
A Grey Jedi has powers from both sides, bought with the same points as
anyone's (the server checks them as ever); its player picks Grey in the
Force menu. A Force Master has every power at level 3, nothing to spend.
Each counts only while roleplay's on and someone plays the character;
where Grey isn't allowed, a Grey setup counts as the side most of its
points are on (those on powers the server has: not g_forcePowerDisable's,
nor team powers outside team games). On Force-aligned teams everyone
plays their team's side (the Force menu opened there makes a setup that
side's, a Grey one too). A Grey Jedi may take either Enlightenment
pickup. An older OpenJK VR isn't offered Grey. Admins pick characters on
the Roleplay menu's People page, or rp force; each is logged ("Roleplay:
... made a Grey Jedi by"), and stays so till an admin or you change it -
an admin made no longer one too (rp list shows which are which).
Companions: each character keeps up to 6 NPCs of its own, made in the
Profile menu's Companions page (a name, the NPC type it fights as - one
of the people, no droids, beasts or vehicles - and a look), and up to
g_rpCompanions of them come out with the player while roleplay's on:
following them, fighting for their faction, getting them up, taking
orders from the wheel. Not in Duel, Power Duel or Siege. One whose
health runs out is knocked out for 2 minutes. They come out only where
the level has room for them (entities, models, sounds, effects, and the
game state every player loads - past it, everyone would be dropped), and
a player's companions bring at most 8 looks new to a level (after that
they come as their types do until the next map); the player is told.
A look must be a model the server has, on the humanoid skeleton, with
its skin (or a custom character's three parts) - so a model or skin
added to the server's files is one players can choose. Players running
this build are told a companion's look (they see it if they have the
model, else its NPC type); every other game - the normal game, mods, an
older OpenJK VR - is sent the companion as its NPC type (in the colours
its owner gave it), so a custom model a player hasn't got can't take
their game down. A player of another game who has the models can say so,
and is sent companions in their own looks from then on: they type
  /setu rpmodels 1
in their console (it lasts till they quit the game - "setu rpmodels 1"
in their autoexec.cfg keeps it), and /setu rpmodels 0 to stop. The
server can't check: a look whose model they haven't got could take their
game down, so it's theirs to say. A companion already out takes its look
for them when it next comes out; one whose look's skin is too long for a
model's name comes in its model's default skin; a level holds 64 types
and looks of companions' (past that, the next come to them as their
types too, till the map changes or restarts). They're told of it in the
console a while after they come in on each map, and the log says who's
said it ("Roleplay: ... says they have the models companions wear"); a
server demo of one who's said it needs those models to play. Players
running this build needn't (they see looks anyway). This build's game
and server program (openjkded) go together - update both. An NPC type of
your own whose model players haven't got still could take theirs down
(this build draws a stand-in): give companions types whose models
everyone has.
The log says who went down, by whom, who got whom up and who gave up,
and which companions came out and were knocked out (lines starting
"Roleplay:"). The serverinfo's "vrRP" says whether roleplay's on, and
these settings: "<on> <g_rpCharacters> <g_rpTalkRange> <g_rpDowned>
<g_rpCompanions> <g_rpCompanionsMax> <g_rpCompanionModels> <g_rpGrey>
<g_rpForceMaster>" ("0": roleplay isn't ready here - see the console).
  rp list                   who plays which character, on which account
  rp rename <player> <name> give someone's character another name
  rp delete <account> <character>   delete a character (online or not)
  rp password <account> <temporary> for a player who forgot theirs (they
                            change it in the Roleplay menu after)
  rp admin <account> on|off make an account an admin, or not (without on
                            or off: whether it is one)
  rp admins                 the admin accounts, and which are here
  rp force <account> <character> grey|master|normal   make a character a
                            Grey Jedi or a Force Master, or neither
                            (online or not)
  rp invasion start <who> [waves <0-50>] [per <1-32>] [cap <1-32>]
              [cleared <seconds> | every <seconds>]
              [side dark | light | creatures]   waves of NPCs at the
                            players, roleplay on or off (<who>: reborn,
                            stormtroopers, rebels, jedi, or an NPC type -
                            or up to 4, a comma between, each mixed in or
                            so many a wave after a colon: reborn:2,jedi;
                            waves 0: till it's stopped; per: a wave's - the
                            mixed ones' together, evenly; cap:
                            up at once; the next wave that long after one's
                            beaten, or every that long - 600 at most, a
                            timer 5 at least; side: theirs, whatever their
                            NPC file says - villains and heroes too;
                            defaults: 4 waves of 6, cap 16, cleared 20,
                            their file's side)
  rp invasion stop [all | dark | light | creatures | <who>]   called off,
                            what's left of it gone (villains, heroes: a
                            side's too)
  rp invasion               how each invasion on is going
  rp war start <side> <who> <side> <who> [<side> <who>] [waves ...] [per
              ...] [cap ...] [cleared ... | every ...] [at <client>]
                            a war: two or three factions' NPCs fighting
                            each other where you stand - by rcon, where
                            client <client> stands (<side>: light, dark,
                            creatures - heroes, villains; <who>: as rp
                            invasion start's, all of it on that side;
                            the settings each side's, as rp invasion
                            start's)
  rp war stop               called off, what's left of it gone
  rp war                    how it's going
Invasions: one a side at once (the dark side's, the light side's,
creatures'), and 32 of their NPCs up at most, every invasion's together -
each one an NPC the server runs, so fewer on a busy server (and none
while the level has fewer than 128 entities free). With roleplay on they
go after the players their side fights (the dark side's Heroes, the light
side's Villains, creatures everyone; nobody someone down or in a duel);
with it off - and for players playing no character, and bots - the dark
side's go after everyone and the light side's fight beside the players.
Their side is their NPC file's, or the one picked (side; the Admin page's
Faction): any NPC type as either army, a neutral one too (picked none, a
droid or civilian is turned away: it'd fight nobody).
They appear out of everyone's sight where they can walk to whom they're
for, 17.5 to 30 m off - else 12.5 to 16.3 m off, else with nothing between,
and only where there's nowhere out of sight, in sight; 8 to 11 m off last
of all (a saber or a creature in sight, or near, where it can walk to
them, before out of sight where it can't). Each of an invasion's for a
player is looked for from further round them than its last for them, a
ring further out or in: they come from all round, near and far, not one
after another behind them (its first, behind them; in sight, the least in
their sight first); a
big type (its NPC file's width and height) only where it fits - on a
cramped level it may find nowhere ("nowhere for more yet": stop it). An
invasion with nobody in the game its side goes after, none of it
fighting and the other side's invasion not on, is over a minute on; with
nobody in the game at all, it waits. Stuck or lost ones come out again elsewhere; bodies go
after 10 s (16 at most lie about), and any still up 2 minutes after the
last wave's all out. A timer's 5 s between waves at least. Not in Duel,
Power Duel or Siege; held in the intermission. Admins have them on their Admin page
(Invasions...) and as a command (rp invasion ...). Everyone's told of each
wave and the end, and the log has every start, wave and end (lines
starting "Invasion:"; an admin's command also "Admin:"; a non-admin's
try "Invasion: ... refused"). Their targetname
is vri_dark, vri_light or vri_creatures (npc kill vri_dark). Give them NPC
types whose models everyone has: one a mod adds can crash the games of
players who don't have it.
An invasion of several NPC types (a lineup) is of one side - the one
picked, or their files' if they share one (else it's turned away: pick
one, or set them on each other in a war); each wave has each one's own
number of it (reborn:2), or a share of per with the others mixed, evenly
(what won't split, one more each, in turn from a wave to the next), and
they come out mixed in, not one type after another.
Wars (rp war: one at once; the Admin page's Mode: War): each side an
invasion of that side (its lineup, all of it on that side whatever their
NPC files say) with the same settings - its waves its reinforcements.
Where the admin stands (by rcon: at <client>), each side comes in from
its own way round them - the first ahead of them, one of a faction
they're not of; theirs last, behind them; the rest evenly round - 12.5 to
20 m off (9 to 12, else 6 to 8, where there's no room so far off), out of
everyone's sight where it can, where it can walk straight there. Each
side's NPCs fight every other side's (the game's AI too: it fights one
side as ever). Anyone else - the players their side fights (by faction,
as an invasion's), companions, other NPCs - they take on only within
about 5.5 m (once after them, until they're 12.5 m off), or for 15 s
after that one hurt one of them (that NPC alone), wherever they are; the
winners, staying their 15 s, go by it too. A side whose last wave's
beaten is out; the last side
standing wins - everyone's told, its NPCs gone 15 s on (all beaten at
once: nobody wins). rp war stop calls it off, as does stopping one of its
sides' invasions (rp invasion stop: everything); an invasion of a side not
in it can go on beside it. Not in Duel, Power Duel or Siege. The log has
its start (where, each side's lineup), each side's waves, each side
beaten, and its end - won, nobody standing, called off (lines starting
"War:").
Admins: a player logged in to an admin account - roleplay on or off - can
use every cheat command with sv_cheats 0 (npc spawn - with a side after
its targetname, or in its place: light, dark, neutral or creature
(heroes, villains), the NPC takes it as it begins, whatever its file
says - a side's word alone after the type is a side, not a name; not a
vehicle; one that doesn't come, they're told why - and npc kill, god,
noclip, notarget, give and the
rest - give forcepowers: every Force power
at level 3, till their powers are set up again), for themselves, though
not while down; and they make characters Grey Jedi or Force Masters (the
People page, or rp force <player> grey|master|normal). The log has each
one (lines starting "Admin:", with the account). Nobody else is shown
who's an admin. Logging out, or rp admin off, ends their god mode,
no-clip, no target and every Force power given. In the Roleplay menu an
admin has an Admin page (spawn any NPC or vehicle in front of them - an
NPC on the side its Faction picks, or as its file says - kill
the ones they spawned or every NPC, switch the cheats, All Force Powers);
the Server page's Admins makes or unmakes them (by rcon on a server you
don't host).
Only the server's own console makes admins (its window, where it has
one), or rcon with the rconpassword. A player typing rp admin is turned
away, and the log says who tried ("typed rp admin - refused"); a vote
can't carry it. So keep the rcon password long and random (20 letters
and digits, say) and to yourself: anyone who has it can make admins, and
do anything else rcon can. Wrong guesses show in the server's console as
"Bad rcon from <address>" - its window, or a service's journal - and
with  seta logfile 1  in server.cfg in qconsole.log too, by games.log.
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
* The log games.log keeps every VR swing, cut and clash, and where VR
  players' Force came from (g_vrSaberLog). It only grows: delete it now
  and then, or set g_vrSaberLog 0.
* Saber colors: players on this build give their sabers any color and a
  black core (Setup > VR > Saber Colors), and everyone on this build sees
  them; players of the normal game see the nearest stock color. Team games
  still go red and blue. g_vrSaberRGB 0: the stock colors for everyone.
* Players of the normal game (or any other build) see a VR player's saber
  where they really hold it, a fist round it, and their arms reaching it -
  the other hand on the hilt when it holds it - the body above the legs
  held in one of the game's own poses, facing the way the player does
  (standing still, the legs stand straight). Knocked down, rolling,
  flipping, in a saber lock or with the saber away, they see the game's
  own animations. Players on this build see VR players as they really
  move either way. g_vrArmsForAll 0 (or the Server page's "Non-VR players
  see"): the game's saber stance for them, as before.
* A VR player's blade is judged as they saw the others (lag compensation,
  g_vrSaberUnlag, up to a quarter of a second back), so players further
  away still meet what they swing at; a server close to its players still
  plays best.
* VR players deflect blaster bolts with their real blade only - where it
  meets them, by its angle and their Saber Defense - and get no automatic
  block against them (g_vrSaberBolts; the disruptor's beam is still
  blocked automatically). With lag compensation (g_vrSaberUnlag 1), a
  bolt that hits something near a VR player - them, a wall, someone
  beside them - or bounces off it waits there for their moves of that
  moment (a quarter of a second at most), in case their blade met it
  first: players near them can see a bolt hang in the air for a moment.
  g_vrSaberBolts 0: bolts are blocked for VR players automatically, as
  for anyone, as before.
* VR players' shots leave their gun's barrel - its muzzle, where their
  hand holds it - the way they point it: they can shoot round a corner
  with only their arm out. Only with room for the shot all the way out
  (nothing within about 12 cm of the line from their character's eyes to
  their hand, nor of the gun; their hand within 1.4 m of those eyes): a
  gun in or through a wall, or in someone, fires from the eye as ever -
  never from beyond a wall. Bolts, the disruptor's beam and every other
  gun's shots; thermal detonators, trip mines and det packs thrown from
  the hand. Only players on this build ("vrGunBarrel 1" in the server
  info, and their game says it goes by the same rule, their VR body
  drawn: their aim dot is traced from the barrel then); players of an
  earlier build, desktop players, bots and NPCs fire from the eye as
  ever, and so does anyone zoomed in, in a vehicle or on an emplaced gun.
  The muzzles come from the game's own weapon models (the server loads
  each the first time it's fired so). The server takes a player's hand
  where their game says it is, as it does for VR sabers - a modified
  game could put it anywhere with room to it within that 1.4 m.
  g_vrGunBarrel 0: everyone's shots from the eye, as before.
* VR players hang on to walls: in mid-air, an empty hand squeezed on a
  wall holds them there until they let go (they drop) or jump (they push
  off - as the game's own wall jump with Force Jump 3, else as high as a
  plain jump). Anyone can, Force or not. The server takes the grip only
  from a player whose avatar has an empty hand at a wall as they take hold
  ("vrWallGrip 1" in the server info; desktop players and bots never), and
  only for a wall within about half a metre of the side of their box.
  Getting hurt or Force pushed or pulled knocks them off, as the game's
  debug melee wall hold does. g_vrWallGrip 0: no hanging on.
* VR players drop their sabers: holding the grip (Saber Grip: Hold), a VR
  player who lets go of their saber anywhere but at their side drops it -
  switched off, falling from their hand as it was going (no faster than
  900 units a second, about 26 m/s; it bounces, and comes to rest lying
  flat), where everyone sees it. They pick it up again with a hand at
  its hilt, or call it to an empty hand pointed at it from up to 4096
  units (about 120 m) away: it flies to that hand, unlit, up to 1000
  units a second, and a wall in the way stops it there. Left lying 20
  seconds, it's back on their belt. A VR player's saber knocked away the
  game's own way (a saber lock lost) lies there the same, instead of
  their trigger calling it back. Only with holsters on (g_vrHolsters 1)
  and only from a player whose avatar held it a moment before ("vrSaberDrop
  1" in the server info; desktop players and bots never) - never a siege
  class's, nor in a saber lock or a duel's countdown, nor one of two
  sabers held both at once. games.log says who dropped, picked up or
  called their saber, and how fast it went (g_vrSaberLog). g_vrSaberDrop
  0: let go of, a saber goes back to its holster, as before; knocked
  away, the game's own way.
* VR players throw their sabers: let go of lit in the middle of a swing -
  their hand going 2.5 m/s or faster in their room (85 units a second;
  walking, jumping or turning doesn't count) at its fastest in the 0.2 s
  before the let go - by a player with Saber Throw and the Force for it
  (the game's 20), a saber is thrown instead of dropped: the game's own
  thrown saber (lit, spinning, cutting whoever it passes for the game's
  30, blocked as the game's), off at three times the hand's speed (450 to
  900 units a second) where they look if they swung within 45 degrees of
  it (some of each to 90; else as swung), never more than 15 degrees below
  level unless they look further down. Out as far as their Saber Throw
  lets it - 384 units at level 1, 576 at 2, 768 at 3 (11, 17, 23 m), or a
  second's flight if that's shorter - then round like a boomerang and back
  into the hand it left, lit (as the game's return: 700 units a second,
  900 at level 3; through anything in its way). Meeting a wall or someone
  on the way out, it comes back from there; the hand it left squeezed
  calls it back early. No throws in a duel, from a saber the game says
  can't be thrown, nor in the game's 0.3 s after a catch: those drop. Any
  thrown saber a VR player's blade meets in its flight - swung into it or
  held in its way, as the player saw it (lag compensation as for their
  cuts) - is knocked out of the air; one thrown by a VR player is knocked
  down by whatever blade meets it (the game's own throws, between players
  without VR, keep the game's odds). Knocked down, a VR player's lies
  there to pick up or call. games.log says who threw, how hard and how far
  it could go, who caught or called it back, and whose blade knocked it
  down (g_vrSaberLog). g_vrSaberThrow 0: let go of mid-swing, a saber
  drops like any other.
* VR players' Force powers come from their Force hand (their empty hand -
  or one holding its own saber, a second one say - with Force gestures
  on): Push and Pull the way they thrust or yanked it, Grip, Lightning and
  Drain where it points - each straight at someone within 10 degrees of
  that (Grip: 15), anywhere on them, whom it works on (never onto, or off,
  someone cloaked or mind-tricking them) - and everyone on this build sees
  them leave that hand ("vrForce 2" in the server info; 1, an earlier
  build's). Each power keeps the
  game's own levels and costs, and its reach - but Grip from the hand, a
  quarter further (320 units, about 9 m; the game's 256 for everyone
  else), catching nobody the game's hold would let go of at once (out of
  sight of where they stand). g_vrForce 0: the game's own aim and reach,
  where they look.
  Whom a VR player's Grip holds at level 2 or 3 moves with their hand
  (g_vrGripMove 1, and the player's own Grip Moves): level 3 anywhere
  in its reach - swung round, lifted, drawn in to about 2 m or sent out
  to about 8 m - level 2 only lifted and lowered; walls stop them, the
  server steers them no faster than the game's own level 3 floats them
  (700 units a second), never further from the gripper than the Grip
  reaches, and let go of with the hand moving they fly on as it moved
  them (a fling throws them, one out to a limit too - off the ground, as
  a Push lifts; falls hurt as ever, a deadly one is the gripper's), with
  it still they drop; the gripper flung off them, or carrying them along,
  throws no one. A Push on them while held has them till it's over, and
  one landing as they're let go of has them too. The gripper's hand lost
  a moment (tracking), whom it holds stays held where it put them, as far
  as its Grip reaches. Players gripping each other round in a ring, how
  long a Grip holds, its cost and what it does to them are the game's.
  g_vrGripMove 0: lifted at level 2 and floated in front of them at level
  3, as before.
  games.log says where each came from (g_vrSaberLog: Lightning's and
  Drain's aim at most four times a second), and whom a Grip moved with
  the hand, how far and how fast they were going when let go.
* Push and Pull at level 1 do more than in the normal game, for everyone
  (VR or not, NPCs too): with nothing on their line (a VR player's thrust
  or yank, or where anyone else looks) - or only the world - they go at
  whoever's nearest it within 30 degrees, as levels 2 and 3 take in
  everyone near it - in their reach (512 units), in sight, and one they
  work on; whoever or whatever is on the line itself is whom they're at,
  as ever (a teammate, someone knocked down, a door). They shove at 384
  (the game's 256 a level: half again level 1's, three quarters of level
  2's), and someone standing with level 1 of the same power only halves
  it instead of stopping it (moving, they don't resist, as ever; a higher
  level stops it, moving or not; Absorb as ever, and a Push it takes down
  to level 1 shoves as the game's level 1). games.log says whom each
  player's Push or Pull shoved, how hard and whether they resisted it
  (g_vrSaberLog).
* Running the server and playing on the same PC is fine: the server takes
  little of the PC. Hosting from home, it takes roughly 2-3 Mbit/s of your
  upload with 8 players (VR bodies and voices included).


CREDITS
-------
Built on OpenJK (the JACoders and its contributors, GPLv2), the Jedi Academy
source code Raven Software released, and id Software's id Tech 3. The VR
side started from Team Beef's JKXR (DrBeef, Baggyg, Bummser). Full credits:
README-VR.txt, or the project's page on GitHub.
