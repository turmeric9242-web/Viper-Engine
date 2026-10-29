#include "gfx/renderer.h"
#include "gfx/glload.h"
#include "gfx/texture.h"
#include "gfx/ui.h"
#include "core/log.h"
#include <stdlib.h>
#include <math.h>

static const char *VS =
    "#version 330 core\n"
    "layout(location=0) in vec3 a_pos;\n"
    "layout(location=1) in vec3 a_nrm;\n"
    "layout(location=2) in vec2 a_uv;\n"
    "uniform mat4 u_vp; uniform mat4 u_model; uniform mat4 u_nrm;\n"
    "uniform vec3 u_scale; uniform float u_tile; uniform float u_sphere;\n"
    "out vec3 v_n; out vec3 v_wp; out vec2 v_uv;\n"
    "void main(){ vec4 wp = u_model * vec4(a_pos, 1.0); v_wp = wp.xyz;\n"
    "  v_n = mat3(u_nrm) * a_nrm; gl_Position = u_vp * wp;\n"
    "  vec3 an = abs(a_nrm); vec2 uv;\n"
    "  if (u_sphere > 0.5) uv = a_uv * vec2(4.0, 2.0);\n"
    "  else if (an.x > 0.5) uv = (a_pos.zy + 0.5) * u_scale.zy;\n"
    "  else if (an.y > 0.5) uv = (a_pos.xz + 0.5) * u_scale.xz;\n"
    "  else uv = (a_pos.xy + 0.5) * u_scale.xy;\n"
    "  v_uv = uv * u_tile; }\n";
static const char *FS =
    "#version 330 core\n"
    "in vec3 v_n; in vec3 v_wp; in vec2 v_uv;\n"
    "uniform vec3 u_color; uniform float u_checker; uniform float u_highlight;\n"
    "uniform sampler2D u_tex; uniform float u_hastex;\n"
    "out vec4 o;\n"
    "void main(){\n"
    "  vec3 n = normalize(v_n); vec3 l = normalize(vec3(0.4, 1.0, 0.3));\n"
    "  float d = max(dot(n, l), 0.0); vec3 c = u_color;\n"
    "  if (u_hastex > 0.5) { vec4 t = texture(u_tex, v_uv); if (t.a < 0.5) discard; c *= pow(t.rgb, vec3(2.2)); }\n"
    "  if (u_checker > 0.5) { float k = mod(floor(v_wp.x) + floor(v_wp.z), 2.0); c *= mix(0.75, 1.0, k); }\n"
    "  vec3 col = c * (0.28 + 0.72 * d);\n"
    "  col = mix(col, vec3(1.0, 0.85, 0.2), u_highlight * 0.35);\n"
    "  o = vec4(pow(col, vec3(1.0 / 2.2)), 1.0); }\n";

typedef struct { unsigned vao, vbo; int count; } Mesh;
static Mesh meshes[3]; /* indexed by MeshType */
static unsigned prog;
static int u_vp, u_model, u_nrm, u_color, u_checker, u_highlight, u_scale, u_tile, u_sphere, u_tex, u_hastex;

static unsigned compile_shader(unsigned type, const char *src) {
    unsigned s = vgl.CreateShader(type);
    vgl.ShaderSource(s, 1, &src, NULL);
    vgl.CompileShader(s);
    int ok = 0; vgl.GetShaderiv(s, VGL_COMPILE_STATUS, &ok);
    if (!ok) { char log[512]; vgl.GetShaderInfoLog(s, sizeof log, NULL, log); VLOG("shader error: %s", log); return 0; }
    return s;
}

static Mesh upload(const float *data, int floats) {
    Mesh m; m.count = floats / 8;
    vgl.GenVertexArrays(1, &m.vao); vgl.BindVertexArray(m.vao);
    vgl.GenBuffers(1, &m.vbo); vgl.BindBuffer(VGL_ARRAY_BUFFER, m.vbo);
    vgl.BufferData(VGL_ARRAY_BUFFER, (ptrdiff_t)floats * (ptrdiff_t)sizeof(float), data, VGL_STATIC_DRAW);
    vgl.VertexAttribPointer(0, 3, GL_FLOAT, 0, 8 * sizeof(float), (void *)0);
    vgl.EnableVertexAttribArray(0);
    vgl.VertexAttribPointer(1, 3, GL_FLOAT, 0, 8 * sizeof(float), (void *)(3 * sizeof(float)));
    vgl.EnableVertexAttribArray(1);
    vgl.VertexAttribPointer(2, 2, GL_FLOAT, 0, 8 * sizeof(float), (void *)(6 * sizeof(float)));
    vgl.EnableVertexAttribArray(2);
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
        /* cube UVs are derived in the vertex shader from position + normal (so tiling follows entity scale) */
        push(buf, &n, p0, N[f], 0, 0); push(buf, &n, p1, N[f], 0, 0); push(buf, &n, p2, N[f], 0, 0);
        push(buf, &n, p0, N[f], 0, 0); push(buf, &n, p2, N[f], 0, 0); push(buf, &n, p3, N[f], 0, 0);
    }
    meshes[MESH_CUBE] = upload(buf, n);
}

