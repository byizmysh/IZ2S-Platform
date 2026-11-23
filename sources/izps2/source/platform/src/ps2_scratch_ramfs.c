#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "ps2_scratch_ramfs.h"
#include <limits.h>
#include <fcntl.h>
#include <string.h>
#define FILE_KIND 2u
#define DIR_KIND 1u
#define WRITABLE(flags) (((flags)&3u)!=0)
static size_t bounded(const char*s,size_t n){size_t i=0;if(!s)return n;while(i<n&&s[i])i++;return i;}
static int overlap(const void*a,size_t n,const void*b,size_t m){uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return n&&m&&(x>UINTPTR_MAX-n||y>UINTPTR_MAX-m||(x<y+m&&y<x+n));}
static int canonical(const char*p){size_t n=bounded(p,PS2_SCRATCH_PATH);if(n<2||n==PS2_SCRATCH_PATH||p[0]!='/'||p[n-1]=='/')return -22;
 for(size_t i=1;i<n;){size_t start=i;while(i<n&&p[i]!='/'){unsigned char c=(unsigned char)p[i++];if(c<32||c==127||c=='\\'||c==':')return -22;}size_t k=i-start;if(!k||k>=PS2_SCRATCH_NAME||(k==1&&p[start]=='.')||(k==2&&p[start]=='.'&&p[start+1]=='.'))return -22;if(i<n)i++;}return 0;}
int ps2_scratch_contains(const Ps2ScratchRamfs*f,const char*p){if(!f||!f->mounted||!p)return 0;size_t n=strlen(f->root);return !strncmp(p,f->root,n)&&(!p[n]||p[n]=='/');}
static int valid(Ps2ScratchRamfs*f){return f&&f->mounted;}
static int child_index(Ps2ScratchRamfs*f,unsigned parent,const char*n,size_t len){for(unsigned i=1;i<PS2_SCRATCH_DENTS;i++)if(f->dents[i].used&&f->dents[i].parent==parent&&strlen(f->dents[i].name)==len&&!memcmp(f->dents[i].name,n,len))return (int)i;return -2;}
static int walk(Ps2ScratchRamfs*f,const char*p,int parent,char name[PS2_SCRATCH_NAME]){if(!valid(f)||canonical(p)||!ps2_scratch_contains(f,p))return -22;size_t root=strlen(f->root);if(!p[root])return parent?-16:0;unsigned at=0;const char*s=p+root+1;
 for(;;){const char*e=strchr(s,'/');size_t n=e?(size_t)(e-s):strlen(s);if(f->nodes[f->dents[at].node].kind!=DIR_KIND)return -20;
  if(parent&&!e){memcpy(name,s,n);name[n]=0;return (int)at;}int next=child_index(f,at,s,n);if(next<0)return next;at=(unsigned)next;if(!e)return (int)at;s=e+1;}}
