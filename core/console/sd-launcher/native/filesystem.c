/* Console-local promoted IFileSystem. Uses the PS2 storage ownership model:
 * bounded transfers, tracked child capabilities, no replay after an unknown
 * transport result, and a borrowed filesystem that is never closed here. */
#include "directory.h"
#define BAD 0xe001u
static int owned(IzFs*s,u32 h){for(unsigned i=0;i<32;i++)if(s->handles[i]==h)return 1;return 0;}
static u32 poison(IzFs*s,u32 e){s->c->poisoned=1;s->c->error=e;return e;}
static u32 reply(IzFs*s,u8*m,int opening,u64*out){
 u32*p=(void*)m,received[30];unsigned at=8,copies=0,moves=0;
 if(p[1]>>31){u32 special=p[2];copies=(special>>1)&15;moves=(special>>5)&15;at+=4;if(special&1)at+=8;for(unsigned i=0;i<copies+moves;i++){received[i]=*(u32*)(m+at);at+=4;}}
 at+=((p[0]>>16)&15)*8+(((p[0]>>20)&15)+((p[0]>>24)&15)+((p[0]>>28)&15))*12;at=(at+15)&~15u;
 if(at>512||(p[1]&1023)<4||*(u32*)(m+at)!=0x4f434653)return poison(s,BAD);
 u32 rc=*(u32*)(m+at+8);
 if(rc&&!copies&&!moves)return rc;
 if(opening&&copies==0&&moves==1&&received[0]&&received[0]<0x80000000u&&received[0]!=s->c->filesystem&&!owned(s,received[0])){
  for(unsigned i=0;i<32;i++)if(!s->handles[i]){s->handles[i]=received[0];s->c->opened++;if(rc){iz_fs_close(s,received[0]);return rc;}*out=received[0];return 0;}}
 if(copies||moves||opening)return poison(s,BAD);
 if(rc)return rc;
 if(out){if((p[1]&1023)<6)return poison(s,BAD);memcpy(out,m+at+16,8);}return 0;
}
static u32 send(IzFs*s,u32 h,u8*m,int opening,u64*out){
 if(s->c->poisoned)return BAD;
 u32 r=(u32)CALL(s->c,F_SEND,m,4096,h,0,0,0,0,0);if(r)return poison(s,r);return reply(s,m,opening,out);
}
static int canonical_path(const char*p){
 if(!p)return 0;size_t n=0;while(n<769&&p[n])n++;if(!n||n>=769||p[0]!='/'||(n>1&&p[n-1]=='/'))return 0;
 for(size_t i=1;i<n;){size_t b=i;while(i<n&&p[i]!='/'){unsigned ch=(u8)p[i++];if(ch<32||ch==127||ch=='\\'||ch==':')return 0;}size_t k=i-b;if(!k||(k==1&&p[b]=='.')||(k==2&&p[b]=='.'&&p[b+1]=='.'))return 0;i++;}return 1;
}
static int valid_path(const char*p){size_t root=sizeof(IZ_SD_ROOT)-1;return canonical_path(p)&&strlen(p)>=root&&!memcmp(p,IZ_SD_ROOT,root)&&(!p[root]||p[root]=='/');}
static int ram_range(IzContext*c,u64 at,u64 bytes){return c->ram_base&&c->ram_bytes&&c->ram_base+c->ram_bytes>=c->ram_base&&at>=c->ram_base&&at<=c->ram_base+c->ram_bytes&&bytes<=c->ram_base+c->ram_bytes-at;}
static int ram_valid(IzContext*c){
 if(!c->ram_only)return !c->ram_file_count;
 if(!c->ram_file_count||c->ram_file_count>17||!ram_range(c,c->ram_files,(u64)c->ram_file_count*sizeof(IzRamFile)))return 0;
 IzRamFile*rows=(void*)(uintptr_t)c->ram_files;
 for(unsigned i=0;i<c->ram_file_count;i++){unsigned n=0;while(n<sizeof rows[i].path&&rows[i].path[n])n++;if(n==sizeof rows[i].path||!valid_path(rows[i].path)||!rows[i].bytes||!ram_range(c,rows[i].address,rows[i].bytes))return 0;for(unsigned j=0;j<i;j++)if(!memcmp(rows[i].path,rows[j].path,n+1))return 0;}
 return 1;
}
static IzRamFile*ram_handle(IzFs*s,u32 h){if(!(h&0x80000000u)||!ram_valid(s->c))return 0;unsigned slot=(h&0x7fffffffu)-1;if(slot>=32||!s->ram_open[slot]||s->ram_open[slot]>s->c->ram_file_count)return 0;return (IzRamFile*)(uintptr_t)s->c->ram_files+s->ram_open[slot]-1;}
static void pointer(u32*p,u64 a){p[0]=(0x301u<<16)|((u32)(a>>36)&7u)<<6|((u32)(a>>32)&15u)<<12;p[1]=(u32)a;}
static u32 path_call_wire(IzFs*s,u32 cmd,const char*first,const char*second,const void*input,u32 bytes,int opening,u64*out){
 memset(s->buffer,0,2048);memcpy(s->buffer,first,strlen(first)+1);if(second)memcpy(s->buffer+1024,second,strlen(second)+1);
 _Alignas(4096) u8 m[4096];memset(m,0,sizeof m);u32*p=(void*)m;u32 count=second?2:1,raw=(8+count*8+15)&~15u;
 p[0]=4|(count<<16);p[1]=(32+bytes+3)/4;pointer(p+2,(u64)(uintptr_t)s->buffer);if(second)pointer(p+4,(u64)(uintptr_t)s->buffer+1024);
 *(u32*)(m+raw)=0x49434653;*(u32*)(m+raw+8)=cmd;memcpy(m+raw+16,input,bytes);return send(s,s->c->filesystem,m,opening,out);
}
static u32 path_call(IzFs*s,u32 cmd,const char*first,const char*second,const void*input,u32 bytes,int opening,u64*out){
 if(!valid_path(first)||(second&&!valid_path(second))||bytes>16)return BAD;return path_call_wire(s,cmd,first,second,input,bytes,opening,out);
}
u32 iz_fs_close(IzFs*s,u32 h){
 if(h&0x80000000u){if(s->c->poisoned||!ram_handle(s,h))return BAD;s->ram_open[(h&0x7fffffffu)-1]=0;s->c->closed++;return 0;}
 if(s->c->poisoned||!owned(s,h)||h==s->c->filesystem)return BAD;
 u32 r=(u32)CALL(s->c,F_CLOSE,h,0,0,0,0,0,0,0);if(r){s->leaked=h;return poison(s,r);}for(unsigned i=0;i<32;i++)if(s->handles[i]==h)s->handles[i]=0;s->c->closed++;return 0;
}
static u32 file_call(IzFs*s,u32 h,u32 cmd,u64*out){
 if(!owned(s,h))return BAD;_Alignas(4096) u8 m[4096];memset(m,0,sizeof m);u32*p=(void*)m;p[0]=4;p[1]=8;p[4]=0x49434653;p[6]=cmd;return send(s,h,m,0,out);
}
u32 iz_fs_open_handle(IzFs*s,const char*path,u32*out){
 if(!out)return BAD;*out=0;if(s->c->poisoned||!valid_path(path))return BAD;
 if(s->c->ram_only){if(!ram_valid(s->c))return poison(s,BAD);IzRamFile*rows=(void*)(uintptr_t)s->c->ram_files;size_t length=strlen(path);if(length>=sizeof rows[0].path)return 0x202;
  for(unsigned i=0;i<s->c->ram_file_count;i++)if(!memcmp(path,rows[i].path,length+1)){for(unsigned slot=0;slot<32;slot++)if(!s->ram_open[slot]){s->ram_open[slot]=i+1;*out=0x80000000u|(slot+1);s->c->opened++;return 0;}return BAD;}return 0x202;
 }
 u32 mode=1;u64 h=0;u32 rc=path_call(s,8,path,0,&mode,4,1,&h);if(!rc)*out=(u32)h;return rc;
}
u32 iz_fs_size(IzFs*s,u32 h,u64*out){
 if(!out)return BAD;*out=0;if(h&0x80000000u){IzRamFile*row=ram_handle(s,h);if(s->c->poisoned||!row)return BAD;*out=row->bytes;return 0;}
 u32 rc=file_call(s,h,4,out);if(!rc&&*out>0x7fffffffffffffffull)return poison(s,BAD);return rc;
}
u32 iz_fs_open(IzFs*s,const char*path,u32*out,u64*size){
 if(!out||!size)return BAD;*out=0;*size=0;u32 h=0,rc=iz_fs_open_handle(s,path,&h);if(rc)return rc;
 rc=iz_fs_size(s,h,size);if(rc){if(!s->c->poisoned)iz_fs_close(s,h);return rc;}*out=h;return 0;
}
u32 iz_fs_directory_open(IzFs*s,const char*path,u32 mode,u32*out){
 if(!out)return BAD;*out=0;if(s->c->ram_only||s->c->poisoned||!canonical_path(path)||!mode||mode>3)return BAD;u64 h=0;u32 rc=path_call_wire(s,9,path,0,&mode,4,1,&h);if(!rc)*out=(u32)h;return rc;
}
static u32 read_directory_page(IzFs*s,u32 h,u32 capacity,u32*count){
 _Alignas(4096) u8 m[4096];memset(m,0,sizeof m);
 u32*p=(void*)m;u64 a=(u64)(uintptr_t)s->buffer;
 p[0]=4|(1u<<24);p[1]=8;p[2]=capacity*sizeof(IzFsDirectoryEntry);
 p[3]=(u32)a;p[4]=((u32)(a>>36)&7)<<2|((u32)(a>>32)&15)<<28;p[8]=0x49434653;
 u64 got=0;u32 rc=send(s,h,m,0,&got);
 if(rc)return rc;
 if(got>capacity)return poison(s,BAD);
 *count=(u32)got;return 0;
}
u32 iz_fs_directory_read(IzFs*s,u32 h,IzFsDirectoryEntry*out,u32 capacity,u32*count){
 if(!count)return BAD;*count=0;if(!out||!capacity||capacity>32||!owned(s,h)||(h&0x80000000u))return BAD;
 u32 rc=read_directory_page(s,h,capacity,count);
 if(!rc)memcpy(out,s->buffer,(size_t)*count*sizeof *out);
 return rc;
}
u32 iz_fs_entry_type(IzFs*s,const char*path,u32*out){
 if(!out)return BAD;*out=0;if(s->c->ram_only||s->c->poisoned||!canonical_path(path))return BAD;u64 type=0;u32 rc=path_call_wire(s,7,path,0,0,0,0,&type);if(!rc){if(type>1)return BAD;*out=(u32)type;}return rc;
}
u32 iz_fs_mkdir(IzFs*s,const char*path){
 if(s->c->ram_only||s->c->poisoned||!canonical_path(path)||!path[1])return BAD;return path_call_wire(s,2,path,0,0,0,0,0);
}
static u32 transfer(IzFs*s,u32 h,u64 offset,void*data,u32 bytes,int writing,u32*crc){
 if(h&0x80000000u){IzRamFile*row=ram_handle(s,h);if(s->c->poisoned||!row||writing||offset>row->bytes||bytes>row->bytes-offset)return BAD;const void*source=(const u8*)(uintptr_t)row->address+offset;if(crc)*crc=iz_crc_update(*crc,source,bytes);memcpy(data,source,bytes);s->c->ram_read_bytes+=bytes;return 0;}
 if(!owned(s,h))return BAD;u8*dest=data;
 for(u32 done=0;done<bytes;){u32 n=bytes-done;if(n>65536)n=65536;
  if(writing)memcpy(s->buffer,dest+done,n);_Alignas(4096) u8 m[4096];memset(m,0,sizeof m);u32*p=(void*)m;u64 a=(u64)(uintptr_t)s->buffer;
  p[0]=4|(writing?1u<<20:1u<<24);p[1]=14;p[2]=n;p[3]=(u32)a;p[4]=1|((u32)(a>>36)&7)<<2|((u32)(a>>32)&15)<<28;
  p[8]=0x49434653;p[10]=writing?1:0;u64 in[]={writing?1u:0u,offset+done,n};memcpy(m+48,in,sizeof in);u64 got=0;
  u32 rc=send(s,h,m,0,writing?0:&got);if(rc)return rc;
  if(!writing){if(got!=n)return 0xe002;if(crc)*crc=iz_crc_update(*crc,s->buffer,n);memcpy(dest+done,s->buffer,n);s->c->read_bytes+=n;s->c->read_calls++;}done+=n;
 }return 0;
}
u32 iz_fs_read(IzFs*s,u32 h,u64 offset,void*data,u32 bytes){return transfer(s,h,offset,data,bytes,0,0);}
/* Verify the SD bytes in cached IPC scratch before copying to the final
 * mapping. Reading a whole coherent GPU allocation back on the CPU can be
 * very slow; no complete image or second pass is needed for its file CRC. */
