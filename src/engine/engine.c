#include "engine/engine.h"
#include "core/log.h"
#include "core/vmath.h"
#include "platform/window.h"
#include "gfx/renderer.h"
#include "scene/scene.h"
#include "physics/physics.h"
#include "blocks/blocks.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

typedef enum { MODE_EDIT, MODE_PLAY } Mode;

static struct {
    Window *win;
    Scene scene, snapshot;
    Mode mode;
    int selected;
    Vec3 cam_pos; float cam_yaw, cam_pitch, play_pitch;
    char scene_path[256];
} G;

static Vec3 cam_forward(void) {
    return v3(-sinf(G.cam_yaw) * cosf(G.cam_pitch), sinf(G.cam_pitch), -cosf(G.cam_yaw) * cosf(G.cam_pitch));
}
static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void select_next(void) {
    for (int k = 1; k <= G.scene.count; k++) {
        int i = (G.selected + k) % G.scene.count;
        if (G.scene.e[i].active) { G.selected = i; return; }
    }
}

static void add_primitive(MeshType m) {
    char name[32]; snprintf(name, sizeof name, "%s_%d", m == MESH_CUBE ? "Cube" : "Sphere", G.scene.count);
    Entity *e = scene_add(&G.scene, name);
    if (!e) return;
    Vec3 p = v3_add(G.cam_pos, v3_mul(cam_forward(), 6.0f));
    e->pos = v3(roundf(p.x * 2) / 2, roundf(p.y * 2) / 2, roundf(p.z * 2) / 2);
    e->spawn = e->pos; e->mesh = m;
    e->body = m == MESH_CUBE ? BODY_STATIC : BODY_DYNAMIC;
    float t = (float)G.scene.count;
    e->color = v3(0.4f + 0.5f * fabsf(sinf(t)), 0.4f + 0.5f * fabsf(sinf(t * 1.7f)), 0.4f + 0.5f * fabsf(sinf(t * 2.3f)));
    G.selected = (int)(e - G.scene.e);
}

static void enter_play(void) {
    G.snapshot = G.scene;
    for (int i = 0; i < G.scene.count; i++) { G.scene.e[i].spawn = G.scene.e[i].pos; G.scene.e[i].vel = v3(0, 0, 0); }
    blocks_bind_scene(&G.scene);
    blocks_reset_vars();
    G.mode = MODE_PLAY; G.play_pitch = 0.25f;
    blocks_update_scene(&G.scene, window_input(G.win), 0.0f, BLOCKS_EV_START);
    VLOG("PLAY");
}
static void exit_play(void) {
    G.scene = G.snapshot;
    G.mode = MODE_EDIT;
    window_set_captured(G.win, 0);
    VLOG("STOP (scene restored)");
}

static void edit_update(const Input *in, float dt) {
    window_set_captured(G.win, in->rmb);
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
    if (in->pressed[VKEY_TAB] && G.scene.count) select_next();
    if (in->pressed['1']) add_primitive(MESH_CUBE);
    if (in->pressed['2']) add_primitive(MESH_SPHERE);
    if (in->pressed[VKEY_F2]) {
        if (scene_save(&G.scene, G.scene_path)) VLOG("saved %s", G.scene_path);
        else VLOG("could not save %s", G.scene_path);
    }
    if (G.selected < 0 || G.selected >= G.scene.count || !G.scene.e[G.selected].active) return;
    Entity *e = &G.scene.e[G.selected];
    const float step = 0.5f;
    if (in->pressed[VKEY_LEFT]) e->pos.x -= step;
    if (in->pressed[VKEY_RIGHT]) e->pos.x += step;
    if (in->pressed[VKEY_UP]) e->pos.z -= step;
    if (in->pressed[VKEY_DOWN]) e->pos.z += step;
    if (in->pressed[VKEY_PGUP]) e->pos.y += step;
    if (in->pressed[VKEY_PGDN]) e->pos.y -= step;
    if (in->pressed['R']) e->rot.y += 15.0f;
    if (in->pressed[VKEY_DELETE]) { e->active = 0; select_next(); }
}