static int free_dent(Ps2ScratchRamfs*f){for(unsigned i=1;i<PS2_SCRATCH_DENTS;i++)if(!f->dents[i].used)return (int)i;return -28;}
static int free_node(Ps2ScratchRamfs*f){for(unsigned i=1;i<PS2_SCRATCH_NODES;i++)if(!f->nodes[i].used)return (int)i;return -28;}
static int is_empty(Ps2ScratchRamfs*f,unsigned dent){for(unsigned i=1;i<PS2_SCRATCH_DENTS;i++)if(f->dents[i].used&&f->dents[i].parent==dent)return 0;return 1;}
static void release_node(Ps2ScratchRamfs*f,unsigned i){Ps2ScratchNode*n=&f->nodes[i];if(n->links||n->opens)return;for(uint64_t p=0;p<n->pages;p++)f->page_map[n->first+p]=0;memset(n,0,sizeof(*n));}
static void remove_dent(Ps2ScratchRamfs*f,unsigned at){unsigned node=f->dents[at].node;memset(&f->dents[at],0,sizeof(f->dents[at]));f->nodes[node].links--;f->nodes[node].version=++f->epoch;release_node(f,node);}
static void stat_node(const Ps2ScratchNode*n,Ps2ScratchStat*s){memset(s,0,sizeof(*s));*s=(Ps2ScratchStat){n->ino,n->bytes,n->version,n->kind,n->links,n->mode};}
static Ps2ScratchDescription*description(Ps2ScratchRamfs*f,unsigned fd){if(!valid(f)||fd>=PS2_SCRATCH_FDS||!f->fds[fd].used)return 0;return &f->descriptions[f->fds[fd].description];}
static int buffer(Ps2ScratchRamfs*f,const void*p,size_t n){if(!p&&n)return -14;if(n&&((uintptr_t)p>UINTPTR_MAX-n||overlap(p,n,f,sizeof(*f))||overlap(p,n,f->data,f->capacity)||overlap(p,n,f->page_map,f->page_count)))return -22;return 0;}
static int resize_node(Ps2ScratchRamfs*f,Ps2ScratchNode*n,uint64_t bytes){if(bytes>INT64_MAX)return -22;if(bytes>f->capacity)return -28;if(bytes==n->bytes)return 0;
 uint64_t need=(bytes+4095)/4096;
 if(need>n->pages){uint64_t cap=n->pages?n->pages:1;while(cap<need&&cap<f->page_count)cap*=2;if(cap>f->page_count)cap=need;uint64_t first=UINT64_MAX;
  for(unsigned attempt=0;attempt<2;attempt++){
   if(n->pages&&n->first+cap<=f->page_count){uint64_t p=n->pages;while(p<cap&&!f->page_map[n->first+p])p++;if(p==cap)first=n->first;}
   if(first==UINT64_MAX){uint64_t run=0;for(uint64_t p=0;p<f->page_count;p++){run=f->page_map[p]?0:run+1;if(run==cap){first=p+1-cap;break;}}}
   if(first!=UINT64_MAX)break;if(cap==need)return -28;cap=need;
  }
  if(first==UINT64_MAX)return -28;
  if(first==n->first&&n->pages){for(uint64_t p=n->pages;p<cap;p++)f->page_map[first+p]=1;memset(f->data+(first+n->pages)*4096,0,(size_t)(cap-n->pages)*4096);}
  else{for(uint64_t p=0;p<cap;p++)f->page_map[first+p]=1;memset(f->data+first*4096,0,(size_t)cap*4096);if(n->bytes)memcpy(f->data+first*4096,f->data+n->first*4096,(size_t)n->bytes);for(uint64_t p=0;p<n->pages;p++)f->page_map[n->first+p]=0;}
  n->first=first;n->pages=cap;
 }else if(bytes<n->bytes){for(uint64_t p=need;p<n->pages;p++)f->page_map[n->first+p]=0;n->pages=need;if(!need)n->first=0;}
 if(bytes>n->bytes)memset(f->data+n->first*4096+n->bytes,0,(size_t)(bytes-n->bytes));
 if(bytes<n->bytes&&need)memset(f->data+n->first*4096+bytes,0,(size_t)(need*4096-bytes));
 n->bytes=bytes;n->version=++f->epoch;return 0;
}
int ps2_scratch_mount(Ps2ScratchRamfs*f,const char*root,void*data,size_t bytes,void*map,size_t map_bytes){if(!f||f->mounted||canonical(root)||!data||!map||!bytes||bytes%4096||bytes>PS2_SCRATCH_MAX_BYTES||map_bytes<bytes/4096)return -22;
 if(!strncmp(root,"/resources",10)&&(!root[10]||root[10]=='/'))return -22;if(!strncmp(root,"/sd",3)&&(!root[3]||root[3]=='/'))return -22;
 if(overlap(f,sizeof(*f),data,bytes)||overlap(f,sizeof(*f),map,bytes/4096)||overlap(data,bytes,map,bytes/4096)||overlap(f,sizeof(*f),root,strlen(root)+1))return -22;
 memset(f,0,sizeof(*f));f->data=data;f->page_map=map;f->capacity=bytes;f->page_count=bytes/4096;memset(map,0,f->page_count);strcpy(f->root,root);f->mounted=1;f->next_ino=1;f->epoch=1;f->nodes[0]=(Ps2ScratchNode){.used=1,.kind=DIR_KIND,.links=1,.mode=0700,.ino=1,.version=1};f->dents[0].used=1;return 0;}
