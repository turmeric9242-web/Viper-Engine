#include "bake/bake.h"
#include "gfx/assets.h"
#include "core/log.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

#define BAKE_MAX_TRIS 200000
#define SUNC {1.0f, 0.94f, 0.82f}
#define SKYC {0.55f, 0.65f, 0.90f}

/* ------------------------------------------------------------ helpers */

static Vec3 xform(Mat4 m, Vec3 p) {
    return v3(m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12],
              m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13],
              m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14]);
}
static Vec3 sun_dir(const Scene *s) {
    float az = DEG2RAD(s->sun_az), el = DEG2RAD(s->sun_el);
    return v3(cosf(el) * sinf(az), sinf(el), cosf(el) * cosf(az));
}
static unsigned mix_bytes(unsigned h, const void *p, size_t n) {
    const unsigned char *b = (const unsigned char *)p;
    while (n--) { h ^= *b++; h *= 16777619u; }
    return h;
}

static void soup_push(TriSoup *s, Vec3 a, Vec3 b, Vec3 c) {
    if (s->n >= s->cap) {
        int cap = s->cap ? s->cap * 2 : 8192;
        Tri *t;
        if (cap > BAKE_MAX_TRIS) cap = BAKE_MAX_TRIS;
        if (cap <= s->cap) return;
        t = (Tri *)realloc(s->t, (size_t)cap * sizeof(Tri));
        if (!t) return;
        s->t = t;
        s->cap = cap;
    }
    s->t[s->n].a = a; s->t[s->n].b = b; s->t[s->n].c = c;
    s->n++;
}

unsigned int bake_signature(const Scene *s) {
    unsigned h = 2166136261u;
    int count = 0;
    for (int i = 0; i < s->count; i++)
        if (s->e[i].active) count++;
    h = mix_bytes(h, &count, sizeof count);
    for (int i = 0; i < s->count; i++) {
        const Entity *e = &s->e[i];
        if (!e->active) continue;
        h = mix_bytes(h, &e->pos, sizeof e->pos);
        h = mix_bytes(h, &e->rot, sizeof e->rot);
        h = mix_bytes(h, &e->scale, sizeof e->scale);
        h = mix_bytes(h, &e->mesh, sizeof e->mesh);
        h = mix_bytes(h, e->mesh_path, strlen(e->mesh_path));
    }
    return h;
}

/* Static geometry as world triangles: cubes, spheres and OBJ meshes. */
void bake_world_tris(const Scene *s, TriSoup *out) {
    memset(out, 0, sizeof *out);
    for (int i = 0; i < s->count; i++) {
        const Entity *e = &s->e[i];
        if (!e->active || e->body != BODY_STATIC) continue;
        Mat4 m = entity_model(e);
        if (e->mesh == MESH_CUBE) {
            static const Vec3 N[6] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
            static const Vec3 U[6] = {{0,1,0},{0,0,1},{0,0,1},{1,0,0},{1,0,0},{0,1,0}};
            static const Vec3 V[6] = {{0,0,1},{0,1,0},{1,0,0},{0,0,1},{0,1,0},{1,0,0}};
            for (int f = 0; f < 6; f++) {
                Vec3 c = v3_mul(N[f], 0.5f), u = v3_mul(U[f], 0.5f), v = v3_mul(V[f], 0.5f);
                Vec3 p0 = xform(m, v3_sub(v3_sub(c, u), v));
                Vec3 p1 = xform(m, v3_sub(v3_add(c, u), v));
                Vec3 p2 = xform(m, v3_add(v3_add(c, u), v));
                Vec3 p3 = xform(m, v3_add(v3_sub(c, u), v));
                soup_push(out, p0, p1, p2);
                soup_push(out, p0, p2, p3);
            }
        } else if (e->mesh == MESH_SPHERE) {
            enum { ST = 16, SL = 24 };
            for (int a = 0; a < ST; a++) for (int b = 0; b < SL; b++) {
                float a0 = VIPER_PI * a / ST, a1 = VIPER_PI * (a + 1) / ST;
                float b0 = 2 * VIPER_PI * b / SL, b1 = 2 * VIPER_PI * (b + 1) / SL;
                Vec3 p00 = v3(sinf(a0) * cosf(b0), cosf(a0), sinf(a0) * sinf(b0));
                Vec3 p01 = v3(sinf(a0) * cosf(b1), cosf(a0), sinf(a0) * sinf(b1));
                Vec3 p10 = v3(sinf(a1) * cosf(b0), cosf(a1), sinf(a1) * sinf(b0));
                Vec3 p11 = v3(sinf(a1) * cosf(b1), cosf(a1), sinf(a1) * sinf(b1));
                soup_push(out, xform(m, v3_mul(p00, .5f)), xform(m, v3_mul(p10, .5f)), xform(m, v3_mul(p11, .5f)));
                soup_push(out, xform(m, v3_mul(p00, .5f)), xform(m, v3_mul(p11, .5f)), xform(m, v3_mul(p01, .5f)));
            }
        } else if (e->mesh == MESH_OBJ) {
            int id = assets_find_obj(e->mesh_path);
            const float *d = NULL;
            int n = 0;
            if (id >= 0 && assets_mesh_cpu(id, &d, &n) && n >= 24) {
                for (int k = 0; k + 23 < n; k += 24) {
                    Vec3 a = xform(m, v3(d[k + 0], d[k + 1], d[k + 2]));
                    Vec3 b = xform(m, v3(d[k + 8], d[k + 9], d[k + 10]));
                    Vec3 c = xform(m, v3(d[k + 16], d[k + 17], d[k + 18]));
                    soup_push(out, a, b, c);
                }
            }
        }
    }
}

