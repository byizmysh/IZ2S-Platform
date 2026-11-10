// Native, collapsible session status. The library keeps its ordinary controls.
const uint RS_STATUS[12]=uint[12](83u,84u,65u,84u,85u,83u,0u,0u,0u,0u,0u,0u);
float rsText(vec2 p,vec2 origin,uint chars[12]){
 float m=0.0;for(uint i=0u;i<12u;i++)if(chars[i]!=0u)m=max(m,focusAscii(p,origin+vec2(float(i)*25.0,0),chars[i]));return m;
}
float rsType(vec2 p,vec2 origin,uint chars[12],float scale){return rsText((p-origin)/scale,vec2(0),chars);}
float rsNumber(vec2 p,vec2 origin,uint n,float scale){
 float m=0.0;for(uint i=0u;i<10u;i++){uint digit=n%10u;n/=10u;m=max(m,focusAscii((p-origin)/scale,vec2(-float(i)*25.0,0),48u+digit));if(n==0u)break;}return m;
}
float rsMetricLabel(vec2 p,vec2 origin,uint index,float scale){
 const uint frames[12]=uint[12](71u,80u,85u,32u,70u,82u,65u,77u,69u,83u,0u,0u);
 const uint inputs[12]=uint[12](73u,78u,80u,85u,84u,32u,69u,86u,69u,78u,84u,83u);
 const uint faults[12]=uint[12](70u,65u,85u,76u,84u,32u,70u,76u,65u,71u,83u,0u);
 const uint restores[12]=uint[12](82u,69u,67u,79u,86u,69u,82u,73u,69u,83u,0u,0u);
 const uint gaps[12]=uint[12](67u,76u,79u,67u,75u,32u,71u,65u,80u,83u,0u,0u);
 const uint waits[12]=uint[12](84u,73u,77u,69u,68u,32u,79u,85u,84u,0u,0u,0u);
 if(index==0u)return rsType(p,origin,frames,scale);if(index==1u)return rsType(p,origin,inputs,scale);
 if(index==2u)return rsType(p,origin,faults,scale);if(index==3u)return rsType(p,origin,restores,scale);
 return index==4u?rsType(p,origin,gaps,scale):rsType(p,origin,waits,scale);
}
uint rsMetricValue(uint index){return index==0u?rs(6u):index==1u?rs(7u):index==2u?rs(4u):index==3u?rs(5u):index==4u?rs(9u):rs(18u);}
float rsStatusHint(vec2 p,vec2 center,float scale){
 float m=line(box(p,center,vec2(18,13),7),.9);
 // Actual glyph ink is 18px high, inside a 32px atlas tile. Center that ink.
 vec2 local=(p-center)/scale;
 m=max(m,focusAscii(local,vec2(-16,-9),76u));m=max(m,focusAscii(local,vec2(0,-9),51u));
 // STATUS begins 1px into its first glyph; compensate for an exact 14px gap.
 return max(m,rsType(p,center+vec2(18.0+14.0-scale,-9.0*scale),RS_STATUS,scale));
}
vec3 rsDraw(vec2 p,vec3 col){
 if(!rsInlineAllowed()||loadProgressVisible())return col;
 if(p.y>=1038.0&&p.y<=1074.0&&p.x>=1048.0&&p.x<=1195.0){
  vec3 ink=rsVisible()?PS_CYAN:PS_MUTED;col=mix(col,ink,rsStatusHint(p,vec2(1070,1056),.62));
 }
 if(!psStatusVisible()||p.y<915.0||p.y>1017.0||p.x<63.0||p.x>1857.0)return col;
 float panel=box(p,vec2(960,966),vec2(896,50),13);col=mix(col,vec3(.06,.21,.49),fill(panel)*.91);col=mix(col,PS_WHITE,line(panel,.6)*.25);
 for(uint i=0u;i<6u;i++){
  float left=64.0+float(i)*(1792.0/6.0),right=left+1792.0/6.0;uint value=rsMetricValue(i);
  if(p.x<left||p.x>right)continue;
  if(i>0u)col=mix(col,PS_WHITE,fill(box(p,vec2(left,966),vec2(.45,30),0))*.17);
  col=mix(col,PS_MUTED,rsMetricLabel(p,vec2(left+18.0,930),i,.48));
  vec3 ink=i>=2u&&value!=0u?vec3(1,.78,.48):PS_CYAN;
  col=mix(col,ink,rsNumber(p,vec2(right-42.0,962),value,.9));
 }
 return col;
}
