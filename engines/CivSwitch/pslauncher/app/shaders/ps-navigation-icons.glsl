// Native vector icons for the two shell cards; independent of package artwork.
vec3 psExchangeGlyph(vec2 p,vec3 col,vec2 center){
 vec2 q=p-center;
 float body=min(box(q,vec2(0,12),vec2(62,43),10),box(q,vec2(-30,-31),vec2(27,13),6));
 vec3 material=mix(vec3(.015,.38,.56),vec3(.03,.78,.91),clamp((55.0-q.y)/100.0,0,1));
 col=mix(col,material,fill(body));col=mix(col,vec3(.36,.91,1),line(body,1.2));
 float top=min(stroke(q,vec2(-28,-8),vec2(28,-8)),min(stroke(q,vec2(17,-19),vec2(28,-8)),stroke(q,vec2(28,-8),vec2(17,3))));
 float bottom=min(stroke(q,vec2(28,22),vec2(-28,22)),min(stroke(q,vec2(-17,11),vec2(-28,22)),stroke(q,vec2(-28,22),vec2(-17,33))));
 col=mix(col,PS_WHITE,fill(top-2.8));col=mix(col,PS_LIME,fill(bottom-2.8));return col;
}
vec3 psNewsGlyph(vec2 p,vec3 col,vec2 center){
 vec2 q=p-center;
 float back=box(q,vec2(6,-5),vec2(57,67),8);col=mix(col,vec3(.04,.70,.86),fill(back));
 float paper=box(q,vec2(-3,4),vec2(57,67),8);col=mix(col,vec3(.91,.96,1),fill(paper));col=mix(col,vec3(.60,.83,.98),line(paper,1.1));
 col=mix(col,vec3(.08,.37,.64),fill(box(q,vec2(-4,-37),vec2(38,6),2)));
 col=mix(col,vec3(.02,.70,.87),fill(box(q,vec2(-25,-5),vec2(17,18),3)));
 for(int i=0;i<3;i++)col=mix(col,vec3(.31,.50,.67),fill(box(q,vec2(20,-17+14*i),vec2(15,2),2)));
 for(int i=0;i<2;i++)col=mix(col,vec3(.31,.50,.67),fill(box(q,vec2(-4,34+13*i),vec2(38,2),2)));
 col=mix(col,PS_LIME,fill(length(q-vec2(47,-55))-14));
 vec2 star=q-vec2(47,-55);float sparkle=min(box(star,vec2(0),vec2(2,7),1),box(star,vec2(0),vec2(7,2),1));
 col=mix(col,vec3(.05,.27,.38),fill(sparkle));return col;
}
