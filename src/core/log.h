#ifndef VIPER_LOG_H
#define VIPER_LOG_H
#include <stdio.h>
#define VLOG(...) do { printf("[viper] "); printf(__VA_ARGS__); printf("\n"); fflush(stdout); } while (0)
#endif
