#include "engine/menu.h"
#include "gfx/font.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

void menu_begin(Menu *m, const char *title) {
    memset(m, 0, sizeof *m);
    snprintf(m->title, sizeof m->title, "%s", title);
    m->current = -1;
}
void menu_add(Menu *m, const char *item) {
    if (m->count >= MENU_MAX) return;
    snprintf(m->items[m->count++], sizeof m->items[0], "%s", item);
}
static void keep_visible(Menu *m, int rows) {
    if (m->cursor < m->scroll) m->scroll = m->cursor;
    if (m->cursor >= m->scroll + rows) m->scroll = m->cursor - rows + 1;
}
static void clamp_scroll(Menu *m, int rows) {
    int max = m->count - rows; if (max < 0) max = 0;
    if (m->scroll > max) m->scroll = max;
    if (m->scroll < 0) m->scroll = 0;
}

void menu_layout(const Menu *m, int fbw, int fbh, MenuLayout *L) {
    int s = fbh >= 1400 ? 2 : 1;
    L->scale = s; L->tscale = 2 * s;
    L->rowh = (float)(FONT_H * L->tscale + 8 * s);
    float titleh = L->rowh + 8.0f * s, footh = (float)(FONT_LINE * s + 12 * s), pad = 8.0f * s;
    int maxrows = (int)((fbh * 0.72f - titleh - footh - 2 * pad) / L->rowh);
    if (maxrows < 3) maxrows = 3;
    L->rows = m->count < maxrows ? m->count : maxrows;
    if (L->rows < 1) L->rows = 1;
    L->pw = 420.0f * s; if (L->pw > fbw - 20.0f) L->pw = (float)fbw - 20.0f;
    L->ph = titleh + L->rows * L->rowh + footh + 2 * pad;
    L->px = ((float)fbw - L->pw) * 0.5f; L->py = ((float)fbh - L->ph) * 0.5f;
    L->lx = L->px + pad; L->lw = L->pw - 2 * pad; L->ly = L->py + titleh + pad;
}

void menu_show(Menu *m, int current) {
    m->open = 1; m->current = current;
    m->cursor = current > 0 ? current : 0;
    m->scroll = 0; m->reveal = 1; m->hold = 0.35f; m->last_mx = m->last_my = -1;
    if (m->cursor >= m->count) m->cursor = m->count ? m->count - 1 : 0;
}

MenuResult menu_update(Menu *m, const Input *in, float dt, int fbw, int fbh) {
    if (!m->open) return MENU_IDLE;
    MenuLayout L; menu_layout(m, fbw, fbh, &L);
    clamp_scroll(m, L.rows);
    if (m->reveal) { keep_visible(m, L.rows); m->reveal = 0; }            /* first frame: show the marked item */

    if (in->pressed[VKEY_ESC]) { m->open = 0; return MENU_CLOSED; }

    int step = 0;
    if (in->pressed[VKEY_UP]) { step = -1; m->hold = 0.35f; }
    else if (in->pressed[VKEY_DOWN]) { step = 1; m->hold = 0.35f; }
    else if (in->down[VKEY_UP] || in->down[VKEY_DOWN]) {
        m->hold -= dt;
        if (m->hold <= 0) { step = in->down[VKEY_UP] ? -1 : 1; m->hold = 0.05f; }
    } else m->hold = 0.35f;
    if (step) { m->cursor += step; keep_visible(m, L.rows); }
    if (in->pressed[VKEY_PGUP]) { m->cursor -= L.rows; keep_visible(m, L.rows); }
    if (in->pressed[VKEY_PGDN]) { m->cursor += L.rows; keep_visible(m, L.rows); }
    if (in->pressed[VKEY_HOME]) { m->cursor = 0; keep_visible(m, L.rows); }
    if (in->pressed[VKEY_END]) { m->cursor = m->count - 1; keep_visible(m, L.rows); }
    if (m->cursor >= m->count) m->cursor = m->count - 1;
    if (m->cursor < 0) m->cursor = 0;

    if (in->scroll != 0) { m->scroll -= (int)lroundf(in->scroll * 3.0f); clamp_scroll(m, L.rows); }

    /* mouse hover moves the highlight (only when the mouse actually moved, so it doesn't fight the keyboard) */
    int inlist = in->mouse_x >= L.lx && in->mouse_x < L.lx + L.lw && in->mouse_y >= L.ly && in->mouse_y < L.ly + L.rows * L.rowh;
    int row = inlist ? m->scroll + (int)((in->mouse_y - L.ly) / L.rowh) : -1;
    if (fabsf(in->mouse_x - m->last_mx) > 0.5f || fabsf(in->mouse_y - m->last_my) > 0.5f) {
        m->last_mx = in->mouse_x; m->last_my = in->mouse_y;
        if (row >= 0 && row < m->count) m->cursor = row;
    }

    if (in->pressed[VKEY_ENTER] && m->count > 0) { m->chosen = m->cursor; m->open = 0; return MENU_CHOSEN; }
    if (in->lmb_pressed) {
        if (row >= 0 && row < m->count) { m->chosen = row; m->open = 0; return MENU_CHOSEN; }
        int inpanel = in->mouse_x >= L.px && in->mouse_x < L.px + L.pw && in->mouse_y >= L.py && in->mouse_y < L.py + L.ph;
        if (!inpanel) { m->open = 0; return MENU_CLOSED; }
    }
    return MENU_IDLE;
}
