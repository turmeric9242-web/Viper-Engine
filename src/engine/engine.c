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
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>

typedef enum { MODE_MENU, MODE_EDIT, MODE_PLAY } Mode;

static struct {
    Window *win;
    Scene scene, snapshot;
    Mode mode;
    int selected;
    Vec3 cam_pos; float cam_yaw, cam_pitch, play_pitch;
    char scene_path[256];
    char argv0[480];
    GuiMenu menu; /* state owned by C; touched by gui_frame each menu frame */
} G;

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
        p->pos = v3(0, 1, 0); p->scale = v3(0.8f, 1.6f, 0.8f);
        p->body = BODY_DYNAMIC; p->controller = 1;
        snprintf(p->script, sizeof p->script, "Player");
        p->follow_dist = 6.0f; p->follow_height = 2.0f;
        p->color = v3(0.85f, 0.3f, 0.25f);
    }
}

static void edit_update(const Input *in, float dt);
static void play_update(const Input *in, float dt);
static void exit_play(void);
static void menu_refresh(void);

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
    if (in->pressed['M']) { G.mode = MODE_MENU; menu_refresh(); window_set_captured(G.win, 0); return; }
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

/* ---------------- project browser ---------------- */

static void menu_refresh(void) {
    fs_mkdir("projects");
    G.menu.nprojects = fs_list_dirs("projects", G.menu.projects, VIP_MENU_MAX);
    fs_getcwd(G.menu.root, sizeof G.menu.root);
    {
        size_t n = strlen(G.menu.root);
        if (n + 10 < sizeof G.menu.root) snprintf(G.menu.root + n, sizeof G.menu.root - n, "/projects");
    }
    for (int i = 0; i < G.menu.nprojects; i++) { /* count entities per project scene */
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
    G.selected = -1;
    G.cam_pos = v3(0, 6, 14); G.cam_pitch = -0.35f;
    G.mode = MODE_EDIT;
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
    if (fs_is_dir("scripts")) { /* give new projects copies of the starter blocks */
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
    snprintf(bat, sizeof bat, "%s/run.bat", dir);
    f = fopen(bat, "w");
    if (f) {
        fprintf(f, "@echo off\r\nstart \"Viper - %s\" viper.exe %s.vscene\r\n", name, name);
        fclose(f);
    }
    statusf("compiled to %s - zip the folder to share your game", dir);
    VLOG("exported projects/%s -> %s", name, dir);
}

/* ---------------- main loop ---------------- */

int engine_run(const char *scene_path, const char *argv0) {
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
    if (G.scene_path[0]) {
        if (!scene_load(&G.scene, G.scene_path)) {
            VLOG("could not read %s - starting with a fresh floor", G.scene_path);
            make_floor_scene(&G.scene);
        }
        blocks_bind_scene(&G.scene);
        G.selected = -1;
        G.cam_pos = v3(0, 6, 14); G.cam_pitch = -0.35f;
        G.mode = MODE_EDIT;
        VLOG("v0.3 ready. EDIT: hold RMB + WASD/QE to fly | TAB select | arrows/PgUp/PgDn move | R rotate | 1/2 add | M browser | F2 save | F5 play | GUI panels both sides");
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
                if (in->pressed[VKEY_F5]) { if (G.mode == MODE_EDIT) enter_play(); else exit_play(); }
                else if (in->pressed[VKEY_ESC]) { if (G.mode == MODE_PLAY) exit_play(); else window_request_close(G.win); }
                if (G.mode == MODE_EDIT) edit_update(in, dt); else play_update(in, dt);
            }

            {
                int w, h;
                window_framebuffer_size(G.win, &w, &h);
                if (w > 0 && h > 0) {
                    renderer_begin(w, h,
                                   G.mode == MODE_PLAY ? v3(0.45f, 0.65f, 0.9f) :
                                   G.mode == MODE_MENU ? v3(0.10f, 0.11f, 0.16f) : v3(0.18f, 0.2f, 0.25f));
                    if (G.mode != MODE_MENU) {
                        Mat4 proj = m4_perspective(DEG2RAD(60.0f), (float)w / (float)h, 0.1f, 500.0f);
                        renderer_set_view_proj(m4_mul(proj, make_view()));
                        renderer_batch_reset();
                        for (int i = 0; i < G.scene.count; i++) {
                            Entity *e = &G.scene.e[i];
                            int sel = (G.mode == MODE_EDIT && i == G.selected);
                            if (!e->active || sel) continue;
                            if (e->mesh == MESH_OBJ && e->mesh_path[0]) {
                                int m = assets_load_obj(e->mesh_path);
                                if (m >= 0) { renderer_draw_obj(m, e, 0); continue; }
                            }
                            if (e->body == BODY_STATIC && (e->mesh == MESH_CUBE || e->mesh == MESH_SPHERE)
                                && !e->script[0] && !e->controller && e->follow_dist <= 0) {
                                renderer_batch_add(e); /* merged static world: 1 draw call */
                                continue;
                            }
                            renderer_draw_entity(e, 0);
                        }
                        renderer_batch_flush();
                        if (G.mode == MODE_EDIT && G.selected >= 0 && G.selected < G.scene.count) {
                            Entity *s = &G.scene.e[G.selected];
                            if (s->active) {
                                if (s->mesh == MESH_OBJ && s->mesh_path[0]) {
                                    int m = assets_load_obj(s->mesh_path);
                                    if (m >= 0) renderer_draw_obj(m, s, 1);
                                    else renderer_draw_entity(s, 1);
                                } else renderer_draw_entity(s, 1);
                            }
                        }
                    }
                    if (G.mode == MODE_MENU) {
                        gui_frame(&G.menu);
                    } else if (G.mode == MODE_EDIT) {
                        memset(&ed, 0, sizeof ed);
                        ed.scene = &G.scene;
                        ed.selected = G.selected;
                        gui_editor_frame(&ed);
                        if (ed.save_pressed) {
                            if (scene_save(&G.scene, G.scene_path)) VLOG("saved %s", G.scene_path);
                            else VLOG("could not save %s", G.scene_path);
                        }
                        if (ed.duplicate_pressed && G.selected >= 0 && G.selected < G.scene.count) {
                            Entity *src = &G.scene.e[G.selected];
                            Entity *dst = scene_add(&G.scene, src->name);
                            if (dst) { *dst = *src; dst->pos.x += 1.0f; }
                        }
                        if (ed.delete_pressed && G.selected >= 0 && G.selected < G.scene.count) {
                            G.scene.e[G.selected].active = 0;
                            G.selected = -1;
                        }
                        G.selected = ed.selected;
                    }
                }
            }
            window_swap(G.win);

            frames++;
            if (now - fps_t >= 0.5) {
                const char *sel = (G.mode == MODE_EDIT && G.selected >= 0 && G.selected < G.scene.count)
                                      ? G.scene.e[G.selected].name : "-";
                snprintf(title, sizeof title, "Viper Engine v0.3 | %s | %s | selected: %s | %.0f fps",
                         G.mode == MODE_MENU ? "BROWSER" : G.mode == MODE_EDIT ? "EDIT" : "PLAY",
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
