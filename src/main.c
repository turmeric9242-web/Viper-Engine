#include <stddef.h>
#include <string.h>
#include "engine/engine.h"
int main(int argc, char **argv) {
    const char *scene = NULL;
    const char *self = argc > 0 ? argv[0] : "viper.exe";
    int play = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--play") || !strcmp(argv[i], "-play") || !strcmp(argv[i], "--game")) play = 1;
        else if (!scene && argv[i][0]) scene = argv[i];
    }
    return engine_run(scene, self, play);
}