int ps2_scratch_stat(Ps2ScratchRamfs*f,const char*p,Ps2ScratchStat*s){if(!s)return -14;int at=walk(f,p,0,0);if(at<0)return at;stat_node(&f->nodes[f->dents[at].node],s);return 0;}
int ps2_scratch_fstat(Ps2ScratchRamfs*f,unsigned fd,Ps2ScratchStat*s){Ps2ScratchDescription*d=description(f,fd);if(!d)return -9;if(!s)return -14;stat_node(&f->nodes[d->node],s);return 0;}
static int create(Ps2ScratchRamfs*f,const char*p,unsigned kind,unsigned mode){char name[PS2_SCRATCH_NAME];int parent=walk(f,p,1,name);if(parent<0)return parent;if(child_index(f,(unsigned)parent,name,strlen(name))>=0)return -17;int dent=free_dent(f),node=free_node(f);if(dent<0||node<0)return -28;if(f->next_ino==UINT64_MAX||f->epoch==UINT64_MAX)return -75;
 f->nodes[node]=(Ps2ScratchNode){.used=1,.kind=kind,.links=1,.mode=mode&0777,.ino=++f->next_ino,.version=++f->epoch};f->dents[dent].used=1;f->dents[dent].parent=(unsigned)parent;f->dents[dent].node=(unsigned)node;strcpy(f->dents[dent].name,name);return dent;}
int ps2_scratch_open(Ps2ScratchRamfs*f,const char*p,unsigned flags,unsigned mode){const unsigned known=O_ACCMODE|O_CREAT|O_EXCL|O_NOCTTY|O_TRUNC|O_APPEND|O_NONBLOCK|O_DSYNC|O_SYNC|O_LARGEFILE|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC;if(!valid(f)||(flags&~known)||(flags&3)==3||((flags&512)&&!WRITABLE(flags)))return -22;int fd=-1,desc=-1;
 for(unsigned i=0;i<PS2_SCRATCH_FDS;i++){if(fd<0&&!f->fds[i].used)fd=(int)i;if(desc<0&&!f->descriptions[i].refs)desc=(int)i;}if(fd<0||desc<0)return -24;
 int at=walk(f,p,0,0);if(at==-2&&(flags&64)){if(flags&O_DIRECTORY)return -2;at=create(f,p,FILE_KIND,mode);}else if(at>=0&&(flags&64)&&(flags&128))return -17;if(at<0)return at;Ps2ScratchNode*n=&f->nodes[f->dents[at].node];
 if((flags&O_DIRECTORY)&&n->kind!=DIR_KIND)return -20;if(n->kind==DIR_KIND&&WRITABLE(flags))return -21;if((flags&512)&&!WRITABLE(flags))return -22;
 if((flags&512)&&n->kind==FILE_KIND){int rc=resize_node(f,n,0);if(rc)return rc;}
 f->fds[fd]=(Ps2ScratchFd){1,(unsigned)desc,(flags&524288)!=0};f->descriptions[desc]=(Ps2ScratchDescription){1,f->dents[at].node,flags,0};n->opens++;return fd;}
