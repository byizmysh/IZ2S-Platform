#include "storage.h"
#include "execution-internal.h"
#define PREFIX "/sd/izmysh/ps2/"
enum {OPEN_DIR=1,READ_DIR,ROWS,CLOSE_DIR,OPEN_GAME,SIZE_GAME,OPEN_BIOS,SIZE_BIOS,SAVE_TYPE,SAVE_MKDIR,CLOSE_ALL};
static int lexical(const char*a,const char*b){while(*a&&*a==*b){a++;b++;}return (u8)*a-(u8)*b;}
static int same(const char*a,const char*b){return !lexical(a,b);}
static u32 lock(IzPs2Storage*s){u32 expected=0;return __atomic_compare_exchange_n(&s->busy,&expected,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)?0:0xee19;}
static void unlock(IzPs2Storage*s){__atomic_store_n(&s->busy,0,__ATOMIC_RELEASE);}
static u32 retain(IzPs2Storage*s,u32 rc){s->error=rc?rc:0xee01;s->state=IZ_PS2_STORAGE_RETAINED;return s->error;}
static void closing(IzPs2Storage*s,u32 rc){s->error=rc;s->state=IZ_PS2_STORAGE_CLOSING;s->phase=CLOSE_ALL;}
static int module_root(const char*p){
 size_t base=sizeof(IZ_SD_ROOT);if(!p||memcmp(p,IZ_SD_ROOT "/",base))return 0;size_t n=0;
 while(base+n<128&&p[base+n]){unsigned c=(u8)p[base+n];if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||(n&&c=='-')))return 0;n++;}
 return n&&n<48&&base+n<128;
}
/* Directory names are bounded UTF-8 components. Invalid entries are skipped,
 * never interpreted as paths, so one unusual filename cannot hide a library. */
