#include "omega-command/common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    int first = 1;
    while (first < argc && argv[first][0] == '-') {
        if (!strcmp(argv[first], "--")) { ++first; break; }
        if (strcmp(argv[first], "-f") && strcmp(argv[first], "-n") && strcmp(argv[first], "-i")) { fprintf(stderr, "mv: invalid option\n"); return 2; }
        ++first;
    }
    if (argc - first < 2) { omega_error_message("mv", "missing destination"); return 2; }
    const char *destination = argv[argc - 1];
    struct stat st;
    int destination_dir = stat(destination, &st) == 0 && S_ISDIR(st.st_mode);
    int result = 0;
    for (int i = first; i < argc - 1; ++i) {
        char *target = destination_dir ? omega_join_path(destination, strrchr(argv[i], '/') ? strrchr(argv[i], '/') + 1 : argv[i]) : omega_duplicate(destination);
        if (!target || rename(argv[i], target) < 0) { omega_error("mv", argv[i]); result = 1; }
        free(target);
    }
    return result;
}
