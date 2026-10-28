#include "gfx/renderer.h"
#include "gfx/assets.h"
#include "gfx/glload.h"
#include "core/log.h"
#include <GLFW/glfw3.h>
#include <stdlib.h>
#include <string.h>

#define VGL_DYNAMIC_DRAW 0x88E8

/* location 2 (a_vcol) is only enabled on the merged static-world VAO;
   unit meshes leave it disabled and the shader picks u_color instead. */
static const char *VS =
    "#version 330 core\n"
    "layout(location=0) in vec3 a_pos;\n"
    "layout(location=1) in vec3 a_nrm;\n"
    "layout(location=2) in vec4 a_vcol;\n"
    "uniform mat4 u_vp; uniform mat4 u_model; uniform mat4 u_nrm;\n"
    "out vec3 v_n; out vec3 v_wp; out vec4 v_vc;\n"
    "void main(){ vec4 wp = u_model * vec4(a_pos, 1.0); v_wp = wp.xyz;\n"
    "  v_n = mat3(u_nrm) * a_nrm; v_vc = a_vcol; gl_Position = u_vp * wp; }\n";
static const char *FS =
    "#version 330 core\n"
    "in vec3 v_n; in vec3 v_wp; in vec4 v_vc;\n"
    "uniform vec3 u_color; uniform float u_checker; uniform float u_highlight;\n"
    "uniform float u_use_vcol;\n"
    "out vec4 o;\n"
    "void main(){\n"
    "  vec3 n = normalize(v_n); vec3 l = normalize(vec3(0.4, 1.0, 0.3));\n"
    "  float d = max(dot(n, l), 0.0);\n"
    "  vec3 c = u_use_vcol > 0.5 ? v_vc.rgb : u_color;\n"
    "  float chk = u_use_vcol > 0.5 ? v_vc.a : u_checker;\n"
    "  if (chk > 0.5) { float k = mod(floor(v_wp.x) + floor(v_wp.z), 2.0); c *= mix(0.75, 1.0, k); }\n"
    "  vec3 col = c * (0.28 + 0.72 * d);\n"
    "  col = mix(col, vec3(1.0, 0.85, 0.2), u_highlight * 0.35);\n"
    "  o = vec4(pow(col, vec3(1.0 / 2.2)), 1.0); }\n";

typedef struct { unsigned vao, vbo; int count; } Mesh;
static Mesh meshes[4]; /* [0]=unused none, [1]=cube, [2]=sphere, [3]=merged static batch */
static unsigned prog;
static int u_vp, u_model, u_nrm, u_color, u_checker, u_highlight, u_use_vcol, u_unlit;
static Mat4 Gvp;

/* ---- frustum (6 planes extracted from Gvp, Gribb-Hartmann) ---- */
typedef struct { float x, y, z, w; } Plane;
static Plane planes[6];
static int planes_valid;

static void extract_planes(void) {
    const float *m = Gvp.m;
    /* row i of the (column-major) matrix: (m[i], m[4+i], m[8+i], m[12+i]) */
    float r[4][4];
    for (int i = 0; i < 4; i++) { r[i][0] = m[i]; r[i][1] = m[4 + i]; r[i][2] = m[8 + i]; r[i][3] = m[12 + i]; }
    Plane *p = planes;
    for (int i = 0; i < 3; i++) { /* left, bottom, near */
        p[i * 2].x = r[3][0] + r[i][0]; p[i * 2].y = r[3][1] + r[i][1];
        p[i * 2].z = r[3][2] + r[i][2]; p[i * 2].w = r[3][3] + r[i][3];
        p[i * 2 + 1].x = r[3][0] - r[i][0]; p[i * 2 + 1].y = r[3][1] - r[i][1];
        p[i * 2 + 1].z = r[3][2] - r[i][2]; p[i * 2 + 1].w = r[3][3] - r[i][3];
    }
    for (int i = 0; i < 6; i++) { /* normalise so sphere test uses true distance */
        float l = sqrtf(p[i].x * p[i].x + p[i].y * p[i].y + p[i].z * p[i].z);
        if (l > 1e-8f) { p[i].x /= l; p[i].y /= l; p[i].z /= l; p[i].w /= l; }
    }
    planes_valid = 1;
}

