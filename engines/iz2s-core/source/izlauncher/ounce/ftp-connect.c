#include <switch.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "ftp-connect.h"
#include "pc-link.h"
#include "ftpsrv.h"
#include "vfs_sd.h"

#define IZ_EXCHANGE_PORT 2121
static const char label[]="IZ2S FTP password v1";
static Thread worker;
static atomic_bool stopping;
static atomic_bool listening,transferring;
static atomic_uint clients;
static atomic_uint local_address;
static atomic_uint_fast64_t sent,received;
static bool active,exchange_owner,nifm_ready;

static void refresh_address(void){
    uint32_t address=0;
    if(nifm_ready&&R_FAILED(nifmGetCurrentIpAddress(&address)))address=0;
    atomic_store(&local_address,address);
}

static void run(void *unused){
    (void)unused;
    uint64_t updated=armGetSystemTick();
    while(!atomic_load(&stopping)){
        if(ftpsrv_loop(100)==FTP_API_LOOP_ERROR_INIT)break;
        struct FtpSrvStats stats;
        ftpsrv_stats(&stats);
        atomic_store(&clients,stats.clients);
        atomic_store(&transferring,stats.transferring);
        atomic_store(&sent,stats.sent);
        atomic_store(&received,stats.received);
        uint64_t tick=armGetSystemTick();
        if(tick-updated>=armGetSystemTickFreq()){
            refresh_address();
            updated=tick;
        }
    }
    atomic_store(&listening,false);
}

static uint32_t start(bool exchange){
    uint8_t key[32],digest[32];
    /* The paired PC link owns the socket device until launcher cleanup. */
    uint32_t result=iz_pc_start();
    if(result)return result;
    result=iz_pc_read_key(key);
    if(result)return result;
    struct FtpSrvConfig config={.user="iz2s",.port=exchange?IZ_EXCHANGE_PORT:IZ_FTP_PORT,
        .timeout=45,.max_sessions=exchange?1:0,
        .bind_ipv4=INADDR_ANY,.peer_allowed=iz_pc_peer_allowed};
    hmacSha256CalculateMac(digest,key,sizeof key,label,sizeof label-1);
    const char *hex="0123456789abcdef";
    for(unsigned i=0;i<sizeof digest;++i){
        config.pass[i*2]=hex[digest[i]>>4];
        config.pass[i*2+1]=hex[digest[i]&15];
    }
    memset(key,0,sizeof key);
    memset(digest,0,sizeof digest);
    ftp_vfs_file_exchange(exchange);
    if(ftpsrv_init(&config)<0){
        result=errno?errno:0xe8c0;
        ftpsrv_exit();
        ftp_vfs_file_exchange(false);
        return result;
    }
    atomic_store(&stopping,false);
    atomic_store(&clients,0);atomic_store(&transferring,false);
    atomic_store(&sent,0);atomic_store(&received,0);atomic_store(&listening,true);
    nifm_ready=R_SUCCEEDED(nifmInitialize(NifmServiceType_User));
    refresh_address();
    result=threadCreate(&worker,run,NULL,NULL,65536,0x2c,-2);
    if(R_SUCCEEDED(result)){
        result=threadStart(&worker);
        if(R_FAILED(result))threadClose(&worker);
    }
    if(R_FAILED(result)){
        if(nifm_ready)nifmExit();
        nifm_ready=false;
        ftpsrv_exit();
        ftp_vfs_file_exchange(false);
        atomic_store(&listening,false);
    }else {active=true;exchange_owner=exchange;}
    return result;
}

void iz_ftp_close(void){
    if(!active)return;
    atomic_store(&stopping,true);
    threadWaitForExit(&worker);
    threadClose(&worker);
    if(nifm_ready)nifmExit();
    nifm_ready=false;
    ftpsrv_exit();
    ftp_vfs_file_exchange(false);
    active=false;
    exchange_owner=false;
}

bool iz_ftp_listening(void){
    return active && !exchange_owner && atomic_load(&listening);
}

uint32_t iz_ftp_address(void){
    const uint32_t current=atomic_load(&local_address);
    return current?current:iz_pc_routed_address();
}

uint32_t iz_ftp_enable(void){
    if(active && exchange_owner)return EBUSY;
    if(iz_ftp_listening())return 0;
    if(active)iz_ftp_close();
    uint32_t result=start(false);
    fprintf(stderr,"IZLAUNCHER_FTP_START rc=0x%08x port=%u\n",result,IZ_FTP_PORT);
    return result;
}

uint32_t iz_ftp_exchange(uint32_t command,Sb4ExchangeFtpState *state){
    if(!state||command>SB4_EXCHANGE_STOP)return EINVAL;
    *state=(Sb4ExchangeFtpState){.port=IZ_EXCHANGE_PORT};
    if(command==SB4_EXCHANGE_STOP){if(exchange_owner)iz_ftp_close();return 0;}
    if(command==SB4_EXCHANGE_START){
        if(active&&!exchange_owner)return EBUSY;
        if(active&&!atomic_load(&listening))iz_ftp_close();
        if(!active){const uint32_t rc=start(true);if(rc)return rc;}
    }
    if(exchange_owner){
        state->running=atomic_load(&listening);
        state->transferring=atomic_load(&transferring);
        state->clients=atomic_load(&clients);
        state->sent=atomic_load(&sent);
        state->received=atomic_load(&received);
    }
    return 0;
}
