#ifndef VIPER_ASSETS_H
#define VIPER_ASSETS_H
/* OBJ + texture loading (v0.3). Uses stb_image, fetched via CMake. */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AObj {
    unsigned vao, vbo;
    int count;
    char path[128];
    float *cpu;   /* interleaved pos+nrm, stride 6 — kept for the bakers */
    int ncpu;
} AObj;

#define ASSETS_MAX_MESH 32
extern AObj assets_meshes[ASSETS_MAX_MESH];
extern int assets_nmeshes;

int assets_load_texture(const char *path); /* returns OpenGL texture id (0 on failure) */
int assets_load_obj(const char *path);     /* returns mesh id (>=0) or -1 on failure */
int assets_find_obj(const char *path);     /* lookup only: no GL, safe on worker threads */
int assets_mesh_cpu(int id, const float **data, int *nfloats); /* CPU triangles for baking */
void assets_shutdown(void);

#ifdef __cplusplus
}
#endif
#endif
