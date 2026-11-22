#define _POSIX_C_SOURCE 200809L
#include <switch.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <sb4/stream.h>
#include "pc-link.h"
#include "menu-update.h"
#include "pc-files.h"
#include "pc-audio.h"
#include "pc-video.h"

#define LINK_PUBLIC_CONTEXT "IZ2S PC Link open channel v1"
#define LINK_CONFIG "sdmc:/iz2s/pc-link.cfg"
#define LINK_STAGE "sdmc:/iz2s/incoming/.pc-delivery.part"
#define LINK_PORT 39524
#define LINK_LEGACY_LIMIT (32u*1024u*1024u)
#define LINK_ARCHIVE_LIMIT UINT32_MAX
#define IMPORT_PENDING UINT64_MAX
#define LINK_RECONNECT_NS UINT64_C(90000000000)

static Thread worker;
static bool started, request_pending, sockets_ready;
static Mutex socket_lock,channel_lock;
static atomic_bool channel_open;
static atomic_uint channel_generation;
static unsigned channel_jobs;
static atomic_bool ending,online,fetching,auto_fetch;
static atomic_int delivery;
static atomic_bool auto_offer;
enum {STORE_IDLE,STORE_REQUEST,STORE_RESULT};
static atomic_int store_state;
static char store_id[48];
static IzPcStoreApp store_apps[IZ_PC_STORE_LIMIT];
static unsigned store_count,store_status;
static atomic_uint_fast64_t import_result;
static atomic_uint previous_import_code;
static uint8_t key[32];
#define LINK_HOST_LIMIT 8
static struct in_addr link_addresses[LINK_HOST_LIMIT];
static unsigned link_count;
static atomic_uint preferred_host;
static atomic_uint routed_address;

enum VideoPhase { VIDEO_IDLE, VIDEO_REQUEST, VIDEO_RESULT };
enum MediaWire { MEDIA_COMBINED, MEDIA_PROBE, MEDIA_SEPARATE };
static enum MediaWire media_wire,video_request_wire;
static Thread audio_worker;
static bool audio_started;
static atomic_int audio_phase;
static uint64_t audio_request_token,audio_arrived;
static uint32_t audio_worker_seen,audio_offset;
static bool audio_batch;
static uint8_t audio_body[28+IZ_PC_PCM_BATCH*IZ_PC_PCM_BYTES];
static IzPcMediaFrame audio_frame;
static enum IzPcVideoResult audio_result;
static Thread video_worker;
static bool video_started,video_reset;
static atomic_int video_phase;
static uint8_t *video_body;
/* UI owns the request fields until publishing VIDEO_REQUEST, and the worker
 * owns the packet until it publishes VIDEO_RESULT. It never gets an app ptr. */
static uint64_t video_token,video_generation,video_request_token;
static atomic_uint_fast64_t video_input_token;
static uint8_t video_operation,video_request_operation;
static uint8_t video_request_input[IZ_PC_INPUT_BYTES];
static uint32_t video_seen,video_request_seen,video_publication,audio_seen,audio_request_seen;
static IzPcMediaFrame media_frame;
static IzPcPcm pcm;
static enum IzPcVideoResult video_result;

bool iz_pc_enabled(void){return atomic_load(&channel_open);}

static bool begin_channel_job(void){
    mutexLock(&channel_lock);
    const bool enabled=iz_pc_enabled();
    if(enabled)++channel_jobs;
    mutexUnlock(&channel_lock);
    return enabled;
}

static void end_channel_job(void){
    mutexLock(&channel_lock);
    --channel_jobs;
    mutexUnlock(&channel_lock);
}

void iz_pc_pause(void){
    mutexLock(&channel_lock);
    atomic_store(&channel_open,false);
    atomic_store(&online,false);
    atomic_store(&fetching,false);
    atomic_store(&auto_offer,false);
    mutexUnlock(&channel_lock);
}

static int connect_host(struct in_addr address) {
    if(!iz_pc_enabled()||atomic_load(&ending))return -1;
    int fd=socket(AF_INET,SOCK_STREAM,0);
    if(fd<0)return -1;
    struct sockaddr_in addr={.sin_family=AF_INET,.sin_port=htons(LINK_PORT),.sin_addr=address};
    struct timeval timeout={.tv_sec=2};
    int flags=fcntl(fd,F_GETFL);
    if(flags<0||fcntl(fd,F_SETFL,flags|O_NONBLOCK))
        goto fail;
    if(connect(fd,(void*)&addr,sizeof addr)<0){
        if(errno!=EINPROGRESS&&errno!=EWOULDBLOCK)goto fail;
        struct pollfd pending={.fd=fd,.events=POLLOUT};
        int result=0;socklen_t bytes=sizeof result;
        if(poll(&pending,1,1200)!=1||getsockopt(fd,SOL_SOCKET,SO_ERROR,&result,&bytes)||result)goto fail;
    }
    if(fcntl(fd,F_SETFL,flags)||setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout)||
       setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof timeout))goto fail;
    return fd;
