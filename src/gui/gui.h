#ifndef VIPER_GUI_H
#define VIPER_GUI_H
/* C99 surface for Dear ImGui (object code in gui.cpp). Plain data only. */
#include "scene/scene.h"
#define VIP_MENU_MAX 16

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* owner: engine.c */
    char projects[VIP_MENU_MAX][64];
    int  nprojects;
    char projsel[64];
    char newname[64];
    char status[256];
    /* owner: gui_frame; engine.c reads + clears */
    int new_pressed, open_pressed, export_pressed, refresh_pressed;
} GuiMenu;

typedef struct {
    Scene *scene;           /* full access: panel edits entity fields in place */
    int selected;           /* index into scene->e, -1 = none */
    char status[256];
    /* set to 1 by gui_editor_frame; engine.c reads + clears */
    int save_pressed, duplicate_pressed, delete_pressed;
} GuiEditor;

int  gui_init(void *glfw_window);
void gui_frame(GuiMenu *menu);
void gui_editor_frame(GuiEditor *ed); /* hierarchy + inspector, Edit mode only */
void gui_shutdown(void);

#ifdef __cplusplus
}
#endif
#endif
