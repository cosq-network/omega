#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

int main(int argc, char **argv) {
    if (argc == 1) return 1;
    if (argc == 3 && (!strcmp(argv[1], "-n") || !strcmp(argv[1], "-z"))) {
        int nonempty = argv[2][0] != '\0';
        return (!strcmp(argv[1], "-n") ? nonempty : !nonempty) ? 0 : 1;
    }
    if (argc == 4 && (!strcmp(argv[2], "=") || !strcmp(argv[2], "==") || !strcmp(argv[2], "!="))) {
        int equal = !strcmp(argv[1], argv[3]);
        return (!strcmp(argv[2], "!=") ? !equal : equal) ? 0 : 1;
    }
    if (argc == 3 && (argv[1][0] == '-' && argv[1][2] == '\0')) {
        struct stat st;
        if (!strcmp(argv[1], "-e")) return stat(argv[2], &st) == 0 ? 0 : 1;
        if (!strcmp(argv[1], "-f")) return stat(argv[2], &st) == 0 && S_ISREG(st.st_mode) ? 0 : 1;
        if (!strcmp(argv[1], "-d")) return stat(argv[2], &st) == 0 && S_ISDIR(st.st_mode) ? 0 : 1;
    }
    return 2;
}
