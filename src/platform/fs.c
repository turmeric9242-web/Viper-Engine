#include "platform/fs.h"
#include "core/log.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#else
#include <sys/types.h>
#include <dirent.h>
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
        _finddata_t fd;
        intptr_t h;
        snprintf(pat, sizeof pat, "%s/*.*", src);
        h = _findfirst(pat, &fd);
        if (h != -1) {
            do {
                if (!strcmp(fd.name, ".") || !strcmp(fd.name, "..")) continue;
                snprintf(s, sizeof s, "%s/%s", src, fd.name);
                snprintf(d, sizeof d, "%s/%s", dst, fd.name);
                if (fd.attrib & _A_SUBDIR) { if (!fs_copy_tree(s, d)) ok = 0; }
                else if (!fs_copy_file(s, d)) ok = 0;
            } while (_findnext(h, &fd) == 0);
            _findclose(h);
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
        _finddata_t fd;
        intptr_t h;
        snprintf(pat, sizeof pat, "%s/*.*", parent);
        h = _findfirst(pat, &fd);
        if (h != -1) {
            do {
                if (n >= max) break;
                if ((fd.attrib & _A_SUBDIR) && strcmp(fd.name, ".") && strcmp(fd.name, ".."))
                    snprintf(names[n++], 64, "%s", fd.name);
            } while (_findnext(h, &fd) == 0);
            _findclose(h);
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
