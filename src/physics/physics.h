#ifndef VIPER_PHYSICS_H
#define VIPER_PHYSICS_H
#include "scene/scene.h"
/* v1: gravity + axis-aligned box collision of dynamic bodies against static bodies.
   Rotation is ignored for collision; spheres collide as boxes. */
void physics_update(Scene *s, float dt);
#endif
