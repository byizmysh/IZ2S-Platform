#include "replay.h"
#include <string.h>
int ps1_replay_take(Ps1Replay*r,void*controls,size_t capacity,const char*root){
 if(!r||!controls||capacity<880||!root)return 0;
 volatile uint32_t*c=(void*)((uint8_t*)controls+848);
 if(c[0]!=IZPSX_REPLAY_MAGIC)return 0;__atomic_thread_fence(__ATOMIC_ACQUIRE);
 uint32_t id=c[1],frames=c[2],bytes=c[3];int valid=id&&id!=r->request&&frames&&frames<=IZPSX_REPLAY_MAX&&bytes==frames*4&&capacity>=IZPSX_REPLAY_INPUT+bytes;
 char path[PS1_SD_PATH_MAX];size_t n=0;
 if(valid){const volatile char*p=(void*)((uint8_t*)controls+IZPSX_REPLAY_PATH);while(n<sizeof path&&p[n]){path[n]=p[n];n++;}if(n==sizeof path)valid=0;else{path[n]=0;valid=ps1_sd_path_valid(root,path)&&strcmp(root,path);}}
 if(valid){memcpy(r->path,path,n+1);memcpy(r->buttons,(uint8_t*)controls+IZPSX_REPLAY_INPUT,bytes);r->frames=frames;}
 r->request=id;c[0]=0;c[5]=valid?1:3;c[6]=valid?0:0xf220;__atomic_thread_fence(__ATOMIC_RELEASE);c[4]=id;return valid?1:-1;
}