fail:
    close(fd);
    return -1;
}

static int open_link(void) {
    unsigned first=atomic_load(&preferred_host);
    for(unsigned i=0;i<link_count;++i){
        const unsigned index=(first+i)%link_count;
        int fd=connect_host(link_addresses[index]);
        if(fd<0)continue;
        struct sockaddr_in local;
        socklen_t length=sizeof local;
        if(!getsockname(fd,(struct sockaddr*)&local,&length))
            atomic_store(&routed_address,local.sin_addr.s_addr);
        atomic_store(&preferred_host,index);
        return fd;
    }
    atomic_store(&routed_address,0);
    return -1;
}

uint32_t iz_pc_routed_address(void){return atomic_load(&routed_address);}

bool iz_pc_peer_allowed(uint32_t address){
    for(unsigned i=0;i<link_count;++i)if(link_addresses[i].s_addr==address)return true;
    return false;
}

static bool transfer(int fd,void *data,size_t bytes,bool sending){
    uint8_t *p=data;
    while(bytes&&iz_pc_enabled()&&!atomic_load(&ending)){
        ssize_t n=sending?send(fd,p,bytes,0):recv(fd,p,bytes,0);
        if(n<=0)return false;
        p+=n;bytes-=(size_t)n;
    }
    return bytes==0;
}

typedef struct { int fd; uint64_t token; } IzPcVideoContext;
static bool video_transfer(void *context,void *data,size_t bytes,bool sending){
    const IzPcVideoContext *request=context;
    if(sending&&atomic_load(&video_input_token)!=request->token)return false;
    return transfer(request->fd,data,bytes,sending);
}

static void video_sign(void *context,uint8_t digest[32],const void *data,size_t bytes){
    (void)context;
    hmacSha256CalculateMac(digest,key,sizeof key,data,bytes);
}

static enum IzPcVideoResult read_media(uint64_t token,uint8_t operation,uint32_t seen,uint32_t heard,
                                       const uint8_t *input,uint8_t *body,size_t capacity,IzPcMediaFrame *frame){
    if(atomic_load(&video_input_token)!=token||!begin_channel_job())return IZ_PC_VIDEO_ERROR;
    const unsigned preferred=atomic_load(&preferred_host);
    const int fd=(operation=='a'||operation=='b')?(preferred<link_count?connect_host(link_addresses[preferred]):-1):open_link();
    enum IzPcVideoResult result=IZ_PC_VIDEO_ERROR;
    if(fd>=0){
        IzPcVideoContext context={fd,token};
        const IzPcVideoIo io={&context,video_transfer,video_sign,sha256CalculateHash};
        result=iz_pc_media_receive(&io,operation,seen,heard,input,body,capacity,frame);
        close(fd);
    }
    end_channel_job();
    return result;
}

static void run_video(void *unused){
    (void)unused;
    while(!atomic_load(&ending)){
        if(atomic_load(&video_phase)!=VIDEO_REQUEST){svcSleepThread(1000000);continue;}
        bool split=video_request_wire!=MEDIA_COMBINED;
        video_result=read_media(video_request_token,split?'y':video_request_operation,video_request_seen,
                               split?0:audio_request_seen,video_request_input,video_body,IZ_PC_MEDIA_CAPACITY,&media_frame);
        if(video_request_wire==MEDIA_PROBE){
            if(video_result!=IZ_PC_VIDEO_ERROR)video_request_wire=MEDIA_SEPARATE;
            else if(atomic_load(&video_input_token)==video_request_token){
                video_result=read_media(video_request_token,'Y',video_request_seen,audio_request_seen,
                                        video_request_input,video_body,IZ_PC_MEDIA_CAPACITY,&media_frame);
                if(video_result!=IZ_PC_VIDEO_ERROR)video_request_wire=MEDIA_COMBINED;
            }
        }
        atomic_store(&video_phase,VIDEO_RESULT);
    }
}

