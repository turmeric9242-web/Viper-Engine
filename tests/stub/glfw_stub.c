/* Fake GLFW + GL: scripted input, counts draw calls. Used only by tools/smoke test. */
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <GLFW/glfw3.h>
static unsigned char atlas_copy[128 * 128 * 4];
static GLFWkeyfun kcb; static GLFWscrollfun scb; static GLFWcursorposfun ccb; static void *up; static int frame, closing;
static long draws, clears; static double t;
int glfwInit(void) { return 1; } void glfwTerminate(void) { printf("[stub] draws=%ld clears=%ld frames=%d\n", draws, clears, frame); }
void glfwWindowHint(int a, int b) { (void)a; (void)b; }
GLFWwindow *glfwCreateWindow(int w, int h, const char *ti, void *a, void *b) { (void)w;(void)h;(void)ti;(void)a;(void)b; return (GLFWwindow *)1; }
void glfwDestroyWindow(GLFWwindow *w) { (void)w; }
void glfwMakeContextCurrent(GLFWwindow *w) { (void)w; } void glfwSwapInterval(int i) { (void)i; } void glfwSwapBuffers(GLFWwindow *w) { (void)w; }
void glfwSetWindowUserPointer(GLFWwindow *w, void *p) { (void)w; up = p; } void *glfwGetWindowUserPointer(GLFWwindow *w) { (void)w; return up; }
GLFWkeyfun glfwSetKeyCallback(GLFWwindow *w, GLFWkeyfun f) { (void)w; kcb = f; return 0; }
GLFWcursorposfun glfwSetCursorPosCallback(GLFWwindow *w, GLFWcursorposfun f) { (void)w; ccb = f; return 0; }
GLFWscrollfun glfwSetScrollCallback(GLFWwindow *w, GLFWscrollfun f) { (void)w; scb = f; return 0; }
void glfwGetWindowSize(GLFWwindow *w, int *a, int *b) { (void)w; *a = 1280; *b = 720; }
void glfwGetCursorPos(GLFWwindow *w, double *x, double *y) { (void)w; *x = *y = 0; }
int glfwWindowShouldClose(GLFWwindow *w) { (void)w; return closing; } void glfwSetWindowShouldClose(GLFWwindow *w, int v) { (void)w; closing = v; }
void glfwGetFramebufferSize(GLFWwindow *w, int *a, int *b) { (void)w; *a = 1280; *b = 720; }
void glfwSetWindowTitle(GLFWwindow *w, const char *s) { (void)w; if (frame % 100 == 0) printf("[stub] title: %s\n", s); }
double glfwGetTime(void) { return t; }
void glfwSetInputMode(GLFWwindow *w, int a, int b) { (void)w; (void)a; (void)b; } int glfwRawMouseMotionSupported(void) { return 0; }
int glfwGetMouseButton(GLFWwindow *w, int b) { (void)w; (void)b; return 0; }
void glfwPollEvents(void) {
    frame++; t += 1.0 / 60;
    if (frame == 5) kcb((GLFWwindow *)1, 294, 0, 1, 0);          /* F5: Play */
    if (frame == 100) kcb((GLFWwindow *)1, 'W', 0, 1, 0);        /* hold W */
    if (frame == 130) kcb((GLFWwindow *)1, ' ', 0, 1, 0);        /* jump */
    if (frame == 131) kcb((GLFWwindow *)1, ' ', 0, 0, 0);
    if (frame == 200) kcb((GLFWwindow *)1, 'W', 0, 0, 0);
    if (frame == 210) kcb((GLFWwindow *)1, 294, 0, 1, 0);        /* F5: Stop */
    if (frame == 215) kcb((GLFWwindow *)1, 294, 0, 0, 0);
    if (frame == 220) kcb((GLFWwindow *)1, 258, 0, 1, 0);        /* TAB select */
#define KEY(f, k) if (frame == (f)) kcb((GLFWwindow *)1, (k), 0, 1, 0); if (frame == (f) + 1) kcb((GLFWwindow *)1, (k), 0, 0, 0)
    KEY(222, 'T'); KEY(230, 264); KEY(234, 264); KEY(238, 257);       /* texture menu: down, down, Enter */
    KEY(250, 'P'); KEY(256, 264); KEY(260, 257);                       /* script menu: down, Enter */
    KEY(270, 'T'); KEY(276, 256);                                      /* open then Esc: closes the menu, must NOT quit */
    KEY(285, 292);                                                     /* F3: vsync toggle */
    if (frame == 400) closing = 1;
    if (ccb && frame % 7 == 0) ccb((GLFWwindow *)1, frame * 0.5, 0);
}
/* GL stubs */
void glClear(unsigned m) { (void)m; clears++; } void glClearColor(float a, float b, float c, float d) { (void)a;(void)b;(void)c;(void)d; }
void glEnable(unsigned c) { (void)c; } void glDisable(unsigned c) { (void)c; } void glBlendFunc(unsigned a, unsigned b) { (void)a; (void)b; } void glBindTexture(unsigned a, unsigned b) { (void)a; (void)b; }
void glGenTextures(int n, unsigned *ids) { while (n--) *ids++ = 900 + (unsigned)(frame % 7) + (unsigned)n; }
void glTexParameteri(unsigned a, unsigned b, int c) { (void)a; (void)b; (void)c; }
static int tex_calls;
void glTexImage2D(unsigned a, int b, int c, int d, int e, int f, unsigned g, unsigned h, const void *p) { (void)a;(void)b;(void)c;(void)f;(void)g;(void)h;
    if (tex_calls++ == 0 && d == 128 && e == 128) memcpy(atlas_copy, p, sizeof atlas_copy); }
