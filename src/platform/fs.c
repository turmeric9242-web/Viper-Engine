#include "platform/fs.h"
#include "core/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <direct.h>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comdlg32.lib")
#else
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#endif

int fs_is_dir(const char *path) {
#ifdef _MSC_VER
    struct _stat s;
    if (!path || _stat(path, &s) != 0) return 0;
    return (s.st_mode & _S_IFDIR) != 0;
#else
    struct stat s;
    if (!path || stat(path, &s) != 0) return 0;
    return S_ISDIR(s.st_mode);
#endif
}

int fs_mkdir(const char *path) {
    if (!path || !path[0]) return 0;
#ifdef _WIN32
    if (_mkdir(path) == 0) return 1;
#else
    if (mkdir(path, 0775) == 0) return 1;
#endif
    return fs_is_dir(path); /* already existing counts as success */
}

int fs_copy_file(const char *src, const char *dst) {
    FILE *a = fopen(src, "rb");
    if (!a) { VLOG("fs: cannot read %s", src); return 0; }
    FILE *b = fopen(dst, "wb");
    if (!b) { VLOG("fs: cannot write %s", dst); fclose(a); return 0; }
    {
        char buf[65536];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, a)) > 0) fwrite(buf, 1, n, b);
    }
    {
        int err = ferror(a) | ferror(b);
        fclose(a);
        fclose(b);
        return err == 0;
    }
}

int fs_copy_tree(const char *src, const char *dst) {
    if (!fs_is_dir(src)) return 0;
    if (!fs_mkdir(dst)) return 0;
    {
        int ok = 1;
#ifdef _WIN32
        char pat[540], s[540], d[540];
        WIN32_FIND_DATAA fd;
        HANDLE h;
        snprintf(pat, sizeof pat, "%s/*", src);
        h = FindFirstFileA(pat, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
                snprintf(s, sizeof s, "%s/%s", src, fd.cFileName);
                snprintf(d, sizeof d, "%s/%s", dst, fd.cFileName);
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { if (!fs_copy_tree(s, d)) ok = 0; }
                else if (!fs_copy_file(s, d)) ok = 0;
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
#else
        DIR *dir = opendir(src);
        if (dir) {
            struct dirent *de;
            while ((de = readdir(dir))) {
                char s[540], d[540];
                struct stat st;
                if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
                snprintf(s, sizeof s, "%s/%s", src, de->d_name);
                snprintf(d, sizeof d, "%s/%s", dst, de->d_name);
                if (stat(s, &st) != 0) continue;
                if (S_ISDIR(st.st_mode)) { if (!fs_copy_tree(s, d)) ok = 0; }
                else if (!fs_copy_file(s, d)) ok = 0;
            }
            closedir(dir);
        }
#endif
        return ok;
    }
}

int fs_list_dirs(const char *parent, char names[][64], int max) {
    int n = 0;
    if (!fs_is_dir(parent)) return 0;
#ifdef _WIN32
    {
        char pat[540];
        WIN32_FIND_DATAA fd;
        HANDLE h;
        snprintf(pat, sizeof pat, "%s/*", parent);
        h = FindFirstFileA(pat, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (n >= max) break;
                if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && strcmp(fd.cFileName, ".") && strcmp(fd.cFileName, ".."))
                    snprintf(names[n++], 64, "%s", fd.cFileName);
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
    }
#else
    {
        DIR *dir = opendir(parent);
        if (dir) {
            struct dirent *de;
            while (n < max && (de = readdir(dir))) {
                if (de->d_name[0] == '.') continue;
                {
                    char p[540];
                    snprintf(p, sizeof p, "%s/%s", parent, de->d_name);
                    if (fs_is_dir(p)) snprintf(names[n++], 64, "%s", de->d_name);
                }
            }
            closedir(dir);
        }
    }
#endif
    return n;
}

/* case-insensitive ".ext" suffix test; ext == NULL/"" accepts every file */
static int has_ext(const char *name, const char *ext) {
    size_t n, e;
    if (!ext || !ext[0]) return 1;
    n = strlen(name);
    e = strlen(ext);
    if (n <= e) return 0;
    for (size_t i = 0; i < e; i++) {
        char a = name[n - e + i], b = ext[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return 0;
    }
    return 1;
}

int fs_list_files(const char *parent, char names[][64], int max, const char *ext) {
    int n = 0;
    if (!fs_is_dir(parent)) return 0;
#ifdef _WIN32
    {
        char pat[540];
        WIN32_FIND_DATAA fd;
        HANDLE h;
        snprintf(pat, sizeof pat, "%s/*", parent);
        h = FindFirstFileA(pat, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (n >= max) break;
                if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 && has_ext(fd.cFileName, ext))
                    snprintf(names[n++], 64, "%s", fd.cFileName);
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
    }
#else
    {
        DIR *dir = opendir(parent);
        if (dir) {
            struct dirent *de;
            while (n < max && (de = readdir(dir))) {
                char p[540];
                if (de->d_name[0] == '.') continue;
                snprintf(p, sizeof p, "%s/%s", parent, de->d_name);
                if (!fs_is_dir(p) && has_ext(de->d_name, ext)) snprintf(names[n++], 64, "%s", de->d_name);
            }
            closedir(dir);
        }
    }
#endif
    return n;
}

int fs_pick_file(char *out, int n, const char *filter_name, const char *filter_ext) {
    if (!out || n < 2) return 0;
    out[0] = 0;
#ifdef _WIN32
    {
        OPENFILENAMEA ofn;
        char buf[520] = "";
        char filter[260];
        snprintf(filter, sizeof filter, "%s (*.%s)%c*.%s%cAll files (*.*)%c*.*%c%c",
                 filter_name && filter_name[0] ? filter_name : "Files",
                 filter_ext && filter_ext[0] ? filter_ext : "*",
                 0,
                 filter_ext && filter_ext[0] ? filter_ext : "*",
                 0, 0, 0, 0);
        memset(&ofn, 0, sizeof ofn);
        ofn.lStructSize = sizeof ofn;
        ofn.lpstrFilter = filter;
        ofn.lpstrFile = buf;
        ofn.nMaxFile = sizeof buf;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetOpenFileNameA(&ofn)) return 0;
        snprintf(out, (size_t)n, "%s", buf);
        return 1;
    }
#else
    (void)filter_name; (void)filter_ext;
    return 0;
#endif
}

int fs_getcwd(char *buf, int n) {
    if (!buf || n <= 0) return 0;
#ifdef _WIN32
    return _getcwd(buf, n) != NULL;
#else
    return getcwd(buf, (size_t)n) != NULL;
#endif
}

int fs_open_path(const char *path) {
    if (!path || !path[0]) return 0;
#ifdef _WIN32
    return (int)(size_t)ShellExecuteA(NULL, "open", path, NULL, NULL, SW_SHOWNORMAL) > 32;
#else
    char cmd[600];
    snprintf(cmd, sizeof cmd, "xdg-open '%s' >/dev/null 2>&1 &", path);
    return system(cmd) == 0;
#endif
}
