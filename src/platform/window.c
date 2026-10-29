#include "platform/window.h"
#include "core/log.h"
#include <GLFW/glfw3.h>
#include <stdlib.h>
#include <string.h>

struct Window {
    GLFWwindow *h;
    Input in;
    double lx, ly;
    int captured, skip, prev_lmb;
};

static void key_cb(GLFWwindow *h, int key, int sc, int action, int mods) {
    (void)sc; (void)mods;
    Window *w = (Window *)glfwGetWindowUserPointer(h);
    if (key < 0 || key >= VKEY_MAX) return;
    if (action == GLFW_PRESS) { w->in.down[key] = 1; w->in.pressed[key] = 1; }
    else if (action == GLFW_RELEASE) w->in.down[key] = 0;
}

static void cursor_cb(GLFWwindow *h, double x, double y) {
    Window *w = (Window *)glfwGetWindowUserPointer(h);
    double dx = x - w->lx, dy = y - w->ly;
    w->lx = x; w->ly = y;
    if (w->skip) { w->skip = 0; return; }
    w->in.mouse_dx += (float)dx; w->in.mouse_dy += (float)dy;
}

static void scroll_cb(GLFWwindow *h, double xo, double yo) {
    (void)xo;
    Window *w = (Window *)glfwGetWindowUserPointer(h);
    w->in.scroll += (float)yo;
}

Window *window_create(int width, int height, const char *title) {
    if (!glfwInit()) { VLOG("glfwInit failed"); return NULL; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);
    GLFWwindow *h = glfwCreateWindow(width, height, title, NULL, NULL);
    if (!h) { VLOG("could not create an OpenGL 3.3 window (update your graphics driver)"); glfwTerminate(); return NULL; }
    Window *w = (Window *)calloc(1, sizeof *w);
    w->h = h;
    glfwSetWindowUserPointer(h, w);
    glfwMakeContextCurrent(h);
    glfwSwapInterval(0);                       /* frame rate uncapped by default (F3 toggles vsync in the engine) */
    glfwSetKeyCallback(h, key_cb);
    glfwSetCursorPosCallback(h, cursor_cb);
    glfwSetScrollCallback(h, scroll_cb);
    glfwGetCursorPos(h, &w->lx, &w->ly);
    return w;
}

void window_destroy(Window *w) { glfwDestroyWindow(w->h); free(w); glfwTerminate(); }
int  window_should_close(Window *w) { return glfwWindowShouldClose(w->h); }
void window_request_close(Window *w) { glfwSetWindowShouldClose(w->h, 1); }

void window_poll(Window *w) {
    memset(w->in.pressed, 0, sizeof w->in.pressed);
    w->in.mouse_dx = w->in.mouse_dy = 0;
    w->in.scroll = 0;
    glfwPollEvents();
    w->in.rmb = glfwGetMouseButton(w->h, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    w->in.lmb = glfwGetMouseButton(w->h, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    w->in.lmb_pressed = w->in.lmb && !w->prev_lmb;
    w->prev_lmb = w->in.lmb;
    double cx, cy; int ww, wh, fw, fh;
    glfwGetCursorPos(w->h, &cx, &cy); glfwGetWindowSize(w->h, &ww, &wh); glfwGetFramebufferSize(w->h, &fw, &fh);
    w->in.mouse_x = ww > 0 ? (float)(cx * fw / ww) : (float)cx;
    w->in.mouse_y = wh > 0 ? (float)(cy * fh / wh) : (float)cy;
}

const Input *window_input(Window *w) { return &w->in; }
void window_swap(Window *w) { glfwSwapBuffers(w->h); }
void window_framebuffer_size(Window *w, int *fw, int *fh) { glfwGetFramebufferSize(w->h, fw, fh); }
void window_set_title(Window *w, const char *t) { glfwSetWindowTitle(w->h, t); }
double window_time(void) { return glfwGetTime(); }
void window_set_vsync(Window *w, int on) { (void)w; glfwSwapInterval(on ? 1 : 0); }

void window_set_captured(Window *w, int on) {
    if (on == w->captured) return;
    w->captured = on; w->skip = 1;
    if (on) {
        glfwSetInputMode(w->h, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        if (glfwRawMouseMotionSupported()) glfwSetInputMode(w->h, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    } else {
        if (glfwRawMouseMotionSupported()) glfwSetInputMode(w->h, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
        glfwSetInputMode(w->h, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
}
