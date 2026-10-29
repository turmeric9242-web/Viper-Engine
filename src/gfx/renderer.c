#include "gfx/renderer.h"
#include "gfx/assets.h"
#include "gfx/glload.h"
#include "core/log.h"
#include <GLFW/glfw3.h>
#include <stdio.h>
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
    "layout(location=3) in vec2 a_uv;\n"
    "uniform mat4 u_vp; uniform mat4 u_model; uniform mat4 u_nrm;\n"
    "out vec3 v_n; out vec3 v_wp; out vec4 v_vc; out vec2 v_uv;\n"
    "void main(){ vec4 wp = u_model * vec4(a_pos, 1.0); v_wp = wp.xyz;\n"
    "  v_n = mat3(u_nrm) * a_nrm; v_vc = a_vcol; v_uv = a_uv; gl_Position = u_vp * wp; }\n";
static const char *FS =
    "#version 330 core\n"
    "in vec3 v_n; in vec3 v_wp; in vec4 v_vc; in vec2 v_uv;\n"
    "uniform vec3 u_color; uniform float u_checker; uniform float u_highlight;\n"
    "uniform float u_use_vcol; uniform float u_unlit;\n"
    "uniform sampler2D u_lm; uniform vec4 u_lmb; uniform float u_use_lm;\n"
    "uniform sampler3D u_pr; uniform vec3 u_prb; uniform vec3 u_prs; uniform float u_use_pr;\n"
    "uniform sampler2D u_tex; uniform float u_use_tex;\n"
    "uniform vec3 u_cam; uniform vec4 u_fog;\n"
    "out vec4 o;\n"
    "void main(){\n"
    "  vec3 n = normalize(v_n); vec3 l = normalize(vec3(0.4, 1.0, 0.3));\n"
    "  float d = max(dot(n, l), 0.0);\n"
    "  vec3 c = u_use_vcol > 0.5 ? v_vc.rgb : u_color;\n"
    "  if (u_use_tex > 0.5) c *= texture(u_tex, v_uv).rgb;\n"
    "  float chk = u_use_vcol > 0.5 ? v_vc.a : u_checker;\n"
    "  if (chk > 0.5) { float k = mod(floor(v_wp.x) + floor(v_wp.z), 2.0); c *= mix(0.75, 1.0, k); }\n"
    "  vec3 col = c * (0.28 + 0.72 * d);\n"
    "  if (u_unlit < 0.5) {\n"
    "    if (u_use_pr > 0.5) {\n"
    "      vec3 uv = clamp((v_wp - u_prb) * u_prs, 0.0, 1.0);\n"
    "      col *= 0.35 + 1.5 * texture(u_pr, uv).rgb;\n"
    "    } else if (u_use_lm > 0.5) {\n"
    "      vec2 uv = clamp((v_wp.xz - u_lmb.xy) * u_lmb.zw, 0.0, 1.0);\n"
    "      col *= 0.35 + 1.5 * texture(u_lm, uv).rgb;\n"
    "    }\n"
    "  } else { col = c; }\n"
    "  col = mix(col, vec3(1.0, 0.85, 0.2), u_highlight * 0.35);\n"
    "  float dist = length(v_wp - u_cam);\n"
    "  float fog = u_fog.y > u_fog.x ? clamp((dist - u_fog.x) / max(u_fog.y - u_fog.x, 1e-3), 0.0, 1.0) : 0.0;\n"
    "  col = mix(col, vec3(0.55, 0.68, 0.85), fog);\n"
    "  o = vec4(pow(col, vec3(1.0 / 2.2)), 1.0); }\n";

typedef struct { unsigned vao, vbo; int count; } Mesh;
static Mesh meshes[4]; /* [0]=unused none, [1]=cube, [2]=sphere, [3]=merged static batch */
static unsigned prog;
static int u_vp, u_model, u_nrm, u_color, u_checker, u_highlight, u_use_vcol, u_unlit;
static int u_lm, u_lmb, u_use_lm, u_pr, u_prb, u_prs, u_use_pr;
static int u_tex, u_use_tex;
static int u_cam, u_fog;
static Vec3 cam_pos = {0, 0, 0};
static float fog_start = 1e5f, fog_end = 2e5f;
static unsigned lm_tex, pr_tex;
static float lm_bounds[4] = {0, 0, 1, 1};
static float pr_min[3] = {0, 0, 0}, pr_scale[3] = {1, 1, 1};
static Mat4 Gvp;

/* ---- per-entity texture cache (keyed by path, loaded on first use) ---- */
#define TEX_MAX 64
static struct { char path[128]; unsigned tex; } tex_cache[TEX_MAX];
static int tex_ncache;
static unsigned white_tex;

