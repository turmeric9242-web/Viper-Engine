#ifndef VIPER_THREAD_H
#define VIPER_THREAD_H
/* Minimal one-shot background thread helper (bakers, downloads). */
#ifdef __cplusplus
extern "C" {
#endif

typedef struct VThread VThread;
VThread *vthread_start(void (*fn)(void *), void *arg); /* NULL if the thread could not start */
void vthread_join(VThread *t);                          /* waits for the thread and frees it */

#ifdef __cplusplus
}
#endif
#endif
