/* Headless logic test: scene loading, Viper Blocks VM, physics. Run from project root. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "scene/scene.h"
#include "physics/physics.h"
#include "blocks/blocks.h"
#include "gfx/image.h"
#include "platform/fs.h"
#include "engine/menu.h"
#include <stdlib.h>

static int fails = 0;
#define CHECK(cond, msg) do { if (cond) printf("PASS %s\n", msg); else { printf("FAIL %s\n", msg); fails++; } } while (0)

static Scene scene;
static Input in;

static void frame(float dt) {
    blocks_update_scene(&scene, &in, dt, BLOCKS_EV_UPDATE);
    blocks_update_scene(&scene, &in, dt, BLOCKS_EV_KEYS);
    physics_update(&scene, dt);
    memset(in.pressed, 0, sizeof in.pressed);
    in.mouse_dx = in.mouse_dy = 0;
}
static Entity *find(const char *n) {
    for (int i = 0; i < scene.count; i++) if (!strcmp(scene.e[i].name, n)) return &scene.e[i];
    return NULL;
}


static int same_file_pixels(const char *name) {
    char p[160], q[160];
    snprintf(p, sizeof p, "tests/data/%s", name); snprintf(q, sizeof q, "%s", p);
    char *dot = strrchr(q, '.'); strcpy(dot, ".rgba");
    int w = 0, h = 0; unsigned char *im = image_load(p, &w, &h);
    FILE *f = fopen(q, "rb");
    if (!im || !f) { if (f) fclose(f); free(im); return 0; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *ex = (unsigned char *)malloc((size_t)n);
    size_t rd = fread(ex, 1, (size_t)n, f); fclose(f);
    int ok = rd == (size_t)n && (long)w * h * 4 == n && memcmp(im, ex, (size_t)n) == 0;
    free(im); free(ex);
    return ok;
}

static void test_images(void) {
    static const char *files[] = {"gray1.png","gray2.png","gray4.png","gray8.png","gray16.png","rgb8.png","rgb16.png",
        "rgba8.png","ga8.png","pal1.png","pal2.png","pal4.png","pal8.png","pil_rgb.png","pil_rgba.png",
        "bmp24.bmp","bmp8.bmp","tga24.tga","tga24rle.tga","tga32.tga","tga8.tga","tga8rle.tga"};
    int ok = 1;
    for (unsigned i = 0; i < sizeof files / sizeof files[0]; i++)
        if (!same_file_pixels(files[i])) { printf("  image mismatch: %s\n", files[i]); ok = 0; }
    CHECK(ok, "PNG (all colour types/bit depths/filters), BMP and TGA (raw + RLE) decode pixel-exact");
    int w, h;
    CHECK(image_load("tests/data/does_not_exist.png", &w, &h) == NULL, "missing image returns NULL");
    CHECK(image_load("scenes/start.vscene", &w, &h) == NULL, "non-image file is rejected cleanly");
    int all = 1;
    for (int i = 0; i < image_builtin_count(); i++) {
        unsigned char *b = image_builtin(image_builtin_name(i), &w, &h);
        if (!b || w != 64 || h != 64) all = 0;
        free(b);
    }
    CHECK(all && image_builtin_count() >= 8, "all built-in textures generate (64x64)");
    CHECK(image_builtin("nope", &w, &h) == NULL && image_texture_name_ok("brick") && !image_texture_name_ok("../x") && !image_texture_name_ok("a/b"),
          "texture names are validated (no path tricks)");
    unsigned char *t = image_load_texture("brick", &w, &h);
    CHECK(t != NULL, "image_load_texture finds a built-in"); free(t);
    char names[64][32]; int n = image_list_textures(names, 64), has_grass = 0;
    for (int i = 0; i < n; i++) if (!strcmp(names[i], "grass")) has_grass = 1;
    CHECK(n >= image_builtin_count() && has_grass, "texture list contains the built-ins");
}

static void test_fs_and_scene_textures(void) {
    static const char *const ex[] = {"vblocks", NULL};
    char names[16][32];
    int n = fs_list("scripts", ex, names, 16);
    CHECK(n == 2 && !strcmp(names[0], "Player") && !strcmp(names[1], "Spinner"), "fs_list finds scripts (sorted, no extension)");
    CHECK(fs_list("no_such_dir", ex, names, 16) == 0, "fs_list on a missing folder returns 0");
    Scene s; CHECK(scene_load(&s, "scenes/start.vscene"), "start scene loads (textured)");
    Entity *fl = NULL; for (int i = 0; i < s.count; i++) if (!strcmp(s.e[i].name, "Floor")) fl = &s.e[i];
    CHECK(fl && !strcmp(fl->texture, "grass") && fabsf(fl->tile - 0.5f) < 1e-6f, "scene parses 'texture' and 'tile'");
    snprintf(fl->texture, sizeof fl->texture, "brick"); fl->tile = 3;
    Scene r; CHECK(scene_save(&s, "viper_test_tex.tmp") && scene_load(&r, "viper_test_tex.tmp"), "textured scene saves and reloads");
    Entity *rf = NULL; for (int i = 0; i < r.count; i++) if (!strcmp(r.e[i].name, "Floor")) rf = &r.e[i];
    CHECK(rf && !strcmp(rf->texture, "brick") && fabsf(rf->tile - 3.0f) < 1e-6f, "texture + tile survive a save/load round trip");
    Entity *none = scene_add(&r, "Plain");
    CHECK(none && none->texture[0] == 0 && none->tile == 1.0f, "new entities start untextured with tile 1");
}

static void test_menu(void) {
    Menu m; Input in; memset(&in, 0, sizeof in);
    menu_begin(&m, "Test");
    for (int i = 0; i < 40; i++) { char b[16]; snprintf(b, sizeof b, "item%d", i); menu_add(&m, b); }
    menu_show(&m, 5);
    CHECK(m.open && m.cursor == 5 && m.current == 5, "menu opens with the cursor on the assigned item");
    { Menu far = m; menu_show(&far, 35); menu_update(&far, &in, 0.016f, 1280, 720);
      MenuLayout FL; menu_layout(&far, 1280, 720, &FL);
      CHECK(far.cursor == 35 && far.scroll <= 35 && far.scroll + FL.rows > 35, "menu opens scrolled so the assigned item is visible"); }
    in.pressed[VKEY_DOWN] = 1; menu_update(&m, &in, 0.016f, 1280, 720); in.pressed[VKEY_DOWN] = 0;
    CHECK(m.cursor == 6, "Down moves the cursor");
    in.pressed[VKEY_ENTER] = 1;
    CHECK(menu_update(&m, &in, 0.016f, 1280, 720) == MENU_CHOSEN && m.chosen == 6 && !m.open, "Enter picks the highlighted item and closes");
    in.pressed[VKEY_ENTER] = 0;
    menu_show(&m, 0);
    in.pressed[VKEY_END] = 1; menu_update(&m, &in, 0.016f, 1280, 720); in.pressed[VKEY_END] = 0;
    MenuLayout L; menu_layout(&m, 1280, 720, &L);
    CHECK(m.cursor == 39 && m.scroll + L.rows > 39 && m.scroll <= 39, "End jumps to the last item and scrolls it into view");
    in.scroll = 1; menu_update(&m, &in, 0.016f, 1280, 720); in.scroll = 0;
    CHECK(m.scroll >= 0, "mouse wheel scrolls without going out of range");
    in.scroll = 50; menu_update(&m, &in, 0.016f, 1280, 720); in.scroll = 0;
    CHECK(m.scroll == 0 && m.cursor == 39, "wheel-scrolling to the top keeps the view there (no snap back to the cursor)");
    /* click on the 3rd visible row */
    in.mouse_x = L.lx + 20; in.mouse_y = L.ly + L.rowh * 2.5f; in.lmb_pressed = 1;
    CHECK(menu_update(&m, &in, 0.016f, 1280, 720) == MENU_CHOSEN && m.chosen == 2, "clicking a row picks it");
    in.lmb_pressed = 0;
    menu_show(&m, 0); in.pressed[VKEY_ESC] = 1;
    CHECK(menu_update(&m, &in, 0.016f, 1280, 720) == MENU_CLOSED && !m.open, "Esc closes without choosing");
    in.pressed[VKEY_ESC] = 0;
    menu_show(&m, 0); in.mouse_x = 2; in.mouse_y = 2; in.lmb_pressed = 1;
    CHECK(menu_update(&m, &in, 0.016f, 1280, 720) == MENU_CLOSED, "clicking outside the panel closes it");
    CHECK(input_key_from_name("F3") == VKEY_F3, "F3 is a known key name for scripts");
}

