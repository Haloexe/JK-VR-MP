OpenJK VR - Jedi Academy multiplayer in a VR headset
=====================================================

CAUTION: this is an early alpha whose code hasn't been reviewed by a
person. It was written with an AI assistant (Claude) and tested as it
was built, but nobody has checked its code by hand or audited it for
security, and it changes a game that talks to other computers over the
internet. Join servers you trust, host games only for people you trust,
and use a roleplay password you don't use anywhere else. Running a
server: see the caution in README-SERVER.txt. It comes with no warranty
of any kind (see LICENSE-OpenJK-GPLv2.txt).

An OpenJK-based multiplayer client that renders Jedi Academy in stereo 3D
on a PC VR headset (OpenXR). Current state (milestone 2):

  * Stereo 3D with the headset's own field of view and lens geometry
  * Head tracking (rotation + limited positional movement)
  * VR controllers: thumbstick movement, snap/smooth turning, buttons;
    the comfort settings in Setup > Controls (VR MOVEMENT, VR VIEW AND
    VR FORCE)
  * The Force in your hand: thrust to Push, yank to Pull, point to Grip
    and move whoever you hold with your hand (fling them to throw them),
    the trigger for Lightning and Drain, your hand held still on your
    chest, before your face, at your belly, at your temple or over your
    head for Heal, Protect, Absorb, Seeing and Team Heal - the trigger on
    your chest for Rage, over your head for Team Energize - a wave across
    your face or chest for Mind Trick, a swing back past your hip for
    Speed (see THE FORCE)
  * Guns are held in and aimed with your right hand - your left, playing
    left-handed (Setup > VR) - with a 3D aim dot
  * Your own body in first person: it stands under you, turns with you,
    and its arms and hands follow your controllers, gripping what they
    hold (works with any player model on the standard skeleton, custom
    models included)
  * Your fingers: your character's hands follow your fingers on the
    controllers - point, thumbs up, a fist, an open hand - and everyone
    running this build sees them (see YOUR FINGERS)
  * First-person lightsaber: the hilt in your hand, the blade where your
    controller points - in either hand, held anywhere along the hilt, the
    usual way or reversed, passed from hand to hand
  * Physical saber combat in games you host (and on servers running this
    build): your real blade cuts, blocks, clashes and deflects blaster
    bolts, harder the faster you swing; blades are solid - yours rests on
    another blade and slides along it rather than passing through; saber
    styles are the blade's weight
  * Saber, fists and scopes aim where you look
  * HUD on a floating plane in front of you
  * Menus, console and loading screens on a floating screen; point at the
    menus with a controller and pull the trigger to click (the other
    trigger right-clicks)
  * The desktop window mirrors the left eye
  * Multiplayer: other players see you as you really move, and you see
    other VR players as they do - arms, hands, head and saber (in games
    hosted with this build, for everyone running it, in a headset or not;
    see PLAYING WITH OTHERS)
  * Proximity voice chat: talk to the players near you and hear them from
    where they are; pick your microphone in Setup > Sound (see VOICE CHAT)
  * Roleplay on servers that switch it on: an account, characters of your
    own (a name, a faction, a description and a bio, and each its own look
    and Force powers), talk that reaches only those near you with speech
    bubbles over their heads, /me and out-of-character chat, and character
    cards; down instead of dead, until someone of your faction gets you
    up; NPCs that take sides by faction; companions of your own - up to 6
    a character, 2 out with you - that follow you, fight for you, get you
    up and take orders from the wheel (see ROLEPLAY)
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
  Left stick           move (where you look, or where your hand points:
                       see VR MOVEMENT, VR VIEW AND VR FORCE)
  Left stick click     walk (hold)
  Right stick L/R      turn (snap 45 degrees, or smooth)
  Right stick up/down  next / previous weapon (straight to your hand)
  Right stick click    change saber style
  Right trigger        attack (saber in your hand, where blades are real:
                       switch it on and off) - the trigger of the hand that
                       holds your weapon: your saber in your left hand, the
                       left one
  Left trigger         alternate attack (the other trigger) - but while
                       you hold the Force grip, Lightning (on your chest,
                       Rage; over your head, Team Energize: THE FORCE,
                       below)
  Right grip           use (doors, switches, pick-ups), squeezed most of
                       the way (Setup > VR: Grip Press, 85%) - a lighter
                       squeeze only closes your character's fingers; at a
                       holster, take out or put away (see HOLSTERS);
                       holding your saber (Saber Grip: Hold), it holds it
                       - no use meanwhile; let go and it's back in its
                       holster
  Left grip            the Force grip: hold it and your empty hand reaches
                       out with the Force - thrust, yank, point, the
                       trigger, held still by your body, waved, swung (THE
                       FORCE, below); a quick squeeze uses the selected
                       Force power - squeezed as far as Use (or, at your
                       saber's hilt, take hold of it with both hands; at a
                       holster, take out or put away; holding a saber
                       (Saber Grip: Hold), it holds it - no Force
                       meanwhile)
                       Taking hold of something - a hilt, a holster - takes
                       only the usual squeeze (past 60%), squeezed afresh
                       where your hand can take hold
  A / B                jump / crouch (or crouch for real: Real Crouch,
                       below)
  X / Y                previous / next Force power (or pick one on the
                       wheel's Force ring)
  Left menu button     game menu (press again to close it)
LEFT-HANDED (Setup > VR, or vr_leftHanded 1): your guns go in your left hand
and you aim with it, your saber goes in your left fist, and the grips swap:
the Force on the right grip, use on the left. A trigger or grip goes on
doing what it did when you pressed it until you let go.
Swap Sticks (below) moves you with the right stick and turns you - and
switches weapon - with the left; the sticks' clicks go with them.

VR MOVEMENT, VR VIEW AND VR FORCE: Setup > Controls - in the main menu, and
Controls in the game's menu - has three buttons under Other, with the
settings for moving about, seeing and using the Force in the headset. Point
at one and click it (the other trigger steps a choice back), or pick it with
the arrow keys (or Tab) and change it with left, right or Enter; push the
stick up or down on a slider to move it a step.
  VR Movement
    Turning          Snap (a step at a time) or Smooth
    Snap Angle       15, 30, 45 (as it comes), 60 or 90 degrees
    Turn Speed       smooth turning: 60 to 360 degrees a second
    Snap Fade        On: your view dips to dark for about a tenth of a
                     second as you snap turn, and turns while it's dark,
                     so you never see the world jump (Off as it comes)
    Comfort Vignette Off (as it comes), Low, Medium or High: the edges of
                     your view darken while the game moves you - the
                     stick, smooth turning, a jump, a fall or a knock -
                     more the faster you go, and clear as you stop.
                     Nothing while you stand still or move in real life
                     (following someone, it goes by how fast they go).
                     Low only shades the far edges, High closes in to a
                     window straight ahead - over the HUD too. Try it if
                     moving with the stick makes you queasy
    Move Direction   where you look, or where the hand holding the moving
                     stick points
    Always Run       the game's own: Yes, the moving stick runs, and walks
                     while you hold it in; No, the other way round
    Swap Sticks      move with the right stick, turn and switch weapon with
                     the left - their clicks swap too (the pose wheel still
                     picks with the right stick: moving with it, you stop
                     while the wheel is open, and move again once the stick
                     has come back to the middle)
    Stick Deadzone   how far a stick must move before it does anything
                     (0.05 to 0.5: more if you drift standing still)
    Play Seated      Yes: you sit, your character stands - your head as
                     you sit up straight is its standing height (taken as
                     you switch it on, and at Recenter)
    Set Height       stand up straight and click it: the game learns how
                     high your eyes are (your controllers buzz, short and
                     firm; long and soft, it couldn't: no floor set up,
                     or not standing up straight - the console says)
    Eye Height (m)   what Set Height measured - or click along it to set
                     it yourself (1 to 2.5 m; Not Set until then: the
                     stick and the arrow keys leave it so, and take it
                     back there going down past 1)
    Real Crouch      Yes (as it comes): once the game knows your height,
                     crouch for real - your head below 70% of your height
                     - and your character crouches; stand up past 75%
                     and it stands. B crouches too
    Recenter         where you stand now becomes the middle, and the menu
                     screen comes in front of you (playing seated, sit up
                     straight: your height comes from there; in a pose,
                     your view goes back to your character's eyes)
  VR View
    Head Movement    your view moves as your head does - leaning, ducking
    Head Reach       how far your view follows your head from your body
                     (ducking, further, once the game knows your height)
                     (0.1 to 0.5 m)
    World Scale      the game's units in a metre (28 to 40: more makes the
                     world around you feel smaller)
    HUD Distance     1 to 6 m in front of you
    HUD Size         30 to 90 degrees of your view
    Menu Distance    1.5 to 6 m in front of you
    Menu Size        1 to 4 m tall (these two move the screen a moment
                     after you stop changing them and let go)
    Gun Aim          where the hand holding the gun points, or where you
                     look
    Gun Angle        tilt guns in your hand (-30 to 30 degrees; their aim
                     tilts with them)
    Resolution       70% to 140% of what the headset asks for (more is
                     sharper, and slower) - from when the game next starts
                     (or after vid_restart)
    Desktop View     what you see in the headset, in the game's window
  VR Force
    Force Gestures   Force grip held (as it comes): your Force hand's
                     gestures (THE FORCE, below), while you hold the Force
                     grip; Open hand: Push and Pull with your hand open
                     too, no grip held - more magical, and easier to set
                     off by accident in a fight; Off: no gestures - the
                     Force grip uses the power picked as long as you hold
                     it, as in the normal game
    Body Gestures    your Force hand held still by your body casts Heal,
                     Protect, Absorb, Seeing and Team Heal, and its trigger
                     there Rage and Team Energize; waved across before your
                     face or chest, Mind Trick; swung down and back past
                     your hip, Speed (Yes, as it comes; THE FORCE, below);
                     No: only Push, Pull, Grip, Lightning and Drain come
                     from your hand, as before
    Hold Time        how long your hand must stay still in a place for its
                     power: 0.3 to 0.8 seconds (0.4 as it comes) - longer
                     if they come by accident, shorter to be quicker
    Thrust Speed     how fast a thrust or a yank must be for Push and Pull,
                     and a swing for Speed: 0.8 to 3 metres a second (1.6
                     as it comes) - lower if they don't come, higher if
                     they come by accident
    Aim Glow         whoever your Grip is aimed at glows while you hold
                     the Force grip out (Yes, as it comes)
    Grip Moves       whoever your Grip holds moves with your hand, and a
                     fling throws them (Yes, as it comes; THE FORCE,
                     below); No: lifted at level 2 and floated in front of
                     you at level 3, as before
Each is a console setting as well (SETTINGS, below).

YOUR HEIGHT. Until the game knows your height, your view goes up and down
with your head from where it was when the game started (or you last pressed
Recenter): recentre in the way you'll play. Once you've used Set Height
(stand up straight, click it - your headset needs its floor set up), the
game goes by how high your head is above the floor: your eyes at your
height are your character's eyes, and ducking, your view follows you down
past Head Reach. Real Crouch crouches your character as you crouch down,
your view staying at your head (running, crouching rolls you, as B does; in
water deep enough to swim in it waits - crouching would take you down). B
still crouches it - standing up, your view goes down with it. No floor? Set
Eye Height yourself, and press Recenter standing up straight. Playing
seated, switch Play Seated on, sit up straight and press Recenter: your
character stands, and (your height known) leaning well down crouches it. In
a pose from the wheel your view starts at your character's eyes, and Real
Crouch waits (crouching would get you up): sit or kneel for real if you
like, and press Recenter to bring your view back to your character's eyes
(as the pose ends, it eases back to your head). Take a pose standing up -
the game won't sit or kneel you from a crouch. Your view never goes lower
than a character lying down has its eyes, so never under the floor; seen
from behind (a saber lock, knocked down, dead) it moves with your head only
as far as Head Reach. Real Crouch needs this build's game files: on a
server with a mod's own, only B crouches.

IN THE MENUS point a controller at the menu screen: the mouse cursor goes
where it points. Your main hand points (the right; the left, left-handed);
lower it - aim it away from the screen - and the other hand points instead,
until you raise it again.
  Trigger              click (the pointing hand's)
  Other trigger        right-click where you point: take a point off a
                       Force power, step a setting back (on a button it
                       clicks, as a right click does)
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

With a gun out you aim with your right hand (your left, left-handed): the
gun is in your hand (the complete third-person model, so it looks right
from every angle) and a small crosshair floats where your shots will land.
With the saber, fists or a scope you aim where you look. Keyboard and mouse
still work; the mouse turns your body.

YOUR BODY: look down and you see your character. Its hands are on your
controllers, its elbows bend on their own, its wrists bend, and its torso
turns with your head (it follows once you look more than 45 degrees to the
side, and straightens out while you walk). The head is hidden so it never
gets in your eyes. Your character's model is measured every frame, so a
small or a tall model, the stock characters or a custom one all get arms
that fit. A gun sits in your hand exactly the way the game holds it in
third person, fingers around the grip. When you reach further than your
character's arm, the body leans in a little so the hand still gets there.
Standing still, your character stands up straight, as you do (the game's
saber stances crouch, which put your hands up at its chest), so its head
is where yours is and your hands sit on its body where they are on yours
- and it bends as you do (BODY BENDS, below); running, crouching, getting
hurt and the Force still play the game's own animations. Everyone running
this build sees you that way, and in games hosted with it blades hit that
body.

BODY BENDS: standing still, your character stands the way you do (Setup >
VR: Body Bends). Its feet stay where they are and it bends from them to put
its head where yours is: lean or bow and it leans and bows - half of a bow
at the hips, the rest along its back - lean to a side and it does; reach
past arm's length and it leans in from the waist. Crouch for real and its
knees bend, its hips going back over its feet, as low as the view follows
you down (about 25 cm, vr_positionalMax - further once the game knows your
height): to the game you're still standing, until Real Crouch crouches you
(your height set: YOUR HEIGHT, above) or you press B.
Each foot stands on the ground under it, on a step or a slope. Look to a
side and its neck turns first, its chest next (once you look more than 45
degrees away), its hips last; swing a saber out and its chest turns with
your arms, up to 30 degrees off its hips. It never takes a step: turning
round, its feet pivot. Running, jumping, a flip, a pose: the game's own
animation, the bend easing out, and back in as you stand still again.
With it on you see other VR players bend the same way, their heads turned
the way they look; they see yours bend if they have it on too (each player's
own Body Bends decides how they see everyone). Blades still meet the upright
body the server has, as before, not the bent one you see. Turned off, your
character - and other VR players', as you see them - stays upright and
moves whole under the head, as it did before.

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

YOUR FINGERS: your character's fingers follow yours (Setup > VR: Finger
Tracking). An empty hand, and a gun hand's trigger finger, do what your
fingers do on the controller:
  the thumb        on a button, the stick or the thumb rest: down; lifted:
                   up (Quest 3 Touch Plus: half way near them)
  the index finger on the trigger: resting on it, closing as you pull it;
                   off it: straight (Touch Plus also feels how far it's
                   curled, and when it's near)
  the other three  the grip: a light squeeze closes them; loose, they rest
                   round the controller - open once your thumb and index
                   finger are lifted too
So: a fist (a light squeeze, finger on the trigger, thumb down), point
(finger off the trigger), thumbs up (finger on, thumb lifted), a finger gun
(finger and thumb lifted), an open hand (let go, lift both). They do nothing
else: Use and the Force take a grip squeezed most of the way (Setup > VR:
Grip Press, 85%; 60% is how it was before). A gun hand holds the gun as
ever, its index finger along the guard until it touches the trigger, then
on it; a saber hand stays closed round its hilt.
What your controllers feel depends on them and on the OpenXR runtime:
  Quest 3 (Touch Plus), with the Meta Quest Link app (Link or Air Link)
  as the runtime: all of the above
  Quest 3 through SteamVR, Quest 2 and Rift Touch, Valve Index: the
  finger on the trigger, the thumb on the controller
  Vive wands: the thumb on the trackpad
  Windows Mixed Reality: nothing (the fingers rest round the grip)
vr_status says what each controller feels.
Everyone running this build sees your fingers, in games hosted with it.
The stock characters' fingers all move; a custom model whose hands have no
finger bones, or whose fingers don't follow them, keeps its hands as made.

LIGHTSABER: with the saber out you stay in first person. The hilt is
locked in your right fist - your left, left-handed (a second saber in the
other) - the fingers closed round it whatever your body is doing, and the
blade points along your fist the way you would hold a real hilt, so it
follows every turn of your wrist. Its trigger lights it and puts it out;
the other trigger is the alternate attack.
Through flips, force jumps, cartwheels and rolls your body stays upright
(in an in-air pose, or a crouch for a roll) with the saber in your hands,
so you can keep fighting mid-air. Fists, saber locks, knockdowns and
vehicles still use the third-person camera.

BOTH HANDS ON THE HILT: bring your other hand to the saber's hilt -
anywhere from the pommel to just short of the emitter (the blade can't be
taken hold of), above or below your first hand, anywhere along a staff -
and squeeze its grip. That hand closes round the hilt with a short buzz,
and while you hold the grip the saber is in both hands:
  * The hand you use leads - the right, or the left if you play
    left-handed (Setup > VR) - whichever hand took the saber first: its
    wrist points the blade, your other hand helping as far as it agrees,
    and its trigger is the saber's. If your other hand had it, the hand you
    use takes it over the moment both are settled on the hilt (a few
    frames), and nothing jumps as it does: the blade, the hilt and both
    hands go on from just where they are, your other hand holding the hilt
    where it held it - only the fist of the hand you use settles onto its
    controller over a moment, the hilt with it.
  * The blade follows both wrists, and the further apart your hands are,
    the more it follows the line between them, so pushing with one hand
    and pulling with the other levers it round like a real two-handed hilt.
  * Your second hand grips the hilt where it took hold: it doesn't slide
    along it or turn round it. To hold it somewhere else, let go and take
    hold again.
  * Let go of the grip to let go. Moving your hands about doesn't make it
    slip off: only pulling that hand a long way (about 70 cm) from where it
    holds breaks the hold. Taking hold and letting go settle smoothly over
    a few frames.
  * Passing it over: holding it with both hands, squeeze the grip of the
    hand that has it (the hand you use). That hand lets go, and the saber
    stays in the other one - where it holds it on the hilt, and the way
    round it holds it - even once you let go of its grip. Nothing turns as
    it changes hands: the blade points where it pointed, the hilt doesn't
    roll over, and the hand stays round it as it held it. From then on hand
    and saber move together, as they lay as you passed it (until you put
    it away, throw it or pass it on again; cg_vrSaberPitch doesn't tilt
    it). It's then your weapon hand: its trigger lights the saber and puts
    it out. (That squeeze isn't use or the Force.) With Saber Grip on Hold
    (as it comes: see HOLSTERS) you're holding that grip all along, so let
    go of it instead: the saber passes over, and the other hand keeps it
    while you hold its grip.
  * Your other hand's saber - on Toggle, or one that came to it without
    its grip (see HOLSTERS) - is still that hand's while the hand you use
    leads it, and that hand holds the hilt without its grip. Let go with
    the hand you use (a moment's slack doesn't count) and the saber's back
    in your other hand as it held it. Squeeze your other hand's grip: on
    Toggle it lets go, the saber the hand you use's now; on Hold its grip
    holds the hilt from then on.
  * The reverse grip: take hold with your second hand the other way round
    (its thumb toward the pommel), then pass it over - or, with the hand
    you use, just take hold so: it leads that way round - and the blade
    comes out of the little finger's side of your fist, below it. To turn
    it back, take hold with the other hand the usual way round and pass it
    back. There's no flipping it in one hand.
  * Held at the pommel, the blade reaches a hand's width further; a staff
    held at both ends is held with both hands however far apart they are.
  * Both hands hold the hilt in the hole of the fist, the way a real hand
    holds a handle, and wrap their fingers round it whatever the body is
    doing (running, jumping, flipping). (Models whose gloves are one solid
    piece, with no finger bones, can't close their fingers.)
  * Near the hilt the second hand's grip takes hold instead of using the
    Force (or Use); anywhere else it does that as usual. cg_vrTwoHand 0
    turns taking hold (and passing) off.
  * On servers with VR saber combat a two-handed saber swings one weight
    heavier (light becomes medium, medium becomes heavy, and a heavy saber
    in both hands wins clashes against a one-handed heavy), and either hand
    drives the swing (sliding your second hand along the hilt doesn't count
    as a swing). Hits and clashes buzz both hands.

HOLSTERS: your weapons hang on your body, and you take them out and put
them away with your hands - in VR, seeing your own body and the saber in
your hands in first person (cg_vrBody and cg_vrSaber, on unless you've
turned them off), in games you host and on servers running this build.
There are four, each holding one weapon, any weapon:
  right hip                 your saber (a staff too)
  left hip                  a second saber
  over your right shoulder  empty (what you put there hangs on your back)
  over your left shoulder   empty
They're on your character's own hips and shoulders, so they fit any size
of model: a hilt hangs at your belt, emitter up (or down: Hilts Hang,
below), a pistol along your thigh, a rifle across your back from the
shoulder.
  * Reach for one: your hand feels a light tick where a squeeze would do
    something there - a firmer one if there's something to take. An empty
    hand reaches what hangs at a hip from about 16 cm away, anywhere up or
    down along it (your own hip needn't be just where your character's
    is), and what's on your back reached over your shoulder from about
    28 cm (no more than about 10 cm in front of the shoulder, nor below
    it); a squeeze a quarter of a second after it's left a holster still
    counts there, so pulling away as you squeeze isn't a miss. A hand
    holding something has to be nearer to put it in or swap - about 15 cm
    from a hip's, 22 cm over a shoulder and hardly in front of it - so a
    hand resting at your side, squeezed for use, doesn't put your gun
    away. Holster Reach (below) makes all of these further or nearer.
    Squeeze that hand's grip:
      - an empty hand takes out what's there. A hilt is in your fist where
        it closed on it, the way round you took it (the blade out of your
        thumb's side of the fist, or the little finger's: reversed), unlit;
        a gun goes to your gun hand (the right; the left, left-handed) -
        only that hand takes guns out
      - a hand holding something puts it in an empty holster, and the hand
        is empty; a saber has to be switched off first, so it's never put
        away lit (nor is a gun taken out while a saber in your hands is lit)
      - a hand holding something at a full holster swaps them: what it held
        goes in, what was there comes out
    A bump in that hand as something comes out or goes in. A squeeze at
    your other hand's hilt still takes hold of it with both hands, even
    beside a holster; anywhere else the grips are use and the Force, as
    ever.
  * Hold the grip (Saber Grip: Hold, as it comes): your saber stays in
    your hand only while you hold that hand's grip, as in Blade & Sorcery.
    Let go (a tenth of a second: a moment's slack doesn't count, and a
    grip that's holding something only lets go once your hand has mostly
    opened) and it goes straight back to its holster. Let go of a lit
    saber and it switches off first, with its sound, then goes back (its
    triggers do nothing meanwhile; mid-swing, or with a Force power's hand
    out, it goes out as soon as the game lets it); in a saber lock or a
    duel's countdown it stays in your hand, lit, till you squeeze and let
    go again. Unlit, let go of over an empty holster, it goes in there
    instead (from then on it goes back there): a light tick, and that
    holster's ring bright, say where - over a full one, or lit, it goes
    back where it's kept. With every holster full (a gun swapped into its
    own) there's nowhere to put it: it stays in your hand, with a firm
    buzz. Your headset's own menu doesn't make you let go. While a hand
    holds a saber its grip only holds it: no use and no Force from that
    grip, so to use something with that hand let go of the saber first
    (it's back on your belt at once) - the other hand's grip is as ever.
      - both hands on the hilt: the hand you use has it (taking it over
        from your other hand once both are on it, unless that one's letting
        go); let go with either and the other goes on holding it
      - two sabers: each hand's grip holds its own. Let go of the second
        and the first stays lit (with the first thrown, the second waits
        for it to come back); let go of the first and both switch off,
        and the one still in your hand becomes your first (light it again)
        - a siege class's both go back. Let go of both and both go back.
      - a saber that comes to your hand any other way (the stick, caught
        after a throw with that grip let go, or a menu was up when you let
        go) stays there until you squeeze that grip and let go
    Saber Grip: Toggle is the other way: a squeeze takes it out, it stays
    in your hand when you let go, and a squeeze at a holster puts it back.
    Guns are always that way.
  * The quick draw: hold the trigger as you take a hilt out and it lights
    as it comes out. Otherwise it comes out unlit, and its trigger lights
    it.
  * One weapon at a time, as the game has it: taking out a gun puts the
    saber in your hand back in its holster, and taking out a saber puts
    the gun back. The game takes a moment (about half a second) to bring a
    gun up before its trigger fires.
  * The stick still switches weapons: right stick up or down brings the
    next weapon straight to your hand, and what you held goes back to its
    holster (one with no holster is put away, as ever). A weapon you pick
    up and switch to comes to your hand the same way.
  * Your hands can be empty: put away what you hold. Your saber stays the
    game's weapon, unlit on your belt, and the triggers do nothing until
    you take something out; with no saber, your hands are the game's fists
    (the trigger punches). Either way you stay in first person, your hands
    free.
  * Two sabers: take either hilt out first and it fights on its own, in
    any single style your saber offense and that saber allow (the right
    stick click picks the style, as with one saber) - the game makes it
    your first saber, which takes it a moment (a squeeze at a holster
    meanwhile, as when you draw both at once, is done when it has). Take
    the other out too and you fight with two; put one back and you're down
    to one. Either trigger lights or puts out both together. A siege
    class's two sabers stay as the class has them: its first comes out
    first and goes back last.
  * A staff is one long hilt: any holster, held anywhere along it.
  * What's where is kept: a weapon stays in the holster you put it in -
    after dying, each weapon you get back hangs where it hung, and the next
    time you play. You spawn with your hands empty and your saber on your
    belt (a gun you spawn with is in your hand).
  * Everyone running this build sees what hangs on you, where you put it.
    And everyone's hilts hang on their belts while their saber isn't in
    their hand - desktop players' and bots' too: the first on the right
    hip, a second on the left. Nothing hangs on a cloaked player.
  * Setup's VR page has:
      Holsters          Yes (No: weapons switch as they always have, and
                        only your hilt hangs on you - on your belt while
                        it's not in your hand, as everyone's does)
      Saber Grip        Hold (above) or Toggle
      Holster Reach     how far from a holster your hand can be: 1 as it
                        comes, up to 2 (twice as far), down to 0.5
      Hilts Hang        Emitter Up or Emitter Down (taken out, a hilt is
                        held the way you took it)
      Holster Markers   a faint ring at each holster: Off, Near a Hand (as
                        a hand comes near it), or Always - brighter where a
                        squeeze would do something, or a saber you let go
                        of would go
      Quick Draw        Yes (above) or No
      Reset Holsters    your saber back on your right hip, a second on your
                        left, the shoulders empty
    In the console: cg_vrHolsters, cg_vrGripHold (1 hold, 0 toggle),
    cg_vrHolsterReach, cg_vrHiltsDown, cg_vrHolsterMarkers (0,
    1 or 2), cg_vrQuickDraw, cg_vrHolsterLayout (what each holster holds -
    right hip, left hip, right shoulder, left shoulder; "1,2,0,0"), and
    cg_vrHolsterDraw 0 (nothing drawn hanging on anyone - your holsters
    still there to reach).
Servers with holsters show "vrHolsters 1" in their server info; the server
setting g_vrHolsters 0 turns them off. On normal servers, and without the
headset, weapons switch as they always have.

SABER COLORS: any color for each of your sabers, and a black core - a dark
core inside the colored glow. Profile > Lightsaber (in game or in the main
menu) has an RGB Colors button under the blade colors, and Setup > VR has
Saber Colors; both open the RGB Colors page:
  First Saber / Second Saber  the saber you're coloring (Second Saber shows
                       with two; a staff is one saber, both blades alike)
  Red, Green, Blue     sliders, 0 to 255 (the arrow keys, or the stick
                       pointing at one, move it 5 at a time)
  Hex                  type a color's six hex digits, such as 00FFC0 (in
                       the headset the VR keyboard comes up); the sliders
                       follow once all six are there
  Core                 White, or Black
  Stock colors         the game's six colors (a color of its own gone)
  Back / Apply         leave it as it was / use these colors
The sabers along the page show how they'll look as you change them. On
the Lightsaber page a blade color swatch still picks a stock color, and
its Apply Changes keeps the colors you set here.
  * Everyone on a server running this build sees your colors and core
    ("vrSaberRGB 1" in its server info); the server setting g_vrSaberRGB 0
    turns them off - everyone sees stock colors. On other servers you see
    your own colors and everyone else sees the stock color nearest yours,
    which is also what players of the normal game see.
  * No blade glows dimmer than a quarter of full brightness, for anyone:
    a very dark color is lifted so a blade can't all but vanish in a fight.
  * In team games blades take their team's red or blue as ever (a black
    core still shows), and Siege's classes keep their set colors. NPCs,
    companions included, keep their stock colors.
  * A roleplay character keeps its own colors and cores with its look:
    switching characters switches them.
  * In the console: saberRGB1, saberRGB2 (six hex digits; empty for the
    stock color), saberCore1, saberCore2 (1 for a black core).

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
  * Deflect blaster bolts with it. A bolt your blade meets - where you
    saw it coming - goes off it as off a mirror held along the blade:
    square across its way (upright, or lying across), it goes straight
    back; lean the blade's point towards the shooter and it goes off at
    twice the lean - down off an upright blade, away from the point off
    one lying across (lean it towards you: the other way); swing into it
    and it's batted the way you swing. Your Saber Defense counts: none
    or 1 stops it dead on the blade, 2 sends it where the blade does,
    and 3 sends it onto whoever fired it too, when that's within 30
    degrees of where the blade sends it. Your hand feels a sharp tap as
    it goes. A bolt your blade misses hits you: no automatic block, and
    the box round the saber stops nothing - nor does a bolt coming along
    the blade at you (pointed at whoever fired) turn: it glances on into
    you, so hold the blade across its way. With lag, a bolt that hits
    something near you - you, a wall, someone beside you - or bounces off
    it waits there for your moves of that moment (a quarter of a second
    at most), in case your blade met it first: it can hang in the air for
    a moment, for everyone, before it lands or goes off your blade. As in
    the normal game, rockets, thermals, mines, det packs, the DEMP2's
    bolts, the concussion rifle's shots and the repeater's and
    flechette's alternate fire aren't deflected, and in a duel other
    people's bolts just stop on the blade; the disruptor's beam is still
    blocked automatically.
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
    The heavier a blade feels in your hand (below), the slower it can
    move - so the swing speeds a cut needs come down by the same share,
    and a full swing still hits as hard: the blue style's are 77% of the
    light weight's, yellow's 83% of medium's, red's 89% of heavy's, and
    the staff's 72% of medium's (it hits and clashes as medium does - it's
    heavier in feel only); two sabers need the speeds they always have.
    Held with both hands, the same share of the heavier weight's speeds.
  * The weight is something you feel, not just numbers:
      - a blade can't be whipped about faster than its weight lets it: it
        has a top speed and takes a moment to get going and to turn back.
        It turns at most 2000 degrees a second with the blue style, 620
        with yellow, 490 with red; the staff 540, between yellow and red;
        two sabers 750 each. Swing faster than that and the blade - and
        your character's hand, which keeps hold of it - falls behind your
        real hand (blue 25 degrees at most, yellow 55, red 63, the staff
        60, two sabers 50), catching up as you slow. Slow, careful moves
        stay exact, and it never carries on past where your hand stops;
        only swung back hard, its weight carries it on a little first.
        Two hands on it turn it a fifth faster. cg_vrSaberInertia sets
        how much (1; 0 = the blade exactly where the controller points
        it; 2 or 3 = heavier still)
      - swinging it, your hands feel the blade's weight: the faster it
        goes, the harder it pulls - red a deep, rough rumble, the staff a
        little less, yellow less again, blue a light buzz (held with both
        hands, one heavier); swung faster than it can go, it strains as it
        falls behind your hand; and a fast swing stopped short, or turned
        back, lands in your hand with a thud. cg_vrSaberWeight sets how
        heavy it feels (0 weightless, 1 normal, up to 2)
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
          a bolt going off your blade: a sharp tap;
          getting cut: a jolt in both hands.
        Holding the saber with both hands, both feel all of it, the second
        as strongly as the first. cg_vrHaptics sets how strong it all is
        (0 off, 1 normal, up to 2).
  * Setup has a VR page (the VR button under Video, Sound and the rest,
    in the main menu and in game) with Left-Handed (above), Finger
    Tracking and Grip Press (YOUR FINGERS, above), Body Bends (BODY BENDS,
    above), Saber Inertia, Saber Weight, Vibration and the holsters'
    settings (HOLSTERS, above):
    point at Left-Handed and click it, or pick it with the arrow keys (or
    Tab) and set it with left (No) or right (Yes), or change it with Enter;
    point at a slider and click or drag it, or push the stick up or down
    (the mouse wheel) to move it a tenth at a time, or pick it with the
    arrow keys and move it with left and right. Each slider shows its
    value. A setting with named choices (Grip Press, Saber Grip, Hilts
    Hang, Holster Markers) steps on as you click it (or push the stick up, or press right or
    Enter) and back as you right-click it (the other trigger), push the
    stick down, or press left.
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
below), g_vrSaberBolts (1: VR players' blades deflect blaster bolts; 0:
bolts are blocked for them automatically, as before), g_vrSaberDebug 1
(prints every cut's speed and damage, and how every clash ended, to the
console), and the stock g_dismember 100 if you want limbs to come off.

