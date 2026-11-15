Brute Force - PC recomp
=======================

Runs the original Xbox game Brute Force natively on Windows. You need your own copy of the game, as an extracted game
folder (the folder that holds default.xbe and the data folder). This download contains no game files.

Install
-------
Double-click Install.bat. It finds your game folder (or asks for it), can set up online play, and adds a "Brute Force"
shortcut to the desktop and Start menu. Run it again any time to change the game folder or the online settings.

Playing
-------
- The game starts full screen. F11 or Alt+Enter switches to a window and back.
- Click in the game to use the mouse (it is then captured); F10 lets it go. F12 saves a screenshot (shotN.bmp).
- Keyboard and mouse: WASD move, mouse aim, left click fire, right click aim, Left Shift sprint, Q / middle click grenade,
  Esc = START (pause). All keys can be changed in the pause menu under KEY BINDINGS. An Xbox / XInput controller works too.

Online play (PLAY ONLINE)
-------------------------
The game's multiplayer menu has PLAY ONLINE (co-op campaign for 2-4 players, Deathmatch and Squad Deathmatch for 2-8).
- Public: the installer's default. Hosting a match lists it for every online player; Join shows every open match.
- Private: choose P in the installer and type a password. Only players who typed the same password see your matches.
- In netplay.txt this is:  online <server address>   and, for private matches,   password <your password>
Everyone needs the same version of this recomp.

The online server
-----------------
One PC runs the server for everyone: "Start online server.bat" (bfserver.exe). That PC needs UDP port 34567 forwarded on
its router, and its internet address (or a name for it) goes in server.txt before the download is shared.

Files
-----
netplay.txt   online settings (no file = offline)
keybinds.txt  your key bindings (made by the KEY BINDINGS screen)
windowed.txt  if this file exists the game starts in a window
bfloader.log  the loader's log (useful when something goes wrong)
xbox_hdd      the Xbox hard disk: your saves and options (made on the first start)
