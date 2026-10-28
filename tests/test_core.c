/* Headless logic test: scene loading, Viper Blocks VM, physics. Run from project root. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "scene/scene.h"
#include "physics/physics.h"
#include "blocks/blocks.h"

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

    CHECK(scene_save(&scene, "/tmp/viper_roundtrip.vscene"), "scene saves");
    Scene s2; CHECK(scene_load(&s2, "/tmp/viper_roundtrip.vscene") && s2.count == 6, "saved scene reloads (inactive entity dropped)");

    printf(fails ? "\n%d TEST(S) FAILED\n" : "\nALL TESTS PASSED\n", fails);
    return fails != 0;
}