/* bounding radius covering cube (half-diagonal) and sphere (0.5*scale) */
static float entity_radius(const Entity *e) {
    float a = fmaxf(e->scale.x, fmaxf(e->scale.y, e->scale.z));
    return 0.87f * a;
}

static int sphere_visible(Vec3 c, float r) {
    if (!planes_valid) return 1;
    for (int i = 0; i < 6; i++)
        if (planes[i].x * c.x + planes[i].y * c.y + planes[i].z * c.z + planes[i].w < -r)
            return 0;
    return 1;
}

int renderer_entity_visible(const Entity *e) {
    if (!e->active) return 0;
    if (e->mesh == MESH_OBJ) return 1; /* unknown bounds: never cull */
    return sphere_visible(e->pos, entity_radius(e));
}

static unsigned compile_shader(unsigned type, const char *src) {
    unsigned s = vgl.CreateShader(type);
    vgl.ShaderSource(s, 1, &src, NULL);
    vgl.CompileShader(s);
    int ok = 0; vgl.GetShaderiv(s, VGL_COMPILE_STATUS, &ok);
    if (!ok) { char log[512]; vgl.GetShaderInfoLog(s, sizeof log, NULL, log); VLOG("shader error: %s", log); return 0; }
    return s;
}

static Mesh upload(const float *data, int floats, int stride, int dynamic) {
    Mesh m; m.count = floats / stride;
    vgl.GenVertexArrays(1, &m.vao); vgl.BindVertexArray(m.vao);
    vgl.GenBuffers(1, &m.vbo); vgl.BindBuffer(VGL_ARRAY_BUFFER, m.vbo);
    vgl.BufferData(VGL_ARRAY_BUFFER, (ptrdiff_t)floats * (ptrdiff_t)sizeof(float), data,
                   dynamic ? VGL_DYNAMIC_DRAW : VGL_STATIC_DRAW);
    vgl.VertexAttribPointer(0, 3, GL_FLOAT, 0, stride * sizeof(float), (void *)0);
    vgl.EnableVertexAttribArray(0);
    vgl.VertexAttribPointer(1, 3, GL_FLOAT, 0, stride * sizeof(float), (void *)(3 * sizeof(float)));
    vgl.EnableVertexAttribArray(1);
    if (stride >= 10) {
        vgl.VertexAttribPointer(2, 4, GL_FLOAT, 0, stride * sizeof(float), (void *)(6 * sizeof(float)));
        vgl.EnableVertexAttribArray(2);
    }
    return m;
}

static void push(float *buf, int *n, Vec3 p, Vec3 nrm) {
    buf[(*n)++] = p.x; buf[(*n)++] = p.y; buf[(*n)++] = p.z;
    buf[(*n)++] = nrm.x; buf[(*n)++] = nrm.y; buf[(*n)++] = nrm.z;
}

static void build_cube(void) {
    static const Vec3 N[6] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    static const Vec3 U[6] = {{0,1,0},{0,0,1},{0,0,1},{1,0,0},{1,0,0},{0,1,0}}; /* u x v == normal */
    static const Vec3 V[6] = {{0,0,1},{0,1,0},{1,0,0},{0,0,1},{0,1,0},{1,0,0}};
    float buf[36 * 6]; int n = 0;
    for (int f = 0; f < 6; f++) {
        Vec3 c = v3_mul(N[f], 0.5f), u = v3_mul(U[f], 0.5f), v = v3_mul(V[f], 0.5f);
        Vec3 p0 = v3_sub(v3_sub(c, u), v), p1 = v3_sub(v3_add(c, u), v);
        Vec3 p2 = v3_add(v3_add(c, u), v), p3 = v3_add(v3_sub(c, u), v);
        push(buf, &n, p0, N[f]); push(buf, &n, p1, N[f]); push(buf, &n, p2, N[f]);
        push(buf, &n, p0, N[f]); push(buf, &n, p2, N[f]); push(buf, &n, p3, N[f]);
    }
    meshes[MESH_CUBE] = upload(buf, n, 6, 0);
}