THE FORCE: your Force hand - the left (the right, left-handed), whenever
it's empty - reaches out with the Force while you hold its grip (the Force
grip, squeezed as far as Use):
  Push             thrust it out, fast and far (from near your chest - with
                   Body Gestures, not steeply up: a hand raised fast to
                   your face or over your head isn't a Push): Push goes
                   the way you thrust
  Pull             reach out (not fast: a quick thrust out is a Push),
                   then yank it back toward you in one go: Pull goes
                   toward where you yanked it from
  Grip             hold it out still a moment (about a third of a second),
                   pointing at someone, first thing after you squeeze -
                   not with your palm turned up (that's Drain coming); if
                   Grip is yours, they stay held while you hold the Force
                   grip, as long as the game holds a Grip (five seconds at
                   level 1, four at levels 2 and 3 - still holding, it
                   grips again three seconds on); below level 3, only
                   while you point at them (within 25 degrees or so, side
                   to side)
  Lightning        its trigger, pointing at them - as long as you hold it
  Drain            the same, your palm turned up
  a quick squeeze  any other power: the one picked on the wheel's Force
                   ring (Heal, Speed, Protect, Absorb, Mind Trick, Seeing,
                   Rage, the team powers) or with X and Y
and, held still close in by you for a moment (Hold Time: 0.4 seconds as it
comes) - or with its trigger pulled there (Body Gestures):
  Heal             on your chest, your palm toward you
  Rage             the same place, the trigger pulled there
  Protect          up before your face, your palm out (the trigger there
                   is Lightning, as anywhere you point)
  Absorb           in front of your belly - or as high as your chest -
                   your palm turned up (the trigger there is Drain, as
                   with your palm up anywhere)
  Seeing           at your temple, beside your eye
  Team Heal        raised high over your head
  Team Energize    the same place, the trigger pulled there
Holding still casts the light (or neutral) power of a place; pulling the
trigger there casts its dark twin at once - Heal and Rage, Team Heal and
Team Energize, Absorb and Drain, Protect and Lightning each share a spot,
and the trigger tells them apart, so a character with both sides' powers
has a gesture for each of them. Only your own powers count: a place with
none of yours does nothing (and the trigger there is Lightning, as
anywhere); one with only its trigger's power (Rage, say: a dark-sider's
chest) does that alone. Your hand must stay still there - passing through
a place, or drifting slowly across it, does nothing, nor does a hand the
headset has lost sight of - and a place's power spends the squeeze: let go
and squeeze again for anything else (no Push, Pull, Grip or Lightning
after it). After a Push, a Pull, the trigger or a Grip, holding still in a
place casts nothing till you squeeze again - but its trigger still casts
its dark twin (Lightning by a hair? Move in - the tick says you're there -
and pull again for Rage; a Grip lets go for it). Once your hand's settled
in a place, it stays there for a couple of centimetres more - trembling at
its edge, or with the trigger's tug, it's still that place's - unless it's
well inside another place of yours. A soft tick says your hand has settled
in a place with a power of yours; keep still and its power comes (or,
where the trigger's is your only one, pull the trigger): two slow pulses
for Heal, a deep rumble for Rage, a firm press for Protect (and a faint
hum while it lasts), a rising buzz for Absorb, a soft ping for Seeing, two
quick pulses for the team powers - or a short, dull buzz if the game won't
(no Force left, still waiting, nothing to heal; the team powers are felt
whenever you cast them in a team game - your game can't tell whether a
teammate near you needed one). Cast Protect, Absorb, Rage or Seeing again
while it's going and it's turned off (a soft release), as in the game -
though not in the second and a half after one of them, Speed or Mind Trick
starts: the game won't then (the dull buzz). Readying Drain with your palm
turned up close in by you? Pull the trigger before Hold Time - kept still
there longer, it's Absorb, if that's yours. Places are measured from your
eyes the way your head faces - as players held their hands: your chest 14
to 44 cm below your eyes, from 10 cm behind them (look down at your hand
and your eyes go over it) to 20 cm in front; your face 12 to 35 cm in
front of them; a palm turned up 14 to 62 cm below them, from 20 cm behind
to 25 cm in front; your temple 8 to 22 cm out to your Force hand's side
(your hand beside your headset); over your head at least 20 cm above them.
Your palm turned up 30 degrees or more is the belly's, not the chest's -
and with it the trigger is Drain, wherever you are but over your head. A
hand already settled on your chest stays on it till the palm's a little
past 30 degrees (held still there, Heal); one settled at your belly stays
there till it's back down a little under. A hand held over your head never
Grips; nor does one held still in a place of yours.
And, moved so (Body Gestures too):
  Mind Trick       wave it across in front of you, side to side: 20 cm
                   or more within half a second, anywhere from 15 cm
                   above your eyes down to your chest (35 cm below
                   them: at your chin is fine), up to 55 cm in front of
                   them (your arm near straight is fine) but within 45
                   somewhere as it goes, and not within half a second
                   of your arm held out still further than that
                   (aiming - for Grip, say - then swept across isn't
                   waving; squeeze afresh with your hand brought in, 40
                   cm or nearer, and it's forgotten), going in or out
                   no more than 10 cm and up or down no more than 20,
                   your palm out ahead (the way you face) as it goes
                   across - in toward the middle, it's a wave once it's
                   8 cm past it, as you face and as you faced halfway
                   across (bringing your hand in to your face from your
                   side, for Protect, isn't waving, your head turning
                   or not; your head following your hand, wave further)
                   - your hand itself moving across (turning your head
                   isn't waving, nor is turning round or walking, the
                   hand still)
  Speed            swing it down and back past your hip, fast: from your
                   side at your waist or above (no more than 25 cm in
                   front of your eyes: a hand held out in front, lowered,
                   isn't a swing), 25 cm or more, all within 0.4
                   seconds, going back as fast as a Push (Thrust Speed),
                   to 8 cm or more behind your eyes and below your chest
Like a place's, a motion's power comes only first thing in a squeeze, and
spends it. A light flutter says Mind Trick's cast, one sharp kick Speed -
or the dull buzz if the game won't (no Force left; Mind Trick with your
hand busy - for a second after a trick, say - a swing going or a flag
carried). Cast either again while it's going and it's turned off (the soft
release), as Protect is - and, as with it, not in the second and a half
after one of them starts (after a level 1 trick of a player the game turns
it off from a second on, though your hand still gets the dull buzz till
the second and a half's up). Wave at once: your hand held still before
your face first, as long as Hold Time, is Protect - and bring it in to
your face for Protect, not across past it: across past the middle, it's a
wave. A thrust that may be a motion waits a moment for it: with Speed
yours, a thrust back and down, low by your side, is Speed if it's the
swing; with Mind Trick yours, a thrust or a yank across in front of your
face or chest is Mind Trick if it's the wave (one straight from your arm
held out aiming can't be: it's the Push or the Pull at once).
If not - your hand slows down, leaves where the motion goes, or a third of
a second passes (out of sight of the headset a moment meanwhile, it waits
on), or you pull the trigger or let go - it's the Push or the Pull it was,
the way it went, a moment late. On a server running this build, a level 1
Mind Trick goes at whoever your hand waved across - level from your eyes
toward your hand halfway across (however high or low you wave), onto
someone within 20 degrees of that - and from a quick squeeze or a key,
where your hand points (within 10 degrees); at nobody there it does
nothing (where you look, the game would hold your weapon and your hand a
second for a trick at the floor or a wall). Levels 2 and 3 trick everyone
round where you look, as in the game. On other servers, every trick goes
where you look.
The VR log (cg_vrLog) says where your hand was in each place - where it
was held still close in by you but in none of them, and where it was as
you pulled the trigger close in for Lightning or Drain, with how far your
head was tipped down - which helps fitting the places to you; and where it
went each time it moved 15 cm or more with the Force grip held, nothing
held down - from where to where, from your eyes, how long it took, how
fast it went at most (from your head) and how your palm was turned then -
which helps fitting the motions to you.
While a Grip holds someone, or Lightning or Drain is going, a thrust or a
yank does nothing (as in the game) - and after a Push, a Pull or the
trigger, let go and squeeze again to Grip. Squeeze the Force grip first,
then the trigger: pulled first, or both at once in a fist, the trigger
does what it always does (alt-attack; with the saber out, throws it). Each
power is as the game has it - your level, its cost, its reach - and the
controller tells you what happens: a light buzz as you reach out, a thump
as you Push and a tug as you Pull (when the game does them), a strain
while you Grip someone, a crackle with Lightning, a steady pull with
Drain. Holding your hand out still before a yank grips instead of pulling:
pull in one go - and before the trigger, it grips them first (the trigger
then lets go for Lightning or Drain, or a place's dark twin): for
Lightning at once, pull the trigger before your hand settles. A hand
holding a saber (two sabers, or both hands on one) holds it: its grip
does what it always did. Force Jump is still A held.
On a server running this build ("vrForce 2" in its server info) the Force
comes out of your hand: Push and Pull go the way you thrust or yanked,
Grip, Lightning and Drain where your hand points - where the controller
points, or, your hand held up where you see it, where your hand is as you
see it: put it over them. Whoever's near either of those, anywhere on them
(head to foot), within 15 degrees for Grip and 10 for Lightning and Drain,
is whom the power goes straight at - someone it works on, in its reach,
the one nearest (once it's on someone it stays on them till someone else
is clearly nearer; never onto, or off, someone cloaked or hidden from you
by a mind trick, unless your Seeing shows them); nobody near, it goes
where the controller points. Everyone on this build sees your Lightning
and Drain go from your hand to them. While you hold the Force grip, your
hand held out to Grip (reaching out, the palm not turned up), and your
Grip could catch someone (not after a Push, a Pull or the trigger in that
squeeze - squeeze again - nor in the three seconds after a Grip ends),
whoever it would catch glows (Aim Glow), and the controller ticks as it
lands on someone new (glow or no glow). Nobody the Grip couldn't hold
glows - someone you can't see from where you stand (over a low wall,
say), or high over you with your hand off to their side - and from your
hand it won't catch them at all (the game would let go of them at once,
your Force spent and the wait begun). Once you hold someone, the Grip
follows your hand along whichever of the two lines holds them. Grip from
your hand reaches a quarter further than the game's (320 units: about 9 m,
7.5 m before). A server running an earlier build ("vrForce 1") takes your
Force from your hand as well, but there it goes where the controller
points, and nobody glows. On other servers the gestures still work, but
every power aims where you look (with a gun out, where it points), as in
the normal game - and a Grip that's caught nobody, held on through a Push
or a Pull, can still catch whom you look at after it. The Force Gestures,
Thrust Speed, Aim Glow and Grip Moves settings are under VR Force (above).
Whoever your Grip holds moves with your hand, on a server running this
build (Grip Moves, as it comes) - as if on the end of a long stick your
hand swings. At level 3, sweep your hand round and they swing round you,
raise or lower it and they rise or sink, pull it back from them (in to
your chest, say) and they come closer, push it toward them and they go
further: about a third of a metre of your hand takes them from 2 m away
to 8 m (a push or a pull doesn't swing them; a move more across the way
to them than toward or away from them is a sweep, wherever your hand
is), and never further from you than the Grip reaches (or than you
caught them). Level 2 only lifts and lowers them - they stay where you
caught them, any move of your hand up or down lifts or lowers them (not
out of the Grip's reach: caught at its very edge, as high as you are,
they can't be lifted), and you still point at them to hold them. Walls,
floors and ceilings stop them as they would anyone, and nobody goes
faster than the game's own level 3 floats them. Let go of the Force grip
with your hand moving and they fly on the way you were moving them -
thrown out to the 8 m limit (or as far as the Grip reaches), in to the
2 m one, or up to the top, with your hand still going, they fly on past
it - up off the ground as a Push lifts whoever it shoves (a swoosh in
the controller says so; falls hurt as they always do, and one that kills
them is yours); let go with your hand still and they drop where they
are - carried along as you run, they keep going with you, and if you're
the one knocked or flung away as the Grip ends, they aren't thrown.
Turning your head doesn't move them; turning with the stick - snap or
smooth - swings them round with you, and walking, leaning, crouching or
stepping takes them along. If your controller loses tracking for a
moment, they stay where you last put them, and when it's back they move
on from there. While you hold the Force grip, your body doesn't lean in
to reach further (a reach right out falls a little short). The strain in
the controller gets heavier the faster you move them, and a VR player
you move about sees their Comfort Vignette (if they use it) as for any
other motion. Level 1 holds them as the game does, and so do Grips on
each other that come back round to you (yours on someone gripping you,
or on someone whose Grip holds someone gripping you). Grip Moves: No, or
a server with g_vrGripMove 0, gives the Grip from your hand as before:
lifted at level 2, and at level 3 floated to just in front of you where
your hand points.

PLAYING WITH OTHERS: everyone who runs this build - in a headset or not -
sees a VR player as they really move, in a game hosted with this build:
  * their body faces where theirs does, their head turns, nods and tilts
    where they look, and their arms and hands are where their real ones
    are, fingers round whatever they hold: the saber along their real
    blade (which is what their blows are judged by), in whichever hand
    and wherever along the hilt they hold it, reversed if they hold it so,
    both hands on the hilt when they hold it with both, a gun where they
    hold it (the normal game draws a left-handed player's saber and gun in
    the right hand, as the game animates it)
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
first, or cut them down first, the cut never lands. Bolts too: your blade
meets them where you saw them, and one that hits something near you
waits for your moves of that moment. (Between two VR players, blades
meet where the server has them. Server setting g_vrSaberUnlag 0 turns it
off.)

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
Server.bat", see README-SERVER.txt. Such a server takes each player's
commands - chat, what the menus ask for - five at once, then one more a
second, against floods (sv_floodProtectBurst, sv_floodProtect); one sent
sooner is dropped, and you're told. What this build asks by itself
(roleplay's cards, a remembered login) waits till the server has room for
it after what it sent before, so it leaves room for yours. A game you host
from the menu takes them all.

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
  up and Force        pick a Force power for the Force grip's quick squeeze
  right               - the ones you know but Push, Pull, Grip, Lightning
                      and Drain (Heal, Speed, Protect, Absorb, Mind Trick,
                      Seeing, Rage, the team powers): a quick squeeze uses
                      it (see THE FORCE; not while you spectate)
  right  Poses        sit (on a chair or a bench: stand with your back to
                      it first), kneel, lie down, sit cross-legged
  down   Companions   orders to your companions (see COMPANIONS, under
                      ROLEPLAY)
  left   Roleplay     read the bio of whom you look at, challenge them to a
                      duel, give up (when you're down), the Roleplay menu
                      (roleplay on or off: you log in there either way -
                      see ROLEPLAY)
Rest on Force, Poses or Roleplay a moment and its ring opens: push toward
what you want there and let go of Y (Back, up and to the left, goes back;
let go with the stick in the middle and nothing happens). A stick already
pushed as the wheel or a ring comes up picks nothing till it moves off
that slot. What can't be done now is greyed out, and the line under the
wheel says why. Without the headset, hold P (or O or I, whichever was free
the first time: bind <key> +posewheel for another), move the mouse toward
a slot and let go (Escape: none).

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
  FORCE POWERS: each character keeps its own, as it keeps its look: what
you set in the game's Force menu while you play it (rank, side and
powers) is that character's, so switching from your Jedi to your Sith
switches your powers too, and your own setup comes back when you log out,
delete the one you play or roleplay goes off. The Editor's Force line
shows its side (Light or Dark), and Force Powers beside it opens the Force
menu. Powers set there, or that come with logging in or out, take hold
where you stand once you're out of a fight - as switching characters
waits: out of a duel, not down, not carrying a flag or riding anything,
neither hurt nor hurting anyone for 10 s (in Duel, Power Duel and Siege,
out of the round) - or as you respawn. One that would give you a saber or
take yours (saber offence or none) respawns you then instead, as
switching characters does, and no more often (with roleplay off, it waits
for your next respawn). One the server would cut down to its rank waits
for you to set it anew in the Force menu - a respawn before then cuts it
down and sends you to spectate to set it (unless the server puts you
straight back in), as the game always has. In play, you're told when your
powers wait. A character made before characters kept their powers takes
yours the first time you play it. The server still holds every setup to
its own rank and rules. An older OpenJK VR, or this one on an older
server, keeps one setup for everything, as the game always has.
  GREY JEDI AND FORCE MASTERS: where the server's owner allows them (the
Server page's Grey Jedi and Force Masters rows), a character can be more
than Light or Dark. A Grey Jedi has powers from both sides, bought with
the same points as anyone's - at Jedi Master that's 100, and one whole
side at level 3 already costs 156, so a Grey Jedi still has to choose. In
the Force menu, Grey comes after Light and Dark; with it, the dark side's
powers show on the left under a heading, the light side's where they
always are, nothing dimmed. Either any character may be Grey, or only
those an admin picks. A Force Master has every power at level 3, with
nothing to spend; only an admin makes one. Its Force menu shows every
power at 3 and changes nothing - the setup you had is kept for when it's
put back. Where a character may not be Grey (any more), its Grey setup
counts as the side most of its points are on (those on powers the server
has): the Force menu says so and dims the other side's powers, and its
powers are set once you pick Light or Dark. You're told as what your
character may be changes, and its powers follow - where you stand once
you're out of a fight, or as you respawn. The Editor's Force line says
Grey or Force Master too. On Force-aligned teams everyone plays their
team's side, Grey or not, and the Force menu opened there makes a setup
that side's - a Grey one too. A Grey Jedi may take either Enlightenment
pickup. The wheel's Force ring has room for seven
powers and Back: with more (a Grey Jedi's, a Force Master's), the first
six and More >, where the rest are (Force > goes back). An older OpenJK VR
doesn't offer Grey; on it a Grey character counts as the side most of
its points are on.
  Your password never leaves your PC: the game makes a key of it and logs
in with that. Remember me keeps the key (never the password) and logs you
in on that server by itself next time; Log out forgets it. A forgotten
password: the server's owner can give you a temporary one. Logging in
works with roleplay off too (it changes nothing else then, but an
admin's cheats): the ROLEPLAY tab and the wheel's Roleplay menu open the
menu then as well.
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
  Down, not dead (Free For All and Team FFA, unless the server's owner
turns it off: g_rpDowned): playing a character, when your health runs out
you go down where you stand instead of dying - flat on your back, sabers
off, able to talk but not to move, fight, switch weapons or use the Force
- and nothing harms you there. Someone of your faction gets you up: from
close by (a few steps) they look at you - or aim at you - and hold use (in
the headset, the right grip) for 3 seconds, and you're up with 25 health;
a bar fills for both of you as they do. Or you give up: hold attack (the
trigger) for 1.5 s - a press made after going down - or Give up on the
wheel's Roleplay ring, or /kill; you die as you would have (the kill is
whoever put you down's) and respawn; going to spectate, or to another
team, while you're down is giving up too. A map's own hazards still kill,
down or not - a door's bump or a breath of water is borne, but not much
more: a pit, water (lying in it, a hand's depth drowns you in about 16 s
unless someone gets you up), lava and slime, crushers, kill zones, lasers,
fire, lightning, things of the map's blowing up. A duel's loser goes down.
Bots, players not playing a character, and anyone riding a vehicle, at a
gun or in a monster's jaws die as ever. While you're down your character
stays as it is: no logging in or out, switching, deleting or changing
faction.
  NPCs take sides: the light side's (Jedi, rebels) are with the Heroes and
go after Villains, the dark side's (Sith, stormtroopers, mercenaries) are
with the Villains and go after Heroes; Neutral characters are left alone -
until they attack an NPC, whose side then fights back for the rest of that
life. Nobody's NPCs, turrets or sentries harm someone who's down, and the
crosshair shows your side's NPCs as friends. Monsters are monsters.
  Admins: the server's owner can make an account an admin (rcon: rp admin
<account> on). Logged in to it - roleplay on or off - your cheats work on
that server even with its cheats off, for you alone, and each one goes in
its log; not while you're down. An Admin button shows at the top of the
Roleplay menu (only to you): its page lists every NPC and vehicle the
game has - Find narrows it - and Spawn puts the one picked in front of
you; Kill my NPCs (the ones you spawned there), Kill all NPCs; God mode,
No-clip and No target, each saying whether it's on; Give all; All Force
Powers (every power at level 3 till you change them or the map changes:
give forcepowers). On the People page, with someone picked, an admin
sees what their character is and makes it a Grey Jedi, a Force Master or
Normal again (logged like a cheat; rp force <player> grey|master|normal;
it stays so till an admin or the owner changes it). The console cheats
work too (npc spawn <name>, god, noclip...). Nobody else is shown who's
an admin; logging out ends your god mode, no-clip, no target and All
Force Powers.
Only the server's owner makes admins - at its console, or by rcon with
its rcon password (on a game you host from your PC, you're the server:
your Server page does it straight away - and rcon reaches it too while
your game has an rcon password, such as one you typed on that page for
another server; its Admins says so). A player typing rp admin is turned
away, and the server's log says who tried.
  COMPANIONS: each of your characters keeps up to 6 NPCs of its own. Make
them in the Profile menu's Companions page (COMPANIONS at the bottom of
the game's player menu, or Companions on the Roleplay menu's Editor
page); it shows the companions of the character you play. New makes one:
a name (up to 32 letters - shown over its head when you look at it, and
on its card), what it fights as (the game's NPCs that are people: Jedi,
Reborn, troopers, officers, mercenaries... - Find narrows the list; no
droids, beasts or vehicles), and its look: as its type comes, one of the
player models (on the server), or a custom character made as your own is
(species, colour, head, torso, legs). The preview shows it as it'll look.
Save sends it to the server, which says if it won't do. Tick up to 2 to
come out with you (Comes out); Change and Delete (asked first) the one
picked.
  While roleplay is on and you play that character, the ticked ones come
out a moment after you spawn, and go when you die, spectate, log out,
switch characters or roleplay goes off. They're on your side: they fight
for your faction, and you and they can't hurt one another (nor turn the
Force on one another), nor your teammates and theirs in a team game. Not
in Duel, Power Duel or Siege.
  Orders - the wheel's Companions ring (down): Follow me (they walk with
you and don't fight), Formation (behind you, beside you, or an escort:
one ahead and one behind - each choice the next), Attack (whoever you're
looking at), Defend me (they fight whoever hurts you or them, or goes
after you: what they do unordered), Hold here, Come to me (to you at once,
every 10 s at most - it also calls back one knocked out whose wait is
over), Pose with me (once they're at their spots they take your pose: you
sit, they sit on the floor). Look at one of them as you let go and the
order is that one's alone. What can't be ordered now is greyed, and says
why.
  They keep their spots - 1.5 m behind you side by side, 1.2 m beside, 2 m
