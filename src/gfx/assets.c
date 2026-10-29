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

/* Global texture cache */
typedef struct {
    char path[160];
    unsigned id;
    int w, h;
} ATex;

#define ASSETS_MAX_TEX 128
static ATex assets_textures[ASSETS_MAX_TEX];
static int assets_ntextures;

int assets_mesh_cpu(int id, const float **data, int *nfloats) {
    if (id < 0 || id >= assets_nmeshes) return 0;
    *data = assets_meshes[id].cpu;
    *nfloats = assets_meshes[id].ncpu;
    return assets_meshes[id].cpu != NULL;
}

static char *read_all(const char *path) {
    FILE *f = fopen(path, "rb");
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

int assets_find_obj(const char *path) { return find_mesh(path); }

int assets_load_texture(const char *path) {
    if (!path || !path[0]) return 0;

    /* Check cache first */
    for (int i = 0; i < assets_ntextures; i++) {
        if (!strcmp(assets_textures[i].path, path)) {
            return (int)assets_textures[i].id;
        }
    }

    if (assets_ntextures >= ASSETS_MAX_TEX) {
        VLOG("assets: texture cache full (%d)", ASSETS_MAX_TEX);
        return 0;
    }

    /* Flip vertically so Y=0 is bottom (standard OpenGL texture coordinate system) */
    stbi_set_flip_vertically_on_load(1);
    int w = 0, h = 0, n = 0;
    unsigned char *data = stbi_load(path, &w, &h, &n, 4);

    /* If not found, try prepending "assets/" */
    char alt_path[256];
    if (!data && strncmp(path, "assets/", 7) != 0 && strncmp(path, "assets\\", 7) != 0) {
        snprintf(alt_path, sizeof alt_path, "assets/%s", path);
        data = stbi_load(alt_path, &w, &h, &n, 4);
    }

    if (!data) {
        VLOG("assets: could not load texture '%s'", path);
        return 0;
    }

    unsigned int tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    vgl.GenerateMipmap(GL_TEXTURE_2D);
    stbi_image_free(data);
    glBindTexture(GL_TEXTURE_2D, 0);

    /* Store in cache */
    ATex *entry = &assets_textures[assets_ntextures++];
    snprintf(entry->path, sizeof entry->path, "%s", path);
    entry->id = tex;
    entry->w = w;
    entry->h = h;

    VLOG("assets: loaded texture '%s' -> %dx%d (GL id %u)", path, w, h, tex);
    return (int)tex;
}

void assets_bind_texture(int mesh_id, const char *tex_path) {
    if (mesh_id < 0 || mesh_id >= assets_nmeshes) return;
    if (!tex_path || !tex_path[0]) return;
    AObj *m = &assets_meshes[mesh_id];
    int tex = assets_load_texture(tex_path);
    if (tex > 0) {
        m->texid = (unsigned)tex;
        snprintf(m->tex_path, sizeof m->tex_path, "%s", tex_path);
    }
}

/* Helper to parse an accompanying .mtl file for diffuse texture (map_Kd) */
static void try_load_mtl(const char *obj_path, const char *mtl_filename, char *out_tex, size_t out_tex_sz) {
    char mtl_path[320];
    const char *slash = strrchr(obj_path, '/');
    if (!slash) slash = strrchr(obj_path, '\\');
    if (slash) {
        int dir_len = (int)(slash - obj_path);
        snprintf(mtl_path, sizeof mtl_path, "%.*s/%s", dir_len, obj_path, mtl_filename);
    } else {
        snprintf(mtl_path, sizeof mtl_path, "%s", mtl_filename);
    }

    char *buf = read_all(mtl_path);
    if (!buf) return;

    char *p = buf;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (*p == '#') { while (*p && *p != '\n') p++; continue; }
        if (strncmp(p, "map_Kd", 6) == 0 && (p[6] == ' ' || p[6] == '\t')) {
            p += 7;
            while (*p == ' ' || *p == '\t') p++;
            char map_file[128];
            int mi = 0;
            while (*p && *p != '\r' && *p != '\n' && mi < 127) {
                map_file[mi++] = *p++;
            }
            map_file[mi] = 0;
            while (mi > 0 && (map_file[mi - 1] == ' ' || map_file[mi - 1] == '\t')) map_file[--mi] = 0;

            if (map_file[0]) {
                if (slash) {
                    int dir_len = (int)(slash - obj_path);
                    snprintf(out_tex, out_tex_sz, "%.*s/%s", dir_len, obj_path, map_file);
                } else {
                    snprintf(out_tex, out_tex_sz, "%s", map_file);
                }
            }
            break;
        }
        while (*p && *p != '\n') p++;
    }
    free(buf);
}

