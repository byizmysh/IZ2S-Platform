float psActivityPixel(ivec2 q){
 if(any(lessThan(q,ivec2(0)))||q.x>=1024||q.y>=512)return 0.0;
 uint n=uint(q.y*1024+q.x);return float((c.atlas[PS_ACTIVITY_TEXT+(n>>4u)]>>((n&15u)*2u))&3u)/3.0;
}
float psActivityText(vec2 p,vec2 origin,uint row,float top,float bottom){
 vec2 q=p-origin;if(q.y<top||q.y>=bottom||q.x<0.0||q.x>=788.0)return 0.0;
 q.y+=float(row)*64.0;ivec2 at=ivec2(floor(q));vec2 f=fract(q);
 return mix(mix(psActivityPixel(at),psActivityPixel(at+ivec2(1,0)),f.x),mix(psActivityPixel(at+ivec2(0,1)),psActivityPixel(at+ivec2(1)),f.x),f.y);
}
void psActivityPanel(vec2 p,inout vec3 col){
 uint count=psActivityCount();float height=psActivityHeight();
 psLabel(p,col,vec2(PS_LEFT,688),PS_ACTIVITY,PS_MUTED);
 psPanel(p,col,vec2(740,PS_BODY_TOP+height*.5),vec2(460,height*.5),16,.20);
 if(loadProgressVisible()){loadProgressDraw(p,col,PS_PROGRESS_ORIGIN,PS_PROGRESS_SIZE);return;}
 if(count==0u){
  vec2 at=vec2(332,760);col=mix(col,PS_MUTED,line(length(p-at)-10.0,.9)*.7);
  float hand=min(stroke(p,at,at+vec2(0,-5)),stroke(p,at,at+vec2(4,2)));col=mix(col,PS_MUTED,fill(hand-.8)*.7);
  psLabel(p,col,vec2(360,750),PS_NO_ACTIVITY,PS_MUTED);return;
 }
 for(uint i=0u;i<count;i++){
  float y=PS_BODY_TOP+16.0+float(i)*96.0;uint index=c.atlas[PS_ACTIVITY_BASE+4u+i*4u];
  if(i>0u)col=mix(col,PS_WHITE,fill(box(p,vec2(740,y-8.0),vec2(428,.4),0))*.10);
  if(index<min(c.library.x,15u)){vec4 icon=focusIcon(p,vec2(334,y+32),slotBase(index),60);col=mix(col,icon.rgb,icon.a);}
  else {col=mix(col,PS_LIME,line(length(p-vec2(334,y+32))-15.0,1));col=mix(col,PS_LIME,fill(stroke(p,vec2(328,y+32),vec2(340,y+32))-1));}
  vec2 textOrigin=vec2(380,y+8.0);col=mix(col,PS_WHITE,psActivityText(p,textOrigin,i,0.0,26.0));col=mix(col,PS_MUTED,psActivityText(p,textOrigin,i,26.0,57.0));
 }
}
