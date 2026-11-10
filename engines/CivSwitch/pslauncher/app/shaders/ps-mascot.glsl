// Layered native IZ mouse: the original silhouette, now shaded and articulated.
// All details are analytic; no sprite upload or new console ABI fields.
float psMouseOval(vec2 p,vec2 center,vec2 radii){return (length((p-center)/radii)-1.0)*min(radii.x,radii.y);}
vec2 psMouseTail(float u,float t){
 float v=1.0-u;vec2 q=v*v*v*vec2(75,105)+3.0*v*v*u*vec2(31,124)+3.0*v*u*u*vec2(55,82)+u*u*u*vec2(12,93);
 q.y+=sin(t*4.6+u*4.0)*2.0*u;return q;
}
vec3 psMouseDraw(vec2 q,vec3 col,float t){
 vec3 edge=vec3(.13,.24,.10),cream=vec3(.93,.96,.68),furDark=vec3(.28,.43,.10),furLight=vec3(.78,.95,.31);
 float shade=psMouseOval(q,vec2(117,113),vec2(43,3));col=mix(col,vec3(.015,.065,.17),fill(shade)*.22);
 // A supple shaded tail follows the gait, rather than the old angular line.
 float tail=1000.0;vec2 previous=psMouseTail(0,t);
 for(int i=1;i<=14;i++){vec2 next=psMouseTail(float(i)/14.0,t);tail=min(tail,stroke(q,previous,next));previous=next;}
 col=mix(col,edge,fill(tail-3.0));col=mix(col,vec3(.69,.84,.31),fill(tail-1.7));
 float tip=length(q-psMouseTail(1,t));col=mix(col,vec3(.82,.93,.47),fill(tip-2.2));
 // Alternating paws remain readable at the actual header size.
 for(int i=0;i<3;i++){float phase=t*9.5+float(i)*2.4;vec2 foot=vec2(86.0+float(i)*27.0+sin(phase)*2.5,109.0-max(0.0,cos(phase))*2.2);float paw=psMouseOval(q,foot,vec2(8,3.6));col=mix(col,edge,fill(paw-.7));col=mix(col,cream,fill(paw+.4));}
 // The large original ear, with a warm inner fold and a lit rim.
 float ear=length(q-vec2(119,48))-25.0;
 vec3 earFur=mix(furDark,furLight,clamp((65.0-q.y)/43.0,0,1));col=mix(col,earFur,fill(ear));col=mix(col,vec3(.84,.97,.50),line(ear,1.0)*.65);
 float inner=psMouseOval(q,vec2(119,47),vec2(14,17));vec3 inside=mix(vec3(.66,.43,.23),vec3(.98,.81,.49),clamp((61.0-q.y)/31.0,0,1));col=mix(col,inside,fill(inner));
 col=mix(col,vec3(.48,.38,.18),fill(stroke(q,vec2(112,51),vec2(117,42))-1.1)*.55);
 col=mix(col,vec3(1,.94,.70),fill(stroke(q,vec2(113,37),vec2(120,35))-.8)*.7);
 // Preserve the recognizable IZ back, head and pointed muzzle.
 vec2 v[24]=vec2[24](vec2(69,103),vec2(72,91),vec2(78,80),vec2(86,71),vec2(99,63),vec2(113,60),
  vec2(124,56),vec2(135,56),vec2(146,59),vec2(155,63),vec2(162,68),vec2(181,84),
  vec2(173,92),vec2(163,98),vec2(152,104),vec2(139,108),vec2(124,110),vec2(99,110),
  vec2(85,110),vec2(73,110),vec2(70,109),vec2(69,107),vec2(68.8,105),vec2(69,103.2));
 float body=polygon(q,v,24),mask=fill(body);
 float light=clamp(.92-(q.y-60.0)/65.0-(q.x-100.0)/260.0,0,1);vec3 fur=mix(furDark,furLight,light);
 float belly=fill(psMouseOval(q,vec2(123,103),vec2(31,12)));fur=mix(fur,cream,belly*.55);
 float cheek=fill(psMouseOval(q,vec2(159,87),vec2(19,12)));fur=mix(fur,cream,cheek*.83);
 fur+=vec3(.08,.10,.04)*exp(-dot((q-vec2(109,77))/vec2(31,16),(q-vec2(109,77))/vec2(31,16)));
 col=mix(col,edge,fill(body-1));col=mix(col,fur,mask);col=mix(col,vec3(.90,.99,.58),line(body,.8)*mask*(1.0-smoothstep(69.0,106.0,q.y))*.60);
 // Sparse fur tufts and the haunch give volume without visual noise.
 float haunch=psMouseOval(q,vec2(94,93),vec2(17,15));col=mix(col,furDark,line(haunch,.65)*mask*(1.0-smoothstep(80.0,100.0,q.x))*.26);
 for(int i=0;i<5;i++){vec2 at=vec2(87+9*i,80+int(mod(float(i)*7.0,13.0)));float tuft=stroke(q,at,at+vec2(4,-2));col=mix(col,cream,fill(tuft-.55)*mask*.28);}
 // Eye, cheek, nose, smile and individual whiskers.
 float blink=1.0-smoothstep(.045,.10,abs(mod(t,4.7)-.15));float eye=psMouseOval(q,vec2(155,76),vec2(4.3,mix(4.5,.7,blink)));
 col=mix(col,vec3(.035,.075,.065),fill(eye));col=mix(col,vec3(1),fill(length(q-vec2(153.8,74.3))-1.25)*(1.0-blink));
 col=mix(col,edge,fill(stroke(q,vec2(149,68),vec2(157,69))-.9)*.8);
 col=mix(col,vec3(.95,.67,.35),fill(psMouseOval(q,vec2(166,88),vec2(5.5,3.5)))*mask*.34);
 col=mix(col,vec3(.48,.25,.18),fill(length(q-vec2(179,83))-4.2));col=mix(col,vec3(.98,.64,.45),fill(length(q-vec2(179,81.6))-2.4));
 col=mix(col,edge,fill(stroke(q,vec2(165,94),vec2(175,88))-.8)*.65);
 float whiskers=min(stroke(q,vec2(171,84),vec2(190,76)),min(stroke(q,vec2(173,87),vec2(194,89)),stroke(q,vec2(171,90),vec2(188,98))));col=mix(col,cream,fill(whiskers-.65)*.88);
 // A graphite fedora with a lime ribbon, brass pin and a shaped crown.
 float brim=psMouseOval(q,vec2(150,56),vec2(27,5));col=mix(col,vec3(.035,.075,.10),fill(brim));col=mix(col,vec3(.39,.57,.35),line(brim,.7)*.8);
 vec2 hat[24];hat[0]=vec2(134,52);hat[1]=vec2(140,31);hat[2]=vec2(156,28);hat[3]=vec2(165,51);
 float crown=polygon(q,hat,4);vec3 felt=mix(vec3(.035,.075,.10),vec3(.19,.29,.30),clamp((160.0-q.x)/30.0,0,1));col=mix(col,felt,fill(crown));col=mix(col,vec3(.46,.63,.42),line(crown,.7)*.75);
 float band=fill(box(q,vec2(150,48),vec2(17,3.2),.8))*fill(crown);col=mix(col,vec3(.72,.95,.27),band);col=mix(col,cream,fill(stroke(q,vec2(138,45),vec2(161,44))-.55)*band*.5);
 float dent=min(stroke(q,vec2(143,32),vec2(150,35)),stroke(q,vec2(150,35),vec2(156,31)));col=mix(col,vec3(.025,.05,.07),fill(dent-.8));
 col=mix(col,vec3(.98,.78,.35),fill(length(q-vec2(158,48))-1.7));return col;
}