static void build_sphere(void) {
    enum { ST = 16, SL = 24 };
    float *buf = (float *)malloc(sizeof(float) * ST * SL * 6 * 6); int n = 0;
    for (int i = 0; i < ST; i++) for (int j = 0; j < SL; j++) {
        float a0 = VIPER_PI * i / ST, a1 = VIPER_PI * (i + 1) / ST;
        float b0 = 2 * VIPER_PI * j / SL, b1 = 2 * VIPER_PI * (j + 1) / SL;
        Vec3 p00 = v3(sinf(a0) * cosf(b0), cosf(a0), sinf(a0) * sinf(b0));
        Vec3 p01 = v3(sinf(a0) * cosf(b1), cosf(a0), sinf(a0) * sinf(b1));
        Vec3 p10 = v3(sinf(a1) * cosf(b0), cosf(a1), sinf(a1) * sinf(b0));
        Vec3 p11 = v3(sinf(a1) * cosf(b1), cosf(a1), sinf(a1) * sinf(b1));
        push(buf, &n, v3_mul(p00, .5f), p00); push(buf, &n, v3_mul(p10, .5f), p10); push(buf, &n, v3_mul(p11, .5f), p11);
        push(buf, &n, v3_mul(p00, .5f), p00); push(buf, &n, v3_mul(p11, .5f), p11); push(buf, &n, v3_mul(p01, .5f), p01);
    }
    meshes[MESH_SPHERE] = upload(buf, n, 6, 0);
    free(buf);
}

/* ---- merged static world: every static cube/sphere baked into one VBO ---- */
static float *batch;
static int batch_n, batch_cap;
static int batch_verts;

void renderer_batch_reset(void) { batch_n = 0; batch_verts = 0; }

