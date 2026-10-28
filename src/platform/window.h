#ifndef VIPER_WINDOW_H
#define VIPER_WINDOW_H
#include "platform/input.h"
typedef struct Window Window;
Window *window_create(int w, int h, const char *title);
void window_destroy(Window *w);
int  window_should_close(Window *w);
void window_request_close(Window *w);
void window_poll(Window *w);                 /* pumps events, refreshes the Input snapshot */
const Input *window_input(Window *w);
void window_swap(Window *w);
void window_framebuffer_size(Window *w, int *fw, int *fh);
void window_set_title(Window *w, const char *title);
void window_set_captured(Window *w, int captured); /* hide + lock the cursor for mouse-look */
void *window_glfw(Window *w);   /* the raw GLFWwindow* (gui module) */
double window_time(void);
#endif
