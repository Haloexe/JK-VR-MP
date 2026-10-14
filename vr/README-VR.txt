OpenJK VR - Jedi Academy multiplayer in a VR headset
=====================================================

An OpenJK-based multiplayer client that renders Jedi Academy in stereo 3D
on a PC VR headset (OpenXR). Current state (milestone 2):

  * Stereo 3D with the headset's own field of view and lens geometry
  * Head tracking (rotation + limited positional movement)
  * VR controllers: thumbstick movement, snap/smooth turning, buttons
  * Guns are held in and aimed with your right hand, with a 3D aim dot
  * Your own body in first person: it stands under you, turns with you,
    and its arms and hands follow your controllers, gripping what they
    hold (works with any player model on the standard skeleton, custom
    models included)
  * First-person lightsaber: the hilt in your hand, the blade where your
    controller points
  * Physical saber combat in games you host (and on servers running this
    build): your real blade cuts, blocks and clashes, harder the faster you
    swing; blades are solid - yours rests on another blade and slides along
    it rather than passing through; saber styles are the blade's weight
  * Saber, fists and scopes aim where you look
  * HUD on a floating plane in front of you
  * Menus, console and loading screens on a floating screen; point at the
    menus with a controller and pull the trigger to click
  * The desktop window mirrors the left eye
  * Multiplayer: other players see you as you really move, and you see
    other VR players as they do - arms, hands, head and saber (in games
    hosted with this build, for everyone running it, in a headset or not;
    see PLAYING WITH OTHERS)
  * Proximity voice chat: talk to the players near you and hear them from
    where they are; pick your microphone in Setup > Sound (see VOICE CHAT)
  * Roleplay on servers that switch it on: an account, characters of your
    own (a name, a faction, a description and a bio), talk that reaches
    only those near you with speech bubbles over their heads, /me and
    out-of-character chat, and character cards (see ROLEPLAY)
  * A keyboard in the headset for typing in menus, chat and the console
  * A dedicated server to run your own servers with all of the above, on
    Windows ("Launch OpenJK VR Server.bat") or Linux (see README-SERVER.txt)

Not yet: shots leaving the gun barrel (they leave from your eye, toward
where you aim), two-handed guns (two-handed sabers work).


INSTALL / RUN
-------------
The OpenJK-VR folder goes inside Jedi Academy's GameData folder:

  ...\Jedi Academy\GameData\OpenJK-VR\

It is self-contained: the original game files (jamp.exe, base\*.pk3, your
saves and configs) are not touched. To uninstall, delete the folder.

1. Connect your headset and start its PC software:
     - Valve Index / HTC Vive / WMR:  SteamVR
     - Meta Quest (Link, Air Link):   Meta Quest Link app, or SteamVR
     - Virtual Desktop:               works through its OpenXR runtime
   The headset software must be the "active OpenXR runtime"
   (SteamVR: Settings > OpenXR > Set SteamVR as OpenXR Runtime).
   Windows Mixed Reality's own runtime has no OpenGL support: use SteamVR.
2. Double-click "Launch OpenJK VR.bat".
3. To use the keyboard as well, click the game window once so it has
   focus (the controllers work without it).

If no headset is found the game starts in normal desktop mode.


CONTROLS (Quest / Touch layout; other controllers are similar)
--------
  Left stick           move (in the direction you look)
  Left stick click     walk (hold)
  Right stick L/R      turn (snap 45 degrees; vr_turnMode 1 = smooth)
  Right stick up/down  next / previous weapon
  Right stick click    change saber style
  Right trigger        attack (saber in your hand, where blades are real:
                       switch it on and off)
  Left trigger         alternate attack
  Right grip           use (doors, switches, pick-ups)
  Left grip            use the selected Force power (or, at your saber's
                       hilt, take hold of it with both hands)
  A / B                jump / crouch
  X / Y                previous / next Force power
  Left menu button     game menu (press again to close it)

IN THE MENUS point a controller at the menu screen: the mouse cursor goes
where it points.
  Trigger              click (either hand; the hand that pulls its
                       trigger becomes the pointer)
  Trigger held         drag sliders and scroll bars
  Either stick up/down scroll the list under the cursor, or step through
                       the setting under it (hold to keep going)
  B                    back (like Escape)
  Left menu button     close the menu / back
Each click gives a small buzz in your hand. The keyboard and mouse keep
working alongside. To type - click a text field (your name, a server
address), open the chat line, or the console - a keyboard comes up on the
menu screen: point at a key and pull the trigger. Shift once makes the next
key a capital (or the symbol over a number); Backspace held repeats; Enter
is Enter; Close (or B) is done.
In Controls, the triggers, B and pushing a stick up or down are busy
clicking, going back and scrolling, so to put a command on one of those use
the console (see the AUX key names below); the other buttons (grips, A, X,
Y, stick clicks) can be bound right there.