void renderer_batch_add(const Entity *e) {
    if (!renderer_entity_visible(e)) return;
    if (e->mesh != MESH_CUBE && e->mesh != MESH_SPHERE) return;

    /* regenerate the unit mesh triangles (same builders as init) and bake
       them through the entity's model matrix into world space */
    static float unit[16384]; /* sphere = 16*24*6*6 = 13824 floats */
    int un = 0;
    if (e->mesh == MESH_CUBE) {
        static const Vec3 N[6] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
        static const Vec3 U[6] = {{0,1,0},{0,0,1},{0,0,1},{1,0,0},{1,0,0},{0,1,0}};
        static const Vec3 V[6] = {{0,0,1},{0,1,0},{1,0,0},{0,0,1},{0,1,0},{1,0,0}};
        for (int f = 0; f < 6; f++) {
            Vec3 c = v3_mul(N[f], 0.5f), u = v3_mul(U[f], 0.5f), v = v3_mul(V[f], 0.5f);
            Vec3 p0 = v3_sub(v3_sub(c, u), v), p1 = v3_sub(v3_add(c, u), v);
            Vec3 p2 = v3_add(v3_add(c, u), v), p3 = v3_add(v3_sub(c, u), v);
            push(unit, &un, p0, N[f]); push(unit, &un, p1, N[f]); push(unit, &un, p2, N[f]);
            push(unit, &un, p0, N[f]); push(unit, &un, p2, N[f]); push(unit, &un, p3, N[f]);
        }
    } else {
        enum { ST = 16, SL = 24 };
        for (int i = 0; i < ST; i++) for (int j = 0; j < SL; j++) {
            float a0 = VIPER_PI * i / ST, a1 = VIPER_PI * (i + 1) / ST;
            float b0 = 2 * VIPER_PI * j / SL, b1 = 2 * VIPER_PI * (j + 1) / SL;
            Vec3 p00 = v3(sinf(a0) * cosf(b0), cosf(a0), sinf(a0) * sinf(b0));
            Vec3 p01 = v3(sinf(a0) * cosf(b1), cosf(a0), sinf(a0) * sinf(b1));
            Vec3 p10 = v3(sinf(a1) * cosf(b0), cosf(a1), sinf(a1) * sinf(b0));
            Vec3 p11 = v3(sinf(a1) * cosf(b1), cosf(a1), sinf(a1) * sinf(b1));
            push(unit, &un, v3_mul(p00, .5f), p00); push(unit, &un, v3_mul(p10, .5f), p10); push(unit, &un, v3_mul(p11, .5f), p11);
            push(unit, &un, v3_mul(p00, .5f), p00); push(unit, &un, v3_mul(p11, .5f), p11); push(unit, &un, v3_mul(p01, .5f), p01);
        }
    }

    Mat4 model = entity_model(e), nrm = entity_normal_matrix(e);
    int need = batch_n + un / 6 * 10;
    if (need > batch_cap) {
        batch_cap = need * 2 + 4096;
        batch = (float *)realloc(batch, (size_t)batch_cap * sizeof(float));
    }
    for (int i = 0; i < un; i += 6) {
        Vec3 p = v3(unit[i], unit[i + 1], unit[i + 2]);
        Vec3 nv = v3(unit[i + 3], unit[i + 4], unit[i + 5]);
        /* transform point + normal by the model / normal matrix (column-major) */
        float x = p.x, y = p.y, z = p.z;
        batch[batch_n++] = model.m[0] * x + model.m[4] * y + model.m[8] * z + model.m[12];
        batch[batch_n++] = model.m[1] * x + model.m[5] * y + model.m[9] * z + model.m[13];
        batch[batch_n++] = model.m[2] * x + model.m[6] * y + model.m[10] * z + model.m[14];
        float nx = nv.x, ny = nv.y, nz = nv.z;
        float wx = nrm.m[0] * nx + nrm.m[4] * ny + nrm.m[8] * nz;
        float wy = nrm.m[1] * nx + nrm.m[5] * ny + nrm.m[9] * nz;
        float wz = nrm.m[2] * nx + nrm.m[6] * ny + nrm.m[10] * nz;
        float l = sqrtf(wx * wx + wy * wy + wz * wz);
        if (l > 1e-8f) { wx /= l; wy /= l; wz /= l; }
        batch[batch_n++] = wx; batch[batch_n++] = wy; batch[batch_n++] = wz;
        batch[batch_n++] = e->color.x; batch[batch_n++] = e->color.y; batch[batch_n++] = e->color.z;
        batch[batch_n++] = e->checker ? 1.0f : 0.0f;
        batch_verts++;
    }
}

void renderer_batch_flush(void) {
    if (batch_verts <= 0) return;
    if (!meshes[3].vbo) {
        meshes[3].vao = 0;
        vgl.GenVertexArrays(1, &meshes[3].vao); vgl.BindVertexArray(meshes[3].vao);
        vgl.GenBuffers(1, &meshes[3].vbo); vgl.BindBuffer(VGL_ARRAY_BUFFER, meshes[3].vbo);
        vgl.VertexAttribPointer(0, 3, GL_FLOAT, 0, 10 * sizeof(float), (void *)0);
        vgl.EnableVertexAttribArray(0);
        vgl.VertexAttribPointer(1, 3, GL_FLOAT, 0, 10 * sizeof(float), (void *)(3 * sizeof(float)));
        vgl.EnableVertexAttribArray(1);
        vgl.VertexAttribPointer(2, 4, GL_FLOAT, 0, 10 * sizeof(float), (void *)(6 * sizeof(float)));
        vgl.EnableVertexAttribArray(2);
    }
    vgl.UseProgram(prog);
    vgl.BindVertexArray(meshes[3].vao);
    vgl.BindBuffer(VGL_ARRAY_BUFFER, meshes[3].vbo);
    vgl.BufferData(VGL_ARRAY_BUFFER, (ptrdiff_t)batch_n * (ptrdiff_t)sizeof(float), batch, VGL_DYNAMIC_DRAW);
    Mat4 ident = m4_identity();
    vgl.UniformMatrix4fv(u_vp, 1, 0, Gvp.m);
    vgl.UniformMatrix4fv(u_model, 1, 0, ident.m);
    vgl.UniformMatrix4fv(u_nrm, 1, 0, ident.m);
    vgl.Uniform1f(u_use_vcol, 1.0f);
    vgl.Uniform1f(u_highlight, 0.0f);
    vgl.Uniform1f(u_unlit, 0.0f);
    glDrawArrays(GL_TRIANGLES, 0, batch_verts);
    vgl.Uniform1f(u_use_vcol, 0.0f);
}