static void run_audio(void *unused){
    (void)unused;
    while(!atomic_load(&ending)){
        if(atomic_load(&audio_phase)!=VIDEO_REQUEST){svcSleepThread(1000000);continue;}
        /* The media decoder requires a nonzero seen field for an audio-only reply. */
        audio_result=read_media(audio_request_token,audio_batch?'b':'a',1,audio_worker_seen,NULL,
                                audio_body,sizeof audio_body,&audio_frame);
        if(audio_batch&&audio_result==IZ_PC_VIDEO_ERROR){
            audio_result=read_media(audio_request_token,'a',1,audio_worker_seen,NULL,
                                    audio_body,sizeof audio_body,&audio_frame);
            if(audio_result!=IZ_PC_VIDEO_ERROR)audio_batch=false;
        }
        audio_arrived=armGetSystemTick();
        atomic_store(&audio_phase,VIDEO_RESULT);
    }
}

static bool start_audio(void){
    if(audio_started)return true;
    atomic_store(&audio_phase,VIDEO_IDLE);
    Result rc=threadCreate(&audio_worker,run_audio,NULL,NULL,65536,0x2c,-2);
    if(R_SUCCEEDED(rc)){
        rc=threadStart(&audio_worker);
        if(R_FAILED(rc))threadClose(&audio_worker);
    }
    return audio_started=R_SUCCEEDED(rc);
}

static void update_audio(IzNativeApp *app,uint64_t token){
    int phase=atomic_load(&audio_phase);
    if(phase==VIDEO_RESULT){
        if(audio_request_token==token&&audio_result==IZ_PC_VIDEO_CURRENT&&audio_frame.pcm){
            /* Do not acknowledge a batch until its samples have reached the audio queue. */
            const uint64_t age=armTicksToNs(armGetSystemTick()-audio_arrived);
            while(age<100000000ull&&audio_offset<audio_frame.pcm_bytes){
                uint32_t rc=iz_pc_pcm_submit_buffered(&pcm,app,audio_frame.pcm+audio_offset);
                if(rc==SB4_STREAM_PENDING)return;
                if(rc)break;
                audio_offset+=IZ_PC_PCM_BYTES;
            }
            audio_seen=audio_frame.audio_sequence;
        }
        phase=VIDEO_IDLE;
    }
    if(phase==VIDEO_IDLE&&media_wire==MEDIA_SEPARATE){
        if(audio_request_token!=token)audio_batch=true;
        audio_offset=0;
        audio_request_token=token;
        audio_worker_seen=audio_seen;
        atomic_store(&audio_phase,VIDEO_REQUEST);
    }else if(phase==VIDEO_IDLE)atomic_store(&audio_phase,VIDEO_IDLE);
}

static uint32_t start_video(void){
    if(!iz_pc_enabled())return SB4_STREAM_UNAVAILABLE;
    if(video_started)return 0;
    if(!started)return SB4_STREAM_UNAVAILABLE;
    video_body=malloc(IZ_PC_MEDIA_CAPACITY);
    if(!video_body)return ENOMEM;
    atomic_store(&video_phase,VIDEO_IDLE);
    Result rc=threadCreate(&video_worker,run_video,NULL,NULL,65536,0x2c,-2);
    if(R_SUCCEEDED(rc)){
        rc=threadStart(&video_worker);
        if(R_FAILED(rc))threadClose(&video_worker);
    }
    if(R_FAILED(rc)){free(video_body);video_body=NULL;return rc;}
    video_started=true;
    return 0;
}

uint32_t iz_pc_video_open(uint32_t source,uint64_t *token){
    if(!token)return SB4_STREAM_INVALID;
    *token=0;
    if(video_token||(source!=SB4_STREAM_BLITBUN&&source!=SB4_STREAM_VACOOTUBE))return SB4_STREAM_INVALID;
    uint32_t rc=start_video();
    if(rc)return rc;
    if(!++video_generation)++video_generation;
    video_operation=source==SB4_STREAM_BLITBUN?'Y':'Z';
    media_wire=source==SB4_STREAM_BLITBUN&&start_audio()?MEDIA_PROBE:MEDIA_COMBINED;
    video_seen=video_publication=audio_seen=0;
    video_reset=true;
    *token=video_token=video_generation;
    atomic_store(&video_input_token,video_token);
    return 0;
}

