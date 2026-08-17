#include "omega-command/common.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    int recursive = 0, force = 0, first = 1;
    while (first < argc && argv[first][0] == '-') {
        if (!strcmp(argv[first], "--")) { ++first; break; }
        for (const char *f = argv[first] + 1; *f; ++f) {
            if (*f == 'r' || *f == 'R') recursive = 1;
            else if (*f == 'f') force = 1;
            else if (*f == 'i') { }
            else { fprintf(stderr, "rm: invalid option -- '%c'\n", *f); return 2; }
        }
        ++first;
    }
    if (first == argc) { omega_error_message("rm", "missing operand"); return force ? 0 : 2; }
    int result = 0;
    for (int i = first; i < argc; ++i) {
        struct stat st;
        if (lstat(argv[i], &st) < 0) { if (!force) { omega_error("rm", argv[i]); result = 1; } continue; }
        if (S_ISDIR(st.st_mode) && !recursive) { omega_error_message("rm", "is a directory"); result = 1; continue; }
        if (omega_remove_tree(argv[i], force) < 0) { omega_error("rm", argv[i]); result = 1; }
    }
    return result;
}
