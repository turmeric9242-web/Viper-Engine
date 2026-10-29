#ifndef VIPER_FS_H
#define VIPER_FS_H
/* Tiny cross-platform directory listing (no GLFW, usable from headless tests). */
int fs_exists(const char *path);
/* Lists files in `dir` whose extension matches one of `exts` (NULL-terminated, e.g. {"png","bmp",NULL},
   case-insensitive). Writes base names (no extension, max 31 chars) sorted A-Z, no duplicates.
   Returns the number written (<= max). A missing directory returns 0. */
int fs_list(const char *dir, const char *const *exts, char names[][32], int max);
#endif