u32 iz_fs_read_crc(IzFs*s,u32 h,u64 offset,void*data,u32 bytes,u32 expected){u32 crc=0,rc=transfer(s,h,offset,data,bytes,0,&crc);return rc?rc:(crc==expected?0:0xe110);}
static int directory_id(const char*p){unsigned n=0;while(n<48&&p[n]){char c=p[n];if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||(n&&c=='-')))return 0;n++;}return n&&n<48;}
void iz_fs_app_path(char*out,const char*id){
 size_t n=sizeof(IZ_SD_ROOT)-1;
 memcpy(out,IZ_SD_ROOT,n);out[n++]='/';
 size_t length=strlen(id);
 memcpy(out+n,id,length);n+=length;
 memcpy(out+n,"/launcher.iza",sizeof "/launcher.iza");
}
enum { DIR_ALL,DIR_WITH_PACKAGE,DIR_VALID_HEADER };
static u32 find_app_backup(IzFs*,const char*,char*);
static void retain_earliest_dir(char names[][48],u32*count,const char name[48]){
 if(*count<IZ_MAX_DIRS){
  memcpy(names[(*count)++],name,48);
  return;
 }
 unsigned largest=0;
 for(unsigned i=1;i<*count;i++)
  if(memcmp(names[i],names[largest],48)>0)largest=i;
 if(memcmp(name,names[largest],48)<0)memcpy(names[largest],name,48);
}
/* Filter missing files first. Inspect headers only if that smaller list
 * still overflows, so a large directory of saves adds no header reads.
 * Keep the earliest eligible names regardless of directory enumeration order. */
