#include "gfx/texture.h"
#include "gfx/glload.h"
#include "gfx/image.h"
#include "core/log.h"
#include <stdlib.h>
#include <string.h>

typedef struct { char name[32]; unsigned id; } Entry;
static Entry cache[128];
static int ncache;

unsigned texture_upload(const unsigned char *rgba, int w, int h, int repeat, int smooth) {
    unsigned id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    int wrap = repeat ? GL_REPEAT : VGL_CLAMP_TO_EDGE;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, smooth ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST);
    /* small (<=128px) textures stay crisp when magnified, big ones are smoothed */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (smooth && w > 128) ? GL_LINEAR : GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    if (smooth) vgl.GenerateMipmap(GL_TEXTURE_2D);
    return id;
}

unsigned texture_get(const char *name) {
    if (!name || !name[0]) return 0;
    for (int i = 0; i < ncache; i++) if (!strcmp(cache[i].name, name)) return cache[i].id;
    if (ncache >= (int)(sizeof cache / sizeof cache[0])) return 0;
    Entry *e = &cache[ncache++];
    strncpy(e->name, name, sizeof e->name - 1); e->name[sizeof e->name - 1] = 0;
    int w = 0, h = 0;
    unsigned char *img = image_load_texture(name, &w, &h);
    if (!img) { VLOG("texture '%s' not found (looked for textures/%s.png|bmp|tga and the built-ins)", name, name); e->id = 0; return 0; }
    e->id = texture_upload(img, w, h, 1, 1);
    free(img);
    return e->id;
}
