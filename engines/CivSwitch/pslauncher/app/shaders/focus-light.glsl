// Analytic lighting and floor; no textures, frame assets or additional pipelines.
const vec3 WARM=vec3(1.0,.94,.72);
float focusEllipse(vec2 p,vec2 center,vec2 radii,float angle){vec2 v=p-center;vec2 q=mat2(cos(angle),-sin(angle),sin(angle),cos(angle))*v;float k=length(q/radii),g=length(q/(radii*radii))/max(k,.001);return (k-1.0)/max(g,.001);}
vec3 focusBackground(vec2 p){
 vec2 uv=(p-vec2(920,450))/vec2(1100,650);float grain=fract(52.9829189*fract(dot(floor(p),vec2(.06711056,.00583715))));
 vec3 col=vec3(.041,.046,.046)+vec3(.012,.013,.010)*max(0.0,1.0-dot(uv,uv));col+=(grain-.5)*.004;
 float d=focusEllipse(p,FOCUS_ORBIT_CENTER,FOCUS_ORBIT_RADII,FOCUS_ORBIT_ANGLE),right=smoothstep(1040,1770,p.x),lower=smoothstep(310,610,p.y);
 float bright=.14+.75*right+.14*lower;col+=WARM*exp(-abs(d)/8.0)*(.008+.045*right);col=mix(col,mix(vec3(.32,.30,.19),WARM,right),line(d,.85)*bright);
 float secondary=focusEllipse(p,vec2(640,418),vec2(1190,222),.045);col=mix(col,vec3(.21,.22,.19),line(secondary,1.1)*.28);
 if(p.x>1400.0&&p.y>400.0){float dotted=focusEllipse(p,vec2(640,400),vec2(1148,205),.045),dash=step(.95,fract((p.x+p.y*1.4)*.045));col=mix(col,BLUE,line(dotted,.5)*dash*.6);}
 float front=p.y-focusRailY(p.x);if(p.y>610.0){col+=WARM*exp(-abs(front)/9.0)*.055;col=mix(col,WARM,line(front,1.0)*.83);col+=WARM*exp(-abs(front)/2.0)*.05;float inner=front+17.0;col=mix(col,vec3(.34,.34,.25),line(inner,.6)*.16);}
 // The small planet gives the orbit its far end and a directional metal highlight.
 vec2 q=(p-FOCUS_PLANET)/42.0;float r=length(q),a=1.0-smoothstep(.985,1.015,r);
 if(r<1.015){float z=sqrt(max(0.0,1.0-dot(q,q)));vec3 n=normalize(vec3(q,z)),l=normalize(vec3(.55,-.7,.4));float lit=max(0.0,dot(n,l)),rim=pow(1.0-z,3.0),spec=pow(max(0.0,dot(reflect(-l,n),vec3(0,0,1))),22.0);vec3 ball=vec3(.023,.027,.027)+WARM*(lit*.14+rim*.075+spec*.35);ball+=WARM*pow(max(0.0,dot(n,normalize(vec3(.8,-.5,.12)))),12.0)*.42;col=mix(col,ball,a);col=mix(col,WARM,line((r-1.0)*42.0,.6)*.3);}
 return col;
}
vec3 focusStation(vec2 p,vec3 col,float x,bool selected,float clip){
 float y=focusRailY(x),dy=p.y-y,dx=p.x-x;vec3 light=selected?BLUE:WARM;
 vec2 e=vec2(dx/19.0,dy/4.4);float d=(length(e)-1.0)*4.4;
 col+=light*exp(-dot(vec2(dx/29.0,dy/12.0),vec2(dx/29.0,dy/12.0)))*.22*clip;
 col=mix(col,light,line(d,.65)*.85*clip);
 if(dy>0.0&&dy<145.0){float fade=exp(-dy/64.0)*(1.0-smoothstep(110.0,145.0,dy)),spread=92.0+dy*.12;float beam=exp(-dx*dx/(spread*spread))*(selected?.24:.022);col+=light*beam*fade*clip;float core=exp(-dx*dx/(8.0+dy*.42));col+=light*core*exp(-dy/46.0)*.12*clip;}
 return col;
}