int ps2_scratch_close(Ps2ScratchRamfs*f,unsigned fd){Ps2ScratchDescription*d=description(f,fd);if(!d)return -9;f->fds[fd].used=0;if(!--d->refs){unsigned node=d->node;f->nodes[node].opens--;memset(d,0,sizeof(*d));release_node(f,node);}return 0;}
int ps2_scratch_dup(Ps2ScratchRamfs*f,unsigned fd,int target,unsigned minimum,unsigned cloexec){Ps2ScratchDescription*d=description(f,fd);if(!d)return -9;if(cloexec>1||minimum>=PS2_SCRATCH_FDS||target>=(int)PS2_SCRATCH_FDS)return -22;if(target<0){for(unsigned i=minimum;i<PS2_SCRATCH_FDS;i++)if(!f->fds[i].used){target=(int)i;break;}}if(target<0)return -24;if((unsigned)target==fd)return -22;unsigned desc=f->fds[fd].description;if(f->fds[target].used)ps2_scratch_close(f,(unsigned)target);f->fds[target]=(Ps2ScratchFd){1,desc,cloexec};d=&f->descriptions[desc];d->refs++;return target;}
int ps2_scratch_mkdir(Ps2ScratchRamfs*f,const char*p,unsigned mode){int at=walk(f,p,0,0);if(at>=0)return -17;if(at!=-2)return at;int rc=create(f,p,DIR_KIND,mode);return rc<0?rc:0;}
int ps2_scratch_unlink(Ps2ScratchRamfs*f,const char*p,unsigned directory){int at=walk(f,p,0,0);if(at<0)return at;if(!at)return -16;Ps2ScratchNode*n=&f->nodes[f->dents[at].node];if(directory){if(n->kind!=DIR_KIND)return -20;if(!is_empty(f,(unsigned)at))return -39;}else if(n->kind==DIR_KIND)return -21;remove_dent(f,(unsigned)at);return 0;}
int ps2_scratch_link(Ps2ScratchRamfs*f,const char*old,const char*newp){int at=walk(f,old,0,0);if(at<0)return at;Ps2ScratchNode*n=&f->nodes[f->dents[at].node];if(n->kind==DIR_KIND)return -1;char name[PS2_SCRATCH_NAME];int parent=walk(f,newp,1,name);if(parent<0)return parent;if(child_index(f,(unsigned)parent,name,strlen(name))>=0)return -17;int next=free_dent(f);if(next<0)return next;f->dents[next].used=1;f->dents[next].parent=(unsigned)parent;f->dents[next].node=f->dents[at].node;strcpy(f->dents[next].name,name);n->links++;n->version=++f->epoch;return 0;}
int ps2_scratch_rename(Ps2ScratchRamfs*f,const char*old,const char*newp,unsigned no_replace){if(no_replace>1)return -22;int at=walk(f,old,0,0);if(at<=0)return at?at:-16;char name[PS2_SCRATCH_NAME];int parent=walk(f,newp,1,name);if(parent<0)return parent;int dest=child_index(f,(unsigned)parent,name,strlen(name));if(dest==at)return 0;
 Ps2ScratchNode*n=&f->nodes[f->dents[at].node];if(dest>=0){Ps2ScratchNode*t=&f->nodes[f->dents[dest].node];if(no_replace)return -17;if(t==n)return 0;if(n->kind!=t->kind)return n->kind==DIR_KIND?-20:-21;if(t->kind==DIR_KIND&&!is_empty(f,(unsigned)dest))return -39;}
 if(n->kind==DIR_KIND){unsigned walk_at=(unsigned)parent;for(unsigned i=0;i<PS2_SCRATCH_DENTS;i++){if(walk_at==(unsigned)at)return -22;if(!walk_at)break;walk_at=f->dents[walk_at].parent;}}
 if(dest>=0)remove_dent(f,(unsigned)dest);f->dents[at].parent=(unsigned)parent;strcpy(f->dents[at].name,name);n->version=++f->epoch;return 0;}