uint32_t iz_pc_video_update(struct IzNativeApp *app,uint64_t token){
    if(!token||token!=video_token)return SB4_STREAM_INVALID;
    if(!iz_pc_enabled()){
        sb4StreamSetSourceReady(app,0);
        return SB4_STREAM_UNAVAILABLE;
    }
    if(audio_started)update_audio(app,token);
    uint32_t ack=0,rc=sb4StreamFrameAck(app,&ack);
    if(rc)return rc;
    if(video_reset){sb4StreamSetSourceReady(app,0);video_reset=false;}
    if(video_publication&&ack==video_publication){
        sb4StreamSetSourceReady(app,1);
        video_publication=0;
    }
    int phase=atomic_load(&video_phase);
    uint32_t status=SB4_STREAM_PENDING;
    if(phase==VIDEO_RESULT){
        if(video_request_token==token){
            media_wire=video_request_wire;
            if(video_result==IZ_PC_VIDEO_FRAME){
                const IzPcVideoFrame *frame=&media_frame.video;
                rc=sb4StreamSubmitFrame(app,frame->width,frame->height,frame->format,
                    frame->pixels,frame->bytes,&video_publication);
                if(rc==SB4_STREAM_PENDING)return rc;
                if(!rc)video_seen=frame->sequence;
                status=rc;
            }else if(video_result==IZ_PC_VIDEO_CURRENT)status=0;
            else {
                sb4StreamSetSourceReady(app,0);
                video_seen=video_publication=0;
                if(media_wire!=MEDIA_SEPARATE)audio_seen=0;
                status=SB4_STREAM_UNAVAILABLE;
            }
            if(media_wire==MEDIA_COMBINED&&!status&&media_frame.pcm){
                rc=iz_pc_pcm_submit(&pcm,app,media_frame.pcm);
                if(!rc||rc==SB4_STREAM_UNAVAILABLE)audio_seen=media_frame.audio_sequence;
                else if(rc!=SB4_STREAM_PENDING)status=rc;
            }
        }
        phase=VIDEO_IDLE;
    }
    if(phase==VIDEO_IDLE){
        video_request_token=token;
        video_request_wire=media_wire;
        video_request_operation=video_operation;
        video_request_seen=video_seen;
        audio_request_seen=audio_seen;
        iz_pc_capture_controls(app,video_request_input);
        atomic_store(&video_phase,VIDEO_REQUEST);
    }
    return status;
}

uint32_t iz_pc_video_close(struct IzNativeApp *app,uint64_t token){
    if(!token||token!=video_token)return SB4_STREAM_INVALID;
    atomic_store(&video_input_token,0);
    const uint32_t hidden=sb4StreamSetSourceReady(app,0);
    const uint32_t sound=iz_pc_pcm_close(&pcm,app);
    if(sound==IZ_NATIVE_STOP_PENDING)return sound;
    video_token=0;
    return sound?sound:hidden;
}

static bool same_digest(const uint8_t a[32],const uint8_t b[32]){
    unsigned difference=0;
    for(unsigned i=0;i<32;++i)difference|=a[i]^b[i];
    return difference==0;
}

static atomic_uint receive_total,receive_bytes;

bool iz_pc_receive_progress(uint32_t *received,uint32_t *total){
    *total=atomic_load(&receive_total);
    *received=atomic_load(&receive_bytes);
    return *total!=0;
}

static bool receive_package(int fd,uint32_t bytes,uint32_t limit,const uint8_t digest[32]){
    atomic_store(&receive_total,0);
    atomic_store(&receive_bytes,0);
    if(!bytes||bytes>limit)return false;
    FILE *file=fopen(LINK_STAGE,"wb");
    if(!file)return false;
    Sha256Context hash;
    sha256ContextCreate(&hash);
    uint8_t chunk[16384],actual[32];
    bool valid=true;
    const uint32_t total=bytes;
    atomic_store(&receive_total,total);
    while(bytes){
        size_t n=bytes<sizeof chunk?bytes:sizeof chunk;
        if(!transfer(fd,chunk,n,false)||fwrite(chunk,1,n,file)!=n){valid=false;break;}
        sha256ContextUpdate(&hash,chunk,n);
        bytes-=(uint32_t)n;
        atomic_store(&receive_bytes,total-bytes);
    }
    sha256ContextGetHash(&hash,actual);
    if(fclose(file))valid=false;
    valid=valid&&same_digest(actual,digest)&&!rename(LINK_STAGE,IZ_PC_LINK_PACKAGE);
    if(!valid)unlink(LINK_STAGE);
    return valid;
}

