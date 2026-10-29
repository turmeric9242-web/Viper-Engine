#ifndef VIPER_UI_H
#define VIPER_UI_H
/* Immediate-mode 2D overlay: coloured rectangles + bitmap text, in framebuffer pixels (origin top-left).
   Colours are packed 0xRRGGBBAA. */
int  ui_init(void);
void ui_begin(int fb_w, int fb_h);
void ui_rect(float x, float y, float w, float h, unsigned rgba);
void ui_text(float x, float y, const char *s, unsigned rgba, int scale);   /* scale = integer pixel multiplier */
int  ui_text_width(const char *s, int scale);
void ui_end(void);
#endif