int assets_load_obj(const char *path) {
    int existing = find_mesh(path);
    if (existing >= 0) return existing;
    if (assets_nmeshes >= ASSETS_MAX_MESH) return -1;

    char *buf = read_all(path);
    if (!buf) { VLOG("assets: could not read %s", path); return -1; }

    enum { MAXV = 65536 };
    float *vx = (float *)malloc(MAXV * sizeof *vx), *vy = (float *)malloc(MAXV * sizeof *vy), *vz = (float *)malloc(MAXV * sizeof *vz);
    float *nx = (float *)malloc(MAXV * sizeof *nx), *ny = (float *)malloc(MAXV * sizeof *ny), *nz = (float *)malloc(MAXV * sizeof *nz);
    float *tu = (float *)malloc(MAXV * sizeof *tu), *tv = (float *)malloc(MAXV * sizeof *tv);
    /* GPU buffer: stride 8 (pos3 + nrm3 + uv2) */
    float *gpu = (float *)malloc(MAXV * 8 * sizeof *gpu);
    /* CPU buffer: stride 6 (pos3 + nrm3) for bakers */
    float *cpu = (float *)malloc(MAXV * 6 * sizeof *cpu);

    if (!vx || !vy || !vz || !nx || !ny || !nz || !tu || !tv || !gpu || !cpu) {
        free(buf); free(vx); free(vy); free(vz); free(nx); free(ny); free(nz);
        free(tu); free(tv); free(gpu); free(cpu);
        return -1;
    }

    int nv = 0, nn = 0, nvt = 0, ngpu = 0, ncpu = 0;
    char mtl_name[128] = {0};

    char *p = buf;
    while (*p) {
        if (*p == '\n' || *p == '\r') { p++; continue; }
        if (*p == '#') { while (*p && *p != '\n') p++; continue; }

        /* mtllib */
        if (strncmp(p, "mtllib", 6) == 0 && (p[6] == ' ' || p[6] == '\t')) {
            p += 7;
            while (*p == ' ' || *p == '\t') p++;
            int mi = 0;
            while (*p && *p != '\r' && *p != '\n' && mi < 127) {
                mtl_name[mi++] = *p++;
            }
            mtl_name[mi] = 0;
            while (mi > 0 && (mtl_name[mi - 1] == ' ' || mtl_name[mi - 1] == '\t')) mtl_name[--mi] = 0;
            while (*p && *p != '\n') p++;
            continue;
        }

        /* vt: texture coordinate */
        if (p[0] == 'v' && p[1] == 't' && (p[2] == ' ' || p[2] == '\t')) {
            if (nvt < MAXV) {
                float u = 0, v = 0;
                sscanf(p + 3, "%f %f", &u, &v);
                tu[nvt] = u;
                tv[nvt] = v;
            }
            nvt++;
            while (*p && *p != '\n') p++;
            continue;
        }

        /* vn: normal */
        if (p[0] == 'v' && p[1] == 'n' && (p[2] == ' ' || p[2] == '\t')) {
            if (nn < MAXV) sscanf(p + 3, "%f %f %f", &nx[nn], &ny[nn], &nz[nn]);
            nn++;
            while (*p && *p != '\n') p++;
            continue;
        }

        /* v: position */
        if (p[0] == 'v' && (p[1] == ' ' || p[1] == '\t')) {
            if (nv < MAXV) sscanf(p + 2, "%f %f %f", &vx[nv], &vy[nv], &vz[nv]);
            nv++;
            while (*p && *p != '\n') p++;
            continue;
        }

        /* f: face */
        if (p[0] == 'f' && (p[1] == ' ' || p[1] == '\t')) {
            p += 2;
            int vi[8], ti[8], ni2[8], c = 0;
            while (*p && *p != '\n' && c < 8) {
                while (*p == ' ' || *p == '\t') p++;
                if (!*p || *p == '\n' || *p == '\r') break;
                int v = 0, vt2 = 0, nr = 0;
                if (sscanf(p, "%d/%d/%d", &v, &vt2, &nr) == 3) {
                    /* v/vt/vn */
                } else if (sscanf(p, "%d//%d", &v, &nr) == 2) {
                    vt2 = 0;
                } else if (sscanf(p, "%d/%d", &v, &vt2) == 2) {
                    nr = 0;
                } else {
                    sscanf(p, "%d", &v);
                    vt2 = 0; nr = 0;
                }
                /* support relative indices (negative) */
                if (v < 0) v = nv + v + 1;
                if (vt2 < 0) vt2 = nvt + vt2 + 1;
                if (nr < 0) nr = nn + nr + 1;

                vi[c] = v - 1;
                ti[c] = (vt2 > 0) ? vt2 - 1 : -1;
                ni2[c] = (nr > 0) ? nr - 1 : -1;
                c++;
                while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') p++;
            }

            /* fan triangulation: (0, i, i+1) */
            for (int i = 1; i + 1 < c; i++) {
                int tri[3] = {0, i, i + 1};
                for (int t = 0; t < 3; t++) {
                    int ia = vi[tri[t]], ita = ti[tri[t]], ina = ni2[tri[t]];
                    if (ngpu + 8 > MAXV * 8 || ia < 0 || ia >= nv) continue;

                    /* Pos */
                    gpu[ngpu++] = vx[ia]; gpu[ngpu++] = vy[ia]; gpu[ngpu++] = vz[ia];
                    /* Normal */
                    if (ina >= 0 && ina < nn) {
                        gpu[ngpu++] = nx[ina]; gpu[ngpu++] = ny[ina]; gpu[ngpu++] = nz[ina];
                    } else {
                        gpu[ngpu++] = 0.0f; gpu[ngpu++] = 1.0f; gpu[ngpu++] = 0.0f;
                    }
                    /* UV */
                    if (ita >= 0 && ita < nvt) {
                        gpu[ngpu++] = tu[ita]; gpu[ngpu++] = tv[ita];
                    } else {
                        gpu[ngpu++] = 0.0f; gpu[ngpu++] = 0.0f;
                    }

                    /* CPU copy for bakers (pos + normal only) */
                    if (ncpu + 6 <= MAXV * 6) {
                        cpu[ncpu++] = vx[ia]; cpu[ncpu++] = vy[ia]; cpu[ncpu++] = vz[ia];
                        if (ina >= 0 && ina < nn) {
                            cpu[ncpu++] = nx[ina]; cpu[ncpu++] = ny[ina]; cpu[ncpu++] = nz[ina];
                        } else {
                            cpu[ncpu++] = 0.0f; cpu[ncpu++] = 1.0f; cpu[ncpu++] = 0.0f;
                        }
                    }
                }
            }
            while (*p && *p != '\n') p++;
            continue;
        }
        while (*p && *p != '\n') p++;
    }
    free(buf); free(vx); free(vy); free(vz); free(nx); free(ny); free(nz); free(tu); free(tv);

    if (ngpu == 0) { VLOG("assets: %s has 0 usable vertices", path); free(gpu); free(cpu); return -1; }

    struct AObj *m = &assets_meshes[assets_nmeshes];
    memset(m, 0, sizeof *m);
    vgl.GenVertexArrays(1, &m->vao); vgl.BindVertexArray(m->vao);
    vgl.GenBuffers(1, &m->vbo); vgl.BindBuffer(VGL_ARRAY_BUFFER, m->vbo);
    vgl.BufferData(VGL_ARRAY_BUFFER, (ptrdiff_t)ngpu * (ptrdiff_t)sizeof(float), gpu, VGL_STATIC_DRAW);

    /* attrib 0: pos (3 floats) */
    vgl.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)0);
    vgl.EnableVertexAttribArray(0);
    /* attrib 1: nrm (3 floats) */
    vgl.VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)(3 * sizeof(float)));
    vgl.EnableVertexAttribArray(1);
    /* attrib 3: uv (2 floats) */
    vgl.VertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)(6 * sizeof(float)));
    vgl.EnableVertexAttribArray(3);
    vgl.BindVertexArray(0);
    free(gpu);

    m->count = ngpu / 8;
    snprintf(m->path, sizeof m->path, "%s", path);
    m->cpu = cpu;
    m->ncpu = ncpu;

    /* Auto-detect associated texture:
       1) From .mtl map_Kd
       2) Or <obj_name>.png
       3) Or <obj_name>.jpg */
    char tex_path[320] = {0};
    if (mtl_name[0]) {
        try_load_mtl(path, mtl_name, tex_path, sizeof tex_path);
    }
    if (!tex_path[0]) {
        /* Try <path_without_ext>.png */
        char base[256];
        snprintf(base, sizeof base, "%s", path);
        char *dot = strrchr(base, '.');
        if (dot) *dot = 0;
        snprintf(tex_path, sizeof tex_path, "%s.png", base);
        FILE *tf = fopen(tex_path, "rb");
        if (tf) {
            fclose(tf);
        } else {
            snprintf(tex_path, sizeof tex_path, "%s.jpg", base);
            tf = fopen(tex_path, "rb");
            if (tf) fclose(tf);
            else tex_path[0] = 0;
        }
    }

    if (tex_path[0]) {
        int tid = assets_load_texture(tex_path);
        if (tid > 0) {
            m->texid = (unsigned)tid;
            snprintf(m->tex_path, sizeof m->tex_path, "%s", tex_path);
            VLOG("assets: %s auto-bound texture '%s'", path, tex_path);
        }
    }

    VLOG("assets: %s -> %d verts (UV stride 8)", path, m->count);
    return assets_nmeshes++;
}

void assets_shutdown(void) {
    for (int i = 0; i < assets_nmeshes; i++) {
        free(assets_meshes[i].cpu);
        assets_meshes[i].cpu = NULL;
    }
    for (int i = 0; i < assets_ntextures; i++) {
        if (assets_textures[i].id) {
            unsigned t = assets_textures[i].id;
            glDeleteTextures(1, &t);
            assets_textures[i].id = 0;
        }
    }
    assets_ntextures = 0;
}