void glViewport(int a, int b, int c, int d) { (void)a;(void)b;(void)c;(void)d; }
void glDrawArrays(unsigned m, int f, int c) { (void)m; (void)f; (void)c; draws++; }
static unsigned next_id = 1;
static void ok1(unsigned a, unsigned b, int *p) { (void)a; (void)b; *p = 1; }
static unsigned mk(unsigned a) { (void)a; return next_id++; } static unsigned mk0(void) { return next_id++; }
static void gen(int n, unsigned *ids) { while (n--) *ids++ = next_id++; }
static int uloc(unsigned p, const char *n) { (void)p; return (int)strlen(n); }
static void nop(void) {}
/* Optional capture used to eyeball the 2D overlay: the first glTexImage2D is the font atlas, and the UI vertex
   buffer of frame 236 (texture picker open) is written to /tmp so a script can rasterise it. */
static void buf_data(unsigned target, ptrdiff_t size, const void *data, unsigned usage) {
    (void)target;
    if (usage == 0x88E0 && frame == 236) {
        FILE *f = fopen("/tmp/ui_dump.bin", "wb"); if (f) { fwrite(data, 1, (size_t)size, f); fclose(f); }
        f = fopen("/tmp/atlas.rgba", "wb"); if (f) { fwrite(atlas_copy, 1, sizeof atlas_copy, f); fclose(f); }
    }
}
GLFWglproc glfwGetProcAddress(const char *n) {
    if (!strcmp(n, "glGetShaderiv") || !strcmp(n, "glGetProgramiv")) return (GLFWglproc)ok1;
    if (!strcmp(n, "glCreateShader")) return (GLFWglproc)mk;
    if (!strcmp(n, "glCreateProgram")) return (GLFWglproc)mk0;
    if (!strcmp(n, "glGenVertexArrays") || !strcmp(n, "glGenBuffers")) return (GLFWglproc)gen;
    if (!strcmp(n, "glGetUniformLocation")) return (GLFWglproc)uloc;
    if (!strcmp(n, "glBufferData")) return (GLFWglproc)buf_data;
    return (GLFWglproc)nop;
}
