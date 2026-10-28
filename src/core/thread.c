#include "core/thread.h"
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>

typedef struct { void (*fn)(void *); void *arg; } Pack;

static DWORD WINAPI trampoline(LPVOID p) {
    Pack *k = (Pack *)p;
    k->fn(k->arg);
    free(k);
    return 0;
}

struct VThread { HANDLE h; };

VThread *vthread_start(void (*fn)(void *), void *arg) {
    Pack *k = (Pack *)malloc(sizeof *k);
    VThread *t = (VThread *)malloc(sizeof *t);
    if (!k || !t) { free(k); free(t); return NULL; }
    k->fn = fn; k->arg = arg;
    t->h = CreateThread(NULL, 0, trampoline, k, 0, NULL);
    if (!t->h) { free(k); free(t); return NULL; }
    return t;
}

void vthread_join(VThread *t) {
    if (!t) return;
    WaitForSingleObject(t->h, INFINITE);
    CloseHandle(t->h);
    free(t);
}

#else

#include <pthread.h>

typedef struct { void (*fn)(void *); void *arg; } Pack;

static void *trampoline(void *p) {
    Pack *k = (Pack *)p;
    k->fn(k->arg);
    free(k);
    return NULL;
}

struct VThread { pthread_t h; };

VThread *vthread_start(void (*fn)(void *), void *arg) {
    Pack *k = (Pack *)malloc(sizeof *k);
    VThread *t = (VThread *)malloc(sizeof *t);
    if (!k || !t) { free(k); free(t); return NULL; }
    k->fn = fn; k->arg = arg;
    if (pthread_create(&t->h, NULL, trampoline, k) != 0) { free(k); free(t); return NULL; }
    return t;
}

void vthread_join(VThread *t) {
    if (!t) return;
    pthread_join(t->h, NULL);
    free(t);
}

#endif
