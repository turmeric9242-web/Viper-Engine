#ifndef VIPER_FONT_H
#define VIPER_FONT_H
#define FONT_W 7        /* glyph bitmap width  (bit 6 = leftmost pixel) */
#define FONT_H 12       /* glyph bitmap height */
#define FONT_ADV 7      /* horizontal advance */
#define FONT_LINE 14    /* line height */
extern const unsigned char VFONT[95][12]; /* ASCII 32..126 */
#endif
