#ifndef VIPER_BLOCKS_H
#define VIPER_BLOCKS_H
#include "scene/scene.h"
#include "platform/input.h"
/* Viper Blocks runtime. Scripts (scripts/<name>.vblocks) are the serialized form of a
   block graph: they are compiled to a flat instruction array and executed by a small VM.
   The future visual editor will read/write this same format. */
enum { BLOCKS_EV_START = 0, BLOCKS_EV_UPDATE = 1, BLOCKS_EV_KEYS = 2, BLOCKS_EV_INTERACT = 3 };

/* Gameplay callbacks the play mode installs; the logic tests build without an
   engine, so every entry is optional (NULL = the block does nothing). */
typedef struct {
    void (*damage)(float amount);          /* damage_player <n> */
    void (*message)(const char *text);     /* message <text> */
    void (*hud)(const char *text, int right); /* hud_left / hud_right <text> */
} BlocksGameApi;

int  blocks_load(const char *name);               /* -> script id, or -1 on error */
void blocks_bind_scene(Scene *s);                 /* resolve entity->script names */
void blocks_reset_vars(void);
void blocks_update_scene(Scene *s, const Input *in, float dt, int event);
void blocks_run_entity(Scene *s, int index, const Input *in, float dt, int event);
void blocks_set_game_api(const BlocksGameApi *api);
#endif
