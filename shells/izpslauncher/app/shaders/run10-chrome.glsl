// Run10 visual identity: analytic mouse, stationary crumbs and original controls.
float polygon(vec2 p,vec2 v[24],int n){
 float d=dot(p-v[0],p-v[0]),s=1.0;
 for(int i=0,j=n-1;i<n;j=i,i++){
  vec2 e=v[j]-v[i],w=p-v[i];vec2 b=w-e*clamp(dot(w,e)/dot(e,e),0.0,1.0);d=min(d,dot(b,b));
  bvec3 test=bvec3(p.y>=v[i].y,p.y<v[j].y,e.x*w.y>e.y*w.x);
  if(all(test)||all(not(test)))s=-s;
 }
 return s*sqrt(d);
}
// Outline sampled from the site's iz-mouse SVG. No texture or sprite frames.
float mouse(vec2 q,float t){
 vec2 v[24]=vec2[24](vec2(69,103),vec2(72,91),vec2(78,80),vec2(86,71),vec2(99,63),vec2(113,60),
  vec2(124,56),vec2(135,56),vec2(146,59),vec2(155,63),vec2(162,68),vec2(181,84),
  vec2(173,92),vec2(163,98),vec2(152,104),vec2(139,108),vec2(124,110),vec2(99,110),
  vec2(85,110),vec2(73,110),vec2(70,109),vec2(69,107),vec2(68.8,105),vec2(69,103.2));
 float body=min(polygon(q,v,24),length(q-vec2(119,48))-25.0);
 float m=fill(body);
 m*=1.0-fill(length(q-vec2(156,80))-3.7);
 float ear=1e4;
 vec2 prev=vec2(108,49);
 for(int i=1;i<=6;i++){
  float u=float(i)/6.0;vec2 next=pow(1.0-u,3.0)*vec2(108,49)+3.0*pow(1.0-u,2.0)*u*vec2(108,39)+3.0*(1.0-u)*u*u*vec2(119,35)+u*u*u*vec2(128,41);
  ear=min(ear,stroke(q,prev,next));prev=next;
 }
 m*=1.0-fill(ear-2.25);
 float wiggle=2.8*sin(t*8.72664626);
 float tail=min(stroke(q,vec2(12,91+wiggle),vec2(36,91+wiggle)),stroke(q,vec2(36,91+wiggle),vec2(44,96)));
 tail=min(tail,stroke(q,vec2(44,96),vec2(44,105)));tail=min(tail,stroke(q,vec2(44,105),vec2(51,110)));
 tail=min(tail,stroke(q,vec2(51,110),vec2(76,110)));
 m=max(m,fill(tail-3.0));m=max(m,fill(length(q-vec2(12,91+wiggle))-5.0));
 // The tilted little crown and dark chevron from the reference author card.
 vec2 crown[24];crown[0]=vec2(130,56);crown[1]=vec2(150,63);crown[2]=vec2(171,61);crown[3]=vec2(162,56);crown[4]=vec2(162,35);crown[5]=vec2(154,39);crown[6]=vec2(145,34);crown[7]=vec2(140,51);
 float hat=polygon(q,crown,8);m=m*(1.0-fill(hat))+line(hat,1.1);
 float chevron=min(stroke(q,vec2(145,72),vec2(132,66)),stroke(q,vec2(132,66),vec2(139,88)));
 m*=1.0-fill(chevron-3.1);
 return clamp(m,0.0,1.0);
}

void menuHeader(vec2 p,inout vec3 col){
 float t=c.motion.x;col=mix(col,INK,focusText(p,vec2(80,65),F_TITLE,1.0));
 // Right aligned below the title; italic shear uses the same lime as the orbit.
 col=mix(col,BLUE,focusText(p,vec2(80.0+float(F_TITLE.z-F_VERSION.z),65.0+float(F_TITLE.w)+10.0),F_VERSION,1.0));

 // A full out-and-back walk takes 24 seconds. Ease at the planet and turn.
 float phase=mod(t,24.0)*.261799388,velocity=sin(phase),direction=velocity>=0.0?1.0:-1.0;
 float angle=mix(-1.0,-.29,.5-.5*cos(phase));vec2 tangent=focusOrbitTangent(angle),normal=vec2(-tangent.y,tangent.x);
 vec2 feet=focusOrbitPoint(angle);float pace=min(1.0,abs(velocity)*4.0),bob=2.0*sin(t*10.0)*pace;
 for(int i=0;i<13;i++){float a=mix(-.98,-.28,float(i)/12.0);vec2 at=focusOrbitPoint(a),n=focusOrbitTangent(a);at+=vec2(-n.y,n.x)*(mod(float(i)*7.0,5.0)-2.0);float radius=2.6+mod(float(i)*3.0,4.0)*.5;col=mix(col,BLUE,fill(length(p-at)-radius)*.9);}
 vec2 delta=p-feet-normal*bob;vec2 q=vec2(dot(delta,tangent)*direction,dot(delta,normal))/.8+vec2(116,110);
 if(q.x>=5.0&&q.x<=194.0&&q.y>=20.0&&q.y<=115.0)col=mix(col,BLUE,mouse(q,t));
}
void menuFooter(vec2 p,inout vec3 col){
 if(p.y<970.0||p.y>1040.0)return;
 col=mix(col,MUTED,focusText(p,vec2(1840.0-float(F_CREDIT.z),996),F_CREDIT,1));
 if(c.rows[1]!=0u||c.bagMeta.y!=0u)return;
 bool picker=c.piece.x!=0;uint count=focusCount(picker);bool appSelected=!picker&&c.state.x<min(c.library.x,15u)&&focusOrdinal(c.state.x,false)<count;
 bool showA=count>0u&&(picker||c.library.x>0u),showB=picker,showX=!picker&&(!showA||appSelected);
 float cursor=95.0;vec2 a=vec2(cursor,1008),b,x;
 if(showA){ivec4 caption=picker?F_INSTALL:appSelected?F_OPEN:F_ADDSMALL;col=mix(col,INK,line(length(p-a)-19.0,1.4));col=mix(col,INK,text(p,a-vec2(TEXT_BUTTON_A.zw)*.5,TEXT_BUTTON_A));col=mix(col,INK,focusText(p,a+vec2(35,-float(caption.w)*.5),caption,1));cursor+=35.0+float(caption.z)+48.0;}
 b=vec2(cursor,1008);
 if(showB){col=mix(col,INK,line(length(p-b)-19.0,1.4));col=mix(col,INK,text(p,b-vec2(TEXT_BUTTON_B.zw)*.5,TEXT_BUTTON_B));col=mix(col,INK,focusText(p,b+vec2(35,-float(F_BACK.w)*.5),F_BACK,1));cursor+=35.0+float(F_BACK.z)+48.0;}
 x=vec2(cursor,1008);
 if(showX){col=mix(col,INK,line(length(p-x)-19.0,1.4));col=mix(col,INK,focusText(p,x-vec2(F_X.zw)*.5,F_X,1));col=mix(col,INK,focusText(p,x+vec2(35,-float(F_ADDSMALL.w)*.5),F_ADDSMALL,1));}
}
