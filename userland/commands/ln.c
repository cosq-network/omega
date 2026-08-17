#include "omega-command/common.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    int symbolic = 0, force = 0, first = 1;
    while (first < argc && argv[first][0] == '-') {
        if (!strcmp(argv[first], "--")) { ++first; break; }
        for (const char *f = argv[first] + 1; *f; ++f) {
            if (*f == 's') symbolic = 1;
            else if (*f == 'f' || *f == 'n') force = 1;
            else { fprintf(stderr, "ln: invalid option -- '%c'\n", *f); return 2; }
        }
        ++first;
    }
    if (argc - first != 2) { omega_error_message("ln", "exactly two operands required"); return 2; }
    if (force) unlink(argv[first + 1]);
    int result = symbolic ? symlink(argv[first], argv[first + 1]) : link(argv[first], argv[first + 1]);
    if (result < 0) { omega_error("ln", argv[first + 1]); return 1; }
    return 0;
}