static bool serve_file_request(int fd,const uint8_t nonce[32],uint32_t bytes,const uint8_t digest[32]){
    uint8_t request[IZ_PC_FILE_REQUEST_MAX], actual[32];
    if(!bytes||bytes>sizeof request||!transfer(fd,request,bytes,false))return false;
    sha256CalculateHash(actual,request,bytes);
    if(!same_digest(actual,digest))return false;
    uint8_t *body=NULL;
    size_t length=0;
    uint32_t status;
    if(request[0]=='D'&&bytes>13){
        status=iz_pc_request_remove(request+13,bytes-13);
        if(!status){
            const uint64_t start=armGetSystemTick(), timeout=armGetSystemTickFreq();
            while(!iz_pc_remove_result(&status)){
                if(armGetSystemTick()-start>=timeout&&iz_pc_expire_remove()){status=EBUSY;break;}
                if(!iz_pc_enabled()||atomic_load(&ending)){iz_pc_expire_remove();status=ECANCELED;break;}
                svcSleepThread(10000000);
            }
        }
        fprintf(stderr,"IZLAUNCHER_PC_REMOVE rc=0x%08x\n",status);
    }else{
        body=malloc(IZ_PC_FILE_LIMIT);
        status=body?iz_pc_files(request,bytes,body,&length):ENOMEM;
    }
    uint8_t signed_data[73],response[72];
    memcpy(signed_data,nonce,32);
    signed_data[32]='R';
    for(unsigned i=0;i<4;i++){
        response[i]=(uint8_t)(status>>(24-i*8));
        response[i+4]=(uint8_t)(length>>(24-i*8));
    }
    sha256CalculateHash(response+8,body?body:(const uint8_t*)"",length);
    memcpy(signed_data+33,response,40);
    hmacSha256CalculateMac(response+40,key,sizeof key,signed_data,sizeof signed_data);
    bool ok=transfer(fd,response,sizeof response,true)&&transfer(fd,body,length,true);
    free(body);
    return ok;
}

static void send_import_result(int fd, uint8_t operation, uint8_t message[69], const uint8_t digest[32]) {
    uint64_t result = IMPORT_PENDING;
    const uint64_t start = armGetSystemTick();
    const uint64_t timeout = armGetSystemTickFreq() * (operation == 'K' ? 300 : 60);
    while (iz_pc_enabled() && !atomic_load(&ending) && armGetSystemTick() - start < timeout) {
        result = atomic_load(&import_result);
        if (result != IMPORT_PENDING) break;
        svcSleepThread(10000000);
    }
    if (result == IMPORT_PENDING) return;

    uint8_t reply[36];
    for (unsigned i = 0; i < 4; ++i)
        reply[i] = (uint8_t)(result >> (24 - i * 8));
    message[32] = operation;
    memcpy(message + 33, digest, 32);
    memcpy(message + 65, reply, 4);
    hmacSha256CalculateMac(reply + 4, key, sizeof key, message, 69);
    transfer(fd, reply, sizeof reply, true);
}

/* K handles full-sized archives. K and J acknowledge the import result.
 * I and F acknowledge the transfer on older PC services. */
