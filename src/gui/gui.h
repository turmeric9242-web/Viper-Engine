#ifndef VIPER_GUI_H
#define VIPER_GUI_H
/* C99 surface for Dear ImGui (whose object code lives in gui.cpp).
   Only plain data crosses this boundary so engine.c stays C. */
#define VIP_MENU_MAX 16

typedef struct {
    /* owner: engine.c */
    char projects[VIP_MENU_MAX][64]; /* folder names under projects/ */
    int  nprojects;
    char projsel[64];   /* highlight + "compile/open" target */
    char newname[64];   /* text input for New project */
    char status[256];   /* one-line feedback shown in the menu */
    /* owner: gui_frame; engine.c reads + clears */
    int new_pressed, open_pressed, export_pressed, refresh_pressed;
} GuiMenu;

int  gui_init(void *glfw_window); /* Window::h as void* keeps GLFW out of headers */
void gui_frame(GuiMenu *menu);    /* NewFrame + project browser + Render, one complete frame */
void gui_shutdown(void);
#endif
