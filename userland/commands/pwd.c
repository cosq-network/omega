#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    int physical = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-P")) physical = 1;
        else if (!strcmp(argv[i], "-L")) physical = 0;
        else { fprintf(stderr, "pwd: invalid option\n"); return 2; }
    }
    (void)physical;
    char *path = getcwd(NULL, 0);
    if (!path) { perror("pwd"); return 1; }
    puts(path); free(path); return 0;
}