With a gun out you aim with your right hand: the gun is in your hand (the
complete third-person model, so it looks right from every angle) and a
small crosshair floats where your shots will land. With the saber, fists or
a scope you aim where you look. Keyboard and mouse still work; the mouse
turns your body.

YOUR BODY: look down and you see your character. Its hands are on your
controllers, its elbows bend on their own, its wrists bend, and its torso
turns with your head (it follows once you look more than 45 degrees to the
side, and straightens out while you walk). The head is hidden so it never
gets in your eyes. Your character's model is measured every frame, so a
small or a tall model, the stock characters or a custom one all get arms
that fit. A gun sits in the right hand exactly the way the game holds it
in third person, fingers around the grip. When you reach further than your
character's arm, the body leans in a little so the hand still gets there.
Standing still, your character stands up straight, as you do (the game's
saber stances crouch, which put your hands up at its chest), so its head
is where yours is and your hands sit on its body where they are on yours;
running, crouching, getting hurt and the Force still play the game's own
animations. Everyone running this build sees you that way, and in games
hosted with it blades hit that body.

Your arms move like real ones. Each elbow goes where holding your hand
the way you hold it is easiest: it hangs when it can, a raised arm goes
up out to the side, it keeps out of your body, and it moves only as far
as your wrist and forearm can't bend or turn. It follows your real hand
on the controller, so a heavy blade lagging behind your swing turns the
hand and wrist, not the elbow. The elbow only bends; a hand turned palm
up or down twists the forearm along its length, so the elbow never wrings
like a towel. Your shoulders lift as you raise a hand overhead and come
forward as you reach out or across. Other players running this build see
your arms the same way.

LIGHTSABER: with the saber out you stay in first person. The hilt is
locked in your right fist (a second saber in your left), the fingers closed
round it whatever your body is doing, and the blade points along your fist
the way you would hold a real hilt, so it follows every turn of your wrist.
Through flips, force jumps, cartwheels and rolls your body stays upright
(in an in-air pose, or a crouch for a roll) with the saber in your hands,
so you can keep fighting mid-air. Fists, saber locks, knockdowns and
vehicles still use the third-person camera.