void bake_free_tris(TriSoup *s) { free(s->t); s->t = NULL; s->n = s->cap = 0; }

/* ------------------------------------------------------------ heightmap */

typedef struct {
    int w, h;
    float minx, minz, sx, sz;
    float *z;
    unsigned char *has;
} HMap;

static void hmap_free(HMap *m) { free(m->z); free(m->has); m->z = NULL; m->has = NULL; }

static void hmap_build(HMap *m, const TriSoup *t, int w, int h,
                       float minx, float minz, float sizex, float sizez) {
    memset(m, 0, sizeof *m);
    m->w = w; m->h = h;
    m->minx = minx; m->minz = minz;
    m->sx = sizex / (float)w; m->sz = sizez / (float)h;
    m->z = (float *)malloc(sizeof(float) * (size_t)w * (size_t)h);
    m->has = (unsigned char *)calloc((size_t)w * (size_t)h, 1);
    if (!m->z || !m->has) { hmap_free(m); return; }
    for (int i = 0; i < w * h; i++) m->z[i] = -1e30f;

    for (int ti = 0; ti < t->n; ti++) {
        const Tri *tr = &t->t[ti];
        float ax = tr->a.x, az = tr->a.z, bx = tr->b.x, bz = tr->b.z, cx = tr->c.x, cz = tr->c.z;
        float minX = fminf(ax, fminf(bx, cx)), maxX = fmaxf(ax, fmaxf(bx, cx));
        float minZ = fminf(az, fminf(bz, cz)), maxZ = fmaxf(az, fmaxf(bz, cz));
        int x0 = (int)floorf((minX - minx) / m->sx) - 1, x1 = (int)ceilf((maxX - minx) / m->sx) + 1;
        int z0 = (int)floorf((minZ - minz) / m->sz) - 1, z1 = (int)ceilf((maxZ - minz) / m->sz) + 1;
        if (x0 < 0) x0 = 0;
        if (z0 < 0) z0 = 0;
        if (x1 >= w) x1 = w - 1;
        if (z1 >= h) z1 = h - 1;
        {
            float det = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz);
            if (fabsf(det) < 1e-9f) continue;
            float inv = 1.0f / det;
            for (int iz = z0; iz <= z1; iz++) {
                float pz = minz + ((float)iz + 0.5f) * m->sz;
                for (int ix = x0; ix <= x1; ix++) {
                    float px = minx + ((float)ix + 0.5f) * m->sx;
                    float l1 = ((bz - cz) * (px - cx) + (cx - bx) * (pz - cz)) * inv;
                    float l2 = ((cz - az) * (px - cx) + (ax - cx) * (pz - cz)) * inv;
                    float l3 = 1.0f - l1 - l2;
                    float y;
                    int idx;
                    if (l1 < -0.002f || l2 < -0.002f || l3 < -0.002f) continue;
                    y = l1 * tr->a.y + l2 * tr->b.y + l3 * tr->c.y;
                    idx = iz * w + ix;
                    if (y > m->z[idx]) { m->z[idx] = y; m->has[idx] = 1; }
                }
            }
        }
    }
}

