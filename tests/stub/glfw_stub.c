/* Fake GLFW + GL: scripted input, counts draw calls. Used only by tools/smoke test. */
#include <stdio.h>
#include <string.h>
#include <GLFW/glfw3.h>
static GLFWkeyfun kcb; static GLFWcursorposfun ccb; static void *up; static int frame, closing;
static long draws, clears; static double t;
int glfwInit(void) { return 1; } void glfwTerminate(void) { printf("[stub] draws=%ld clears=%ld frames=%d\n", draws, clears, frame); }
void glfwWindowHint(int a, int b) { (void)a; (void)b; }
GLFWwindow *glfwCreateWindow(int w, int h, const char *ti, void *a, void *b) { (void)w;(void)h;(void)ti;(void)a;(void)b; return (GLFWwindow *)1; }
void glfwDestroyWindow(GLFWwindow *w) { (void)w; }
void glfwMakeContextCurrent(GLFWwindow *w) { (void)w; } void glfwSwapInterval(int i) { (void)i; } void glfwSwapBuffers(GLFWwindow *w) { (void)w; }
void glfwSetWindowUserPointer(GLFWwindow *w, void *p) { (void)w; up = p; } void *glfwGetWindowUserPointer(GLFWwindow *w) { (void)w; return up; }
GLFWkeyfun glfwSetKeyCallback(GLFWwindow *w, GLFWkeyfun f) { (void)w; kcb = f; return 0; }
GLFWcursorposfun glfwSetCursorPosCallback(GLFWwindow *w, GLFWcursorposfun f) { (void)w; ccb = f; return 0; }
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
    if (frame == 300) closing = 1;
    if (ccb && frame % 7 == 0) ccb((GLFWwindow *)1, frame * 0.5, 0);
}
/* GL stubs */
void glClear(unsigned m) { (void)m; clears++; } void glClearColor(float a, float b, float c, float d) { (void)a;(void)b;(void)c;(void)d; }
void glEnable(unsigned c) { (void)c; } void glViewport(int a, int b, int c, int d) { (void)a;(void)b;(void)c;(void)d; }
void glDrawArrays(unsigned m, int f, int c) { (void)m; (void)f; (void)c; draws++; }
static unsigned next_id = 1;
static void ok1(unsigned a, unsigned b, int *p) { (void)a; (void)b; *p = 1; }
static unsigned mk(unsigned a) { (void)a; return next_id++; } static unsigned mk0(void) { return next_id++; }
static void gen(int n, unsigned *ids) { while (n--) *ids++ = next_id++; }
static int uloc(unsigned p, const char *n) { (void)p; return (int)strlen(n); }
static void nop(void) {}
GLFWglproc glfwGetProcAddress(const char *n) {
    if (!strcmp(n, "glGetShaderiv") || !strcmp(n, "glGetProgramiv")) return (GLFWglproc)ok1;
    if (!strcmp(n, "glCreateShader")) return (GLFWglproc)mk;
    if (!strcmp(n, "glCreateProgram")) return (GLFWglproc)mk0;
    if (!strcmp(n, "glGenVertexArrays") || !strcmp(n, "glGenBuffers")) return (GLFWglproc)gen;
    if (!strcmp(n, "glGetUniformLocation")) return (GLFWglproc)uloc;
    return (GLFWglproc)nop;
}
