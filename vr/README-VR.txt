OpenJK VR - Jedi Academy multiplayer in a VR headset
=====================================================

An OpenJK-based multiplayer client that renders Jedi Academy in stereo 3D
on a PC VR headset (OpenXR). Current state (milestone 2):

  * Stereo 3D with the headset's own field of view and lens geometry
  * Head tracking (rotation + limited positional movement)
  * VR controllers: thumbstick movement, snap/smooth turning, buttons
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
    build): your real blade cuts, blocks and clashes, harder the faster you
    swing; blades are solid - yours rests on another blade and slides along
    it rather than passing through; saber styles are the blade's weight
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
    own (a name, a faction, a description and a bio), talk that reaches
    only those near you with speech bubbles over their heads, /me and
    out-of-character chat, and character cards; down instead of dead,
    until someone of your faction gets you up; NPCs that take sides by
    faction; companions of your own - up to 6 a character, 2 out with you -
    that follow you, fight for you, get you up and take orders from the
    wheel (see ROLEPLAY)
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
  Right stick up/down  next / previous weapon (straight to your hand)
  Right stick click    change saber style
  Right trigger        attack (saber in your hand, where blades are real:
                       switch it on and off) - the trigger of the hand that
                       holds your weapon: your saber in your left hand, the
                       left one
  Left trigger         alternate attack (the other trigger)
  Right grip           use (doors, switches, pick-ups), squeezed most of
                       the way (Setup > VR: Grip Press, 85%) - a lighter
                       squeeze only closes your character's fingers; at a
                       holster, take out or put away (see HOLSTERS);
                       holding your saber (Saber Grip: Hold), it holds it
                       - no use meanwhile; let go and it's back in its
                       holster
  Left grip            use the selected Force power, squeezed as far as
                       Use (or, at your saber's hilt, take hold of it with
                       both hands; at a holster, take out or put away;
                       holding a saber (Saber Grip: Hold), it holds it - no
                       Force meanwhile)
                       Taking hold of something - a hilt, a holster - takes
                       only the usual squeeze (past 60%), squeezed afresh
                       where your hand can take hold
  A / B                jump / crouch
  X / Y                previous / next Force power
  Left menu button     game menu (press again to close it)
LEFT-HANDED (Setup > VR, or vr_leftHanded 1): your guns go in your left hand
and you aim with it, your saber goes in your left fist, and the grips swap:
the Force on the right grip, use on the left. A trigger or grip goes on
doing what it did when you pressed it until you let go.

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
you down (about 25 cm, vr_positionalMax): to the game you're still
standing, so to duck under something press B, which crouches you as ever.
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
below), g_vrSaberDebug 1 (prints every cut's speed and damage, and how
every clash ended, to the console), and the stock g_dismember 100 if you
want limbs to come off.

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
  down   Companions   orders to your companions (see COMPANIONS, under
                      ROLEPLAY)
  left   Roleplay     read the bio of whom you look at, challenge them to a
                      duel, give up (when you're down), the Roleplay menu
                      (roleplay on or off: you log in there either way -
                      see ROLEPLAY)
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
No-clip and No target, each saying whether it's on; Give all. The console
cheats work too (npc spawn <name>, god, noclip...). Nobody else is shown
who's an admin; logging out ends your god mode, no-clip and no target.
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
  * Saber combat: blaster bolts are still deflected automatically; no
    saber locks or disarming while you hold the saber in VR; a clash or a
    cut shows on your blade a moment after it happens (it comes back from
    the game's server); custom sabers with more than two blades only fight
    with the first two.
  * Guns are held in your gun hand only (the right; the left,
    left-handed); only sabers can be held with both hands.
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
                               controllers, menu pointer
  codemp/qcommon/vr_public.h   state the engine shares with the cgame
  codemp/rd-common/tr_vr.h     state shared with the renderer
  codemp/rd-vanilla/tr_scene.cpp   per-eye camera, HUD placement
  codemp/rd-vanilla/tr_main.cpp    asymmetric per-eye projection
  codemp/cgame/cg_vr.c         hands, aim dot
  codemp/cgame/cg_vrbody.c     your body: placement, facing, arm IK; the
                               blade's weight and solid blades; the blade
                               poses sent to the server, haptics
  codemp/qcommon/vr_net.h      blade poses on the wire, and the avatar
                               (VR_POSE_PROTOCOL 5: its head and fingers)
  codemp/qcommon/vr_fingers.h  your fingers from what the controllers feel
  codemp/game/g_vrsaber.c      physical saber combat on the server
  codemp/game/bg_vrsaber.h     how heavy a blade feels, and the share of
                               its cut speeds that makes up for it
  codemp/ui/ui_vrsetup.c       the VR page of Setup (base\ui\vr_setup.menu)
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
