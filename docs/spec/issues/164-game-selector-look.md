# 164: The game selector's look, the same on desktop, Android and the web

Status: done. Affects `as3d/frontend.h` (`GameCard`, `Marquee`, `GameSelector`),
`engine/src/ui/screens_launcher.cpp`, `launcher_marquee.{h,cpp}`, `as2_widgets.cpp` /
`as2_draw.h` (`titleLogoAt`), `apps/game/{launcher_screen,game_view,game_loop,main}.*`,
`apps/web/site/*`, `tools/web_build.sh`, `tools/web_marquees.py`, tests `launcher_test.cpp` and
`apps/web/test/walk.py`, `docs/web.md`, `docs/android.md`. Builds on issue 163.

The owner's request: a better looking game selector, the same on the web, Android and the PC
client; the marquee of AirStrike 3D is the 3D banner of its own main menu, the marquee of
AirStrike 2 is its rusty logo followed by the small target-shaped "2" emblem.

## The marquees

Each card's picture is its game's own title, animated as on the game's title screen, on a dark
stage of its own (the same box on every card: 0.46 of the card's inner width high, 88 to 170):

* **AirStrike 3D** (`Marquee::Banner`): the banner object of its data (`objects\banner.obj`, the
  flaming "AirStrike 3D" mesh), drawn by the very code of its main menu. `GameView`'s banner is
  now `BannerMesh` (`game_view.h`): `drawBanner` passes it the 800x200 viewport of the menu, the
  selector passes it a viewport scaled so that the part of it the title covers
  (`ui::kBannerContent`, measured on the mesh over 8 seconds of its motion) fills the stage,
  scissored to the stage. The main menu's pixels did not change (`tools/regress_as3d.sh`). The
  selector keeps the first game's files mounted while it is up (the mesh's resources load
  from them) and loads its definitions once per opening.
* **AirStrike 2** (`Marquee::TitleLogo`): `gfx\logo\logo.tga` with `clouds.tga` scrolling
  through its letters, the spinning, swelling `two3.tga` emblem at its end and `glow.tga`, by
  `as2::titleLogoAt`, the function the title screen's `titleLogo` now calls (same floats at
  scale 1, offset 0), at offset and scale to fit the stage, at the title clock (half the
  selector's clock).
* **Gulf Thunder** (`Marquee::Emblem`): `gfx\logo\logo_gulf.tga` (512x256) with the clouds
  added through it, as `docs/spec/gulf/frontend.delta.md` 3 describes, white tint (its main
  menu's), fitted in the stage.

Without pictures (or without the banner drawer) a card shows its title in large text.

## The screen

Virtual 800x600 as before: black bars with a rust rule, "Choose a game" in the top bar,
Exit (left) and Play (right) in the bottom bar with the key hint. New:

* **Content-sized cards.** A card is: stage, title, version, two lines of save summary
  (centred in a two-line slot), a rule, "Click / Tap to play". Its height follows its width
  and the row is centred in the band between the rules: no empty bands. Cards are at most 380
  wide, 18 apart, the row at most 1100 wide and 28 from the screen's edge (from the 800x600
  field's in the 4:3 screen mode, which keeps everything, wash and bars included, inside the
  field: `GameSelector::setView`). Exit and Play sit at the row's edges.
* **The focus.** The current card has a dark red fill, a frame that breathes (about once a
  second), a soft glow outside it, a lighter stage and a "Click to play" that breathes with it;
  the others are dim. The headless picture at a chosen moment: `as3d_game --headless
  --selector-shot F.png --selector-time S [--screen 4x3] [--insets L,T,R,B]`.
* **Cutouts** move the row and the buttons (`setSafeArea`), as before.

## The web: the page draws it, from renders

Decision: option (a). The engine cannot draw the selector before a game is chosen without
fetching a game: the banner's object definition, mesh and textures sit inside AirStrike 3D's
25 MB pak (`DefDatabase::load` reads the pak's definitions), and in the bring-your-own build the
files are the player's Blobs in IndexedDB. The page must never fetch a whole game before it is
chosen, and `?game=` and "Change game" (which reloads the page without it) keep working.

* **Bundled**: `tools/web_build.sh` runs the desktop engine headless
  (`as3d_game --headless --selector-marquees DIR --selector-games ... --size 800x600`), which
  draws each marquee with the same code as the selector (`GameSelector::drawMarquee`, loop fit
  on) into 352x162 frames, and `tools/web_marquees.py` turns them into `marquee/<key>.webp`, one
  looping animated WebP per game (75 frames of 84 ms for the banner: 2 pi seconds; 100 frames of
  126 ms for the logos: 4 pi seconds of the selector's clock; about 256, 340 and 546 KB). The
  loops are exact: the banner's yaw swings at the pitch's period and the clouds run 1.6 times
  as fast as on a title screen, so that both motions of a logo repeat together. These are
  renders of the games' own art: the bundled site only (they sit beside the paks).
* **Bring your own**: nothing of any game in the site. Until files are dropped the cards are
  text ("Files needed", shown small in the drop panel). Once a game's files are stored, the
  selector shows its cards with marquees drawn in the browser from those files (`marquee.js`
  reads the pak table and the TGAs, draws AirStrike 2's and Gulf Thunder's logos with the
  engine's arithmetic). **Difference**: AirStrike 3D's card keeps its title in large text (the
  banner is a 3D mesh only the engine draws).
* The page lays the cards out in the engine's virtual units (`--u`: one of them, by height or by
  width), with the engine's colours and proportions, Left/Right/Enter, a pulsing current card.
  Its text is in a system font (the engine's is the games' bitmap font, which the byo site
  cannot have), and it shows the download size where the apps show the save summary (the engine
  reads the saves; it is not running yet).
* `?games=as3d,as2` narrows the selector (tests, pictures).

## Tests

`launcher_test.cpp`: the marquee kind per game; the layout for 1 to 3 cards at 800x600,
1024x768, 640x480, 1280x800, 1600x720, 1600x1200 and 2400x1080, in Wide and 4:3 (centred,
inside the screen and the field, one row, equal gaps, equal marquees, vertically centred in the
band, buttons in the bar); cutouts; pixel checks of the drawn marquees of every game present
(lit, moving over time, nothing past the stage's frame), the pulse of the focus, and the exact
loops. `apps/web/test/walk.py`: the selector's cards and marquees (only renders fetched, no
game file), the pulse, the arrows, byo text cards, byo marquees from stored files.
