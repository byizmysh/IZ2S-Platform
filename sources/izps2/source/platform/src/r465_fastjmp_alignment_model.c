#include <assert.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
typedef struct {uint64_t pre[2],words[22],post[2];} Buffer;
extern int fastjmp_set(void*) __attribute__((returns_twice));
extern int r465_control_set(void*) __attribute__((returns_twice));
extern void fastjmp_jmp(const void*,int) __attribute__((noreturn));
extern void r465_control_jmp(const void*,int) __attribute__((noreturn));
static void init(Buffer*b,uint64_t seed){b->pre[0]=seed;b->pre[1]=~seed;b->post[0]=seed^0x519a;b->post[1]=~seed^0x519a;}
static void check(const Buffer*b,uint64_t seed){assert(b->pre[0]==seed&&b->pre[1]==~seed&&b->post[0]==(seed^0x519a)&&b->post[1]==(~seed^0x519a));}
__attribute__((noinline,noreturn)) static void nested(Buffer*outer,int value,int lane,uint64_t seed){
 Buffer inner __attribute__((aligned(16)));init(&inner,seed);volatile int stage=0;
 volatile uint64_t live[16];for(unsigned i=0;i<16;i++)live[i]=seed+i*17;
 int result=lane?fastjmp_set(inner.words):r465_control_set(inner.words);
 if(!stage){stage=1;if(lane)fastjmp_jmp(inner.words,value);else r465_control_jmp(inner.words,value);}
 assert(stage==1&&result==value);check(&inner,seed);for(unsigned i=0;i<16;i++)assert(live[i]==seed+i*17);
 if(lane)fastjmp_jmp(outer->words,value);else r465_control_jmp(outer->words,value);
}
__attribute__((noinline)) static uint64_t roundtrip(int lane,int value,uint64_t seed){
 Buffer outer __attribute__((aligned(16)));init(&outer,seed);volatile int stage=0;
 volatile uint64_t live[16];for(unsigned i=0;i<16;i++)live[i]=seed+i*31;
 int result=lane?fastjmp_set(outer.words):r465_control_set(outer.words);
 if(!stage){stage=1;nested(&outer,value,lane,seed^0x812973);}
 assert(stage==1&&result==value);check(&outer,seed);uint64_t sum=(uint32_t)result;
 for(unsigned i=0;i<16;i++){assert(live[i]==seed+i*31);sum^=live[i];}return sum;
}
typedef struct {unsigned id,cases;uint64_t checksum;} Job;
static void*worker(void*arg){Job*j=arg;const int values[6]={0,1,-1,INT_MIN,INT_MAX,73};
 for(unsigned i=0;i<8;i++)for(unsigned k=0;k<6;k++){
  uint64_t seed=UINT64_C(0x739da91be001)+(uint64_t)j->id*731+i*971;
  uint64_t before=roundtrip(0,values[k],seed),after=roundtrip(1,values[k],seed);assert(before==after);j->checksum^=after;j->cases++;
 }return 0;
}
int main(void){pthread_t threads[4];Job jobs[4]={0};unsigned cases=0;uint64_t checksum=0;
 assert(sizeof(((Buffer*)0)->words)==176&&_Alignof(Buffer)>=8);
 for(unsigned i=0;i<4;i++){jobs[i].id=i;assert(!pthread_create(&threads[i],0,worker,&jobs[i]));}
 for(unsigned i=0;i<4;i++){assert(!pthread_join(threads[i],0));cases+=jobs[i].cases;checksum^=jobs[i].checksum;}
 assert(cases==192);printf("{\"passed\":true,\"pairedCases\":%u,\"nestedRoundTrips\":%u,\"threads\":4,\"checksum\":%llu}\n",cases,cases*4,(unsigned long long)checksum);return 0;
}