/* 1 = sun reaches this point, 0 = in shadow (heightfield march) */
static int sun_visible(const HMap *m, float x, float z, float y, Vec3 sun) {
    float lxz = sqrtf(sun.x * sun.x + sun.z * sun.z);
    float ux, uz, slope, step;
    if (lxz < 1e-4f) return 1;
    ux = sun.x / lxz; uz = sun.z / lxz;
    slope = sun.y / lxz;
    step = fmaxf(m->sx, m->sz) * 1.5f;
    for (int k = 1; k <= 110; k++) {
        float d = (float)k * step;
        float px = x + ux * d, pz = z + uz * d;
        int ix = (int)((px - m->minx) / m->sx), iz = (int)((pz - m->minz) / m->sz);
        int idx;
        if (ix < 0 || ix >= m->w || iz < 0 || iz >= m->h) break;
        idx = iz * m->w + ix;
        if (m->has[idx] && m->z[idx] > y + slope * d + 0.05f) return 0;
    }
    return 1;
}

/* 1 = open, 0 = fully enclosed (12 directions, 2 radii) */
static float ao_open(const HMap *m, float x, float z, float y, float radius) {
    int occ = 0, total = 0;
    if (radius <= 0.0f) return 1.0f;
    for (int a = 0; a < 12; a++) {
        float ang = 2.0f * VIPER_PI * (float)a / 12.0f;
        for (int r = 1; r <= 2; r++) {
            float d = radius * 0.5f * (float)r;
            float px = x + cosf(ang) * d, pz = z + sinf(ang) * d;
            int ix = (int)((px - m->minx) / m->sx), iz = (int)((pz - m->minz) / m->sz);
            total++;
            if (ix < 0 || ix >= m->w || iz < 0 || iz >= m->h) continue;
            {
                int idx = iz * m->w + ix;
                if (m->has[idx] && m->z[idx] > y + 0.12f) occ++;
            }
        }
    }
    return total ? 1.0f - (float)occ / (float)total : 1.0f;
}

static void blur3(float *img, int w, int h) {
    float *tmp = (float *)malloc(sizeof(float) * (size_t)w * (size_t)h);
    if (!tmp) return;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int xa = x > 0 ? x - 1 : x, xb = x + 1 < w ? x + 1 : x;
            tmp[y * w + x] = (img[y * w + xa] + img[y * w + x] + img[y * w + xb]) / 3.0f;
        }
    }
    for (int y = 0; y < h; y++) {
        int ya = y > 0 ? y - 1 : y, yb = y + 1 < h ? y + 1 : y;
        for (int x = 0; x < w; x++)
            img[y * w + x] = (tmp[ya * w + x] + tmp[y * w + x] + tmp[yb * w + x]) / 3.0f;
    }
    free(tmp);
}

/* ------------------------------------------------------------- lightmap */

