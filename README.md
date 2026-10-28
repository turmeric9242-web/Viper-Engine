# Viper Engine v0.4 (C + GLFW + OpenGL 3.3)

3D-only, modular game engine. The Python prototype in `Downloads/Viper/Viper` is the parity spec; this C
engine is the real thing. v0.4 = editor shell (Import/Map/Bake OC/Lighting/Scripts/Marketplace), baked
lightmaps + PVS occlusion + light probes, and a **first-person play mode that behaves like `play.py`**.

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
`exports/<name>/` (engine exe + project + `assets/` + `market/` + `run.bat`). `run.bat` launches
`viper.exe <name>.vscene --play`, so a compiled game boots straight into first person - no editor.
`viper.exe path\to\scene.vscene` still opens the editor for development.

## Controls
**Edit mode:** hold RMB + WASD/QE fly (Shift = fast) | TAB select | arrows move X/Z, PgUp/PgDn move Y | R rotate |
1/2/3 gizmo mode (move/scale/rotate) | F1-F6 tabs | Ctrl+Z/Y undo/redo | F2 save | F5 Play | M project browser | ESC quit.
**Play mode (first person, same numbers as `play.py`):** WASD walk 4.5 / Shift run 8.0 | mouse look | SPACE jump 7.5 |
LMB hit-scan (pushes crates) | E interact with the scripted unit you look at | F fog | fall damage + 4 s respawn.
HUD: fps/PVS line, messages, `[E] interact`, `hud_left`/`hud_right`, health bar, crosshair. F5 or ESC stops
the editor play session; in a compiled game ESC quits.

## Vipers Blocks gameplay API (scripts/*.vblocks)
`on interact` runs when the player presses **E** while looking at that entity (within 4 m). Gameplay actions
available to scripts:
```
damage_player N          message TEXT
hud_left TEXT            hud_right TEXT
```
The VM calls back into the play mode through `BlocksGameApi` (`src/blocks/blocks.h`), so the headless logic
tests still build without the engine.

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

## Honest status of v0.4
`build-windows` (GitHub Actions) is the compile check - there is no local Windows toolchain. The editor shell,
baked lightmaps/PVS/probes, background bake job, first-person play mode and the `--play` standalone entry all
build there. Still approximate vs the Python prototype: lightmaps/probes are top-down heightfield shelters,
PVS uses entity AABBs as occluders, physics runs on one thread, and audio/animation are absent.

## v0.4
- Editor shell matching `editor.py`: 6 tabs (Import / Map / Bake OC / Lighting / Scripts / Marketplace),
  assets + scene lists, per-tab inspector, status bar, viewport toolbar/overlay, gizmo, multi-select, undo/redo.
- Baking: occlusion (3D PVS), lightmap (sun + AO + denoise), 3D probes - all on a worker thread with progress,
  cancel and sidecar files (`<scene>.pvs`, `.lightmap`, `.probes`) that reload when the project opens.
- Play mode (`play.py` parity): first-person camera at EYE 1.6, mouse look, WASD 4.5 / Shift 8, jump 7.5,
  step-up 0.45, gravity 22, baked lighting/probes/PVS in the shader, dynamic bodies, interact raycast, health,
  fall damage, respawn, HUD and crosshair, F fog.
- Compiled game: `menu -> Compile project` copies exe + project + assets and writes `run.bat` that starts
  `viper.exe <name>.vscene --play` in first person.
- Still open (see `PARITY.md`): script editor syntax highlighting / find-replace, marketplace network downloads,
  auto-res governor + GL timer queries + bench overlay, audio.
