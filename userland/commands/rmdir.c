#include "omega-command/common.h"
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv) {
    int result = 0;
    if (argc < 2) { omega_error_message("rmdir", "missing operand"); return 2; }
    for (int i = 1; i < argc; ++i) if (rmdir(argv[i]) < 0) { omega_error("rmdir", argv[i]); result = 1; }
    return result;
}
