#include <stdio.h>
#include <stdlib.h>
#include "cvm.h"

#define SOURCE_CAP 32768

static const char *app_paths[] = {
    "userland/apps/utilities/calc.c",
    "userland/apps/games/guess.c",
    "userland/apps/examples/hello.c",
    "userland/apps/examples/functions.c",
    "userland/apps/games/life.c",
    "userland/apps/games/tic-tac-toe.c",
    "userland/apps/examples/string-tools.c",
    "userland/apps/examples/prime-sieve.c",
    "userland/apps/examples/sort-demo.c",
    "userland/apps/examples/number-theory.c"
};

int main(void) {
    char source[SOURCE_CAP];
    char image[CVM_IMAGE_CAP];
    int count = (int)(sizeof(app_paths) / sizeof(app_paths[0]));

    for (int i = 0; i < count; i++) {
        FILE *file = fopen(app_paths[i], "rb");
        if (!file) {
            perror(app_paths[i]);
            return 1;
        }

        size_t length = fread(source, 1, sizeof(source) - 1, file);
        int read_error = ferror(file);
        fclose(file);
        if (read_error || length == sizeof(source) - 1) {
            fprintf(stderr, "compiler app test: could not read %s completely\n", app_paths[i]);
            return 1;
        }
        source[length] = 0;

        int image_size = c_compile(source, image, sizeof(image));
        if (image_size <= 0 || image[3] != '6') {
            fprintf(stderr, "compiler app test: %s failed: %s\n",
                    app_paths[i], c_compile_error());
            return 1;
        }
        printf("Aiden C compiled %s (%d-byte AEX6 image)\n", app_paths[i], image_size);
    }

    puts("All bundled DenOS C applications compile successfully.");
    return 0;
}