BOTH HANDS ON THE HILT: bring your left hand to the saber's hilt (below
your right hand, above it on a long hilt, or anywhere along a staff) and
squeeze the left grip. Your left hand closes round the hilt with a short
buzz, and while you hold the grip the saber is in both hands:
  * The blade follows both wrists, and the further apart your hands are,
    the more it follows the line between them, so pushing with one hand
    and pulling with the other levers it round like a real two-handed hilt.
  * Your left hand grips the hilt where it took hold: it doesn't slide
    along it or turn round it. To hold it somewhere else, let go and take
    hold again.
  * Let go of the grip to let go. Moving your hands about doesn't make it
    slip off: only pulling your left hand a long way (about 70 cm) from
    where it holds breaks the hold. Taking hold and letting go settle
    smoothly over a few frames.
  * Both hands hold the hilt in the hole of the fist, the way a real hand
    holds a handle, and wrap their fingers round it whatever the body is
    doing (running, jumping, flipping). (Models whose gloves are one solid
    piece, with no finger bones, can't close their fingers.)
  * Near the hilt the left grip takes hold instead of using the Force;
    anywhere else it uses the Force as usual. cg_vrTwoHand 0 turns taking
    hold off.
  * On servers with VR saber combat a two-handed saber swings one weight
    heavier (light becomes medium, medium becomes heavy, and a heavy saber
    in both hands wins clashes against a one-handed heavy), and either hand
    drives the swing (sliding your left hand along the hilt doesn't count
    as a swing). Hits and clashes buzz both hands.

SABER COMBAT in games you host (Create Game, bots included) and on any
server running this build: your real blade is the weapon.
  * Swing it. A hit's damage comes from how fast the blade moves where it
    lands: a big arm swing or a stab cuts deep, a slow push or a blade just
    resting on someone only singes, and flicking your wrist with your arm
    still only grazes (running, turning, crouching or taking stairs with the
    blade out doesn't cut either). You cut the same opponent at most every
    0.3 s, whichever blade it is: two sabers, or a staff's two ends, don't
    double up. Walls stop your blade, even where your real hand goes
    through them. The trigger switches the saber on and off; it no longer
    starts the game's canned swings - your arm swings.
  * Block with it. Only what your blade physically stops is blocked, thrown
    sabers included (no automatic blocks, saber locks or disarms while you
    hold it). Blades meet only where their cores really touch, at the same
    moment - not their glow, and not the box round the saber - whether you
    swing into a bot's blade or its swing comes at yours, and one blade
    never passes through another (except a heavy blow breaking a guard).
    When they meet they spark, your hand buzzes hard, and the swinging
    blade can't cut for a moment while it recovers. The stronger blow wins:
    weight, plus one if the swing was at full speed. Your swing into a
    bot's guard is parried; into its swing, both bounce and the weaker
    blade reels twice as long (a weaker bot swing is knocked away). Only a
    heavy blade swung at full speed breaks a guard - and only one it
    out-weighs by two (a heavy guard holds against a one-handed blow, not
    a two-handed one) - and carries on through, cutting for half damage.
    Anything slower is parried, however heavy. A bot's
    swing that meets your blade held still bounces off it (a much stronger
    guard knocks it away; a much stronger swing jars your guard for a
    moment); one that misses your blade lands.
  * Blades are solid. Yours can't go through another blade as you see it:
    swung or pushed into one, it stops there and rests on it - the cores
    touching, sparking where they meet - and slides along it as you move
    your hand along it; take your hand back, or round its end, and it comes
    away. Your real hand can go on past (nothing pushes it back), so your
    character's hand turns to keep hold of the blade where it rests, and
    you feel it pressed on harder the further your hand goes. Push on far
    enough - 60 degrees through (up to 75 if a heavy blade was trailing a
    fast swing as it met the other one) - and it slips by. A heavy blow
    that breaks a guard (above) goes on through. Only your own blade is
    stopped: a blade swung into yours doesn't shove it (in its owner's
    game it stops on yours). Teammates' blades and other people's duels
    aren't in the way, and walls and floors never are - the blade sinks
    into them, sparking, as it always has. cg_vrSaberSolid 0 lets blades
    pass through each other again (the game still stops them where they
    met).
  * Bots fight back with their guard: they see your swing coming and raise
    their saber into its path - how often and how quickly goes with their
    saber defence. It still has to meet your blade to stop it: change the
    angle or feint and it gets through. Players without VR guard the same
    way (their automatic block). g_vrSaberBotGuard 0 turns it off, 0.5
    makes it half as likely, 2 makes them guard every swing, faster.
  * Saber styles are the blade's weight (right stick click cycles them):
      light  (fast style)    small quick swings cut, up to 25 damage,
                             recovers quickly after a clash
      medium (medium style)  up to 40 damage
      heavy  (strong style)  needs big swings, up to 50 damage, breaks
                             through guards at full speed, recovers slowly
    Slower swings cut for less (a quarter at the slowest). A player starts
    with 100 health (125, and 25 armour, outside duels), so it takes two or
    three full-speed heavy blows to bring someone down, or more lighter cuts.
    Dual sabers are two medium blades, and the staff is medium too. With
    dual sabers the right stick click switches the left one off instead;
    the right one is then a light saber, as in the normal game. With the
    staff it switches the second blade off (click again for both). Held
    with both hands (above), a saber or staff swings one weight heavier.
  * The weight is something you feel, not just numbers:
      - a heavier blade can't be whipped about like a light one: it has a
        top speed (heavy, the strong style, turns at most 550 degrees a
        second; medium 750; light as fast as your hand) and takes a
        moment to get going and to turn back. Swing faster than that
        and the blade - and your character's hand, which keeps hold of it
        - falls behind your real hand, catching up as you slow. Slow,
        careful moves stay exact, and it never carries on past where your
        hand stops; only swung back hard, its weight carries it on a
        little first. Two hands on it turn it a fifth faster.
        cg_vrSaberInertia sets how much (1; 0 = the blade exactly where
        the controller points it; 2 or 3 = heavier still)
      - swinging it, your hands feel the blade's weight: the faster it
        goes, the harder it pulls - a heavy blade a deep, rough rumble, a
        medium one a little less, a light one a light buzz; swung faster
        than it can go, it strains as it falls behind your hand; and a fast swing
        stopped short, or turned back, lands in your hand with a thud.
        cg_vrSaberWeight sets how heavy it feels (0 weightless, 1 normal,
        up to 2)
      - swings whoosh, deeper and heavier the heavier the style (other
        players hear your swings too)
      - another blade stops yours dead for a moment; cutting through
        someone drags it
      - a medium blow nudges whoever it hits, a heavy one shoves them back,
        and a heavy one near full speed knocks them off their feet
      - your hands feel it, the moment it happens as you see it:
          your blade going into someone: a solid blow, as hard as your
          swing (a light touch when it's slow), then the drag of cutting
          through them for as long as the blade is in them;
          the game saying it cut: a deeper rumble right after, longer the
          heavier the blade; a kill: a long, heavy one;
          blades meeting: a knock, harder the faster your swing (longer
          the heavier the blade), then a grind while they rest together,
          harder the harder you push, and a sharp give if it slips by; a
          grind when it scrapes a wall;
          getting cut: a jolt in both hands.
        Holding the saber with both hands, both feel it. cg_vrHaptics
        sets how strong it all is (0 off, 1 normal, up to 2).
  * Cutting someone ends your spawn protection, like attacking always has.
On normal servers the saber works the old way (the trigger swings).
Servers fighting this way show "vrSaber 1" in their server info.
Server settings: g_vrSaber (1; 0 = the old animated combat),
g_vrSaberLight / g_vrSaberMedium / g_vrSaberHeavy (each "cut-from
full-damage-at max-damage recover": swing speeds in m/s, recovery after a
clash in ms; defaults "1.8 5.3 25 150", "2.5 7.1 40 220", "3.2 8.8 50
300"), g_vrSaberSpeedScale (1; higher = faster swings needed for every
weight), g_vrSaberDamageScale (1; e.g. 1.5 hits half as hard again, 0.5
half as hard), g_vrSaberUnlag (1: blades judged as the VR player saw them,
below), g_vrSaberDebug 1 (prints every cut's speed and damage, and how
every clash ended, to the console), and the stock g_dismember 100 if you
want limbs to come off.

PLAYING WITH OTHERS: everyone who runs this build - in a headset or not -
sees a VR player as they really move, in a game hosted with this build:
  * their body faces where theirs does, their head turns, nods and tilts
    where they look, and their arms and hands are where their real ones
    are, fingers round whatever they hold: the saber along their real
    blade (which is what their blows are judged by), both hands on the
    hilt when they hold it with both, a gun where they hold it
  * they stay upright through flips and rolls, as they do in their headset
  * cg_vrOthers 0 shows them with the game's animations instead.
Players without VR - this build's desktop copy (below), or the normal game
- fight the normal way, and block automatically as in the normal game:
against your swing that is their guard raised into its path (as bots do),
so it has to meet your blade, and a feint gets past it. With the normal
game they see VR players' animated characters.
Blades are judged as you saw them (lag compensation): your game shows the
others where the server had them a moment ago, and your moves reach it a
moment later still (20-60 ms on a LAN), so your blade meets their blade and
their body where they were when you saw them, and their cut on you waits
for your moves of that moment - if your blade met that swing of theirs
first, or cut them down first, the cut never lands. (Between two VR
players, blades meet where the server has them. Server setting
g_vrSaberUnlag 0 turns it off.)

VOICE CHAT: talk to the players near you, and hear them from where they
are - louder near, quieter further off, from the side they're on, their
lips moving as they talk. It works in games hosted with this build, for
everyone running it (in a headset or not). Set it up in Setup > Sound, in
the main menu or in game (left menu button, Setup, Sound), under the sound
settings:
  Voice Chat          on or off
  Voice Chat Volume   how loud the others are
  Microphone          click the one you talk into (the green one is in
                      use; "System default" is the one Windows uses). A
                      Quest's own microphone, over Link or Air Link, is
                      "Headset Microphone (Oculus Virtual Audio Device)".
  Input Mode          Voice Activity: you're heard when you speak.
                      Push to Talk: only while +voice is held - put it on
                      a button first, e.g. the left stick click:
                      bind AUX24 +voice   (that button walks otherwise)
  Voice Activation    how loud you are now: the bar turns green when you'd
                      be heard, and you're heard when it goes past the
                      orange mark. Click or drag along the bar to move the
                      mark (just above the bar's level when you're quiet).
  Mic Boost           if the others say you're too quiet
Everyone within about 45 m hears you (sv_voiceRange 1600 game units);
spectators hear the players near them (or near whoever they follow) and
other spectators. Console: voice_mics and voice_mic <number> list and pick
microphones; voice_mute <name or number> silences someone (voice_unmute
<name>, or voice_unmute all). If others hear themselves echo, the headset's
speakers are reaching its microphone: raise the orange mark, or use
headphones. Your settings: cl_voice, cl_voiceVolume, cl_voiceMic,
cl_voiceMode, cl_voiceThreshold, cl_voiceGain. Hosting: sv_voiceChat 0
turns it off on your server; sv_voiceRange 0 lets everyone hear everyone.

Hosting: start a game from the menu the way you do with bots; your PC is
then the server. Others connect with  connect <your address>  in the
console (Shift+~); for players over the internet forward UDP port 29070 to
your PC. To see VR players as they move, the others need this build too:
give them this whole OpenJK-VR folder (it goes in their own GameData
folder, as above). A server that runs on its own, for others to join
whenever (on this PC or a rented Linux server): "Launch OpenJK VR
Server.bat", see README-SERVER.txt.

Duels: the Duel game type is the simplest way to fight one at a time. In
Free For All you can also challenge someone to a private duel the Jedi
way: salute them. Hold your lit blade upright close in front of your face,
in one hand, and keep it there while you look at them. A light tick says
it's being read as a salute (move if you didn't mean one), and a moment
later the challenge goes out, felt in that hand. When someone challenges
you, you feel it in both hands and see who: accept by saluting them back
within 10 seconds (5 on other servers). Both sabers go off, and on again
two seconds later, when the duel starts. From then on nobody else can hurt
either of you, nor can you hurt them, until one of you is down or you're
far apart. On a server of this build any number of duels go on at once,
and bots accept a challenge. To challenge someone else, lower the blade
and salute them (a few seconds after the last). Players without VR use
engage_duel (look at them, press it); you can put it on a button too, e.g.
the left stick click:  bind AUX24 engage_duel  (that button walks
otherwise). cg_vrDuelSalute 0 turns the salute off.

The wheel: hold Y, push the right stick toward a slot and let go of Y (a
quick press of Y still picks the next Force power):
  up     Say...       the chat line, with the headset's keyboard
  right  Poses        sit (on a chair or a bench: stand with your back to
                      it first), kneel, lie down, sit cross-legged
  down   Companions   (still to come)
  left   Roleplay     read the bio of whom you look at, challenge them to a
                      duel, the Roleplay menu (see ROLEPLAY)
Rest on Poses or Roleplay a moment and its ring opens: push toward what you
want there and let go of Y (Back, up and to the left, goes back; let go
with the stick in the middle and nothing happens). What can't be done now
is greyed out, and the line under the wheel says why. Without the
headset, hold P (or O or I, whichever was free the first time: bind <key>
+posewheel for another), move the mouse toward a slot and let go (Escape:
none).

Poses: your character goes down into it and your view with it, to its
eyes; your head and arms stay yours, so you can still talk with your
hands, and the body stays facing the way it went down while you look
about. Moving, jumping, crouching, attacking or using the Force gets you
up again (healing you can do sat) - so does lighting your saber, which a
pose puts away - and in the wheel the pose you're in reads "Stand up".
Everyone sees it, players without VR too. In the console: pose sit, pose
cross, pose kneel, pose lie, pose stand.

ROLEPLAY: on a server with roleplay switched on (its owner switches it on
and off at any time, no map change: g_rp 1 - or set Roleplay on Create
game when you host), you play characters of your own. Open the Roleplay
menu - the ROLEPLAY tab under the in-game menu's bar, or Roleplay menu on
the wheel's Roleplay ring - and make an account (a name for it and a
password, twice). Then make a character there: a name no one else on the
server has, and in the Editor its faction (Heroes, Villains or Neutral), a
one-line description and a bio. While roleplay is on, everyone sees your
character's name instead of yours, and what you wear while you play it
(model, saber and skin colours, chosen in the game's own player menu)
stays with that character. An account keeps up to 5 characters (the
server's g_rpCharacters). Switching characters respawns you, so it waits
until you're out of a duel and haven't been hurt for 10 s. With roleplay
off the game plays as before, and nothing is lost.
  Your password never leaves your PC: the game makes a key of it and logs
in with that. Remember me keeps the key (never the password) and logs you
in on that server by itself next time; Log out forgets it. A forgotten
password: the server's owner can give you a temporary one.
  Talk (the chat key, or Say... on the wheel) reaches only the players
within about 15 m (the server's g_rpTalkRange), and shows over your head
in a speech bubble for 6 s;  /me draws his saber  shows as "* Kael draws
his saber". The team chat key, or /ooc <text>, goes to everyone, out of
character, under your own name. Commands work typed into the chat line
too.
  Look at someone playing a character - within 4 m, with your own saber
off or fists out - for half a second, and their card shows: name, faction
and description. Their bio: Read their bio on the wheel's Roleplay ring,
or the menu's People page (everyone here, with their faction and
description).
  The menu's other pages: Chat & cards (speech bubbles, cards and Remember
me on or off: cg_rpBubbles, cg_rpCards, cg_rpRemember), Help, and Server,
for the owner: roleplay on or off, characters per account and how far
talk reaches - straight into a game you host; on your own server, after
typing its rcon password into the page.
  In the console (Shift+~):  new <account> <password>,  login <account>
<password>,  logout,  changepassword <old> <new>,  bio (of whom you look
at), and  rp  for your characters: rp new <name>, rp play <name or
number>, rp faction heroes|villains|neutral, rp desc <text>, rp bio
<text>, rp delete <name>; rp look <player> shows anyone's card. A password
typed in the console shows as *s and isn't kept in its history (one with a
; or // in it goes in quotes; one with a " in it can only be typed in the
menu), and a line that would cut a password short doesn't run.