int bake_lightmap(const Scene *s, const TriSoup *tris, BakeProgress progress, void *ud,
                  volatile int *cancel, BakedLightmap *out) {
    float minx = 1e30f, maxx = -1e30f, minz = 1e30f, maxz = -1e30f;
    int res;
    HMap hm;
    float *sunv = NULL, *aov = NULL;
    unsigned char *ok = NULL;

    memset(out, 0, sizeof *out);
    if (!tris || tris->n <= 0) return 0;
    for (int i = 0; i < tris->n; i++) {
        const Tri *t = &tris->t[i];
        const Vec3 *p[3] = {&t->a, &t->b, &t->c};
        for (int k = 0; k < 3; k++) {
            if (p[k]->x < minx) minx = p[k]->x;
            if (p[k]->x > maxx) maxx = p[k]->x;
            if (p[k]->z < minz) minz = p[k]->z;
            if (p[k]->z > maxz) maxz = p[k]->z;
        }
    }
    minx -= 1.0f; minz -= 1.0f; maxx += 1.0f; maxz += 1.0f;
    res = s->lm_res;
    if (res < 64) res = 64;
    if (res > 1024) res = 1024;

    out->w = res; out->h = res;
    out->minx = minx; out->minz = minz;
    out->sizex = maxx - minx; out->sizez = maxz - minz;

    hmap_build(&hm, tris, res, res, minx, minz, out->sizex, out->sizez);
    if (!hm.z || !hm.has) { hmap_free(&hm); return 0; }

    sunv = (float *)malloc(sizeof(float) * (size_t)res * (size_t)res);
    aov = (float *)malloc(sizeof(float) * (size_t)res * (size_t)res);
    ok = (unsigned char *)malloc((size_t)res * (size_t)res);
    out->rgb = (unsigned char *)malloc((size_t)res * (size_t)res * 3);
    if (!sunv || !aov || !ok || !out->rgb) {
        free(sunv); free(aov); free(ok); hmap_free(&hm);
        bake_lightmap_free(out);
        return 0;
    }

    {
        Vec3 sun = sun_dir(s);
        float ao_rad = s->ao_radius > 0.0f ? s->ao_radius : 3.0f;
        for (int iz = 0; iz < res; iz++) {
            if (cancel && *cancel) { free(sunv); free(aov); free(ok); hmap_free(&hm); bake_lightmap_free(out); return 0; }
            for (int ix = 0; ix < res; ix++) {
                int idx = iz * res + ix;
                if (!hm.has[idx]) { sunv[idx] = 0.0f; aov[idx] = 1.0f; continue; }
                {
                    float x = minx + ((float)ix + 0.5f) * hm.sx;
                    float z = minz + ((float)iz + 0.5f) * hm.sz;
                    float y = hm.z[idx];
                    sunv[idx] = sun_visible(&hm, x, z, y, sun) ? 1.0f : 0.0f;
                    aov[idx] = ao_open(&hm, x, z, y, ao_rad);
                }
            }
            if (progress) progress(0.55f * (float)(iz + 1) / (float)res, ud);
        }
    }

    blur3(sunv, res, res);
    blur3(aov, res, res);
    if (progress) progress(0.7f, ud);

    {
        float sunc[3] = SUNC, skyc[3] = SKYC;
        float ambient = s->ambient < 0.0f ? 0.0f : s->ambient;
        float ao_strength = s->ao_strength;
        if (ao_strength < 0.0f) ao_strength = 0.0f;
        if (ao_strength > 1.0f) ao_strength = 1.0f;
        for (int i = 0; i < res * res; i++) {
            float a, sc;
            if (!hm.has[i]) { ok[i] = 0; out->rgb[i * 3] = 0; out->rgb[i * 3 + 1] = 0; out->rgb[i * 3 + 2] = 0; continue; }
            ok[i] = 1;
            sc = sunv[i];
            a = 1.0f - (1.0f - aov[i]) * ao_strength;
            for (int c = 0; c < 3; c++) {
                float v = sc * sunc[c] + ambient * a * skyc[c];
                if (v > 1.0f) v = 1.0f;
                if (v < 0.0f) v = 0.0f;
                out->rgb[i * 3 + c] = (unsigned char)(v * 255.0f + 0.5f);
            }
        }
    }

    /* dilate empty texels so walls and corners still sample something sane */
    for (int pass = 0; pass < 4; pass++) {
        int changed = 0;
        for (int iz = 0; iz < res; iz++) for (int ix = 0; ix < res; ix++) {
            int idx = iz * res + ix;
            int sum[3] = {0, 0, 0}, n = 0;
            if (ok[idx]) continue;
            for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
                int nx = ix + dx, nz = iz + dz;
                if (nx < 0 || nx >= res || nz < 0 || nz >= res) continue;
                if (!ok[nz * res + nx]) continue;
                sum[0] += out->rgb[(nz * res + nx) * 3 + 0];
                sum[1] += out->rgb[(nz * res + nx) * 3 + 1];
                sum[2] += out->rgb[(nz * res + nx) * 3 + 2];
                n++;
            }
            if (n) {
                out->rgb[idx * 3 + 0] = (unsigned char)(sum[0] / n);
                out->rgb[idx * 3 + 1] = (unsigned char)(sum[1] / n);
                out->rgb[idx * 3 + 2] = (unsigned char)(sum[2] / n);
                ok[idx] = 2;
                changed = 1;
            }
        }
        for (int i = 0; i < res * res; i++) if (ok[i] == 2) ok[i] = 1;
        if (!changed) break;
    }

    free(sunv); free(aov); free(ok);
    hmap_free(&hm);
    if (progress) progress(1.0f, ud);
    return 1;
}

void bake_lightmap_free(BakedLightmap *lm) {
    if (!lm) return;
    free(lm->rgb);
    lm->rgb = NULL;
    lm->w = lm->h = 0;
}

/* ------------------------------------------------------------ occlusion */