static enum IzPcDelivery exchange(uint8_t operation){
    bool fetch=operation!='S';
    int fd=open_link();
    if(fd<0){atomic_store(&online,false);return IZ_PC_ERROR;}
    uint8_t message[69],request[33],reply[69],auth[32];
    bool verified=false;
    enum IzPcDelivery result=IZ_PC_ERROR;
    if(!transfer(fd,message,32,false))goto done;
    request[0]=message[32]=operation;
    hmacSha256CalculateMac(request+1,key,sizeof key,message,33);
    if(!transfer(fd,request,sizeof request,true)||!transfer(fd,reply,sizeof reply,false))goto done;
    memcpy(message+32,reply,37);
    hmacSha256CalculateMac(auth,key,sizeof key,message,sizeof message);
    if(!same_digest(auth,reply+37))goto done;
    uint32_t bytes=(uint32_t)reply[1]<<24|(uint32_t)reply[2]<<16|(uint32_t)reply[3]<<8|reply[4];
    if(reply[0]==0&&!bytes){
        verified=true;
        result=IZ_PC_EMPTY;
        if(!fetch&&reply[5]==0x49&&reply[6]==0x5a&&atomic_load(&auto_fetch))atomic_store(&auto_offer,true);
        if(operation=='K'&&reply[5]==1){
            uint32_t code=(uint32_t)reply[6]<<24|(uint32_t)reply[7]<<16|(uint32_t)reply[8]<<8|reply[9];
            atomic_store(&previous_import_code,code);
            result=IZ_PC_IMPORT_REPORTED;
        }
        if(operation=='K'&&reply[5]==2)result=IZ_PC_TRANSFER_REPORTED;
    }
    if(reply[0]==3&&fetch&&!bytes){verified=true;result=IZ_PC_UPDATE_REQUIRED;}
    if(reply[0]==2&&!fetch){
        verified=true;
        if(serve_file_request(fd,message,bytes,reply+5))result=IZ_PC_FILES;
    }
    if(reply[0]==1&&fetch){
        verified=true;
        uint32_t limit = operation == 'K' ? LINK_ARCHIVE_LIMIT : LINK_LEGACY_LIMIT;
        if(receive_package(fd,bytes,limit,reply+5)){
            result=IZ_PC_PACKAGE;
            if(operation=='J'||operation=='K'){
                atomic_store(&online,true);
                atomic_store(&import_result,IMPORT_PENDING);
                atomic_store(&delivery,IZ_PC_PACKAGE);
                send_import_result(fd,operation,message,reply+5);
            }else{
                message[32]='A';
                memcpy(message+33,reply+5,32);
                hmacSha256CalculateMac(auth,key,sizeof key,message,65);
                transfer(fd,auth,sizeof auth,true);
                atomic_store(&delivery,IZ_PC_PACKAGE);
            }
        }
    }
done:
    atomic_store(&online,verified);
    close(fd);
    return result;
}

static bool menu_download(void *context,void *data,size_t bytes){
    return transfer(*(int*)context,data,bytes,false);
}
/* Separate from application transfers: the only target is a stock menu slot. */
static bool menu_request(uint8_t operation,const uint8_t *body,size_t bytes){
    int fd=open_link();
    if(fd<0)return false;
    uint8_t nonce[32],signed_data[32+1+IZ_MENU_REPORT_BYTES],request[1+IZ_MENU_REPORT_BYTES+32],reply[69],mac[32];
    bool ok=false;
    if(!transfer(fd,nonce,sizeof nonce,false))goto done;
    memcpy(signed_data,nonce,sizeof nonce);
    request[0]=operation;memcpy(request+1,body,bytes);
    memcpy(signed_data+32,request,1+bytes);
    hmacSha256CalculateMac(request+1+bytes,key,sizeof key,signed_data,33+bytes);
    if(!transfer(fd,request,33+bytes,true)||!transfer(fd,reply,sizeof reply,false))goto done;
    uint8_t receipt[69];memcpy(receipt,nonce,32);memcpy(receipt+32,reply,37);
    hmacSha256CalculateMac(mac,key,sizeof key,receipt,sizeof receipt);
    if(!same_digest(mac,reply+37))goto done;
    uint32_t length=(uint32_t)reply[1]<<24|(uint32_t)reply[2]<<16|(uint32_t)reply[3]<<8|reply[4];
    if(operation=='T'){
        if(reply[0]==4&&!length&&atomic_load(&auto_fetch))
            ok=menu_request('H',reply+5,16);
    }else if(reply[0]==1){
        ok=iz_menu_receive(body,length,menu_download,&fd);
    }
done:
    close(fd);return ok;
}

static void store_exchange(void){
    store_count=0;
    store_status=EIO;
    const bool select=store_id[0]!=0;
    const size_t request_size=select?49:1;
    int fd=open_link();
    if(fd<0)return;
    struct timeval timeout={.tv_sec=15};
    setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout);
    uint8_t request[81]={select?'U':'C'}, signed_data[81], reply[69], digest[32];
    if(!transfer(fd,signed_data,32,false))goto done;
    if(select)memcpy(request+1,store_id,sizeof store_id);
    memcpy(signed_data+32,request,request_size);
    hmacSha256CalculateMac(request+request_size,key,sizeof key,signed_data,32+request_size);
    if(!transfer(fd,request,request_size+32,true)||!transfer(fd,reply,sizeof reply,false))goto done;
    memcpy(signed_data+32,reply,37);
    hmacSha256CalculateMac(digest,key,sizeof key,signed_data,69);
    if(!same_digest(digest,reply+37))goto done;
    uint32_t bytes=(uint32_t)reply[1]<<24|(uint32_t)reply[2]<<16|(uint32_t)reply[3]<<8|reply[4];
    if(bytes>sizeof store_apps||bytes%sizeof store_apps[0]||(select&&bytes))goto done;
    if(!transfer(fd,store_apps,bytes,false))goto done;
    sha256CalculateHash(digest,store_apps,bytes);
    if(!same_digest(digest,reply+5))goto done;
    store_status=reply[0];
    if(!store_status){
        store_count=bytes/sizeof store_apps[0];
        for(unsigned i=0;i<store_count;i++){
            store_apps[i].id[sizeof store_apps[i].id-1]=0;
            store_apps[i].name[sizeof store_apps[i].name-1]=0;
        }
        if(select)atomic_store(&fetching,true);
    }
