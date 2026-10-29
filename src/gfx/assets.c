#include "gfx/assets.h"
#include "gfx/glload.h"
#include "core/log.h"
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

AObj assets_meshes[ASSETS_MAX_MESH];
int assets_nmeshes;

int assets_mesh_cpu(int id, const float **data, int *nfloats) {
    if (id < 0 || id >= assets_nmeshes) return 0;
    *data = assets_meshes[id].cpu;
    *nfloats = assets_meshes[id].ncpu;
    return assets_meshes[id].cpu != NULL;
}

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
    if (!path || !path[0]) return -1;
    for (int i = 0; i < assets_nmeshes; i++)
        if (!strcmp(assets_meshes[i].path, path)) return i;
    return -1;
}

/* Worker-thread safe: never touches GL, only the already-loaded CPU cache. */
int assets_find_obj(const char *path) { return find_mesh(path); }

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
    float *out = (float *)malloc(MAXV * 8 * sizeof *out);
    if (!vx || !vy || !vz || !nx || !ny || !nz || !out) {
        free(buf); free(vx); free(vy); free(vz); free(nx); free(ny); free(nz); free(out);
        return -1;
    }
    int nv = 0, nn = 0, nt = 0, no = 0;
    float *tu = (float *)malloc(MAXV * sizeof *tu), *tv = (float *)malloc(MAXV * sizeof *tv);
    if (!tu || !tv) { free(tu); free(tv); free(buf); free(vx); free(vy); free(vz); free(nx); free(ny); free(nz); free(out); return -1; }

    char *p = buf;
    while (*p) {
        if (*p == '\n' || *p == '\r') { p++; continue; }
        if (*p == '#') { while (*p && *p != '\n') p++; continue; }
        if (p[0] == 'v' && (p[1] == ' ' || p[1] == '\t')) {
            if (nv < MAXV) sscanf(p + 2, "%f %f %f", &vx[nv], &vy[nv], &vz[nv]);
            nv++;
            while (*p && *p != '\n') p++; continue;
        }
        if (p[0] == 'v' && p[1] == 't' && (p[2] == ' ' || p[2] == '\t')) {
            if (nt < MAXV) sscanf(p + 3, "%f %f", &tu[nt], &tv[nt]);
            nt++;
            while (*p && *p != '\n') p++; continue;
        }
        if (p[0] == 'v' && p[1] == 'n' && (p[2] == ' ' || p[2] == '\t')) {
            if (nn < MAXV) sscanf(p + 3, "%f %f %f", &nx[nn], &ny[nn], &nz[nn]);
            nn++;
            while (*p && *p != '\n') p++; continue;
        }
        if (p[0] == 'f' && (p[1] == ' ' || p[1] == '\t')) {
            p += 2;
            int vi[4], ni[4], ti[4], c = 0;
            while (*p && *p != '\n' && c < 4) {
                while (*p == ' ' || *p == '\t') p++;
                if (!*p || *p == '\n') break;
                int v = 0, tv_i = 0, nr = 0;
                if (sscanf(p, "%d/%d/%d", &v, &tv_i, &nr) < 3) { tv_i = 0; nr = 0; if (sscanf(p, "%d/%d", &v, &nr) < 2) { nr = 0; sscanf(p, "%d", &v); } }
                vi[c] = v - 1; ni[c] = (nr > 0) ? nr - 1 : -1; ti[c] = (tv_i > 0) ? tv_i - 1 : -1;
                c++;
                while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
            }
            /* fan-triangulate: (0,i,i+1) */
            for (int i = 1; i + 1 < c; i++) {
                int tri[3] = {0, i, i + 1};
                for (int t = 0; t < 3; t++) {
                    int ia = vi[tri[t]], ina = ni[tri[t]], ita = ti[tri[t]];
                    if (no + 8 > MAXV * 8 || ia < 0 || ia >= nv) continue;
                    out[no++] = vx[ia]; out[no++] = vy[ia]; out[no++] = vz[ia];
                    if (ina >= 0 && ina < nn) { out[no++] = nx[ina]; out[no++] = ny[ina]; out[no++] = nz[ina]; }
                    else { out[no++] = 0; out[no++] = 1; out[no++] = 0; }
                    if (ita >= 0 && ita < nt) { out[no++] = tu[ita]; out[no++] = tv[ita]; }
                    else { out[no++] = 0; out[no++] = 0; }
                }
            }
            while (*p && *p != '\n') p++; continue;
        }
        while (*p && *p != '\n') p++;
    }
    free(buf); free(vx); free(vy); free(vz); free(nx); free(ny); free(nz); free(tu); free(tv);

    if (no == 0) { VLOG("assets: %s has 0 usable vertices", path); free(out); return -1; }

    struct AObj *m = &assets_meshes[assets_nmeshes];
    vgl.GenVertexArrays(1, &m->vao); vgl.BindVertexArray(m->vao);
    vgl.GenBuffers(1, &m->vbo); vgl.BindBuffer(VGL_ARRAY_BUFFER, m->vbo);
    vgl.BufferData(VGL_ARRAY_BUFFER, (ptrdiff_t)no * (ptrdiff_t)sizeof(float), out, VGL_STATIC_DRAW);
    vgl.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)0);
    vgl.EnableVertexAttribArray(0);
    vgl.VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)(3 * sizeof(float)));
    vgl.EnableVertexAttribArray(1);
    vgl.VertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)(6 * sizeof(float)));
    vgl.EnableVertexAttribArray(3);
    vgl.BindVertexArray(0);
    m->count = no / 8;
    snprintf(m->path, sizeof m->path, "%s", path);
    m->cpu = out;               /* handed to the baker, freed by assets_shutdown */
    m->ncpu = no;
    VLOG("assets: %s -> %d verts", path, m->count);
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
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    vgl.GenerateMipmap(GL_TEXTURE_2D);
    stbi_image_free(data);
    glBindTexture(GL_TEXTURE_2D, 0);
    VLOG("assets: texture %s -> %dx%d", path, w, h);
    return (int)tex;
}

void assets_shutdown(void) {
    for (int i = 0; i < assets_nmeshes; i++) { free(assets_meshes[i].cpu); assets_meshes[i].cpu = NULL; }
}
