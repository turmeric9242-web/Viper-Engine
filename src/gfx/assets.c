#include "gfx/assets.h"
#include "core/log.h"
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

/* Shared with renderer.c (renderer_draw_obj) via extern. */
struct AObj { unsigned vao, vbo; int count; char path[128]; };
struct AObj assets_meshes[32];
int assets_nmeshes;

static char *read_all(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    buf[rd] = 0;
    fclose(f);
    return buf;
}

static int find_mesh(const char *path) {
    for (int i = 0; i < assets_nmeshes; i++)
        if (!strcmp(assets_meshes[i].path, path)) return i;
    return -1;
}

int assets_load_obj(const char *path) {
    int existing = find_mesh(path);
    if (existing >= 0) return existing;
    if (assets_nmeshes >= (int)(sizeof assets_meshes / sizeof assets_meshes[0])) return -1;

    char *buf = read_all(path);
    if (!buf) { VLOG("assets: could not read %s", path); return -1; }

    /* two passes: count v/f to size, then fill. Simple fixed cap for v0.3. */
    enum { MAXV = 65536 };
    float *vx = (float *)malloc(MAXV * sizeof *vx), *vy = (float *)malloc(MAXV * sizeof *vy), *vz = (float *)malloc(MAXV * sizeof *vz);
    float *nx = (float *)malloc(MAXV * sizeof *nx), *ny = (float *)malloc(MAXV * sizeof *ny), *nz = (float *)malloc(MAXV * sizeof *nz);
    float *out = (float *)malloc(MAXV * 6 * sizeof *out);
    if (!vx || !vy || !vz || !nx || !ny || !nz || !out) {
        free(buf); free(vx); free(vy); free(vz); free(nx); free(ny); free(nz); free(out);
        return -1;
    }
    int nv = 0, nn = 0, no = 0;

    char *p = buf;
    while (*p) {
        if (*p == '\n' || *p == '\r') { p++; continue; }
        if (*p == '#') { while (*p && *p != '\n') p++; continue; }
        if (p[0] == 'v' && (p[1] == ' ' || p[1] == '\t')) {
            if (nv < MAXV) sscanf(p + 2, "%f %f %f", &vx[nv], &vy[nv], &vz[nv]);
            nv++;
            while (*p && *p != '\n') p++; continue;
        }
        if (p[0] == 'v' && p[1] == 'n' && (p[2] == ' ' || p[2] == '\t')) {
            if (nn < MAXV) sscanf(p + 3, "%f %f %f", &nx[nn], &ny[nn], &nz[nn]);
            nn++;
            while (*p && *p != '\n') p++; continue;
        }
        if (p[0] == 'f' && (p[1] == ' ' || p[1] == '\t')) {
            p += 2;
            int vi[4], ni[4], c = 0;
            while (*p && *p != '\n' && c < 4) {
                while (*p == ' ' || *p == '\t') p++;
                if (!*p || *p == '\n') break;
                int v = 0, vt = 0, nr = 0;
                if (sscanf(p, "%d/%d/%d", &v, &vt, &nr) < 3) { vt = 0; nr = 0; if (sscanf(p, "%d/%d", &v, &nr) < 2) { nr = 0; sscanf(p, "%d", &v); } }
                vi[c] = v - 1; ni[c] = (nr > 0) ? nr - 1 : -1;
                c++;
                while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
            }
            /* fan-triangulate: (0,i,i+1) */
            for (int i = 1; i + 1 < c; i++) {
                int tri[3] = {0, i, i + 1};
                for (int t = 0; t < 3; t++) {
                    int ia = vi[tri[t]], ina = ni[tri[t]];
                    if (no + 6 > MAXV * 6 || ia < 0 || ia >= nv) continue;
                    out[no++] = vx[ia]; out[no++] = vy[ia]; out[no++] = vz[ia];
                    if (ina >= 0 && ina < nn) { out[no++] = nx[ina]; out[no++] = ny[ina]; out[no++] = nz[ina]; }
                    else { out[no++] = 0; out[no++] = 1; out[no++] = 0; }
                }
            }
            while (*p && *p != '\n') p++; continue;
        }
        while (*p && *p != '\n') p++;
    }
    free(buf); free(vx); free(vy); free(vz); free(nx); free(ny); free(nz);

    if (no == 0) { VLOG("assets: %s has 0 usable vertices", path); free(out); return -1; }

    struct AObj *m = &assets_meshes[assets_nmeshes];
    glGenVertexArrays(1, &m->vao);
    glGenBuffers(1, &m->vbo);
    glBindVertexArray(m->vao);
    glBindBuffer(GL_ARRAY_BUFFER, m->vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(no * sizeof(float)), out, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    m->count = no / 6;
    snprintf(m->path, sizeof m->path, "%s", path);
    VLOG("assets: %s -> %d verts", path, m->count);
    free(out);
    return assets_nmeshes++;
}

int assets_load_texture(const char *path) {
    int w, h, n;
    unsigned char *data = stbi_load(path, &w, &h, &n, 4);
    if (!data) { VLOG("assets: could not load texture %s", path); return 0; }
    unsigned int tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    stbi_image_free(data);
    glBindTexture(GL_TEXTURE_2D, 0);
    VLOG("assets: texture %s -> %dx%d", path, w, h);
    return (int)tex;
}

void assets_shutdown(void) {}
