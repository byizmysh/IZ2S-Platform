// Caller supplies izPadRead/izPadWrite over its owned binding-zero input.
// Preserve the legacy five-source mailbox; the fixed 8-player sweep is separate.
void izNormalizePad(){
 const uint e=0x7f0000u/4u,h=e+12272u/4u;
 if(izPadRead(e)!=0x48425344u||izPadRead(e+1u)!=1u||(izPadRead(e+3u)&64u)==0u)return;
 bool sweep=izPadRead(h)==0x495a5044u&&izPadRead(h+1u)==1u&&izPadRead(h+2u)==32u;
 uint buttons=0u,upper=0u,attributes=0u;ivec4 axes=ivec4(0);
 // In a current session read handheld plus every player/style. For older
 // five-source sessions use only connected samples, never a stale primary pad.
 uint count=sweep?33u:5u;
 for(uint i=0u;i<count;i++){
  uint p=sweep?(i==0u?e+160u:e+3072u+(i-1u)*16u):e+160u+i*16u;
  uint flags=izPadRead(p+8u);if((flags&1u)==0u)continue;
  buttons|=izPadRead(p+2u);upper|=izPadRead(p+3u);attributes|=flags;
  for(uint j=0u;j<4u;j++){int value=clamp(int(izPadRead(p+4u+j)),-32767,32767);if(abs(value)>abs(axes[j]))axes[j]=value;}
 }
 izPadWrite(2u,buttons);izPadWrite(3u,upper);for(uint j=0u;j<4u;j++)izPadWrite(4u+j,uint(axes[j]));izPadWrite(8u,attributes);
}