static void play_update(const Input *in, float dt) {
    window_set_captured(G.win, 1);
    G.play_pitch = clampf(G.play_pitch + in->mouse_dy * 0.003f, -0.1f, 1.2f);
    blocks_update_scene(&G.scene, in, dt, BLOCKS_EV_UPDATE);
    blocks_update_scene(&G.scene, in, dt, BLOCKS_EV_KEYS);
    physics_update(&G.scene, dt);
}

static Mat4 make_view(void) {
    if (G.mode == MODE_PLAY) {
        for (int i = 0; i < G.scene.count; i++) {
            const Entity *e = &G.scene.e[i];
            if (!e->active || e->follow_dist <= 0) continue;
            float yaw = DEG2RAD(e->rot.y);
            Vec3 fwd = v3(-sinf(yaw), 0, -cosf(yaw));
            Vec3 eye = v3_add(e->pos, v3_add(v3_mul(fwd, -e->follow_dist * cosf(G.play_pitch)),
                                             v3(0, e->follow_height + e->follow_dist * sinf(G.play_pitch), 0)));
            return m4_look_at(eye, v3_add(e->pos, v3(0, 0.8f, 0)), v3(0, 1, 0));
        }
    }
    return m4_look_at(G.cam_pos, v3_add(G.cam_pos, cam_forward()), v3(0, 1, 0));
}

int engine_run(const char *scene_path) {
    memset(&G, 0, sizeof G);
    snprintf(G.scene_path, sizeof G.scene_path, "%s", scene_path);
    G.win = window_create(1280, 720, "Viper Engine");
    if (!G.win) return 1;
    if (!renderer_init()) { VLOG("renderer failed to initialise"); window_destroy(G.win); return 1; }
    if (!scene_load(&G.scene, G.scene_path)) {
        VLOG("could not read %s - starting with an empty floor", G.scene_path);
        scene_clear(&G.scene);
        Entity *f = scene_add(&G.scene, "Floor");
        f->pos = v3(0, -0.5f, 0); f->scale = v3(40, 1, 40); f->checker = 1; f->body = BODY_STATIC;
        f->color = v3(0.35f, 0.55f, 0.4f);
    }
    blocks_bind_scene(&G.scene);
    G.cam_pos = v3(0, 6, 14); G.cam_pitch = -0.35f;
    VLOG("Viper Engine v0.1 ready. EDIT: hold RMB + WASD/QE to fly | TAB select | arrows/PgUp/PgDn move | R rotate | 1/2 add | F2 save | F5 play");

    double last = window_time(), fps_t = last; int frames = 0; char title[256] = "Viper Engine";
    while (!window_should_close(G.win)) {
        window_poll(G.win);
        const Input *in = window_input(G.win);
        double now = window_time();
        float dt = (float)(now - last); last = now;
        if (dt > 0.05f) dt = 0.05f;

        if (in->pressed[VKEY_F5]) { if (G.mode == MODE_EDIT) enter_play(); else exit_play(); }
        else if (in->pressed[VKEY_ESC]) { if (G.mode == MODE_PLAY) exit_play(); else window_request_close(G.win); }
        if (G.mode == MODE_EDIT) edit_update(in, dt); else play_update(in, dt);

        int w, h; window_framebuffer_size(G.win, &w, &h);
        if (w > 0 && h > 0) {
            renderer_begin(w, h, G.mode == MODE_PLAY ? v3(0.45f, 0.65f, 0.9f) : v3(0.18f, 0.2f, 0.25f));
            Mat4 proj = m4_perspective(DEG2RAD(60.0f), (float)w / (float)h, 0.1f, 500.0f);
            renderer_set_view_proj(m4_mul(proj, make_view()));
            for (int i = 0; i < G.scene.count; i++)
                if (G.scene.e[i].active) renderer_draw_entity(&G.scene.e[i], G.mode == MODE_EDIT && i == G.selected);
        }
        window_swap(G.win);

        frames++;
        if (now - fps_t >= 0.5) {
            const char *sel = (G.mode == MODE_EDIT && G.selected < G.scene.count) ? G.scene.e[G.selected].name : "-";
            snprintf(title, sizeof title, "Viper Engine v0.1 | %s | %s | selected: %s | %.0f fps",
                     G.mode == MODE_EDIT ? "EDIT" : "PLAY", G.scene.name, sel, frames / (now - fps_t));
            window_set_title(G.win, title);
            fps_t = now; frames = 0;
        }
    }
    window_destroy(G.win);
    return 0;
}
