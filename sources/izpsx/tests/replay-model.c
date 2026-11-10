#include "replay.h"
#include "sd-layout.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void){
 size_t bytes=32768;uint8_t*c=calloc(1,bytes);assert(c);uint32_t*q=(void*)(c+848);Ps1Replay r={0};
 assert(!ps1_replay_take(&r,c,bytes,IZPSX_SD_GAMES));strcpy((char*)c+4096,IZPSX_SD_GAMES "/tekken3/game.cue");
 uint32_t*input=(void*)(c+8192);for(unsigned i=0;i<2400;i++)input[i]=i&0xffff;
 q[0]=IZPSX_REPLAY_MAGIC;q[1]=1;q[2]=2400;q[3]=9600;assert(ps1_replay_take(&r,c,bytes,IZPSX_SD_GAMES)==1&&r.frames==2400&&r.buttons[2399]==2399&&!q[0]&&q[4]==1&&q[5]==1&&!q[6]);
 assert(!ps1_replay_take(&r,c,bytes,IZPSX_SD_GAMES));q[0]=IZPSX_REPLAY_MAGIC;assert(ps1_replay_take(&r,c,bytes,IZPSX_SD_GAMES)==-1&&q[4]==1&&q[5]==3);
 const char*bad[]={IZPSX_SD_GAMES "/../bios/scph101.bin",IZPSX_SD_GAMES,"/sd/izmysh/ps2/game.iso","relative.cue",IZPSX_SD_GAMES "/a\\b.cue"};
 for(unsigned i=0;i<sizeof bad/sizeof bad[0];i++){strcpy((char*)c+4096,bad[i]);q[0]=IZPSX_REPLAY_MAGIC;q[1]++;assert(ps1_replay_take(&r,c,bytes,IZPSX_SD_GAMES)==-1&&q[5]==3);}
 strcpy((char*)c+4096,IZPSX_SD_GAMES "/valid.cue");uint32_t counts[]={0,3601,UINT32_MAX};
 for(unsigned i=0;i<3;i++){q[0]=IZPSX_REPLAY_MAGIC;q[1]++;q[2]=counts[i];assert(ps1_replay_take(&r,c,bytes,IZPSX_SD_GAMES)==-1);}
 q[0]=IZPSX_REPLAY_MAGIC;q[1]++;q[2]=2400;q[3]=9599;assert(ps1_replay_take(&r,c,bytes,IZPSX_SD_GAMES)==-1);
 q[0]=IZPSX_REPLAY_MAGIC;q[1]++;q[3]=9600;assert(ps1_replay_take(&r,c,9000,IZPSX_SD_GAMES)==-1);
 memset(c+4096,'x',PS1_SD_PATH_MAX);q[0]=IZPSX_REPLAY_MAGIC;q[1]++;assert(ps1_replay_take(&r,c,bytes,IZPSX_SD_GAMES)==-1);
 free(c);puts("PASS: finite recorded input, exactly-once acknowledgement, path scope, replay rejection, frame/byte bounds and unterminated path");return 0;
}
