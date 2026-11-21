# Brute Force recomp: notes for Claude Code

Goal: run the original Xbox game Brute Force (owned by the user) natively on this Windows PC.
History, findings and milestones are in `README.md` (stage 3 = the PC loader, Milestones 72-77).

## The PC loader (`loader\`)
- `bflaunch.exe`: linked at 0x10000 so the Xbox address range is free. It allows only one copy at a time
  (a named mutex), deletes `stop_loader.txt`, swaps in `bfcore_new.dll` if present, then loads `bfcore.dll`.
- `bfcore.dll`: `bfcore.c` (XBE loading, fs: patches, exception handling, port I/O), `kernel.c` + `kernel2.c`
  (the 147 Xbox kernel imports, files, waits, timers, GPU register emulation), `gpu.c` (push-buffer reader, NOP
  interrupts, fences, frame handoff), `render.c` (software NV2A renderer + GPU-path command sender), `input.c`
  (XInput replacement), `dsound.c` (DirectSound replacement and mixer).
- `bfview.exe` (`bfview.c` + `bfgl.c`): the game window, started by the loader; draws with OpenGL from the command
  ring (or shows the software renderer's frames); keys go back through shared memory.
- The game's files come from `C:\Users\zackh\OneDrive\Desktop\iso\Brute Force (USA) (En,Fr,De,Es,It).xiso\` (D:).
  The Xbox hard disk is `loader\xbox_hdd\` (the older `loader\hdd\` is a stale cache and can be deleted).
- Run: `run_loader.bat`, log in `loader\bfloader.log` (status every 3 s, GPU stats every 5 s).
- Stop: close the game window, create `loader\stop_loader.txt`, or `taskkill /f /im bflaunch.exe`.
  It stops by itself after 10 minutes. Never start a second copy (six copies at once once crashed the PC).

## Building (freestanding, no Windows SDK)
Run `loader\build.bat` (it does the steps below with `C:\Program Files\LLVM\bin`; clang writes `.o` files, not `.obj`).
Needs LLVM (clang, lld-link, llvm-dlltool). Import libs come from the `.def` files:
```
llvm-dlltool -m i386 -d kernel32.def -l kernel32.lib -k   (same for ntdll, user32, gdi32, k32_launch)
set F=--target=i686-pc-windows-msvc -O1 -ffreestanding -fno-builtin -fno-stack-protector -mno-sse -Wno-incompatible-pointer-types -Wno-pointer-sign
clang %F% -c bfcore.c util.c kernel.c kernel2.c gpu.c
lld-link /dll /entry:DllMain /base:0x10000000 /nodefaultlib /out:bfcore.dll bfcore.obj util.obj kernel.obj kernel2.obj gpu.obj kernel32.lib ntdll.lib user32.lib gdi32.lib
clang %F% -c bflaunch.c
lld-link /subsystem:console /entry:start /base:0x10000 /fixed /dynamicbase:no /largeaddressaware /opt:noref /nodefaultlib /out:bflaunch.exe bflaunch.obj k32_launch.lib user32.lib
```

## Where it stands (Nov 5, 2026, Milestone 77)
- The game is drawn by the software renderer `render.c` (vertex programs, textures, combiners) and shown by
  `bfview.exe`, a separate window program fed through shared memory (a window inside the loader process made
  Windows freeze the process). `frame.bmp` in `loader\` is refreshed about every 5 s with the current picture.
- Input: `input.c` replaces the game's XInput functions (PC controller + keyboard via the viewer).
  `auto_press.txt` in `loader\` taps START+A every 4 s, for unattended tests (delete it afterwards).
- Milestone 79: the viewer draws with OpenGL (`bfgl.c`, protocol `bfgl.h`, shared memory + command ring); the
  tutorial mission runs at ~27 frames/s. `software.txt` in `loader\` forces the software renderer.
  The NOP software interrupts need the DPCs run at once and PFIFO CACHE1 status following the pending NOP (page 0x3000 trapped).
- Milestone 80: sound by HLE of DirectSound (`dsound.c`; output played by the viewer from the shared ring
  "BruteForcePCLoader_Audio"), Bink movies shown (CPU-written frame buffers presented from memory).
  `nosound.txt` disables sound. No music yet (the soundtrack is WMA).
- Milestone 81: vertex programs run on the GPU (VSH -> GLSL in bfgl.c; vertex arrays read from shared memory,
  cached per range). The mission now runs at ~50-90 game frames/s. `cpuvertex.txt` forces CPU vertex programs.
  Mipmaps are generated in the viewer.
- Milestone 82: fog in the viewer's fragment shader (0x2A4 enable, 0x29C mode, 0x9C0 params, oFog -> R3.a).
  A 9.5-minute mission run had no faults.
- Music: the game's real music is streamed from `data\sounds\<level>.xwb` through the dsound.c stream HLE (works
  since M80). `music\default.wma` is unused by the game; the viewer loops it through MCI only if `wmamusic.txt`
  exists (Milestones 83-84).
- Milestone 85: the game's clock: XAPI QueryPerformanceCounter (0x2F2986) and Bink's timer (0x377780) are bare
  rdtsc with an assumed 733 MHz; both are patched (input.c) to return the PC timer scaled to 733 MHz. Movies and
  splash were ~5x too fast before. The mixer has a soft limiter (Bink's 4 tracks clipped).
- Milestone 86: stutter fixed. Cause: the loader polling PC XInput with no controller connected; each check left
  every other loader thread suspended ~1.5 s. The controller is now polled by bfview.exe (ring pad[30..34]); never
  load XInput in the loader. What the window shows: the surface just drawn at each flip (0x130); for CPU-written
  screens (Bink) the candidate buffer whose memory changes (gpu_display_addr). Game CRTC writes are skipped when
  device +0x1B8 (present params +0x48) is set; don't patch that (menus flicker).
  Vblank on the performance counter (16,667 us). Opt-in: profile.txt, flipstall.txt, vsync.txt, canary.txt.
- Milestone 87: mouse (click to capture, F10 releases; left = fire, right = grenade; mouse_sens.txt), pixel-count
  reports (0x17C8/0x17CC/0x17D0 -> CMD_ZPASS -> GL occlusion query; report at physical offset; fixed false lens
  glare), F12 screenshots (shotN.bmp), game reboot -> restart_loader.txt -> run_loader.bat restarts.
  Aim notes in README (lock-on not yet removed).
- Milestone 88 (README has the details): direct mouse look (hook 0x11E570 -> mouse_apply; turn input +0x5F8,
  pitch via 0x11E7C0), aim assist off (0xC4E70/0xC4EC0 lock, 0x13389D friction/magnetism), Division camera built
  from the character in cam_after (camera node + game camera world transform [char+0x6B8]+0x214; follow update
  skipped; gamecamera.txt = original), sprint Left Shift (0x12CA48 scales desired velocity), right click = aim,
  middle/Q = grenade. Pixel counts min 1, full vertex-range hashing, vsync on.
- Testing with the user: run, they report; never start a second loader. The tutorial pop-ups and auto_press START
  presses disturb automated turn tests.
- Milestone 89 (README has details): character shadow map = PROJECT3D on a depth texture (0x2E), sampled with sampler2DShadow;
  NV2A compares stored OP r (reverse of GL, so the func is swapped), bias toward lit 0.003 (no polygon offset from the game).
  Glow haze: the glow threshold is the scene's STENCIL, read as an R8G8B8A8 texture at the depth buffer's address;
  dep_color copies stencil -> alpha of a colour texture each frame. Border colour, shared depth buffers, cube maps also done.
  Live A/B switches (read every second): shadowmode.txt, glowmode.txt, glowlog.txt.
- Milestone 90: player shots (hitscan 0x2317E0 -> ray 0x230860, hooked at 0x231868) are re-aimed from the muzzle at the
  world point under the fixed reticle (viewer depth at 320,190 -> pad[50]; unprojected with VP constants slots 10-13).
  Spread: disk 18 px hip, 1.2 px aiming. Reticle quad scaled in the viewer by pad[54] (1.25 hip -> 0.8 aim). Aiming moves at 0.6.
  The shooter of a weapon: [[[weapon+0x1B0]+8]+0x1D4] (NOT weapon+8, which every character shares - that re-aimed the AI's
  shots). noaimfix.txt turns it off. bf.dis offset = VA - 0x10000.
- Milestone 92: the game's follow camera update (0x702A0) always runs now (skipping it froze the health bar); cam_after
  overrides the camera after it. camskip2.txt / noblockwrite.txt are A/B switches.
- Milestone 91: the game camera's world transform is [[camera+0x50]+0x5C] (+0x14 rotation, +0x38 eye) - NOT
  [char+0x6B8]+0x214 (that only worked by heap adjacency for the first character; after a death it crashed or hid the world).
  Written only if it holds a rotation; the follow update runs 1.5 s after a target change. Loader probes must check
  memory with readable() (VirtualQuery) - a fault in our own code kills the process. The new squad member being invisible for
  a few seconds is the game's own behaviour.
- Milestone 93 (weapon poses): no gun-up motion exists; MT_DASH (8) is already the run; requesting a motion every frame
  freezes the animation. Poses are done on the skin palette: hook 0x95460 (bones, index, count); bone records 72 bytes
  (+4 part, joint pos at part+0x38; +8 skin 3x4 world). Player skeleton #f18f3616 (29 R UpperArm, 52 R Forearm, 57 R Hand,
  L = R-1). Rifle = weapon+0x6C part (+0x14 rot, +0x38 pos); barrel = its local Z (signed palm -> origin). Sprint pose done
  (README). The aim pose is switched OFF at the user's request (aimpose_on.txt re-enables it). Test aids: forcepose.txt,
  forceaim.txt, camorbit.txt. The 30-minute safety stop is opt-in (timelimit.txt).
- Milestone 94 (unfinished): camera collision via the world raycast 0xDDD10 (README has the arguments and filters); the
  camera ray never hits the walls yet, so it is off unless camcollide.txt exists.
- Milestone 95 (README has details): the user's Razorback replaces the MK-ASLT: 30 rounds / 625 rpm (razor_patch, type +0xE4/+0xCC),
  razorback.stl drawn instead of its mesh with razorback_tex.raw (side projection), name in the xbox_hdd string tables (.orig backups),
  shot/reload sounds from razor_shots.wav / razor_reload.wav (dsound.c, by ADPCM region). Off switches: norazorback.txt, norazortex.txt,
  norazorsound.txt. Left hand pinned to the gun by IK; the body heading follows the aim via a hook at 0x11D7F7 (+0x4A0 target,
  +0x5FC rate; bodylag.txt = original); mouse turn cap 60 rad/s. Launch bflaunch.exe directly (Start-Process on run_loader.bat silently did nothing); check the process is up.
- Milestone 96 (README has details): KEY BINDINGS in the pause menu. tools\xmbedit.py edits .xmb files token by token (blocks of
  at most ~3,900 words or the game crashes); tools\kbmenu_patch.py adds the button and screen (English; *.kb_orig = originals).
  Top menu = [[0x469b58]+8]->vfunc 0x78, list sentinel +8, node +8 = menu, name hash at +0x24. Bindings in loader\keybinds.txt
  (Esc = START, V = B; wheel = keys 0x0E/0x0F). Shotguns keep hip spread when aiming.
- Milestone 97 (README has details):
  - **Online:** System Link over UDP (`net.c`, emulated NVNET chip; `netplay.txt` = `host` / `join addr`). Hosting is tested; joining from a 2nd PC is not yet.
  - **Window:** full screen by default (F11 toggles, `windowed.txt`).
  - **Download:** `dist\make_dist.ps1` -> BruteForceRecomp.zip. It holds an installer plus patches (`tools\make_patches.py` after editing game files); never put xbox_hdd in it.
- Milestone 100 (online relay): PLAY ONLINE needs no setup. Every loader runs net.c's BFN2 "online" mode against bflist.exe
  ("relay" mode) on 127.0.0.1:<port-1>; the helper carries the frames over a WebSocket to the worker's Durable Object relay
  rooms (dist\matchlist, wss://brute-force-list...workers.dev/relay?room=R, a MAC-learning switch). On by default from
  server.txt; old "list" lines mean the relay; "offline" in netplay.txt turns it off; "password" = private room.
  Deploy the worker: `npx wrangler@4 deploy` in dist\matchlist. relay_log.txt = helper connects/disconnects.
  On a friend's PC the WebSocket upgrade got no answer and the helper (no receive limit) hung forever: the handshake now has
  10 s limits, each failure is logged with its step and Windows error, the session uses the system proxy (WinHttpOpen 4),
  and a quiet connected room (receive timeout 12002) is not treated as lost. relay_log.txt starts with the helper's version
  ("relay helper started (version ...)"), a watchdog logs a step still waiting after 15 s / 60 s, a busy port is logged.
  The installer closes bflist/bfview/bfserver running from the install folder before copying (an old hung helper kept
  bflist.exe locked, so updates silently failed) and stops with a message if a file can't be replaced.
  THE real cause on the friend's PC: 41 days of uptime. GetTickCount above 2^31 (24.8 days) makes (int)(now - 0) negative;
  the helper's "time to connect" (tryat = 0) was never true. Never compare ticks signed against a value that may be 0: start
  timers at GetTickCount() or test for 0 first (fixed in bflist.c, net.c is_hosting, dsound.c g_rz_mute).
  After that fix the friend's helper said "relay connected" but none of its frames reached the room. Round-trip test: the
  worker answers a 4-byte "PING" with "PONG"; relay_log.txt says "relay round trip ok (N ms)" or "FAILED: no answer in 6 s";
  "relay data: sent N, received M (send errors)" every 30 s. To test the room from here: a .NET ClientWebSocket on
  wss://.../relay?room=0 hears every broadcast (my game's "received" count went up from test frames).
- Milestone 104: THE online bug: every System Link frame was all zeros. The game's NIC buffers live outside the 64 MB pool
  (e.g. 0x00B84E2E, from NtAllocateVirtualMemory); MmGetPhysicalAddress returns such addresses unchanged and net.c read
  0x80000000 + address (zeros) and wrote received frames there. kernel.c now remembers the pages it handed out unchanged
  (phys_is_direct) and net.c PHYS() uses them directly. [frame] log lines show the first 24 frames sent/dropped.
  Live room view: https://brute-force-list.zacharyhill1234.workers.dev/stats?room=0 (connections, frames, last frame bytes;
  room objects keep their code until everyone disconnects - ROOM_PREFIX in worker.js gives fresh rooms).
  Original (unpatched) default.xbe: the start-up disc authentication is skipped in memory (jge -> jmp at 0x2F8257).
  The in-game widescreen widening is off by default (wide.txt): it made the view pull back further after each sprint.
- Milestone 106 (online joining, untested with 2 PCs): nothing answered the consoles' DHCP, so they had no IP and the match
  reply carried address 0.0.0.0 (the joiner ignored it). net.c local_answer answers DHCP (10.<MAC bytes 3-5>, mask 255.0.0.0,
  server/gateway 10.0.0.1 = MAC 02:42:46:00:00:01, ARP for it answered too) on the PC itself; noaddr.txt = off. Verified: the host's
  reply now carries its 10.x address (a stand-in joiner replaying a real search over a .NET WebSocket into room 0).
- Performance panel (F3 in the game window; perf.txt = shown at start): bfgl.c perf_present/perf_render/perf_draw (GDI text into a DIB,
  drawn top-left in present_tex), log perf_log.csv (one line per second while shown). Loader numbers in RING pad[84..93] from gpu.c
  perf_sample (game flips/s, slowest flip, CPU via GetProcessTimes/GetSystemTimes, net_stats); relay ping = bflist.exe PING every 2 s ->
  "BFNP" report to net.c. Screen capture of the full-screen window comes out black; ask the user to look.
- Performance (Nov 17): busy scenes were 27-37 fps. Causes found with the panel + perf_profile.txt (viewer ms/s per command type, GPU ms/s
  from sampled GL_TIME_ELAPSED queries) and live switches: (1) the camera probe switched render targets twice per draw -> now recorded
  during the frame and replayed in one batch at the flip (bfgl.c probe_draw/probe_replay; probeevery.txt = groups); (2) render.c sent
  the full 8 KB register block before every draw (~370 MB/s) -> gpu.c tracks changed registers, render.c sends CMD_STATE_DELTA pairs
  (fullstate.txt = old way); (3) plain vsync halved late frames to 30 -> adaptive vsync (-1; fullvsync.txt = plain). Now ~60 in busy
  scenes. Left: waits on pixel counts (zq_poll; zlag.txt = one frame late, once flickered objects) and the reticle depth read.
  Then: plain vsync again (tearing with adaptive; adaptivevsync.txt / novsyncpresent.txt, read every second); specialised pixel
  shaders (bfgl.c spec_prog/fs_spec: per vertex program + combiner setup, stages written out with literal register numbers - const
  arrays + loop were SLOWER; nospecfs.txt = the general g_fs; spec_fs_sample.txt = one generated source). GPU fill was the limit
  (resscale.txt 1 test). With NVIDIA Control Panel triple buffering on for bfview.exe, busy scenes hold 60.
  No driver setting needed now: the full-screen window is 1 px taller than the monitor (bfview.c set_full; exactfull.txt = exact),
  which keeps Windows compositing it (an exact-size window got independent flip and tore), and the viewer presents without waiting
  (swap interval 0); the compositor shows the newest frame each refresh = triple buffering on any card. plainvsync.txt = interval 1.
- Missing terrain (Nov 19): the game sends a terrain section only if its visibility test (pixel count of an 18-vertex triangle-strip
  box, prim 6) counts enough; ours were 0 or too few, so ground appeared only when walked up to (also in older versions). bfgl.c
  zrep_write: 18-vertex strip tests report 50000 (realquads.txt = real counts); 4-vertex quads (probably the sun glare) stay real -
  glare not yet checked on a day level. Found with frame_capture.txt (capframe.txt: every draw of one frame + any-samples query)
  and fixedreports.txt. Ruled out: speed fixes, camera, probe, depth clamp/range, drawing rules (dbg.txt letters), disc streaming.
  With the whole map drawn, per-frame vertex hashing and CMP unpacking cost ~400 ms/s: CMP normals now cached per source range
  (cmp_get; oldcmp.txt), faster 64-bit hash (oldhash.txt), and ranges unchanged for 30 checks are re-hashed every 8th frame
  (staggered; hashall.txt = every frame). Busy spots ~55 fps; left: waits on pixel counts (~140 ms/s) and the probe readback.
- Prompts and interact (Nov 19): HUD messages ("Y switch to ...") use a 2nd icon sheet 256x512 hash 0x255B36DC (4 A/B/X/Y sets, found
  by colour in texdump\); bfgl.c keysheet2_build puts key caps on them (g_cells2). Interact is its own action KB_INTERACT (22nd line
  of keybinds.txt, default E): X icons show it. input.c: Reload presses X with the reticle object hidden (always reloads), Interact
  presses X only when an object that is not a character is under the reticle (g_rt from aim_reticle_keep). E/R not yet tested.
- Waits/ping (Nov 19): terrain box reports are written at once in do_zpass (no GPU wait); the probe reads its PBO two cycles later
  (3 PBOs). bflist.c times the panel ping with QPC (version 2026-11-19a): 42-58 ms, but TCP connect to the nearest edge (PDX) is
  ~45 ms itself, so the relay adds little; player-to-player = both pings. Lower needs direct connections (hole punching).
- Specials and shots (Nov 20): special energy = player char +0x490 (float, 100 full), draining while a special is on (special_on:
  dropped within 1.5 s). During it lock-on/reticle targets pass, shots are not re-aimed (the game aims at the lock), and the camera
  centres (goal shoulder 0) so the game's target box (placed from its camera) lines up. nospeciallock.txt / nospecialcenter.txt = off.
  Bullet trail (0x2212E0) reads the weapon's stored direction +0x25C+barrel*12: aim_fix writes the re-aimed direction there too
  (notracerfix.txt). Freeze guard: D3D push-buffer wait 0x371DFB on device+0x196C released after 0.5 s if the GPU is idle ([unstuck]).
  Correction: special centring is now opt-in (specialcenter.txt, it shook); the special's shots aim at the locked character
  (targeting object g_rt +0x5D0, chest = position y + 0.35). Scope (zoom = player +0x7A8 non-zero): cam_build keeps the game's
  own scope camera and aim_fix leaves the shot to the game (our shoulder camera was 4.4 m behind the gun: shots low-left);
  ourscope.txt = old behaviour. Shader slots 10-13 are NOT the scope's view-projection.
- Performance (Nov 21): a busy mission ran ~42 fps with the viewer ~16% busy and the GPU ~25%: the GAME thread was the limit,
  ~88% "in Windows" (profile.txt profiler, kernel.c; it now also prints Ke waits, sleeps, and the loader function the game thread
  entered Windows from). Cause: input.c readable() did a VirtualQuery per call, thousands per frame (pose / weapon / aim code) -
  now cached per 4 KB page for 1 s (uncachedread.txt = old). Also: D3D's write-combine flush 0x372180 (3 trapped register accesses
  per call) patched to sfence; ret (wcflushtrap.txt = original); switch files in input.c go through fattr_cached (1 s). Remaining
  traps ~10k/s, all in D3D's interrupt routine 0x3777xx-0x377Cxx on the ticker ([traps] line every 10 s, kernel2.c mmio_fault).
  Rule: never call Windows per object per frame from hooks - cache it.
- Specials (Nov 21): skipping camera collision during every special made ALL specials shake (found by A/B against the published
  build + specialcollide.txt) - collision is skipped only in lock_mode. The Flint lock-on package (target pass-through, steering,
  smoothing, swipe, shot steering, sprint ban) is OFF by default (flintlock.txt = on) because it leaked into other specials
  (Tex's Berserker also makes the type-16 targeting object). lock_mode learns per character type (c+0xC) whether a special
  really locks (+0x5D0 character). The Flint package now = targets pass + shot steering (hit ray) + swipe + sprint ban; the
  mouse lock-out / steering (locksteer.txt), lock smoothing (locksmooth.txt) and collision-off (specialnocollide.txt) are opt-in.
- Flint lock, final (Nov 21, user: "feels right"): lock_mode (flintlock.txt, learned per character type) -> input.c lock_target():
  the nearest valid character (alive: health +0x54 != 0; within 45 m; in front ~70 deg) from the game's candidates (aim_is_character)
  + its pick (+0x5D0), once the mouse has been still 0.4 s; any mouse movement (> 3 px) releases it. While locked the camera turns
  to the target (cam_build lock camera, nolockcam.txt), Flint follows it (pend 0.6), shots aim at the body's middle (body_center:
  mean of the part objects, 0x80 apart, [+0x3F8] = part 56) and the hit ray is steered there (bray_wrap). NEVER write the game's
  +0x5D0 (froze it). Re-running the hit ray within one shot crashed the game. The cover test (coverunlock.txt) is off; its RING
  slots are pad[96..107] (pad[64..79] = the camera probe matrix - overwriting it broke collision). oldlock.txt = hard lock + swipes.
  Test: infinitespecial.txt refills the special energy below 60.
- Frame cap (Nov 21): after the readable() fix the game ran ~170 frames/s on a 60 Hz screen (choppy, skipping turns). input.c
  mouse_apply waits for the next vblank (g_vblanks follows the monitor; max 20 ms) - one game frame per refresh as on the Xbox.
  nofpscap.txt = uncapped. (The earlier "Tex runs 4 updates per frame" was this.)
- Zoom, final design (Nov 20): OUR camera does the scope (gamescope.txt = the game's scope camera, which popped/slid).
  Eye eases to the scope eye (g_sc_off: right 0, up 0.444, forward -0.485 from Flint's position); on zoom-in the pitch
  (g_view_pitch) and the camera's own yaw (g_cam_yaw + g_yaw_off) ease onto the point that was under the hip reticle
  (reticle_world, depth at 320,190); zoom-out eases back. Flint turns after the camera at 0.35 x the gap per frame (pend,
  sign g_turn_sgn learnt from hip mouse turns) - a full-gap turn overshot and swung her (jitter). The hit is the hitscan
  0x230860 -> 0x230040 (bray_wrap): origin ~0.5 m ahead of the eye, direction = Flint's facing, *a5 = range; zoomed shots
  steer its direction at the crosshair line 100 m out, and the trail (stored dir, drawn from the gun) is pointed at the
  ray's end point after the call. A moved projectile start (pos) is ignored by the game. Depth under the crosshair is
  noisy while zoomed (near/far swings): not used for zoomed aim. Sprint is off during a special (it shook the lock view);
  the lock (lock_char) is held through gaps < 0.5 s and uses a 2.2 s special window.
- Milestone 101 (hit marker): hitmarker.wav (from the user's clip) plays when the player's shot hits a character. Hook on the
  bullet ray call 0x2309ED (0x230040, stdcall 6 args; result arg4: +0x34 = SHOOTER, +0x44 = object hit; a character when
  [[obj+0xC]+4] == 3; alive when health, a float at character +0x54, is above 0) and on the pass-through path 0x2309CB
  (0x230400). The red X (hitmarker_img.raw, 96x96 RGBA) is drawn by bfgl.c present_tex from RING pad[56] (pad is now 64
  words). nohitsound.txt / nohitmark.txt = off. Game objects live at
  0x80000000+: never range-test game pointers with < 0x7FFE0000 (that hid every hit).
- Milestone 102 (camera collision, works): the viewer measures it. input.c cam_probe_send puts a probe matrix in RING
  pad[60..79] (pivot -> wanted eye, 10 degrees, near 0.7); every game vertex shader has a probe mode (u_pr/u_pon) that takes
  (gl_x w, gl_y w, w, 1) back to the world and into the probe; bfgl.c probe_draw redraws solid 3D draws (depth test + writes,
  no blend, 640x480 target) into a 16x16 depth FBO with culling off; probe_flip reads it back via PBOs -> pad[80] distance,
  pad[81] counter; cam_collide stops the camera 0.3 m in front (in at once, out over ~1/3 s). nocamcollide.txt = off.
  (The world raycast 0xDDD10 never hit walls from the camera: Milestone 94 is superseded.)
- Milestone 103 (mouse in the menus, works): silver arrow cursor (bfview.c make_arrow); hovering an option highlights it, left click
  = A, right click = B. bfgl.c menu_glyph/menu_flip find the options each frame from the font quads (256x512 fonts; unselected
  colour 30,110,150, selected 215,236,251; lines touching/overlapping = one option); gl_menu_keys steps the game's selection with
  private keys 0x88 up / 0x89 down / 0x8A A / 0x8B B (input.c adds them to the pad). bfview.c must not rewrite those keys from
  GetAsyncKeyState (a held key flickering to 0 read as extra presses). Where the game stores a menu's selection was not found
  (not in the top menu object or one level down); pad[57] = the top menu's +0x88 busy flag (not set on every menu).
  Test aids: menutext.txt (menu_text_log.txt), menulog.txt (menu_mouse_log.txt), menusel.txt.
- Milestone 105: weapons on the ground could not be picked up (hold X = R). Found by A/B with nogamehooks.txt, then
  aimassist.txt / noweaponhook.txt: the aim-assist removal blanked every lock / reticle target, and pick-up uses the object
  under the reticle (+0x5E4). +0x5E4 is NOT the enemy: one targeting object (type 16, the follow camera's vtable 0x3A2640)
  drives both the lock-on and the pick-up choice, so a "characters only" filter brought the lock-on back. Final: lock and
  reticle targets are blanked while the mouse aims, except while X is held (g_last_pad[1] >> 16 > 30): then pick-up works.
- Power throttling (bfcore.c): the request with IGNORE_TIMER_RESOLUTION is Windows 11 only; Windows 10 refused the whole
  call ("could not be turned off" on a friend's PC with frame drops and flicker), so it retries with EXECUTION_SPEED alone.
- Next: weapon poses (bone level), START icon in HUD text draws black.
- Semaphore releases (0x1D70) are now written to D3D's fence word, replacing the GET==PUT fence shortcut.
- Test aids: `bench.txt` (renderer benchmark at start), `rbench.bat` (standalone), the `[profile]` log lines
  (ticker samples by bfcore RVA, look up in `bfcore.map`), `noviewer.txt`, `noxinput.txt`.
- Next: mission speed (vertex programs on the workers, vertex cache, maybe a GPU backend), then sound.

## Earlier notes (Milestone 76)
- Build with `loader\build.bat` (LLVM is installed in `C:\Program Files\LLVM`). It also writes `bfcore.map`,
  which resolves loader addresses (the DLL is relocated, e.g. 0x73B3xxxx -> 0x1000xxxx).
- The game boots, unpacks `data\common.tgz` into Z:, loads everything for the `splash_screen` level, and runs its
  frame loop: about 35 flips/s, about 145 draw calls per frame. Only clears reach the screen.
- Fixed in M76: NULL handles in NtWaitForMultipleObjectsEx (the I/O thread used to exit), KeDelayExecutionThread(0) = yield
  plus 1 ms timer resolution (the old "bfns.xsb loop" was just slow), APU free-space register 0xFE820010 = 0x80,
  NV2A NOP(param != 0) -> PGRAPH INTR_ERROR software interrupt.
- To look at game code: flatten the XBE (sections at their addresses, base 0x10000) and disassemble with llvm-objdump.
- GPU: semaphore releases are recorded but not written to memory (writing them to PA offset stalled the game).
- Next: draw (vertex arrays, vertex shader programs, textures, register combiners), then input (XInput), then sound.