static int seg_blocked(Vec3 a, Vec3 b, const Entity *skip, const Scene *s, int skip_i) {
    float t0, t1;
    Vec3 d = v3_sub(b, a);
    for (int k = 0; k < s->count; k++) {
        const Entity *o = &s->e[k];
        Vec3 lo, hi;
        if (k == skip_i || !o->active || o->body != BODY_STATIC) continue;
        (void)skip;
        lo = v3(o->pos.x - fabsf(o->scale.x) * 0.5f, o->pos.y - fabsf(o->scale.y) * 0.5f, o->pos.z - fabsf(o->scale.z) * 0.5f);
        hi = v3(o->pos.x + fabsf(o->scale.x) * 0.5f, o->pos.y + fabsf(o->scale.y) * 0.5f, o->pos.z + fabsf(o->scale.z) * 0.5f);
        t0 = 0.0f; t1 = 1.0f;
        {
            const float oo[3] = {a.x, a.y, a.z}, dd[3] = {d.x, d.y, d.z};
            const float l[3] = {lo.x, lo.y, lo.z}, h[3] = {hi.x, hi.y, hi.z};
            int hit = 1;
            for (int ax = 0; ax < 3 && hit; ax++) {
                if (fabsf(dd[ax]) < 1e-9f) {
                    if (oo[ax] < l[ax] || oo[ax] > h[ax]) hit = 0;
                    continue;
                }
                {
                    float u = (l[ax] - oo[ax]) / dd[ax], v = (h[ax] - oo[ax]) / dd[ax];
                    if (u > v) { float tmp = u; u = v; v = tmp; }
                    if (u > t0) t0 = u;
                    if (v < t1) t1 = v;
                    if (t0 > t1) hit = 0;
                }
            }
            if (hit && t0 < 0.985f && t1 > 0.015f) return 1;
        }
    }
    return 0;
}

int bake_occlusion(const Scene *s, const TriSoup *tris, BakeProgress progress, void *ud,
                   volatile int *cancel, BakedOcclusion *out) {
    float minx = 1e30f, maxx = -1e30f, miny = 1e30f, maxy = -1e30f, minz = 1e30f, maxz = -1e30f;
    int any = 0;
    (void)tris;
    memset(out, 0, sizeof *out);
    for (int i = 0; i < s->count; i++) {
        const Entity *e = &s->e[i];
        if (!e->active || e->body != BODY_STATIC) continue;
        any = 1;
        if (e->pos.x - fabsf(e->scale.x) * 0.5f < minx) minx = e->pos.x - fabsf(e->scale.x) * 0.5f;
        if (e->pos.x + fabsf(e->scale.x) * 0.5f > maxx) maxx = e->pos.x + fabsf(e->scale.x) * 0.5f;
        if (e->pos.y - fabsf(e->scale.y) * 0.5f < miny) miny = e->pos.y - fabsf(e->scale.y) * 0.5f;
        if (e->pos.y + fabsf(e->scale.y) * 0.5f > maxy) maxy = e->pos.y + fabsf(e->scale.y) * 0.5f;
        if (e->pos.z - fabsf(e->scale.z) * 0.5f < minz) minz = e->pos.z - fabsf(e->scale.z) * 0.5f;
        if (e->pos.z + fabsf(e->scale.z) * 0.5f > maxz) maxz = e->pos.z + fabsf(e->scale.z) * 0.5f;
    }
    if (!any) return 0;

    {
        float cell = s->oc_cell > 0.25f ? s->oc_cell : 4.0f;
        float cy = s->oc_cell_y > 0.25f ? s->oc_cell_y : 2.5f;
        int nx = (int)ceilf((maxx - minx) / cell);
        int nz = (int)ceilf((maxz - minz) / cell);
        int ny = (int)ceilf((maxy - miny) / cy);
        if (nx < 1) nx = 1;
        if (nz < 1) nz = 1;
        if (ny < 1) ny = 1;
        if (nx > 48) nx = 48;
        if (nz > 48) nz = 48;
        if (ny > 8) ny = 8;
        /* keep the bitmask cell count sane */
        while ((long)nx * nz * ny > 40000) {
            if (nx > nz) nx--; else nz--;
            if (nx < 1 || nz < 1) break;
        }
        out->nx = nx; out->ny = ny; out->nz = nz;
        out->n_units = s->count;
        out->bytes = (s->count + 7) / 8;
        out->minx = minx; out->miny = miny; out->minz = minz;
        out->cell = cell; out->cell_y = cy;
        out->vis = (unsigned char *)calloc((size_t)(nx * ny * nz) * (size_t)out->bytes, 1);
        if (!out->vis) { out->nx = 0; return 0; }
    }

    {
        int total = out->nx * out->ny * out->nz, done = 0;
        for (int iy = 0; iy < out->ny; iy++) {
            for (int iz = 0; iz < out->nz; iz++) {
                for (int ix = 0; ix < out->nx; ix++) {
                    int cellidx = (iy * out->nz + iz) * out->nx + ix;
                    Vec3 eye = v3(out->minx + ((float)ix + 0.5f) * out->cell,
                                  out->miny + ((float)iy + 0.5f) * out->cell_y,
                                  out->minz + ((float)iz + 0.5f) * out->cell);
                    unsigned char *bits;
                    if (cancel && *cancel) { bake_occlusion_free(out); return 0; }
                    bits = out->vis + (size_t)cellidx * (size_t)out->bytes;
                    for (int j = 0; j < s->count; j++) {
                        const Entity *e = &s->e[j];
                        Vec3 tgt, offs[3];
                        int vis = 0;
                        if (!e->active || e->body != BODY_STATIC) continue;
                        tgt = e->pos;
                        offs[0] = v3(tgt.x, tgt.y + fabsf(e->scale.y) * 0.45f, tgt.z);
                        offs[1] = tgt;
                        offs[2] = v3(tgt.x, tgt.y - fabsf(e->scale.y) * 0.45f, tgt.z);
                        for (int k = 0; k < 3 && !vis; k++) {
                            if (v3_len(v3_sub(offs[k], eye)) < 0.05f) { vis = 1; break; }
                            if (!seg_blocked(eye, offs[k], e, s, j)) vis = 1;
                        }
                        if (vis) bits[j >> 3] |= (unsigned char)(1u << (j & 7));
                    }
                    done++;
                    if (progress && (done & 31) == 0) progress((float)done / (float)total, ud);
                }
            }
        }
    }
    if (progress) progress(1.0f, ud);
    return 1;
}

