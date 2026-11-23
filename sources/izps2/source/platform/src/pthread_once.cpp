#include "pthread_once.h"
#include "once.h"
#include <pthread.h>
#include <cstdlib>
namespace {
pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t condition=PTHREAD_COND_INITIALIZER;
[[noreturn]] void fatal(uint32_t){std::abort();}
void wait(uint32_t *address,uint32_t expected){
    if(pthread_mutex_lock(&mutex))fatal(1);
    while(__atomic_load_n(address,__ATOMIC_ACQUIRE)==expected)
        if(pthread_cond_wait(&condition,&mutex))fatal(2);
    if(pthread_mutex_unlock(&mutex))fatal(3);
}
void wake(uint32_t *){
    if(pthread_mutex_lock(&mutex))fatal(4);
    if(pthread_cond_broadcast(&condition))fatal(5);
    if(pthread_mutex_unlock(&mutex))fatal(6);
}
}
int ps2_pthread_once_initialize(void){Ps2OnceApi api{wait,wake,fatal};return ps2_once_initialize(&api);}
static_assert(sizeof(pthread_once_t)==sizeof(uint32_t),"port pthread_once ABI");
extern "C" int pthread_once(pthread_once_t *control,void (*initialize)(void)){
    return ps2_once_run(reinterpret_cast<uint32_t*>(control),initialize);
}