done:
    close(fd);
}

bool iz_pc_store_request(const char *id){
    if(!iz_pc_enabled()||atomic_load(&store_state)!=STORE_IDLE||(id&&strlen(id)>=sizeof store_id))return false;
    memset(store_id,0,sizeof store_id);
    if(id)memcpy(store_id,id,strlen(id));
    atomic_store(&store_state,STORE_REQUEST);
    return true;
}

bool iz_pc_store_result(IzPcStoreApp *apps,unsigned *count,unsigned *status,bool *selected){
    if(atomic_load(&store_state)!=STORE_RESULT)return false;
    *count=store_count;
    *status=store_status;
    *selected=store_id[0]!=0;
    if(store_count)memcpy(apps,store_apps,store_count*sizeof *apps);
    atomic_store(&store_state,STORE_IDLE);
    return true;
}

static void run(void *unused){
    (void)unused;
    unsigned delay=0,retry=0,menu_tick=0,generation=0;
    uint64_t offline_since=0;
    while(!atomic_load(&ending)){
        if(!begin_channel_job()){
            svcSleepThread(100000000);
            continue;
        }
        const unsigned current_generation=atomic_load(&channel_generation);
        if(generation!=current_generation){
            generation=current_generation;
            delay=retry=menu_tick=0;
            offline_since=armGetSystemTick();
        }
        if(atomic_load(&store_state)==STORE_REQUEST){
            store_exchange();
            atomic_store(&store_state,STORE_RESULT);
        }
        bool manual=atomic_exchange(&fetching,false);
        bool automatic=atomic_exchange(&auto_offer,false)&&atomic_load(&auto_fetch)&&!retry;
        bool fetch=manual||automatic;
        if(retry)--retry;
        if(fetch||!delay){
            enum IzPcDelivery result=exchange(fetch?'K':'S');
            if(fetch&&result==IZ_PC_ERROR&&!atomic_load(&online))result=exchange('J');
            if(fetch&&result==IZ_PC_ERROR&&!atomic_load(&online))result=exchange('I');
            if(fetch&&result==IZ_PC_ERROR&&!atomic_load(&online))result=exchange('F');
            if(fetch&&result!=IZ_PC_PACKAGE)atomic_store(&delivery,result);
            if(automatic&&result!=IZ_PC_PACKAGE)retry=150;
            if(atomic_load(&online))offline_since=0;
            else {
                const uint64_t now=armGetSystemTick();
                if(!offline_since)offline_since=now;
                // Switching from dock Ethernet to Wi-Fi can take several seconds.
                if(armTicksToNs(now-offline_since)>=LINK_RECONNECT_NS)iz_pc_pause();
            }
            delay=result==IZ_PC_FILES?1:6;
        }
        if(iz_pc_enabled()&&!menu_tick&&atomic_load(&online)&&!fetch&&atomic_load(&store_state)==STORE_IDLE){
            uint8_t report[IZ_MENU_REPORT_BYTES];iz_menu_report(report);
            menu_request('T',report,sizeof report);menu_tick=30;
        }
        if(menu_tick)--menu_tick;
        --delay;
        end_channel_job();
        svcSleepThread(100000000);
    }
}

// Retained as a binary compatibility entry point for the menu updater. It now
// returns a public protocol tag and never reads a pairing file from the SD.
uint32_t iz_pc_read_key(uint8_t out[32]){
    sha256CalculateHash(out,LINK_PUBLIC_CONTEXT,sizeof LINK_PUBLIC_CONTEXT-1);
    return 0;
}

uint32_t iz_pc_open_sockets(void){
    mutexLock(&socket_lock);
    Result result=sockets_ready?0:socketInitializeDefault();
    if(R_SUCCEEDED(result))sockets_ready=true;
    mutexUnlock(&socket_lock);
    return result;
}

