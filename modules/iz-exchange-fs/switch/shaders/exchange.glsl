const uint FX=(0x7f8000u-1024u)/4u;
uint fx(uint offset){return c.atlas[FX+offset/4u];}
bool fxReady(){return fx(64u)==0x495a4658u;}
bool fxBusy(){return !fxReady()||fx(0u)!=fx(68u)||fx(72u)==3u;}
uint fxCount(){return fxReady()?min(12u,fx(76u)):0u;}
#ifndef HB_READONLY
void fxRequest(uint op,uint arg){if(fxBusy())return;c.atlas[FX+1u]=op;c.atlas[FX+2u]=arg;uint seq=fx(0u)+1u;c.atlas[FX]=seq==0u?1u:seq;}
#endif