static int name(const char*p,size_t*length){
 size_t n=0;while(n<0x301&&p[n])n++;if(!n||n>=256||same(p,".")||same(p,".."))return 0;
 for(size_t i=0;i<n;){unsigned c=(u8)p[i++];if(c<32||c==127||c=='/'||c=='\\'||c==':')return 0;if(c<128)continue;
  unsigned count=c>=0xc2&&c<=0xdf?1:c>=0xe0&&c<=0xef?2:c>=0xf0&&c<=0xf4?3:0;if(!count||count>n-i)return 0;
  unsigned first=(u8)p[i];if((c==0xe0&&first<0xa0)||(c==0xed&&first>=0xa0)||(c==0xf0&&first<0x90)||(c==0xf4&&first>=0x90))return 0;
  for(unsigned j=0;j<count;j++)if(((u8)p[i++]&0xc0)!=0x80)return 0;
 }*length=n;return 1;
}
static unsigned lower(unsigned c){return c>='A'&&c<='Z'?c+32:c;}
static unsigned point(const char**p){const u8*s=(const void*)*p;unsigned c=*s++,n=c<128?0:c<0xe0?1:c<0xf0?2:3;if(n){c&=(1u<<(6-n))-1;for(unsigned i=0;i<n;i++)c=(c<<6)|(*s++&63);}*p=(const char*)s;return c>=0x410&&c<=0x42f?c+32:c==0x401?0x451:lower(c);}
static int compare(const IzPs2Game*a,const IzPs2Game*b){
 const char*x=a->name,*y=b->name;
 while(*x&&*y){if(*x>='0'&&*x<='9'&&*y>='0'&&*y<='9'){
   const char*xe=x,*ye=y;while(*xe>='0'&&*xe<='9')xe++;while(*ye>='0'&&*ye<='9')ye++;while(x+1<xe&&*x=='0')x++;while(y+1<ye&&*y=='0')y++;
   if(xe-x!=ye-y)return xe-x<ye-y?-1:1;while(x<xe){if(*x!=*y)return *x<*y?-1:1;x++;y++;}x=xe;y=ye;continue;
  }unsigned c=point(&x),d=point(&y);if(c!=d)return c<d?-1:1;
 }if(*x||*y)return *x?1:-1;int raw=lexical(a->name,b->name);return raw?raw:a->root<b->root?-1:a->root>b->root;
}
static int game(const char*p,size_t n){return n>4&&p[n-4]=='.'&&((lower(p[n-3])=='i'&&lower(p[n-2])=='s'&&lower(p[n-1])=='o')||(lower(p[n-3])=='e'&&lower(p[n-2])=='l'&&lower(p[n-1])=='f'));}
static int join(char*out,size_t capacity,const char*root,const char*area,const char*leaf){size_t a=strlen(root),b=strlen(area),c=leaf?strlen(leaf):0;if(a+b+c+1>capacity)return 0;memcpy(out,root,a);memcpy(out+a,area,b);if(leaf)memcpy(out+a+b,leaf,c);out[a+b+c]=0;return 1;}
static u32 row(IzPs2Storage*s,const IzFsDirectoryEntry*e){
 if(e->type!=1)return 0;size_t n=0;u32 root=s->directory_index%s->root_count;int bios=s->directory_index>=s->root_count;char physical[256];
 if(!name(e->name,&n)||!e->bytes||e->bytes>0x7fffffffffffffffull||!join(physical,sizeof physical,s->roots[root],bios?"/bios/":"/games/",e->name)||n+(bios?sizeof(PREFIX "bios/"):sizeof(PREFIX "games/"))>256){s->rejected++;return 0;}
 if(bios){if(e->bytes!=4194304)return 0;IzPs2Game candidate;memset(&candidate,0,sizeof candidate);memcpy(candidate.name,e->name,n+1);candidate.bytes=e->bytes;candidate.root=root;if(!s->has_bios[root]||compare(&candidate,&s->bios[root])<0){s->bios[root]=candidate;s->has_bios[root]=1;}return 0;}
 if(!game(e->name,n))return 0;
 for(unsigned i=0;i<s->count;i++)if(s->games[i].root==root&&same(s->games[i].name,e->name))return s->games[i].bytes==e->bytes?0:0xee18;
 if(s->count==IZ_PS2_STORAGE_GAMES)return 0xee04;IzPs2Game*entry=s->games+s->count;memset(entry,0,sizeof *entry);memcpy(entry->name,e->name,n+1);entry->bytes=e->bytes;entry->root=root;
 unsigned lo=0,hi=s->count;while(lo<hi){unsigned mid=(lo+hi)/2;if(compare(entry,s->games+s->order[mid])<0)hi=mid;else lo=mid+1;}for(unsigned i=s->count;i>lo;i--)s->order[i]=s->order[i-1];s->order[lo]=(uint16_t)s->count;s->count++;return 0;
}
u32 iz_ps2_storage_begin(IzPs2Storage*s,IzFs*fs,const char roots[][128],u32 count){
 if(!s||!fs||!fs->c||!fs->buffer||!roots||!count||count>IZ_PS2_STORAGE_ROOTS||!fs->c->filesystem)return 0xee02;
 for(unsigned i=0;i<count;i++){if(!module_root(roots[i]))return 0xee02;for(unsigned j=0;j<i;j++)if(same(roots[i],roots[j]))return 0xee02;}
 char copied[IZ_PS2_STORAGE_ROOTS][128];memcpy(copied,roots,(size_t)count*128);u32 rc=lock(s);if(rc)return rc;if(s->directory||s->game_handle||s->bios_handle||s->entered||s->state==IZ_PS2_STORAGE_RETAINED){unlock(s);return 0xee19;}
 if(fs->c->poisoned){s->fs=fs;rc=retain(s,0xee01);}else{memset((u8*)s+4,0,sizeof *s-4);s->fs=fs;s->root_count=count;memcpy(s->roots,copied,(size_t)count*128);s->state=IZ_PS2_STORAGE_SCANNING;s->phase=OPEN_DIR;}unlock(s);return rc;
}
static const char*save_path(unsigned i){switch(i){case 0:return "/izmysh";case 1:return "/izmysh/ps2";case 2:return "/izmysh/ps2/saves";default:return "/izmysh/ps2/saves/cards";}}
static u32 advance(IzPs2Storage*s){
 u32 rc=0;char path[256];
 switch(s->phase){
 case OPEN_DIR:if(s->directory_index==s->root_count*2){s->state=IZ_PS2_STORAGE_CATALOG;break;}if(!join(path,sizeof path,s->roots[s->directory_index%s->root_count],s->directory_index>=s->root_count?"/bios":"/games",0))return 0xee02;
  rc=iz_fs_directory_open(s->fs,path,2,&s->directory);if(rc==0x202){s->directory_index++;return 0;}if(!rc)s->phase=READ_DIR;break;
 case READ_DIR:rc=iz_fs_directory_read(s->fs,s->directory,s->rows,32,&s->row_count);if(!rc){s->seen+=s->row_count;if(s->seen>200000)return 0xee04;s->row=0;s->phase=s->row_count?ROWS:CLOSE_DIR;}break;
 case ROWS:for(unsigned n=0;n<8&&s->row<s->row_count;n++){rc=row(s,s->rows+s->row++);if(rc)return rc;}if(s->row==s->row_count)s->phase=READ_DIR;break;
 case CLOSE_DIR:rc=iz_fs_close(s->fs,s->directory);if(!rc){s->directory=0;s->directory_index++;s->phase=OPEN_DIR;}break;
 case OPEN_GAME:{IzPs2Game*g=s->games+s->selected;if(!join(path,sizeof path,s->roots[g->root],"/games/",g->name))return 0xee02;rc=iz_fs_open_handle(s->fs,path,&s->game_handle);if(!rc)s->phase=SIZE_GAME;break;}
 case SIZE_GAME:{u64 bytes=0;rc=iz_fs_size(s->fs,s->game_handle,&bytes);if(!rc){if(bytes!=s->games[s->selected].bytes)return 0xee05;s->phase=OPEN_BIOS;}break;}
 case OPEN_BIOS:{IzPs2Game*b=s->bios+s->bios_root;if(!join(path,sizeof path,s->roots[b->root],"/bios/",b->name))return 0xee02;rc=iz_fs_open_handle(s->fs,path,&s->bios_handle);if(!rc)s->phase=SIZE_BIOS;break;}
 case SIZE_BIOS:{u64 bytes=0;rc=iz_fs_size(s->fs,s->bios_handle,&bytes);if(!rc){if(bytes!=4194304)return 0xee05;s->save_index=0;s->phase=SAVE_TYPE;}break;}
 case SAVE_TYPE:{if(s->save_index==4){s->state=IZ_PS2_STORAGE_READY;break;}u32 type=0;rc=iz_fs_entry_type(s->fs,save_path(s->save_index),&type);if(rc==0x202){if(s->save_race)return 0xee09;s->phase=SAVE_MKDIR;return 0;}if(!rc){if(type)return 0xee06;s->save_index++;s->save_race=0;}break;}
 case SAVE_MKDIR:rc=iz_fs_mkdir(s->fs,save_path(s->save_index));if(!rc){s->save_index++;s->save_race=0;s->phase=SAVE_TYPE;}else if(rc==0x402){s->save_race=1;s->phase=SAVE_TYPE;return 0;}break;
 case CLOSE_ALL:{u32*h=s->directory?&s->directory:s->bios_handle?&s->bios_handle:s->game_handle?&s->game_handle:0;if(!h){s->state=s->error?IZ_PS2_STORAGE_FAILED:IZ_PS2_STORAGE_CLOSED;break;}rc=iz_fs_close(s->fs,*h);if(!rc)*h=0;break;}
 default:return 0xee02;
 }return rc;
}
u32 iz_ps2_storage_step(IzPs2Storage*s,u32 budget,u32*done){
 if(!s||!done||!budget||budget>64)return 0xee02;*done=0;u32 rc=lock(s);if(rc)return rc;
 if(s->state==IZ_PS2_STORAGE_IDLE||s->state>=IZ_PS2_STORAGE_ENTERED){*done=1;unlock(s);return 0xee02;}
 if(s->state!=IZ_PS2_STORAGE_RETAINED&&s->fs->c->poisoned)retain(s,s->error?s->error:0xee01);
 while((s->state==IZ_PS2_STORAGE_SCANNING||s->state==IZ_PS2_STORAGE_OPENING||s->state==IZ_PS2_STORAGE_CLOSING)&&budget--){rc=advance(s);s->units++;if(rc){if(s->fs->c->poisoned||s->state==IZ_PS2_STORAGE_CLOSING){rc=retain(s,rc);break;}closing(s,rc);rc=0;}}
 if(s->state==IZ_PS2_STORAGE_CATALOG||s->state==IZ_PS2_STORAGE_READY||s->state==IZ_PS2_STORAGE_CLOSED||s->state==IZ_PS2_STORAGE_FAILED||s->state==IZ_PS2_STORAGE_RETAINED){*done=1;rc=s->error;}unlock(s);return rc;
}
u32 iz_ps2_storage_select(IzPs2Storage*s,u32 rank){
 if(!s)return 0xee02;u32 rc=lock(s);if(rc)return rc;
 if(s->state!=IZ_PS2_STORAGE_CATALOG||rank>=s->count||s->entered||s->game_handle||s->bios_handle)rc=0xee03;
 else{u32 index=s->order[rank],root=s->games[index].root;if(!s->has_bios[root]){root=0;while(root<s->root_count&&!s->has_bios[root])root++;}if(root==s->root_count)rc=0xee07;else{s->selected=index;s->bios_root=root;s->state=IZ_PS2_STORAGE_OPENING;s->phase=OPEN_GAME;}}
 unlock(s);return rc;
}
u32 iz_ps2_storage_cancel(IzPs2Storage*s,u32 reason){
 if(!s)return 0xee02;u32 rc=lock(s);if(rc)return rc;if(s->state==IZ_PS2_STORAGE_RETAINED||s->entered||(s->fs&&s->fs->c->poisoned))rc=retain(s,s->error?s->error:0xee01);else if(s->state!=IZ_PS2_STORAGE_CLOSED&&s->state!=IZ_PS2_STORAGE_FAILED&&s->state!=IZ_PS2_STORAGE_IDLE&&s->state!=IZ_PS2_STORAGE_CLOSING)closing(s,reason);unlock(s);return rc;
}
u32 iz_ps2_storage_entered(IzPs2Storage*s){if(!s)return 0xee02;u32 rc=lock(s);if(rc)return rc;if(s->state!=IZ_PS2_STORAGE_READY||s->entered||s->fs->c->poisoned)rc=0xee02;else{s->entered=1;s->state=IZ_PS2_STORAGE_ENTERED;}unlock(s);return rc;}
#ifdef IZ_PS2_EXECUTION
u32 iz_ps2_storage_retire(IzPs2Storage*s,const IzPs2Execution*e){if(!s)return 0xee02;u32 rc=lock(s);if(rc)return rc;
 if(s->state!=IZ_PS2_STORAGE_ENTERED||!s->entered||s->fs->c->poisoned||!iz_ps2_execution_release_match(e,s,IZ_PS2_RETIRE_STORAGE))rc=0xee12;else closing(s,0);unlock(s);return rc;
}
#endif
static int span(u64 p,u64 n,u64 alignment){return p&&!(p&(alignment-1))&&p<(1ull<<39)&&n<=(1ull<<39)-p;}
static void w32(u8*p,unsigned at,u32 v){memcpy(p+at,&v,4);}
static void w64(u8*p,unsigned at,u64 v){memcpy(p+at,&v,8);}
static int token(const u8*p){u64 a=0,b=0;memcpy(&a,p,8);memcpy(&b,p+8,8);return (a|b)!=0;}
u32 iz_ps2_storage_pack(const IzPs2Storage*s,const IzPs2StorageBindings*b,IzPs2StoragePages*out){
 if(!s||!b||!out||s->state!=IZ_PS2_STORAGE_READY||s->entered||s->count>IZ_PS2_STORAGE_GAMES||s->selected>=s->count||s->root_count>IZ_PS2_STORAGE_ROOTS||s->bios_root>=s->root_count||!s->has_bios[s->bios_root]||s->fs->c->poisoned||!s->game_handle||!s->bios_handle||s->game_handle==s->bios_handle||!token(b->read_token)||!token(b->save_token)||b->sdk!=s->fs->c->sdk)return 0xee08;
 const u64 p[]={b->read_cache,b->files,b->segments,b->mount,b->save_cache,b->save,b->sdk,(u64)(uintptr_t)s->fs->buffer},n[]={65536,4096,4096,4096,65536,4096,6164480,65536};
 for(unsigned i=0;i<8;i++){if(!span(p[i],n[i],i==1||i==2?8:4096))return 0xee08;for(unsigned j=0;j<i;j++)if(p[i]<p[j]+n[j]&&p[j]<p[i]+n[i])return 0xee08;}
 u64 source=(u64)(uintptr_t)s,dest=(u64)(uintptr_t)out,bind=(u64)(uintptr_t)b;if(source>~0ull-sizeof *s||dest>~0ull-sizeof *out||bind>~0ull-sizeof *b||(dest<source+sizeof *s&&source<dest+sizeof *out)||(dest<bind+sizeof *b&&bind<dest+sizeof *out))return 0xee08;
 unsigned game_owned=0,bios_owned=0;for(unsigned i=0;i<32;i++){game_owned+=s->fs->handles[i]==s->game_handle;bios_owned+=s->fs->handles[i]==s->bios_handle;}if(game_owned!=1||bios_owned!=1)return 0xee08;
 memset(out,0,sizeof *out);w32(out->mount,0,0x32445350);w32(out->mount,4,1);w32(out->mount,8,72);w32(out->mount,12,1);w64(out->mount,16,b->read_cache);w64(out->mount,24,65536);w64(out->mount,32,b->files);w64(out->mount,40,b->segments);w32(out->mount,48,2);w32(out->mount,52,2);memcpy(out->mount+56,b->read_token,16);
 const IzPs2Game*entries[]={s->games+s->selected,s->bios+s->bios_root};
 for(unsigned i=0;i<2;i++){u8*f=out->files+i*280,*segment=out->segments+i*288;join((char*)f,256,i?"/scratch/bios/":PREFIX "games/","",entries[i]->name);w64(f,256,entries[i]->bytes);w32(f,264,i);w32(f,268,1);w32(f,272,i+1);join((char*)segment,256,i?PREFIX "bios/":PREFIX "games/","",entries[i]->name);w64(segment,264,entries[i]->bytes);w64(segment,272,entries[i]->bytes);w32(segment,280,i?s->bios_handle:s->game_handle);}
 w32(out->save,0,0x32575350);w32(out->save,4,1);w32(out->save,8,64);w32(out->save,12,1);w64(out->save,16,b->sdk);w32(out->save,24,s->fs->c->filesystem);w64(out->save,32,b->save_cache);w64(out->save,40,65536);memcpy(out->save+48,b->save_token,16);return 0;
}
