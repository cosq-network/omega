#include "omega-command/common.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int show_path(const char *path, int all, int long_format, int classify, int recursive, int explicit_operand);

static int compare_names(const void *left, const void *right) {
    const char *const *a = left;
    const char *const *b = right;
    return strcmp(*a, *b);
}

static int show_directory(const char *path, int all, int long_format, int classify, int recursive) {
    DIR *directory = opendir(path);
    if (!directory) { omega_error("ls", path); return 1; }
    struct dirent *entry;
    char **names = NULL;
    size_t count = 0, capacity = 0;
    while ((entry = readdir(directory)) != NULL) {
        if (!all && entry->d_name[0] == '.') continue;
        if (count == capacity) {
            capacity = capacity ? capacity * 2 : 32;
            char **grown = realloc(names, capacity * sizeof(*names));
            if (!grown) { closedir(directory); return 1; }
            names = grown;
        }
        names[count++] = omega_duplicate(entry->d_name);
    }
    closedir(directory);
    qsort(names, count, sizeof(*names), compare_names);
    for (size_t i = 0; i < count; ++i) {
        char *full = omega_join_path(path, names[i]);
        struct stat st;
        if (!full || lstat(full, &st) < 0) {
            if (full) free(full);
            omega_error("ls", names[i]);
            continue;
        }
        omega_print_entry(full, names[i], &st, long_format, classify);
        if (recursive && S_ISDIR(st.st_mode) && strcmp(names[i], ".") && strcmp(names[i], "..")) {
            printf("\n%s:\n", full);
            show_directory(full, all, long_format, classify, recursive);
        }
        free(full);
        free(names[i]);
    }
    free(names);
    return 0;
}

static int show_path(const char *path, int all, int long_format, int classify, int recursive, int explicit_operand) {
    struct stat st;
    if (lstat(path, &st) < 0) { omega_error("ls", path); return 1; }
    if (!S_ISDIR(st.st_mode) || explicit_operand) {
        if (S_ISDIR(st.st_mode) && explicit_operand) {
            printf("%s:\n", path);
            return show_directory(path, all, long_format, classify, recursive);
        }
        return omega_print_entry(path, path, &st, long_format, classify) < 0;
    }
    return show_directory(path, all, long_format, classify, recursive);
}

int omega_ls_main(int argc, char **argv) {
    int all = 0, long_format = 0, classify = 0, recursive = 0, directory_only = 0;
    int first = 1;
    while (first < argc && argv[first][0] == '-' && argv[first][1]) {
        if (!strcmp(argv[first], "--")) { ++first; break; }
        for (const char *flag = argv[first] + 1; *flag; ++flag) {
            if (*flag == 'a') all = 1;
            else if (*flag == 'l') long_format = 1;
            else if (*flag == 'F') classify = 1;
            else if (*flag == 'R') recursive = 1;
            else if (*flag == 'd') directory_only = 1;
            else if (*flag == '1' || *flag == 'C' || *flag == 'h' || *flag == 't') { }
            else { fprintf(stderr, "ls: invalid option -- '%c'\n", *flag); return 2; }
        }
        ++first;
    }
    if (first == argc) return show_path(".", all, long_format, classify, recursive, directory_only);
    int result = 0;
    for (int i = first; i < argc; ++i) {
        if (i != first) putchar('\n');
        if (show_path(argv[i], all, long_format, classify, recursive, directory_only || argc - first > 1) != 0) result = 1;
    }
    return result;
}

#ifndef OMEGA_LS_NO_MAIN
int main(int argc, char **argv) { return omega_ls_main(argc, argv); }
#endif
