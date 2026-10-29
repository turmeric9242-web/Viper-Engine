#include "engine/engine.h"
#include "core/log.h"
#include "core/vmath.h"
#include "platform/window.h"
#include "platform/fs.h"
#include "gui/gui.h"
#include "gfx/renderer.h"
#include "gfx/assets.h"
#include "scene/scene.h"
#include "physics/physics.h"
#include "blocks/blocks.h"
#include "bake/bake.h"
#include "core/thread.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>

typedef enum { MODE_MENU, MODE_EDIT, MODE_PLAY } Mode;

#define UNDO_MAX 16
#define GRID_N 6
static const float GRIDS[GRID_N] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f};
static const char *BAKE_NAMES[3] = {"occlusion", "lightmap", "probes"};

/* first-person gameplay state (play.py: EYE 1.6, walk 4.5, run 8, jump 7.5) */
#define PL_EYE 1.6f
#define PL_WALK 4.5f
#define PL_RUN 8.0f
#define PL_JUMP 7.5f
#define PL_GRAVITY 22.0f
#define PL_STEP 0.45f
#define PL_MAX_HEALTH 100.0f
#define PL_MSG_MAX 5

typedef struct {
    int on, player, standalone;
    Vec3 eye, vel;
    float yaw, pitch;          /* radians */
    float health, dead_wait, time;
    float hint_t;
    int grounded, target, fog;
    float fps;
    char fps_txt[160];
    char msg[PL_MSG_MAX][160];
    float msg_t[PL_MSG_MAX];
    int nmsg;
    char left[6][96], right[6][96];
    float hud_t[12];
    int nleft, nright;
} PlayMode;

typedef struct {
    int running, kind;         /* 0 occlusion | 1 lightmap | 2 probes */
    volatile int cancel, done, ok;
    volatile float progress;
    TriSoup tris;
    Scene scene;
    VThread *thread;
    BakedOcclusion oc;
    BakedLightmap lm;
    BakedProbes pr;
    unsigned sig;
    char error[128];
} BakeJob;

typedef struct {
    int active, axis, hot;
    int n;
    int idx[MAX_ENTITIES];
    Vec3 sp[MAX_ENTITIES], ss[MAX_ENTITIES];
    float sr[MAX_ENTITIES];
    Vec3 pivot;
    float t0, ang0, px0;
} Gizmo;

static struct {
    Window *win;
    Scene scene, snapshot;
    Mode mode;
    int selected;
    unsigned group[8];         /* multi-select bits */

    /* editor state (mirrors editor.py) */
    int tab;                   /* 0 Import | 1 Map | 2 Bake OC | 3 Lighting | 4 Scripts | 5 Market */
    int emode;                 /* 0 select, 1 place */
    int gizmo_mode;            /* 0 move, 1 scale, 2 rotate */
    int grid_i;
    float layer, place_rot, place_scale;
    char msg[256];
    float msg_t;
    int dirty;
    int ui_hover;
    float mx_last, my_last;

    Vec3 cam_pos; float cam_yaw, cam_pitch, play_pitch;
    char scene_path[256];
    char argv0[480];
    GuiMenu menu;

    /* undo / redo */
    Scene undo[UNDO_MAX], redo[UNDO_MAX];
    int undo_n, redo_n;
    Scene pre;

    /* viewport + projection */
    int vx, vy, vw, vh;
    Vec3 bg;
    Mat4 vp;

    Gizmo gz;

    /* assets + baked data + scripts */
    int nassets;
    char assets[VIP_ASSET_MAX][64];
    int asset_sel;
    char lm_path[320], oc_path[320], pr_path[320];
    int has_lm, lm_stale, has_oc, oc_stale, has_pr, pr_stale;
    int nscripts;
    char scripts[VIP_SCRIPT_MAX][64];
    int script_sel;
    char script_name[64];
    char script_buf[24000];
    int script_dirty;
    char import_log[512];

    /* marketplace */
    int nmarket;
    char market_names[VIP_MARKET_MAX][64];
    char market_desc[VIP_MARKET_MAX][160];
    char market_files[VIP_MARKET_MAX][64];
    int market_sel;
    char market_status[256];

    /* play mode */
    PlayMode pl;

    /* baked data (loaded sidecars or fresh bake results) */
    BakedLightmap blm;
    BakedOcclusion boc;
    BakedProbes bpr;
    unsigned bake_sig;

    /* background bake job */
    BakeJob job;
    int job_running;
    char job_name[32];
    float job_p;
    char job_text[128];
    char job_error[128];
} G;

/* ------------------------------------------------------------------ misc */

static Vec3 cam_forward(void) {
    return v3(-sinf(G.cam_yaw) * cosf(G.cam_pitch), sinf(G.cam_pitch), -cosf(G.cam_yaw) * cosf(G.cam_pitch));
}
static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void statusf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(G.menu.status, sizeof G.menu.status, fmt, ap);
    va_end(ap);
}
static void msg(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(G.msg, sizeof G.msg, fmt, ap);
    va_end(ap);
    G.msg_t = 3.0f;
}

/* --------------------------------------------------------------- selection */

static int group_has(int i) {
    if (i < 0 || i >= MAX_ENTITIES) return 0;
    return (G.group[i >> 5] >> (i & 31)) & 1u;
}
static void group_set(int i, int on) {
    if (i < 0 || i >= MAX_ENTITIES) return;
    if (on) G.group[i >> 5] |= 1u << (i & 31);
    else G.group[i >> 5] &= ~(1u << (i & 31));
}
static void group_clear(void) { memset(G.group, 0, sizeof G.group); }
static int group_count(void) {
    int n = 0;
    for (int i = 0; i < MAX_ENTITIES; i++) if (group_has(i)) n++;
    return n;
}
static void select_only(int i) {
    group_clear();
    G.selected = i;
    if (i >= 0 && i < G.scene.count) group_set(i, 1);
}
static void select_all(void) {
    group_clear();
    for (int i = 0; i < G.scene.count; i++) if (G.scene.e[i].active) group_set(i, 1);
    if (G.selected < 0 || !group_has(G.selected))
        for (int i = 0; i < G.scene.count; i++) if (group_has(i)) { G.selected = i; break; }
}
static void select_next(void) {
    for (int k = 1; k <= G.scene.count; k++) {
        int i = (G.selected + k) % G.scene.count;
        if (G.scene.e[i].active) { select_only(i); return; }
    }
}

/* -------------------------------------------------------------- undo/redo */

static void push_undo_scene(const Scene *s) {
    if (G.undo_n == UNDO_MAX) {
        memmove(&G.undo[0], &G.undo[1], sizeof(Scene) * (UNDO_MAX - 1));
        G.undo_n--;
    }
    G.undo[G.undo_n++] = *s;
    G.redo_n = 0;
}
static void push_undo(void) { push_undo_scene(&G.scene); }

static void clamp_selection(void) {
    if (G.selected >= 0 && G.selected < G.scene.count && G.scene.e[G.selected].active) return;
    G.selected = -1;
    for (int i = 0; i < G.scene.count; i++)
        if (group_has(i) && G.scene.e[i].active) { G.selected = i; return; }
    for (int i = 0; i < G.scene.count; i++)
        if (G.scene.e[i].active) { G.selected = i; group_clear(); group_set(i, 1); return; }
    group_clear();
}
static void do_undo(void) {
    if (!G.undo_n) { msg("nothing to undo"); return; }
    if (G.redo_n == UNDO_MAX) {
        memmove(&G.redo[0], &G.redo[1], sizeof(Scene) * (UNDO_MAX - 1));
        G.redo_n--;
    }
    G.redo[G.redo_n++] = G.scene;
    G.scene = G.undo[--G.undo_n];
    blocks_bind_scene(&G.scene);
    clamp_selection();
    G.dirty = 1;
    msg("undo (%d left)", G.undo_n);
}
static void do_redo(void) {
    if (!G.redo_n) { msg("nothing to redo"); return; }
    if (G.undo_n == UNDO_MAX) {
        memmove(&G.undo[0], &G.undo[1], sizeof(Scene) * (UNDO_MAX - 1));
        G.undo_n--;
    }
    G.undo[G.undo_n++] = G.scene;
    G.scene = G.redo[--G.redo_n];
    blocks_bind_scene(&G.scene);
    clamp_selection();
    G.dirty = 1;
    msg("redo (%d left)", G.redo_n);
}

/* ------------------------------------------------------- project / assets */

static void scan_assets(void) {
    G.nassets = 0;
    snprintf(G.assets[G.nassets++], 64, "Cube");
    snprintf(G.assets[G.nassets++], 64, "Sphere");
    {
        char names[VIP_ASSET_MAX][64];
        int n = fs_list_files("assets", names, VIP_ASSET_MAX, ".obj");
        for (int i = 0; i < n && G.nassets < VIP_ASSET_MAX; i++)
            snprintf(G.assets[G.nassets++], 64, "%s", names[i]);
    }
    if (G.asset_sel >= G.nassets) G.asset_sel = 0;
}
static void scan_scripts(void) {
    int cur = G.script_sel;
    G.nscripts = fs_list_files("scripts", G.scripts, VIP_SCRIPT_MAX, ".vblocks");
    if (G.script_sel >= G.nscripts) G.script_sel = 0;
    (void)cur;
}
static void scan_market(void) {
    char dirs[VIP_MARKET_MAX][64];
    G.nmarket = fs_list_dirs("market", dirs, VIP_MARKET_MAX);
    for (int i = 0; i < G.nmarket; i++) {
        snprintf(G.market_names[i], 64, "%s", dirs[i]);
        snprintf(G.market_desc[i], 160, "Local asset pack in market/%s", dirs[i]);
        G.market_files[i][0] = 0;
        {
            char sub[160], files[16][64];
            int n;
            snprintf(sub, sizeof sub, "market/%s", dirs[i]);
            n = fs_list_files(sub, files, 16, ".obj");
            if (n > 0) snprintf(G.market_files[i], 64, "%s", files[0]);
        }
    }
    if (G.market_sel >= G.nmarket) G.market_sel = 0;
}

static void load_script(int i) {
    if (i < 0 || i >= G.nscripts) return;
    char path[200];
    snprintf(path, sizeof path, "scripts/%s", G.scripts[i]);
    FILE *f = fopen(path, "rb");
    if (!f) { msg("could not open %s", path); return; }
    size_t n = fread(G.script_buf, 1, sizeof G.script_buf - 1, f);
    fclose(f);
    G.script_buf[n] = 0;
    snprintf(G.script_name, sizeof G.script_name, "%s", G.scripts[i]);
    G.script_dirty = 0;
    msg("opened %s", G.script_name);
}
static void save_script(void) {
    if (!G.script_name[0]) { msg("no script open"); return; }
    char path[200];
    snprintf(path, sizeof path, "scripts/%s", G.script_name);
    FILE *f = fopen(path, "wb");
    if (!f) { msg("could not write %s", path); return; }
    fwrite(G.script_buf, 1, strlen(G.script_buf), f);
    fclose(f);
    G.script_dirty = 0;
    msg("saved %s", G.script_name);
}
static int file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}
static void new_script(void) {
    char path[200];
    int n = 0;
    do {
        snprintf(path, sizeof path, "scripts/script_%d.vblocks", n++);
    } while (file_exists(path));
    FILE *f = fopen(path, "wb");
    if (!f) { msg("could not create %s", path); return; }
    fprintf(f, "# new Viper Blocks script\n# see SCRIPTING_GUIDE.md for the block format\n");
    fclose(f);
    scan_scripts();
    for (int i = 0; i < G.nscripts; i++) {
        char want[64];
        snprintf(want, sizeof want, "script_%d.vblocks", n - 1);
        if (!strcmp(G.scripts[i], want)) { G.script_sel = i; load_script(i); break; }
    }
}

