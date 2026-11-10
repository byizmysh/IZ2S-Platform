float calcGlyph(vec2 p,uint ch,float size){
 ivec2 q=ivec2(floor(p/size));
 if(ch<32u||ch>127u||any(lessThan(p,vec2(0)))||q.x>=4||q.y>=7)return 0.0;
 return float((c.atlas[CB+512u+ch-32u]>>uint(q.y*4+q.x))&1u);
}
float calcLabel(vec2 p,uint word,float s){
 int n=word>65535u?3:word>255u?2:1;
 p+=vec2(float(n*5-1)*s,7.0*s)*.5;
 int i=int(floor(p.x/(5.0*s)));if(i<0||i>=n)return 0.0;
 return calcGlyph(p-vec2(float(i)*5.0*s,0),(word>>uint(i*8))&255u,s);
}
