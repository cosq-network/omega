#include "omega-command/common.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

int omega_write_all(int fd, const void *buffer, size_t length) {
    const char *cursor = (const char *)buffer;
    while (length) {
        ssize_t written = write(fd, cursor, length);
        if (written < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (written == 0) return -1;
        cursor += written;
        length -= (size_t)written;
    }
    return 0;
}

int omega_copy_fd(int input, int output) {
    char buffer[8192];
    for (;;) {
        ssize_t count = read(input, buffer, sizeof(buffer));
        if (count == 0) return 0;
        if (count < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (omega_write_all(output, buffer, (size_t)count) < 0) return -1;
    }
}

void omega_error(const char *command, const char *path) {
    fprintf(stderr, "%s: %s: %s\n", command, path, strerror(errno));
}

void omega_error_message(const char *command, const char *message) {
    fprintf(stderr, "%s: %s\n", command, message);
}

char *omega_join_path(const char *left, const char *right) {
    size_t a = strlen(left), b = strlen(right);
    int separator = a != 0 && left[a - 1] != '/';
    char *result = malloc(a + b + (size_t)separator + 1);
    if (!result) return NULL;
    memcpy(result, left, a);
    if (separator) result[a++] = '/';
    memcpy(result + a, right, b);
    result[a + b] = '\0';
    return result;
}

char *omega_duplicate(const char *text) {
    size_t length = strlen(text);
    char *copy = malloc(length + 1);
    if (!copy) return NULL;
    memcpy(copy, text, length + 1);
    return copy;
}

int omega_mode(const char *text, mode_t *mode) {
    char *end = NULL;
    unsigned long value = strtoul(text, &end, 8);
    if (!text[0] || !end || *end || value > 07777) return -1;
    *mode = (mode_t)value;
    return 0;
}

int omega_make_parents(const char *path, mode_t mode) {
    char *copy = omega_duplicate(path);
    if (!copy) return -1;
    for (char *cursor = copy + 1; *cursor; ++cursor) {
        if (*cursor != '/') continue;
        *cursor = '\0';
        if (mkdir(copy, mode) < 0 && errno != EEXIST) {
            free(copy);
            return -1;
        }
        *cursor = '/';
    }
    free(copy);
    return 0;
}

int omega_remove_tree(const char *path, int force) {
    struct stat st;
    if (lstat(path, &st) < 0) {
        if (force && errno == ENOENT) return 0;
        return -1;
    }
    if (!S_ISDIR(st.st_mode)) return unlink(path);

    DIR *directory = opendir(path);
    if (!directory) return -1;
    struct dirent *entry;
    int result = 0;
    while ((entry = readdir(directory)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        char *child = omega_join_path(path, entry->d_name);
        if (!child || omega_remove_tree(child, force) < 0) {
            if (!force) result = -1;
            if (child) free(child);
            if (!force) break;
        }
        free(child);
    }
    closedir(directory);
    if (result == 0 && rmdir(path) < 0 && !(force && errno == ENOENT)) result = -1;
    return result;
}

void omega_print_mode(mode_t mode, char type, char *out) {
    out[0] = type;
    const char bits[] = "rwxrwxrwx";
    for (int i = 0; i < 9; ++i) out[i + 1] = (mode & (1u << (8 - i))) ? bits[i] : '-';
    if (mode & S_ISUID) out[3] = (mode & S_IXUSR) ? 's' : 'S';
    if (mode & S_ISGID) out[6] = (mode & S_IXGRP) ? 's' : 'S';
    if (mode & S_ISVTX) out[9] = (mode & S_IXOTH) ? 't' : 'T';
    out[10] = '\0';
}

int omega_print_entry(const char *name, const char *display, const struct stat *st,
                     int long_format, int classify) {
    if (long_format) {
        char mode[11];
        char type = S_ISDIR(st->st_mode) ? 'd' : S_ISLNK(st->st_mode) ? 'l' : '-';
        omega_print_mode(st->st_mode, type, mode);
        printf("%s %lu %u %u %lld %s", mode, (unsigned long)st->st_nlink,
               (unsigned)st->st_uid, (unsigned)st->st_gid,
               (long long)st->st_size, display);
        if (S_ISLNK(st->st_mode)) {
            char target[PATH_MAX];
            ssize_t length = readlink(name, target, sizeof(target) - 1);
            if (length >= 0) {
                target[length] = '\0';
                printf(" -> %s", target);
            }
        }
        putchar('\n');
    } else {
        fputs(display, stdout);
        if (classify) putchar(S_ISDIR(st->st_mode) ? '/' : ((st->st_mode & 0111) ? '*' : ' '));
        putchar('\n');
    }
    return ferror(stdout) ? -1 : 0;
}
