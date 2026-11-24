// A small mailbox in the unused gap before the SDK extension. ABI 1 is intact.
const uint CB=(0x700000u-1024u)/4u,CMAGIC=0x495a4335u;
const uint CALC_LABEL[28]=uint[28](0x434du,0x524du,0x534du,0x2b4du,0x2d4du,
 60u,0x4543u,67u,126u,118u,55u,56u,57u,47u,37u,52u,53u,54u,120u,0x782f31u,
 49u,50u,51u,45u,61u,48u,46u,43u);
vec4 calcRect(int id){
 int col=id%5,row=id/5;vec2 h=vec2(64,44);
 if(id==25){col=0;row=5;h.x=132.0;}
 if(id==26){col=2;row=5;}if(id==27){col=3;row=5;}
 vec2 p=vec2(688+col*136,364+row*100);
 if(id==25)p.x+=68.0;if(id==24){p.y+=50.0;h.y=94.0;}
 return vec4(p,h);
}
int calcCell(vec2 p){
 ivec2 q=ivec2(floor((p-vec2(620,314))/vec2(136,100)));
 if(any(lessThan(q,ivec2(0)))||q.x>4||q.y>5)return -1;
 if(q.y==5)return q.x<2?25:q.x==2?26:q.x==3?27:24;
 return q.y*5+q.x;
}
int calcHit(vec2 p){int k=calcCell(p);if(k<0)return -1;vec4 r=calcRect(k);return all(lessThanEqual(abs(p-r.xy),r.zw))?k:-1;}