int renderer_init(void) {
    if (!vgl_load()) return 0;
    unsigned vs = compile_shader(VGL_VERTEX_SHADER, VS), fs = compile_shader(VGL_FRAGMENT_SHADER, FS);
    if (!vs || !fs) return 0;
    prog = vgl.CreateProgram();
    vgl.AttachShader(prog, vs); vgl.AttachShader(prog, fs); vgl.LinkProgram(prog);
    int ok = 0; vgl.GetProgramiv(prog, VGL_LINK_STATUS, &ok);
    if (!ok) { char log[512]; vgl.GetProgramInfoLog(prog, sizeof log, NULL, log); VLOG("link error: %s", log); return 0; }
    vgl.DeleteShader(vs); vgl.DeleteShader(fs);
    u_vp = vgl.GetUniformLocation(prog, "u_vp");       u_model = vgl.GetUniformLocation(prog, "u_model");
    u_nrm = vgl.GetUniformLocation(prog, "u_nrm");     u_color = vgl.GetUniformLocation(prog, "u_color");
    u_checker = vgl.GetUniformLocation(prog, "u_checker"); u_highlight = vgl.GetUniformLocation(prog, "u_highlight");
    u_use_vcol = vgl.GetUniformLocation(prog, "u_use_vcol");
    u_unlit = vgl.GetUniformLocation(prog, "u_unlit");
    build_cube(); build_sphere();
    glEnable(GL_DEPTH_TEST);
    glEnable(VGL_MULTISAMPLE);
    return 1;
}

void renderer_begin(int w, int h, Vec3 sky) { renderer_begin_view(w, h, 0, 0, w, h, sky, sky); }

/* Draw into a sub-rect of the window (the editor viewport). vx/vy use
   top-left origin window pixels; the GL viewport origin is bottom-left. */
void renderer_begin_view(int fbw, int fbh, int vx, int vy, int vw, int vh, Vec3 bg, Vec3 sky) {
    if (vw < 1) vw = 1;
    if (vh < 1) vh = 1;
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, fbw, fbh);
    glClearColor(bg.x, bg.y, bg.z, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (vx || vy || vw != fbw || vh != fbh) {
        glEnable(GL_SCISSOR_TEST);
        glScissor(vx, fbh - vy - vh, vw, vh);
        glViewport(vx, fbh - vy - vh, vw, vh);
        glClearColor(sky.x, sky.y, sky.z, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);
    }
    vgl.UseProgram(prog);
}

/* ---- immediate-style line batches (gizmo, selection boxes, grid, ghost) ----
   verts = nverts * 6 floats: position xyz + colour rgb. */
static unsigned line_vao, line_vbo;

