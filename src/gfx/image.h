#ifndef VIPER_IMAGE_H
#define VIPER_IMAGE_H
#include <stddef.h>
/* CPU-side images: PNG / BMP / TGA decoders written from scratch (no external libs) plus procedural
   built-in textures. Everything returns tightly packed RGBA8, top row first. free() the result. */
unsigned char *image_load(const char *path, int *w, int *h);                        /* by file contents */
unsigned char *image_decode_png(const unsigned char *d, size_t n, int *w, int *h);

int  image_builtin_count(void);
const char *image_builtin_name(int i);
unsigned char *image_builtin(const char *name, int *w, int *h);                     /* NULL if unknown */

/* Texture lookup used by the engine: textures/<name>.png|bmp|tga wins over a built-in of the same name. */
unsigned char *image_load_texture(const char *name, int *w, int *h);
int  image_list_textures(char names[][32], int max);                                /* built-ins + files, unique */
int  image_texture_name_ok(const char *name);                                       /* no paths / odd characters */
#endif
