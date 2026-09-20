#include <cstdio>
#include "sdb.h"

int main() {
    printf("========================================\n");
    printf("  Stratum-Core (SCore) - RISC-V 32-bit Simulator\n");
    printf("  Build: " __DATE__ " " __TIME__ "\n");
    printf("  Enter 'help' for debugger commands.\n");
    printf("========================================\n\n");

    sdb_mainloop();

    return 0;
}
