# Viper Engine v0.1 (C + GLFW + OpenGL 3.3)

3D-only, modular game engine. v0.1 = the Milestone-1 vertical slice: create a 3D scene, add a player and
floor, drive the player with Viper Blocks, press Play.

## Get the .exe
**A) GitHub Releases (easiest):** open the repo's **Releases** page and download `ViperEngine-<version>.zip` -
it contains `viper.exe` + `scenes/` + `scripts/`, ready to run. The `release` workflow publishes one when you
push a `v...` tag (or run it manually from the Actions tab).

**B) Dev builds:** Actions tab -> `build-windows` -> download the artifact (latest pushed commit; tests run
locally only now).

**C) Locally:** install CMake and Visual Studio Build Tools (or MinGW-w64), then run `build_windows.bat`.
CMake downloads and builds GLFW + Dear ImGui itself; `viper.exe` is statically linked (single file, no DLLs).

Run `viper.exe` with no arguments for the project browser GUI: make a project (starter floor + player scene),
open an existing one under `projects/`, or press **Compile project** to export a standalone game folder into
`exports/<name>/` (engine exe + scenes + scripts + `run.bat`). The old way still works:
`viper.exe path\to\scene.vscene` opens straight in the editor.

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
| gui | Dear ImGui wrapper + project browser (new/open/compile) | platform |

`tests/test_core.c` is a headless test of scene + blocks + physics (no GPU needed): `viper_test`.

## Honest status of v0.1
Verified here (Linux): all logic modules + 15 headless tests pass; the whole engine compiles warning-free and a
scripted Edit->Play->Stop session runs against a fake GLFW. NOT verified: real window/OpenGL rendering, the CMake
download of GLFW, and the Windows build - I had no GPU, Windows toolchain, or network. Expect small first-run fixes.

Not in v0.1 (next milestones): GUI panels (hierarchy/inspector/block editor - currently keyboard + text files),
PBR/shadows/glTF, sphere/capsule/mesh colliders + raycasts, rotation-aware physics, audio, animation, UI, templates,
export tooling.

## v0.2 (work in progress)
- Project browser GUI (Dear ImGui, checked in at startup or with `M` from the editor): New project (starter
  floor + player scene), Open project, and Compile project -> a runnable standalone copy under `exports/`.
- Parity target (the Python prototype, `AGENTS.md` there is the spec): still missing OBJ/MTL + texture loading,
  baked lightmaps, baked occlusion culling (3D PVS), lightprobe grids, transform gizmo + multi-select,
  marketplace, inline Viper-Blocks code editor, GPU/threads performance work.
