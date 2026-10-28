#ifndef VIPER_GUI_H
#define VIPER_GUI_H
/* C99 surface for Dear ImGui (object code in gui.cpp). Plain data only. */
#include "scene/scene.h"
#define VIP_MENU_MAX 16
#define VIP_ASSET_MAX 48
#define VIP_SCRIPT_MAX 48
#define VIP_MARKET_MAX 24

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* owner: engine.c */
    char projects[VIP_MENU_MAX][64];
    int  nprojects;
    int  counts[VIP_MENU_MAX];  /* entity count per project (-1 = no scene file) */
    char root[320];             /* absolute path of the projects/ folder */
    char projsel[64];
    char newname[64];
    char status[256];
    /* owner: gui_frame; engine.c reads + clears */
    int new_pressed, open_pressed, export_pressed, refresh_pressed, open_folder_pressed;
} GuiMenu;

/* Editor: mirrors editor.py's layout (tab bar / left assets+scene / right
   properties / bottom status / viewport toolbar). The engine owns the state;
   the UI reads it and reports clicks back through the action flags. */
typedef struct {
    /* ---- engine-owned; the UI may edit these in place ---- */
    Scene *scene;
    const char *scene_path;
    int selected;              /* primary selection, -1 = none */
    unsigned group[8];         /* multi-select bitmask (MAX_ENTITIES <= 256) */
    int tab;                   /* 0 Import | 1 Map | 2 Bake OC | 3 Lighting | 4 Scripts | 5 Market */
    int mode;                  /* 0 = select, 1 = place */
    int gizmo_mode;            /* 0 move, 1 scale, 2 rotate */
    int grid_i;                /* index into the grid sizes */
    float layer;               /* grid plane height */
    float place_rot, place_scale;
    char msg[256];             /* transient status message */
    float msg_t;
    int dirty;

    /* asset palette (built-ins + imported .obj) */
    int nassets;
    char assets[VIP_ASSET_MAX][64];
    int asset_sel;

    /* baked data presence / staleness */
    int has_lm, lm_stale, has_oc, oc_stale, has_pr, pr_stale;

    /* background bake job */
    int job_running;
    char job_name[32];
    float job_p;               /* 0..1, <0 = indeterminate */
    char job_text[128];
    char job_error[128];

    char import_log[512];

    /* scripts tab */
    int nscripts;
    char scripts[VIP_SCRIPT_MAX][64];
    int script_sel;
    int script_load_req;       /* engine reloads script_buf from script_sel */
    char script_name[64];      /* file currently open ("" = none) */
    char script_buf[24000];
    int script_dirty;
    int script_saved_flash;

    /* marketplace tab */
    int nmarket;
    char market_names[VIP_MARKET_MAX][64];
    char market_desc[VIP_MARKET_MAX][160];
    char market_files[VIP_MARKET_MAX][64];
    int market_sel;
    int market_nsources;
    char market_sources[8][160];
    int market_source_sel;
    char market_status[256];
    int market_busy;
    char market_item[64];

    /* viewport rect in window pixels (filled by the UI, used by the engine) */
    int vp_x, vp_y, vp_w, vp_h;

    /* ---- set by the UI; engine reads + clears ---- */
    int ui_hover;              /* ImGui wants the mouse (set every frame) */
    int push_undo;
    int save_pressed, play_pressed, duplicate_pressed, delete_pressed, focus_pressed;
    int select_all_pressed, undo_pressed, redo_pressed;
    int bake_oc_pressed, bake_lm_pressed, bake_pr_pressed, cancel_pressed;
    int import_pressed, assign_script_pressed;
    int script_new_pressed, script_save_pressed;
    int market_refresh_pressed, market_get_pressed, market_add_source_pressed;
} GuiEditor;

int  gui_init(void *glfw_window);
void gui_frame(GuiMenu *menu);
void gui_editor_frame(GuiEditor *ed); /* full editor shell */
void gui_shutdown(void);

#ifdef __cplusplus
}
#endif
#endif
