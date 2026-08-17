// Minimal musl smoke test: no malloc, just write+exit.
#include <unistd.h>

int main(int argc, char** argv, char** envp) {
    (void)argc; (void)argv; (void)envp;
    const char msg[] = "Omega musl no-malloc ok\n";
    write(1, msg, sizeof(msg) - 1);
    return 0;
}
