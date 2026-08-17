#include "omega-command/common.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

int main(int argc, char **argv) {
    int parents = 0; mode_t mode = 0777; int first = 1;
    while (first < argc && argv[first][0] == '-') {
        if (!strcmp(argv[first], "--")) { ++first; break; }
        if (!strcmp(argv[first], "-p")) parents = 1;
        else if (!strcmp(argv[first], "-m") && first + 1 < argc) {
            if (omega_mode(argv[++first], &mode) < 0) { omega_error_message("mkdir", "invalid mode"); return 2; }
        } else { fprintf(stderr, "mkdir: invalid option\n"); return 2; }
        ++first;
    }
    if (first == argc) { omega_error_message("mkdir", "missing operand"); return 2; }
    int result = 0;
    for (int i = first; i < argc; ++i) {
        if (parents && omega_make_parents(argv[i], mode) < 0) { omega_error("mkdir", argv[i]); result = 1; }
        if (mkdir(argv[i], mode) < 0 && !(parents && errno == EEXIST)) { omega_error("mkdir", argv[i]); result = 1; }
    }
    return result;
}
