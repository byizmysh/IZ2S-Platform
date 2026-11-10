/* SHA-256 core reused from the local portable application-status client.
 * This protocol has its own domain and never treats telemetry as native RPC. */
#include "crypto.h"
#include "memory.h"
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

void iz_recovery_sha256(const void *data,size_t bytes,unsigned char out[32]){struct sha s;sha_init(&s);sha_add(&s,data,bytes);sha_end(&s,out);}
void iz_recovery_mac(const unsigned char key[32],const unsigned char nonce[32],const unsigned char header[128],const void *data,size_t bytes,unsigned char out[32]){
 static const char domain[]="IZ2S native control v3";
 unsigned char pad[64],inner[32];struct sha s;
 memset(pad,0x36,64);for(unsigned i=0;i<32;i++)pad[i]^=key[i];sha_init(&s);sha_add(&s,pad,64);sha_add(&s,domain,sizeof domain);sha_add(&s,nonce,32);sha_add(&s,header,80);sha_add(&s,header+112,16);sha_add(&s,data,bytes);sha_end(&s,inner);
 memset(pad,0x5c,64);for(unsigned i=0;i<32;i++)pad[i]^=key[i];sha_init(&s);sha_add(&s,pad,64);sha_add(&s,inner,32);sha_end(&s,out);memset(pad,0,64);memset(inner,0,32);
}