void bake_occlusion_free(BakedOcclusion *oc) {
    if (!oc) return;
    free(oc->vis);
    oc->vis = NULL;
    oc->nx = oc->ny = oc->nz = 0;
}

int bake_occlusion_visible(const BakedOcclusion *oc, Vec3 eye, int entity) {
    int ix, iy, iz, idx;
    size_t base;
    if (!oc || !oc->vis) return 1;
    if (entity < 0 || entity >= oc->n_units) return 1;
    ix = (int)((eye.x - oc->minx) / oc->cell);
    iz = (int)((eye.z - oc->minz) / oc->cell);
    iy = (int)((eye.y - oc->miny) / oc->cell_y);
    if (ix < 0 || ix >= oc->nx || iz < 0 || iz >= oc->nz || iy < 0 || iy >= oc->ny) return 1;
    idx = (iy * oc->nz + iz) * oc->nx + ix;
    base = (size_t)idx * (size_t)oc->bytes;
    return (oc->vis[base + (entity >> 3)] >> (entity & 7)) & 1;
}

/* --------------------------------------------------------------- probes */

int bake_probes(const Scene *s, const TriSoup *tris, BakeProgress progress, void *ud,
                volatile int *cancel, BakedProbes *out) {
    float minx = 1e30f, maxx = -1e30f, miny = 1e30f, maxy = -1e30f, minz = 1e30f, maxz = -1e30f;
    HMap hm;
    Vec3 sun;
    int nx, ny, nz, res = 256;
    float voxel;
    memset(out, 0, sizeof *out);
    if (!tris || tris->n <= 0) return 0;
    for (int i = 0; i < tris->n; i++) {
        const Tri *t = &tris->t[i];
        const Vec3 *p[3] = {&t->a, &t->b, &t->c};
        for (int k = 0; k < 3; k++) {
            if (p[k]->x < minx) minx = p[k]->x;
            if (p[k]->x > maxx) maxx = p[k]->x;
            if (p[k]->y < miny) miny = p[k]->y;
            if (p[k]->y > maxy) maxy = p[k]->y;
            if (p[k]->z < minz) minz = p[k]->z;
            if (p[k]->z > maxz) maxz = p[k]->z;
        }
    }
    minx -= 1.0f; miny -= 0.5f; minz -= 1.0f;
    maxx += 1.0f; maxy += 1.0f; maxz += 1.0f;
    voxel = s->probe_voxel > 0.4f ? s->probe_voxel : 2.0f;
    nx = (int)ceilf((maxx - minx) / voxel);
    ny = (int)ceilf((maxy - miny) / voxel);
    nz = (int)ceilf((maxz - minz) / voxel);
    if (nx < 1) nx = 1;
    if (ny < 1) ny = 1;
    if (nz < 1) nz = 1;
    while ((long)nx * ny * nz > 16000) {
        voxel *= 1.15f;
        nx = (int)ceilf((maxx - minx) / voxel);
        ny = (int)ceilf((maxy - miny) / voxel);
        nz = (int)ceilf((maxz - minz) / voxel);
    }
    out->nx = nx; out->ny = ny; out->nz = nz;
    out->minx = minx; out->miny = miny; out->minz = minz;
    out->sizex = (float)nx * voxel; out->sizey = (float)ny * voxel; out->sizez = (float)nz * voxel;
    out->rgb = (unsigned char *)malloc((size_t)nx * (size_t)ny * (size_t)nz * 3);
    if (!out->rgb) return 0;

    hmap_build(&hm, tris, res, res, minx, minz, maxx - minx, maxz - minz);
    if (!hm.z || !hm.has) { hmap_free(&hm); bake_probes_free(out); return 0; }

    sun = sun_dir(s);
    {
        float sunc[3] = SUNC, skyc[3] = SKYC;
        float ambient = s->ambient > 0.0f ? s->ambient : 0.4f;
        for (int iy = 0; iy < ny; iy++) {
            if (cancel && *cancel) { hmap_free(&hm); bake_probes_free(out); return 0; }
            for (int iz = 0; iz < nz; iz++) for (int ix = 0; ix < nx; ix++) {
                float x = minx + ((float)ix + 0.5f) * voxel;
                float y = miny + ((float)iy + 0.5f) * voxel;
                float z = minz + ((float)iz + 0.5f) * voxel;
                float sky = ao_open(&hm, x, z, y, voxel * 2.5f);
                float sv = sun_visible(&hm, x, z, y, sun) ? 1.0f : 0.0f;
                int idx = ((iy * nz + iz) * nx + ix) * 3;
                for (int c = 0; c < 3; c++) {
                    float v = skyc[c] * sky * ambient + sunc[c] * sv * 0.34f;
                    if (v > 1.0f) v = 1.0f;
                    if (v < 0.0f) v = 0.0f;
                    out->rgb[idx + c] = (unsigned char)(v * 255.0f + 0.5f);
                }
            }
            if (progress) progress((float)(iy + 1) / (float)ny, ud);
        }
    }
    hmap_free(&hm);
    if (progress) progress(1.0f, ud);
    return 1;
}

