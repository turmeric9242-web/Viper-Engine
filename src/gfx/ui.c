#include "gfx/ui.h"
#include "gfx/glload.h"
#include "gfx/font.h"
#include "gfx/texture.h"
#include "core/log.h"
#include <string.h>

#define ATLAS 128
#define CELL_W 8
#define CELL_H 16
#define MAXV (6 * 4096)

static const char *VS =
    "#version 330 core\n"
    "layout(location=0) in vec2 a_pos; layout(location=1) in vec2 a_uv; layout(location=2) in vec4 a_col;\n"
    "uniform vec2 u_size; out vec2 v_uv; out vec4 v_col;\n"
    "void main(){ v_uv = a_uv; v_col = a_col; vec2 p = a_pos / u_size * 2.0 - 1.0; gl_Position = vec4(p.x, -p.y, 0.0, 1.0); }\n";
static const char *FS =
    "#version 330 core\n"
    "in vec2 v_uv; in vec4 v_col; uniform sampler2D u_tex; out vec4 o;\n"
    "void main(){ o = v_col * texture(u_tex, v_uv); }\n";

static unsigned prog, vao, vbo, atlas;
static int u_size, u_tex, fbw, fbh, nv;
static float vbuf[MAXV * 8];

static unsigned compile(unsigned type, const char *src) {
    unsigned s = vgl.CreateShader(type);
    vgl.ShaderSource(s, 1, &src, NULL); vgl.CompileShader(s);
    int ok = 0; vgl.GetShaderiv(s, VGL_COMPILE_STATUS, &ok);
    if (!ok) { char log[512]; vgl.GetShaderInfoLog(s, sizeof log, NULL, log); VLOG("ui shader error: %s", log); return 0; }
    return s;
}

int ui_init(void) {
    unsigned vs = compile(VGL_VERTEX_SHADER, VS), fs = compile(VGL_FRAGMENT_SHADER, FS);
    if (!vs || !fs) return 0;
    prog = vgl.CreateProgram(); vgl.AttachShader(prog, vs); vgl.AttachShader(prog, fs); vgl.LinkProgram(prog);
    int ok = 0; vgl.GetProgramiv(prog, VGL_LINK_STATUS, &ok);
    if (!ok) { VLOG("ui program link failed"); return 0; }
    vgl.DeleteShader(vs); vgl.DeleteShader(fs);
    u_size = vgl.GetUniformLocation(prog, "u_size"); u_tex = vgl.GetUniformLocation(prog, "u_tex");

    /* font atlas: 16 columns x 6 rows of 8x16 cells, solid white block in the bottom-right cell */
    static unsigned char px[ATLAS * ATLAS * 4];
    memset(px, 0, sizeof px);
    for (int g = 0; g < 95; g++) {
        int cx = (g % 16) * CELL_W, cy = (g / 16) * CELL_H + 1;
        for (int y = 0; y < FONT_H; y++) for (int x = 0; x < FONT_W; x++)
            if (VFONT[g][y] & (1 << (FONT_W - 1 - x))) {
                unsigned char *p = px + ((cy + y) * ATLAS + cx + x) * 4;
                p[0] = p[1] = p[2] = p[3] = 255;
            }
    }
    for (int y = 112; y < 128; y++) for (int x = 120; x < 128; x++) {
        unsigned char *p = px + (y * ATLAS + x) * 4; p[0] = p[1] = p[2] = p[3] = 255;
    }
    atlas = texture_upload(px, ATLAS, ATLAS, 0, 0);

    vgl.GenVertexArrays(1, &vao); vgl.BindVertexArray(vao);
    vgl.GenBuffers(1, &vbo); vgl.BindBuffer(VGL_ARRAY_BUFFER, vbo);
    vgl.VertexAttribPointer(0, 2, GL_FLOAT, 0, 8 * sizeof(float), (void *)0); vgl.EnableVertexAttribArray(0);
    vgl.VertexAttribPointer(1, 2, GL_FLOAT, 0, 8 * sizeof(float), (void *)(2 * sizeof(float))); vgl.EnableVertexAttribArray(1);
    vgl.VertexAttribPointer(2, 4, GL_FLOAT, 0, 8 * sizeof(float), (void *)(4 * sizeof(float))); vgl.EnableVertexAttribArray(2);
    return 1;
}

static void flush(void) {
    if (!nv) return;
    vgl.BindVertexArray(vao); vgl.BindBuffer(VGL_ARRAY_BUFFER, vbo);
    vgl.BufferData(VGL_ARRAY_BUFFER, (ptrdiff_t)nv * 8 * (ptrdiff_t)sizeof(float), vbuf, VGL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, nv);
    nv = 0;
}

void ui_begin(int w, int h) {
    fbw = w; fbh = h; nv = 0;
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    vgl.UseProgram(prog);
    vgl.Uniform2f(u_size, (float)w, (float)h);
    vgl.Uniform1i(u_tex, 0);
    glBindTexture(GL_TEXTURE_2D, atlas);
}

static void quad(float x, float y, float w, float h, float u0, float v0, float u1, float v1, unsigned c) {
    if (nv + 6 > MAXV) flush();
    float r = (float)((c >> 24) & 255) / 255.0f, g = (float)((c >> 16) & 255) / 255.0f,
          b = (float)((c >> 8) & 255) / 255.0f, a = (float)(c & 255) / 255.0f;
    const float X[6] = {x, x + w, x + w, x, x + w, x}, Y[6] = {y, y, y + h, y, y + h, y + h};
    const float U[6] = {u0, u1, u1, u0, u1, u0}, V[6] = {v0, v0, v1, v0, v1, v1};
    for (int i = 0; i < 6; i++) {
        float *o = vbuf + (size_t)nv * 8;
        o[0] = X[i]; o[1] = Y[i]; o[2] = U[i]; o[3] = V[i]; o[4] = r; o[5] = g; o[6] = b; o[7] = a;
        nv++;
    }
}

void ui_rect(float x, float y, float w, float h, unsigned rgba) {
    float u = 124.5f / ATLAS, v = 120.5f / ATLAS;
    quad(x, y, w, h, u, v, u, v, rgba);
}

int ui_text_width(const char *s, int scale) { return (int)strlen(s) * FONT_ADV * scale; }

void ui_text(float x, float y, const char *s, unsigned rgba, int scale) {
    for (; *s; s++, x += (float)(FONT_ADV * scale)) {
        int c = (unsigned char)*s;
        if (c <= 32 || c > 126) continue;
        int g = c - 32, cx = (g % 16) * CELL_W, cy = (g / 16) * CELL_H + 1;
        quad(x, y, (float)(FONT_W * scale), (float)(FONT_H * scale),
             (float)cx / ATLAS, (float)cy / ATLAS, (float)(cx + FONT_W) / ATLAS, (float)(cy + FONT_H) / ATLAS, rgba);
    }
}

void ui_end(void) {
    flush();
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    (void)fbw; (void)fbh;
}
