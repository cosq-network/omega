#include "omega-command/common.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    int number = 0;
    int first = 1;
    while (first < argc && argv[first][0] == '-' && argv[first][1]) {
        if (!strcmp(argv[first], "--")) { ++first; break; }
        for (const char *f = argv[first] + 1; *f; ++f) {
            if (*f == 'n') number = 1;
            else { fprintf(stderr, "cat: invalid option -- '%c'\n", *f); return 2; }
        }
        ++first;
    }
    if (first == argc) return omega_copy_fd(STDIN_FILENO, STDOUT_FILENO) < 0;
    int result = 0;
    unsigned long line = 1;
    for (int i = first; i < argc; ++i) {
        int fd = !strcmp(argv[i], "-") ? STDIN_FILENO : open(argv[i], O_RDONLY);
        if (fd < 0) { omega_error("cat", argv[i]); result = 1; continue; }
        if (!number) {
            if (omega_copy_fd(fd, STDOUT_FILENO) < 0) result = 1;
        } else {
            char buffer[4096]; ssize_t n;
            int at_line_start = 1;
            while ((n = read(fd, buffer, sizeof(buffer))) > 0) {
                for (ssize_t j = 0; j < n; ++j) {
                    if (at_line_start) { printf("%6lu\t", line++); at_line_start = 0; }
                    putchar(buffer[j]);
                    if (buffer[j] == '\n') at_line_start = 1;
                }
            }
            if (n < 0) result = 1;
        }
        if (fd != STDIN_FILENO) close(fd);
    }
    return result;
}
