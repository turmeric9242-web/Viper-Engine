#ifndef VIPER_RENDERER_H
#define VIPER_RENDERER_H
#include "core/vmath.h"
#include "scene/scene.h"
int  renderer_init(void);                       /* needs a current GL context */
void renderer_begin(int w, int h, Vec3 sky);    /* viewport + clear */
void renderer_set_view_proj(Mat4 view_proj);
void renderer_draw_entity(const Entity *e, int highlight);
#endif