static u32 read_dir_names(IzFs*s,char names[][48],u32*count,unsigned filter){
 *count=0;
 u32 directory=0,rc=iz_fs_directory_open(s,IZ_SD_ROOT,1,&directory);
 if(rc)return !s->c->poisoned&&(rc&0x3fffffu)==0x202?0:rc;
 unsigned seen=0;
 for(;;){
  u32 got=0;rc=read_directory_page(s,directory,32,&got);
  if(rc||!got)break;
  if((seen+=got)>200000){rc=0xe003;break;}
  // Package opens overwrite IPC scratch. Keep only the names from this page.
  char page[32][48];unsigned entries=0;
  for(unsigned i=0;i<got;i++){
   const IzFsDirectoryEntry*row=(const void*)(s->buffer+i*sizeof *row);
   if(row->type!=0||!directory_id(row->name))continue;
   size_t length=strlen(row->name)+1;
   char*name;
   if(filter!=DIR_ALL)name=page[entries++];
   else {
    if(*count==IZ_MAX_DIRS){rc=0xe004;break;}
    name=names[(*count)++];
   }
   memset(name,0,48);memcpy(name,row->name,length);
  }
  for(unsigned i=0;!rc&&i<entries;i++){
   char path[128];iz_fs_app_path(path,page[i]);
   u32 file=0;u64 size=0;
   u32 opened=filter==DIR_VALID_HEADER?iz_fs_open(s,path,&file,&size):iz_fs_open_handle(s,path,&file);
   if(s->c->poisoned){rc=s->c->error;break;}
   if((opened&0x3fffffu)==0x202){
    // Overflow filtering must keep recoverable modules. Publication will
    // restore the selected packages after closing the root enumeration.
    char backup[160];
    opened=find_app_backup(s,page[i],backup);
    if(!opened)opened=iz_fs_open(s,backup,&file,&size);
    if(s->c->poisoned){rc=s->c->error;break;}
    if(opened)continue; // Optional recovery must not hide other packages.
   }
   if(opened){
    if((opened&0x3fffffu)==0x202)continue;
    rc=opened;break;
   }
   IzAppHeader header;
   u32 checked=filter==DIR_VALID_HEADER&&size>=sizeof header?iz_fs_read(s,file,0,&header,sizeof header):0;
   if(s->c->poisoned){rc=s->c->error;break;}
   rc=iz_fs_close(s,file);
   if(rc)break;
   // A file truncated between GetSize and Read cannot occupy a menu slot.
   if(checked==0xe002)continue;
   if(checked){rc=checked;break;}
   if(filter==DIR_VALID_HEADER&&(size<sizeof header||iz_validate_header(&header,size)||memcmp(header.id,page[i],48)))continue;
   if(filter==DIR_VALID_HEADER)retain_earliest_dir(names,count,page[i]);
   else {
    if(*count==IZ_MAX_DIRS){rc=0xe004;break;}
    memcpy(names[(*count)++],page[i],48);
   }
  }
  if(rc)break;
 }
 if(!s->c->poisoned){
  u32 closed=iz_fs_close(s,directory);
  if(closed)return closed;
 }
 return rc;
}
u32 iz_fs_dirs(IzFs*s,char names[][48],u32*count){
 *count=0;
 if(s->c->ram_only){
  if(!ram_valid(s->c)||!directory_id(s->c->ram_app_id))return BAD;
  memcpy(names[0],s->c->ram_app_id,48);*count=1;return 0;
 }
 // Keep ordinary cards on the single-pass path. Only valid package headers
 // count against capacity when the unfiltered listing overflows.
 u32 rc=read_dir_names(s,names,count,DIR_ALL);
 if(rc==0xe004&&!s->c->poisoned)rc=read_dir_names(s,names,count,DIR_WITH_PACKAGE);
 return rc==0xe004&&!s->c->poisoned?read_dir_names(s,names,count,DIR_VALID_HEADER):rc;
}
static u32 commit(IzFs*s){
 _Alignas(4096) u8 m[4096];memset(m,0,sizeof m);u32*p=(void*)m;p[0]=4;p[1]=8;p[4]=0x49434653;p[6]=10;return send(s,s->c->filesystem,m,0,0);
}
static int launcher_stage_name(const char*name,const char*suffix){
 if(memcmp(name,".launcher-",10)||memcmp(name+26,suffix,5))return 0;
 for(unsigned i=10;i<26;i++)
  if(!((name[i]>='a'&&name[i]<='f')||(name[i]>='0'&&name[i]<='9')))return 0;
 return 1;
}
/* Recognize one installer pair without trusting its unpublished image. */
static u32 find_app_backup(IzFs*s,const char*id,char*backup){
 if(s->c->ram_only||!directory_id(id))return 0x202;
 char root[128],stage[160],candidate[31]={0};
 iz_fs_app_path(root,id);
 size_t base=strlen(root)-(sizeof "/launcher.iza"-1);
 root[base]=0;
 memcpy(backup,root,base);backup[base]='/';
 memcpy(stage,backup,base+1);
 u32 dir=0,rc=iz_fs_directory_open(s,root,2,&dir),seen=0;
 if(rc)return rc;
 for(;;){
  u32 got=0;rc=read_directory_page(s,dir,32,&got);
  if(rc||!got)break;
  if((seen+=got)>4096){rc=0x202;break;}
  // Copy the short names before a lookup overwrites the FS scratch page.
  char names[32][31];unsigned count=0;
  for(unsigned i=0;i<got;i++){
   const IzFsDirectoryEntry*row=(const void*)(s->buffer+i*sizeof *row);
   if(row->type==1&&launcher_stage_name(row->name,".bak"))memcpy(names[count++],row->name,31);
  }
  for(unsigned i=0;i<count;i++){
   memcpy(stage+base+1,names[i],26);memcpy(stage+base+27,".tmp",5);
   u32 type=0;rc=iz_fs_entry_type(s,stage,&type);
   if((rc&0x3fffffu)==0x202&&!s->c->poisoned){rc=0;continue;}
   if(rc)break;
   if(type!=1)continue;
   if(candidate[0]){rc=0x202;break;}
   memcpy(candidate,names[i],31);
  }
  if(rc)break;
 }
 if(!s->c->poisoned){u32 closed=iz_fs_close(s,dir);if(!rc)rc=closed;}
 if(rc||!candidate[0])return rc?rc:0x202;
 memcpy(backup+base+1,candidate,sizeof candidate);
 return 0;
}
static u32 open_app_backup(IzFs*s,const char*id,char*backup,u32*file,u64*size){
 u32 rc=find_app_backup(s,id,backup);
 return rc?rc:iz_fs_open(s,backup,file,size);
}
u32 iz_fs_open_app_backup(IzFs*s,const char*id,u32*file,u64*size){
 char backup[160];
 return open_app_backup(s,id,backup,file,size);
}
u32 iz_fs_restore_app(IzFs*s,const char*id){
 char target[128],backup[160];
 u32 file=0;u64 size=0;u32 rc=open_app_backup(s,id,backup,&file,&size);
 if(rc)return rc;
 iz_fs_app_path(target,id);
 IzAppHeader header;
 rc=size<sizeof header?0x202:iz_fs_read(s,file,0,&header,sizeof header);
 if(!rc&&(iz_validate_header(&header,size)||memcmp(header.id,id,strlen(id)+1)))rc=0x202;
 if(!s->c->poisoned){u32 closed=iz_fs_close(s,file);if(!rc)rc=closed;}
 if(rc)return rc;
 // Rename does not replace a destination on this filesystem. Never use the
 // temporary package as a source, or retry an uncertain operation.
 rc=path_call(s,5,backup,target,0,0,0,0);
 return rc?rc:commit(s);
}
static int asset_name(const char*p){
 if(memcmp(p,"asset-",6))return 0;for(unsigned i=6;i<70;i++)if(!((p[i]>='0'&&p[i]<='9')||(p[i]>='a'&&p[i]<='f')))return 0;return !memcmp(p+70,".izdata",8);
}
u32 iz_fs_remove_app(IzFs*s,const char*id){
 if(s->c->ram_only||!directory_id(id))return BAD;
 char root[128]=IZ_SD_ROOT "/",path[256],stage[256];size_t base=strlen(root);memcpy(root+base,id,strlen(id)+1);base=strlen(root);
 memcpy(path,root,base);memcpy(path+base,"/launcher.iza",sizeof "/launcher.iza");
 // Verify the currently published identity, then close every child capability
 // before renaming/deleting. User directories, games, BIOS and saves are never
 // traversed or selected, including symlink-like/invalid directory entries.
 u32 file=0;u64 size=0;u32 rc=iz_fs_open(s,path,&file,&size);if(rc)return rc;IzAppHeader h;
 rc=size<sizeof h?0xe111:iz_fs_read(s,file,0,&h,sizeof h);if(!rc)rc=iz_validate_header(&h,size);if(!rc&&memcmp(h.id,id,strlen(id)+1))rc=0xe112;
 if(!s->c->poisoned){u32 closed=iz_fs_close(s,file);if(!rc)rc=closed;}if(rc)return rc;
 char owned_files[128][78];u32 count=0,seen=0,mode=2;u64 dir=0;rc=path_call(s,9,root,0,&mode,4,1,&dir);if(rc)return rc;
 for(;;){
  _Alignas(4096) u8 m[4096];memset(m,0,sizeof m);u32*p=(void*)m;u64 a=(u64)(uintptr_t)s->buffer;
  p[0]=4|(1u<<24);p[1]=8;p[2]=32*0x310;p[3]=(u32)a;p[4]=((u32)(a>>36)&7)<<2|((u32)(a>>32)&15)<<28;p[8]=0x49434653;
  u64 got=0;rc=send(s,(u32)dir,m,0,&got);if(rc)break;if(got>32){rc=poison(s,BAD);break;}if(!got)break;
  if((seen+=(u32)got)>4096){rc=0xe003;break;}
  for(unsigned i=0;i<got;i++){
   const IzFsDirectoryEntry*row=(const void*)(s->buffer+i*sizeof *row);
   if(row->type!=1||(!asset_name(row->name)&&!launcher_stage_name(row->name,".tmp")))continue;
   if(count==128){rc=0xe004;break;}
   memset(owned_files[count],0,sizeof owned_files[0]);memcpy(owned_files[count++],row->name,strlen(row->name)+1);
  }
  if(rc)break;
 }
 if(!s->c->poisoned){u32 closed=iz_fs_close(s,(u32)dir);if(!rc)rc=closed;}if(rc)return rc;
 // Invalidate interrupted update pairs before unpublishing, so a later
 // scan cannot revive an app that the user removed.
 int invalidated=0;
 for(unsigned i=0;i<count;i++)if(launcher_stage_name(owned_files[i],".tmp")){
  memcpy(stage,root,base);stage[base]='/';memcpy(stage+base+1,owned_files[i],31);
  rc=path_call(s,1,stage,0,0,0,0,0);
  if(rc)return rc;
  invalidated=1;
 }
 if(invalidated){rc=commit(s);if(rc)return rc;}
 // Unpublish first, commit, then remove only immutable owned companions. A
 // failed/unknown mutation is never replayed or rolled back into a broken app.
 memcpy(stage,root,base);memcpy(stage+base,"/.removed-",10);const char hex[]="0123456789abcdef";u64 nonce=CALL(s->c,F_TICK,0,0,0,0,0,0,0,0);
 for(unsigned i=0;i<16;i++)stage[base+10+i]=hex[(nonce>>(60-i*4))&15];memcpy(stage+base+26,".iza",sizeof ".iza");
 rc=path_call(s,5,path,stage,0,0,0,0);if(rc)return rc;rc=commit(s);if(rc)return rc;
 for(unsigned i=0;!rc&&i<count;i++)if(asset_name(owned_files[i])){memcpy(path,root,base);path[base]='/';memcpy(path+base+1,owned_files[i],78);rc=path_call(s,1,path,0,0,0,0,0);}
 if(!rc)rc=path_call(s,1,stage,0,0,0,0,0);if(!rc)rc=commit(s);return rc;
}
u32 iz_fs_probe(IzFs*s){
 /* A unique caller nonce prevents replacing a user's file. Both names remain
  * inside the launcher's own directory. Transport failures retain ownership. */
 char path[128]=IZ_SD_ROOT "/iz-sd-launcher/probe-00000000.tmp",renamed[128];
 const char hex[]="0123456789abcdef";size_t n=strlen(path);for(unsigned i=0;i<8;i++)path[n-12+i]=hex[(s->c->probe_nonce>>(28-4*i))&15];memcpy(renamed,path,n+1);memcpy(renamed+n-3,"ok\0",3);
 u64 in[]={0,4096};u32 rc=path_call(s,0,path,0,in,16,0,0);if(rc)return rc;u32 mode=3;u64 h=0;
 rc=path_call(s,8,path,0,&mode,4,1,&h);if(rc)return rc;
 u8 data[4096];for(unsigned i=0;i<sizeof data;i++)data[i]=(u8)(i*37+s->c->probe_nonce);u32 expected=iz_crc(data,sizeof data);
 rc=transfer(s,(u32)h,0,data,sizeof data,1,0);if(!rc)rc=file_call(s,(u32)h,2,0);if(!rc){memset(data,0,sizeof data);rc=iz_fs_read(s,(u32)h,0,data,sizeof data);}if(!rc&&iz_crc(data,sizeof data)!=expected)rc=0xe005;
 if(!s->c->poisoned){u32 close=iz_fs_close(s,(u32)h);if(!rc)rc=close;}
 if(!rc)rc=path_call(s,5,path,renamed,0,0,0,0);if(!rc)rc=path_call(s,1,renamed,0,0,0,0,0);
 if(!rc){_Alignas(4096) u8 m[4096];memset(m,0,sizeof m);u32*p=(void*)m;p[0]=4;p[1]=8;p[4]=0x49434653;p[6]=10;rc=send(s,s->c->filesystem,m,0,0);}s->c->probe_crc=expected;return rc;
}
u32 iz_fs_write_file(IzFs*s,const char*path,const void*data,u32 bytes){
 if(s->c->ram_only||!valid_path(path)||!data||!bytes||bytes>16u*1024*1024)return BAD;
 size_t length=strlen(path);if(length>740)return BAD;char stage[769],backup[769];memcpy(stage,path,length);memcpy(backup,path,length);
 const char hex[]="0123456789abcdef";u64 nonce=CALL(s->c,F_TICK,0,0,0,0,0,0,0,0);stage[length]='.';backup[length]='.';
 for(unsigned i=0;i<16;i++)stage[length+1+i]=backup[length+1+i]=hex[(nonce>>(60-i*4))&15];memcpy(stage+length+17,".tmp",5);memcpy(backup+length+17,".bak",5);
 u64 create[]={0,bytes};u32 rc=path_call(s,0,stage,0,create,16,0,0);if(rc)return rc;u32 mode=3;u64 file=0;
 rc=path_call(s,8,stage,0,&mode,4,1,&file);if(rc)return rc;
 rc=transfer(s,(u32)file,0,(void*)data,bytes,1,0);if(!rc)rc=file_call(s,(u32)file,2,0);
 u8 verify[4096];for(u32 offset=0;!rc&&offset<bytes;){u32 n=bytes-offset;if(n>sizeof verify)n=sizeof verify;rc=iz_fs_read(s,(u32)file,offset,verify,n);if(!rc&&memcmp(verify,(const u8*)data+offset,n))rc=0xe005;offset+=n;}
 if(!s->c->poisoned){u32 close=iz_fs_close(s,(u32)file);if(!rc)rc=close;}if(rc)return rc;
 u64 type=0;rc=path_call(s,7,path,0,0,0,0,&type);int existing=!rc;if(rc&&rc!=0x202)return rc;if(existing&&type!=1)return BAD;
 if(existing){rc=path_call(s,5,path,backup,0,0,0,0);if(rc)return rc;}
 rc=path_call(s,5,stage,path,0,0,0,0);
 if(rc){
  // Only a confirmed refusal leaves the old save safe to put back.
  if(existing&&!s->c->poisoned){
   u32 restored=path_call(s,5,backup,path,0,0,0,0);
   if(!restored)restored=commit(s);
   if(restored)return restored;
   u32 removed=path_call(s,1,stage,0,0,0,0,0);
   if(!removed)removed=commit(s);
   if(s->c->poisoned)return removed;
  }
  return rc;
 }
 rc=commit(s);if(rc)return rc;
 if(existing){rc=path_call(s,1,backup,0,0,0,0,0);if(!rc)rc=commit(s);}return rc;
}
/* Streaming checkpoint bank. The caller may replace only the inactive bank;
 * the active committed bank is kept until the new file passes readback. */
