# Viper Engine v0.2 (C + GLFW + OpenGL 3.3)

3D-only, modular game engine. Create a 3D scene, add a player and floor, drive the player with Viper Blocks, press Play.
v0.1 was the Milestone-1 vertical slice; v0.2 adds textures, an uncapped frame rate and list-based script/texture pickers.

## Get the .exe
**A) GitHub (no installs):** push this folder to a GitHub repo -> Actions tab -> `build-windows` -> download the
`ViperEngine-v0.2-windows` artifact. It runs the logic tests first, then packages `viper.exe` + `scenes/` + `scripts/` + `textures/`.

**B) Locally:** install CMake and Visual Studio Build Tools (or MinGW-w64), then run `build_windows.bat`.
CMake downloads and builds GLFW itself; `viper.exe` is statically linked (single file, no DLLs).

Run `viper.exe` from the folder that contains `scenes/`, `scripts/` and `textures/`. Optional: `viper.exe path\to\other.vscene`.

## Controls
**Edit mode:** hold RMB + WASD/QE fly (Shift = fast) | TAB select | arrows move X/Z, PgUp/PgDn move Y (0.5 steps) |
R rotate 15 deg | 1 add cube | 2 add sphere | DELETE remove | **T texture picker | P script picker | - / = halve/double
texture tiling** | F2 save scene | F3 toggle vsync | F5 Play | ESC quit.
**Pickers (T / P):** a list pops up for the selected object. Up/Down/PgUp/PgDn/Home/End + Enter, or hover + click, mouse
wheel scrolls, Esc (or clicking outside) closes. The current choice is marked "assigned"; `(none)` clears it. The script list is
built from `scripts/*.vblocks`, so new script files appear automatically. Picking a texture sets the object's colour to white
(colour multiplies the texture). F2 saves the choice into the .vscene.

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
Attach with the P picker in the editor, or `script Player` in a `.vscene` entity. `controller 1` + `body dynamic` makes an entity walkable;
`follow DIST HEIGHT` makes the game camera follow it.

## Textures
`texture NAME` and `tile F` in a `.vscene` entity (or the T picker; `-`/`=` change tiling). Tiling is per world unit and follows
the object's scale, so a 60x60 floor repeats instead of stretching. Built-in procedural textures: grass, dirt, stone, brick,
wood, sand, metal, tiles, checker. Your own: drop `.png` / `.bmp` / `.tga` into `textures/` (PNG must be non-interlaced; no
JPEG). A file named like a built-in replaces it. The decoders are written in-engine, no extra libraries.

## Frame rate
Uncapped by default (`glfwSwapInterval(0)`); the window title and the editor HUD show the live fps. F3 switches vsync on/off.
Physics uses a clamped delta time with sub-stepping, so very high fps does not change game behaviour.

## Modules (src/)
| folder | role | depends on |
|---|---|---|
| core | math, logging | - |
| scene | entities, `.vscene` load/save | core |
| physics | gravity, AABB collision | scene |
| blocks | Viper Blocks compiler + VM | scene, platform/input |
| platform | window + input (GLFW lives ONLY here) | - |
| gfx | GL loader, shaders, meshes, textures, image decoders, 2D overlay + bitmap font | scene |
| engine | editor/play loop, picker menus | everything |

`tests/test_core.c` is a headless test of scene + blocks + physics + image decoding + menus (no GPU needed): `viper_test`.
It reads `tests/data/` (PNG/BMP/TGA fixtures with reference pixels) and must be run from the project root.

## Honest status of v0.2
Verified here (Linux, no GPU/window): 60+ headless tests pass (also under AddressSanitizer/UBSan), PNG/BMP/TGA decoders are pixel-exact
against reference encoders and survived 3,200 corrupted-file fuzz runs, the whole engine compiles warning-free, and a scripted
Edit -> pick texture -> pick script -> Esc -> Play session runs against a fake GLFW. The 2D overlay's vertex data was rasterised on
the CPU to check the layout. NOT verified: real OpenGL output (textured shading, text and menu drawing on a real driver), the CMake
GLFW download and the Windows build. Expect small first-run fixes, especially in the new shaders.

Not yet: GUI panels (hierarchy / inspector / block editor), PBR/shadows/glTF, sphere/capsule/mesh colliders + raycasts,
rotation-aware physics, audio, animation, in-game UI, templates, export tooling, click-to-select in the viewport.
