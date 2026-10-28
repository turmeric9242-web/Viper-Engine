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
| OBJ/MTL + texture loading, texture atlas | ⬜ planned v0.3 |
| Merged static world (2 draw calls) + frustum culling | ⬜ planned v0.3+ |
| Transform gizmo (move/scale/rotate), multi-select, undo | ⬜ planned v0.3 |
| Inspector/hierarchy GUI panels (ImGui) | ⬜ planned v0.3–0.4 |
| Baked lightmap (top-down, rotate+sweep, AO, denoise) | ⬜ planned v0.4 |
| Baked occlusion culling (3D PVS, eye-position sampling) | ⬜ planned v0.4–0.5 |
| 3D lightprobe grid (bounce lighting) | ⬜ planned v0.5 |
| Code editor tab (syntax, find/replace, assign-to-unit) | ⬜ planned v0.5 |
| Marketplace (catalog, cards, offline cache) | ⬜ planned later |
| Threading (physics threads, render threads, governors) | ⬜ planned later |
| Bench/governor tooling (auto-res, GL timer queries) | ⬜ planned later |

## How to release a version

- Tag: `git tag v0.3` + `git push origin v0.3` → `release` workflow builds + publishes the zip.
- Or Actions tab → `release` → Run workflow → type the version.
- Dev builds: every push to `main` produces a `build-windows` artifact.
