#include "gfx/renderer.h"
#include "gfx/assets.h"
#include "gfx/glload.h"
#include "core/log.h"
#include <GLFW/glfw3.h>
#include <stdlib.h>
#include <string.h>

static const char *VS =
    "#version 330 core\n"
    "layout(location=0) in vec3 a_pos;\n"
    "layout(location=1) in vec3 a_nrm;\n"
    "uniform mat4 u_vp; uniform mat4 u_model; uniform mat4 u_nrm;\n"
    "out vec3 v_n; out vec3 v_wp;\n"
    "void main(){ vec4 wp = u_model * vec4(a_pos, 1.0); v_wp = wp.xyz;\n"
    "  v_n = mat3(u_nrm) * a_nrm; gl_Position = u_vp * wp; }\n";
static const char *FS =
    "#version 330 core\n"
    "in vec3 v_n; in vec3 v_wp;\n"
    "uniform vec3 u_color; uniform float u_checker; uniform float u_highlight;\n"
    "out vec4 o;\n"
    "void main(){\n"
    "  vec3 n = normalize(v_n); vec3 l = normalize(vec3(0.4, 1.0, 0.3));\n"
    "  float d = max(dot(n, l), 0.0); vec3 c = u_color;\n"
    "  if (u_checker > 0.5) { float k = mod(floor(v_wp.x) + floor(v_wp.z), 2.0); c *= mix(0.75, 1.0, k); }\n"
    "  vec3 col = c * (0.28 + 0.72 * d);\n"
    "  col = mix(col, vec3(1.0, 0.85, 0.2), u_highlight * 0.35);\n"
    "  o = vec4(pow(col, vec3(1.0 / 2.2)), 1.0); }\n";

typedef struct { unsigned vao, vbo; int count; } Mesh;
static Mesh meshes[3];
static unsigned prog;
static int u_vp, u_model, u_nrm, u_color, u_checker, u_highlight;
static Mat4 Gvp;

static unsigned compile_shader(unsigned type, const char *src) {
    unsigned s = vgl.CreateShader(type);
    vgl.ShaderSource(s, 1, &src, NULL);
    vgl.CompileShader(s);
    int ok = 0; vgl.GetShaderiv(s, VGL_COMPILE_STATUS, &ok);
    if (!ok) { char log[512]; vgl.GetShaderInfoLog(s, sizeof log, NULL, log); VLOG("shader error: %s", log); return 0; }
    return s;
}

static Mesh upload(const float *data, int floats) {
    Mesh m; m.count = floats / 6;
    vgl.GenVertexArrays(1, &m.vao); vgl.BindVertexArray(m.vao);
    vgl.GenBuffers(1, &m.vbo); vgl.BindBuffer(VGL_ARRAY_BUFFER, m.vbo);
    vgl.BufferData(VGL_ARRAY_BUFFER, (ptrdiff_t)floats * (ptrdiff_t)sizeof(float), data, VGL_STATIC_DRAW);
    vgl.VertexAttribPointer(0, 3, GL_FLOAT, 0, 6 * sizeof(float), (void *)0);
    vgl.EnableVertexAttribArray(0);
    vgl.VertexAttribPointer(1, 3, GL_FLOAT, 0, 6 * sizeof(float), (void *)(3 * sizeof(float)));
    vgl.EnableVertexAttribArray(1);
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
    meshes[MESH_CUBE] = upload(buf, n);
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
    meshes[MESH_SPHERE] = upload(buf, n);
    free(buf);
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
    build_cube(); build_sphere();
    glEnable(GL_DEPTH_TEST);
    glEnable(VGL_MULTISAMPLE);
    return 1;
}

void renderer_begin(int w, int h, Vec3 sky) {
    glViewport(0, 0, w, h);
    glClearColor(sky.x, sky.y, sky.z, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    vgl.UseProgram(prog);
}

void renderer_set_view_proj(Mat4 vp) { Gvp = vp; vgl.UniformMatrix4fv(u_vp, 1, 0, vp.m); }

/* Shared with assets.c */
struct AObj { unsigned vao, vbo; int count; char path[128]; };
extern struct AObj assets_meshes[32];
extern int assets_nmeshes;
void renderer_draw_obj(int mesh_id, const Entity *e, int highlight) {
    if (mesh_id < 0 || mesh_id >= assets_nmeshes) return;
    if (assets_meshes[mesh_id].count <= 0) return;
    vgl.UseProgram(prog);
    Mat4 model = entity_model(e), nrm = entity_normal_matrix(e);
    vgl.UniformMatrix4fv(u_vp, 1, 0, Gvp.m);
    vgl.UniformMatrix4fv(u_model, 1, 0, model.m);
    vgl.UniformMatrix4fv(u_nrm, 1, 0, nrm.m);
    Vec3 col = highlight ? v3(fminf(e->color.x + 0.25f, 1), fminf(e->color.y + 0.25f, 1), fminf(e->color.z + 0.25f, 1)) : e->color;
    vgl.Uniform3f(u_color, col.x, col.y, col.z);
    vgl.Uniform1f(u_checker, 0.0f);
    vgl.Uniform1f(u_highlight, highlight ? 1.0f : 0.0f);
    vgl.BindVertexArray(assets_meshes[mesh_id].vao);
    glDrawArrays(GL_TRIANGLES, 0, assets_meshes[mesh_id].count);
}
