#ifndef VIPER_FS_H
#define VIPER_FS_H
/* Small file-system helpers used by the project browser (gui) and the "compile project" exporter. */

int fs_is_dir(const char *path);
int fs_mkdir(const char *path);                         /* 1 on success (already existing counts) */
int fs_copy_file(const char *src, const char *dst);     /* 1 on success */
int fs_copy_tree(const char *src, const char *dst);     /* copies every entry of src into dst (created) */
int fs_list_dirs(const char *parent, char names[][64], int max); /* fills names; returns how many */
#endif
