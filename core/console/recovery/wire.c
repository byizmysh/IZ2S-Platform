#include "wire.h"
#include "crypto.h"
#include "memory.h"
static uint32_t get32(const uint8_t*p){uint32_t n=0;for(unsigned i=0;i<4;i++)n|=(uint32_t)p[i]<<(8*i);return n;}
static uint64_t get64(const uint8_t*p){uint64_t n=0;for(unsigned i=0;i<8;i++)n|=(uint64_t)p[i]<<(8*i);return n;}
static void put32(uint8_t*p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));}
static void put64(uint8_t*p,uint64_t n){for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(n>>(8*i));}
static int same(const uint8_t*a,const uint8_t*b,size_t n){unsigned diff=0;for(size_t i=0;i<n;i++)diff|=a[i]^b[i];return diff==0;}
static int length(uint32_t type){switch(type){case IZ_RECOVERY_CHALLENGE:case IZ_RECOVERY_DISCOVERY:return 32;case IZ_RECOVERY_PROOF:case IZ_RECOVERY_PING:return 0;case IZ_RECOVERY_RECEIPT:return 64;case IZ_RECOVERY_REQUEST:return 0x8080;case IZ_RECOVERY_RESPONSE:return 16+0x8000;default:return -1;}}
int iz_recovery_frame_size(const void*data,size_t have){
 if(have<IZ_RECOVERY_HEADER_BYTES)return 0;if(!data)return -1;const uint8_t*p=data;int bytes=length(get32(p+12));
 if(memcmp(p,"IZ2SRCP3",8)||get32(p+8)!=3||bytes<0||get32(p+16)!=(uint32_t)bytes||get32(p+20))return -1;
 for(unsigned i=112;i<128;i++)if(p[i])return -1;return (int)IZ_RECOVERY_HEADER_BYTES+bytes;
}
size_t iz_recovery_encode(void*out,size_t capacity,uint32_t type,uint64_t sequence,const uint8_t session[16],const uint8_t nonce[32],const uint8_t key[32],const void*payload,uint32_t bytes){
 int wanted=length(type);if(!out||!session||!nonce||!key||wanted<0||bytes!=(uint32_t)wanted||capacity<IZ_RECOVERY_HEADER_BYTES+bytes||(bytes&&!payload))return 0;
 uint8_t*p=out;memset(p,0,IZ_RECOVERY_HEADER_BYTES);memcpy(p,"IZ2SRCP3",8);put32(p+8,3);put32(p+12,type);put32(p+16,bytes);put64(p+24,sequence);memcpy(p+32,session,16);
 if(bytes)memcpy(p+IZ_RECOVERY_HEADER_BYTES,payload,bytes);iz_recovery_sha256(p+IZ_RECOVERY_HEADER_BYTES,bytes,p+48);iz_recovery_mac(key,nonce,p,p+IZ_RECOVERY_HEADER_BYTES,bytes,p+80);return IZ_RECOVERY_HEADER_BYTES+bytes;
}
int iz_recovery_decode(IzRecoveryFrame*out,const void*data,size_t bytes,const uint8_t session[16],const uint8_t nonce[32],const uint8_t key[32]){
 if(!out||!data||!session||!nonce||!key)return 0;int size=iz_recovery_frame_size(data,bytes);if(size<=0||(size_t)size!=bytes)return 0;const uint8_t*p=data;uint8_t hash[32],mac[32];
 if(!same(p+32,session,16))return 0;uint32_t n=get32(p+16);iz_recovery_mac(key,nonce,p,p+IZ_RECOVERY_HEADER_BYTES,n,mac);if(!same(mac,p+80,32))return 0;
 iz_recovery_sha256(p+IZ_RECOVERY_HEADER_BYTES,n,hash);if(!same(hash,p+48,32))return 0;
 out->type=get32(p+12);out->sequence=get64(p+24);out->bytes=n;out->payload=p+IZ_RECOVERY_HEADER_BYTES;memcpy(out->hash,hash,32);return 1;
}
