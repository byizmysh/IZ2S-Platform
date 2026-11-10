// IZ monogram: white geometric letters and the mascot's hat in brand lime.
// Keep the silhouette and dimensions in sync with assets/brand/izps-mark.svg.
vec3 psBrandMark(vec2 p,vec3 col,vec2 origin){
 vec2 q=p-origin;
 if(q.x< -2.0||q.x>60.0||q.y< -2.0||q.y>55.0)return col;
 float letterI=min(box(q,vec2(6.5,38),vec2(2.75,14),.6),min(box(q,vec2(6.5,26.75),vec2(6.5,2.75),.6),box(q,vec2(6.5,49.25),vec2(6.5,2.75),.6)));
 float letterZ=min(box(q,vec2(36,26.75),vec2(14,2.75),.6),box(q,vec2(36,49.25),vec2(14,2.75),.6));
 // Square diagonal joins keep the Z crisp at the native header size.
 vec2 diagonal[24];diagonal[0]=vec2(43,28);diagonal[1]=vec2(50,28);diagonal[2]=vec2(29,48);diagonal[3]=vec2(22,48);
 letterZ=min(letterZ,polygon(q,diagonal,4));col=mix(col,PS_WHITE,fill(min(letterI,letterZ)));
 vec2 h=q-vec2(38,14);h=vec2(.990216*h.x-.139543*h.y,.139543*h.x+.990216*h.y);
 vec2 crown[24];crown[0]=vec2(-11,0);crown[1]=vec2(-9,-14);crown[2]=vec2(-2,-11);crown[3]=vec2(6,-14);crown[4]=vec2(12,0);
 float felt=polygon(h,crown,5);col=mix(col,PS_LIME,fill(felt));
 col=mix(col,vec3(.035,.21,.36),fill(box(h,vec2(.5,-2),vec2(11.5,1.5),.3))*fill(felt));
 col=mix(col,PS_CYAN,fill(box(h,vec2(6,-2),vec2(1.4,1.3),.3))*fill(felt));
 col=mix(col,PS_LIME,fill(box(h,vec2(0,2),vec2(18,2),1.5)));
 return col;
}
