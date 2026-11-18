#define _POSIX_C_SOURCE 200809L
#include "../legacy/native/module-folder.h"
#include "pc-files.h"
#include "../legacy/native/abi.h"
#include <dirent.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <switch/services/ncm.h>
#include <switch/services/ns.h>
#include <switch/nacp.h>

enum { REMOVE_IDLE, REMOVE_WAITING, REMOVE_RUNNING, REMOVE_DONE };
static atomic_int remove_state;
static char remove_id[48];
static uint32_t remove_result;
/* Only numeric observations cross from the menu thread to the PC worker.
 * Never expose an executing app pointer or wait for the graphics thread. */
static atomic_uint catalog_state[8];
const char *iz_ounce_jit_build_id(void);
uint32_t iz_foldrift_sd_status(void);
uint64_t iz_ounce_jit_session_tick(void);
void iz_pc_catalog_state(uint32_t page,uint32_t pages,uint32_t nro,uint32_t iza,uint32_t shown,uint32_t rejected,uint32_t error,uint32_t generation){
    const uint32_t values[]={page,pages,nro,iza,shown,rejected,error,generation};
    for(unsigned i=0;i<8;i++)atomic_store(&catalog_state[i],values[i]);
}

static bool module_id(const char *id,size_t size){
    if(!size||size>=sizeof remove_id)return false;
    for(size_t i=0;i<size;++i){
        char c=id[i];
        if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||(i&&c=='-')))return false;
    }
    return true;
}

uint32_t iz_pc_request_remove(const uint8_t *id,size_t bytes){
    if(!module_id((const char*)id,bytes))return EINVAL;
    if(atomic_load(&remove_state)!=REMOVE_IDLE)return EBUSY;
    memset(remove_id,0,sizeof remove_id);
    memcpy(remove_id,id,bytes);
    atomic_store(&remove_state,REMOVE_WAITING);
    return 0;
}

bool iz_pc_take_remove(char id[48]){
    int phase=REMOVE_WAITING;
    if(!atomic_compare_exchange_strong(&remove_state,&phase,REMOVE_RUNNING))return false;
    memcpy(id,remove_id,sizeof remove_id);
    return true;
}

void iz_pc_finish_remove(uint32_t result){
    remove_result=result;
    atomic_store(&remove_state,REMOVE_DONE);
}

bool iz_pc_remove_result(uint32_t *result){
    if(atomic_load(&remove_state)!=REMOVE_DONE)return false;
    *result=remove_result;
    atomic_store(&remove_state,REMOVE_IDLE);
    return true;
}

bool iz_pc_expire_remove(void){
    int phase=REMOVE_WAITING;
    return atomic_compare_exchange_strong(&remove_state,&phase,REMOVE_IDLE);
}

static void store64(uint8_t *out,uint64_t value){
    for(unsigned i=0;i<8;i++)out[i]=(uint8_t)(value>>(56-i*8));
}

static int visible(const char *name){
    return strcmp(name,".")&&strcmp(name,"..")&&strcasecmp(name,"pc-link.key")&&strcasecmp(name,"pc-link.cfg")&&
           strncasecmp(name,".pc-delivery.",13);
}

static int sd_path(char out[775],const uint8_t *input,size_t size){
    if(!size||size>768||input[0]!='/')return 0;
    memcpy(out,"sdmc:",5);
    for(size_t at=1;at<size;){
        const size_t start=at;
        while(at<size&&input[at]!='/'){
            uint8_t c=input[at++];
            if(c<32||c==127||c==':'||c=='\\')return 0;
        }
        size_t length=at-start;
        if(!length||length>255)return 0;
        char part[256];
        memcpy(part,input+start,length);part[length]=0;
        if(!visible(part))return 0;
        if(at<size&&++at==size)return 0;
    }
    memcpy(out+5,input,size);out[5+size]=0;
    return 1;
}

static bool list_file(uint8_t *body,size_t *total,const char *name,size_t length,const struct stat *info){
    if(length>255||length+10>IZ_PC_FILE_LIMIT-*total)return false;
    body[*total]=S_ISDIR(info->st_mode);
    body[*total+1]=(uint8_t)length;
    store64(body+*total+2,info->st_size);
    memcpy(body+*total+10,name,length);
    *total+=length+10;
    return true;
}

static uint32_t list(const char *folder,uint8_t *body,size_t *written,bool installed){
    DIR *dir=opendir(folder);
    if(!dir)return errno;
    uint32_t result=0;
    size_t total=0;
    for(;;){
        errno=0;
        struct dirent *item=readdir(dir);
        if(!item){result=errno;break;}
        if(!visible(item->d_name))continue;
        size_t size=strlen(item->d_name);
        char child[1040];
        int n=snprintf(child,sizeof child,"%s%s%s",folder,
                       folder[strlen(folder)-1]=='/'?"":"/",item->d_name);
        struct stat info;
        if(n<0||(size_t)n>=sizeof child||lstat(child,&info))continue;
        if(installed){
            if(!S_ISDIR(info.st_mode)||!module_id(item->d_name,size))continue;
            char launcher[sizeof child+sizeof "/launcher.iza"];
            snprintf(launcher,sizeof launcher,"%s/launcher.nro",child);
            if(!lstat(launcher,&info)&&S_ISREG(info.st_mode)){
                char name[52];
                int length=snprintf(name,sizeof name,"%s.nro",item->d_name);
                if(length<0||(size_t)length>=sizeof name||
                   !list_file(body,&total,name,(size_t)length,&info)){result=EOVERFLOW;break;}
            }
            snprintf(launcher,sizeof launcher,"%s/launcher.iza",child);
            if(lstat(launcher,&info)||!S_ISREG(info.st_mode))continue;
        }
        if(!S_ISDIR(info.st_mode)&&!S_ISREG(info.st_mode))continue;
        if(!list_file(body,&total,item->d_name,size,&info)){result=EOVERFLOW;break;}
    }
    if(closedir(dir)&&!result)result=errno;
    if(!result)*written=total;
    return result;
}

