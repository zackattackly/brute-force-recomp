Brute Force - PC recomp
=======================

Runs the original Xbox game Brute Force natively on Windows. You need your own copy of the game, as an extracted game
folder (the folder that holds default.xbe and the data folder). This download contains no game files.

Install
-------
Double-click Install.bat. It finds your game folder (or asks for it) and adds a "Brute Force" shortcut to the desktop and
Start menu. Run it again any time to change the game folder or to update.

Playing
-------
- The game starts full screen. F11 or Alt+Enter switches to a window and back.
- Click in the game to use the mouse (it is then captured); F10 lets it go. F12 saves a screenshot (shotN.bmp).
- Keyboard and mouse: WASD move, mouse aim, left click fire, right click aim, Left Shift sprint, Q / middle click grenade,
  Esc = START (pause). All keys can be changed in the pause menu under KEY BINDINGS. An Xbox / XInput controller works too.

Online play (PLAY ONLINE)
-------------------------
The game's multiplayer menu has PLAY ONLINE (co-op campaign for 2-4 players, Deathmatch and Squad Deathmatch for 2-8).
There is nothing to set up: pick PLAY ONLINE, then host a match or join one. Every hosted match shows up for every online
player. The game traffic goes through the online relay, so nobody needs to forward ports on a router.
- Private matches: put a line   password <your password>   in netplay.txt (next to the game). Only players with the same
  password see your matches.
- No online play at all: put the word   offline   in netplay.txt.
Everyone needs the same version of this recomp.

Files
-----
netplay.txt   optional online settings (password ..., offline); no file = online, public
keybinds.txt  your key bindings (made by the KEY BINDINGS screen)
windowed.txt  if this file exists the game starts in a window
bfloader.log  the loader's log (useful when something goes wrong)
xbox_hdd      the Xbox hard disk: your saves and options (made on the first start)
