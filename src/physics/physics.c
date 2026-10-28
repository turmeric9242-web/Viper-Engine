#include "physics/physics.h"
#include <math.h>

#define GRAVITY 25.0f
#define EPS 1e-4f
#define SUBSTEP (1.0f / 120.0f)

static float *ax(Vec3 *v, int i) { return i == 0 ? &v->x : (i == 1 ? &v->y : &v->z); }

static int overlap(Vec3 ap, Vec3 ah, Vec3 bp, Vec3 bh) {
    return fabsf(ap.x - bp.x) < ah.x + bh.x - EPS &&
           fabsf(ap.y - bp.y) < ah.y + bh.y - EPS &&
           fabsf(ap.z - bp.z) < ah.z + bh.z - EPS;
}

static void step(Scene *s, float dt) {
    for (int i = 0; i < s->count; i++) {
        Entity *d = &s->e[i];
        if (!d->active || d->body != BODY_DYNAMIC) continue;
        Vec3 ah = v3_mul(d->scale, 0.5f);
        d->grounded = 0;
        if (d->controller) {
            Vec3 iv = d->input_vel;
            float l = sqrtf(iv.x * iv.x + iv.z * iv.z);
            if (l > d->input_max && l > 1e-6f) { float k = d->input_max / l; iv.x *= k; iv.z *= k; }
            d->vel.x = iv.x; d->vel.z = iv.z;
        }
        d->vel.y -= GRAVITY * dt;
        for (int a = 0; a < 3; a++) {
            float delta = *ax(&d->vel, a) * dt;
            *ax(&d->pos, a) += delta;
            for (int j = 0; j < s->count; j++) {
                Entity *b = &s->e[j];
                if (!b->active || b->body != BODY_STATIC) continue;
                Vec3 bh = v3_mul(b->scale, 0.5f);
                if (!overlap(d->pos, ah, b->pos, bh)) continue;
                float bp = *ax(&b->pos, a), bhh = *ax(&bh, a), ahh = *ax(&ah, a);
                if (delta > 0) *ax(&d->pos, a) = bp - bhh - ahh;
                else if (delta < 0) { *ax(&d->pos, a) = bp + bhh + ahh; if (a == 1) d->grounded = 1; }
                else continue;
                *ax(&d->vel, a) = 0;
            }
        }
        if (d->pos.y < -40.0f) { d->pos = d->spawn; d->vel = v3(0, 0, 0); }
    }
}

void physics_update(Scene *s, float dt) {
    int n = (int)ceilf(dt / SUBSTEP);
    if (n < 1) n = 1;
    if (n > 8) n = 8;
    for (int i = 0; i < n; i++) step(s, dt / (float)n);
    for (int i = 0; i < s->count; i++) { s->e[i].input_vel = v3(0, 0, 0); s->e[i].input_max = 0; }
}
