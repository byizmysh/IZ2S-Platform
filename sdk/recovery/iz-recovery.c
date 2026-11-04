/* Application recovery protocol with an optional POSIX stream adapter.
 * IZR_NO_POSIX builds the same state machine for an ordinary native owner.
 * No bootstrap, native dispatcher, platform address lookup or injection. */
#include "iz-recovery.h"
#include <string.h>
#ifndef IZR_NO_POSIX
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
static int posix_open(void *context,uint32_t ipv4,uint16_t port){
 (void)context;int fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0)return-1;
 int flags=fcntl(fd,F_GETFL,0);if(flags<0||fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0){close(fd);return-1;}
 struct sockaddr_in a;memset(&a,0,sizeof a);a.sin_family=AF_INET;a.sin_port=htons(port);a.sin_addr.s_addr=ipv4;
 if(connect(fd,(struct sockaddr *)&a,sizeof a)<0&&errno!=EINPROGRESS){close(fd);return-1;}return fd;
}
static int posix_connected(void *context,int fd){
 (void)context;struct pollfd p={fd,POLLOUT,0};int r=poll(&p,1,0);if(r<0)return errno==EINTR?0:-1;if(r==0)return 0;
 int err=0;socklen_t len=sizeof err;return getsockopt(fd,SOL_SOCKET,SO_ERROR,&err,&len)<0||err?-1:1;
}
static ptrdiff_t posix_result(ssize_t n){return n>=0?(ptrdiff_t)n:(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR?IZR_IO_AGAIN:IZR_IO_FAILED);}
static ptrdiff_t posix_send(void *context,int fd,const unsigned char *p,size_t n){(void)context;return posix_result(send(fd,p,n,MSG_NOSIGNAL));}
static ptrdiff_t posix_receive(void *context,int fd,unsigned char *p,size_t n){(void)context;return posix_result(recv(fd,p,n,0));}
static void posix_close(void *context,int fd){(void)context;close(fd);}
static const struct izr_transport posix_transport={posix_open,posix_connected,posix_send,posix_receive,posix_close};
#endif
#define MAGIC 0x31525a49u
#define MAX_SAFE 9007199254740991ULL
#define SLOT 2048
/* Small SHA-256 implementation used only for checksums and HMAC. */
struct sha {uint32_t h[8];uint64_t bits;unsigned char block[64];size_t used;};
static const uint32_t k[64]={
 0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
 0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
 0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
 0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
 0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
 0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
 0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
 0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
static uint32_t rr(uint32_t n,unsigned r){return(n>>r)|(n<<(32-r));}
static void compress(struct sha *s){
 uint32_t w[64];for(unsigned i=0;i<16;i++){const unsigned char *b=s->block+i*4;w[i]=(uint32_t)b[0]<<24|(uint32_t)b[1]<<16|(uint32_t)b[2]<<8|b[3];}
 for(unsigned i=16;i<64;i++){uint32_t a=w[i-15],b=w[i-2];w[i]=w[i-16]+(rr(a,7)^rr(a,18)^(a>>3))+w[i-7]+(rr(b,17)^rr(b,19)^(b>>10));}
 uint32_t a=s->h[0],b=s->h[1],c=s->h[2],d=s->h[3],e=s->h[4],f=s->h[5],g=s->h[6],h=s->h[7];
 for(unsigned i=0;i<64;i++){uint32_t t=h+(rr(e,6)^rr(e,11)^rr(e,25))+((e&f)^(~e&g))+k[i]+w[i];uint32_t u=(rr(a,2)^rr(a,13)^rr(a,22))+((a&b)^(a&c)^(b&c));h=g;g=f;f=e;e=d+t;d=c;c=b;b=a;a=t+u;}
 s->h[0]+=a;s->h[1]+=b;s->h[2]+=c;s->h[3]+=d;s->h[4]+=e;s->h[5]+=f;s->h[6]+=g;s->h[7]+=h;
}
static void sha_init(struct sha *s){const uint32_t h[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};memset(s,0,sizeof *s);memcpy(s->h,h,sizeof h);}
static void sha_add(struct sha *s,const void *data,size_t n){const unsigned char *p=data;s->bits+=(uint64_t)n*8;while(n){size_t a=64-s->used;if(a>n)a=n;memcpy(s->block+s->used,p,a);s->used+=a;p+=a;n-=a;if(s->used==64){compress(s);s->used=0;}}}
static void sha_end(struct sha *s,unsigned char out[32]){uint64_t bits=s->bits;unsigned char one=0x80,zero=0;sha_add(s,&one,1);while(s->used!=56)sha_add(s,&zero,1);unsigned char b[8];for(unsigned i=0;i<8;i++)b[7-i]=(unsigned char)(bits>>(8*i));sha_add(s,b,8);for(unsigned i=0;i<8;i++)for(unsigned j=0;j<4;j++)out[i*4+j]=(unsigned char)(s->h[i]>>(24-j*8));}
static void hmac(const struct izr_client *c,const char *context,const unsigned char *data,size_t n,unsigned char out[32]){
 unsigned char pad[64],inner[32];struct sha s;
 memset(pad,0x36,sizeof pad);for(unsigned i=0;i<32;i++)pad[i]^=c->key[i];sha_init(&s);sha_add(&s,pad,64);sha_add(&s,context,strlen(context));sha_add(&s,c->nonce,32);sha_add(&s,data,n);sha_end(&s,inner);
 memset(pad,0x5c,sizeof pad);for(unsigned i=0;i<32;i++)pad[i]^=c->key[i];sha_init(&s);sha_add(&s,pad,64);sha_add(&s,inner,32);sha_end(&s,out);memset(pad,0,sizeof pad);memset(inner,0,sizeof inner);
}
static void put16(unsigned char *p,uint16_t v){p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);}
static void put32(unsigned char *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));}
static void put64(unsigned char *p,uint64_t v){for(unsigned i=0;i<8;i++)p[i]=(unsigned char)(v>>(8*i));}
static uint16_t get16(const unsigned char *p){return(uint16_t)((uint16_t)p[0]|(uint16_t)p[1]<<8);}
static uint32_t get32(const unsigned char *p){return(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t get64(const unsigned char *p){uint64_t v=0;for(unsigned i=0;i<8;i++)v|=(uint64_t)p[i]<<(8*i);return v;}
static int commit(struct izr_client *c,const struct izr_status *s){
 if(c->sequence>=MAX_SAFE||s->heartbeat>MAX_SAFE||s->completed>MAX_SAFE||s->pending>MAX_SAFE||s->used_bytes>MAX_SAFE||s->app_count>15||s->operation>3||(s->operation==0)!=(s->pending==0)||(s->pending&&s->pending<=s->completed))return-1;
 size_t length=0;while(length<64&&s->active[length]){unsigned char x=(unsigned char)s->active[length];if(!((x>='a'&&x<='z')||(x>='A'&&x<='Z')||(x>='0'&&x<='9')||x=='-'||x=='_'||x=='.'))return-1;length++;}if(length==64)return-1;
 unsigned char b[SLOT];memset(b,0,sizeof b);uint64_t seq=c->sequence+1;
 put32(b,MAGIC);put16(b+4,1);put16(b+6,64);put64(b+8,seq);put32(b+16,256);
 memcpy(b+64,c->session,16);memcpy(b+80,c->process_id,16);put64(b+96,s->heartbeat);put64(b+104,s->completed);put64(b+112,s->pending);put32(b+120,s->operation);memcpy(b+124,s->active,length);put32(b+188,s->catalog_revision);memcpy(b+192,s->catalog_hash,32);put64(b+224,s->used_bytes);put32(b+232,s->app_count);
 struct sha hash;sha_init(&hash);sha_add(&hash,b,24);sha_add(&hash,b+56,SLOT-56);sha_end(&hash,b+24);
 memcpy(c->passport+(seq%2)*SLOT,b,SLOT);c->sequence=seq;return 0;
}
static void dropped(struct izr_client *c,uint64_t now){if(c->fd>=0)c->transport.close(c->transport_context,c->fd);c->fd=-1;c->phase=0;c->authenticated=0;c->have=c->sent=0;c->retry_at=now+c->retry_ms+(c->process_id[0]%53);if(c->retry_ms<8000)c->retry_ms*=2;if(c->retry_ms>8000)c->retry_ms=8000;}
int izr_init_transport(struct izr_client *c,uint32_t ipv4,uint16_t port,const unsigned char key[32],const unsigned char session[16],const unsigned char process_id[16],const struct izr_transport *transport,void *context){
 if(!c||!port||!key||!session||!process_id||!transport||!transport->open||!transport->connected||!transport->send||!transport->receive||!transport->close)return-1;
 struct izr_transport owned=*transport;
 memset(c,0,sizeof *c);c->ipv4=ipv4;c->port=port;c->fd=-1;c->retry_ms=250;c->transport=owned;c->transport_context=context;memcpy(c->key,key,32);memcpy(c->session,session,16);memcpy(c->process_id,process_id,16);return 0;
}
#ifndef IZR_NO_POSIX
int izr_init(struct izr_client *c,uint32_t ipv4,uint16_t port,const unsigned char key[32],const unsigned char session[16],const unsigned char process_id[16]){return izr_init_transport(c,ipv4,port,key,session,process_id,&posix_transport,0);}
#endif
void izr_stop(struct izr_client *c){if(!c)return;if(c->fd>=0)c->transport.close(c->transport_context,c->fd);c->fd=-1;c->phase=-1;c->authenticated=0;memset(c->key,0,32);}
int izr_tick(struct izr_client *c,uint64_t now,const struct izr_status *status){
 if(!c||!status||c->phase<0)return-1;
 if(c->phase==0){
  if(now<c->retry_at)return 0;
  c->fd=c->transport.open(c->transport_context,c->ipv4,c->port);if(c->fd<0){dropped(c,now);return 0;}
  c->phase=1;c->deadline=now+5000;return 0;
 }
 if(now>=c->deadline){dropped(c,now);return 0;}
 if(c->phase==1){
  int r=c->transport.connected(c->transport_context,c->fd);if(r<0){dropped(c,now);return 0;}if(r==0)return 0;
  c->phase=2;c->have=0;c->want=8;
 }
 if(c->phase==3){
  ptrdiff_t n=c->transport.send(c->transport_context,c->fd,c->wire+c->sent,c->want-c->sent);
  if(n==IZR_IO_AGAIN)return 0;
  if(n<=0||(size_t)n>c->want-c->sent){dropped(c,now);return 0;}
  c->sent+=(size_t)n;if(c->sent<c->want)return 0;c->phase=4;c->have=0;c->want=8;return 0;
 }
 if(c->phase==2||c->phase==4){
  ptrdiff_t n=c->transport.receive(c->transport_context,c->fd,c->wire+c->have,c->want-c->have);
  if(n==IZR_IO_AGAIN)return 0;
  if(n<=0||(size_t)n>c->want-c->have){dropped(c,now);return 0;}
  c->have+=(size_t)n;if(c->have<c->want)return 0;
  if(c->want==8){uint16_t kind=get16(c->wire+4),size=get16(c->wire+6);if(get32(c->wire)!=MAGIC||kind!=(c->phase==2?1:3)||size!=(c->phase==2?32:40)){dropped(c,now);return 0;}c->want=8+size;return 0;}
  if(c->phase==2){
   memcpy(c->nonce,c->wire+8,32);if(commit(c,status)<0){dropped(c,now);return-1;}
   put32(c->wire,MAGIC);put16(c->wire+4,2);put16(c->wire+6,4096+32);memcpy(c->wire+8,c->passport,4096);hmac(c,"RUN18 status proof v1",c->passport,4096,c->wire+8+4096);c->sent=0;c->want=IZR_WIRE_BYTES;c->phase=3;c->deadline=now+5000;
  }else{
   unsigned char digest[32];hmac(c,"RUN18 status ack v1",c->wire+8,8,digest);unsigned diff=0;for(unsigned i=0;i<32;i++)diff|=digest[i]^c->wire[16+i];
   if(diff||get64(c->wire+8)!=c->sequence){dropped(c,now);return 0;}
   c->authenticated=1;c->retry_ms=250;c->phase=2;c->have=0;c->want=8;c->deadline=now+5000;return 1;
  }
 }
 return 0;
}