ahead and behind - turning with you once you've turned past 45 degrees for
a second; where the way to you isn't straight they follow your trail
(round corners and pillars, up and down stairs), in single file where
there's no room for two (the one nearer you along the way takes the nearer
place), and step round anyone in their way - someone standing on a spot,
they stop by them. With nowhere behind you (your back to a wall) they wait
near you, or come your way. Stuck for 5 s (a second at the foot of a lift
you went up), more than 40 m away, or where your trail can't be walked (a
lift, a train, a jump up a ledge, lava), they come to just behind you -
you in mid-air, to just behind where you left the ground. They keep out of
lava and slime, and slow down at an edge rather than walk off it. They
fight no further than 15 m from you (5 m from where they hold - one with
a gun or thermals still takes on anyone up to 15 m off). One with a gun
and no clear shot (you, your other companion, a pillar or a doorway's
side in the way) steps aside till it has one, or goes after its enemy,
inside those limits (a rocket never nearer than 5.5 m); it backs you up in
a saber fight, fires the disruptor without sniping, and opens fire within
2 s, a villain's too. Thermals are lobbed to burst at their target's
feet, never within 5 m of the thrower (fists for anyone that near); fists
and a baton go up to their enemy and hit. One that can't get at its enemy
(a wall, a ledge or lava in the way) gives up after 2 s and comes back,
leaving them be for 5 s, or till they come at you. When you're down, one of
yours (one not fighting, if there is one) comes to you and gets you up (3 s
by you), as someone of your side would.
  A companion whose health runs out is knocked out: it lies there 10 s,
