#ifndef OMEGA_COMMAND_COMMON_H
#define OMEGA_COMMAND_COMMON_H

#include <dirent.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>

int omega_write_all(int fd, const void *buffer, size_t length);
int omega_copy_fd(int input, int output);
void omega_error(const char *command, const char *path);
void omega_error_message(const char *command, const char *message);
char *omega_join_path(const char *left, const char *right);
char *omega_duplicate(const char *text);
int omega_mode(const char *text, mode_t *mode);
int omega_make_parents(const char *path, mode_t mode);
int omega_remove_tree(const char *path, int force);
void omega_print_mode(mode_t mode, char type, char *out);
int omega_print_entry(const char *name, const char *display, const struct stat *st,
                     int long_format, int classify);

#endif
