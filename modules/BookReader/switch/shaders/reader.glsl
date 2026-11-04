const uint READER_MAGIC=0x424f4f4bu;
vec2 readerSize(){return vec2(c.atlas[26],c.atlas[27]);}
vec2 readerPoint(vec2 p){return c.atlas[19]==90u?vec2(p.y,1920.0-p.x):c.atlas[19]==270u?vec2(1080.0-p.y,p.x):p;}
uint readerByte(uint offset){return (c.atlas[offset>>2u]>>((offset&3u)*8u))&255u;}
uint readerFace(){return c.atlas[18]*2u+c.atlas[17];}
uint readerAdvance(uint ch,uint face){return c.atlas[c.atlas[5]+face*96u+clamp(ch,32u,127u)-32u]*c.atlas[16]/48u;}
uint readerPageCount(){return max(1u,c.atlas[11]);}
uint readerPage(){return min(c.game.x,readerPageCount()-1u);}
uint readerMeta(){return c.atlas[9]+readerPage()*3u;}
#ifndef HB_READONLY
void readerRequest(uint op,uint arg){if(c.piece.w!=0)return;c.piece.x=int(op);c.piece.y=c.piece.y>=1000000?1:c.piece.y+1;c.piece.z=int(arg);c.piece.w=1;}
void readerDecode(){
 uint meta=readerMeta(),first=c.atlas[meta],count=min(c.atlas[meta+1u],64u),face=readerFace();
 for(uint row=0u;row<count;row++){
  uint line=c.atlas[8]+(first+row)*3u,offset=c.atlas[line],length=min(c.atlas[line+1u],192u),outIndex=0u,pen=0u;
  c.atlas[c.atlas[24]+row*193u]=0u;
  for(uint i=0u;i<length;i++){
   uint token=readerByte(offset+i),dict=c.atlas[6]+(token-128u)*2u,bytes=token<128u?1u:min(c.atlas[dict+1u],25u),at=token<128u?offset+i:c.atlas[dict];
   for(uint j=0u;j<bytes&&outIndex<192u;j++){
    uint ch=readerByte(at+j);c.atlas[c.atlas[23]+row*192u+outIndex]=ch;pen+=readerAdvance(ch,face);outIndex++;c.atlas[c.atlas[24]+row*193u+outIndex]=pen;
   }
  }
  c.atlas[512u+row]=outIndex;
 }
 c.rows[0]=c.atlas[meta+2u];c.rows[1]=readerPageCount();c.game.w=readerPage()+1u;
}
#endif
