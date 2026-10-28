#ifndef VIPER_BAKE_H
#define VIPER_BAKE_H
/* Offline bakers: lightmap (top-down heightfield, sun sweep + AO + denoise),
   occlusion PVS (cell grid, eye-height ray tests) and a light probe grid.
   All three run on a worker thread and report progress / honour cancel. */
#include "scene/scene.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { Vec3 a, b, c; } Tri;
typedef struct { Tri *t; int n, cap; } TriSoup;

/* Static world tessellated to world-space triangles (assets must be loaded). */
void bake_world_tris(const Scene *s, TriSoup *out);
void bake_free_tris(TriSoup *s);
unsigned int bake_signature(const Scene *s);

typedef void (*BakeProgress)(float frac, void *ud);

/* ---------------- lightmap ---------------- */
typedef struct {
    int w, h;
    float minx, minz;   /* world rect origin */
    float sizex, sizez; /* world rect size */
    unsigned char *rgb; /* w*h*3 */
} BakedLightmap;

int  bake_lightmap(const Scene *s, const TriSoup *tris, BakeProgress progress, void *ud,
                   volatile int *cancel, BakedLightmap *out);
void bake_lightmap_free(BakedLightmap *lm);

/* ---------------- occlusion PVS ---------------- */
typedef struct {
    int nx, ny, nz, n_units, bytes;
    float minx, miny, minz, cell, cell_y;
    unsigned char *vis; /* (nx*ny*nz) * bytes */
} BakedOcclusion;

int  bake_occlusion(const Scene *s, const TriSoup *tris, BakeProgress progress, void *ud,
                    volatile int *cancel, BakedOcclusion *out);
void bake_occlusion_free(BakedOcclusion *oc);
int  bake_occlusion_visible(const BakedOcclusion *oc, Vec3 eye, int entity);

/* ---------------- light probes ---------------- */
typedef struct {
    int nx, ny, nz;
    float minx, miny, minz;
    float sizex, sizey, sizez;
    unsigned char *rgb; /* nx*ny*nz*3 */
} BakedProbes;

int  bake_probes(const Scene *s, const TriSoup *tris, BakeProgress progress, void *ud,
                 volatile int *cancel, BakedProbes *out);
void bake_probes_free(BakedProbes *p);

/* ---------------- files (raw binary next to the .vscene) ---------------- */
int bake_save_lightmap(const char *path, const BakedLightmap *lm, unsigned int sig);
int bake_load_lightmap(const char *path, BakedLightmap *lm, unsigned int sig, int *stale);
int bake_save_occlusion(const char *path, const BakedOcclusion *oc, unsigned int sig);
int bake_load_occlusion(const char *path, BakedOcclusion *oc, unsigned int sig, int *stale);
int bake_save_probes(const char *path, const BakedProbes *p, unsigned int sig);
int bake_load_probes(const char *path, BakedProbes *p, unsigned int sig, int *stale);

#ifdef __cplusplus
}
#endif
#endif
