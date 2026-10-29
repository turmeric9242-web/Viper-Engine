#ifndef VIPER_MENU_H
#define VIPER_MENU_H
#include "platform/input.h"
/* Pick-from-a-list popup used by the editor (script / texture assignment). Pure logic, no GL:
   engine.c draws it. Keyboard: Up/Down/PgUp/PgDn/Home/End, Enter = pick, Esc = close.
   Mouse: hover + click a row, wheel scrolls, click outside closes. */
#define MENU_MAX 128
typedef struct {
    int open;
    char title[48], note[64];
    char items[MENU_MAX][32];
    int count, cursor, scroll;
    int current;              /* index of the item that is currently assigned (marked), -1 = none */
    int chosen;               /* valid right after menu_update returned MENU_CHOSEN */
    int reveal;               /* scroll the cursor into view on the first update after menu_show */
    float hold, last_mx, last_my;
} Menu;
typedef struct { float px, py, pw, ph, lx, ly, lw, rowh; int rows, scale, tscale; } MenuLayout;
typedef enum { MENU_IDLE, MENU_CHOSEN, MENU_CLOSED } MenuResult;

void menu_begin(Menu *m, const char *title);           /* start filling a new list */
void menu_add(Menu *m, const char *item);
void menu_show(Menu *m, int current);                  /* open it, cursor on `current` */
void menu_layout(const Menu *m, int fbw, int fbh, MenuLayout *L);
MenuResult menu_update(Menu *m, const Input *in, float dt, int fbw, int fbh);
#endif
