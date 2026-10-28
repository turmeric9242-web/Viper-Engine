#ifndef VIPER_ASSETS_H
#define VIPER_ASSETS_H
/* OBJ + texture loading (v0.3). Uses stb_image, fetched via CMake. */

#ifdef __cplusplus
extern "C" {
#endif

int assets_load_texture(const char *path); /* returns OpenGL texture id (0 on failure) */
int assets_load_obj(const char *path);     /* returns mesh id (>=0) or -1 on failure */
void assets_shutdown(void);

#ifdef __cplusplus
}
#endif
#endif
