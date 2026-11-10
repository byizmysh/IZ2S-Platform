#include "server.h"
#include "crypto.h"
#include "memory.h"
enum { FREE,SEND_CHALLENGE,READ_PROOF,SEND_RECEIPT,READ_REQUEST,SEND_RESPONSE };
static void put32(uint8_t*p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));}
static void put64(uint8_t*p,uint64_t n){for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(n>>(8*i));}
static void drop(IzRecoveryServer*s,unsigned index){IzRecoveryPeer*p=&s->peers[index];if(p->fd>=0)s->transport.close(s->owner,p->fd);memset(p,0,sizeof *p);p->fd=-1;if(s->active==(int)index)s->active=-1;}
static void receipt(IzRecoveryServer*s,IzRecoveryPeer*p){
 uint8_t body[64];memset(body,0,sizeof body);put64(body,s->journal.sequence);put32(body+8,s->journal.state);put32(body+12,s->journal.response_bytes);memcpy(body+16,s->journal.hash,32);put64(body+48,s->journal.executions);
 p->want=iz_recovery_encode(p->output,sizeof p->output,IZ_RECOVERY_RECEIPT,s->journal.sequence,s->journal.session,p->nonce,s->key,body,sizeof body);p->sent=0;p->phase=SEND_RECEIPT;
}
int iz_recovery_server_init(IzRecoveryServer*s,const uint8_t session[16],const uint8_t key[32],const IzRecoveryTransport*t,void*owner){
 if(!s||!session||!key||!t||!t->listen||!t->accept||!t->receive||!t->send||!t->close||!t->execute)return 0;IzRecoveryTransport copy=*t;memset(s,0,sizeof *s);s->transport=copy;s->owner=owner;s->listener=-1;s->active=-1;s->peers[0].fd=s->peers[1].fd=-1;memcpy(s->key,key,32);iz_recovery_init(&s->journal,session);return 1;
}
static void admit(IzRecoveryServer*s,unsigned index,int fd,uint64_t now){
 IzRecoveryPeer*p=&s->peers[index];memset(p,0,sizeof *p);p->fd=fd;p->phase=SEND_CHALLENGE;p->deadline=now+5000;
 uint8_t header[128],nonce[32],body[32];memset(header,0,sizeof header);memset(nonce,0,sizeof nonce);memcpy(body,s->journal.session,16);put64(body+16,++s->generation);put64(body+24,now);iz_recovery_mac(s->key,nonce,header,body,sizeof body,p->nonce);
 p->want=iz_recovery_encode(p->output,sizeof p->output,IZ_RECOVERY_CHALLENGE,0,s->journal.session,p->nonce,s->key,p->nonce,32);
}
static int message(IzRecoveryServer*s,unsigned index){
 IzRecoveryPeer*p=&s->peers[index];IzRecoveryFrame f;if(!iz_recovery_decode(&f,p->input,p->have,s->journal.session,p->nonce,s->key))return 0;
 if(p->phase==READ_PROOF){
  if(f.type!=IZ_RECOVERY_PROOF||f.sequence)return 0;
  // Only a fully authenticated replacement may close the old route.
  if(s->active>=0&&s->active!=(int)index)drop(s,(unsigned)s->active);s->active=(int)index;s->connections++;receipt(s,p);return 1;
 }
 if(p->phase!=READ_REQUEST||s->active!=(int)index)return 0;
 if(f.type==IZ_RECOVERY_PING){receipt(s,p);return 1;}
 if(f.type!=IZ_RECOVERY_REQUEST)return 0;
 uint32_t action=iz_recovery_begin(&s->journal,s->journal.session,f.sequence,f.hash);
 if(action==IZ_RECOVERY_EXECUTE){
  if(s->frozen){s->journal.state=IZ_RECOVERY_EXECUTING;receipt(s,p);return 1;}
  if(s->transport.execute(s->owner,f.payload,s->response)){s->frozen=1;receipt(s,p);return 1;}
  if(iz_recovery_complete(&s->journal,s->response,sizeof s->response)!=IZ_RECOVERY_CACHED){s->frozen=1;return 0;}
 }else if(action==IZ_RECOVERY_PENDING){receipt(s,p);return 1;}
 else if(action!=IZ_RECOVERY_CACHED)return 0;
 p->want=iz_recovery_encode(p->output,sizeof p->output,IZ_RECOVERY_RESPONSE,f.sequence,s->journal.session,p->nonce,s->key,s->journal.response,s->journal.response_bytes);p->sent=0;p->phase=SEND_RESPONSE;return p->want!=0;
}
void iz_recovery_server_tick(IzRecoveryServer*s,uint64_t now){
 if(!s||s->stopped)return;
 if(s->listener<0&&now>=s->retry_at){s->listener=s->transport.listen(s->owner);if(s->listener<0)s->retry_at=now+1000;}
 if(s->listener>=0){
  int slot=s->peers[0].phase==FREE?0:s->peers[1].phase==FREE?1:-1;
  if(slot>=0){int fd=s->transport.accept(s->owner,s->listener);if(fd>=0){if(s->generation==UINT64_MAX)s->transport.close(s->owner,fd);else admit(s,(unsigned)slot,fd,now);}else if(fd==IZ_RECOVERY_IO_FAILED){s->transport.close(s->owner,s->listener);s->listener=-1;s->retry_at=now+1000;}}
 }
 for(unsigned i=0;i<2;i++){
  IzRecoveryPeer*p=&s->peers[i];if(p->phase==FREE)continue;
  if(s->active!=(int)i&&now>=p->deadline){drop(s,i);continue;}
  /* Drain ready work without sleeping between a header, its body and the
   * reply. Offer one complete record to TCP: separate 16 KiB sends left a
   * tiny response tail waiting for delayed ACK/Nagle on every SDK call.
   * The operation budget bounds each peer and AGAIN always yields. */
  for(unsigned work=0;work<8&&p->phase!=FREE;work++){
   if(p->phase==SEND_CHALLENGE||p->phase==SEND_RECEIPT||p->phase==SEND_RESPONSE){
    size_t count=p->want-p->sent;ptrdiff_t n=s->transport.send(s->owner,p->fd,p->output+p->sent,count);if(n==IZ_RECOVERY_IO_AGAIN)break;if(n<=0||(size_t)n>count){drop(s,i);break;}p->sent+=(size_t)n;
    if(p->sent==p->want){p->phase=p->phase==SEND_CHALLENGE?READ_PROOF:READ_REQUEST;p->have=0;p->want=IZ_RECOVERY_HEADER_BYTES;}continue;
   }
   size_t count=p->want-p->have;ptrdiff_t n=s->transport.receive(s->owner,p->fd,p->input+p->have,count);if(n==IZ_RECOVERY_IO_AGAIN)break;if(n<=0||(size_t)n>count){drop(s,i);break;}p->have+=(size_t)n;
   if(p->have<p->want)continue;if(p->want==IZ_RECOVERY_HEADER_BYTES){int size=iz_recovery_frame_size(p->input,p->have);if(size<=0){drop(s,i);break;}p->want=(size_t)size;if(p->have<p->want)continue;}
   if(!message(s,i)){drop(s,i);break;}
  }
 }
}
void iz_recovery_server_wake(IzRecoveryServer*s){if(s&&!s->stopped)s->retry_at=0;}
void iz_recovery_server_stop(IzRecoveryServer*s){if(!s||s->stopped)return;s->stopped=1;for(unsigned i=0;i<2;i++)drop(s,i);if(s->listener>=0)s->transport.close(s->owner,s->listener);s->listener=-1;memset(s->key,0,32);}
