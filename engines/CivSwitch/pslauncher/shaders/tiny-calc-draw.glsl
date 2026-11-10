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
vec3 calcDraw(vec2 p){
 vec3 ink=vec3(.12,.20,.28),col=BG;
 float d=box(p,vec2(960,540),vec2(368,476),10);
 col=mix(col,vec3(.02),fill(box(p,vec2(967,548),vec2(373,480),12))*.65);
 col=mix(col,vec3(.84,.88,.93),fill(d));col=mix(col,vec3(.57,.64,.72),line(d,1.2));
 if(p.x<600.0||p.x>1320.0||p.y<72.0||p.y>1008.0)return col;
 float title=box(p,vec2(960,96),vec2(359,23),4);
 col=mix(col,mix(vec3(.96),vec3(.80,.85,.90),clamp((p.y-73.0)/46.0,0.0,1.0)),fill(title));
 col=mix(col,ink,calcLabel(p-vec2(672,96),0x504f50u,4));
 col=mix(col,ink,calcLabel(p-vec2(758,96),0x4c4143u,4));
 col=mix(col,ink,calcGlyph(p-vec2(796,82),67u,4));
 col=mix(col,vec3(.76,.30,.25),fill(box(p,vec2(1272,96),vec2(28,20),3)));
 col=mix(col,vec3(1),calcLabel(p-vec2(1272,96),88u,3));
 float display=box(p,vec2(960,216),vec2(336,64),2);
 col=mix(col,mix(vec3(.89,.93,.99),vec3(1),clamp((p.y-152.0)/128.0,0.0,1.0)),fill(display));
 col=mix(col,vec3(.54,.61,.69),line(display,1));
 uint base=CB+320u;
 if(c.atlas[base+8u]!=0u)col=mix(col,ink,calcGlyph(p-vec2(640,164),77u,2));
 col=mix(col,ink,calcLabel(p-vec2(1270,168),c.atlas[base+9u],2));
 int n=0;for(int i=0;i<24;i++){uint ch=(c.atlas[base+uint(i)/4u]>>uint((i%4)*8))&255u;if(ch==0u)break;n++;}
 float size=min(9.0,608.0/max(1.0,float(n)*5.0));vec2 q=p-vec2(1272.0-float(n*5-1)*size,252.0-7.0*size);
 int at=int(floor(q.x/(5.0*size)));
 if(at>=0&&at<n){uint ch=(c.atlas[base+uint(at)/4u]>>uint((at%4)*8))&255u;col=mix(col,ink,calcGlyph(q-vec2(float(at)*5.0*size,0),ch,size));}
 int id=calcCell(p);
 if(id>=0){
  vec4 r=calcRect(id);float edge=box(p,r.xy,r.zw,4),g=clamp((p.y-r.y+r.w)/(r.w*2.0),0.0,1.0);
  bool focus=c.atlas[CB+4u]==uint(id),down=focus&&c.atlas[CB+3u]>0u;
  vec3 color=down?mix(vec3(.85,.93,1),vec3(.61,.76,.89),g):mix(vec3(.98,.99,1),vec3(.77,.83,.90),g);
  col=mix(col,color,fill(edge));col=mix(col,focus?vec3(.65,.48,.17):vec3(.57,.64,.72),line(edge,.9));
  if(edge<0.0){float label;
   if(id==5)label=fill(min(stroke(p-r.xy,vec2(20,0),vec2(-20,0)),min(stroke(p-r.xy,vec2(-20,0),vec2(-8,-11)),stroke(p-r.xy,vec2(-20,0),vec2(-8,11))))-2.0);
   else if(id==9){vec2 v=p-r.xy;label=fill(min(stroke(v,vec2(-22,0),vec2(-14,0)),min(stroke(v,vec2(-14,0),vec2(-6,14)),min(stroke(v,vec2(-6,14),vec2(6,-15)),stroke(v,vec2(6,-15),vec2(23,-15)))))-2.0);}
   else label=calcLabel(p-r.xy,CALC_LABEL[id],id<10||id==19?4.0:6.0);
   col=mix(col,ink,label);
  }
 }
 col=mix(col,ink,calcLabel(p-vec2(656,966),0x434142u,3));
 col=mix(col,ink,calcGlyph(p-vec2(680,955.5),75u,3));
 return col;
}