Watching yourself: "Launch OpenJK Desktop.bat" starts the same game
without the headset (also for friends without VR). It keeps its own
settings (Documents\My Games\OpenJK-VR-Desktop), so it never changes your
VR setup. Start the VR game first and host, then in the desktop copy:
  name Watcher           (so the two of you aren't both "Padawan")
  connect 127.0.0.1      (not "localhost": that only reaches the same copy)
  team s                 (spectate)
then click to follow the next player (jump stops following). With a saber
out you see them from behind; with a gun type  cg_thirdPerson 1.

Playing on a monitor (the desktop copy, or vr_enabled 0): Setup > Video has
  Display Mode       Windowed, Fullscreen, or Borderless Window (a window
                     covering the screen, with no edges: switching away
                     from the game is instant)
  Video Mode         Desktop (your monitor's own resolution - best with a
                     borderless window), every resolution your monitor
                     has (widescreen and ultrawide too), or Custom
  Custom Resolution  the size Custom is: click it and type one, e.g.
                     2560x1080
then Apply Changes. On a widescreen the HUD and menus keep their shape
(r_aspectCorrect2D 1; 0 stretches them across the screen as the original
game did), and you see more to the sides rather than less above and below
(cg_fovAspectAdjust 1, at your cg_fov). In the console: r_mode -2 is the
desktop's resolution, r_mode -1 uses r_customwidth and r_customheight,
r_fullscreen 1 is fullscreen, r_noborder 1 (with r_fullscreen 0) a
borderless window - then vid_restart.

LOGS for working out what feels off (both on by default):
  games.log    (g_vrSaberLog 1, games you host) every swing - how fast,
               which weight, one or two hands - every cut and clash, every
               bot swing your blade stopped, every touch of blades, every
               guard raised against you (bots, players without VR), and
               every cut on you your blade stopped as you saw it ("blocked")
  vrdebug.log  (cg_vrLog 1) your two-handed holds, swings, clashes, the
               animation and fingers of the hand holding the saber, every
               2 s how your arms are drawn ("arms:" - each elbow's height,
               how your real wrist is bent and your forearm turned against
               the arm drawn, and the shoulder), and every 10 s how
               smoothly the game arrives ("net:" - snapshots a second,
               ping, and how often others had to stand still because the
               next snapshot was late)
Both are in Documents\My Games\OpenJK-VR\base.

The controller buttons are ordinary keys named AUX16-AUX27, so you can
rebind them in the console, e.g.  bind AUX23 force_throw
  AUX16 right trigger   AUX20 A   AUX24 left stick click
  AUX17 left trigger    AUX21 B   AUX25 right stick click
  AUX18 right grip      AUX22 X   AUX26 right stick up
  AUX19 left grip       AUX23 Y   AUX27 right stick down

Console (Shift+~) commands:
  vr_recenter        re-centre your position and the menu screen
  vr_status          print headset / runtime information


SETTINGS (console cvars, saved automatically)
---------------------------------------------
  vr_enabled 1           0 = desktop mode (then: vid_restart); the VR
                         launcher always starts in VR (for desktop mode
                         use "Launch OpenJK Desktop.bat")
  vr_resolutionScale 1   eye resolution vs. headset recommendation (vid_restart)
  vr_worldScale 34       game units per metre (bigger = world feels smaller)
  vr_positional 1        camera follows your head movement
  vr_positionalMax 0.25  ...up to this many metres
  vr_hudFov 50           HUD size (vertical degrees)
  vr_hudDepth 3          HUD distance (metres)
  vr_menuDistance 3      menu screen distance (metres)
  vr_menuHeight 2.2      menu screen height (metres)
  vr_headAim 1           0 = aim with the mouse only; head just looks around
  vr_mirror 1            show the left eye in the desktop window
  vr_handAim 1           0 = guns aim with your head too
  vr_turnMode 0          0 = snap turn, 1 = smooth turn
  vr_turnAngle 45        snap turn angle
  vr_turnSpeed 180       smooth turn speed (degrees per second)
  vr_moveDirection 0     1 = move where your left hand points
  vr_deadzone 0.2        thumbstick deadzone
  cg_vrAimDot 1          the floating crosshair (cg_vrAimDotSize to resize)
  cg_vrGunX/Y/Z 0        nudge the gun in your hand (forward/left/up units)
  cg_vrWorldGun 1        0 = hold the old first-person gun models instead
  vr_gunPitch 0          tilt the gun (and its aim) in your hand, + = up
  cg_vrBody 1            0 = no body in first person (classic view)
  cg_vrBodyArms 1        0 = body without tracked arms (animated arms)
  cg_vrOthers 1          0 = other VR players animated as in the normal
                         game (not as they really move)
  cg_vrBodyBack 4        how far (units) the body stands behind your eyes;
                         smaller = closer to you, but you may see the neck
                         when looking straight down
  cg_vrBodyTwist 45      how far you can turn your head before the torso
                         follows (degrees)
  cg_vrHandX/Y/Z 0       nudge an empty hand or the saber hilt on the
                         controller (forward/inward/up units); a gun's
                         grip follows cg_vrGunX/Y/Z
  cg_vrHandPitch 0       tilt an empty hand on the controller (degrees)
  cg_vrBodyWrist 1       0 = stiff wrists (the forearm turns the hand)
  cg_vrSaber 1           0 = saber in third person, as in the stock game
  cg_vrSaberPitch 0      tilt the blade forward from your fist (degrees)
  cg_vrSaberSolid 1      0 = blades pass through each other (the game
                         still stops them where they met)
  cg_vrTwoHand 1         0 = the left grip never takes hold of the hilt
  cg_vrHaptics 1         how strong the controllers' vibration is (0 off,
                         up to 2)
  cg_vrSaberWeight 1     how heavy the saber feels as you swing it (the
                         vibration of its weight: 0 none, up to 2)
  cg_vrSaberInertia 1    how much a medium or heavy blade resists being
                         whipped about (0 = none: exactly where the
                         controller points it; up to 3)
  cg_vrLog 1             0 = don't write vrdebug.log
  cg_vrTwoHandLever 0.7  how much the line between your hands steers a
                         two-handed blade when they are far apart
                         (0 = only your wrists, 1 = only the line)
  cg_vrBodyDebug 0       1 = log what the body found on your model,
                         2 = also log how exactly the hand grips the gun,
                         3 = also show the body's own copy of the gun