void bake_probes_free(BakedProbes *p) {
    if (!p) return;
    free(p->rgb);
    p->rgb = NULL;
    p->nx = p->ny = p->nz = 0;
}

/* ---------------------------------------------------------------- files */

typedef struct {
    char magic[4];
    unsigned int sig;
    int w, h;
    float minx, minz, sizex, sizez;
} LmHdr;
typedef struct {
    char magic[4];
    unsigned int sig;
    int nx, ny, nz, n_units, bytes;
    float minx, miny, minz, cell, cell_y;
} OcHdr;
typedef struct {
    char magic[4];
    unsigned int sig;
    int nx, ny, nz;
    float minx, miny, minz, sizex, sizey, sizez;
} PrHdr;

int bake_save_lightmap(const char *path, const BakedLightmap *lm, unsigned int sig) {
    LmHdr h;
    FILE *f;
    if (!lm || !lm->rgb) return 0;
    memset(&h, 0, sizeof h);
    memcpy(h.magic, "VLM1", 4);
    h.sig = sig; h.w = lm->w; h.h = lm->h;
    h.minx = lm->minx; h.minz = lm->minz; h.sizex = lm->sizex; h.sizez = lm->sizez;
    f = fopen(path, "wb");
    if (!f) return 0;
    fwrite(&h, sizeof h, 1, f);
    fwrite(lm->rgb, 1, (size_t)lm->w * (size_t)lm->h * 3, f);
    fclose(f);
    return 1;
}

int bake_load_lightmap(const char *path, BakedLightmap *lm, unsigned int sig, int *stale) {
    LmHdr h;
    FILE *f;
    memset(lm, 0, sizeof *lm);
    f = fopen(path, "rb");
    if (!f) return 0;
    if (fread(&h, sizeof h, 1, f) != 1 || memcmp(h.magic, "VLM1", 4) || h.w <= 0 || h.h <= 0 || h.w > 4096 || h.h > 4096) {
        fclose(f);
        return 0;
    }
    lm->w = h.w; lm->h = h.h;
    lm->minx = h.minx; lm->minz = h.minz; lm->sizex = h.sizex; lm->sizez = h.sizez;
    lm->rgb = (unsigned char *)malloc((size_t)h.w * (size_t)h.h * 3);
    if (!lm->rgb || fread(lm->rgb, 1, (size_t)h.w * (size_t)h.h * 3, f) != (size_t)h.w * (size_t)h.h * 3) {
        fclose(f);
        bake_lightmap_free(lm);
        return 0;
    }
    fclose(f);
    if (stale) *stale = h.sig != sig;
    return 1;
}

