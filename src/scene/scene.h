#ifndef VIPER_SCENE_H
#define VIPER_SCENE_H
#include "core/vmath.h"
#define MAX_ENTITIES 256

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { MESH_NONE, MESH_CUBE, MESH_SPHERE, MESH_OBJ } MeshType;
typedef enum { BODY_NONE, BODY_STATIC, BODY_DYNAMIC } BodyType;

typedef struct {
    char name[32];
    int active;
    Vec3 pos, rot /* degrees, yaw = rot.y */, scale, color;
    MeshType mesh;
    char mesh_path[64];     /* for MESH_OBJ: assets/ or project assets/ */
    char tex_path[64];
    int checker;
    BodyType body;
    int controller;         /* dynamic body driven by 'walk' blocks */
    Vec3 vel, spawn, input_vel;
    float input_max;
    int grounded;
    char script[32];        /* scripts/<name>.vblocks */
    int script_id;
    float follow_dist, follow_height; /* >0 => game camera follows this entity */
} Entity;

typedef struct {
    char name[64];
    Entity e[MAX_ENTITIES];
    int count;
} Scene;

void scene_clear(Scene *s);
Entity *scene_add(Scene *s, const char *name);
int scene_load(Scene *s, const char *path);
int scene_save(const Scene *s, const char *path);
Mat4 entity_model(const Entity *e);
Mat4 entity_normal_matrix(const Entity *e);

#ifdef __cplusplus
}
#endif
#endif
