# C ↔ Python parity roadmap

The Python engine (`Downloads/Viper/Viper`, see its `AGENTS.md`) is the prototype/spec.
This C engine is the professional rewrite. Status below tracks what is ported and what is next.

## Feature parity table

| Python prototype feature | C status |
|---|---|
| 3D scene entities (transform/color/scale, checker floor) | ✅ v0.1 (`scene`) |
| Editor fly-cam + grid move/rotate + add/delete primitives | ✅ v0.1 (keyboard-only) |
| Run-your-scripts VM (Scratch-style blocks → bytecode, events, vars, walk/jump/turn) | ✅ v0.1 (`blocks`) |
| Physics: gravity, AABB, player controller | ✅ v0.1 (first pass) |
| Edit/Play/Stop with scene restore | ✅ v0.1 |
| Project management (make/open projects) | ✅ v0.2 (project browser GUI) |
| "Compile" a project (standalone export) | ✅ v0.2 (exports/<name>/ + run.bat) |
| OBJ mesh loading (v, f with fan triangulation, vn normals) | ✅ v0.3 (`assets.c`, `mesh obj` in .vscene) |
| PNG/JPG texture loading (stb_image) | ✅ v0.3 (`assets_load_texture`, ready to bind) |
| Merged static world (2 draw calls) + frustum culling | ✅ v0.3 (`renderer_batch_*`, Gribb-Hartmann plane culling) |
| Transform gizmo (move/scale/rotate), multi-select, undo | ✅ v0.4 (`gizmo` overlay, `Gz`, snapshot undo) |
| Inspector/hierarchy GUI panels (ImGui) | ✅ v0.3 (`gui_editor_frame`) |
| Main menu: project list w/ paths + counts, create/open/compile | ✅ v0.3 (project browser) |
| Baked lightmap (top-down, rotate+sweep, AO, denoise) | ✅ v0.4 (`bake.c`, background job, `.lightmap` sidecar) |
| Baked occlusion culling (3D PVS, eye-position sampling) | ✅ v0.4 (cell grid + segment/AABB, `.pvs`, used in play) |
| 3D lightprobe grid (bounce lighting) | ✅ v0.4 (`.probes`, trilinear in shader) |
| First-person play mode (play.py: eye 1.6, WASD/run/jump, LMB/E, HUD) | ✅ v0.4 (`play.c` logic in `engine.c`, `gui_hud_frame`) |
| Player health / damage / respawn / hit-scan / interact hint | ✅ v0.4 (fall damage, `damage_player` block, `ray_aabb` pick) |
| Scripts: on_start / on_update / key / **on interact** | ✅ v0.4 (`blocks` + gameplay hooks) |
| Compiled game starts in first person (`viper.exe game.vscene --play`) | ✅ v0.4 (menu Compile → `run.bat --play`) |
| Code editor tab (syntax, find/replace, assign-to-unit) | 🟡 v0.4 (list + editor + assign; highlighting pending) |
| Marketplace (catalog, cards, offline cache) | 🟡 v0.4 (local `market/` cards + install; network pending) |
| Threading (physics threads, render threads, governors) | 🟡 v0.4 (bakers run on a worker thread) |
| Bench/governor tooling (auto-res, GL timer queries) | ⬜ planned v0.5 |

## Play mode parity (`play.py` → C)

| play.py | C (v0.4) |
|---|---|
| `EYE = 1.6`, pos = feet | camera at `pos.y - half.y + 1.6` |
| mouse look (0.0025 rad/px, pitch ±1.5) | `engine.c: play_update` |
| WASD 4.5 / Shift 8, SPACE jump 7.5, gravity 22 | `pl_move`, substeps 1/120, step-up 0.45 |
| LMB shoot / E interact | LMB hit-scan pushes dynamic bodies; E runs `on interact` of the looked-at scripted unit |
| health 100, damage, 4 s respawn | `play_damage`, `play_respawn`, fall damage |
| HUD: fps line, messages, `[E] interact`, hud_left/right, crosshair | `GuiHud` + `gui_hud_frame` (ImGui foreground draw list) |
| baked lightmap/probes/occlusion applied | shader samples `u_lm`/`u_pr`; static entities skipped per PVS row |
| F fog toggle, ESC quit | same (ESC returns to the editor, or quits a compiled game) |
| dynamic bodies driven by physics | `physics_update` + player passed as a static obstacle |
| scripts `on_start/on_update/on_interact` | `.vblocks` handlers, `damage_player` / `message` / `hud_left` / `hud_right` blocks |

## How to release a version

- Tag: `git tag v0.3` + `git push origin v0.3` → `release` workflow builds + publishes the zip.
- Or Actions tab → `release` → Run workflow → type the version.
- Dev builds: every push to `main` produces a `build-windows` artifact.
