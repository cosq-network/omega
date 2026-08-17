#include <unistd.h>

int main(void) {
    char *const argv[] = {"/bin/echo", "Omega execve command probe", 0};
    char *const envp[] = {"OMEGA_PROBE=1", 0};
    execve("/bin/echo", argv, envp);
    return 111;
}
