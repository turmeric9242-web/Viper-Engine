#ifndef VIPER_INPUT_H
#define VIPER_INPUT_H
/* Platform-independent input snapshot. Key codes match GLFW's values
   (letters/digits are uppercase ASCII). */
#define VKEY_MAX 350
enum {
    VKEY_SPACE = 32, VKEY_ESC = 256, VKEY_ENTER = 257, VKEY_TAB = 258,
    VKEY_DELETE = 261, VKEY_RIGHT = 262, VKEY_LEFT = 263, VKEY_DOWN = 264,
    VKEY_UP = 265, VKEY_PGUP = 266, VKEY_PGDN = 267,
    VKEY_F1 = 290, VKEY_F2 = 291, VKEY_F3 = 292, VKEY_F4 = 293, VKEY_F5 = 294,
    VKEY_F6 = 295,
    VKEY_LSHIFT = 340, VKEY_LCTRL = 341, VKEY_MINUS = 45, VKEY_EQUAL = 61
};
typedef struct {
    unsigned char down[VKEY_MAX];
    unsigned char pressed[VKEY_MAX]; /* went down this frame */
    float mouse_dx, mouse_dy;        /* movement this frame (relative look) */
    float mx, my;                    /* absolute cursor position, window pixels */
    int rmb, lmb;                    /* current button state */
    int lmb_pressed;                 /* left button went down this frame */
    float wheel;                     /* scroll delta this frame */
} Input;

int input_key_from_name(const char *name); /* "W", "SPACE", "SHIFT"... -1 if unknown */
#endif