uint32_t iz_pc_files(const uint8_t *request,size_t bytes,uint8_t *body,size_t *written){
    *written=0;
    if(bytes<14||bytes>IZ_PC_FILE_REQUEST_MAX)return EINVAL;
    char name[775];
    if(!sd_path(name,request+13,bytes-13))return EINVAL;
    /* Read-only metadata RPC. Match RUN22: application metadata from internal,
     * SD and game-card databases. The host divides overflowing title ranges,
     * so this fixed-size response does not cap the number of installed games. */
    if(request[0]=='G'){
        unsigned storage=0;unsigned long long low=0,high=0;int end=0;
        if(sscanf(name,"sdmc:/games/%u/%16llx/%16llx%n",&storage,&low,&high,&end)!=3||
           name[end]||low>high||(storage!=4&&storage!=5&&storage!=2))return EINVAL;
        Result rc=ncmInitialize();if(R_FAILED(rc))return rc;
        NcmContentMetaDatabase db={0};
        rc=ncmOpenContentMetaDatabase(&db,(NcmStorageId)storage);
        if(R_SUCCEEDED(rc)){
            NcmContentMetaKey keys[32];s32 total=0,count=0;
            rc=ncmContentMetaDatabaseList(&db,&total,&count,keys,32,
                NcmContentMetaType_Application,0,low,high,NcmContentInstallType_Full);
            if(R_SUCCEEDED(rc)){
                if(count<0||count>32||total<count)rc=EIO;
                else{
                    /* LE counts, followed by the SDK's 16-byte metadata keys. */
                    memcpy(body,&total,4);memcpy(body+4,&count,4);
                    memcpy(body+8,keys,(size_t)count*sizeof *keys);
                    *written=8+(size_t)count*sizeof *keys;
                }
            }
            ncmContentMetaDatabaseClose(&db);
        }
        ncmExit();return rc;
    }
    if(request[0]=='N'){
        unsigned long long title=0;int end=0;
        if(sscanf(name,"sdmc:/games/%16llx%n",&title,&end)!=1||name[end])return EINVAL;
        Result rc=nsInitialize();if(R_FAILED(rc))return rc;
        NsApplicationControlData *control=calloc(1,sizeof *control);
        if(!control){nsExit();return ENOMEM;}
        u64 actual=0;
        rc=nsGetApplicationControlData(NsApplicationControlSource_Storage,title,
            control,sizeof *control,&actual);
        if(R_SUCCEEDED(rc)){
            if(actual<sizeof control->nacp)rc=EIO;
            else{
                NacpLanguageEntry *entry=NULL;
                rc=nacpGetLanguageEntry(&control->nacp,&entry);
                if(entry){*written=strnlen(entry->name,sizeof entry->name);memcpy(body,entry->name,*written);}
            }
        }
        free(control);nsExit();return rc;
    }
    if(request[0]=='Q'){
        if(strcmp(name,"sdmc:/"))return EINVAL;
        int n=snprintf((char*)body,IZ_PC_FILE_LIMIT,"{\"foldriftPaths\":1,\"gamesRpc\":1,\"foldriftStorage\":%u,\"runtime\":\"run25-r799\",\"runtimeBuild\":\"%s\",\"jitSessionTick\":%llu,\"page\":%u,\"pages\":%u,\"nro\":%u,\"iza\":%u,\"shown\":%u,\"rejected\":%u,\"scanError\":%u,\"generation\":%u}",
            iz_foldrift_sd_status(),iz_ounce_jit_build_id(),(unsigned long long)iz_ounce_jit_session_tick(),
            atomic_load(&catalog_state[0]),atomic_load(&catalog_state[1]),atomic_load(&catalog_state[2]),atomic_load(&catalog_state[3]),
            atomic_load(&catalog_state[4]),atomic_load(&catalog_state[5]),atomic_load(&catalog_state[6]),atomic_load(&catalog_state[7]));
        if(n<0||(unsigned)n>=IZ_PC_FILE_LIMIT)return EOVERFLOW;
        *written=(size_t)n;return 0;
    }
    if(request[0]=='B')return list(name,body,written,false);
    if(request[0]=='L')return strcmp(name,"sdmc:" IZ_SD_ROOT)?EINVAL:list(name,body,written,true);
    struct stat info;
    if(lstat(name,&info))return errno;
    if(!S_ISDIR(info.st_mode)&&!S_ISREG(info.st_mode))return EINVAL;
    if(request[0]=='T'){
        body[0]=S_ISDIR(info.st_mode);
        store64(body+1,info.st_size);
        *written=9;
        return 0;
    }
    if(request[0]!='R'||!S_ISREG(info.st_mode))return EINVAL;
    uint64_t offset=0;
    for(unsigned i=1;i<=8;i++)offset=offset<<8|request[i];
    uint32_t length=(uint32_t)request[9]<<24|(uint32_t)request[10]<<16|
                    (uint32_t)request[11]<<8|request[12];
    if(offset>(uint64_t)info.st_size||length>IZ_PC_FILE_LIMIT||
       length>(uint64_t)info.st_size-offset)return EINVAL;
    FILE *file=fopen(name,"rb");
    if(!file)return errno;
    uint32_t result=0;
    if(fseeko(file,(off_t)offset,SEEK_SET)||fread(body,1,length,file)!=length)result=EIO;
    if(fclose(file)&&!result)result=EIO;
    if(!result)*written=length;
    return result;
}
