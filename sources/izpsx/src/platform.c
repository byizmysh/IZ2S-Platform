/* Owned memory and file adapter. No Linux system calls or foreign FILE structs. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "platform.h"
#include "fs-file.h"
#include "sd-layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
Ps1Context *ps1_context;
static int own_errno;
int *__errno_location(void){return &own_errno;}
typedef struct Block {size_t bytes;struct Block*next;uint64_t free,pad;} Block;
static Block *heap;
static uint64_t used;
typedef struct {Ps1File *file;uint64_t at,handle;int used,eof,error;} Stream;
static Stream streams[64];
static Ps1File dynamic_files[64];
static char dynamic_names[64][PS1_SD_PATH_MAX],dynamic_paths[64][PS1_SD_PATH_MAX];
static unsigned dynamic_count;
FILE *const stdin=NULL,*const stdout=NULL,*const stderr=NULL;
void ps1_platform_init(void){heap=(void*)(uintptr_t)ps1_context->base.heap;heap->bytes=ps1_context->base.heap_bytes-sizeof(Block);heap->next=NULL;heap->free=1;used=0;memset(streams,0,sizeof streams);dynamic_count=0;ps1_ifile_reset();}
void *malloc(size_t n){
 if(n>SIZE_MAX-15)return NULL;n=(n+15)&~(size_t)15;if(!n)n=16;
 for(Block*b=heap;b;b=b->next)if(b->free&&b->bytes>=n){
  if(b->bytes>=n+sizeof(Block)+16){Block*t=(void*)((char*)(b+1)+n);*t=(Block){b->bytes-n-sizeof(Block),b->next,1,0};b->bytes=n;b->next=t;}
  b->free=0;used+=b->bytes;if(used>ps1_context->base.heap_peak)ps1_context->base.heap_peak=used;return b+1;
 }return NULL;
}
void free(void*p){if(!p)return;Block*b=(Block*)p-1;uintptr_t at=(uintptr_t)b,lo=ps1_context->base.heap,hi=lo+ps1_context->base.heap_bytes;if(at<lo||at>hi-sizeof(Block)||b->free||b->bytes>hi-at-sizeof(Block))ps1_fatal(81);used-=b->bytes;b->free=1;for(Block*a=heap;a&&a->next;)if(a->free&&a->next->free){a->bytes+=sizeof(Block)+a->next->bytes;a->next=a->next->next;}else a=a->next;}
void *calloc(size_t n,size_t s){if(s&&n>SIZE_MAX/s)return NULL;void*p=malloc(n*s);if(p)memset(p,0,n*s);return p;}
void *realloc(void*p,size_t n){if(!p)return malloc(n);if(!n){free(p);return NULL;}Block*b=(Block*)p-1;if(b->bytes>=n)return p;void*q=malloc(n);if(q){memcpy(q,p,b->bytes);free(p);}return q;}
char *strdup(const char*s){size_t n=strlen(s)+1;char*p=malloc(n);if(p)memcpy(p,s,n);return p;}
char *strndup(const char*s,size_t limit){size_t n=0;while(n<limit&&s[n])n++;char*p=malloc(n+1);if(p){memcpy(p,s,n);p[n]=0;}return p;}
void exit(int code){ps1_fatal(code?code:80);}void abort(void){ps1_fatal(82);}
char *getenv(const char*k){(void)k;return NULL;}
long sysconf(int n){return n==_SC_PAGESIZE?4096:-1;}pid_t getpid(void){return 1;}
int setvbuf(FILE*f,char*b,int mode,size_t n){return 0;}char *setlocale(int c,const char*l){return "C";}
int gettimeofday(struct timeval*t,void*z){(void)z;uint64_t v=((uint64_t(*)(void))(uintptr_t)ps1_context->base.tick)();t->tv_sec=v/ps1_context->base.tick_hz;t->tv_usec=v%ps1_context->base.tick_hz*1000000/ps1_context->base.tick_hz;return 0;}
time_t time(time_t*t){time_t v=946684800;if(t)*t=v;return v;}
struct tm *localtime(const time_t*t){static struct tm v={.tm_year=100,.tm_mon=0,.tm_mday=1};return &v;}
static Ps1File *find(const char*path){
 Ps1File*f=(void*)(uintptr_t)ps1_context->files;
 for(uint64_t i=0;i<ps1_context->file_count;i++)if(!strcmp(path,(char*)(uintptr_t)f[i].path))return f+i;
 Ps1FileIO*io=(void*)(uintptr_t)ps1_context->base.wad;
 if(io&&io->directory&&!strncmp(path,"/ps1/",5)&&strlen(path)<PS1_SD_PATH_MAX-sizeof(IZPSX_SD_GAMES)+4){
  for(unsigned i=0;i<dynamic_count;i++)if(!strcmp(path,dynamic_names[i]))return dynamic_files+i;
  if(dynamic_count==64){own_errno=EMFILE;return NULL;}
  char physical[PS1_SD_PATH_MAX];
  if(!strcmp(path,"/ps1/scph101.bin"))strcpy(physical,IZPSX_SD_BIOS);
  else{strcpy(physical,IZPSX_SD_GAMES);strcat(physical,path+4);}
  if(!ps1_sd_path_valid(IZPSX_SD_ROOT,physical)){own_errno=EINVAL;return NULL;}
  uint64_t size=0;Ps1SdDirectory*sd=(void*)(uintptr_t)io->directory;
  if(ps1_sd_statfile(sd,IZPSX_SD_ROOT,physical,&size)){own_errno=ENOENT;return NULL;}
  unsigned at=dynamic_count++;strcpy(dynamic_names[at],path);strcpy(dynamic_paths[at],physical);
  dynamic_files[at]=(Ps1File){(uintptr_t)dynamic_names[at],(uintptr_t)dynamic_paths[at],size,size,0,3};return dynamic_files+at;
 }own_errno=ENOENT;return NULL;
}
/* These SDK functions may use a per-thread error policy. Enter the SDK's
 * scoped non-aborting context for each operation and restore the saved pointer. */