/* -------------------------------------------------------------- primitives */

static void set_entity_kind(Entity *e, const char *asset) {
    if (!strcmp(asset, "Cube")) { e->mesh = MESH_CUBE; e->body = BODY_STATIC; }
    else if (!strcmp(asset, "Sphere")) { e->mesh = MESH_SPHERE; e->body = BODY_DYNAMIC; }
    else {
        e->mesh = MESH_OBJ; e->body = BODY_STATIC;
        snprintf(e->mesh_path, sizeof e->mesh_path, "assets/%s", asset);
    }
    float t = (float)G.scene.count;
    e->color = v3(0.4f + 0.5f * fabsf(sinf(t)), 0.4f + 0.5f * fabsf(sinf(t * 1.7f)),
                  0.4f + 0.5f * fabsf(sinf(t * 2.3f)));
}
static const char *primary_asset(void) {
    if (G.asset_sel < 0 || G.asset_sel >= G.nassets) return "Cube";
    return G.assets[G.asset_sel];
}
static void add_primitive(const char *asset, Vec3 pos) {
    char name[32];
    const char *dot = strrchr(asset, '.');
    int len = dot ? (int)(dot - asset) : (int)strlen(asset);
    if (len > 30) len = 30;
    snprintf(name, sizeof name, "%.*s_%d", len, asset, G.scene.count);
    Entity *e = scene_add(&G.scene, name);
    if (!e) { msg("entity limit reached"); return; }
    e->pos = pos;
    e->spawn = pos;
    e->scale = v3(G.place_scale, G.place_scale, G.place_scale);
    e->rot.y = G.place_rot;
    set_entity_kind(e, asset);
    select_only((int)(e - G.scene.e));
    G.dirty = 1;
    msg("placed %s", name);
}

static void duplicate_group(void) {
    if (!group_count()) { msg("nothing selected"); return; }
    push_undo();
    int added = 0;
    for (int i = 0; i < G.scene.count && added < 64; i++) {
        if (!group_has(i) || !G.scene.e[i].active) continue;
        Entity copy = G.scene.e[i];
        Entity *e = scene_add(&G.scene, copy.name);
        if (!e) break;
        *e = copy;
        e->pos.x += GRIDS[G.grid_i];
        e->spawn = e->pos;
        added++;
    }
    group_clear();
    for (int i = G.scene.count - added; i < G.scene.count; i++) group_set(i, 1);
    if (added) G.selected = G.scene.count - added;
    G.dirty = 1;
    msg("duplicated %d unit(s)", added);
}
static void delete_group(void) {
    if (!group_count()) { msg("nothing selected"); return; }
    push_undo();
    int n = 0;
    for (int i = 0; i < G.scene.count; i++)
        if (group_has(i) && G.scene.e[i].active) { G.scene.e[i].active = 0; n++; }
    group_clear();
    G.selected = -1;
    G.dirty = 1;
    clamp_selection();
    msg("deleted %d unit(s)", n);
}
static void rotate_group(float deg) {
    if (!group_count()) return;
    push_undo();
    for (int i = 0; i < G.scene.count; i++)
        if (group_has(i) && G.scene.e[i].active) G.scene.e[i].rot.y += deg;
    G.dirty = 1;
}
static void scale_group(float f) {
    if (!group_count()) return;
    push_undo();
    for (int i = 0; i < G.scene.count; i++) {
        if (!group_has(i) || !G.scene.e[i].active) continue;
        Entity *e = &G.scene.e[i];
        e->scale = v3(fmaxf(e->scale.x * f, 0.02f), fmaxf(e->scale.y * f, 0.02f), fmaxf(e->scale.z * f, 0.02f));
    }
    G.dirty = 1;
}
static void focus_selection(void) {
    Entity *t = NULL;
    if (G.selected >= 0 && G.selected < G.scene.count && G.scene.e[G.selected].active)
        t = &G.scene.e[G.selected];
    else
        for (int i = 0; i < G.scene.count; i++)
            if (group_has(i) && G.scene.e[i].active) { t = &G.scene.e[i]; break; }
    if (!t) { msg("nothing to focus"); return; }
    float d = 3.0f * fmaxf(t->scale.x, fmaxf(t->scale.y, t->scale.z)) + 4.0f;
    G.cam_pos = v3(t->pos.x + d * 0.6f, t->pos.y + d * 0.45f, t->pos.z + d);
    G.cam_yaw = atan2f(G.cam_pos.x - t->pos.x, G.cam_pos.z - t->pos.z);
    G.cam_pitch = -atan2f(G.cam_pos.y - t->pos.y, d);
    msg("focused %s", t->name);
}

/* ----------------------------------------------------------- view/projection */

static void project_point(Vec3 p, float *sx, float *sy, int *ok) {
    const float *m = G.vp.m;
    float cx = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
    float cy = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
    float cw = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
    if (cw < 1e-4f) { *ok = 0; return; }
    float nx = cx / cw, ny = cy / cw;
    *sx = (float)G.vx + (nx * 0.5f + 0.5f) * (float)G.vw;
    *sy = (float)G.vy + (0.5f - ny * 0.5f) * (float)G.vh;
    *ok = 1;
}

static void screen_ray(float mx, float my, Vec3 *ro, Vec3 *rd) {
    float aspect = (float)G.vw / (float)(G.vh > 0 ? G.vh : 1);
    float t = tanf(DEG2RAD(60.0f) * 0.5f);
    float nx = ((mx - (float)G.vx) / (float)(G.vw > 0 ? G.vw : 1)) * 2.0f - 1.0f;
    float ny = 1.0f - ((my - (float)G.vy) / (float)(G.vh > 0 ? G.vh : 1)) * 2.0f;
    Vec3 f = cam_forward();
    Vec3 r = v3_norm(v3_cross(f, v3(0, 1, 0)));
    Vec3 u = v3_cross(r, f);
    Vec3 d = v3_add(f, v3_add(v3_mul(r, nx * t * aspect), v3_mul(u, ny * t)));
    *ro = G.cam_pos;
    *rd = v3_norm(d);
}

static int ray_aabb(Vec3 o, Vec3 d, Vec3 lo, Vec3 hi, float *tout) {
    float t0 = 0.0f, t1 = 1e30f;
    const float oo[3] = {o.x, o.y, o.z}, dd[3] = {d.x, d.y, d.z};
    const float l[3] = {lo.x, lo.y, lo.z}, h[3] = {hi.x, hi.y, hi.z};
    for (int k = 0; k < 3; k++) {
        if (fabsf(dd[k]) < 1e-8f) {
            if (oo[k] < l[k] || oo[k] > h[k]) return 0;
            continue;
        }
        float a = (l[k] - oo[k]) / dd[k], b = (h[k] - oo[k]) / dd[k];
        if (a > b) { float tmp = a; a = b; b = tmp; }
        if (a > t0) t0 = a;
        if (b < t1) t1 = b;
        if (t0 > t1) return 0;
    }
    *tout = t0;
    return 1;
}

static int entity_boxes(const Entity *e, Vec3 *lo, Vec3 *hi) {
    if (!e->active) return 0;
    Vec3 h = v3(fabsf(e->scale.x) * 0.5f, fabsf(e->scale.y) * 0.5f, fabsf(e->scale.z) * 0.5f);
    *lo = v3_sub(e->pos, h);
    *hi = v3_add(e->pos, h);
    return 1;
}

static int pick_entity(Vec3 ro, Vec3 rd) {
    int best = -1;
    float bt = 1e30f;
    for (int i = 0; i < G.scene.count; i++) {
        Vec3 lo, hi;
        float t;
        if (!entity_boxes(&G.scene.e[i], &lo, &hi)) continue;
        if (ray_aabb(ro, rd, lo, hi, &t) && t < bt) { bt = t; best = i; }
    }
    return best;
}

/* ----------------------------------------------------------------- gizmo */