then goes, and can come again 2 minutes later (Come to me, or your next
life) - the page shows the wait. One put away hurt (unticked, or changed)
comes back as hurt for 2 minutes. The server's owner sets how many come
out with each player and on the whole server, and whether they may look
like player models; a level near its limits holds them back for a while,
and says so. Someone without the model a companion wears sees a stand-in
(a Jedi, a trooper or a rebel). A custom character whose species and
parts have long names (jedi_stkotor_european's, say) looks itself to
players running this build; others see that species' default look.
Without the headset, the same wheel (hold P) and the same page.
  Roleplay needs this build on the server and on your PC: an older OpenJK
VR on a server with this one is told to update, and can't play a
character there.
  The menu's other pages: Chat & cards (speech bubbles, cards and Remember
me on or off: cg_rpBubbles, cg_rpCards, cg_rpRemember), Help, and Server,
for the owner: roleplay on or off, characters per account, how far talk
reaches and whether running out of health downs players - straight into a
game you host; on your own server, after typing its rcon password into the
page (it shows the server's settings as they are). Its Companions: how
many come out with each player (none, 1 or 2), how many on the server at
once, and whether they may look like player models. Its Admins makes an
account an admin, or not (the console shows the server's answer).
  In the console (Shift+~):  new <account> <password>,  login <account>
<password>,  logout,  changepassword <old> <new>,  bio (of whom you look
at), and  rp  for your characters: rp new <name>, rp play <name or
number>, rp faction heroes|villains|neutral, rp desc <text>, rp bio
<text>, rp delete <name>, rp giveup (down); rp look <player> shows anyone's
card; rpcomp list (your character's companions), rpcomp order follow|
formation|attack|defend|hold|come|pose (bind one to a key, if you like:
bind F1 rpcomp order come). A password
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
With more than one monitor the game opens on the one it was on last time,
where it was (fullscreen too: move the window to the monitor you want, then
choose Fullscreen). The picture always fills the window, on a monitor with
other Windows scaling (125%, 150%) too - the window keeps its resolution -
and a window at the Desktop resolution takes the resolution of the monitor
it's moved to (the game restarts its display for that, once you've let go of
it). vid_xpos and vid_ypos are where it was.
Brightness (Setup > Video, r_gamma) is drawn into the game's own picture
(r_gammaShaders 1), so it never changes the rest of your screen and
switching away from the game (Alt+Tab) leaves your desktop as it is;
r_gammaShaders 0 (then vid_restart) sets it on the monitor itself, as the
original game did. A borderless window is an ordinary window to Windows,
not one in true fullscreen, so switching away and back doesn't flash the
screen black; the taskbar still stays behind it (r_noborderPopup 1, then
vid_restart: as before).

LOGS for working out what feels off (both on by default):
  games.log    (g_vrSaberLog 1, games you host) every swing - how fast,
               which weight, one or two hands - every cut and clash, every
               bot swing your blade stopped, every touch of blades, every
               guard raised against you (bots, players without VR),
               every cut on you your blade stopped as you saw it
               ("blocked"), and every bolt your blade deflected
               ("deflect": whose, your Saber Defense, what became of it)
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
  vr_setHeight       your eyes' height above the floor becomes your height
                     (stand up straight)
  vr_status          print headset / runtime information


SETTINGS (console cvars, saved automatically)
---------------------------------------------
(The ones for turning, moving, your view, the HUD, the menu screen, guns'
aim, the resolution and the Force gestures are in Setup > Controls too: VR
MOVEMENT, VR VIEW AND VR FORCE, above.)
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
  vr_snapFade 0          1 = snap turns dip the view to dark (Snap Fade)
  vr_vignette 0          Comfort Vignette: 1 = low, 2 = medium, 3 = high
  vr_moveDirection 0     1 = move where the hand holding the moving stick
                         points
  vr_swapSticks 0        1 = move with the right stick, turn and switch
                         weapon with the left (their clicks swap too)
  vr_deadzone 0.2        thumbstick deadzone
  vr_seated 0            1 = Play Seated: your head as you sit is at your
                         height (taken at Recenter)
  vr_height 0            how high your eyes are, standing, in metres above
                         the floor (vr_setHeight measures it; 0 = not known)
  vr_realCrouch 1        your head below 70% of vr_height crouches you
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
                         when looking straight down (the server's told it
                         too: where your neck is, for your Grip)
  cg_vrBodyTwist 45      how far you can turn your head before the torso
                         follows (degrees; with Body Bends, the chest -
                         up to 70, the furthest the neck turns - the hips
                         another 30 after it)
  cg_vrBodyBend 1        0 = your body (and other VR players', as you see
                         them) stays upright, moved whole under the head
                         (Setup, VR: Body Bends)
  cg_vrHandX/Y/Z 0       nudge an empty hand or the saber hilt on the
                         controller (forward/inward/up units); a gun's
                         grip follows cg_vrGunX/Y/Z
  cg_vrHandPitch 0       tilt an empty hand on the controller (degrees)
  cg_vrBodyWrist 1       0 = stiff wrists (the forearm turns the hand)
  cg_vrSaber 1           0 = saber in third person, as in the stock game
  cg_vrSaberPitch 0      tilt the blade forward from your fist (degrees;
                         not a saber passed over from the other hand: it
                         stays at the angle it lay at as you passed it)
  cg_vrSaberSolid 1      0 = blades pass through each other (the game
                         still stops them where they met)
  cg_vrTwoHand 1         0 = the left grip never takes hold of the hilt
  cg_vrFingers 1         0 = your character's fingers as the animation
                         has them (Setup, VR: Finger Tracking)
  vr_gripPress 0.85      how far a grip must be squeezed for Use and the
                         Force (0.6 to 0.95; Setup, VR: Grip Press) - a
                         lighter squeeze only closes your fingers
  vr_forceGestures 1     your Force hand's gestures: 1 = while the Force
                         grip's held, 2 = Push and Pull with an open hand
                         too, 0 = off (the Force grip uses the power picked)
  vr_forceThrust 1.6     how fast a thrust or yank must be for Push and
                         Pull, or a swing for Speed (metres a second, 0.8
                         to 3)
  vr_forceBody 1         0 = no places or motions: your Force hand held
                         still by your body (or its trigger there), waved
                         or swung casts nothing (Setup, Controls, VR Force:
                         Body Gestures)
  vr_forceHold 0.4       how long your Force hand must stay still in a place
                         for its power (seconds, 0.3 to 0.8; Setup,
                         Controls, VR Force: Hold Time)
  cg_vrForceGlow 1       0 = no glow on whom your Grip's aimed at (the tick
                         as it lands on someone still comes; Setup,
                         Controls, VR Force: Aim Glow)
  cg_vrGripMove 1        0 = whom your Grip holds doesn't move with your
                         hand: lifted at level 2, floated in front of you
                         at level 3 (Setup, Controls, VR Force: Grip Moves)
  cg_vrHaptics 1         how strong the controllers' vibration is (0 off,
                         up to 2; Setup, VR: Vibration)
  cg_vrSaberWeight 1     how heavy the saber feels as you swing it (the
                         vibration of its weight: 0 none, up to 2; Setup,
                         VR: Saber Weight)
  cg_vrSaberInertia 1    how much a blade's weight holds it back as you
                         swing it (0 = none: exactly where the controller
                         points it; up to 3; Setup, VR: Saber Inertia)
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
  * Saber combat: the disruptor's beam is still blocked automatically; no
    saber locks or disarming while you hold the saber in VR; a clash, a
    cut or a bolt going off your blade shows a moment after it happens (it
    comes back from the game's server); custom sabers with more than two
    blades only fight with the first two.
  * A bolt your blade sends back goes on from the moment it met the blade
    as you saw it: with lag, the others - and whoever fired it - see it
    jump a little way along its new way, and one held near you can be
    seen hanging in the air for a moment first. With lag, a bolt turned by
    something else before your moves of that moment came in (another
    blade, a Force Push) can't be met where it was before that.
  * Guns are held in your gun hand only (the right; the left,
    left-handed); only sabers can be held with both hands.
  * The Force: whom your Grip moves goes where your hand takes them as the
    server sees your hand - with lag, a moment behind it. Your shoulders
    aren't tracked: the game puts them where your body faces as it
    follows your head, so turning your body round (not with the stick)
    swings whom you hold a little more or less than you turned, depending
    where your arm points. The server has your head where your body's
    eyes are drawn, so as the game's animation bobs them - walking,
    landing - whom you hold far out bobs a little with them. A push or a
    pull is told from a sweep by which way your hand moves - toward whom
    you hold, back from them, or across - not by where your arm points:
    a swing of your arm that takes your hand mostly toward them (up from
    hanging at your side, or up at someone held high above you) pushes
    them on rather than swinging them. With your hand right by your
    shoulder, a small move of it swings them further than the same move
    with your arm out.
    Only the Force grip's hand is the Force hand: with your saber in that
    hand (and the other one empty), there's none (switch vr_leftHanded).
  * Fingers: the game's skeleton moves three fingers a hand - the thumb,
    the index finger, and the other three together - and each follows
    only what your controller can feel (see YOUR FINGERS).
  * Body Bends: the body bends only standing still (moving, the game's
    animation has it), takes no steps (its feet pivot as it turns round),
    can't stand taller than the model stands or crouch lower than the
    view follows you, and blades meet the server's upright body, not the
    bent one (see BODY BENDS).
  * Holsters: on a model with a cape or a jetpack, what hangs on your back
    (and a hilt at the hip) can sink into it, and long hair or lekku can
    cover a shoulder's; a hilt can touch the leg in some of the game's
    poses (a run, a crouch). Others with the normal game see your weapon
    in hand as the game animates it, and nothing hanging on you.
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
                               controllers, menu pointer; the Comfort
                               Vignette and Snap Fade, drawn over each
                               eye's finished picture (VR_DrawComfort);
                               your height, Play Seated and Real Crouch
                               (VR_HeightFrame, VR_PlaceEyes; the cgame
                               says where the game's eyes are each frame:
                               cg_view.c CG_VR_ReportEyes)
  codemp/qcommon/vr_public.h   state the engine shares with the cgame
  codemp/rd-common/tr_vr.h     state shared with the renderer
  codemp/rd-vanilla/tr_scene.cpp   per-eye camera, HUD placement
  codemp/rd-vanilla/tr_main.cpp    asymmetric per-eye projection
  codemp/cgame/cg_vr.c         hands, aim dot
  codemp/cgame/cg_vrbody.c     your body: placement, facing, arm IK; the
                               blade's weight and solid blades; the blade
                               poses sent to the server, haptics
  codemp/qcommon/vr_net.h      blade poses on the wire, and the avatar
                               (VR_POSE_PROTOCOL 7: its head and fingers,
                               its Force hand; a Push's or Pull's way)
  codemp/qcommon/vr_fingers.h  your fingers from what the controllers feel
  codemp/qcommon/vr_force.h    the Force hand's gestures (the engine's:
                               client/cl_vr.cpp makes the game's inputs)
  codemp/cgame/cg_vrforce.c    the Force hand, the player's side: its feel,
                               its effects, the wheel's Force ring
  codemp/game/g_vrforce.c      the Force aimed from the hand, on the server
  codemp/game/g_vrsaber.c      physical saber combat on the server
  codemp/game/bg_vrsaber.h     how heavy a blade feels, and the share of
                               its cut speeds that makes up for it
  codemp/ui/ui_vrsetup.c       the VR page of Setup (base\ui\vr_setup.menu)
  codemp/ui/ui_vrcontrols.c    VR Movement, VR View and VR Force in Setup >
                               Controls (base\ui\vr_controls.menu)
  codemp/cgame/cg_vrholster.c  holsters, the player's side: where they hang
                               on the model, taking out, putting away,
                               swapping, two sabers, everyone's hilts
  codemp/game/g_vrholster.c    holsters, the server's side (shared rules:
                               game/bg_vrholster.h, bg_pmove.c, bg_saber.c)
  codemp/client/cl_voice.cpp   voice chat: microphone, Opus, sending,
                               receiving (clc_voice / svc_voice, only with
                               servers whose systeminfo has sv_voice)
  codemp/client/snd_voice.cpp  voices played from where the players are
  codemp/server/sv_client.cpp  the server passing voices on to those near
  codemp/ui/ui_vrvoice.c       the voice chat settings (base\ui\vr_voice.menu)
  codemp/client/cl_vrkeyboard.cpp  the headset's keyboard
  codemp/cgame/cg_vrpose.c     the wheel (poses: game/bg_vrpose.c)
  codemp/game/g_vrrp.c         roleplay, the server's side: the switch,
                               accounts, logins, characters, chat, admins
                               (g_cmds.c asks it whether a cheat runs)
  codemp/cgame/cg_vrrp.c       roleplay, the player's side: logging in,
                               speech bubbles, cards, the Admin page's cheats
  codemp/game/g_vrdown.c       roleplay: down, not dead; NPCs taking sides
  codemp/cgame/cg_vrdown.c     being down, getting someone up: the panel,
                               the prompts, Give up
  codemp/game/g_vrcomp.c       roleplay: companions, the server's side -
                               coming out, following, orders, fighting
  codemp/cgame/cg_vrcomp.c     companions, the player's side: the wheel's
                               orders, whose they are, stand-in models
  codemp/game/bg_vrcomp.c      what a companion may fight as and look like
  codemp/game/bg_vrcrypt.c     SHA-256, HMAC and PBKDF2 for the logins
  codemp/ui/ui_vrrp.c          the Roleplay menu (base\ui\vr_rp.menu)
  codemp/ui/ui_vrcomp.c        the Companions menu (base\ui\vr_comp.menu)
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
