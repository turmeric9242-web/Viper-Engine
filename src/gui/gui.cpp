#include "gui/gui.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <stdio.h>
#include <string.h>

static int g_active;
static ImFont *g_mono;
static ImFont *g_hud_font;

/* editor.py layout constants */
#define TAB_H 34
#define LEFT_W 260
#define RIGHT_W 330
#define STATUS_H 24

static const ImGuiWindowFlags PANEL_FLAGS =
    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus |
    ImGuiWindowFlags_NoSavedSettings;
static const ImGuiWindowFlags CHROME_FLAGS = PANEL_FLAGS | ImGuiWindowFlags_NoScrollbar;

static const char *TABS[6] = {"Import", "Map", "Bake OC", "Lighting", "Scripts", "Marketplace"};

static const ImVec4 C_ACCENT(0.25f, 0.45f, 0.85f, 1.0f);
static const ImVec4 C_GOOD(0.40f, 0.85f, 0.50f, 1.0f);
static const ImVec4 C_BAD(0.92f, 0.40f, 0.34f, 1.0f);
static const ImVec4 C_DIM(0.58f, 0.61f, 0.66f, 1.0f);
static const ImVec4 C_WARN(0.95f, 0.78f, 0.30f, 1.0f);

/* ---------------------------------------------------------------- helpers */

static bool group_has(const GuiEditor *ed, int i) {
    if (i < 0 || i >= MAX_ENTITIES) return 0;
    return (ed->group[i >> 5] >> (i & 31)) & 1u;
}
static void group_set(GuiEditor *ed, int i, int on) {
    if (i < 0 || i >= MAX_ENTITIES) return;
    if (on) ed->group[i >> 5] |= 1u << (i & 31);
    else ed->group[i >> 5] &= ~(1u << (i & 31));
}
static void group_clear(GuiEditor *ed) { memset(ed->group, 0, sizeof ed->group); }

static void mark_undo(GuiEditor *ed) {
    if (ImGui::IsItemActivated()) ed->push_undo = 1;
}

static bool drag1(GuiEditor *ed, const char *label, float *v, float speed) {
    bool ch = ImGui::DragFloat(label, v, speed);
    mark_undo(ed);
    return ch;
}
static bool drag3(GuiEditor *ed, const char *label, float *v, float speed) {
    bool ch = ImGui::DragFloat3(label, v, speed);
    mark_undo(ed);
    return ch;
}
static bool text1(GuiEditor *ed, const char *label, char *buf, size_t n) {
    bool ch = ImGui::InputText(label, buf, n);
    mark_undo(ed);
    return ch;
}
static bool check1(GuiEditor *ed, const char *label, int *v) {
    bool b = *v != 0;
    bool ch = ImGui::Checkbox(label, &b);
    if (ch) { *v = b ? 1 : 0; ed->push_undo = 1; }
    return ch;
}

static void section(const char *t) {
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("%s", t);
    ImGui::Spacing();
}

static void hint(const char *t) { ImGui::TextColored(C_DIM, "%s", t); }

/* ------------------------------------------------------------- tab bar */

static void draw_tabs(GuiEditor *ed, int W) {
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2((float)W, TAB_H), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 3));
    ImGui::Begin("##tabbar", nullptr, CHROME_FLAGS);
    float tw = ((float)W - 8.0f - 5.0f * 4.0f) / 6.0f;
    if (tw > 170.0f) tw = 170.0f;
    if (tw < 40.0f) tw = 40.0f;
    for (int i = 0; i < 6; i++) {
        if (i) ImGui::SameLine(0, 4);
        bool active = ed->tab == i;
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, C_ACCENT);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.50f, 0.90f, 1.0f));
        }
        if (ImGui::Button(TABS[i], ImVec2(tw, TAB_H - 10.0f))) ed->tab = i;
        if (active) ImGui::PopStyleColor(2);
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

/* --------------------------------------------------------- left column */

static void assets_list(GuiEditor *ed) {
    ImGui::BeginChild("##assets", ImVec2(0, 0), false);
    if (ed->nassets == 0) ImGui::TextDisabled("no assets - import an .obj");
    for (int i = 0; i < ed->nassets; i++) {
        bool sel = ed->asset_sel == i;
        if (ImGui::Selectable(ed->assets[i], sel)) {
            ed->asset_sel = i;
            ed->mode = 1; /* palette click arms place mode, like editor.py */
        }
    }
    ImGui::EndChild();
}

