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
void renderer_set_view_proj(Mat4 view_proj);    /* also extracts frustum planes for culling */
int  renderer_entity_visible(const Entity *e);  /* frustum cull test (1 = draw it) */
void renderer_draw_entity(const Entity *e, int highlight);
void renderer_draw_obj(int mesh_id, const Entity *e, int highlight);

/* Merged static world: bake every static cube/sphere into one VBO,1 draw call. */
void renderer_batch_reset(void);                /* call once per frame before adds */
void renderer_batch_add(const Entity *e);       /* ignores culled / non-cube-sphere entities */
void renderer_batch_flush(void);                /* uploads + draws the whole batch */
#endif