On first start the VR build turns off view bobbing (cg_bobPitch/Roll/Up);
the camera dip on landing is also skipped in the headset.
Settings and downloads live in Documents\My Games\OpenJK-VR, separate from
regular OpenJK.

Camera: guns, the lightsaber and force powers are first person. The game
itself switches to a third-person camera (behind your character) with fists
out, in saber locks, while knocked down, in vehicles, on emplaced guns,
while watching another player and when dead.
Head tracking still works there: you look around from the third-person
camera. "cg_thirdPerson 1" gives third person all the time.


KNOWN LIMITATIONS
-----------------
  * Shots on normal servers still start at your eye, not the gun barrel
    (they go exactly where the aim dot shows). The VR server mod will fire
    them from the gun.
  * Other players see your real arms and head only in games hosted with
    this build, and only if they run it too (in a headset or not);
    elsewhere they see an animated character facing where your gun
    points.
  * Online, other players' arms and blades are drawn a little behind where
    the server has them (the same delay as everyone's movement). Your
    blade is judged as you saw them (see PLAYING WITH OTHERS), but someone
    else's cut on you lands only once your moves for that moment are in -
    a moment later on their screen; with much lag (over a quarter second)
    it lands anyway. Between two VR players blades meet where the server
    has them, so those blocks are the most exact with a low ping.
  * Solid blades stop only your own blade. Another player's blade is drawn
    where they had it a moment ago; one swung fast into yours can pass
    through it on your screen (it stopped on yours in their game, or the
    server parried it).
  * Demos recorded with this build in games hosted with it (record, or
    the server's svrecord / sv_autoDemo of players running it) keep the VR
    bodies, so they play back in this build only (the normal game and
    older builds stop at the first one).
  * The game picks your leg animation from the gun's direction, so with
    the gun pointed far to the side of where you walk, the legs may strafe.
  * If your real arm reaches much further than the character's, the hand
    stops at full stretch while the gun stays in your real hand.
  * On normal servers saber hits and blocks still come from the game's
    swing animations; your real blade only counts on servers running this
    build.
  * Saber combat: blaster bolts are still deflected automatically; no
    saber locks or disarming while you hold the saber in VR; a clash or a
    cut shows on your blade a moment after it happens (it comes back from
    the game's server); custom sabers with more than two blades only fight
    with the first two.
  * Guns are held in the right hand only (for now); only sabers can be
    held with both hands.
  * Mirrors show your character without a head (it is hidden for your view).
  * Full-screen 2D effects (damage flash, fades, scope overlay) only cover
    the HUD rectangle.
  * Things near the edge of your view can pop in (the game culls with its
    own, narrower field of view).
  * Name tags and the third-person dynamic crosshair are drawn at the
    game's field of view and may not line up with the 3D scene.
  * On emplaced guns and vehicles the game limits how far you can turn;
    head yaw beyond that limit is ignored.
  * Video recording (/video) records black frames in VR.
  * Voice chat works only in games hosted with this build (others don't
    pass voices on), between players running it. There's no echo
    cancelling: in a game where you hear others through speakers next to
    your microphone, use headphones or raise the voice activation mark.
    On a Sound page a menu mod has filled up, the voice chat settings are
    behind a "Voice Chat Settings..." button there (where EAX was).
  * Mods that ship their own client DLL (cgame) need an x86_64 build of it;
    servers running server-side mods (JA+, base, etc.) work.
  * rd-vanilla only (the experimental rend2 renderer has no VR support).


HOW IT WORKS (for modders)
--------------------------
VR lives in the engine (openjk.x86_64.exe), the renderer
(rd-vanilla_x86_64.dll) and the client game module (base\cgamex86_64.dll,
for the gun in your hand and your body). It works on ordinary servers.
Servers running this build (base\jampgamex86_64.dll and the engine) also
fight with the VR player's real blades: the client sends the blade poses
with every usercmd (clc_moveVR, only to servers whose systeminfo has
sv_vrx), and the game module sweeps them for cuts and clashes. The dedicated
server (openjkded.x86_64.exe) is the same server code and game module, with
no client. Source files:

  codemp/client/cl_vr.cpp      OpenXR session, frame loop, tracking, aim,
                               controllers, menu pointer
  codemp/qcommon/vr_public.h   state the engine shares with the cgame
  codemp/rd-common/tr_vr.h     state shared with the renderer
  codemp/rd-vanilla/tr_scene.cpp   per-eye camera, HUD placement
  codemp/rd-vanilla/tr_main.cpp    asymmetric per-eye projection
  codemp/cgame/cg_vr.c         hands, aim dot
  codemp/cgame/cg_vrbody.c     your body: placement, facing, arm IK; the
                               blade's weight and solid blades; the blade
                               poses sent to the server, haptics
  codemp/qcommon/vr_net.h      blade poses on the wire
  codemp/game/g_vrsaber.c      physical saber combat on the server
  codemp/client/cl_voice.cpp   voice chat: microphone, Opus, sending,
                               receiving (clc_voice / svc_voice, only with
                               servers whose systeminfo has sv_voice)
  codemp/client/snd_voice.cpp  voices played from where the players are
  codemp/server/sv_client.cpp  the server passing voices on to those near
  codemp/ui/ui_vrvoice.c       the voice chat settings (base\ui\vr_voice.menu)
  codemp/client/cl_vrkeyboard.cpp  the headset's keyboard
  codemp/cgame/cg_vrpose.c     the wheel (poses: game/bg_vrpose.c)
  codemp/game/g_vrrp.c         roleplay, the server's side: the switch,
                               accounts, logins, characters, chat
  codemp/cgame/cg_vrrp.c       roleplay, the player's side: logging in,
                               speech bubbles, cards
  codemp/game/bg_vrcrypt.c     SHA-256, HMAC and PBKDF2 for the logins
  codemp/ui/ui_vrrp.c          the Roleplay menu (base\ui\vr_rp.menu)
  shared/sdl/sdl_window.cpp    session creation after the GL context
  vr/build-win64.sh            how these binaries were built
  vr/build-linux-server.sh     the Linux dedicated server (vr/server/)

The arm IK re-aims the upper arm and forearm with Ghoul2 bone overrides
after the game's animation, and turns the hand at the wrist with a new
engine bone transform (G2API_SetBoneVRTransform), so any model on the
_humanoid skeleton works. cg_vrBodyDebug 1 prints what the solver's
self-checks find on your model.

Build (Linux, cross-compiling for Windows with llvm-mingw):  vr/build-win64.sh
Build (Windows, Visual Studio 2022): run CreateVisualStudio2022Projects.bat,
build the "openjk.x86_64" project (x64, Release), and copy
lib/openxr/bin/x64/libopenxr_loader.dll next to the exe.


CREDITS
-------
OpenJK VR was made by Haloexe, developed with Claude (Anthropic). It stands
on the work of many others:

  * OpenJK (https://github.com/JACoders/OpenJK) by the JACoders and every
    OpenJK contributor: the engine and game code this is built on (GPLv2).
  * Raven Software, who made Jedi Academy and released its source code under
    the GPLv2, and id Software, whose id Tech 3 (Quake III Arena) engine it
    grew from.
  * Team Beef (DrBeef, Baggyg and Bummser) for JKXR, their VR port of Jedi
    Outcast and Jedi Academy (https://github.com/Team-Beef-Studios/JKXR,
    GPLv2; lead programmer DrBeef, additional development by MuadDib and
    BaggyG). OpenJK VR's OpenXR session, swapchain and frame handling started
    from JKXR's TBXR_Common.cpp, and its floating screen for menus, the
    console and loading follows JKXR's VR_UseScreenLayer. JKXR showed what
    Jedi Knight can be in VR, with a motion-controlled lightsaber, guns in
    your hands and the game's menus in a headset, and this multiplayer
    version follows its lead. Support them: https://www.patreon.com/teambeef
  * The Khronos Group for the OpenXR SDK. Its headers and loader are
    "Apache-2.0 OR MIT", used here under MIT (LICENSE-OpenXR-loader.txt);
    the loader includes JsonCpp by Baptiste Lepilleur and the JsonCpp
    Authors.
  * Opus by Xiph.Org, Skype, Jean-Marc Valin, Timothy B. Terriberry, Koen Vos
    and the other Opus authors, for voice chat (BSD, LICENSE-Opus.txt).
  * The libraries that come with OpenJK: SDL2 (Sam Lantinga and the SDL
    contributors), zlib (Jean-loup Gailly and Mark Adler), minizip (Gilles
    Vollant), libpng (Cosmin Truta and the libpng authors), libjpeg (the
    Independent JPEG Group) and gsl-lite (Martin Moene). This software is
    based in part on the work of the Independent JPEG Group.
  * llvm-mingw by Martin Storsjö, with LLVM and mingw-w64, which builds the
    Windows version.

Licence: GPLv2 (LICENSE-OpenJK-GPLv2.txt), like OpenJK and JKXR. Jedi
Academy's own files are not included: you need the game. This is a fan
project, not made, distributed or supported by Activision, Raven Software,
Lucasfilm or Disney. Star Wars, Jedi and Jedi Knight are trademarks of
Lucasfilm Ltd. and Disney.
