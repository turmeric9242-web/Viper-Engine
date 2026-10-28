#ifndef VIPER_RENDERER_H
#define VIPER_RENDERER_H
#include "core/vmath.h"
#include "scene/scene.h"
int  renderer_init(void);                       /* needs a current GL context */
void renderer_begin(int w, int h, Vec3 sky);    /* viewport + clear */
/* render 3D into a sub-rect (editor viewport); vx/vy are window pixels, top-left origin */
void renderer_begin_view(int fbw, int fbh, int vx, int vy, int vw, int vh, Vec3 bg, Vec3 sky);
/* line list: nverts * (x,y,z,r,g,b) — gizmo, selection boxes, ghost, grid */
void renderer_lines(const float *verts, int nverts, int depth_test);

/* baked lighting: world-space lightmap + light probe grid */
void renderer_set_lightmap(int w, int h, const unsigned char *rgb,
                           float minx, float minz, float sizex, float sizez);
void renderer_clear_lightmap(void);
void renderer_set_probes(int nx, int ny, int nz, const unsigned char *rgb,
                         float minx, float miny, float minz, float vx, float vy, float vz);
void renderer_clear_probes(void);
void renderer_set_view_proj(Mat4 view_proj);    /* also extracts frustum planes for culling */
void renderer_set_camera(Vec3 eye);             /* fog falloff origin */
void renderer_set_fog(float start, float end);  /* huge end = fog off */
int  renderer_entity_visible(const Entity *e);  /* frustum cull test (1 = draw it) */
void renderer_draw_entity(const Entity *e, int highlight);
void renderer_draw_obj(int mesh_id, const Entity *e, int highlight);

/* Merged static world: bake every static cube/sphere into one VBO,1 draw call. */
void renderer_batch_reset(void);                /* call once per frame before adds */
void renderer_batch_add(const Entity *e);       /* ignores culled / non-cube-sphere entities */
void renderer_batch_flush(void);                /* uploads + draws the whole batch */
#endif