int bake_save_occlusion(const char *path, const BakedOcclusion *oc, unsigned int sig) {
    OcHdr h;
    FILE *f;
    if (!oc || !oc->vis) return 0;
    memset(&h, 0, sizeof h);
    memcpy(h.magic, "VPV1", 4);
    h.sig = sig;
    h.nx = oc->nx; h.ny = oc->ny; h.nz = oc->nz;
    h.n_units = oc->n_units; h.bytes = oc->bytes;
    h.minx = oc->minx; h.miny = oc->miny; h.minz = oc->minz;
    h.cell = oc->cell; h.cell_y = oc->cell_y;
    f = fopen(path, "wb");
    if (!f) return 0;
    fwrite(&h, sizeof h, 1, f);
    fwrite(oc->vis, 1, (size_t)(oc->nx * oc->ny * oc->nz) * (size_t)oc->bytes, f);
    fclose(f);
    return 1;
}

int bake_load_occlusion(const char *path, BakedOcclusion *oc, unsigned int sig, int *stale) {
    OcHdr h;
    FILE *f;
    size_t bytes;
    memset(oc, 0, sizeof *oc);
    f = fopen(path, "rb");
    if (!f) return 0;
    if (fread(&h, sizeof h, 1, f) != 1 || memcmp(h.magic, "VPV1", 4) ||
        h.nx <= 0 || h.ny <= 0 || h.nz <= 0 || h.nx > 256 || h.ny > 64 || h.nz > 256 || h.bytes <= 0) {
        fclose(f);
        return 0;
    }
    oc->nx = h.nx; oc->ny = h.ny; oc->nz = h.nz;
    oc->n_units = h.n_units; oc->bytes = h.bytes;
    oc->minx = h.minx; oc->miny = h.miny; oc->minz = h.minz;
    oc->cell = h.cell; oc->cell_y = h.cell_y;
    bytes = (size_t)(h.nx * h.ny * h.nz) * (size_t)h.bytes;
    oc->vis = (unsigned char *)malloc(bytes);
    if (!oc->vis || fread(oc->vis, 1, bytes, f) != bytes) {
        fclose(f);
        bake_occlusion_free(oc);
        return 0;
    }
    fclose(f);
    if (stale) *stale = h.sig != sig;
    return 1;
}

int bake_save_probes(const char *path, const BakedProbes *p, unsigned int sig) {
    PrHdr h;
    FILE *f;
    if (!p || !p->rgb) return 0;
    memset(&h, 0, sizeof h);
    memcpy(h.magic, "VPR1", 4);
    h.sig = sig;
    h.nx = p->nx; h.ny = p->ny; h.nz = p->nz;
    h.minx = p->minx; h.miny = p->miny; h.minz = p->minz;
    h.sizex = p->sizex; h.sizey = p->sizey; h.sizez = p->sizez;
    f = fopen(path, "wb");
    if (!f) return 0;
    fwrite(&h, sizeof h, 1, f);
    fwrite(p->rgb, 1, (size_t)p->nx * (size_t)p->ny * (size_t)p->nz * 3, f);
    fclose(f);
    return 1;
}

int bake_load_probes(const char *path, BakedProbes *p, unsigned int sig, int *stale) {
    PrHdr h;
    FILE *f;
    size_t bytes;
    memset(p, 0, sizeof *p);
    f = fopen(path, "rb");
    if (!f) return 0;
    if (fread(&h, sizeof h, 1, f) != 1 || memcmp(h.magic, "VPR1", 4) ||
        h.nx <= 0 || h.ny <= 0 || h.nz <= 0 || h.nx > 256 || h.ny > 256 || h.nz > 256) {
        fclose(f);
        return 0;
    }
    p->nx = h.nx; p->ny = h.ny; p->nz = h.nz;
    p->minx = h.minx; p->miny = h.miny; p->minz = h.minz;
    p->sizex = h.sizex; p->sizey = h.sizey; p->sizez = h.sizez;
    bytes = (size_t)h.nx * (size_t)h.ny * (size_t)h.nz * 3;
    p->rgb = (unsigned char *)malloc(bytes);
    if (!p->rgb || fread(p->rgb, 1, bytes, f) != bytes) {
        fclose(f);
        bake_probes_free(p);
        return 0;
    }
    fclose(f);
    if (stale) *stale = h.sig != sig;
    return 1;
}
