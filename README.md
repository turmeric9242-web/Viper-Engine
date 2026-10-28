# Viper Engine v0.1 (C + GLFW + OpenGL 3.3)

3D-only, modular game engine. v0.1 = the Milestone-1 vertical slice: create a 3D scene, add a player and
floor, drive the player with Viper Blocks, press Play.

## Get the .exe
**A) GitHub (no installs):** push this folder to a GitHub repo -> Actions tab -> `build-windows` -> download the
`ViperEngine-v0.1-windows` artifact. It runs the logic tests first, then packages `viper.exe` + `scenes/` + `scripts/`.

**B) Locally:** install CMake and Visual Studio Build Tools (or MinGW-w64), then run `build_windows.bat`.
CMake downloads and builds GLFW itself; `viper.exe` is statically linked (single file, no DLLs).

Run `viper.exe` from the folder that contains `scenes/` and `scripts/`. Optional: `viper.exe path\to\other.vscene`.

## Controls
**Edit mode:** hold RMB + WASD/QE fly (Shift = fast) | TAB select | arrows move X/Z, PgUp/PgDn move Y (0.5 steps) |
R rotate 15 deg | 1 add cube | 2 add sphere | DELETE remove | F2 save scene | F5 Play | ESC quit.
**Play mode:** WASD move, Shift run, SPACE jump, mouse turns, R respawn (from `Player.vblocks`) | F5 or ESC = Stop
(scene is restored to how it was before Play).

## Viper Blocks (scripts/*.vblocks)
This text format is the serialized block graph; it is compiled to bytecode and run by a real VM (`src/blocks`).
The future visual editor reads/writes the same file.
```
on start | on update | on key_pressed <KEY>        events
if key_down K | key_pressed K | grounded | gt A B | lt A B | eq A B  (prefix with 'not'),  else, end
repeat N ... end
walk FWD RIGHT SPEED      (relative to facing; diagonals are normalised)   turn DEG_PER_SEC   mouse_look SENS
jump FORCE                set_pos X Y Z   set_color R G B   print TEXT
set_var NAME VALUE   change_var NAME VALUE   (use $name anywhere a number is expected)
```
Attach with `script Player` in a `.vscene` entity. `controller 1` + `body dynamic` makes an entity walkable;
`follow DIST HEIGHT` makes the game camera follow it.

## Modules (src/)
| folder | role | depends on |
|---|---|---|
| core | math, logging | - |
| scene | entities, `.vscene` load/save | core |
| physics | gravity, AABB collision | scene |
| blocks | Viper Blocks compiler + VM | scene, platform/input |
| platform | window + input (GLFW lives ONLY here) | - |
| gfx | GL loader, shaders, meshes | scene |
| engine | editor/play loop wiring it all | everything |

`tests/test_core.c` is a headless test of scene + blocks + physics (no GPU needed): `viper_test`.

## Honest status of v0.1
Verified here (Linux): all logic modules + 15 headless tests pass; the whole engine compiles warning-free and a
scripted Edit->Play->Stop session runs against a fake GLFW. NOT verified: real window/OpenGL rendering, the CMake
download of GLFW, and the Windows build - I had no GPU, Windows toolchain, or network. Expect small first-run fixes.

Not in v0.1 (next milestones): GUI panels (hierarchy/inspector/block editor - currently keyboard + text files),
PBR/shadows/glTF, sphere/capsule/mesh colliders + raycasts, rotation-aware physics, audio, animation, UI, templates,
export tooling.
