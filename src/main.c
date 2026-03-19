#include <stdio.h>
#include "container.h"

int main(int argc, char *argv[]) {
    if (argc < 3 || __builtin_strcmp(argv[1], "run") != 0) {
        fprintf(stderr, "Usage: %s run <program>\n", argv[0]);
        return 1;
    }
    container_run(argv[2]);
    return 0;
}