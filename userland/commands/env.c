#include "omega-command/common.h"
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern char **environ;

int main(int argc, char **argv) {
    int first = 1;
    while (first < argc && argv[first][0] == '-') {
        if (!strcmp(argv[first], "--")) { ++first; break; }
        if (strcmp(argv[first], "-i")) { fprintf(stderr, "env: invalid option\n"); return 125; }
        clearenv(); ++first;
    }
    while (first < argc && strchr(argv[first], '=')) {
        char *assignment = omega_duplicate(argv[first]);
        if (!assignment || putenv(assignment) < 0) return 125;
        ++first;
    }
    if (first == argc) {
        for (char **entry = environ; entry && *entry; ++entry) puts(*entry);
        return 0;
    }
    execvp(argv[first], &argv[first]);
    omega_error("env", argv[first]);
    return errno == ENOENT ? 127 : 126;
}
