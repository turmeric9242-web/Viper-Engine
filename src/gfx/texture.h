#ifndef VIPER_TEXTURE_H
#define VIPER_TEXTURE_H
/* GL texture cache. Needs a current GL context and vgl_load(). */
unsigned texture_get(const char *name);   /* GL id for textures/<name>.png|bmp|tga or a built-in; 0 = none/missing */
unsigned texture_upload(const unsigned char *rgba, int w, int h, int repeat, int smooth);
#endif