static void scene_list(GuiEditor *ed) {
    ImGui::BeginChild("##scene", ImVec2(0, 0), false);
    int shown = 0;
    for (int i = 0; i < ed->scene->count; i++) {
        Entity *e = &ed->scene->e[i];
        if (!e->active) continue;
        shown++;
        const char *tag = e->body == BODY_STATIC ? "S" : e->body == BODY_DYNAMIC ? "D" : "-";
        const char *mark = e->script[0] ? "*" : " ";
        char label[96];
        snprintf(label, sizeof label, "%s%s %-18s##e%d", tag, mark, e->name, i);
        bool sel = group_has(ed, i) || ed->selected == i;
        if (ImGui::Selectable(label, sel)) {
            ImGuiIO &io = ImGui::GetIO();
            if (io.KeyCtrl) {
                group_set(ed, i, !group_has(ed, i));
                ed->selected = i;
            } else if (io.KeyShift) {
                group_set(ed, i, 1);
                ed->selected = i;
            } else {
                group_clear(ed);
                group_set(ed, i, 1);
                ed->selected = i;
            }
        }
    }
    if (!shown) ImGui::TextDisabled("empty scene");
    ImGui::EndChild();
}

static void draw_left(GuiEditor *ed, int W, int H) {
    (void)W;
    ImGui::SetNextWindowPos(ImVec2(0, TAB_H), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(LEFT_W, (float)(H - TAB_H - STATUS_H)), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(1.0f);
    ImGui::Begin("##left", nullptr, PANEL_FLAGS);

    if (ed->tab == 4) { /* scripts: file list */
        ImGui::Text("Scripts (%d)", ed->nscripts);
        ImGui::Separator();
        ImGui::BeginChild("##scripts", ImVec2(0, 0), false);
        for (int i = 0; i < ed->nscripts; i++) {
            bool sel = ed->script_sel == i;
            char label[96];
            snprintf(label, sizeof label, "%s##s%d", ed->scripts[i], i);
            if (ImGui::Selectable(label, sel)) {
                ed->script_sel = i;
                ed->script_load_req = 1;
            }
        }
        if (!ed->nscripts) ImGui::TextDisabled("no scripts/ folder");
        ImGui::EndChild();
        ImGui::End();
        return;
    }
    if (ed->tab == 5) { /* marketplace: sources */
        ImGui::Text("Sources (%d)", ed->market_nsources);
        ImGui::Separator();
        for (int i = 0; i < ed->market_nsources; i++) {
            bool sel = ed->market_source_sel == i;
            char label[200];
            snprintf(label, sizeof label, "%s##ms%d", ed->market_sources[i], i);
            if (ImGui::Selectable(label, sel)) ed->market_source_sel = i;
        }
        ImGui::Spacing();
        if (ImGui::Button("Refresh catalog", ImVec2(-1, 0))) ed->market_refresh_pressed = 1;
        ImGui::Spacing();
        hint("Catalogs are folders or URLs with a catalog.json. The bundled market/ folder always works offline.");
        if (ed->market_status[0]) {
            ImGui::Spacing();
            ImGui::TextWrapped("%s", ed->market_status);
        }
        ImGui::End();
        return;
    }

    /* assets / scene toggle (all other tabs) */
    float hw = (LEFT_W - 4.0f) * 0.5f;
    static int left_mode = 0; /* 0 = assets, 1 = scene (Map tab state) */
    if (ImGui::Button("Assets", ImVec2(hw, 22))) left_mode = 0;
    ImGui::SameLine(0, 4);
    if (ImGui::Button("Scene", ImVec2(hw, 22))) left_mode = 1;
    if (ed->tab == 0) left_mode = 0;
    ImGui::Spacing();
    if (left_mode == 1 && ed->tab != 0) scene_list(ed);
    else assets_list(ed);
    ImGui::End();
}

/* -------------------------------------------------------- right column */

static void unit_props(GuiEditor *ed) {
    if (ed->selected < 0 || ed->selected >= ed->scene->count ||
        !ed->scene->e[ed->selected].active) {
        ImGui::TextDisabled("no selection");
        hint("click a unit in the viewport or the scene list");
        return;
    }
    Entity *e = &ed->scene->e[ed->selected];
    ImGui::TextColored(C_ACCENT, "%s", e->name);
    ImGui::PushItemWidth(-96);
    text1(ed, "name", e->name, sizeof e->name);
    drag3(ed, "pos", &e->pos.x, 0.1f);
    drag3(ed, "rot", &e->rot.x, 1.0f);
    drag3(ed, "scale", &e->scale.x, 0.05f);
    if (ImGui::ColorEdit3("color", &e->color.x)) ed->push_undo = 1;
    {
        int mesh = (int)e->mesh;
        if (ImGui::Combo("mesh", &mesh, "none\0cube\0sphere\0obj\0")) { e->mesh = (MeshType)mesh; ed->push_undo = 1; }
        int body = (int)e->body;
        if (ImGui::Combo("body", &body, "none\0static\0dynamic\0")) { e->body = (BodyType)body; ed->push_undo = 1; }
    }
    check1(ed, "checker", &e->checker);
    check1(ed, "controller", &e->controller);
    text1(ed, "script", e->script, sizeof e->script);
    drag1(ed, "follow dist", &e->follow_dist, 0.1f);
    drag1(ed, "follow height", &e->follow_height, 0.1f);
    if (e->mesh == MESH_OBJ) {
        text1(ed, "mesh path", e->mesh_path, sizeof e->mesh_path);
        text1(ed, "tex path", e->tex_path, sizeof e->tex_path);
    }
    ImGui::PopItemWidth();
    ImGui::Spacing();
    if (ImGui::Button("Duplicate", ImVec2(100, 0))) ed->duplicate_pressed = 1;
    ImGui::SameLine();
    if (ImGui::Button("Delete", ImVec2(100, 0))) ed->delete_pressed = 1;
}

static void panel_import(GuiEditor *ed) {
    ImGui::TextWrapped("Import .obj models with their .mtl and textures. Imported files are copied into the project's assets/ folder and show up in the palette.");
    ImGui::Spacing();
    if (ImGui::Button("Import .obj...", ImVec2(-1, 32))) ed->import_pressed = 1;
    ImGui::Spacing();
    hint("Assets are looked up relative to the current working directory.");
    section("Log");
    ImGui::TextWrapped("%s", ed->import_log[0] ? ed->import_log : "nothing imported yet");
}

static void panel_map(GuiEditor *ed) {
    Scene *s = ed->scene;
    ImGui::PushItemWidth(-120);
    section("Sun / ambient");
    drag1(ed, "azimuth", &s->sun_az, 1.0f);
    drag1(ed, "elevation", &s->sun_el, 1.0f);
    drag1(ed, "ambient", &s->ambient, 0.01f);
    ImGui::PopItemWidth();
    section("Selected unit");
    unit_props(ed);
    ImGui::Spacing();
    {
        int n = 0;
        for (int i = 0; i < MAX_ENTITIES; i++) if (group_has(ed, i)) n++;
        ImGui::TextDisabled("%d unit(s) selected", n);
    }
}

static void job_widget(GuiEditor *ed) {
    if (ed->job_running) {
        ImGui::ProgressBar(ed->job_p < 0 ? 0.5f : ed->job_p, ImVec2(-1, 0), ed->job_name);
        if (ImGui::Button("Cancel", ImVec2(-1, 0))) ed->cancel_pressed = 1;
    }
    if (ed->job_error[0]) ImGui::TextColored(C_BAD, "%s", ed->job_error);
    if (ed->job_text[0] && !ed->job_running) ImGui::TextColored(C_GOOD, "%s", ed->job_text);
}

static void panel_oc(GuiEditor *ed) {
    Scene *s = ed->scene;
    ImGui::TextWrapped("Baked occlusion culling: cells are sampled from eye height, static units are tested with rays. Runtime matches editor.py's PVS grid.");
    ImGui::PushItemWidth(-120);
    section("Settings");
    drag1(ed, "cell size", &s->oc_cell, 0.1f);
    drag1(ed, "cell height", &s->oc_cell_y, 0.1f);
    {
        int q = s->oc_quality;
        if (ImGui::SliderInt("quality", &q, 1, 4)) { s->oc_quality = q; ed->push_undo = 1; }
    }
    ImGui::PopItemWidth();
    ImGui::Spacing();
    if (!ed->has_oc) ImGui::TextColored(C_WARN, "not baked yet");
    else if (ed->oc_stale) ImGui::TextColored(C_WARN, "stale - geometry changed, re-bake");
    else ImGui::TextColored(C_GOOD, "baked and up to date");
    ImGui::Spacing();
    if (ImGui::Button("Bake occlusion", ImVec2(-1, 32))) ed->bake_oc_pressed = 1;
    check1(ed, "preview visible cells", &s->preview_oc);
    ImGui::Spacing();
    job_widget(ed);
}

static void panel_light(GuiEditor *ed) {
    Scene *s = ed->scene;
    ImGui::TextWrapped("Baked lightmap: top-down heightfield with a shadow sweep, ambient occlusion and a denoise pass. Sun colour comes from azimuth/elevation.");
    ImGui::PushItemWidth(-120);
    section("Sun");
    drag1(ed, "azimuth", &s->sun_az, 1.0f);
    drag1(ed, "elevation", &s->sun_el, 1.0f);
    drag1(ed, "ambient", &s->ambient, 0.01f);
    section("Lightmap");
    {
        int r = s->lm_res;
        if (ImGui::SliderInt("resolution", &r, 64, 1024)) { s->lm_res = r; ed->push_undo = 1; }
        int n = s->lm_samples;
        if (ImGui::SliderInt("samples", &n, 1, 32)) { s->lm_samples = n; ed->push_undo = 1; }
    }
    drag1(ed, "AO strength", &s->ao_strength, 0.01f);
    drag1(ed, "AO radius", &s->ao_radius, 0.1f);
    ImGui::PopItemWidth();
    ImGui::Spacing();
    if (!ed->has_lm) ImGui::TextColored(C_WARN, "not baked yet");
    else if (ed->lm_stale) ImGui::TextColored(C_WARN, "stale - re-bake for accurate shadows");
    else ImGui::TextColored(C_GOOD, "baked and up to date");
    ImGui::Spacing();
    if (ImGui::Button("Bake lighting", ImVec2(-1, 32))) ed->bake_lm_pressed = 1;
    if (ImGui::Button("Bake light probes", ImVec2(-1, 28))) ed->bake_pr_pressed = 1;
    if (!ed->has_pr) ImGui::TextColored(C_DIM, "no light probe grid");
    else if (ed->pr_stale) ImGui::TextColored(C_WARN, "probes stale");
    check1(ed, "show baked", &s->show_baked);
    ImGui::Spacing();
    job_widget(ed);
}

static void panel_scripts(GuiEditor *ed) {
    ImGui::Text("Script");
    ImGui::InputText("##scriptname", ed->script_name, sizeof ed->script_name, ImGuiInputTextFlags_ReadOnly);
    ImGui::Spacing();
    if (ImGui::Button("New script", ImVec2(-1, 0))) ed->script_new_pressed = 1;
    if (ImGui::Button("Save (Ctrl+S)", ImVec2(-1, 0))) ed->script_save_pressed = 1;
    if (ImGui::Button("Assign to selection", ImVec2(-1, 0))) ed->assign_script_pressed = 1;
    if (ed->script_saved_flash) ImGui::TextColored(C_GOOD, "saved");
    if (ed->script_dirty) ImGui::TextColored(C_WARN, "unsaved changes");
    section("Format");
    ImGui::TextWrapped("Viper Blocks text format: one instruction per line. Comments start with #. See scripts/ for examples.");
    ImGui::Spacing();
    hint("Ctrl+S saves the open script; the engine reloads it on the next F5 play.");
}

static void panel_market(GuiEditor *ed) {
    ImGui::Text("Marketplace");
    ImGui::Separator();
    if (ed->market_sel >= 0 && ed->market_sel < ed->nmarket) {
        ImGui::TextColored(C_ACCENT, "%s", ed->market_names[ed->market_sel]);
        ImGui::TextWrapped("%s", ed->market_desc[ed->market_sel]);
        ImGui::Spacing();
        if (ImGui::Button("Download", ImVec2(-1, 30))) ed->market_get_pressed = 1;
        ImGui::Spacing();
        hint(ed->market_files[ed->market_sel][0] ? ed->market_files[ed->market_sel]
                                                 : "not downloaded yet");
    } else {
        ImGui::TextDisabled("pick a pack on the cards page");
    }
    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("Add source folder...", ImVec2(-1, 0))) ed->market_add_source_pressed = 1;
    if (ImGui::Button("Refresh", ImVec2(-1, 0))) ed->market_refresh_pressed = 1;
    if (ed->market_busy) ImGui::TextColored(C_WARN, "downloading...");
    if (ed->market_status[0]) { ImGui::Spacing(); ImGui::TextWrapped("%s", ed->market_status); }
}

