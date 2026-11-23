#ifndef PS2_PTHREAD_ONCE_H
#define PS2_PTHREAD_ONCE_H
#ifdef __cplusplus
extern "C" {
#endif
/* Install after SDK thread binding and before C++ constructors. */
int ps2_pthread_once_initialize(void);
#ifdef __cplusplus
}
#endif
#endif