static Vec3 axis_vec(int a) {
    if (a == 0) return v3(1, 0, 0);
    if (a == 1) return v3(0, 1, 0);
    return v3(0, 0, 1);
}
static Vec3 gizmo_pivot(void) {
    if (G.selected >= 0 && G.selected < G.scene.count && G.scene.e[G.selected].active)
        return G.scene.e[G.selected].pos;
    for (int i = 0; i < G.scene.count; i++)
        if (group_has(i) && G.scene.e[i].active) return G.scene.e[i].pos;
    return v3(0, 0, 0);
}
static float gizmo_len(Vec3 pivot) {
    float px = 0, py = 0;
    int ok = 0;
    project_point(pivot, &px, &py, &ok);
    if (!ok) return 1.0f;
    float best = 0;
    for (int i = 0; i < 3; i++) {
        float qx, qy; int ok2 = 0;
        project_point(v3_add(pivot, axis_vec(i)), &qx, &qy, &ok2);
        if (!ok2) continue;
        float d = sqrtf((qx - px) * (qx - px) + (qy - py) * (qy - py));
        if (d > best) best = d;
    }
    if (best < 1e-3f) return 1.0f;
    return 96.0f / best;
}
static float seg_dist(float px, float py, float ax, float ay, float bx, float by) {
    float dx = bx - ax, dy = by - ay;
    float l2 = dx * dx + dy * dy;
    float t = l2 > 1e-6f ? ((px - ax) * dx + (py - ay) * dy) / l2 : 0.0f;
    t = clampf(t, 0.0f, 1.0f);
    float qx = ax + dx * t, qy = ay + dy * t;
    return sqrtf((px - qx) * (px - qx) + (py - qy) * (py - qy));
}
static int gizmo_hit(float mx, float my) {
    if (G.selected < 0 || G.selected >= G.scene.count || !G.scene.e[G.selected].active) return -1;
    Vec3 pivot = gizmo_pivot();
    float L = gizmo_len(pivot), sx, sy;
    int ok = 0;
    project_point(pivot, &sx, &sy, &ok);
    if (!ok) return -1;
    if (G.gizmo_mode == 2) {
        float px = 0, py = 0; int ok0 = 0;
        project_point(pivot, &px, &py, &ok0);
        if (!ok0) return -1;
        for (int i = 0; i < 48; i++) {
            float a0 = 2.0f * VIPER_PI * i / 48.0f, a1 = 2.0f * VIPER_PI * (i + 1) / 48.0f;
            float x0, y0, x1, y1; int o0 = 0, o1 = 0;
            project_point(v3(pivot.x + cosf(a0) * L, pivot.y, pivot.z + sinf(a0) * L), &x0, &y0, &o0);
            project_point(v3(pivot.x + cosf(a1) * L, pivot.y, pivot.z + sinf(a1) * L), &x1, &y1, &o1);
            if (o0 && o1 && seg_dist(mx, my, x0, y0, x1, y1) < 11.0f) return 0;
        }
        return -1;
    }
    for (int i = 0; i < 3; i++) {
        float tx, ty; int ok2 = 0;
        project_point(v3_add(pivot, v3_mul(axis_vec(i), L)), &tx, &ty, &ok2);
        if (ok2 && seg_dist(mx, my, sx, sy, tx, ty) < 11.0f) return i;
    }
    if (G.gizmo_mode == 1 && (mx - sx) * (mx - sx) + (my - sy) * (my - sy) < 144.0f) return 3;
    return -1;
}
/* parameter t on the line (o + a t) closest to ray (ro + rd s) */
static float axis_param(Vec3 o, Vec3 a, Vec3 ro, Vec3 rd) {
    Vec3 w0 = v3_sub(o, ro);
    float d = v3_dot(a, w0), b = v3_dot(a, rd), e = v3_dot(rd, w0);
    float den = 1.0f - b * b;
    if (fabsf(den) < 1e-5f) return 0.0f;
    return (b * e - d) / den;
}
static float plane_angle(Vec3 c, Vec3 ro, Vec3 rd) {
    if (fabsf(rd.y) < 1e-5f) return 0.0f;
    float t = (c.y - ro.y) / rd.y;
    if (t < 0.0f) return 0.0f;
    return atan2f(ro.x + rd.x * t - c.x, ro.z + rd.z * t - c.z);
}
static void gizmo_begin(int axis, const Input *in) {
    push_undo();
    G.gz.active = 1;
    G.gz.axis = axis;
    G.gz.hot = axis;
    G.gz.pivot = gizmo_pivot();
    G.gz.n = 0;
    for (int i = 0; i < G.scene.count && G.gz.n < MAX_ENTITIES; i++) {
        if (!group_has(i) || !G.scene.e[i].active) continue;
        G.gz.idx[G.gz.n] = i;
        G.gz.sp[G.gz.n] = G.scene.e[i].pos;
        G.gz.ss[G.gz.n] = G.scene.e[i].scale;
        G.gz.sr[G.gz.n] = G.scene.e[i].rot.y;
        G.gz.n++;
    }
    {
        Vec3 ro, rd;
        screen_ray(in->mx, in->my, &ro, &rd);
        G.gz.t0 = axis_param(G.gz.pivot, axis_vec(axis <= 2 ? axis : 0), ro, rd);
        G.gz.ang0 = plane_angle(G.gz.pivot, ro, rd);
        G.gz.px0 = in->mx;
    }
}
static void gizmo_drag(const Input *in) {
    if (!G.gz.active) return;
    Vec3 ro, rd;
    screen_ray(in->mx, in->my, &ro, &rd);
    int snap = !in->down[VKEY_LSHIFT];
    float grid = GRIDS[G.grid_i];
    if (G.gizmo_mode == 0) {
        float t = axis_param(G.gz.pivot, axis_vec(G.gz.axis <= 2 ? G.gz.axis : 0), ro, rd);
        Vec3 d = v3_mul(axis_vec(G.gz.axis <= 2 ? G.gz.axis : 0), t - G.gz.t0);
        for (int k = 0; k < G.gz.n; k++) {
            Entity *e = &G.scene.e[G.gz.idx[k]];
            Vec3 p = v3_add(G.gz.sp[k], d);
            if (snap) { p.x = roundf(p.x / grid) * grid; p.y = roundf(p.y / grid) * grid; p.z = roundf(p.z / grid) * grid; }
            e->pos = p;
        }
    } else if (G.gizmo_mode == 1) {
        float f;
        if (G.gz.axis == 3) f = expf((in->mx - G.gz.px0) * 0.01f);
        else {
            float t = axis_param(G.gz.pivot, axis_vec(G.gz.axis), ro, rd);
            f = fabsf(G.gz.t0) > 1e-3f ? t / G.gz.t0 : 1.0f;
        }
        if (f < 0.01f) f = 0.01f;
        for (int k = 0; k < G.gz.n; k++) {
            Entity *e = &G.scene.e[G.gz.idx[k]];
            Vec3 s = G.gz.ss[k];
            if (G.gz.axis == 3) s = v3(s.x * f, s.y * f, s.z * f);
            else if (G.gz.axis == 0) s.x *= f;
            else if (G.gz.axis == 1) s.y *= f;
            else s.z *= f;
            e->scale = v3(fmaxf(s.x, 0.02f), fmaxf(s.y, 0.02f), fmaxf(s.z, 0.02f));
        }
    } else {
        float ang = plane_angle(G.gz.pivot, ro, rd);
        float delta = (ang - G.gz.ang0) * 180.0f / VIPER_PI;
        if (snap) delta = roundf(delta / 15.0f) * 15.0f;
        for (int k = 0; k < G.gz.n; k++) G.scene.e[G.gz.idx[k]].rot.y = G.gz.sr[k] + delta;
    }
    G.dirty = 1;
}