u32 iz_fs_checkpoint_begin(IzFs*s,const char*path,u32*out){
 if(!out)return BAD;*out=0;if(s->c->ram_only||s->c->poisoned||!valid_path(path))return BAD;
 size_t n=strlen(path);const char*suffix="/.launcher-state-a.izsave";size_t tail=strlen(suffix);
 if(n<=tail)return BAD;const char*p=path+n-tail;
 if(memcmp(p,suffix,17)||(p[17]!='a'&&p[17]!='b')||memcmp(p+18,suffix+18,tail-18+1))return BAD;
 u64 type=0;u32 rc=path_call(s,7,path,0,0,0,0,&type);if(rc&&rc!=0x202)return rc;
 if(!rc){if(type!=1)return BAD;rc=path_call(s,1,path,0,0,0,0,0);if(rc)return rc;}
 u64 create[]={0,1024};rc=path_call(s,0,path,0,create,16,0,0);if(rc)return rc;
 u32 mode=3;u64 h=0;rc=path_call(s,8,path,0,&mode,4,1,&h);if(!rc)*out=(u32)h;return rc;
}
u32 iz_fs_checkpoint_resize(IzFs*s,u32 h,u64 bytes){
 if(s->c->ram_only||s->c->poisoned||!owned(s,h)||bytes<1024||bytes>144u*1024*1024)return BAD;
 _Alignas(4096)u8 m[4096];memset(m,0,sizeof m);u32*p=(void*)m;p[0]=4;p[1]=10;p[4]=0x49434653;p[6]=3;memcpy(m+32,&bytes,8);return send(s,h,m,0,0);
}
u32 iz_fs_checkpoint_write(IzFs*s,u32 h,u64 at,const void*data,u32 bytes){
 if(s->c->ram_only||s->c->poisoned||!data||!bytes||bytes>65536||at>144u*1024*1024||bytes>144u*1024*1024-at)return BAD;return transfer(s,h,at,(void*)data,bytes,1,0);
}
u32 iz_fs_checkpoint_finish(IzFs*s,u32 h){
 if(s->c->ram_only||s->c->poisoned||!owned(s,h))return BAD;u32 rc=file_call(s,h,2,0);
 if(!s->c->poisoned){u32 close=iz_fs_close(s,h);if(!rc)rc=close;}if(!rc)rc=commit(s);return rc;
}