uint32_t iz_pc_start(void){
    if(iz_pc_enabled())return 0;
    if(started){
        mutexLock(&channel_lock);
        const bool busy=channel_jobs!=0;
        if(!busy){
            request_pending=false;
            atomic_store(&auto_offer,false);
            atomic_store(&fetching,false);
            atomic_store(&store_state,STORE_IDLE);
            atomic_store(&online,false);
            if(atomic_load(&delivery)!=IZ_PC_PACKAGE)atomic_store(&delivery,IZ_PC_IDLE);
            atomic_fetch_add(&channel_generation,1);
            atomic_store(&channel_open,true);
        }
        mutexUnlock(&channel_lock);
        return busy?EBUSY:0;
    }
    // This is a public protocol tag, not a pairing secret. The user opens
    // the channel explicitly with R + L3 and can close it with the same chord.
    iz_pc_read_key(key);
    char address[32]="10.42.0.1";
    FILE *config=fopen(LINK_CONFIG,"rb");
    link_count=0;
    if(config){
        if(!fgets(address,sizeof address,config)||!strchr(address,'\n')){
            fclose(config);
            return 0xe8b2;
        }
        address[strcspn(address,"\r\n")]=0;
    }
    if(inet_pton(AF_INET,address,&link_addresses[0])!=1||
       link_addresses[0].s_addr==0){
        if(config)fclose(config);
        return 0xe8b2;
    }
    link_count=1;
    if(config){
        // The second line is reserved for the original protocol port.
        if(fgets(address,sizeof address,config)){
            while(link_count<LINK_HOST_LIMIT&&fgets(address,sizeof address,config)){
                struct in_addr candidate;
                address[strcspn(address,"\r\n")]=0;
                if(inet_pton(AF_INET,address,&candidate)==1&&candidate.s_addr&&
                   !iz_pc_peer_allowed(candidate.s_addr))link_addresses[link_count++]=candidate;
            }
        }
        fclose(config);
    }
    atomic_store(&preferred_host,0);
    atomic_store(&routed_address,0);
    Result rc=iz_pc_open_sockets();
    if(R_FAILED(rc))return rc;
    atomic_store(&ending,false);
    rc=threadCreate(&worker,run,NULL,NULL,65536,0x2c,-2);
    if(R_FAILED(rc))return rc;
    rc=threadStart(&worker);
    if(R_FAILED(rc)){threadClose(&worker);return rc;}
    started=true;
    atomic_fetch_add(&channel_generation,1);
    atomic_store(&channel_open,true);
    return 0;
}

bool iz_pc_menu_idle(void){
    return !request_pending&&!atomic_load(&fetching)&&
        atomic_load(&store_state)==STORE_IDLE&&atomic_load(&delivery)!=IZ_PC_PACKAGE;
}
void iz_pc_allow_auto_fetch(bool allowed){atomic_store(&auto_fetch,allowed);}

bool iz_pc_fetch(void){
    if(!iz_pc_enabled())return false;
    if(!request_pending){
        request_pending=true;
        atomic_store(&fetching,true);
    }
    return true;
}

bool iz_pc_online(void){return iz_pc_enabled()&&atomic_load(&online);}
enum IzPcDelivery iz_pc_delivery(uint32_t *code){
    enum IzPcDelivery result=atomic_exchange(&delivery,IZ_PC_IDLE);
    if(result==IZ_PC_IMPORT_REPORTED)*code=atomic_load(&previous_import_code);
    if(result!=IZ_PC_IDLE){
        request_pending=false;
        if(result==IZ_PC_PACKAGE||result==IZ_PC_ERROR)atomic_store(&receive_total,0);
    }
    return result;
}

void iz_pc_import_result(uint32_t code){
    atomic_store(&import_result,code);
}

void iz_pc_close(void){
    iz_pc_pause();
    if(started){
        atomic_store(&ending,true);
        atomic_store(&video_input_token,0);
        if(audio_started){
            threadWaitForExit(&audio_worker);
            threadClose(&audio_worker);
            audio_started=false;
        }
        if(video_started){
            threadWaitForExit(&video_worker);
            threadClose(&video_worker);
            free(video_body);
            video_body=NULL;
            video_started=false;
            video_token=0;
        }
        threadWaitForExit(&worker);
        threadClose(&worker);
        started=false;
    }
    mutexLock(&socket_lock);
    if(sockets_ready)socketExit();
    sockets_ready=false;
    mutexUnlock(&socket_lock);
    memset(key,0,sizeof key);
}
