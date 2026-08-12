#include <omega/stdio.h>
#include <omega/stdlib.h>
#include <omega/string.h>

int main(int argc, char** argv, char** envp) {
    (void)envp;
    if (argc != 1 || !argv || !argv[0] || strcmp(argv[0], "/init") != 0) return 2;
    char* buffer = malloc(32);
    if (!buffer) return 3;
    memcpy(buffer, "Omega C SDK init is alive", 25);
    buffer[25] = 0;
    return puts(buffer) < 0 ? 4 : 0;
}
