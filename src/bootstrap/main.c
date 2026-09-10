/* Entry point of the bootstrap compiler (toolchain.md 1). */
#include <stdio.h>

#include "driver.h"

int main(int argc, char** argv) {
    return driver_main(argc, argv, stdout, stderr);
}