void renderer_lines(const float *verts, int nverts, int depth_test) {
    if (nverts <= 0) return;
    if (!line_vao) {
        vgl.GenVertexArrays(1, &line_vao); vgl.BindVertexArray(line_vao);
        vgl.GenBuffers(1, &line_vbo); vgl.BindBuffer(VGL_ARRAY_BUFFER, line_vbo);
        vgl.VertexAttribPointer(0, 3, GL_FLOAT, 0, 6 * sizeof(float), (void *)0);
        vgl.EnableVertexAttribArray(0);
        vgl.VertexAttribPointer(2, 3, GL_FLOAT, 0, 6 * sizeof(float), (void *)(3 * sizeof(float)));
        vgl.EnableVertexAttribArray(2);
    }
    vgl.BindVertexArray(line_vao);
    vgl.BindBuffer(VGL_ARRAY_BUFFER, line_vbo);
    vgl.BufferData(VGL_ARRAY_BUFFER, (ptrdiff_t)nverts * 6, verts, VGL_DYNAMIC_DRAW);
    vgl.UseProgram(prog);
    Mat4 ident = m4_identity();
    vgl.UniformMatrix4fv(u_vp, 1, 0, Gvp.m);
    vgl.UniformMatrix4fv(u_model, 1, 0, ident.m);
    vgl.UniformMatrix4fv(u_nrm, 1, 0, ident.m);
    vgl.Uniform1f(u_use_vcol, 1.0f);
    vgl.Uniform1f(u_unlit, 1.0f);
    vgl.Uniform1f(u_highlight, 0.0f);
    vgl.Uniform1f(u_checker, 0.0f);
    if (!depth_test) glDisable(GL_DEPTH_TEST);
    glDrawArrays(GL_LINES, 0, nverts);
    if (!depth_test) glEnable(GL_DEPTH_TEST);
    vgl.Uniform1f(u_unlit, 0.0f);
}

void renderer_set_view_proj(Mat4 vp) { Gvp = vp; planes_valid = 0; extract_planes(); }

static void draw_mesh(const Mesh *m, const Entity *e, int highlight) {
    Mat4 model = entity_model(e), nrm = entity_normal_matrix(e);
    vgl.UseProgram(prog);
    vgl.UniformMatrix4fv(u_vp, 1, 0, Gvp.m);
    vgl.UniformMatrix4fv(u_model, 1, 0, model.m);
    vgl.UniformMatrix4fv(u_nrm, 1, 0, nrm.m);
    Vec3 col = highlight ? v3(fminf(e->color.x + 0.25f, 1), fminf(e->color.y + 0.25f, 1), fminf(e->color.z + 0.25f, 1)) : e->color;
    vgl.Uniform3f(u_color, col.x, col.y, col.z);
    vgl.Uniform1f(u_checker, e->checker ? 1.0f : 0.0f);
    vgl.Uniform1f(u_highlight, highlight ? 1.0f : 0.0f);
    vgl.Uniform1f(u_use_vcol, 0.0f);
    vgl.Uniform1f(u_unlit, 0.0f);
    vgl.BindVertexArray(m->vao);
    glDrawArrays(GL_TRIANGLES, 0, m->count);
}

void renderer_draw_entity(const Entity *e, int highlight) {
    if (!renderer_entity_visible(e)) return;
    if (e->mesh == MESH_CUBE || e->mesh == MESH_SPHERE) draw_mesh(&meshes[e->mesh], e, highlight);
}

/* Shared with assets.c */
struct AObj { unsigned vao, vbo; int count; char path[128]; };
extern struct AObj assets_meshes[32];
extern int assets_nmeshes;
void renderer_draw_obj(int mesh_id, const Entity *e, int highlight) {
    if (mesh_id < 0 || mesh_id >= assets_nmeshes) return;
    if (assets_meshes[mesh_id].count <= 0) return;
    if (!renderer_entity_visible(e)) return;
    Mat4 model = entity_model(e), nrm = entity_normal_matrix(e);
    vgl.UseProgram(prog);
    vgl.UniformMatrix4fv(u_vp, 1, 0, Gvp.m);
    vgl.UniformMatrix4fv(u_model, 1, 0, model.m);
    vgl.UniformMatrix4fv(u_nrm, 1, 0, nrm.m);
    Vec3 col = highlight ? v3(fminf(e->color.x + 0.25f, 1), fminf(e->color.y + 0.25f, 1), fminf(e->color.z + 0.25f, 1)) : e->color;
    vgl.Uniform3f(u_color, col.x, col.y, col.z);
    vgl.Uniform1f(u_checker, 0.0f);
    vgl.Uniform1f(u_highlight, highlight ? 1.0f : 0.0f);
    vgl.Uniform1f(u_use_vcol, 0.0f);
    vgl.BindVertexArray(assets_meshes[mesh_id].vao);
    glDrawArrays(GL_TRIANGLES, 0, assets_meshes[mesh_id].count);
}