static unsigned tex_for(const char *path) {
    if (!path || !path[0]) return 0;
    for (int i = 0; i < tex_ncache; i++)
        if (!strcmp(tex_cache[i].path, path)) return tex_cache[i].tex;
    int id = assets_load_texture(path);
    if (!id) return 0;
    if (tex_ncache >= TEX_MAX) return (unsigned)id; /* full: load un-cached */
    snprintf(tex_cache[tex_ncache].path, sizeof tex_cache[tex_ncache].path, "%s", path);
    tex_cache[tex_ncache].tex = (unsigned)id;
    tex_ncache++;
    return (unsigned)id;
}

/* bind the entity's texture (if any) to unit 2; returns the u_use_tex flag */
static float bind_entity_tex(const Entity *e) {
    unsigned t = 0;
    if (e && e->tex_path[0]) t = tex_for(e->tex_path);
    vgl.ActiveTexture(VGL_TEXTURE0 + 2);
    glBindTexture(GL_TEXTURE_2D, t ? t : white_tex);
    vgl.ActiveTexture(VGL_TEXTURE0);
    return t ? 1.0f : 0.0f;
}

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
    if (stride == 8) { /* pos3 + nrm3 + uv2 */
        vgl.VertexAttribPointer(3, 2, GL_FLOAT, 0, stride * sizeof(float), (void *)(6 * sizeof(float)));
        vgl.EnableVertexAttribArray(3);
    }
    return m;
}

static void push(float *buf, int *n, Vec3 p, Vec3 nrm, float u, float v) {
    buf[(*n)++] = p.x; buf[(*n)++] = p.y; buf[(*n)++] = p.z;
    buf[(*n)++] = nrm.x; buf[(*n)++] = nrm.y; buf[(*n)++] = nrm.z;
    buf[(*n)++] = u; buf[(*n)++] = v;
}

static void build_cube(void) {
    static const Vec3 N[6] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    static const Vec3 U[6] = {{0,1,0},{0,0,1},{0,0,1},{1,0,0},{1,0,0},{0,1,0}}; /* u x v == normal */
    static const Vec3 V[6] = {{0,0,1},{0,1,0},{1,0,0},{0,0,1},{0,1,0},{1,0,0}};
    float buf[36 * 8]; int n = 0;
    for (int f = 0; f < 6; f++) {
        Vec3 c = v3_mul(N[f], 0.5f), u = v3_mul(U[f], 0.5f), v = v3_mul(V[f], 0.5f);
        Vec3 p0 = v3_sub(v3_sub(c, u), v), p1 = v3_sub(v3_add(c, u), v);
        Vec3 p2 = v3_add(v3_add(c, u), v), p3 = v3_add(v3_sub(c, u), v);
        push(buf, &n, p0, N[f], 0, 1); push(buf, &n, p1, N[f], 1, 1); push(buf, &n, p2, N[f], 1, 0);
        push(buf, &n, p0, N[f], 0, 1); push(buf, &n, p2, N[f], 1, 0); push(buf, &n, p3, N[f], 0, 0);
    }
    meshes[MESH_CUBE] = upload(buf, n, 8, 0);
}

