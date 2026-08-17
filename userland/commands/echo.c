#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    int newline = 1, first = 1;
    if (first < argc && !strcmp(argv[first], "-n")) { newline = 0; ++first; }
    for (int i = first; i < argc; ++i) {
        if (i != first) putchar(' ');
        fputs(argv[i], stdout);
    }
    if (newline) putchar('\n');
    return ferror(stdout) ? 1 : 0;
}
