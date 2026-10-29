#include "platform/fs.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif

int fs_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static int ci_cmp(const char *a, const char *b) {
    for (;; a++, b++) {
        int x = tolower((unsigned char)*a), y = tolower((unsigned char)*b);
        if (x != y) return x - y;
        if (!x) return 0;
    }
}

static int ext_ok(const char *ext, const char *const *exts) {
    for (int i = 0; exts[i]; i++) if (ci_cmp(ext, exts[i]) == 0) return 1;
    return 0;
}

/* Adds base name of `file` if its extension is wanted; keeps the array sorted and unique. */
static int add(const char *file, const char *const *exts, char names[][32], int n, int max) {
    const char *dot = strrchr(file, '.');
    if (!dot || dot == file || !ext_ok(dot + 1, exts)) return n;
    size_t len = (size_t)(dot - file);
    if (len == 0 || len > 31) return n;
    char base[32]; memcpy(base, file, len); base[len] = 0;
    int pos = 0;
    while (pos < n && ci_cmp(names[pos], base) < 0) pos++;
    if (pos < n && ci_cmp(names[pos], base) == 0) return n;      /* duplicate base name */
    if (n >= max) return n;
    for (int i = n; i > pos; i--) memcpy(names[i], names[i - 1], 32);
    memcpy(names[pos], base, 32);
    return n + 1;
}

int fs_list(const char *dir, const char *const *exts, char names[][32], int max) {
    int n = 0;
#ifdef _WIN32
    char pat[300]; snprintf(pat, sizeof pat, "%s\\*", dir);
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) n = add(fd.cFileName, exts, names, n, max); }
    while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) n = add(de->d_name, exts, names, n, max);
    closedir(d);
#endif
    return n;
}
