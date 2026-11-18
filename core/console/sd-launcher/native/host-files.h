/* Included only by the native packet executor. This is a read-only operation
 * over the supervisor's borrowed SD root, not a caller-provided IPC message.
 * Every opened child is closed on this same SDK caller before completion. */
#define IZ_FILE_MAGIC 0x495a4652u
#define IZ_FILE_HEADER 64u
#define IZ_FILE_MAX (32768u-IZ_FILE_HEADER)
enum {IZ_FILE_PROTOCOL=0xe9c0u,IZ_FILE_RANGE};
static int file_path_valid(const char*path,u32 bytes){
 if(bytes<2||bytes>769||path[0]!='/'||path[bytes-1]||strlen(path)!=bytes-1||(bytes>2&&path[bytes-2]=='/'))return 0;
 for(u32 at=1;at<bytes-1;){u32 start=at;while(at<bytes-1&&path[at]!='/'){u8 ch=(u8)path[at++];if(ch<32||ch==127||ch=='\\'||ch==':')return 0;}u32 n=at-start;if(!n||(n==1&&path[start]=='.')||(n==2&&path[start]=='.'&&path[start+1]=='.'))return 0;at++;}return 1;
}
static u32 file_uncertain(IzHostRpcOwner*g,u32 epoch,u32 error){__atomic_store_n(&g->opaque,1,__ATOMIC_RELEASE);hr_revoke(g,epoch);return error?error:IZ_FILE_PROTOCOL;}
static u32 file_reply(IzHostRpcOwner*g,u32 epoch,u8*m,int opening,u32*handle,u64*value){
 u32*p=(void*)m,at=8,copies=0,moves=0,received=0;
 if(p[1]>>31){u32 special=p[2];copies=(special>>1)&15;moves=(special>>5)&15;at+=4;if(special&1)at+=8;if(copies+moves==1)memcpy(&received,m+at,4);at+=(copies+moves)*4;}
 at+=((p[0]>>16)&15)*8+(((p[0]>>20)&15)+((p[0]>>24)&15)+((p[0]>>28)&15))*12;at=(at+15)&~15u;
 u32 words=p[1]&1023;if(at>512||words<4||words*4u>4096-at||*(u32*)(m+at)!=0x4f434653)return file_uncertain(g,epoch,IZ_FILE_PROTOCOL);
 u32 rc=*(u32*)(m+at+8);
 if(opening&&copies==0&&moves==1&&received&&received<0x80000000u&&received!=g->service_filesystem){*handle=received;return rc;}
 if(copies||moves||(opening&&!rc))return file_uncertain(g,epoch,IZ_FILE_PROTOCOL);
 if(rc)return rc;
 if(value){if(words<6)return file_uncertain(g,epoch,IZ_FILE_PROTOCOL);memcpy(value,m+at+16,8);}return 0;
}
static u32 file_send(IzHostRpcOwner*g,u32 epoch,u32 handle,u8*m,int opening,u32*opened,u64*value){
 u32 rc=(u32)((IzFunction)(uintptr_t)g->service_send)((u64)(uintptr_t)m,4096,handle,0,0,0,0,0);
 return rc?file_uncertain(g,epoch,rc):file_reply(g,epoch,m,opening,opened,value);
}
static int file_cancelled(IzHostRpcOwner*g,u32 epoch){return hr_epoch(g)!=epoch||hr_revoked(g);}
static u32 packet_read_file(IzHostRpcOwner*g,u32 epoch,const IzHostRpcPacket*p,const void*data,u64 bytes,u64*result){
 if(!g->graphics_mode||!g->service_filesystem||g->service_filesystem>=0x80000000u||!packet_range(g->service_sdk,0x5e1000)||g->service_send<g->service_sdk||g->service_send>=g->service_sdk+0x5e1000||g->service_send%4||g->service_close<g->service_sdk||g->service_close>=g->service_sdk+0x5e1000||g->service_close%4||g->service_reserved||p->destination||p->source||p->first||p->second||p->length<2||p->length>769||p->value>IZ_FILE_MAX||bytes!=sizeof *p+p->length||p->args[0][0]>0x7fffffffffffffffull)return IZ_RPC_INVALID;
 for(unsigned i=0;i<8;i++)if(p->args[1][i]||(i&&p->args[0][i]))return IZ_RPC_INVALID;
 char path[769];memset(path,0,sizeof path);memcpy(path,data,p->length);if(!file_path_valid(path,p->length))return IZ_RPC_INVALID;
 _Alignas(4096) u8 message[4096];u8*out=(void*)(uintptr_t)(g->sdk_scratch+0x4040);u32 status=0,error=0,closed=0,file=0,flags=0,length=0;u64 size=0,offset=p->args[0][0];
 memset(out,0,IZ_FILE_HEADER+p->value);__atomic_add_fetch(&g->cpu_jobs,1,__ATOMIC_RELEASE);
 if(file_cancelled(g,epoch))status=IZ_RPC_REVOKED;
 else{
  memset(message,0,sizeof message);u32*w=(void*)message;u64 name=(u64)(uintptr_t)path;w[0]=4|(1u<<16);w[1]=9;w[2]=(0x301u<<16)|((u32)(name>>36)&7u)<<6|((u32)(name>>32)&15u)<<12;w[3]=(u32)name;w[4]=0x49434653;w[6]=8;w[8]=1;
  error=file_send(g,epoch,g->service_filesystem,message,1,&file,0);if(file)flags=1;
  if(!error&&!file_cancelled(g,epoch)){
   memset(message,0,sizeof message);w[0]=4;w[1]=8;w[4]=0x49434653;w[6]=4;error=file_send(g,epoch,file,message,0,0,&size);
   if(!error&&(size>0x7fffffffffffffffull||offset>size))error=IZ_FILE_RANGE;
   if(!error&&!file_cancelled(g,epoch)){
    u64 available=size-offset;u32 count=available<p->value?(u32)available:p->value;
    if(count){
     memset(message,0,sizeof message);u64 address=(u64)(uintptr_t)(out+IZ_FILE_HEADER);w[0]=4|(1u<<24);w[1]=14;w[2]=count;w[3]=(u32)address;w[4]=1|((u32)(address>>36)&7u)<<2|((u32)(address>>32)&15u)<<28;w[8]=0x49434653;u64 input[]={0,offset,count};memcpy(message+48,input,sizeof input);u64 actual=0;
     error=file_send(g,epoch,file,message,0,0,&actual);if(!error){if(actual>count)error=file_uncertain(g,epoch,IZ_FILE_PROTOCOL);else length=(u32)actual;}
    }
   }
  }
 }
 /* Even an uncertain read/size result cannot make this known child belong to
  * another owner. Close it once; an unknown open has no invented handle. */
 if(file){closed=(u32)((IzFunction)(uintptr_t)g->service_close)(file,0,0,0,0,0,0,0);if(closed){if(!error)error=closed;file_uncertain(g,epoch,closed);}else flags|=2;}
 if(__atomic_load_n(&g->opaque,__ATOMIC_ACQUIRE))status=IZ_RPC_UNCERTAIN;else if(file_cancelled(g,epoch))status=IZ_RPC_REVOKED;
 if(error||status)length=0;
 u32 header[8]={IZ_FILE_MAGIC,1,length,error,closed,flags,error?3u:0u,0};memcpy(out,header,sizeof header);memcpy(out+32,&size,8);memcpy(out+40,&offset,8);result[0]=error?3:0;result[1]=length;
 __atomic_sub_fetch(&g->cpu_jobs,1,__ATOMIC_RELEASE);return status;
}