static void draw_right(GuiEditor *ed, int W, int H) {
    ImGui::SetNextWindowPos(ImVec2((float)(W - RIGHT_W), TAB_H), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(RIGHT_W, (float)(H - TAB_H - STATUS_H)), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(1.0f);
    ImGui::Begin("##right", nullptr, PANEL_FLAGS);
    switch (ed->tab) {
        case 0: panel_import(ed); break;
        case 1: panel_map(ed); break;
        case 2: panel_oc(ed); break;
        case 3: panel_light(ed); break;
        case 4: panel_scripts(ed); break;
        case 5: panel_market(ed); break;
        default: break;
    }
    ImGui::End();
}

/* ------------------------------------------------------ viewport extras */

static void draw_toolbar(GuiEditor *ed) {
    ImGui::SetNextWindowPos(ImVec2(LEFT_W + 8.0f, TAB_H + 8.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.82f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
    ImGui::Begin("##toolbar", nullptr, CHROME_FLAGS | ImGuiWindowFlags_AlwaysAutoResize |
                                        ImGuiWindowFlags_NoFocusOnAppearing);
    const char *names[8] = {"Select", "Place", "Move", "Scale", "Rotate", "Dup", "Del", "Focus"};
    for (int i = 0; i < 8; i++) {
        if (i) ImGui::SameLine(0, 3);
        bool active = (i == 0 && ed->mode == 0) || (i == 1 && ed->mode == 1) ||
                      (i == 2 && ed->mode == 0 && ed->gizmo_mode == 0) ||
                      (i == 3 && ed->mode == 0 && ed->gizmo_mode == 1) ||
                      (i == 4 && ed->mode == 0 && ed->gizmo_mode == 2);
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, C_ACCENT);
        if (ImGui::Button(names[i], ImVec2(56, 22))) {
            switch (i) {
                case 0: ed->mode = 0; break;
                case 1: ed->mode = 1; break;
                case 2: ed->mode = 0; ed->gizmo_mode = 0; break;
                case 3: ed->mode = 0; ed->gizmo_mode = 1; break;
                case 4: ed->mode = 0; ed->gizmo_mode = 2; break;
                case 5: ed->duplicate_pressed = 1; break;
                case 6: ed->delete_pressed = 1; break;
                case 7: ed->focus_pressed = 1; break;
            }
        }
        if (active) ImGui::PopStyleColor();
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

static void draw_overlay(GuiEditor *ed, int W, int H) {
    ImDrawList *dl = ImGui::GetBackgroundDrawList();
    char line[256];
    const char *pal = (ed->asset_sel >= 0 && ed->asset_sel < ed->nassets) ? ed->assets[ed->asset_sel] : "-";
    snprintf(line, sizeof line, "[%s]  %s   rot %.0f  scale %.2f  layer y=%.2f",
             ed->mode ? "PLACE" : "SELECT", pal, ed->place_rot, ed->place_scale, ed->layer);
    dl->AddText(ImVec2((float)LEFT_W + 10.0f, (float)(TAB_H + 40)), IM_COL32(235, 238, 245, 230), line);
    const char *hintline = "1 move  2 scale  3 rotate | Shift+click multi-select | Ctrl+A all | F focus | Del | C dup | G grid | Shift = free";
    dl->AddText(ImVec2((float)LEFT_W + 10.0f, (float)(TAB_H + 58)), IM_COL32(160, 168, 180, 220), hintline);
    (void)W;
    (void)H;
}

/* ---------------------------------------------------------- scripts tab */

static void draw_code_editor(GuiEditor *ed, int W, int H) {
    int x = LEFT_W, y = TAB_H, w = W - LEFT_W - RIGHT_W, h = H - TAB_H - STATUS_H;
    ImGui::SetNextWindowPos(ImVec2((float)x, (float)y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2((float)w, (float)h), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(1.0f);
    ImGui::Begin("##code", nullptr, PANEL_FLAGS);
    if (!ed->script_name[0]) {
        ImGui::TextDisabled("no script open - pick one on the left or press New script");
    } else {
        ImGui::Text("%s%s", ed->script_name, ed->script_dirty ? " *" : "");
        ImGui::Separator();
        if (g_mono) ImGui::PushFont(g_mono);
        ImGuiInputTextFlags f = ImGuiInputTextFlags_AllowTabInput;
        if (ImGui::InputTextMultiline("##codebuf", ed->script_buf, sizeof ed->script_buf,
                                      ImVec2(-1, -1), f))
            ed->script_dirty = 1;
        if (g_mono) ImGui::PopFont();
    }
    ImGui::End();
}

/* ------------------------------------------------------- marketplace tab */

static void draw_market_cards(GuiEditor *ed, int W, int H) {
    int x = LEFT_W, y = TAB_H, w = W - LEFT_W - RIGHT_W, h = H - TAB_H - STATUS_H;
    ImGui::SetNextWindowPos(ImVec2((float)x, (float)y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2((float)w, (float)h), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(1.0f);
    ImGui::Begin("##market", nullptr, PANEL_FLAGS);
    ImGui::Text("Starter catalog - %d pack(s)", ed->nmarket);
    ImGui::Separator();
    ImGui::BeginChild("##cards", ImVec2(0, 0), false);
    for (int i = 0; i < ed->nmarket; i++) {
        ImGui::PushID(i);
        ImGui::BeginGroup();
        bool sel = ed->market_sel == i;
        if (sel) ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.18f, 0.26f, 0.42f, 1.0f));
        float cw = (float)w - 40.0f;
        if (cw < 160.0f) cw = 160.0f;
        ImGui::BeginChild("card", ImVec2(cw, 92.0f), true);
        ImGui::TextColored(C_ACCENT, "%s", ed->market_names[i]);
        ImGui::TextWrapped("%s", ed->market_desc[i]);
        if (ed->market_files[i][0]) ImGui::TextColored(C_GOOD, "downloaded: %s", ed->market_files[i]);
        if (ImGui::Button("Details", ImVec2(90, 0))) ed->market_sel = i;
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) ed->market_sel = i;
        ImGui::EndChild();
        if (sel) ImGui::PopStyleColor();
        ImGui::EndGroup();
        ImGui::Spacing();
        ImGui::PopID();
    }
    if (!ed->nmarket) ImGui::TextDisabled("no catalog loaded - add a source folder on the left");
    ImGui::EndChild();
    ImGui::End();
}

/* ---------------------------------------------------------- status bar */

static void draw_status(GuiEditor *ed, int W, int H) {
    ImGui::SetNextWindowPos(ImVec2(0, (float)(H - STATUS_H)), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2((float)W, STATUS_H), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 4));
    ImGui::Begin("##status", nullptr, CHROME_FLAGS);
    if (ed->msg[0]) ImGui::TextColored(C_GOOD, "%s", ed->msg);
    else ImGui::TextColored(C_DIM, "ready");
    char info[256];
    const char *base = ed->scene_path ? ed->scene_path : "";
    const char *slash = strrchr(base, '/');
    if (slash) base = slash + 1;
    snprintf(info, sizeof info, "%d units   %s%s", ed->scene->count, base, ed->dirty ? " *" : "");
    float tw = ImGui::CalcTextSize(info).x;
    ImGui::SameLine((float)W - tw - 16.0f);
    ImGui::Text("%s", info);
    ImGui::End();
    ImGui::PopStyleVar();
}

/* --------------------------------------------------------------- frames */

extern "C" int gui_init(void *glfw_window) {
    IMGUI_CHECKVERSION();
    g_active = 1;
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.Fonts->AddFontDefault();
    g_mono = NULL;
    {
        FILE *f = fopen("C:/Windows/Fonts/consola.ttf", "rb");
        if (f) {
            fclose(f);
            g_mono = io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/consola.ttf", 15.0f);
        }
    }
    g_hud_font = NULL;
    {
        FILE *f = fopen("C:/Windows/Fonts/segoeui.ttf", "rb");
        if (f) {
            fclose(f);
            g_hud_font = io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf", 22.0f);
        }
    }
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGuiStyle &st = ImGui::GetStyle();
    st.WindowRounding = 0.0f;
    st.FrameRounding = 3.0f;
    ImGui_ImplGlfw_InitForOpenGL((GLFWwindow *)glfw_window, true);
    ImGui_ImplOpenGL3_Init("#version 130");
    return 1;
}

extern "C" void gui_shutdown(void) {
    if (!g_active) return;
    g_active = 0;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

extern "C" void gui_frame(GuiMenu *m) {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(1280, 720), ImGuiCond_Appearing);
    ImGui::SetNextWindowBgAlpha(1.0f);
    if (ImGui::Begin("mainmenu", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse)) {

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.2f, 1.0f));
        ImGui::Text("VIPER ENGINE");
        ImGui::PopStyleColor();
        ImGui::TextDisabled("project browser - pick a project, or create a new one");
        ImGui::Separator();

        ImGui::Text("Projects folder:");
        ImGui::SameLine();
        ImGui::TextWrapped("%s", m->root[0] ? m->root : "projects/");
        ImGui::SameLine();
        if (ImGui::Button("Open folder")) m->open_folder_pressed = 1;
        ImGui::SameLine();
        if (ImGui::Button("Refresh")) m->refresh_pressed = 1;
        ImGui::Spacing();

        ImGui::BeginChild("projectlist", ImVec2(0, 320), true);
        if (m->nprojects == 0)
            ImGui::TextDisabled("no projects yet - type a name below and press Create");
        for (int i = 0; i < m->nprojects; i++) {
            bool sel = strcmp(m->projsel, m->projects[i]) == 0;
            char label[96];
            if (m->counts[i] >= 0)
                snprintf(label, sizeof label, "%s  (%d entities)##%d", m->projects[i], m->counts[i], i);
            else
                snprintf(label, sizeof label, "%s  (no scene)##%d", m->projects[i], i);
            if (ImGui::Selectable(label, sel))
                snprintf(m->projsel, sizeof m->projsel, "%s", m->projects[i]);
            if (sel) {
                char full[400];
                snprintf(full, sizeof full, "%s/%s/%s.vscene",
                         m->root[0] ? m->root : "projects", m->projects[i], m->projects[i]);
                ImGui::TextDisabled("%s", full);
            }
        }
        ImGui::EndChild();
        ImGui::Spacing();

        ImGui::Separator();
        ImGui::Text("Create new project");
        ImGui::SetNextItemWidth(320);
        ImGui::InputTextWithHint("##newname", "MyGame", m->newname, sizeof m->newname);
        ImGui::SameLine();
        if (ImGui::Button("Create project")) m->new_pressed = 1;
        ImGui::TextDisabled("creates %s/<name>/<name>.vscene with a starter floor + player",
                            m->root[0] ? m->root : "projects");
        ImGui::Spacing();

        ImGui::Separator();
        ImGui::Text("Selected project");
        bool have = m->projsel[0] != 0;
        if (!have) ImGui::TextDisabled("select a project above first");
        else {
            ImGui::Text("Project: %s", m->projsel);
            if (ImGui::Button("Open in editor")) m->open_pressed = 1;
            ImGui::SameLine();
            if (ImGui::Button("Compile to standalone .exe")) m->export_pressed = 1;
        }
        ImGui::Spacing();

        ImGui::Separator();
        if (m->status[0]) ImGui::TextWrapped("%s", m->status);
        ImGui::TextDisabled("ESC quits | F1-F6 editor tabs | F5 plays");
    }
    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

/* ------------------------------------------------------------- play HUD */

static void hud_text(ImDrawList *dl, ImFont *font, float size, float x, float y,
                     ImU32 col, const char *s) {
    if (!s || !s[0]) return;
    dl->AddText(font, size, ImVec2(x + 1.5f, y + 1.5f), IM_COL32(0, 0, 0, 220), s);
    dl->AddText(font, size, ImVec2(x, y), col, s);
}
static float hud_text_w(ImFont *font, float size, const char *s) {
    if (!s || !s[0]) return 0.0f;
    return font->CalcTextSizeA(size, 100000.0f, 0.0f, s).x;
}

extern "C" void gui_hud_frame(const GuiHud *hud) {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGuiIO &io = ImGui::GetIO();
    float W = io.DisplaySize.x, H = io.DisplaySize.y;
    ImFont *font = g_hud_font ? g_hud_font : ImGui::GetFont();
    const float big = g_hud_font ? 22.0f : 15.0f;
    const float small = g_hud_font ? 17.0f : 13.0f;
    ImDrawList *dl = ImGui::GetForegroundDrawList();

    if (hud->fps[0]) hud_text(dl, font, small, 10.0f, 8.0f, IM_COL32(255, 255, 255, 235), hud->fps);

    for (int i = 0; i < hud->nmsg && i < 5; i++)
        hud_text(dl, font, big, W * 0.5f - 100.0f, 80.0f + 26.0f * i,
                 IM_COL32(255, 240, 150, 255), hud->msg[i]);

    if (hud->hint[0])
        hud_text(dl, font, big, W * 0.5f - 40.0f, H * 0.5f + 30.0f, IM_COL32(255, 255, 255, 255), hud->hint);

    if (hud->dead > 0.0f) {
        char t[96];
        snprintf(t, sizeof t, "--- YOU DIED ---  respawn in %.1fs", hud->dead);
        hud_text(dl, font, big, W * 0.5f - hud_text_w(font, big, t) * 0.5f, H * 0.42f,
                 IM_COL32(255, 80, 70, 255), t);
    }

    for (int i = 0; i < hud->nleft && i < 6; i++)
        hud_text(dl, font, big, 14.0f, H - 70.0f - 30.0f * (float)(hud->nleft - 1 - i),
                 IM_COL32(255, 185, 45, 255), hud->left[i]);
    for (int i = 0; i < hud->nright && i < 6; i++) {
        const char *s = hud->right[i];
        hud_text(dl, font, big, W - 16.0f - hud_text_w(font, big, s),
                 H - 70.0f - 30.0f * (float)(hud->nright - 1 - i), IM_COL32(255, 255, 255, 255), s);
    }

    /* health bar, bottom centre-left (kept small so it reads like a HUD, not a menu) */
    {
        float bw = 200.0f, bh = 12.0f, bx = 14.0f, by = H - 30.0f;
        float f = hud->health < 0.0f ? 0.0f : (hud->health > 100.0f ? 1.0f : hud->health / 100.0f);
        dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + bh), IM_COL32(0, 0, 0, 170), 2.0f);
        ImU32 col = f > 0.5f ? IM_COL32(90, 200, 110, 230)
                  : (f > 0.25f ? IM_COL32(230, 190, 70, 230) : IM_COL32(220, 70, 60, 235));
        dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw * f, by + bh), col, 2.0f);
        char hp[48];
        snprintf(hp, sizeof hp, "HP %d", (int)(hud->health + 0.5f));
        hud_text(dl, font, small, bx + bw + 10.0f, by - 3.0f, IM_COL32(235, 235, 235, 235), hp);
    }

    if (hud->crosshair) {
        float cx = W * 0.5f, cy = H * 0.5f;
        dl->AddLine(ImVec2(cx - 6, cy), ImVec2(cx - 2, cy), IM_COL32(255, 255, 255, 230), 1.5f);
        dl->AddLine(ImVec2(cx + 2, cy), ImVec2(cx + 6, cy), IM_COL32(255, 255, 255, 230), 1.5f);
        dl->AddLine(ImVec2(cx, cy - 6), ImVec2(cx, cy - 2), IM_COL32(255, 255, 255, 230), 1.5f);
        dl->AddLine(ImVec2(cx, cy + 2), ImVec2(cx, cy + 6), IM_COL32(255, 255, 255, 230), 1.5f);
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

extern "C" void gui_editor_frame(GuiEditor *ed) {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGuiIO &io = ImGui::GetIO();
    int W = (int)io.DisplaySize.x;
    int H = (int)io.DisplaySize.y;
    if (W < 320) W = 320;
    if (H < 240) H = 240;

    /* the 3D viewport is the centre rect; the engine renders there */
    ed->vp_x = LEFT_W;
    ed->vp_y = TAB_H;
    ed->vp_w = W - LEFT_W - RIGHT_W;
    ed->vp_h = H - TAB_H - STATUS_H;
    if (ed->vp_w < 64) ed->vp_w = 64;
    if (ed->vp_h < 64) ed->vp_h = 64;

    draw_tabs(ed, W);
    draw_left(ed, W, H);
    draw_right(ed, W, H);
    if (ed->tab == 4) draw_code_editor(ed, W, H);
    else if (ed->tab == 5) draw_market_cards(ed, W, H);
    else {
        draw_toolbar(ed);
        draw_overlay(ed, W, H);
    }
    draw_status(ed, W, H);

    ed->ui_hover = io.WantCaptureMouse ? 1 : 0;

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