static void build_sphere(void) {
    enum { ST = 16, SL = 24 };
    float *buf = (float *)malloc(sizeof(float) * ST * SL * 6 * 8); int n = 0;
    for (int i = 0; i < ST; i++) for (int j = 0; j < SL; j++) {
        float a0 = VIPER_PI * i / ST, a1 = VIPER_PI * (i + 1) / ST;
        float b0 = 2 * VIPER_PI * j / SL, b1 = 2 * VIPER_PI * (j + 1) / SL;
        Vec3 p00 = v3(sinf(a0) * cosf(b0), cosf(a0), sinf(a0) * sinf(b0));
        Vec3 p01 = v3(sinf(a0) * cosf(b1), cosf(a0), sinf(a0) * sinf(b1));
        Vec3 p10 = v3(sinf(a1) * cosf(b0), cosf(a1), sinf(a1) * sinf(b0));
        Vec3 p11 = v3(sinf(a1) * cosf(b1), cosf(a1), sinf(a1) * sinf(b1));
        float u0 = (float)j / SL, u1 = (float)(j + 1) / SL, t0 = (float)i / ST, t1 = (float)(i + 1) / ST;
        push(buf, &n, v3_mul(p00, .5f), p00, u0, t0); push(buf, &n, v3_mul(p10, .5f), p10, u0, t1); push(buf, &n, v3_mul(p11, .5f), p11, u1, t1);
        push(buf, &n, v3_mul(p00, .5f), p00, u0, t0); push(buf, &n, v3_mul(p11, .5f), p11, u1, t1); push(buf, &n, v3_mul(p01, .5f), p01, u1, t0);
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
    u_scale = vgl.GetUniformLocation(prog, "u_scale"); u_tile = vgl.GetUniformLocation(prog, "u_tile");
    u_sphere = vgl.GetUniformLocation(prog, "u_sphere"); u_tex = vgl.GetUniformLocation(prog, "u_tex");
    u_hastex = vgl.GetUniformLocation(prog, "u_hastex");
    build_cube(); build_sphere();
    if (!ui_init()) return 0;
    glEnable(GL_DEPTH_TEST);
    glEnable(VGL_MULTISAMPLE);
    return 1;
}

void renderer_begin(int w, int h, Vec3 sky) {
    glViewport(0, 0, w, h);
    glClearColor(sky.x, sky.y, sky.z, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    vgl.UseProgram(prog);
    vgl.Uniform1i(u_tex, 0);
}

void renderer_set_view_proj(Mat4 vp) { vgl.UniformMatrix4fv(u_vp, 1, 0, vp.m); }

void renderer_draw_entity(const Entity *e, int highlight) {
    if (e->mesh == MESH_NONE) return;
    Mat4 model = entity_model(e), nrm = entity_normal_matrix(e);
    vgl.UniformMatrix4fv(u_model, 1, 0, model.m);
    vgl.UniformMatrix4fv(u_nrm, 1, 0, nrm.m);
    vgl.Uniform3f(u_color, e->color.x, e->color.y, e->color.z);
    vgl.Uniform1f(u_checker, e->checker ? 1.0f : 0.0f);
    vgl.Uniform1f(u_highlight, highlight ? 1.0f : 0.0f);
    unsigned tex = texture_get(e->texture);
    vgl.Uniform1f(u_hastex, tex ? 1.0f : 0.0f);
    if (tex) {
        glBindTexture(GL_TEXTURE_2D, tex);
        vgl.Uniform3f(u_scale, fabsf(e->scale.x), fabsf(e->scale.y), fabsf(e->scale.z));
        vgl.Uniform1f(u_tile, e->tile > 0.0f ? e->tile : 1.0f);
        vgl.Uniform1f(u_sphere, e->mesh == MESH_SPHERE ? 1.0f : 0.0f);
    }
    vgl.BindVertexArray(meshes[e->mesh].vao);
    glDrawArrays(GL_TRIANGLES, 0, meshes[e->mesh].count);
}
