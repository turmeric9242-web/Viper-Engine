#include "scene/scene.h"
#include "core/log.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static const char *MESH_NAMES[] = {"none", "cube", "sphere"};
static const char *BODY_NAMES[] = {"none", "static", "dynamic"};

static void copy_str(char *dst, size_t n, const char *src) {
    strncpy(dst, src, n - 1); dst[n - 1] = 0;
}
static int lookup(const char **names, int n, const char *s) {
    for (int i = 0; i < n; i++) if (strcmp(names[i], s) == 0) return i;
    return -1;
}

void scene_clear(Scene *s) { memset(s, 0, sizeof *s); copy_str(s->name, sizeof s->name, "untitled"); }

Entity *scene_add(Scene *s, const char *name) {
    if (s->count >= MAX_ENTITIES) return NULL;
    Entity *e = &s->e[s->count++];
    memset(e, 0, sizeof *e);
    copy_str(e->name, sizeof e->name, name);
    e->active = 1;
    e->scale = v3(1, 1, 1);
    e->color = v3(0.8f, 0.8f, 0.8f);
    e->mesh = MESH_CUBE;
    e->script_id = -1;
    return e;
}

int scene_load(Scene *s, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    scene_clear(s);
    char line[256]; Entity *cur = NULL; int ln = 0;
    while (fgets(line, sizeof line, f)) {
        ln++;
        char *hash = strchr(line, '#'); if (hash) *hash = 0;
        char *key = line; while (isspace((unsigned char)*key)) key++;
        char *end = key + strlen(key);
        while (end > key && isspace((unsigned char)end[-1])) *--end = 0;
        if (!*key) continue;
        char *rest = key; while (*rest && !isspace((unsigned char)*rest)) rest++;
        if (*rest) { *rest++ = 0; while (isspace((unsigned char)*rest)) rest++; }
        if (!strcmp(key, "scene")) { copy_str(s->name, sizeof s->name, rest); continue; }
        if (!strcmp(key, "entity")) {
            cur = scene_add(s, *rest ? rest : "Entity");
            if (!cur) VLOG("%s:%d: too many entities", path, ln);
            continue;
        }
        if (!cur) { VLOG("%s:%d: '%s' outside entity", path, ln, key); continue; }
        if (!strcmp(key, "mesh")) { int m = lookup(MESH_NAMES, 3, rest); if (m >= 0) cur->mesh = (MeshType)m; }
        else if (!strcmp(key, "body")) { int b = lookup(BODY_NAMES, 3, rest); if (b >= 0) cur->body = (BodyType)b; }
        else if (!strcmp(key, "pos")) { sscanf(rest, "%f %f %f", &cur->pos.x, &cur->pos.y, &cur->pos.z); cur->spawn = cur->pos; }
        else if (!strcmp(key, "rot")) sscanf(rest, "%f %f %f", &cur->rot.x, &cur->rot.y, &cur->rot.z);
        else if (!strcmp(key, "scale")) sscanf(rest, "%f %f %f", &cur->scale.x, &cur->scale.y, &cur->scale.z);
        else if (!strcmp(key, "color")) sscanf(rest, "%f %f %f", &cur->color.x, &cur->color.y, &cur->color.z);
        else if (!strcmp(key, "checker")) cur->checker = atoi(rest);
        else if (!strcmp(key, "controller")) cur->controller = atoi(rest);
        else if (!strcmp(key, "script")) copy_str(cur->script, sizeof cur->script, rest);
        else if (!strcmp(key, "follow")) sscanf(rest, "%f %f", &cur->follow_dist, &cur->follow_height);
        else VLOG("%s:%d: unknown property '%s'", path, ln, key);
    }
    fclose(f);
    return 1;
}

int scene_save(const Scene *s, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return 0;
    fprintf(f, "scene %s\n", s->name);
    for (int i = 0; i < s->count; i++) {
        const Entity *e = &s->e[i];
        if (!e->active) continue;
        fprintf(f, "\nentity %s\n  mesh %s\n  pos %g %g %g\n  rot %g %g %g\n  scale %g %g %g\n  color %g %g %g\n",
                e->name, MESH_NAMES[e->mesh], e->pos.x, e->pos.y, e->pos.z, e->rot.x, e->rot.y, e->rot.z,
                e->scale.x, e->scale.y, e->scale.z, e->color.x, e->color.y, e->color.z);
        if (e->checker) fprintf(f, "  checker 1\n");
        if (e->body) fprintf(f, "  body %s\n", BODY_NAMES[e->body]);
        if (e->controller) fprintf(f, "  controller 1\n");
        if (e->script[0]) fprintf(f, "  script %s\n", e->script);
        if (e->follow_dist > 0) fprintf(f, "  follow %g %g\n", e->follow_dist, e->follow_height);
    }
    fclose(f);
    return 1;
}

static Mat4 entity_rotation(const Entity *e) {
    return m4_mul(m4_rot_y(DEG2RAD(e->rot.y)), m4_mul(m4_rot_x(DEG2RAD(e->rot.x)), m4_rot_z(DEG2RAD(e->rot.z))));
}
Mat4 entity_model(const Entity *e) {
    return m4_mul(m4_translate(e->pos), m4_mul(entity_rotation(e), m4_scale(e->scale)));
}
Mat4 entity_normal_matrix(const Entity *e) {
    Vec3 inv = v3(1.0f / fmaxf(fabsf(e->scale.x), 1e-4f), 1.0f / fmaxf(fabsf(e->scale.y), 1e-4f),
                  1.0f / fmaxf(fabsf(e->scale.z), 1e-4f));
    return m4_mul(entity_rotation(e), m4_scale(inv));
}
