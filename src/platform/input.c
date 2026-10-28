#include "platform/input.h"
#include <ctype.h>
#include <string.h>

static const struct { const char *name; int key; } NAMES[] = {
    {"SPACE", VKEY_SPACE}, {"ESC", VKEY_ESC}, {"ENTER", VKEY_ENTER}, {"TAB", VKEY_TAB},
    {"DELETE", VKEY_DELETE}, {"RIGHT", VKEY_RIGHT}, {"LEFT", VKEY_LEFT}, {"DOWN", VKEY_DOWN},
    {"UP", VKEY_UP}, {"PGUP", VKEY_PGUP}, {"PGDN", VKEY_PGDN}, {"F2", VKEY_F2},
    {"F5", VKEY_F5}, {"SHIFT", VKEY_LSHIFT}, {"CTRL", VKEY_LCTRL}
};

int input_key_from_name(const char *name) {
    char up[16]; size_t i, n = strlen(name);
    if (n == 0 || n >= sizeof up) return -1;
    for (i = 0; i < n; i++) up[i] = (char)toupper((unsigned char)name[i]);
    up[n] = 0;
    if (n == 1 && isalnum((unsigned char)up[0])) return up[0];
    for (i = 0; i < sizeof NAMES / sizeof NAMES[0]; i++)
        if (strcmp(up, NAMES[i].name) == 0) return NAMES[i].key;
    return -1;
}
