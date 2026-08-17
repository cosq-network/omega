#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv, char** envp) {
    (void)argc; (void)argv; (void)envp;
    char* buffer = malloc(64);
    if (!buffer) return 3;
    strcpy(buffer, "Omega musl hello");
    printf("%s\n", buffer);
    free(buffer);
    return 0;
}