int main(void) {
    CHECK(scene_load(&scene, "scenes/start.vscene"), "scene loads");
    CHECK(scene.count == 7, "scene has 7 entities");
    blocks_bind_scene(&scene);
    Entity *p = find("Player"), *ball = find("Ball"), *spin = find("Spinner");
    CHECK(p && p->script_id >= 0, "Player script compiled");
    CHECK(spin && spin->script_id >= 0, "Spinner script (if/else/nesting) compiled");
    blocks_reset_vars();
    blocks_update_scene(&scene, &in, 1.0f / 60, BLOCKS_EV_START);

    for (int i = 0; i < 90; i++) frame(1.0f / 60);
    CHECK(fabsf(p->pos.y - 0.9f) < 0.01f && p->grounded, "player lands on floor and is grounded");
    CHECK(fabsf(ball->pos.y - 0.6f) < 0.01f, "ball falls and rests on floor");

    float z0 = p->pos.z; in.down['W'] = 1;
    for (int i = 0; i < 60; i++) frame(1.0f / 60);
    CHECK(p->pos.z < z0 - 5.0f && p->pos.z > z0 - 7.0f, "W walks forward ~6 units/s (-Z)");
    in.down['W'] = 0;
    float x0 = p->pos.x; in.down['D'] = 1; in.down['W'] = 1;
    for (int i = 0; i < 60; i++) frame(1.0f / 60);
    float dist = sqrtf((p->pos.x - x0) * (p->pos.x - x0));
    CHECK(dist > 3.5f && dist < 4.6f, "diagonal movement is normalised (not 1.41x)");
    in.down['D'] = in.down['W'] = 0;

    for (int i = 0; i < 30; i++) frame(1.0f / 60);
    float maxy = p->pos.y; in.pressed[' '] = 1;
    for (int i = 0; i < 90; i++) { frame(1.0f / 60); if (p->pos.y > maxy) maxy = p->pos.y; }
    CHECK(maxy > 0.9f + 1.4f, "SPACE jumps ~1.6 units");
    CHECK(fabsf(p->pos.y - 0.9f) < 0.01f, "player lands again after jump");

    float yaw0 = p->rot.y; in.mouse_dx = 100; frame(1.0f / 60);
    CHECK(fabsf((p->rot.y - yaw0) + 12.0f) < 0.01f, "mouse_look turns player (-12 deg for dx=100)");

    Entity *crate = find("Crate"); p->pos = v3(crate->pos.x - 4, 0.9f, crate->pos.z); p->vel = v3(0, 0, 0);
    p->rot.y = -90.0f; in.down['W'] = 1;
    for (int i = 0; i < 120; i++) frame(1.0f / 60);
    CHECK(p->pos.x < crate->pos.x - 0.75f - 0.5f + 0.01f, "player is blocked by the crate");
    in.down['W'] = 0;

    Entity *fl = find("Floor"); fl->active = 0; p->pos = v3(0, 2, 0);
    int fell = 0, respawned = 0;
    for (int i = 0; i < 400; i++) {
        frame(1.0f / 60);
        if (p->pos.y < -30.0f) fell = 1;
        if (fell && p->pos.y > 0.0f) respawned = 1;
    }
    CHECK(respawned, "falling out of the world respawns the player at its spawn point");

    CHECK(scene_save(&scene, "viper_test_roundtrip.tmp"), "scene saves");
    Scene s2; CHECK(scene_load(&s2, "viper_test_roundtrip.tmp") && s2.count == 6, "saved scene reloads (inactive entity dropped)");

    test_images();
    test_fs_and_scene_textures();
    test_menu();

    remove("viper_test_roundtrip.tmp"); remove("viper_test_tex.tmp");
    printf(fails ? "\n%d TEST(S) FAILED\n" : "\nALL TESTS PASSED\n", fails);
    return fails != 0;
}
