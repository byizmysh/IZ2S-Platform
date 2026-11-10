#include "abi.h"
#include "../memory.h"
static int transient(IzRecoveryNative*c){int e=(int)NET(c,NET_ERRNO,0,0,0,0,0,0);return e==4||e==11||e==35;}
static void close_fd(void*owner,int fd){IzRecoveryNative*c=owner;NET(c,NET_CLOSE,fd,0,0,0,0,0);}
static int nonblocking(IzRecoveryNative*c,int fd){int flags=(int)NET(c,NET_FCNTL,fd,3,0,0,0,0);return flags>=0&&(int)NET(c,NET_FCNTL,fd,4,flags|0x800,0,0,0)>=0;}
static int listen_fd(void*owner){
 IzRecoveryNative*c=owner;int fd=(int)NET(c,NET_SOCKET,2,1,0,0,0,0);if(fd<0)return IZ_RECOVERY_IO_FAILED;
 uint8_t address[16]={16,2};address[2]=(uint8_t)(c->port>>8);address[3]=(uint8_t)c->port;
 if(!nonblocking(c,fd)||(int)NET(c,NET_BIND,fd,address,16,0,0,0)<0||(int)NET(c,NET_LISTEN,fd,4,0,0,0,0)<0){close_fd(c,fd);return IZ_RECOVERY_IO_FAILED;}return fd;
}
static int accept_fd(void*owner,int fd){
 IzRecoveryNative*c=owner;struct {int32_t fd;int16_t events,revents;}poll={fd,1,0};int ready=(int)NET(c,NET_POLL,&poll,1,0,0,0,0);
 if(ready<0)return transient(c)?IZ_RECOVERY_IO_AGAIN:IZ_RECOVERY_IO_FAILED;if(!ready)return IZ_RECOVERY_IO_AGAIN;if(poll.revents&56)return IZ_RECOVERY_IO_FAILED;if(!(poll.revents&1))return IZ_RECOVERY_IO_AGAIN;
 uint8_t address[16];uint32_t size=16;int client=(int)NET(c,NET_ACCEPT,fd,address,&size,0,0,0);if(client<0)return transient(c)?IZ_RECOVERY_IO_AGAIN:IZ_RECOVERY_IO_FAILED;
 if(!nonblocking(c,client)){close_fd(c,client);return IZ_RECOVERY_IO_AGAIN;}return client;
}
static ptrdiff_t receive_fd(void*owner,int fd,void*data,size_t bytes){IzRecoveryNative*c=owner;int64_t n=(int64_t)NET(c,NET_RECV,fd,data,bytes,0x80,0,0);return n>=0?(ptrdiff_t)n:transient(c)?IZ_RECOVERY_IO_AGAIN:IZ_RECOVERY_IO_FAILED;}
static ptrdiff_t send_fd(void*owner,int fd,const void*data,size_t bytes){IzRecoveryNative*c=owner;int64_t n=(int64_t)NET(c,NET_SEND,fd,data,bytes,0x80,0,0);return n>=0?(ptrdiff_t)n:transient(c)?IZ_RECOVERY_IO_AGAIN:IZ_RECOVERY_IO_FAILED;}
static int execute(void*owner,const uint8_t request[0x8080],uint8_t response[IZ_RECOVERY_RESPONSE_BYTES]){
 IzRecoveryNative*c=owner;uint64_t words[10];memcpy(words,request,sizeof words);memset(response,0,IZ_RECOVERY_RESPONSE_BYTES);
 // Packet authentication and the existing NativeClient function/memory guards
 // precede this trusted four-argument dispatcher. Both legacy result positions
 // and the original scratch layout are preserved for existing RPC clients.
 for(unsigned i=0;i<2;i++)if(words[i*5]<0x1000||words[i*5]>=(1ull<<39)||words[i*5]%4){uint64_t bad=0xe901;memcpy(response,&bad,8);memcpy(response+8,&bad,8);return 0;}
 memcpy((void*)(uintptr_t)(c->scratch+0x40000),request,0x8080);uint64_t result[2];c->dispatches++;
 typedef uint64_t(*Call)(uint64_t,uint64_t,uint64_t,uint64_t);for(unsigned i=0;i<2;i++){uint64_t*w=words+i*5;result[i]=((Call)(uintptr_t)w[0])(w[1],w[2],w[3],w[4]);}
 memcpy(response,&result[1],8);memcpy(response+8,&result[0],8);memcpy(response+16,(void*)(uintptr_t)(c->scratch+0x4040),0x8000);return 0;
}
static void announce(IzRecoveryNative*c,uint64_t now){
 if(!c->ip||c->host_count>8||c->discovery_port<1024||c->discovery_port>65535||c->beacon_sequence==UINT64_MAX)return;
 if(c->udp_fd<0)c->udp_fd=(int)NET(c,NET_SOCKET,2,2,17,0,0,0);if(c->udp_fd<0)return;
 uint8_t body[32],packet[160],nonce[32];uint32_t wake=__atomic_load_n(&c->wake,__ATOMIC_ACQUIRE),features=1;memset(body,0,sizeof body);memset(nonce,0,sizeof nonce);memcpy(body,&c->port,4);memcpy(body+4,&c->ip,4);memcpy(body+8,&now,8);memcpy(body+16,&c->network_type,4);memcpy(body+20,&c->network_status,4);
 /* Repeat the latest application request in every authenticated beacon.
  * An offline request must not become a lost one-shot UDP event. */
 memcpy(body+24,&wake,4);memcpy(body+28,&features,4);
 size_t bytes=iz_recovery_encode(packet,sizeof packet,IZ_RECOVERY_DISCOVERY,++c->beacon_sequence,c->session,nonce,c->key,body,sizeof body);
 for(unsigned i=0;i<=c->host_count;i++){uint8_t address[16]={16,2};address[2]=(uint8_t)(c->discovery_port>>8);address[3]=(uint8_t)c->discovery_port;if(i==c->host_count){address[4]=239;address[5]=255;address[6]=77;address[7]=15;}else memcpy(address+4,&c->host_ips[i],4);
  int64_t sent=(int64_t)NET(c,NET_SENDTO,c->udp_fd,packet,bytes,0x80,address,16);if(sent==(int64_t)bytes)c->beacon_packets++;
 }
}
static uint32_t qualify(IzRecoveryNative*c,uint32_t stage){
 if(stage==1)return 0;
 if(stage==2){c->thread_type=NET(c,NET_CURRENT,0,0,0,0,0,0);return c->thread_type?0:0xe910;}
 if(stage==3){(void)NET(c,NET_TICK,0,0,0,0,0,0);return 0;}
 if(stage==4){uint32_t status[3]={0};uint32_t rc=(uint32_t)NET(c,NET_STATUS,status,0,0,0,0,0);if(!rc){c->network_type=status[0];c->network_status=status[2];}return rc;}
 if(stage==5)return (uint32_t)NET(c,NET_ADDRESS,&c->ip,0,0,0,0,0);
 if(stage==6){c->server.listener=(int)NET(c,NET_SOCKET,2,1,0,0,0,0);return c->server.listener>=0?0:0xe911;}
 if(stage==7)return nonblocking(c,c->server.listener)?0:0xe912;
 if(stage==8){uint8_t address[16]={16,2};address[2]=(uint8_t)(c->port>>8);address[3]=(uint8_t)c->port;if((int)NET(c,NET_BIND,c->server.listener,address,16,0,0,0)<0)return 0xe913;return (int)NET(c,NET_LISTEN,c->server.listener,4,0,0,0,0)<0?0xe914:0;}
 if(stage==9){struct {int32_t fd;int16_t events,revents;}p={c->server.listener,1,0};return (int)NET(c,NET_POLL,&p,1,0,0,0,0)<0?0xe915:0;}
 if(stage==10){announce(c,1);return 0;}
 return 0xe916;
}
uint64_t iz_net_run(IzRecoveryNative*c){
 if(!c||c->magic!=0x33524e49||c->version!=3||c->port<1024||c->port>65535||c->scratch<4096||c->scratch%16||c->scratch_bytes!=0x80000||c->scratch>=(1ull<<39)-c->scratch_bytes)return 0xe900;
 for(unsigned i=0;i<NET_COUNT;i++)if(!c->functions[i]){c->state=4;return c->error=0xe902;}
 // Populate callbacks with runtime PC-relative addresses. A const aggregate
 // initializer emits ABS64 relocations which become zero-based pointers when
 // this raw image is mapped at its actual console RX address.
 IzRecoveryTransport transport;volatile IzRecoveryTransport*bound=&transport;
 bound->listen=listen_fd;bound->accept=accept_fd;bound->receive=receive_fd;bound->send=send_fd;bound->close=close_fd;bound->execute=execute;
 if(!iz_recovery_server_init(&c->server,c->session,c->key,&transport,c)){c->state=4;return c->error=0xe903;}
 c->state=2;c->udp_fd=-1;uint32_t wake=c->wake;uint64_t next_address=0,next_announcement=0;
 while(!__atomic_load_n(&c->stop,__ATOMIC_ACQUIRE)){
  uint32_t step=__atomic_load_n(&c->qualification_stage,__ATOMIC_ACQUIRE);
  if(step){if(step!=c->qualification_done){uint32_t rc=qualify(c,step);if(rc){c->error=rc;break;}__atomic_store_n(&c->qualification_done,step,__ATOMIC_RELEASE);}c->worker_ticks++;NET(c,NET_SLEEP,1000000,0,0,0,0,0);continue;}
  if(c->qualification_done){if(c->server.listener>=0)close_fd(c,c->server.listener);c->server.listener=-1;c->qualification_done=0;}
  if(!c->thread_type)c->thread_type=NET(c,NET_CURRENT,0,0,0,0,0,0);
  uint64_t now=NET(c,NET_TICK,0,0,0,0,0,0)/31250;
  if(now>=next_address){uint32_t status[3]={0};if(!(uint32_t)NET(c,NET_STATUS,status,0,0,0,0,0)){c->network_type=status[0];c->network_status=status[2];}uint32_t ip=0;if((uint32_t)NET(c,NET_ADDRESS,&ip,0,0,0,0,0))ip=0;if(ip!=c->ip){c->ip=ip;c->network_changes++;/* Keep a healthy wildcard listener across adapter changes. */iz_recovery_server_wake(&c->server);if(c->udp_fd>=0)close_fd(c,c->udp_fd);c->udp_fd=-1;next_announcement=0;}next_address=now+1000;}
  uint32_t requested=__atomic_load_n(&c->wake,__ATOMIC_ACQUIRE);if(requested!=wake){wake=requested;iz_recovery_server_wake(&c->server);next_announcement=0;}
  if(now>=next_announcement){announce(c,now);next_announcement=now+2000;}
  iz_recovery_server_tick(&c->server,now);
  // The authenticated host installs a replacement control route before the
  // old BASIC socket closes. Park only the separately identified BASIC thread,
  // never the renderer, this worker, or an unauthenticated connection.
  if(c->foreground_policy==1&&!c->foreground_state&&c->server.connections&&c->foreground_thread&&c->foreground_thread!=c->thread_type&&c->suspend_thread&&c->resume_thread){
   c->foreground_state=1;((IzNetFunction)(uintptr_t)c->suspend_thread)(c->foreground_thread,0,0,0,0,0);c->foreground_state=2;
  }
  c->worker_ticks++;NET(c,NET_SLEEP,1000000,0,0,0,0,0);
 }
 iz_recovery_server_stop(&c->server);if(c->udp_fd>=0)close_fd(c,c->udp_fd);c->udp_fd=-1;
 if(c->foreground_state==2&&c->resume_thread){((IzNetFunction)(uintptr_t)c->resume_thread)(c->foreground_thread,0,0,0,0,0);c->foreground_state=3;}
 memset(c->key,0,32);c->state=c->error?4:3;return c->error;
}
