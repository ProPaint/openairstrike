OpenAirStrike: put your own game files in this folder (ports/openairstrike/gamefiles/)
====================================================================================

OpenAirStrike plays AirStrike 3D (1.70), AirStrike 2 (2.51) and AirStrike II: Gulf Thunder
(2.71) with the files of your own copy of each game (the Windows version). No game file comes
with it.

Copy here, for each game you own, one of:

  - the game's install folder from a Windows PC (the folder holding the game's .exe and its
    "data" folder with pak0.apk, pak1.apk, ...), copied whole;
  - the game as you downloaded it: a zip, RAR or 7z archive, or its setup .exe (an Inno Setup
    installer; NSIS and Gentee installers are refused: install those on a PC first and copy
    the install folder).

Then start OpenAirStrike from the Ports menu. It installs what is new here into
ports/openairstrike/data/ (an archive can take a minute), then opens the game; with more than
one game, on a game selector. Once a game has started, the files here can be deleted to free
space. If nothing is recognised, the game says so on screen with what it found, and
ports/openairstrike/log.txt has the details.