static uint64_t fs_enter(Ps1FileIO*io){uint64_t previous=0;((void(*)(uint64_t*))(uintptr_t)io->enter)(&previous);return previous;}
static void fs_restore(Ps1FileIO*io,uint64_t previous){((void(*)(uint64_t))(uintptr_t)io->restore)(previous);}
static uint32_t fs_open(Ps1FileIO*io,uint64_t*out,const char*path){uint64_t previous=fs_enter(io);uint32_t rc=((uint32_t(*)(uint64_t*,const char*,int))(uintptr_t)io->open)(out,path,1);fs_restore(io,previous);return rc;}
static uint32_t fs_size(Ps1FileIO*io,int64_t*out,uint64_t handle){uint64_t previous=fs_enter(io);uint32_t rc=((uint32_t(*)(int64_t*,uint64_t))(uintptr_t)io->size)(out,handle);fs_restore(io,previous);return rc;}
static void fs_close(Ps1FileIO*io,uint64_t handle){uint64_t previous=fs_enter(io);((void(*)(uint64_t))(uintptr_t)io->close)(handle);fs_restore(io,previous);}
static uint32_t fs_read(Ps1FileIO*io,uint64_t handle,int64_t offset,void*out,uint64_t bytes){uint64_t previous=fs_enter(io);uint32_t rc=((uint32_t(*)(uint64_t,int64_t,void*,uint64_t))(uintptr_t)io->read)(handle,offset,out,bytes);fs_restore(io,previous);return rc;}
FILE *fopen(const char*path,const char*mode){
 Ps1File*f=find(path);if(!f)return NULL;int writing=strchr(mode,'w')||strchr(mode,'a')||strchr(mode,'+');
 if(writing&&(!f->writable||f->reserved)){own_errno=EROFS;return NULL;}
 for(unsigned i=0;i<64;i++)if(!streams[i].used){
  Stream*s=streams+i;*s=(Stream){.file=f,.at=strchr(mode,'a')?f->bytes:0,.used=1};
  if(f->reserved){
   Ps1FileIO*io=(void*)(uintptr_t)ps1_context->base.wad;uint32_t rc=0;int64_t size=0;
   if(f->reserved==1&&io&&io->open&&io->read&&io->size&&io->close&&io->enter&&io->restore){
    rc=fs_open(io,&s->handle,(char*)(uintptr_t)f->data);
    if(!rc){rc=fs_size(io,&size,s->handle);if(rc||size<0||(uint64_t)size!=f->bytes)fs_close(io,s->handle);}
   }else if(f->reserved==2&&io&&io->send&&f->data>0&&f->data<0x80000000ull){s->handle=f->data;rc=ps1_ifile_size(io,s->handle,&size);}
   else if(f->reserved==3&&io&&io->directory){uint32_t handle=0;uint64_t bytes=0;rc=ps1_sd_openfile((void*)(uintptr_t)io->directory,IZPSX_SD_ROOT,(char*)(uintptr_t)f->data,&handle,&bytes);s->handle=handle;size=(int64_t)bytes;if(!rc&&(bytes>INT64_MAX||bytes!=f->bytes)){ps1_sd_closefile((void*)(uintptr_t)io->directory,handle);rc=1;}}
   else{s->used=0;own_errno=ENOSYS;return NULL;}
   if(rc||size<0||(uint64_t)size!=f->bytes){io->errors++;s->used=0;own_errno=EIO;return NULL;}
   io->opened++;
  }
  if(strchr(mode,'w'))f->bytes=0;return (void*)s;
 }own_errno=EMFILE;return NULL;
}
int fclose(FILE*f){if(!f)return EOF;Stream*s=(void*)f;if(!s->used)return EOF;
 if(s->file->reserved){Ps1FileIO*io=(void*)(uintptr_t)ps1_context->base.wad;if(s->file->reserved==1)fs_close(io,s->handle);else if(s->file->reserved==3&&ps1_sd_closefile((void*)(uintptr_t)io->directory,s->handle)){io->errors++;s->error=1;return EOF;}io->opened--;if(s->file->reserved==3)ps1_ifile_reset();}
 s->used=0;return 0;
}
size_t fread(void*p,size_t z,size_t n,FILE*f){
 Stream*s=(void*)f;if(!s||!s->used||!z||n>SIZE_MAX/z)return 0;size_t bytes=z*n;
 if(s->at>s->file->bytes)bytes=0;else if(bytes>s->file->bytes-s->at){bytes=s->file->bytes-s->at;s->eof=1;}if(!bytes)return 0;
 if(s->file->reserved){Ps1FileIO*io=(void*)(uintptr_t)ps1_context->base.wad;uint32_t rc;
  if(s->file->reserved==2||s->file->reserved==3)rc=ps1_ifile_read(io,s->handle,s->at,p,bytes,s->file->bytes);
  else{rc=fs_read(io,s->handle,s->at,p,bytes);if(!rc){io->reads++;io->bytes+=bytes;}}
  if(rc){s->error=1;io->errors++;own_errno=EIO;return 0;}
 }else memcpy(p,(void*)(uintptr_t)(s->file->data+s->at),bytes);
 s->at+=bytes;return bytes/z;
}
size_t fwrite(const void*p,size_t z,size_t n,FILE*f){Stream*s=(void*)f;if(!s||!s->used||!s->file->writable||!z||n>SIZE_MAX/z)return 0;size_t bytes=z*n;if(s->at>s->file->capacity||bytes>s->file->capacity-s->at){s->error=1;own_errno=ENOSPC;return 0;}memcpy((void*)(uintptr_t)(s->file->data+s->at),p,bytes);s->at+=bytes;if(s->at>s->file->bytes)s->file->bytes=s->at;return n;}
int fseeko(FILE*f,off_t n,int w){Stream*s=(void*)f;if(!s||!s->used)return -1;int64_t start=w==SEEK_SET?0:w==SEEK_CUR?s->at:w==SEEK_END?s->file->bytes:-1;if(start<0||n < -start||n>INT64_MAX-start){own_errno=EINVAL;return -1;}s->at=start+n;s->eof=0;return 0;}
int fseek(FILE*f,long n,int w){return fseeko(f,n,w);}off_t ftello(FILE*f){Stream*s=(void*)f;return s&&s->used?(off_t)s->at:-1;}long ftell(FILE*f){return ftello(f);}
void rewind(FILE*f){fseeko(f,0,SEEK_SET);}int feof(FILE*f){return ((Stream*)(void*)f)->eof;}int ferror(FILE*f){return f?((Stream*)(void*)f)->error:1;}int fflush(FILE*f){return 0;}int fileno(FILE*f){return f?(int)((Stream*)(void*)f-streams)+3:-1;}
char *fgets(char*p,int n,FILE*f){int i=0;while(i+1<n){char c;if(fread(&c,1,1,f)!=1)break;p[i++]=c;if(c=='\n')break;}if(!i)return NULL;p[i]=0;return p;}
int getchar(void){return EOF;}
int open(const char*p,int flags,...){FILE*f=fopen(p,(flags&O_RDWR)?"r+b":(flags&O_WRONLY)?"wb":"rb");return f?fileno(f):-1;}
static FILE *fdfile(int fd){return fd>=3&&fd<67&&streams[fd-3].used?(void*)(streams+fd-3):NULL;}
int close(int fd){FILE*f=fdfile(fd);return f?fclose(f):-1;}ssize_t read(int fd,void*p,size_t n){FILE*f=fdfile(fd);return f?(ssize_t)fread(p,1,n,f):-1;}ssize_t write(int fd,const void*p,size_t n){FILE*f=fdfile(fd);return f?(ssize_t)fwrite(p,1,n,f):-1;}off_t lseek(int fd,off_t n,int w){FILE*f=fdfile(fd);return f&&!fseeko(f,n,w)?ftello(f):-1;}
int ftruncate(int fd,off_t n){Stream*s=(void*)fdfile(fd);if(!s||n<0||!s->file->writable||(uint64_t)n>s->file->capacity)return -1;s->file->bytes=n;return 0;}
static int fstat_file(Ps1File*f,struct stat*s){memset(s,0,sizeof *s);s->st_size=f->bytes;s->st_mode=S_IFREG|0444|(f->writable?0222:0);return 0;}
int stat(const char*p,struct stat*s){Ps1File*f=find(p);return f?fstat_file(f,s):-1;}int fstat(int fd,struct stat*s){Stream*f=(void*)fdfile(fd);return f?fstat_file(f->file,s):-1;}
DIR *opendir(const char*p){own_errno=ENOENT;return NULL;}struct dirent *readdir(DIR*d){return NULL;}int closedir(DIR*d){return 0;}int mkdir(const char*p,mode_t m){own_errno=EROFS;return -1;}int remove(const char*p){own_errno=EROFS;return -1;}int rename(const char*p,const char*q){own_errno=EROFS;return -1;}
void *dlopen(const char*p,int flags){return NULL;}void *dlsym(void*h,const char*n){return NULL;}int dlclose(void*h){return 0;}char *dlerror(void){return NULL;}
int printf(const char*f,...){char b[1024];va_list a;va_start(a,f);int r=vsnprintf(b,sizeof b,f,a);va_end(a);ps1_log(b);return r;}
int fprintf(FILE*out,const char*f,...){char b[1024];va_list a;va_start(a,f);int r=vsnprintf(b,sizeof b,f,a);va_end(a);ps1_log(b);return r;}
void perror(const char*s){ps1_log(s);}
void *ps1_jit_rx(size_t bytes){if(bytes>ps1_context->jit_bytes||!ps1_context->jit_rx||!ps1_context->jit_rw)ps1_fatal(91);memset((void*)(uintptr_t)ps1_context->jit_rw,0,bytes);return (void*)(uintptr_t)ps1_context->jit_rx;}
long ps1_jit_offset(void){return ps1_context->jit_rw-ps1_context->jit_rx;}
void ps1_jit_publish(void*start,void*end){uintptr_t a=(uintptr_t)start,b=(uintptr_t)end,rx=ps1_context->jit_rx,rw=ps1_context->jit_rw,n=ps1_context->jit_bytes;if(a>=rw&&a-rw<n){a-=rw;b-=rw;}else if(a>=rx&&a-rx<n){a-=rx;b-=rx;}else ps1_fatal(92);if(b<a||b>n||!ps1_context->publish)ps1_fatal(93);ps1_context->jit_flushes++;if(((int(*)(Ps1Context*,uint64_t,uint64_t))(uintptr_t)ps1_context->publish)(ps1_context,a,b))ps1_fatal(94);}
void ps1_jit_close(void){}
