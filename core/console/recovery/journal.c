#include "journal.h"
static int same(const uint8_t*a,const uint8_t*b,size_t n){unsigned diff=0;for(size_t i=0;i<n;i++)diff|=a[i]^b[i];return diff==0;}
void iz_recovery_init(IzRecoveryJournal*j,const uint8_t session[16]){uint8_t*p=(void*)j;for(size_t i=0;i<sizeof *j;i++)p[i]=0;for(unsigned i=0;i<16;i++)j->session[i]=session[i];}
uint32_t iz_recovery_begin(IzRecoveryJournal*j,const uint8_t session[16],uint64_t sequence,const uint8_t hash[32]){
 if(!j||!session||!hash||!sequence)return IZ_RECOVERY_INVALID;
 if(!same(j->session,session,16))return IZ_RECOVERY_WRONG_SESSION;
 if(sequence==j->sequence){
  if(!same(j->hash,hash,32))return IZ_RECOVERY_WRONG_HASH;
  if(j->state==IZ_RECOVERY_COMPLETED)return IZ_RECOVERY_CACHED;
  return j->state==IZ_RECOVERY_EXECUTING?IZ_RECOVERY_PENDING:IZ_RECOVERY_INVALID;
 }
 if(j->state==IZ_RECOVERY_EXECUTING)return IZ_RECOVERY_PENDING;
 if(j->sequence==UINT64_MAX||sequence!=j->sequence+1)return IZ_RECOVERY_WRONG_ORDER;
 j->sequence=sequence;for(unsigned i=0;i<32;i++)j->hash[i]=hash[i];j->response_bytes=0;
 j->state=IZ_RECOVERY_EXECUTING;j->executions++;return IZ_RECOVERY_EXECUTE;
}
uint32_t iz_recovery_complete(IzRecoveryJournal*j,const void*response,uint32_t bytes){
 if(!j||j->state!=IZ_RECOVERY_EXECUTING||!response||bytes!=IZ_RECOVERY_RESPONSE_BYTES)return IZ_RECOVERY_INVALID;
 const uint8_t*p=response;for(uint32_t i=0;i<bytes;i++)j->response[i]=p[i];j->response_bytes=bytes;j->state=IZ_RECOVERY_COMPLETED;return IZ_RECOVERY_CACHED;
}
