// SD save files use the same promoted filesystem capability as PS1. The
// launcher retains that capability; this client owns only files it opens.
#include "ps2_sd_saves.h"
#ifdef PS2_NATIVE_SD_LIVE_CARD
#include "ps2_sd_card.h"
#endif
#include "native_sd_write.h"
#include "hbxl.h"
#include "hbx_sdk.h"
#include <cstdio>
#include <cstring>
#include <mutex>
namespace {
Ps2SdWrite state{};std::mutex owner;thread_local uint64_t depth=0;
void enter(uint64_t*previous){*previous=depth;if(!depth)owner.lock();++depth;}
void restore(uint64_t previous){if(depth!=previous+1){state.poisoned=1;return;}depth=previous;if(!depth)owner.unlock();}
uint32_t open(uint64_t*h,const char*p,int m){return depth?ps2_sd_write_open(&state,h,p,m):PS2_SD_WRITE_INVALID;}
void close(uint64_t h){if(depth)ps2_sd_write_close(&state,h);else state.poisoned=1;}
uint32_t size(int64_t*n,uint64_t h){return depth?ps2_sd_write_size(&state,n,h):PS2_SD_WRITE_INVALID;}
uint32_t read(size_t*n,uint64_t h,int64_t o,void*p,size_t b){return depth?ps2_sd_write_read(&state,n,h,o,p,b):PS2_SD_WRITE_INVALID;}
uint32_t write(uint64_t h,int64_t o,const void*p,size_t n,const uint32_t*v){return depth?ps2_sd_write_write(&state,h,o,p,n,v):PS2_SD_WRITE_INVALID;}
uint32_t flush(uint64_t h){return depth?ps2_sd_write_flush(&state,h):PS2_SD_WRITE_INVALID;}
uint32_t resize(uint64_t h,int64_t n){return depth?ps2_sd_write_resize(&state,h,n):PS2_SD_WRITE_INVALID;}
uint32_t create(const char*p,int64_t n,int option){return depth?ps2_sd_write_create(&state,p,n,option):PS2_SD_WRITE_INVALID;}
int missing(uint32_t r){return r==0x202;}
#ifdef PS2_NATIVE_SD_LIVE_CARD
uint32_t rename(const char*a,const char*b){uint64_t previous;enter(&previous);const uint32_t r=ps2_sd_write_rename(&state,a,b);restore(previous);return r;}
#endif
}
extern "C" int ps2_sd_saves_native_initialize(){
 if(ps2::SdSavesEnabled())return ps2_sd_write_healthy(&state);
 if(!hbxl_ctx||hbxl_ctx->magic!=HBX_ABI_MAGIC||hbxl_ctx->abi_version!=3||hbxl_ctx->size<sizeof(*hbxl_ctx)||!hbxl_ctx->args[6])return 0;
 const auto*c=reinterpret_cast<const Ps2SdWriteConfig*>(hbxl_ctx->args[6]);
 if((hbxl_ctx->args[6]&7)||hbxl_ctx->args[6]>(1ull<<39)-sizeof(*c)||c->sdk!=hbxl_ctx->audio.initialize_parameter-0x1c7f30)return 0;
 static const unsigned char send[]={0x41,4,0,0xd4,0xc0,3,0x5f,0xd6},close_cut[]={0xc1,2,0,0xd4,0xc0,3,0x5f,0xd6};
 if(!c->sdk||(c->sdk&4095)||c->sdk>(1ull<<39)-6164480||
  std::memcmp(reinterpret_cast<const void*>(c->sdk+0x15845c),send,sizeof send)||
  std::memcmp(reinterpret_cast<const void*>(c->sdk+0x1583ec),close_cut,sizeof close_cut))return 0;
 if(ps2_sd_write_initialize(&state,c,reinterpret_cast<Ps2SdSend>(c->sdk+0x15845c),reinterpret_cast<Ps2SdClose>(c->sdk+0x1583ec)))return 0;
 uint64_t previous;enter(&previous);uint32_t result=0;
 for(const char*p:{"ps2sd:/izmysh/ps2/saves","ps2sd:/izmysh/ps2/saves/cards"}){uint32_t type=UINT32_MAX;
  result=ps2_sd_write_type(&state,&type,p);if(result||type){if(!result)result=PS2_SD_WRITE_INVALID;break;}}
 restore(previous);
 if(result){std::fprintf(stderr,"PS2 SD saves: promoted SD directory check failed (0x%08x)\n",result);return 0;}
 Ps2SdkFsApi api{enter,restore,open,close,size,read,write,flush,resize,create,missing};
 if(!ps2::ConfigureSdSaves(api,"ps2sd:/izmysh/ps2/saves")
#ifdef PS2_NATIVE_SD_LIVE_CARD
  ||!ps2::ConfigureLiveSdCard(api,rename,"ps2sd:/izmysh/ps2/saves/cards")
#endif
 ){ps2_sd_saves_native_shutdown();return 0;}
 std::fprintf(stderr,"PS2 SD saves: promoted SD capability configured; no SDK remount\n");return 1;
}
extern "C" void ps2_sd_saves_native_shutdown(){
#ifdef PS2_NATIVE_SD_LIVE_CARD
 const bool cards_closed=ps2::ShutdownLiveSdCard();
#else
 const bool cards_closed=true;
#endif
 const bool saves_closed=ps2::ShutdownSdSaves();
 if(!cards_closed||!saves_closed||!ps2_sd_write_shutdown(&state)){
  std::fprintf(stderr,"PS2 SD saves: native file ownership retained\n");return;}
 std::fprintf(stderr,"PS2 SD saves: all owned files closed; borrowed SD capability retained by launcher\n");
}