int ps2_scratch_read(Ps2ScratchRamfs*f,unsigned fd,void*out,size_t n,uint64_t offset,unsigned advance,size_t*actual){if(actual)*actual=0;Ps2ScratchDescription*d=description(f,fd);if(!d)return -9;if(!actual)return -14;if((d->flags&3)==1)return -9;Ps2ScratchNode*node=&f->nodes[d->node];if(node->kind!=FILE_KIND)return -21;if(offset>INT64_MAX||n>(uint64_t)INT64_MAX-offset||advance>1)return -22;int rc=buffer(f,out,n);if(rc)return rc;uint64_t at=advance?d->offset:offset;if(at>=node->bytes)return 0;if(n>node->bytes-at)n=(size_t)(node->bytes-at);if(n)memcpy(out,f->data+node->first*4096+at,n);if(advance)d->offset+=n;*actual=n;return 0;}
int ps2_scratch_write(Ps2ScratchRamfs*f,unsigned fd,const void*data,size_t n,uint64_t offset,unsigned advance,size_t*actual){if(actual)*actual=0;Ps2ScratchDescription*d=description(f,fd);if(!d)return -9;if(!actual)return -14;if(!WRITABLE(d->flags))return -9;Ps2ScratchNode*node=&f->nodes[d->node];if(node->kind!=FILE_KIND)return -21;if(offset>INT64_MAX||advance>1)return -22;int rc=buffer(f,data,n);if(rc)return rc;uint64_t at=(d->flags&1024)?node->bytes:advance?d->offset:offset;if(n>(uint64_t)INT64_MAX-at)return -22;if(!n)return 0;if(at+n>node->bytes){rc=resize_node(f,node,at+n);if(rc)return rc;}memcpy(f->data+node->first*4096+at,data,n);node->version=++f->epoch;if(advance)d->offset=at+n;*actual=n;return 0;}
int ps2_scratch_truncate(Ps2ScratchRamfs*f,unsigned fd,uint64_t bytes){Ps2ScratchDescription*d=description(f,fd);if(!d)return -9;if(!WRITABLE(d->flags))return -22;Ps2ScratchNode*n=&f->nodes[d->node];if(n->kind!=FILE_KIND)return -21;return resize_node(f,n,bytes);}
int64_t ps2_scratch_seek(Ps2ScratchRamfs*f,unsigned fd,int64_t off,int whence){Ps2ScratchDescription*d=description(f,fd);if(!d)return -9;uint64_t base=whence==0?0:whence==1?d->offset:whence==2?f->nodes[d->node].bytes:0;if(whence<0||whence>2)return -22;if(off<0){uint64_t n=(uint64_t)(-(off+1))+1;if(n>base)return -22;base-=n;}else{if((uint64_t)off>INT64_MAX-base)return -75;base+=(uint64_t)off;}d->offset=base;return (int64_t)base;}
int ps2_scratch_sync(Ps2ScratchRamfs*f,unsigned fd){if(!description(f,fd))return -9;__atomic_thread_fence(__ATOMIC_SEQ_CST);f->sync_calls++;return 0;}
int ps2_scratch_child(Ps2ScratchRamfs*f,unsigned fd,uint64_t pos,char name[PS2_SCRATCH_NAME],Ps2ScratchStat*s,uint64_t*next){Ps2ScratchDescription*d=description(f,fd);if(!d)return -9;if(!name||!s||!next)return -14;if(f->nodes[d->node].kind!=DIR_KIND)return -20;int dent=-1;for(unsigned i=0;i<PS2_SCRATCH_DENTS;i++)if(f->dents[i].used&&f->dents[i].node==d->node){dent=(int)i;break;}if(dent<0)return -2;if(pos>=PS2_SCRATCH_DENTS)return 0;for(unsigned i=(unsigned)pos;i<PS2_SCRATCH_DENTS;i++)if(i&&f->dents[i].used&&f->dents[i].parent==(unsigned)dent){strcpy(name,f->dents[i].name);stat_node(&f->nodes[f->dents[i].node],s);*next=i+1;return 1;}return 0;}
int ps2_scratch_fd_path(Ps2ScratchRamfs*f,unsigned fd,char path[PS2_SCRATCH_PATH]){Ps2ScratchDescription*d=description(f,fd);if(!d)return -9;if(!path)return -14;unsigned chain[PS2_SCRATCH_DENTS],count=0;int at=-1;for(unsigned i=0;i<PS2_SCRATCH_DENTS;i++)if(f->dents[i].used&&f->dents[i].node==d->node){at=(int)i;break;}if(at<0)return -2;while(at){if(count==PS2_SCRATCH_DENTS)return -22;chain[count++]=(unsigned)at;at=(int)f->dents[at].parent;}size_t n=strlen(f->root);memcpy(path,f->root,n+1);while(count){const char*s=f->dents[chain[--count]].name;size_t k=strlen(s);if(n+1+k>=PS2_SCRATCH_PATH)return -36;path[n++]='/';memcpy(path+n,s,k+1);n+=k;}return 0;}