static void build_sphere(void) {
    enum { ST = 16, SL = 24 };
    float *buf = (float *)malloc(sizeof(float) * ST * SL * 6 * 8); int n = 0;
    for (int i = 0; i < ST; i++) for (int j = 0; j < SL; j++) {
        float a0 = VIPER_PI * i / ST, a1 = VIPER_PI * (i + 1) / ST;
        float b0 = 2 * VIPER_PI * j / SL, b1 = 2 * VIPER_PI * (j + 1) / SL;
        float u0 = (float)j / SL, u1 = (float)(j + 1) / SL;
        float v0 = (float)i / ST, v1 = (float)(i + 1) / ST;
        Vec3 p00 = v3(sinf(a0) * cosf(b0), cosf(a0), sinf(a0) * sinf(b0));
        Vec3 p01 = v3(sinf(a0) * cosf(b1), cosf(a0), sinf(a0) * sinf(b1));
        Vec3 p10 = v3(sinf(a1) * cosf(b0), cosf(a1), sinf(a1) * sinf(b0));
        Vec3 p11 = v3(sinf(a1) * cosf(b1), cosf(a1), sinf(a1) * sinf(b1));
        push(buf, &n, v3_mul(p00, .5f), p00, u0, v0); push(buf, &n, v3_mul(p10, .5f), p10, u0, v1); push(buf, &n, v3_mul(p11, .5f), p11, u1, v1);
        push(buf, &n, v3_mul(p00, .5f), p00, u0, v0); push(buf, &n, v3_mul(p11, .5f), p11, u1, v1); push(buf, &n, v3_mul(p01, .5f), p01, u1, v0);
    }
    meshes[MESH_SPHERE] = upload(buf, n, 8, 0);
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
    if (e->tex_path[0]) return; /* textured units draw individually (per-entity bind) */

    /* regenerate the unit mesh triangles (same builders as init) and bake
       them through the entity's model matrix into world space */
    static float unit[32768]; /* sphere = 16*24*6*8 = 18432 floats */
    int un = 0;
    if (e->mesh == MESH_CUBE) {
        static const Vec3 N[6] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
        static const Vec3 U[6] = {{0,1,0},{0,0,1},{0,0,1},{1,0,0},{1,0,0},{0,1,0}};
        static const Vec3 V[6] = {{0,0,1},{0,1,0},{1,0,0},{0,0,1},{0,1,0},{1,0,0}};
        for (int f = 0; f < 6; f++) {
            Vec3 c = v3_mul(N[f], 0.5f), u = v3_mul(U[f], 0.5f), v = v3_mul(V[f], 0.5f);
            Vec3 p0 = v3_sub(v3_sub(c, u), v), p1 = v3_sub(v3_add(c, u), v);
            Vec3 p2 = v3_add(v3_add(c, u), v), p3 = v3_add(v3_sub(c, u), v);
            push(unit, &un, p0, N[f], 0, 1); push(unit, &un, p1, N[f], 1, 1); push(unit, &un, p2, N[f], 1, 0);
            push(unit, &un, p0, N[f], 0, 1); push(unit, &un, p2, N[f], 1, 0); push(unit, &un, p3, N[f], 0, 0);
        }
    } else {
        enum { ST = 16, SL = 24 };
        for (int i = 0; i < ST; i++) for (int j = 0; j < SL; j++) {
            float a0 = VIPER_PI * i / ST, a1 = VIPER_PI * (i + 1) / ST;
            float b0 = 2 * VIPER_PI * j / SL, b1 = 2 * VIPER_PI * (j + 1) / SL;
            float u0 = (float)j / SL, u1 = (float)(j + 1) / SL;
            float v0 = (float)i / ST, v1 = (float)(i + 1) / ST;
            Vec3 p00 = v3(sinf(a0) * cosf(b0), cosf(a0), sinf(a0) * sinf(b0));
            Vec3 p01 = v3(sinf(a0) * cosf(b1), cosf(a0), sinf(a0) * sinf(b1));
            Vec3 p10 = v3(sinf(a1) * cosf(b0), cosf(a1), sinf(a1) * sinf(b0));
            Vec3 p11 = v3(sinf(a1) * cosf(b1), cosf(a1), sinf(a1) * sinf(b1));
            push(unit, &un, v3_mul(p00, .5f), p00, u0, v0); push(unit, &un, v3_mul(p10, .5f), p10, u0, v1); push(unit, &un, v3_mul(p11, .5f), p11, u1, v1);
            push(unit, &un, v3_mul(p00, .5f), p00, u0, v0); push(unit, &un, v3_mul(p11, .5f), p11, u1, v1); push(unit, &un, v3_mul(p01, .5f), p01, u1, v0);
        }
    }

    Mat4 model = entity_model(e), nrm = entity_normal_matrix(e);
    int need = batch_n + un / 8 * 10;
    if (need > batch_cap) {
        batch_cap = need * 2 + 4096;
        batch = (float *)realloc(batch, (size_t)batch_cap * sizeof(float));
    }
    for (int i = 0; i < un; i += 8) {
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
    vgl.Uniform1f(u_use_tex, 0.0f);
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
    u_lm = vgl.GetUniformLocation(prog, "u_lm");         u_lmb = vgl.GetUniformLocation(prog, "u_lmb");
    u_use_lm = vgl.GetUniformLocation(prog, "u_use_lm");
    u_pr = vgl.GetUniformLocation(prog, "u_pr");         u_prb = vgl.GetUniformLocation(prog, "u_prb");
    u_prs = vgl.GetUniformLocation(prog, "u_prs");       u_use_pr = vgl.GetUniformLocation(prog, "u_use_pr");
    u_tex = vgl.GetUniformLocation(prog, "u_tex");       u_use_tex = vgl.GetUniformLocation(prog, "u_use_tex");
    u_cam = vgl.GetUniformLocation(prog, "u_cam");       u_fog = vgl.GetUniformLocation(prog, "u_fog");
    vgl.UseProgram(prog);
    vgl.Uniform1i(u_lm, 0);
    vgl.Uniform1i(u_pr, 1);
    vgl.Uniform1i(u_tex, 2);
    { /* 1x1 white fallback bound to unit 2 when an entity has no texture */
        unsigned char px[4] = {255, 255, 255, 255};
        glGenTextures(1, &white_tex);
        vgl.ActiveTexture(VGL_TEXTURE0 + 2);
        glBindTexture(GL_TEXTURE_2D, white_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        vgl.ActiveTexture(VGL_TEXTURE0);
    }
    build_cube(); build_sphere();
    glEnable(GL_DEPTH_TEST);
    glEnable(VGL_MULTISAMPLE);
    return 1;
}

/* Baked lighting: lightmap (world XZ) + light probe grid (world XYZ). */
void renderer_set_lightmap(int w, int h, const unsigned char *rgb,
                           float minx, float minz, float sizex, float sizez) {
    if (!lm_tex) glGenTextures(1, &lm_tex);
    glBindTexture(GL_TEXTURE_2D, lm_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb);
    glBindTexture(GL_TEXTURE_2D, 0);
    lm_bounds[0] = minx; lm_bounds[1] = minz;
    lm_bounds[2] = sizex > 1e-6f ? 1.0f / sizex : 0.0f;
    lm_bounds[3] = sizez > 1e-6f ? 1.0f / sizez : 0.0f;
}

void renderer_clear_lightmap(void) {
    if (lm_tex) { glDeleteTextures(1, &lm_tex); lm_tex = 0; }
    vgl.UseProgram(prog);
    vgl.Uniform1f(u_use_lm, 0.0f);
}

void renderer_set_probes(int nx, int ny, int nz, const unsigned char *rgb,
                         float minx, float miny, float minz, float vx, float vy, float vz) {
    if (!pr_tex) glGenTextures(1, &pr_tex);
    glBindTexture(VGL_TEXTURE_3D, pr_tex);
    glTexParameteri(VGL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(VGL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(VGL_TEXTURE_3D, GL_TEXTURE_WRAP_S, 0x812F);
    glTexParameteri(VGL_TEXTURE_3D, GL_TEXTURE_WRAP_T, 0x812F);
    glTexParameteri(VGL_TEXTURE_3D, 0x8072 /* WRAP_R */, 0x812F);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    vgl.TexImage3D(VGL_TEXTURE_3D, 0, GL_RGB, nx, ny, nz, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb);
    glBindTexture(VGL_TEXTURE_3D, 0);
    pr_min[0] = minx; pr_min[1] = miny; pr_min[2] = minz;
    pr_scale[0] = vx > 1e-6f ? 1.0f / vx : 0.0f;
    pr_scale[1] = vy > 1e-6f ? 1.0f / vy : 0.0f;
    pr_scale[2] = vz > 1e-6f ? 1.0f / vz : 0.0f;
}

void renderer_clear_probes(void) {
    if (pr_tex) { glDeleteTextures(1, &pr_tex); pr_tex = 0; }
    vgl.UseProgram(prog);
    vgl.Uniform1f(u_use_pr, 0.0f);
}

static void bind_baked(void) {
    vgl.UseProgram(prog);
    vgl.Uniform1f(u_use_lm, lm_tex ? 1.0f : 0.0f);
    vgl.Uniform1f(u_use_pr, pr_tex ? 1.0f : 0.0f);
    vgl.Uniform4f(u_lmb, lm_bounds[0], lm_bounds[1], lm_bounds[2], lm_bounds[3]);
    vgl.Uniform3f(u_prb, pr_min[0], pr_min[1], pr_min[2]);
    vgl.Uniform3f(u_prs, pr_scale[0], pr_scale[1], pr_scale[2]);
    vgl.Uniform3f(u_cam, cam_pos.x, cam_pos.y, cam_pos.z);
    vgl.Uniform4f(u_fog, fog_start, fog_end, 0.0f, 0.0f);
    vgl.ActiveTexture(VGL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, lm_tex);
    vgl.ActiveTexture(VGL_TEXTURE0 + 1);
    glBindTexture(VGL_TEXTURE_3D, pr_tex);
    vgl.ActiveTexture(VGL_TEXTURE0);
}

void renderer_set_camera(Vec3 eye) { cam_pos = eye; }
void renderer_set_fog(float start, float end) { fog_start = start; fog_end = end; }

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
    bind_baked();
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
    vgl.Uniform1f(u_use_tex, 0.0f);
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
    vgl.Uniform1f(u_use_tex, bind_entity_tex(e));
    vgl.BindVertexArray(m->vao);
    glDrawArrays(GL_TRIANGLES, 0, m->count);
}

void renderer_draw_entity(const Entity *e, int highlight) {
    if (!renderer_entity_visible(e)) return;
    if (e->mesh == MESH_CUBE || e->mesh == MESH_SPHERE) draw_mesh(&meshes[e->mesh], e, highlight);
}

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
    vgl.Uniform1f(u_use_tex, bind_entity_tex(e));
    vgl.BindVertexArray(assets_meshes[mesh_id].vao);
    glDrawArrays(GL_TRIANGLES, 0, assets_meshes[mesh_id].count);
}
