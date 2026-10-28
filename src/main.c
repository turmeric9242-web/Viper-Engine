#include <stddef.h>
#include "engine/engine.h"
int main(int argc, char **argv) {
    return engine_run(argc > 1 ? argv[1] : NULL, argc > 0 ? argv[0] : "viper.exe");
}