static void line_push(float *buf, int *n, Vec3 a, Vec3 b, Vec3 c) {
    buf[(*n)++] = a.x; buf[(*n)++] = a.y; buf[(*n)++] = a.z;
    buf[(*n)++] = c.x; buf[(*n)++] = c.y; buf[(*n)++] = c.z;
    buf[(*n)++] = b.x; buf[(*n)++] = b.y; buf[(*n)++] = b.z;
    buf[(*n)++] = c.x; buf[(*n)++] = c.y; buf[(*n)++] = c.z;
}
static void box_lines(float *buf, int *n, Vec3 lo, Vec3 hi, Vec3 c) {
    Vec3 p[8] = {
        v3(lo.x, lo.y, lo.z), v3(hi.x, lo.y, lo.z), v3(hi.x, lo.y, hi.z), v3(lo.x, lo.y, hi.z),
        v3(lo.x, hi.y, lo.z), v3(hi.x, hi.y, lo.z), v3(hi.x, hi.y, hi.z), v3(lo.x, hi.y, hi.z)
    };
    static const int E[12][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
    for (int i = 0; i < 12; i++) line_push(buf, n, p[E[i][0]], p[E[i][1]], c);
}

static int cursor_on_grid(float mx, float my, Vec3 *out);

static void draw_editor_overlay(void) {
    static float buf[65536];
    int n = 0;

    /* ground grid at the current layer */
    {
        float g = GRIDS[G.grid_i];
        float ext = 24.0f * g;
        float cx = roundf(G.cam_pos.x / g) * g, cz = roundf(G.cam_pos.z / g) * g;
        Vec3 gc = v3(0.30f, 0.32f, 0.36f);
        int lines = (int)(ext / g);
        if (lines > 48) { g = ext / 48.0f; lines = 48; }
        for (int i = -lines; i <= lines; i++) {
            line_push(buf, &n, v3(cx - ext, G.layer, cz + i * g), v3(cx + ext, G.layer, cz + i * g), gc);
            line_push(buf, &n, v3(cx + i * g, G.layer, cz - ext), v3(cx + i * g, G.layer, cz + ext), gc);
        }
    }

    /* selection boxes */
    {
        Vec3 sc = v3(1.0f, 0.85f, 0.2f);
        for (int i = 0; i < G.scene.count; i++) {
            if (!group_has(i) || !G.scene.e[i].active) continue;
            if (i == G.selected) continue;
            Vec3 lo, hi;
            if (entity_boxes(&G.scene.e[i], &lo, &hi)) box_lines(buf, &n, lo, hi, sc);
        }
    }

    /* gizmo */
    if (G.selected >= 0 && G.selected < G.scene.count && G.scene.e[G.selected].active && G.emode == 0) {
        Vec3 pivot = gizmo_pivot();
        float L = gizmo_len(pivot);
        Vec3 col[3] = {v3(0.92f, 0.27f, 0.27f), v3(0.35f, 0.86f, 0.35f), v3(0.31f, 0.51f, 0.96f)};
        Vec3 hot = v3(1.0f, 1.0f, 0.47f);
        if (G.gizmo_mode == 2) {
            Vec3 c = G.gz.hot >= 0 ? hot : v3(0.94f, 0.78f, 0.24f);
            for (int i = 0; i < 48; i++) {
                float a0 = 2.0f * VIPER_PI * i / 48.0f, a1 = 2.0f * VIPER_PI * (i + 1) / 48.0f;
                line_push(buf, &n, v3(pivot.x + cosf(a0) * L, pivot.y, pivot.z + sinf(a0) * L),
                          v3(pivot.x + cosf(a1) * L, pivot.y, pivot.z + sinf(a1) * L), c);
            }
        } else {
            for (int i = 0; i < 3; i++) {
                Vec3 tip = v3_add(pivot, v3_mul(axis_vec(i), L));
                Vec3 c = G.gz.hot == i ? hot : col[i];
                line_push(buf, &n, pivot, tip, c);
                if (G.gizmo_mode == 0) { /* arrow head */
                    Vec3 perp = i == 1 ? v3(1, 0, 0) : v3(0, 1, 0);
                    Vec3 a = v3_add(tip, v3_mul(perp, L * 0.06f));
                    Vec3 b = v3_sub(tip, v3_mul(perp, L * 0.06f));
                    line_push(buf, &n, a, v3_add(tip, v3_mul(v3_sub(pivot, tip), 0.12f)), c);
                    line_push(buf, &n, b, v3_add(tip, v3_mul(v3_sub(pivot, tip), 0.12f)), c);
                } else { /* scale bar */
                    float t = L * 0.07f;
                    if (i == 0) { line_push(buf, &n, v3(tip.x, tip.y - t, tip.z), v3(tip.x, tip.y + t, tip.z), c); line_push(buf, &n, v3(tip.x, tip.y, tip.z - t), v3(tip.x, tip.y, tip.z + t), c); }
                    if (i == 1) { line_push(buf, &n, v3(tip.x - t, tip.y, tip.z), v3(tip.x + t, tip.y, tip.z), c); line_push(buf, &n, v3(tip.x, tip.y, tip.z - t), v3(tip.x, tip.y, tip.z + t), c); }
                    if (i == 2) { line_push(buf, &n, v3(tip.x - t, tip.y, tip.z), v3(tip.x + t, tip.y, tip.z), c); line_push(buf, &n, v3(tip.x, tip.y - t, tip.z), v3(tip.x, tip.y + t, tip.z), c); }
                }
            }
        }
    }

    /* ghost preview while placing */
    if (G.emode == 1) {
        Vec3 p;
        if (cursor_on_grid(G.mx_last, G.my_last, &p)) {
            float s = fmaxf(G.place_scale, 0.1f) * 0.5f;
            Vec3 lo = v3(p.x - s, p.y, p.z - s), hi = v3(p.x + s, p.y + s * 2.0f, p.z + s);
            box_lines(buf, &n, lo, hi, v3(0.45f, 1.0f, 0.55f));
        }
    }

    if (n) renderer_lines(buf, n, 0);
}

/* --------------------------------------------------------------- placement */

static int cursor_on_grid(float mx, float my, Vec3 *out) {
    Vec3 ro, rd;
    screen_ray(mx, my, &ro, &rd);
    if (fabsf(rd.y) < 1e-4f) return 0;
    float t = (G.layer - ro.y) / rd.y;
    if (t < 0.0f || t > 500.0f) return 0;
    Vec3 p = v3_add(ro, v3_mul(rd, t));
    float g = GRIDS[G.grid_i];
    *out = v3(roundf(p.x / g) * g, G.layer, roundf(p.z / g) * g);
    return 1;
}

/* ------------------------------------------------------------------ update */

static void make_floor_scene(Scene *s) {
    scene_clear(s);
    snprintf(s->name, sizeof s->name, "start");
    Entity *f = scene_add(s, "Floor");
    if (f) {
        f->pos = v3(0, -0.5f, 0); f->scale = v3(40, 1, 40); f->checker = 1; f->body = BODY_STATIC;
        f->color = v3(0.35f, 0.55f, 0.4f);
    }
    Entity *p = scene_add(s, "Player");
    if (p) {
        snprintf(p->name, sizeof p->name, "Player");
        p->pos = v3(0, 0.9f, 0); p->scale = v3(1, 1.8f, 1);
        p->body = BODY_DYNAMIC; p->controller = 1;
        p->color = v3(0.85f, 0.3f, 0.25f);
    }
}

/* ------------------------------------------------------- baked data / jobs */

static void baked_paths(void) {
    snprintf(G.lm_path, sizeof G.lm_path, "%s.lightmap", G.scene_path);
    snprintf(G.oc_path, sizeof G.oc_path, "%s.pvs", G.scene_path);
    snprintf(G.pr_path, sizeof G.pr_path, "%s.probes", G.scene_path);
}

static void baked_free(void) {
    bake_lightmap_free(&G.blm);
    bake_occlusion_free(&G.boc);
    bake_probes_free(&G.bpr);
    renderer_clear_lightmap();
    renderer_clear_probes();
    G.has_lm = G.lm_stale = G.has_oc = G.oc_stale = G.has_pr = G.pr_stale = 0;
}

/* Load the sidecars that sit next to the .vscene (play.py: Lightmap/Probes/OcclusionData). */
static void baked_load(void) {
    int stale = 0;
    baked_free();
    if (!G.scene_path[0]) return;
    baked_paths();
    G.bake_sig = bake_signature(&G.scene);
    if (bake_load_lightmap(G.lm_path, &G.blm, G.bake_sig, &stale)) {
        G.has_lm = 1; G.lm_stale = stale;
        renderer_set_lightmap(G.blm.w, G.blm.h, G.blm.rgb, G.blm.minx, G.blm.minz, G.blm.sizex, G.blm.sizez);
        VLOG("lightmap %s (%dx%d%s)", G.lm_path, G.blm.w, G.blm.h, stale ? ", stale" : "");
    }
    if (bake_load_occlusion(G.oc_path, &G.boc, G.bake_sig, &stale)) {
        G.has_oc = 1; G.oc_stale = stale;
        VLOG("occlusion %s (%dx%dx%d%s)", G.oc_path, G.boc.nx, G.boc.ny, G.boc.nz, stale ? ", stale" : "");
    }
    if (bake_load_probes(G.pr_path, &G.bpr, G.bake_sig, &stale)) {
        G.has_pr = 1; G.pr_stale = stale;
        renderer_set_probes(G.bpr.nx, G.bpr.ny, G.bpr.nz, G.bpr.rgb,
                            G.bpr.minx, G.bpr.miny, G.bpr.minz,
                            G.bpr.sizex / (float)(G.bpr.nx > 0 ? G.bpr.nx : 1),
                            G.bpr.sizey / (float)(G.bpr.ny > 0 ? G.bpr.ny : 1),
                            G.bpr.sizez / (float)(G.bpr.nz > 0 ? G.bpr.nz : 1));
        VLOG("probes %s (%dx%dx%d%s)", G.pr_path, G.bpr.nx, G.bpr.ny, G.bpr.nz, stale ? ", stale" : "");
    }
}

/* Play-time PVS: skip static entities the bake says are hidden from this cell. */
static int pl_entity_visible(int i) {
    if (i < 0 || i >= G.scene.count) return 1;
    if (!G.has_oc || !G.boc.vis || G.oc_stale || G.scene.e[i].body != BODY_STATIC) return 1;
    return bake_occlusion_visible(&G.boc, G.pl.eye, i);
}

static void bake_progress_cb(float frac, void *ud) {
    BakeJob *j = (BakeJob *)ud;
    j->progress = frac;
}

static void bake_worker(void *ud) {
    BakeJob *j = (BakeJob *)ud;
    int ok = 0;
    char why[96];
    why[0] = 0;
    if (j->cancel) { snprintf(j->error, sizeof j->error, "cancelled"); j->done = 1; return; }
    if (j->kind == 0)
        ok = bake_occlusion(&j->scene, &j->tris, bake_progress_cb, j, &j->cancel, &j->oc);
    else if (j->kind == 1)
        ok = bake_lightmap(&j->scene, &j->tris, bake_progress_cb, j, &j->cancel, &j->lm);
    else
        ok = bake_probes(&j->scene, &j->tris, bake_progress_cb, j, &j->cancel, &j->pr);
    if (!ok) {
        if (j->cancel) snprintf(j->error, sizeof j->error, "cancelled");
        else snprintf(j->error, sizeof j->error, "%s bake failed%s%s", BAKE_NAMES[j->kind],
                      why[0] ? ": " : "", why);
    }
    j->ok = ok;
    j->done = 1;
}

static void bake_start(int kind) {
    BakeJob *j = &G.job;
    if (G.job_running) { msg("a bake is already running"); return; }
    if (!G.scene_path[0]) { msg("save the scene before baking"); return; }
    /* assets_load_obj uploads to GL, so warm every OBJ on the main thread first;
       the worker then only reads the CPU copy through assets_find_obj. */
    for (int i = 0; i < G.scene.count; i++)
        if (G.scene.e[i].active && G.scene.e[i].mesh == MESH_OBJ && G.scene.e[i].mesh_path[0])
            assets_load_obj(G.scene.e[i].mesh_path);

    memset(j, 0, sizeof *j);
    j->kind = kind;
    j->sig = bake_signature(&G.scene);
    j->scene = G.scene;
    bake_world_tris(&j->scene, &j->tris);
    j->progress = 0.0f;
    j->thread = vthread_start(bake_worker, j);
    if (!j->thread) {
        bake_free_tris(&j->tris);
        G.job_error[0] = 0;
        snprintf(G.job_error, sizeof G.job_error, "could not start the worker thread");
        msg("%s", G.job_error);
        return;
    }
    G.job_running = 1;
    G.job_p = 0.0f;
    snprintf(G.job_name, sizeof G.job_name, "%s", BAKE_NAMES[kind]);
    snprintf(G.job_text, sizeof G.job_text, "baking %s...", BAKE_NAMES[kind]);
    snprintf(G.job_error, sizeof G.job_error, " ");
    msg("baking %s (background)", BAKE_NAMES[kind]);
}

static void bake_poll(void) {
    BakeJob *j = &G.job;
    if (!G.job_running) return;
    G.job_p = j->progress;
    if (!j->done) {
        snprintf(G.job_text, sizeof G.job_text, "baking %s... %d%%", BAKE_NAMES[j->kind],
                 (int)(j->progress * 100.0f));
        return;
    }
    if (j->thread) { vthread_join(j->thread); j->thread = NULL; }

    if (j->ok) {
        baked_paths();
        if (j->kind == 0) {
            bake_occlusion_free(&G.boc);
            G.boc = j->oc;
            memset(&j->oc, 0, sizeof j->oc);
            G.has_oc = 1; G.oc_stale = 0;
            bake_save_occlusion(G.oc_path, &G.boc, j->sig);
            msg("occlusion baked (%dx%dx%d) -> %s", G.boc.nx, G.boc.ny, G.boc.nz, G.oc_path);
        } else if (j->kind == 1) {
            bake_lightmap_free(&G.blm);
            G.blm = j->lm;
            memset(&j->lm, 0, sizeof j->lm);
            G.has_lm = 1; G.lm_stale = 0;
            renderer_set_lightmap(G.blm.w, G.blm.h, G.blm.rgb,
                                  G.blm.minx, G.blm.minz, G.blm.sizex, G.blm.sizez);
            bake_save_lightmap(G.lm_path, &G.blm, j->sig);
            msg("lightmap baked (%dx%d) -> %s", G.blm.w, G.blm.h, G.lm_path);
        } else {
            bake_probes_free(&G.bpr);
            G.bpr = j->pr;
            memset(&j->pr, 0, sizeof j->pr);
            G.has_pr = 1; G.pr_stale = 0;
            renderer_set_probes(G.bpr.nx, G.bpr.ny, G.bpr.nz, G.bpr.rgb,
                                G.bpr.minx, G.bpr.miny, G.bpr.minz,
                                G.bpr.sizex / (float)(G.bpr.nx > 0 ? G.bpr.nx : 1),
                                G.bpr.sizey / (float)(G.bpr.ny > 0 ? G.bpr.ny : 1),
                                G.bpr.sizez / (float)(G.bpr.nz > 0 ? G.bpr.nz : 1));
            bake_save_probes(G.pr_path, &G.bpr, j->sig);
            msg("light probes baked (%dx%dx%d) -> %s", G.bpr.nx, G.bpr.ny, G.bpr.nz, G.pr_path);
        }
        G.bake_sig = j->sig;
        G.job_error[0] = 0;
    } else {
        snprintf(G.job_error, sizeof G.job_error, "%s", j->error[0] ? j->error : "bake failed");
        msg("%s", G.job_error);
    }
    bake_free_tris(&j->tris);
    G.job_running = 0;
    G.job_p = 0.0f;
    G.job_text[0] = 0;
}

/* ------------------------------------------------------- play mode (FPS) */

static Vec3 pl_forward(void) {
    return v3(-sinf(G.pl.yaw) * cosf(G.pl.pitch), sinf(G.pl.pitch), -cosf(G.pl.yaw) * cosf(G.pl.pitch));
}
static Vec3 pl_half(const Entity *p) {
    float hx = fabsf(p->scale.x) * 0.5f, hz = fabsf(p->scale.z) * 0.5f, hy = fabsf(p->scale.y) * 0.5f;
    if (hx > 0.35f) hx = 0.35f;
    if (hz > 0.35f) hz = 0.35f;
    if (hy < 0.4f) hy = 0.9f;
    return v3(hx, hy, hz);
}

static void play_msg_push(const char *text) {
    PlayMode *pl = &G.pl;
    if (!text || !text[0]) return;
    for (int i = PL_MSG_MAX - 1; i > 0; i--) {
        memcpy(pl->msg[i], pl->msg[i - 1], sizeof pl->msg[i]);
        pl->msg_t[i] = pl->msg_t[i - 1];
    }
    snprintf(pl->msg[0], sizeof pl->msg[0], "%s", text);
    pl->msg_t[0] = 3.0f;
    if (pl->nmsg < PL_MSG_MAX) pl->nmsg++;
}

static void play_hud_set(const char *text, int right) {
    PlayMode *pl = &G.pl;
    int n = right ? pl->nright : pl->nleft;
    char (*arr)[96] = right ? pl->right : pl->left;
    int base = right ? 6 : 0;
    if (!text || !text[0]) return;
    for (int i = 0; i < n; i++)
        if (!strcmp(arr[i], text)) { pl->hud_t[base + i] = pl->time; return; }
    if (n >= 6) n = 5; /* replace the oldest line when full */
    snprintf(arr[n], sizeof arr[n], "%s", text);
    pl->hud_t[base + n] = pl->time;
    if (right) { if (pl->nright < 6) pl->nright++; } else { if (pl->nleft < 6) pl->nleft++; }
}

static void play_damage(float amount) {
    PlayMode *pl = &G.pl;
    char buf[128];
    if (pl->health <= 0.0f || amount <= 0.0f) return;
    pl->health -= amount;
    if (pl->health < 0.0f) pl->health = 0.0f;
    snprintf(buf, sizeof buf, "-%d HP", (int)(amount + 0.5f));
    play_hud_set(buf, 1);
    if (pl->health <= 0.0f) {
        pl->dead_wait = 4.0f;
        play_msg_push("--- YOU DIED ---");
    }
}

static void play_api_damage(float amount) { play_damage(amount); }
static void play_api_message(const char *text) { play_msg_push(text); }
static void play_api_hud(const char *text, int right) { play_hud_set(text, right); }

static BlocksGameApi PLAY_API = { play_api_damage, play_api_message, play_api_hud };

static int pl_solid(const Entity *e) { return e->active && e->body != BODY_NONE; }

static int pl_blocked(Vec3 c, Vec3 half, int skip) {
    for (int i = 0; i < G.scene.count; i++) {
        const Entity *e = &G.scene.e[i];
        Vec3 h;
        if (i == skip || !pl_solid(e)) continue;
        h = v3(fabsf(e->scale.x) * 0.5f, fabsf(e->scale.y) * 0.5f, fabsf(e->scale.z) * 0.5f);
        if (fabsf(c.x - e->pos.x) < half.x + h.x - 1e-3f &&
            fabsf(c.y - e->pos.y) < half.y + h.y - 1e-3f &&
            fabsf(c.z - e->pos.z) < half.z + h.z - 1e-3f) return 1;
    }
    return 0;
}

/* highest solid top under the player's footprint, looking at most `probe` up */
static float pl_ground(Vec3 c, Vec3 half, int skip, float probe) {
    float g = -1e30f;
    float feet = c.y - half.y;
    for (int i = 0; i < G.scene.count; i++) {
        const Entity *e = &G.scene.e[i];
        Vec3 h;
        if (i == skip || !pl_solid(e)) continue;
        h = v3(fabsf(e->scale.x) * 0.5f, fabsf(e->scale.y) * 0.5f, fabsf(e->scale.z) * 0.5f);
        if (fabsf(c.x - e->pos.x) >= half.x + h.x - 1e-3f) continue;
        if (fabsf(c.z - e->pos.z) >= half.z + h.z - 1e-3f) continue;
        {
            float top = e->pos.y + h.y;
            if (top <= feet + probe && top > g) g = top;
        }
    }
    return g;
}

static void pl_move(Entity *p, Vec3 half, Vec3 wish, float dt) {
    PlayMode *pl = &G.pl;
    int n = (int)ceilf(dt * 120.0f);
    float h;
    if (n < 1) n = 1;
    if (n > 8) n = 8;
    h = dt / (float)n;
    for (int s = 0; s < n; s++) {
        float dx = wish.x * h, dz = wish.z * h;
        if (dx != 0.0f) {
            Vec3 t = p->pos;
            t.x += dx;
            if (!pl_blocked(t, half, pl->player)) p->pos = t;
            else {
                t = p->pos; t.x += dx; t.y += PL_STEP;
                if (!pl_blocked(t, half, pl->player)) p->pos = t;
            }
        }
        if (dz != 0.0f) {
            Vec3 t = p->pos;
            t.z += dz;
            if (!pl_blocked(t, half, pl->player)) p->pos = t;
            else {
                t = p->pos; t.z += dz; t.y += PL_STEP;
                if (!pl_blocked(t, half, pl->player)) p->pos = t;
            }
        }
        /* stick to the ground when walking down stairs / while grounded */
        if (pl->grounded) {
            float g0 = pl_ground(p->pos, half, pl->player, PL_STEP + 0.05f);
            float feet = p->pos.y - half.y;
            if (g0 > -1e29f && fabsf(feet - g0) <= PL_STEP + 0.06f) {
                p->pos.y = g0 + half.y;
                if (pl->vel.y < 0.0f) pl->vel.y = 0.0f;
            } else {
                pl->grounded = 0; /* walked off a ledge */
            }
        }
        pl->vel.y -= PL_GRAVITY * h;
        {
            float ny = p->pos.y + pl->vel.y * h;
            float g = pl_ground(p->pos, half, pl->player, PL_STEP + 0.05f);
            if (pl->vel.y <= 0.0f && g > -1e29f && ny - half.y <= g) {
                if (!pl->grounded && pl->vel.y < -12.0f) play_damage((-pl->vel.y - 12.0f) * 5.0f);
                p->pos.y = g + half.y;
                pl->vel.y = 0.0f;
                pl->grounded = 1;
            } else {
                p->pos.y = ny;
                if (pl->vel.y > 0.05f) pl->grounded = 0;
                else if (g <= -1e29f || p->pos.y - half.y > g + 0.06f) pl->grounded = 0;
            }
        }
    }
    p->vel = pl->vel;
    p->rot.y = pl->yaw * 180.0f / VIPER_PI;
}

static int pl_pick(Vec3 ro, Vec3 rd, int skip, float maxd, float *dist) {
    int best = -1;
    float bt = maxd;
    for (int i = 0; i < G.scene.count; i++) {
        const Entity *e = &G.scene.e[i];
        Vec3 lo, hi;
        float t;
        if (i == skip || !pl_solid(e)) continue;
        if (!entity_boxes(e, &lo, &hi)) continue;
        if (ray_aabb(ro, rd, lo, hi, &t) && t < bt) { bt = t; best = i; }
    }
    if (dist) *dist = bt;
    return best;
}

static int play_look_target(void) {
    Vec3 lo, hi;
    float t, bt = 4.0f;
    int best = -1;
    Vec3 o = G.pl.eye, d = pl_forward();
    for (int i = 0; i < G.scene.count; i++) {
        const Entity *e = &G.scene.e[i];
        if (i == G.pl.player || !e->active) continue;
        if (e->script_id < 0) continue; /* play.py only hints scripted units */
        if (!entity_boxes(e, &lo, &hi)) continue;
        if (ray_aabb(o, d, lo, hi, &t) && t < bt) { bt = t; best = i; }
    }
    return best;
}

static void play_shoot(void) {
    float dist = 200.0f;
    Vec3 ro = G.pl.eye, rd = pl_forward();
    int hit = pl_pick(ro, rd, G.pl.player, 200.0f, &dist);
    if (hit < 0) return;
    {
        Entity *e = &G.scene.e[hit];
        char buf[96];
        if (e->body == BODY_DYNAMIC) {
            e->vel = v3_add(e->vel, v3_mul(rd, 9.0f));
            snprintf(buf, sizeof buf, "hit %s", e->name);
        } else snprintf(buf, sizeof buf, "hit %s (%.0fm)", e->name, dist);
        play_msg_push(buf);
    }
}

static void play_respawn(void) {
    Entity *p;
    PlayMode *pl = &G.pl;
    if (pl->player < 0 || pl->player >= G.scene.count) return;
    p = &G.scene.e[pl->player];
    p->pos = p->spawn;
    p->vel = v3(0, 0, 0);
    pl->vel = v3(0, 0, 0);
    pl->health = PL_MAX_HEALTH;
    pl->dead_wait = 0.0f;
    pl->grounded = 0;
    pl->yaw = DEG2RAD(p->rot.y);
    pl->pitch = 0.0f;
    pl->eye = v3(p->pos.x, p->pos.y - pl_half(p).y + PL_EYE, p->pos.z);
}

static void enter_play(void) {
    int standalone = G.pl.standalone;
    PlayMode *pl = &G.pl;
    Entity *p;

    G.snapshot = G.scene;
    memset(pl, 0, sizeof *pl);
    pl->standalone = standalone;
    pl->player = -1;
    pl->fog = 1;
    pl->health = PL_MAX_HEALTH;

    for (int i = 0; i < G.scene.count; i++)
        if (G.scene.e[i].active && G.scene.e[i].controller) { pl->player = i; break; }
    if (pl->player < 0)
        for (int i = 0; i < G.scene.count; i++) {
            const char *n = G.scene.e[i].name;
            if (G.scene.e[i].active && (!strcmp(n, "Player") || !strcmp(n, "player"))) { pl->player = i; break; }
        }
    if (pl->player < 0)
        for (int i = 0; i < G.scene.count; i++)
            if (G.scene.e[i].active && G.scene.e[i].body == BODY_DYNAMIC) { pl->player = i; break; }
    if (pl->player < 0) {
        Entity *e = scene_add(&G.scene, "Player");
        if (!e) { VLOG("play: no player and no room for one"); return; }
        e->pos = v3(0, 0.9f, 0);
        e->scale = v3(1, 1.8f, 1);
        e->color = v3(0.85f, 0.3f, 0.25f);
        pl->player = G.scene.count - 1;
    }
    p = &G.scene.e[pl->player];
    p->active = 1;
    p->spawn = p->pos;
    p->vel = v3(0, 0, 0);
    p->input_vel = v3(0, 0, 0);
    p->controller = 1;
    p->body = BODY_STATIC;   /* the engine drives the player; bodies still collide with it */
    if (p->scale.y < 1.0f) p->scale.y = 1.8f;

    blocks_reset_vars();
    blocks_bind_scene(&G.scene);
    p->script_id = -1;       /* walk/turn blocks must not fight the FPS controller */
    blocks_set_game_api(&PLAY_API);

    pl->yaw = DEG2RAD(p->rot.y);
    pl->pitch = 0.0f;
    pl->grounded = 0;
    pl->vel = v3(0, 0, 0);
    pl->eye = v3(p->pos.x, p->pos.y - pl_half(p).y + PL_EYE, p->pos.z);
    pl->on = 1;

    G.mode = MODE_PLAY;
    window_set_captured(G.win, 1);
    blocks_update_scene(&G.scene, window_input(G.win), 0.0f, BLOCKS_EV_START);
    VLOG("PLAY: first person, eye %.1f, health %.0f", PL_EYE, pl->health);
}

static void exit_play(void) {
    int standalone = G.pl.standalone;
    G.scene = G.snapshot;
    blocks_bind_scene(&G.scene);
    blocks_set_game_api(NULL);
    G.pl.on = 0;
    G.mode = MODE_EDIT;
    window_set_captured(G.win, 0);
    scan_assets();
    VLOG("STOP (scene restored)");
    if (standalone) window_request_close(G.win);
}

static void edit_update(const Input *in, float dt);
static void play_update(const Input *in, float dt);
static void menu_refresh(void);

static void edit_update(const Input *in, float dt) {
    window_set_captured(G.win, in->rmb);
    if (G.msg_t > 0) { G.msg_t -= dt; if (G.msg_t <= 0) G.msg[0] = 0; }

    if (in->pressed['M'] && !in->down[VKEY_LCTRL]) {
        G.mode = MODE_MENU; menu_refresh(); window_set_captured(G.win, 0); return;
    }
    /* tabs (F1..F6) */
    for (int i = 0; i < 6; i++) {
        if (in->pressed[VKEY_F1 + i]) { G.tab = i; msg("tab: %s", i == 0 ? "Import" : i == 1 ? "Map" : i == 2 ? "Bake OC" : i == 3 ? "Lighting" : i == 4 ? "Scripts" : "Marketplace"); }
    }

    if (in->rmb) {
        G.cam_yaw -= in->mouse_dx * 0.003f;
        G.cam_pitch = clampf(G.cam_pitch - in->mouse_dy * 0.003f, -1.5f, 1.5f);
        Vec3 f = cam_forward(), r = v3(cosf(G.cam_yaw), 0, -sinf(G.cam_yaw));
        float sp = 8.0f * (in->down[VKEY_LSHIFT] ? 3.0f : 1.0f) * dt;
        if (in->down['W']) G.cam_pos = v3_add(G.cam_pos, v3_mul(f, sp));
        if (in->down['S']) G.cam_pos = v3_sub(G.cam_pos, v3_mul(f, sp));
        if (in->down['D']) G.cam_pos = v3_add(G.cam_pos, v3_mul(r, sp));
        if (in->down['A']) G.cam_pos = v3_sub(G.cam_pos, v3_mul(r, sp));
        if (in->down['E']) G.cam_pos.y += sp;
        if (in->down['Q']) G.cam_pos.y -= sp;
        return;
    }
    if (in->wheel != 0.0f && !G.ui_hover)
        G.cam_pos = v3_add(G.cam_pos, v3_mul(cam_forward(), in->wheel * 1.2f));

    /* undo / redo */
    if (in->down[VKEY_LCTRL] && in->pressed['Z']) do_undo();
    if (in->down[VKEY_LCTRL] && in->pressed['Y']) do_redo();
    if (in->down[VKEY_LCTRL] && in->pressed['A']) { select_all(); msg("selected all"); }
    if (in->down[VKEY_LCTRL] && in->pressed['S']) {
        if (G.tab == 4 && G.script_name[0]) save_script();
        else if (scene_save(&G.scene, G.scene_path)) { G.dirty = 0; msg("saved %s", G.scene_path); }
        else msg("could not save %s", G.scene_path);
    }
    if (in->pressed[VKEY_F2]) {
        if (scene_save(&G.scene, G.scene_path)) { G.dirty = 0; msg("saved %s", G.scene_path); }
        else msg("could not save %s", G.scene_path);
    }

    /* tool shortcuts (editor.py) */
    if (in->pressed[VKEY_TAB]) { G.emode = !G.emode; msg(G.emode ? "place mode" : "select mode"); }
    if (in->pressed['1']) G.gizmo_mode = 0;
    if (in->pressed['2']) G.gizmo_mode = 1;
    if (in->pressed['3']) G.gizmo_mode = 2;
    if (in->pressed['G']) { G.grid_i = (G.grid_i + 1) % GRID_N; msg("grid %.2f", GRIDS[G.grid_i]); }
    if (in->pressed[VKEY_PGUP]) G.layer += GRIDS[G.grid_i];
    if (in->pressed[VKEY_PGDN]) G.layer -= GRIDS[G.grid_i];
    if (in->pressed['R']) rotate_group(15.0f);
    if (in->pressed[VKEY_EQUAL]) scale_group(1.1f);
    if (in->pressed[VKEY_MINUS]) scale_group(1.0f / 1.1f);
    if (in->pressed['C'] && !in->down[VKEY_LCTRL]) duplicate_group();
    if (in->pressed[VKEY_DELETE]) delete_group();
    if (in->pressed['F']) focus_selection();

    if (G.selected >= 0 && G.selected < G.scene.count && G.scene.e[G.selected].active) {
        const float step = GRIDS[G.grid_i];
        int moved = 0;
        if (in->pressed[VKEY_LEFT]) { for (int i = 0; i < G.scene.count; i++) if (group_has(i)) G.scene.e[i].pos.x -= step; moved = 1; }
        if (in->pressed[VKEY_RIGHT]) { for (int i = 0; i < G.scene.count; i++) if (group_has(i)) G.scene.e[i].pos.x += step; moved = 1; }
        if (in->pressed[VKEY_UP]) { for (int i = 0; i < G.scene.count; i++) if (group_has(i)) G.scene.e[i].pos.z -= step; moved = 1; }
        if (in->pressed[VKEY_DOWN]) { for (int i = 0; i < G.scene.count; i++) if (group_has(i)) G.scene.e[i].pos.z += step; moved = 1; }
        if (in->pressed['R'] || moved) G.dirty = 1;
    }

    /* viewport mouse */
    G.mx_last = in->mx; G.my_last = in->my;
    if (!G.ui_hover && in->lmb_pressed) {
        G.gz.hot = gizmo_hit(in->mx, in->my);
        if (G.gz.hot >= 0) {
            gizmo_begin(G.gz.hot, in);
        } else if (G.emode == 1) {
            Vec3 p;
            if (cursor_on_grid(in->mx, in->my, &p)) { push_undo(); add_primitive(primary_asset(), p); }
            else msg("aim at the grid plane");
        } else {
            Vec3 ro, rd;
            screen_ray(in->mx, in->my, &ro, &rd);
            int hit = pick_entity(ro, rd);
            if (hit >= 0) {
                if (in->down[VKEY_LCTRL]) { group_set(hit, !group_has(hit)); G.selected = hit; }
                else if (in->down[VKEY_LSHIFT]) { group_set(hit, 1); G.selected = hit; }
                else select_only(hit);
                msg("selected %s", G.scene.e[hit].name);
            } else { select_only(-1); }
        }
    }
    if (G.gz.active) {
        if (in->lmb) gizmo_drag(in);
        else { G.gz.active = 0; msg("transform complete"); }
    }
    if (!G.gz.active && !G.ui_hover && G.emode == 0) {
        int h = gizmo_hit(in->mx, in->my);
        if (h != G.gz.hot) G.gz.hot = h;
    }
}

static void play_update(const Input *in, float dt) {
    PlayMode *pl = &G.pl;
    Entity *p;
    Vec3 wish = v3(0, 0, 0);

    if (pl->player < 0 || pl->player >= G.scene.count) return;
    p = &G.scene.e[pl->player];
    pl->time += dt;

    window_set_captured(G.win, 1);
    pl->yaw -= in->mouse_dx * 0.0025f;
    pl->pitch = clampf(pl->pitch - in->mouse_dy * 0.0025f, -1.5f, 1.5f);
    if (in->pressed['F']) { pl->fog = !pl->fog; play_msg_push(pl->fog ? "fog on" : "fog off"); }

    if (pl->health > 0.0f) {
        float speed = in->down[VKEY_LSHIFT] ? PL_RUN : PL_WALK;
        float fx = -sinf(pl->yaw), fz = -cosf(pl->yaw);
        float rx = cosf(pl->yaw), rz = -sinf(pl->yaw);
        float mf = (float)((in->down['W'] ? 1 : 0) - (in->down['S'] ? 1 : 0));
        float mr = (float)((in->down['D'] ? 1 : 0) - (in->down['A'] ? 1 : 0));
        float wx = fx * mf + rx * mr, wz = fz * mf + rz * mr;
        float l = sqrtf(wx * wx + wz * wz);
        if (l > 1e-5f) { wx /= l; wz /= l; }
        wish = v3(wx * speed, 0, wz * speed);
        if (in->pressed[VKEY_SPACE] && pl->grounded) { pl->vel.y = PL_JUMP; pl->grounded = 0; }
    } else {
        pl->dead_wait -= dt;
        if (pl->dead_wait <= 0.0f) { play_respawn(); play_msg_push("respawned - go get them"); }
    }

    pl_move(p, pl_half(p), wish, dt);
    pl->eye = v3(p->pos.x, p->pos.y - pl_half(p).y + PL_EYE, p->pos.z);

    /* scripts: entity behaviour (on_start ran on enter), then per-frame input */
    blocks_update_scene(&G.scene, in, dt, BLOCKS_EV_UPDATE);
    blocks_update_scene(&G.scene, in, dt, BLOCKS_EV_KEYS);

    /* dynamic bodies (crates, props) */
    physics_update(&G.scene, dt);

    /* interaction hint refreshed at 20 Hz, like play.py's _cross */
    if (pl->time - pl->hint_t >= 0.05f) {
        pl->hint_t = pl->time;
        pl->target = play_look_target();
    }
    if (in->pressed['E'] && pl->target >= 0 && pl->target < G.scene.count) {
        blocks_run_entity(&G.scene, pl->target, in, dt, BLOCKS_EV_INTERACT);
        pl->target = play_look_target();
    }
    if (in->lmb_pressed && pl->health > 0.0f) play_shoot();

    /* expire messages and one-shot HUD lines */
    {
        int w = 0;
        for (int i = 0; i < pl->nmsg; i++) {
            pl->msg_t[i] -= dt;
            if (pl->msg_t[i] > 0.0f) {
                if (w != i) { memcpy(pl->msg[w], pl->msg[i], sizeof pl->msg[w]); pl->msg_t[w] = pl->msg_t[i]; }
                w++;
            }
        }
        pl->nmsg = w;
    }
    for (int side = 0; side < 2; side++) {
        char (*arr)[96] = side ? pl->right : pl->left;
        int *n = side ? &pl->nright : &pl->nleft;
        int base = side ? 6 : 0, w = 0;
        for (int i = 0; i < *n; i++) {
            if (pl->time - pl->hud_t[base + i] <= 2.5f) {
                if (w != i) { memcpy(arr[w], arr[i], sizeof arr[w]); pl->hud_t[base + w] = pl->hud_t[base + i]; }
                w++;
            }
        }
        *n = w;
    }
}

static Mat4 make_view(void) {
    if (G.mode == MODE_PLAY)
        return m4_look_at(G.pl.eye, v3_add(G.pl.eye, pl_forward()), v3(0, 1, 0));
    return m4_look_at(G.cam_pos, v3_add(G.cam_pos, cam_forward()), v3(0, 1, 0));
}

/* ---------------- project browser ---------------- */

static void menu_refresh(void) {
    fs_mkdir("projects");
    G.menu.nprojects = fs_list_dirs("projects", G.menu.projects, VIP_MENU_MAX);
    fs_getcwd(G.menu.root, sizeof G.menu.root);
    {
        size_t n = strlen(G.menu.root);
        if (n + 10 < sizeof G.menu.root) snprintf(G.menu.root + n, sizeof G.menu.root - n, "/projects");
    }
    for (int i = 0; i < G.menu.nprojects; i++) {
        char file[400];
        FILE *f;
        snprintf(file, sizeof file, "projects/%s/%s.vscene", G.menu.projects[i], G.menu.projects[i]);
        f = fopen(file, "r");
        G.menu.counts[i] = -1;
        if (f) {
            char line[256];
            int c = 0;
            while (fgets(line, sizeof line, f))
                if (!strncmp(line, "entity ", 7)) c++;
            fclose(f);
            G.menu.counts[i] = c;
        }
    }
    if (!G.menu.status[0])
        snprintf(G.menu.status, sizeof G.menu.status, "%d project(s) found - open one, or create a new one",
                 G.menu.nprojects);
}

static void open_project(const char *name) {
    char file[200];
    snprintf(file, sizeof file, "projects/%s/%s.vscene", name, name);
    if (!scene_load(&G.scene, file)) {
        statusf("could not open projects/%s", name);
        VLOG("could not open %s", file);
        return;
    }
    snprintf(G.scene_path, sizeof G.scene_path, "%s", file);
    blocks_bind_scene(&G.scene);
    baked_load();
    G.selected = -1;
    group_clear();
    G.undo_n = G.redo_n = 0;
    G.dirty = 0;
    G.cam_pos = v3(0, 6, 14); G.cam_pitch = -0.35f;
    G.mode = MODE_EDIT;
    G.tab = 1;
    scan_assets();
    scan_scripts();
    scan_market();
    statusf("opened projects/%s", name);
    VLOG("opened %s", file);
}

static void menu_open(void) {
    if (!G.menu.projsel[0]) { statusf("click a project in the list first"); return; }
    open_project(G.menu.projsel);
}

static void menu_new(void) {
    const char *name = G.menu.newname;
    char dir[96], sub[128], file[200];
    if (!name[0]) { statusf("type a project name first"); return; }
    snprintf(dir, sizeof dir, "projects/%s", name);
    if (fs_is_dir(dir)) { statusf("%s already exists - pick another name", dir); return; }
    fs_mkdir("projects");
    if (!fs_mkdir(dir)) { statusf("could not create %s", dir); return; }
    make_floor_scene(&G.scene);
    snprintf(file, sizeof file, "%s/%s.vscene", dir, name);
    if (!scene_save(&G.scene, file)) { statusf("could not create %s", file); return; }
    if (fs_is_dir("scripts")) {
        snprintf(sub, sizeof sub, "%s/scripts", dir);
        fs_copy_tree("scripts", sub);
    }
    VLOG("created %s", file);
    menu_refresh();
    open_project(name);
}

static void menu_export(void) {
    const char *name = G.menu.projsel;
    char dir[160], exe[260], bat[300], srcfile[200];
    FILE *f;
    if (!name[0]) { statusf("select a project to compile first"); return; }
    snprintf(dir, sizeof dir, "exports/%s", name);
    snprintf(srcfile, sizeof srcfile, "projects/%s", name);
    fs_mkdir("exports");
    if (!fs_is_dir(srcfile) || !fs_copy_tree(srcfile, dir)) { statusf("could not copy projects/%s", name); return; }
    snprintf(exe, sizeof exe, "%s/viper.exe", dir);
    if (G.argv0[0] && fs_copy_file(G.argv0, exe)) VLOG("packaged engine exe %s", exe);
    else statusf("warning: engine exe not packaged (rename it to viper.exe next to this build)");
    /* the game needs its assets + bundled marketplace next to the exe */
    {
        char dst_assets[260], dst_market[260];
        snprintf(dst_assets, sizeof dst_assets, "%s/assets", dir);
        snprintf(dst_market, sizeof dst_market, "%s/market", dir);
        if (fs_is_dir("assets")) fs_copy_tree("assets", dst_assets);
        if (fs_is_dir("market")) fs_copy_tree("market", dst_market);
    }
    snprintf(bat, sizeof bat, "%s/run.bat", dir);
    f = fopen(bat, "w");
    if (f) {
        fprintf(f, "@echo off\r\nstart \"Viper - %s\" viper.exe %s.vscene --play\r\n", name, name);
        fclose(f);
    }
    statusf("compiled to %s - run.bat starts the game in first person", dir);
    VLOG("exported projects/%s -> %s", name, dir);
}

/* --------------------------------------------------------------- gui sync */

static void gui_sync_out(GuiEditor *ed) {
    memset(ed, 0, sizeof *ed);
    ed->scene = &G.scene;
    ed->scene_path = G.scene_path;
    ed->selected = G.selected;
    memcpy(ed->group, G.group, sizeof ed->group);
    ed->tab = G.tab;
    ed->mode = G.emode;
    ed->gizmo_mode = G.gizmo_mode;
    ed->grid_i = G.grid_i;
    ed->layer = G.layer;
    ed->place_rot = G.place_rot;
    ed->place_scale = G.place_scale;
    snprintf(ed->msg, sizeof ed->msg, "%s", G.msg);
    ed->msg_t = G.msg_t;
    ed->dirty = G.dirty;
    ed->nassets = G.nassets;
    memcpy(ed->assets, G.assets, sizeof ed->assets);
    ed->asset_sel = G.asset_sel;
    ed->has_lm = G.has_lm; ed->lm_stale = G.lm_stale;
    ed->has_oc = G.has_oc; ed->oc_stale = G.oc_stale;
    ed->has_pr = G.has_pr; ed->pr_stale = G.pr_stale;
    ed->job_running = G.job_running;
    snprintf(ed->job_name, sizeof ed->job_name, "%s", G.job_name);
    ed->job_p = G.job_p;
    snprintf(ed->job_text, sizeof ed->job_text, "%s", G.job_text);
    snprintf(ed->job_error, sizeof ed->job_error, "%s", G.job_error);
    snprintf(ed->import_log, sizeof ed->import_log, "%s", G.import_log);
    ed->nscripts = G.nscripts;
    memcpy(ed->scripts, G.scripts, sizeof ed->scripts);
    ed->script_sel = G.script_sel;
    snprintf(ed->script_name, sizeof ed->script_name, "%s", G.script_name);
    memcpy(ed->script_buf, G.script_buf, sizeof ed->script_buf);
    ed->script_dirty = G.script_dirty;
    ed->nmarket = G.nmarket;
    memcpy(ed->market_names, G.market_names, sizeof ed->market_names);
    memcpy(ed->market_desc, G.market_desc, sizeof ed->market_desc);
    memcpy(ed->market_files, G.market_files, sizeof ed->market_files);
    ed->market_sel = G.market_sel;
    snprintf(ed->market_status, sizeof ed->market_status, "%s", G.market_status);
    ed->market_nsources = 1;
    snprintf(ed->market_sources[0], sizeof ed->market_sources[0], "market/ (bundled)");
    ed->market_source_sel = 0;
}

static void do_import(void) {
    char path[600];
    if (!fs_pick_file(path, sizeof path, "Wavefront OBJ", "obj")) { msg("import cancelled"); return; }
    if (!fs_is_dir("assets")) fs_mkdir("assets");
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    {
        const char *bs = strrchr(base, '\\');
        if (bs) base = bs + 1;
    }
    char dst[700];
    snprintf(dst, sizeof dst, "assets/%s", base);
    if (!fs_copy_file(path, dst)) { msg("could not copy %s", base); return; }
    snprintf(G.import_log, sizeof G.import_log, "imported %s -> %s", base, dst);
    scan_assets();
    msg("imported %s", base);
}

static void gui_sync_in(GuiEditor *ed) {
    /* plain state */
    if (ed->tab != G.tab) { G.tab = ed->tab; G.msg[0] = 0; }
    G.selected = ed->selected;
    memcpy(G.group, ed->group, sizeof G.group);
    G.emode = ed->mode;
    G.gizmo_mode = ed->gizmo_mode;
    G.grid_i = ed->grid_i;
    G.layer = ed->layer;
    G.asset_sel = ed->asset_sel;
    G.script_sel = ed->script_sel;
    G.script_dirty = ed->script_dirty;
    G.market_sel = ed->market_sel;
    memcpy(G.script_buf, ed->script_buf, sizeof G.script_buf);
    G.ui_hover = ed->ui_hover;
    if (G.selected < 0 || G.selected >= G.scene.count || !G.scene.e[G.selected].active) {
        if (G.selected != -1) { G.selected = -1; }
    }

    /* undo from a widget that just started editing: keep the pre-frame scene */
    if (ed->push_undo) push_undo_scene(&G.pre);

    if (ed->save_pressed) {
        if (scene_save(&G.scene, G.scene_path)) { G.dirty = 0; msg("saved %s", G.scene_path); }
        else msg("could not save %s", G.scene_path);
    }
    if (ed->play_pressed) { G.mode = MODE_PLAY; enter_play(); return; }
    if (ed->duplicate_pressed) duplicate_group();
    if (ed->delete_pressed) delete_group();
    if (ed->focus_pressed) focus_selection();
    if (ed->select_all_pressed) select_all();
    if (ed->undo_pressed) do_undo();
    if (ed->redo_pressed) do_redo();
    if (ed->import_pressed) do_import();
    if (ed->script_scan_req) scan_scripts();
    if (ed->script_changed) {
        blocks_bind_scene(&G.scene);
        G.dirty = 1;
    }
    if (ed->script_load_req) load_script(G.script_sel);
    if (ed->script_new_pressed) new_script();
    if (ed->script_save_pressed) save_script();
    if (ed->assign_script_pressed) {
        if (G.selected < 0 || G.selected >= G.scene.count) msg("select a unit first");
        else if (!G.script_name[0]) msg("open a script first");
        else {
            push_undo();
            char base[64];
            snprintf(base, sizeof base, "%s", G.script_name);
            char *dot = strstr(base, ".vblocks");
            if (dot) *dot = 0;
            snprintf(G.scene.e[G.selected].script, sizeof G.scene.e[G.selected].script, "%s", base);
            blocks_bind_scene(&G.scene);
            G.dirty = 1;
            msg("assigned %s to %s", base, G.scene.e[G.selected].name);
        }
    }
    if (ed->market_refresh_pressed) { scan_market(); msg("catalog refreshed"); }
    if (ed->market_get_pressed) {
        int i = G.market_sel;
        if (i >= 0 && i < G.nmarket) {
            char sub[160], files[32][64], dst[300];
            int n;
            snprintf(sub, sizeof sub, "market/%s", G.market_names[i]);
            n = fs_list_files(sub, files, 32, NULL);
            if (!fs_is_dir("assets")) fs_mkdir("assets");
            int ok = 0;
            for (int k = 0; k < n; k++) {
                char src[400];
                snprintf(src, sizeof src, "%s/%s", sub, files[k]);
                snprintf(dst, sizeof dst, "assets/%s", files[k]);
                if (fs_copy_file(src, dst)) ok++;
            }
            if (ok) {
                snprintf(G.market_status, sizeof G.market_status, "installed %d file(s) from %s", ok, G.market_names[i]);
                snprintf(G.market_files[i], 64, "%d file(s) in assets/", ok);
                scan_assets();
                msg("installed %s", G.market_names[i]);
            } else snprintf(G.market_status, sizeof G.market_status, "nothing to install in %s", G.market_names[i]);
        }
    }
    if (ed->market_add_source_pressed) {
        char path[600];
        if (fs_pick_file(path, sizeof path, "catalog.json", "json")) {
            snprintf(G.market_status, sizeof G.market_status, "custom URL catalogs arrive with the network pass - local market/ packs work now");
            msg("added %s", path);
        }
    }
    if (ed->cancel_pressed && G.job_running) {
        G.job.cancel = 1;
        msg("cancelling %s...", G.job_name);
    }
    if (ed->bake_oc_pressed) bake_start(0);
    if (ed->bake_lm_pressed) bake_start(1);
    if (ed->bake_pr_pressed) bake_start(2);
}

/* ---------------- main loop ---------------- */

int engine_run(const char *scene_path, const char *argv0, int play_only) {
    memset(&G, 0, sizeof G);
    if (argv0 && argv0[0]) snprintf(G.argv0, sizeof G.argv0, "%s", argv0);
    if (scene_path && scene_path[0]) snprintf(G.scene_path, sizeof G.scene_path, "%s", scene_path);
    G.win = window_create(1280, 720, "Viper Engine");
    if (!G.win) return 1;
    if (!renderer_init()) { VLOG("renderer failed to initialise"); window_destroy(G.win); return 1; }
    if (!gui_init(window_glfw(G.win))) {
        VLOG("GUI failed to initialise");
        window_destroy(G.win);
        return 1;
    }
    G.emode = 0;
    G.gizmo_mode = 0;
    G.grid_i = 2;
    G.place_scale = 1.0f;
    G.bg = v3(0.10f, 0.11f, 0.14f);
    G.tab = 1;
    G.vx = 260; G.vy = 34; G.vw = 1280 - 260 - 330; G.vh = 720 - 34 - 24;
    fs_mkdir("assets");
    scan_assets();
    scan_scripts();
    scan_market();

    if (G.scene_path[0]) {
        if (!scene_load(&G.scene, G.scene_path)) {
            VLOG("could not read %s - starting with a fresh floor", G.scene_path);
            make_floor_scene(&G.scene);
        }
        blocks_bind_scene(&G.scene);
        baked_load();
        G.selected = -1;
        G.cam_pos = v3(0, 6, 14); G.cam_pitch = -0.35f;
        G.mode = MODE_EDIT;
        if (play_only) {
            G.pl.standalone = 1;
            enter_play();
            VLOG("compiled game: first-person play mode (ESC quits)");
        } else {
            VLOG("v0.4 editor ready. F1-F6 tabs | RMB+WASD fly | LMB select | TAB place/select | 1/2/3 gizmo | Ctrl+Z/Y | F5 play | M browser");
        }
    } else {
        G.mode = MODE_MENU;
        menu_refresh();
        VLOG("project browser ready. pick or create a project");
    }

    {
        double last = window_time(), fps_t = last;
        int frames = 0;
        char title[256] = "Viper Engine";
        GuiEditor ed;
        while (!window_should_close(G.win)) {
            window_poll(G.win);
            const Input *in = window_input(G.win);
            double now = window_time();
            float dt = (float)(now - last); last = now;
            if (dt > 0.05f) dt = 0.05f;

            G.pre = G.scene; /* pre-frame snapshot for widget undo */
            bake_poll();

            if (G.mode == MODE_MENU) {
                if (in->pressed[VKEY_ESC]) window_request_close(G.win);
                if (G.menu.new_pressed) { G.menu.new_pressed = 0; menu_new(); }
                if (G.menu.open_pressed) { G.menu.open_pressed = 0; menu_open(); }
                if (G.menu.export_pressed) { G.menu.export_pressed = 0; menu_export(); }
                if (G.menu.refresh_pressed) { G.menu.refresh_pressed = 0; menu_refresh(); }
                if (G.menu.open_folder_pressed) {
                    G.menu.open_folder_pressed = 0;
                    if (!fs_open_path(G.menu.root[0] ? G.menu.root : "projects"))
                        statusf("could not open the projects folder");
                }
            } else {
                if (in->pressed[VKEY_F5]) {
                    if (G.mode == MODE_EDIT) enter_play();
                    else if (!G.pl.standalone) exit_play();
                } else if (in->pressed[VKEY_ESC]) {
                    if (G.mode == MODE_PLAY) exit_play(); else window_request_close(G.win);
                }
                if (G.mode == MODE_EDIT) edit_update(in, dt);
                else if (G.mode == MODE_PLAY) play_update(in, dt);
            }

            {
                int fw, fh, ww, wh;
                window_framebuffer_size(G.win, &fw, &fh);
                window_size(G.win, &ww, &wh);
                if (G.mode == MODE_PLAY) {
                    renderer_set_camera(G.pl.eye);
                    renderer_set_fog(G.pl.fog ? 30.0f : 1e5f, G.pl.fog ? 110.0f : 2e5f);
                } else {
                    renderer_set_camera(G.cam_pos);
                    renderer_set_fog(1e5f, 2e5f);
                }
                if (fw > 0 && fh > 0) {
                    float dpi_x = ww > 0 ? (float)fw / (float)ww : 1.0f;
                    float dpi_y = wh > 0 ? (float)fh / (float)wh : 1.0f;
                    Vec3 sky = G.mode == MODE_PLAY ? v3(0.55f, 0.68f, 0.85f)
                             : G.mode == MODE_MENU ? v3(0.10f, 0.11f, 0.16f) : v3(0.18f, 0.2f, 0.25f);
                    if (G.mode == MODE_EDIT) {
                        int vx = (int)((float)G.vx * dpi_x), vy = (int)((float)G.vy * dpi_y);
                        int vw = (int)((float)G.vw * dpi_x), vh = (int)((float)G.vh * dpi_y);
                        renderer_begin_view(fw, fh, vx, vy, vw, vh, G.bg, sky);
                    } else {
                        renderer_begin(fw, fh, sky);
                    }
                    if (G.mode != MODE_MENU) {
                        float aspect = G.mode == MODE_EDIT && G.vh > 0
                                           ? (float)G.vw / (float)G.vh
                                           : (float)ww / (float)(wh > 0 ? wh : 1);
                        Mat4 proj = m4_perspective(DEG2RAD(60.0f), aspect, 0.1f, 500.0f);
                        G.vp = m4_mul(proj, make_view());
                        renderer_set_view_proj(G.vp);
                        renderer_batch_reset();
                        for (int i = 0; i < G.scene.count; i++) {
                            Entity *e = &G.scene.e[i];
                            int sel = (G.mode == MODE_EDIT && (i == G.selected || group_has(i)));
                            if (!e->active) continue;
                            if (G.mode == MODE_PLAY) {
                                if (i == G.pl.player) continue;          /* first person: no body in view */
                                if (!pl_entity_visible(i)) continue;     /* baked PVS */
                            }
                            if (e->mesh == MESH_OBJ && e->mesh_path[0]) {
                                int m = assets_load_obj(e->mesh_path);
                                if (m >= 0) { renderer_draw_obj(m, e, sel); continue; }
                            }
                            if (!sel && e->body == BODY_STATIC && (e->mesh == MESH_CUBE || e->mesh == MESH_SPHERE)
                                && !e->tex_path[0] && !e->script[0] && !e->controller && e->follow_dist <= 0) {
                                renderer_batch_add(e);
                                continue;
                            }
                            renderer_draw_entity(e, sel);
                        }
                        renderer_batch_flush();
                        if (G.mode == MODE_EDIT) draw_editor_overlay();
                    }
                    if (G.mode == MODE_MENU) {
                        gui_frame(&G.menu);
                    } else if (G.mode == MODE_EDIT) {
                        gui_sync_out(&ed);
                        gui_editor_frame(&ed);
                        gui_sync_in(&ed);
                    } else {
                        GuiHud hud;
                        PlayMode *pl = &G.pl;
                        memset(&hud, 0, sizeof hud);
                        snprintf(hud.fps, sizeof hud.fps, "%.0f fps  %s  render x1",
                                 pl->fps, (G.has_oc && !G.oc_stale && G.boc.vis) ? "[static-merged +PVS]" : "[static-merged]");
                        for (int i = 0; i < pl->nmsg && i < 5; i++)
                            snprintf(hud.msg[i], sizeof hud.msg[i], "%s", pl->msg[pl->nmsg - 1 - i]);
                        hud.nmsg = pl->nmsg;
                        if (pl->target >= 0) snprintf(hud.hint, sizeof hud.hint, "[E] interact");
                        hud.nleft = pl->nleft;
                        hud.nright = pl->nright;
                        for (int i = 0; i < pl->nleft; i++) snprintf(hud.left[i], sizeof hud.left[i], "%s", pl->left[i]);
                        for (int i = 0; i < pl->nright; i++) snprintf(hud.right[i], sizeof hud.right[i], "%s", pl->right[i]);
                        hud.health = pl->health;
                        hud.dead = pl->dead_wait > 0.0f ? pl->dead_wait : 0.0f;
                        hud.crosshair = 1;
                        gui_hud_frame(&hud);
                    }
                }
            }
            window_swap(G.win);

            frames++;
            if (now - fps_t >= 0.5) {
                const char *sel = (G.mode == MODE_EDIT && G.selected >= 0 && G.selected < G.scene.count)
                                      ? G.scene.e[G.selected].name : "-";
                G.pl.fps = (float)((double)frames / (now - fps_t));
                snprintf(title, sizeof title, "Viper Engine v0.4 | %s | %s | %s | selected: %s | %.0f fps",
                         G.mode == MODE_MENU ? "BROWSER" : G.mode == MODE_EDIT ? "EDIT" : "PLAY",
                         G.mode == MODE_EDIT ? (G.tab == 0 ? "Import" : G.tab == 1 ? "Map" : G.tab == 2 ? "Bake OC" :
                                                G.tab == 3 ? "Lighting" : G.tab == 4 ? "Scripts" : "Marketplace") : "",
                         G.scene.name, sel, frames / (now - fps_t));
                window_set_title(G.win, title);
                fps_t = now; frames = 0;
            }
        }
    }
    gui_shutdown();
    window_destroy(G.win);
    return 0;
}
